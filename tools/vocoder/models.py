# tools/vocoder/models.py
# High-Fidelity Lightweight Neural Vocoder (HiFi-GAN V2 architecture)
# Converts 80-channel log-mel spectrograms directly into clean, human-sounding PCM waveforms.

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.nn.utils import weight_norm, remove_weight_norm


def init_weights(m, mean=0.0, std=0.01):
    classname = m.__class__.__name__
    if classname.find("Conv") != -1:
        m.weight.data.normal_(mean, std)


def get_padding(kernel_size, dilation=1):
    return int((kernel_size * dilation - dilation) / 2)


class ResBlock(nn.Module):
    def __init__(self, channels, kernel_size=3, dilations=(1, 3, 5)):
        super(ResBlock, self).__init__()
        self.convs1 = nn.ModuleList([
            weight_norm(nn.Conv1d(channels, channels, kernel_size, 1,
                                  dilation=d, padding=get_padding(kernel_size, d)))
            for d in dilations
        ])
        self.convs1.apply(init_weights)

        self.convs2 = nn.ModuleList([
            weight_norm(nn.Conv1d(channels, channels, kernel_size, 1,
                                  dilation=1, padding=get_padding(kernel_size, 1)))
            for _ in dilations
        ])
        self.convs2.apply(init_weights)

    def forward(self, x):
        for c1, c2 in zip(self.convs1, self.convs2):
            xt = F.leaky_relu(x, 0.1)
            xt = c1(xt)
            xt = F.leaky_relu(xt, 0.1)
            xt = c2(xt)
            x = xt + x
        return x

    def remove_weight_norm(self):
        for c in self.convs1:
            remove_weight_norm(c)
        for c in self.convs2:
            remove_weight_norm(c)


class Generator(nn.Module):
    def __init__(self, in_channels=80, upsample_initial_channel=128,
                 upsample_rates=(8, 5, 2, 2), upsample_kernel_sizes=(16, 11, 4, 4),
                 resblock_kernel_sizes=(3, 7), resblock_dilation_sizes=((1, 3, 5), (1, 3, 5))):
        super(Generator, self).__init__()
        self.num_kernels = len(resblock_kernel_sizes)
        self.num_upsamples = len(upsample_rates)

        self.conv_pre = weight_norm(nn.Conv1d(in_channels, upsample_initial_channel, 7, 1, padding=3))

        self.ups = nn.ModuleList()
        for i, (u, k) in enumerate(zip(upsample_rates, upsample_kernel_sizes)):
            ch_in = upsample_initial_channel // (2 ** i)
            ch_out = upsample_initial_channel // (2 ** (i + 1))
            self.ups.append(weight_norm(
                nn.ConvTranspose1d(ch_in, ch_out, k, u, padding=(k - u) // 2)
            ))

        self.resblocks = nn.ModuleList()
        for i in range(len(self.ups)):
            ch = upsample_initial_channel // (2 ** (i + 1))
            for j, (k, d) in enumerate(zip(resblock_kernel_sizes, resblock_dilation_sizes)):
                self.resblocks.append(ResBlock(ch, k, d))

        ch_last = upsample_initial_channel // (2 ** len(upsample_rates))
        self.conv_post = weight_norm(nn.Conv1d(ch_last, 1, 7, 1, padding=3))
        self.ups.apply(init_weights)
        self.conv_post.apply(init_weights)

    def forward(self, x):
        x = self.conv_pre(x)
        for i in range(self.num_upsamples):
            x = F.leaky_relu(x, 0.1)
            x = self.ups[i](x)
            xs = None
            for j in range(self.num_kernels):
                if xs is None:
                    xs = self.resblocks[i * self.num_kernels + j](x)
                else:
                    xs += self.resblocks[i * self.num_kernels + j](x)
            x = xs / self.num_kernels
        x = F.leaky_relu(x)
        x = self.conv_post(x)
        x = torch.tanh(x)
        return x

    def remove_weight_norm(self):
        remove_weight_norm(self.conv_pre)
        for u in self.ups:
            remove_weight_norm(u)
        for r in self.resblocks:
            r.remove_weight_norm()
        remove_weight_norm(self.conv_post)


class DiscriminatorP(nn.Module):
    def __init__(self, period, kernel_size=5, stride=3):
        super(DiscriminatorP, self).__init__()
        self.period = period
        self.convs = nn.ModuleList([
            weight_norm(nn.Conv2d(1, 32, (kernel_size, 1), (stride, 1), padding=(get_padding(kernel_size, 1), 0))),
            weight_norm(nn.Conv2d(32, 128, (kernel_size, 1), (stride, 1), padding=(get_padding(kernel_size, 1), 0))),
            weight_norm(nn.Conv2d(128, 256, (kernel_size, 1), (stride, 1), padding=(get_padding(kernel_size, 1), 0))),
            weight_norm(nn.Conv2d(256, 512, (kernel_size, 1), (stride, 1), padding=(get_padding(kernel_size, 1), 0))),
            weight_norm(nn.Conv2d(512, 512, (kernel_size, 1), 1, padding=(2, 0))),
        ])
        self.conv_post = weight_norm(nn.Conv2d(512, 1, (3, 1), 1, padding=(1, 0)))

    def forward(self, x):
        fmap = []
        b, c, t = x.shape
        if t % self.period != 0:
            n_pad = self.period - (t % self.period)
            x = F.pad(x, (0, n_pad), "reflect")
            t = t + n_pad
        x = x.view(b, c, t // self.period, self.period)

        for l in self.convs:
            x = l(x)
            x = F.leaky_relu(x, 0.1)
            fmap.append(x)
        x = self.conv_post(x)
        fmap.append(x)
        x = torch.flatten(x, 1, -1)
        return x, fmap


class MultiPeriodDiscriminator(nn.Module):
    def __init__(self, periods=(2, 3, 5, 7, 11)):
        super(MultiPeriodDiscriminator, self).__init__()
        self.discriminators = nn.ModuleList([
            DiscriminatorP(p) for p in periods
        ])

    def forward(self, y, y_hat):
        y_d_rs, y_d_gs, fmap_rs, fmap_gs = [], [], [], []
        for d in self.discriminators:
            y_d_r, fmap_r = d(y)
            y_d_g, fmap_g = d(y_hat)
            y_d_rs.append(y_d_r)
            fmap_rs.append(fmap_r)
            y_d_gs.append(y_d_g)
            fmap_gs.append(fmap_g)
        return y_d_rs, y_d_gs, fmap_rs, fmap_gs


class DiscriminatorS(nn.Module):
    def __init__(self):
        super(DiscriminatorS, self).__init__()
        self.convs = nn.ModuleList([
            weight_norm(nn.Conv1d(1, 64, 15, 1, padding=7)),
            weight_norm(nn.Conv1d(64, 128, 41, 4, groups=4, padding=20)),
            weight_norm(nn.Conv1d(128, 256, 41, 4, groups=16, padding=20)),
            weight_norm(nn.Conv1d(256, 512, 41, 4, groups=64, padding=20)),
            weight_norm(nn.Conv1d(512, 512, 41, 4, groups=256, padding=20)),
            weight_norm(nn.Conv1d(512, 512, 5, 1, padding=2)),
        ])
        self.conv_post = weight_norm(nn.Conv1d(512, 1, 3, 1, padding=1))

    def forward(self, x):
        fmap = []
        for l in self.convs:
            x = l(x)
            x = F.leaky_relu(x, 0.1)
            fmap.append(x)
        x = self.conv_post(x)
        fmap.append(x)
        x = torch.flatten(x, 1, -1)
        return x, fmap


class MultiScaleDiscriminator(nn.Module):
    def __init__(self, num_scales=3):
        super(MultiScaleDiscriminator, self).__init__()
        self.discriminators = nn.ModuleList([
            DiscriminatorS() for _ in range(num_scales)
        ])
        self.meanpools = nn.ModuleList([
            nn.AvgPool1d(4, 2, padding=2) for _ in range(max(0, num_scales - 1))
        ])

    def forward(self, y, y_hat):
        y_d_rs, y_d_gs, fmap_rs, fmap_gs = [], [], [], []
        for i, d in enumerate(self.discriminators):
            if i != 0:
                y = self.meanpools[i - 1](y)
                y_hat = self.meanpools[i - 1](y_hat)
            y_d_r, fmap_r = d(y)
            y_d_g, fmap_g = d(y_hat)
            y_d_rs.append(y_d_r)
            fmap_rs.append(fmap_r)
            y_d_gs.append(y_d_g)
            fmap_gs.append(fmap_g)
        return y_d_rs, y_d_gs, fmap_rs, fmap_gs


def feature_loss(fmap_r, fmap_g):
    loss = 0
    for dr, dg in zip(fmap_r, fmap_g):
        for rl, gl in zip(dr, dg):
            loss += torch.mean(torch.abs(rl - gl))
    return loss * 2


def discriminator_loss(disc_real_outputs, disc_generated_outputs):
    loss = 0
    r_losses, g_losses = [], []
    for dr, dg in zip(disc_real_outputs, disc_generated_outputs):
        r_loss = torch.mean((1 - dr) ** 2)
        g_loss = torch.mean(dg ** 2)
        loss += (r_loss + g_loss)
        r_losses.append(r_loss.item())
        g_losses.append(g_loss.item())
    return loss, r_losses, g_losses


def generator_loss(disc_outputs):
    loss = 0
    gen_losses = []
    for dg in disc_outputs:
        l = torch.mean((1 - dg) ** 2)
        loss += l
        gen_losses.append(l.item())
    return loss, gen_losses
