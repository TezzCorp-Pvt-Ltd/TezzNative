# TezzNative Neural Vocoder (HiFi-GAN V2)

This directory contains the Python training pipeline, model architecture, and binary weight exporter for the TezzNative C runtime neural vocoder.

## Architecture

- **Generator:** HiFi-GAN V2 architecture (~556k parameters)
  - 80-channel log-mel spectrogram input (16 kHz audio, 160-sample hop length, 400-sample window)
  - Upsampling: 4 stages with strides `[8, 5, 2, 2]` ($8 \times 5 \times 2 \times 2 = 160\times$)
  - Multi-Receptive Field (MRF) ResBlocks with dilations `[1, 3, 5]`
- **Discriminators:**
  - Multi-Period Discriminator (MPD) with periods `[2, 3, 5, 7, 11]`
  - Multi-Scale Discriminator (MSD) across 3 temporal scales
- **Runtime Inference:** Implemented in pure C (`TezzCorp_WebSites/tn_tezzcorp_com/download/sdk/runtime/speech/neural_vocoder.c`) using ping-pong buffers and LeakyReLU activations.

## Files

- `models.py`: PyTorch definitions for Generator, MPD, and MSD.
- `meldataset.py`: Mel-spectrogram extraction and in-memory audio dataset loader.
- `train.py`: GPU training script with automated native weight export on checkpoints.
- `export_native.py`: Exporter converting PyTorch `.pt` state dict into raw float32 `vocoder_weights.bin` for C inference.
- `test_synth.py`: Validation script for mel-spectrogram inference.
