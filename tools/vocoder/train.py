# tools/vocoder/train.py
# High-Efficiency Neural Vocoder Training Pipeline for NVIDIA GeForce GTX 1650 (CUDA)
# Trains on LJSpeech-1.1 to generate clean, human-quality, noise-free speech waveforms.

import os
import glob
import time
import argparse
import itertools
import numpy as np
import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

from models import Generator, MultiPeriodDiscriminator, MultiScaleDiscriminator, feature_loss, generator_loss, discriminator_loss
from meldataset import LJSpeechDataset, LogMelSpectrogram
from export_native import export_binary


def train(args):
    torch.manual_seed(42)
    if os.cpu_count():
        torch.set_num_threads(os.cpu_count())
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f">> Training on Device: {device}")
    if torch.cuda.is_available():
        print(f"   GPU: {torch.cuda.get_device_name(0)}")
        print(f"   VRAM Total: {torch.cuda.get_device_properties(0).total_memory / (1024**3):.2f} GB")

    checkpoint_dir = os.path.join(args.output_dir, "checkpoints")
    os.makedirs(checkpoint_dir, exist_ok=True)

    # 1. Gather audio files from LJSpeech
    dataset_wav_pattern = os.path.join(args.dataset_dir, "wavs", "*.wav")
    audio_files = sorted(glob.glob(dataset_wav_pattern))
    if len(audio_files) == 0:
        raise FileNotFoundError(f"No WAV files found at: {dataset_wav_pattern}")

    print(f">> Found {len(audio_files)} WAV files in LJSpeech-1.1.")
    if args.max_files > 0:
        audio_files = audio_files[:args.max_files]
        print(f"   Using {len(audio_files)} files for this training run.")

    # 2. Dataset & DataLoader
    dataset = LJSpeechDataset(audio_files, segment_size=args.segment_size, sample_rate=args.sample_rate)
    loader = DataLoader(dataset, batch_size=args.batch_size, shuffle=True, drop_last=True, pin_memory=(device.type == "cuda"))

    # 3. Models
    generator = Generator().to(device)
    if args.fast_d:
        mpd = MultiPeriodDiscriminator(periods=(2, 3, 5)).to(device)
        msd = MultiScaleDiscriminator(num_scales=1).to(device)
    else:
        mpd = MultiPeriodDiscriminator().to(device)
        msd = MultiScaleDiscriminator().to(device)
    mel_fn = LogMelSpectrogram(sample_rate=args.sample_rate).to(device)

    if args.resume and os.path.isfile(args.resume):
        print(f">> Resuming Generator from: {args.resume}")
        generator.load_state_dict(torch.load(args.resume, map_location=device))

    # 4. Optimizers & Schedulers
    optim_g = torch.optim.AdamW(generator.parameters(), lr=args.learning_rate, betas=(0.8, 0.99))
    optim_d = torch.optim.AdamW(itertools.chain(msd.parameters(), mpd.parameters()), lr=args.learning_rate, betas=(0.8, 0.99))

    scheduler_g = torch.optim.lr_scheduler.ExponentialLR(optim_g, gamma=0.999)
    scheduler_d = torch.optim.lr_scheduler.ExponentialLR(optim_d, gamma=0.999)

    generator.train()
    mpd.train()
    msd.train()

    print(f">> Generator Parameters: {sum(p.numel() for p in generator.parameters()):,}")
    print(f">> Multi-Period Discriminator: {sum(p.numel() for p in mpd.parameters()):,}")
    print(f">> Multi-Scale Discriminator : {sum(p.numel() for p in msd.parameters()):,}")
    print(f">> Starting Training: {args.epochs} Epochs, Batch Size: {args.batch_size}...")

    step = args.start_step
    start_time = time.time()
    done = False

    for epoch in range(1, args.epochs + 1):
        if done:
            break
        for batch_idx, (mel, y) in enumerate(loader):
            step += 1
            mel = mel.to(device)
            y = y.to(device)

            # ── 1. Train Generator ─────────────────────────────
            optim_g.zero_grad()
            y_g_hat = generator(mel)

            # Compute Mel Spectrogram Loss
            y_g_hat_mel = mel_fn(y_g_hat)
            loss_mel = F.l1_loss(mel, y_g_hat_mel) * 45.0

            # Multi-Period Discriminator Adversarial Loss
            y_df_hat_r, y_df_hat_g, fmap_f_r, fmap_f_g = mpd(y, y_g_hat)
            loss_fm_f = feature_loss(fmap_f_r, fmap_f_g)
            loss_gen_f, _ = generator_loss(y_df_hat_g)

            # Multi-Scale Discriminator Adversarial Loss
            y_ds_hat_r, y_ds_hat_g, fmap_s_r, fmap_s_g = msd(y, y_g_hat)
            loss_fm_s = feature_loss(fmap_s_r, fmap_s_g)
            loss_gen_s, _ = generator_loss(y_ds_hat_g)

            loss_gen_all = loss_gen_s + loss_gen_f + loss_fm_s + loss_fm_f + loss_mel
            loss_gen_all.backward()
            optim_g.step()

            # ── 2. Train Discriminators ─────────────────────────
            optim_d.zero_grad()

            # MPD
            y_df_r, y_df_g, _, _ = mpd(y, y_g_hat.detach())
            loss_disc_f, _, _ = discriminator_loss(y_df_r, y_df_g)

            # MSD
            y_ds_r, y_ds_g, _, _ = msd(y, y_g_hat.detach())
            loss_disc_s, _, _ = discriminator_loss(y_ds_r, y_ds_g)

            loss_disc_all = loss_disc_s + loss_disc_f
            loss_disc_all.backward()
            optim_d.step()

            if step % args.log_interval == 0:
                elapsed = time.time() - start_time
                print(f"Epoch [{epoch}/{args.epochs}] Step {step} | Loss G: {loss_gen_all.item():.4f} (Mel: {loss_mel.item():.4f}) | Loss D: {loss_disc_all.item():.4f} | {elapsed:.1f}s")

            if step % args.save_interval == 0:
                ckpt_path = os.path.join(checkpoint_dir, f"generator_step_{step}.pt")
                torch.save(generator.state_dict(), ckpt_path)
                torch.save(generator.state_dict(), os.path.join(checkpoint_dir, "generator_latest.pt"))
                print(f"   [SAVED CHECKPOINT]: {ckpt_path}")
                # Auto-export native weights
                export_binary(ckpt_path, os.path.join(checkpoint_dir, "vocoder_weights.bin"))
                export_binary(ckpt_path, "build/speech/vocoder_weights.bin")

            if args.max_steps > 0 and step >= args.max_steps:
                print(f"\n>> Reached target max steps: {step}")
                done = True
                break

        scheduler_g.step()
        scheduler_d.step()

    # Final save
    final_path = os.path.join(checkpoint_dir, "generator_final.pt")
    torch.save(generator.state_dict(), final_path)
    torch.save(generator.state_dict(), os.path.join(checkpoint_dir, "generator_latest.pt"))
    print(f"\n>> [TRAINING COMPLETE] Final Checkpoint Saved: {final_path}")
    export_binary(final_path, os.path.join(checkpoint_dir, "vocoder_weights.bin"))
    export_binary(final_path, "build/speech/vocoder_weights.bin")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset-dir", default="tezzmodel/LJSpeech-1.1", help="Path to LJSpeech dataset directory")
    parser.add_argument("--output-dir", default="tools/vocoder", help="Output directory for checkpoints")
    parser.add_argument("--epochs", type=int, default=10, help="Number of training epochs")
    parser.add_argument("--batch-size", type=int, default=6, help="Batch size (6 fits safely in 4 GB GTX 1650 VRAM)")
    parser.add_argument("--learning-rate", type=float, default=0.0002, help="AdamW learning rate")
    parser.add_argument("--segment-size", type=int, default=6400, help="Audio crop segment size (e.g. 40 frames * 160 = 6400)")
    parser.add_argument("--sample-rate", type=int, default=16000, help="Target audio sample rate (16000 Hz)")
    parser.add_argument("--resume", type=str, default="", help="Path to checkpoint to resume Generator from")
    parser.add_argument("--start-step", type=int, default=0, help="Initial step count when resuming")
    parser.add_argument("--max-steps", type=int, default=0, help="Stop training when this step is reached")
    parser.add_argument("--max-files", type=int, default=0, help="Max files to use (0 = all files)")
    parser.add_argument("--log-interval", type=int, default=10, help="Logging step interval")
    parser.add_argument("--fast-d", action="store_true", help="Use lightweight discriminators for 4x faster CPU training")
    parser.add_argument("--save-interval", type=int, default=100, help="Checkpoint save step interval")
    args = parser.parse_args()
    train(args)
