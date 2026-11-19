# tools/vocoder/test_synth.py
# Verification tool to test audio synthesis directly from trained Generator checkpoint.

import os
import wave
import struct
import numpy as np
import torch
import torch.nn.functional as F

from models import Generator
from meldataset import load_wav_std, LogMelSpectrogram


def test_synthesis(checkpoint_path="tools/vocoder/checkpoints/generator_latest.pt",
                   wav_path="tezzmodel/LJSpeech-1.1/wavs/LJ001-0001.wav",
                   output_wav="tools/vocoder/test_reconstructed.wav"):
    print(f">> Testing synthesis with Checkpoint: {checkpoint_path}")
    print(f">> Source audio: {wav_path}")

    # Load audio
    audio, sr = load_wav_std(wav_path)
    audio = audio.squeeze(0)

    # Resample to 16000 Hz if needed
    if sr != 16000:
        target_len = int(round(audio.shape[-1] * 16000 / sr))
        audio = F.interpolate(audio.view(1, 1, -1), size=target_len, mode='linear', align_corners=False).view(-1)

    # Normalize
    max_amp = torch.max(torch.abs(audio))
    if max_amp > 0:
        audio = audio / max_amp * 0.95

    # Extract 16kHz log-mel spectrogram
    mel_extractor = LogMelSpectrogram(sample_rate=16000, hop_size=160)
    with torch.no_grad():
        mel = mel_extractor(audio.unsqueeze(0))

    print(f"   Mel Spectrogram Shape: {mel.shape} ({mel.shape[-1]} frames = {mel.shape[-1]*160} audio samples)")

    # Load Generator
    generator = Generator()
    state_dict = torch.load(checkpoint_path, map_location="cpu")
    generator.load_state_dict(state_dict)
    generator.remove_weight_norm()
    generator.eval()

    with torch.no_grad():
        y_g = generator(mel)

    pcm_data = y_g.squeeze().numpy()
    pcm_int16 = np.clip(pcm_data * 32767.0, -32768, 32767).astype(np.int16)

    out_dir = os.path.dirname(output_wav)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with wave.open(output_wav, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(pcm_int16.tobytes())

    print(f">> [OK] Generated WAV: {output_wav}")
    print(f"   Sample Rate : 16000 Hz")
    print(f"   Samples     : {len(pcm_int16)}")
    print(f"   Duration    : {len(pcm_int16)/16000.0:.2f} s")
    print(f"   Max Amp     : {np.max(np.abs(pcm_data)):.4f}")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", default="tools/vocoder/checkpoints/generator_latest.pt")
    parser.add_argument("--wav", default="tezzmodel/LJSpeech-1.1/wavs/LJ001-0001.wav")
    parser.add_argument("--output", default="tools/vocoder/test_reconstructed.wav")
    args = parser.parse_args()
    test_synthesis(args.checkpoint, args.wav, args.output)
