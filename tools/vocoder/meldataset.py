# tools/vocoder/meldataset.py
# Pure PyTorch and standard library audio loader and log-mel extractor for LJSpeech.
# Zero dependency on external audio codecs; uses standard library 'wave' module.

import os
import wave
import random
import math
import numpy as np
import torch
import torch.utils.data
import torch.nn.functional as F


def load_wav_std(path):
    with wave.open(path, "rb") as w:
        sr = w.getframerate()
        n_frames = w.getnframes()
        raw = w.readframes(n_frames)
        data = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
        return torch.from_numpy(data).unsqueeze(0), sr


def hz_to_mel(hz):
    return 2595.0 * math.log10(1.0 + hz / 700.0)


def mel_to_hz(mel):
    return 700.0 * (10.0 ** (mel / 2595.0) - 1.0)


def build_mel_basis(sample_rate=16000, n_fft=512, n_mels=80, f_min=20.0, f_max=8000.0):
    n_freq = n_fft // 2 + 1
    weights = np.zeros((n_mels, n_freq), dtype=np.float32)

    min_mel = hz_to_mel(f_min)
    max_mel = hz_to_mel(f_max if f_max is not None else sample_rate / 2.0)
    mel_points = np.linspace(min_mel, max_mel, n_mels + 2)
    hz_points = mel_to_hz(mel_points)

    for m in range(n_mels):
        lower = hz_points[m]
        center = hz_points[m + 1]
        upper = hz_points[m + 2]
        for k in range(n_freq):
            freq = (sample_rate / 2.0) * k / (n_freq - 1)
            if freq >= lower and freq <= center and center > lower:
                weights[m, k] = (freq - lower) / (center - lower)
            elif freq > center and freq <= upper and upper > center:
                weights[m, k] = (upper - freq) / (upper - center)

    return torch.from_numpy(weights)


class LogMelSpectrogram(torch.nn.Module):
    def __init__(self, sample_rate=16000, n_fft=512, win_size=400, hop_size=160, n_mels=80, f_min=20.0, f_max=8000.0):
        super(LogMelSpectrogram, self).__init__()
        self.sample_rate = sample_rate
        self.n_fft = n_fft
        self.win_size = win_size
        self.hop_size = hop_size
        self.n_mels = n_mels
        self.register_buffer("window", torch.hann_window(win_size))
        self.register_buffer("mel_basis", build_mel_basis(sample_rate, n_fft, n_mels, f_min, f_max))

    def forward(self, y):
        # y: [B, 1, T] or [B, T]
        if y.dim() == 3:
            y = y.squeeze(1)

        # Pad symmetrically
        pad = int((self.n_fft - self.hop_size) / 2)
        y = F.pad(y.unsqueeze(1), (pad, pad), mode="reflect").squeeze(1)

        spec = torch.stft(
            y,
            n_fft=self.n_fft,
            hop_length=self.hop_size,
            win_length=self.win_size,
            window=self.window.to(y.device),
            center=False,
            pad_mode="reflect",
            normalized=False,
            onesided=True,
            return_complex=True
        )

        power_spec = spec.real.pow(2) + spec.imag.pow(2)
        mel_output = torch.matmul(self.mel_basis.to(y.device), power_spec)
        log_mel = torch.log(torch.clamp(mel_output, min=1e-10))
        return log_mel


class LJSpeechDataset(torch.utils.data.Dataset):
    def __init__(self, audio_files, segment_size=6400, sample_rate=16000, hop_size=160):
        self.audio_files = audio_files
        self.segment_size = segment_size
        self.sample_rate = sample_rate
        self.hop_size = hop_size
        self.mel_extractor = LogMelSpectrogram(sample_rate=sample_rate, hop_size=hop_size)
        self.cache = {}

    def __len__(self):
        return len(self.audio_files)

    def __getitem__(self, idx):
        if idx in self.cache:
            audio = self.cache[idx]
        else:
            filename = self.audio_files[idx]
            audio, sr = load_wav_std(filename)
            audio = audio.squeeze(0)

            # Resample to target sample rate (16000 Hz) if needed
            if sr != self.sample_rate:
                target_len = int(round(audio.shape[-1] * self.sample_rate / sr))
                audio = F.interpolate(audio.view(1, 1, -1), size=target_len, mode='linear', align_corners=False).view(-1)

            # Normalize audio amplitude
            max_amp = torch.max(torch.abs(audio))
            if max_amp > 0:
                audio = audio / max_amp * 0.95

            if len(self.cache) < 1000:
                self.cache[idx] = audio

        # Cut or pad audio segment
        if audio.size(0) >= self.segment_size:
            max_audio_start = audio.size(0) - self.segment_size
            audio_start = random.randint(0, max_audio_start)
            audio = audio[audio_start:audio_start + self.segment_size]
        else:
            audio = F.pad(audio, (0, self.segment_size - audio.size(0)), "constant")

        # Extract Mel spectrogram
        with torch.no_grad():
            mel = self.mel_extractor(audio.unsqueeze(0)).squeeze(0)

        return mel, audio.unsqueeze(0)
