# TezzNative AI Training & Deployment Guide (Desktop Setup)

This document provides a comprehensive guide for setting up, training, and testing the TezzNative Speech AI system (HiFi-GAN Neural Vocoder and Acoustic Model) on a dedicated desktop workstation equipped with **12 GB VRAM (CUDA)** and **16 GB System RAM**.

---

## 1. Desktop Workstation Specifications & Optimizations

| Resource | Specification | Optimization for TezzNative Training |
| :--- | :--- | :--- |
| **GPU** | NVIDIA CUDA with 12 GB VRAM | Batch size 16–24, PyTorch Automatic Mixed Precision (AMP `fp16`), `cudnn.benchmark=True` |
| **System RAM** | 16 GB DDR4/DDR5 | Multi-worker streaming data loader (`num_workers=4`, `pin_memory=True`), avoiding RAM swapping |
| **Storage** | NVMe / SSD recommended | Fast I/O for 13,100 WAV files (~2.6 GB uncompressed) |
| **OS** | Windows 10/11 64-bit | Native PowerShell / Git Bash / Command Prompt |

---

## 2. Desktop Environment Setup

### 2.1 Git Clone / Repository Transfer
Transfer the repository to your desktop machine:
```powershell
git clone <your-repo-url> TezzNative
cd TezzNative
```
*Note: If copying manually over USB/network, you can omit `tezzmodel/` to save transfer time and let the automated downloader fetch LJSpeech fresh, or copy `tezzmodel/` to reuse existing files.*

### 2.2 Python & PyTorch CUDA Setup
Ensure Python 3.10–3.12 is installed, then set up a virtual environment:
```powershell
# Create virtual environment
python -m venv .venv
.\.venv\Scripts\Activate.ps1

# Upgrade pip
python -m pip install --upgrade pip

# Install PyTorch with CUDA 12.4 support
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu124

# Install training utilities
pip install numpy scipy tqdm soundfile
```

### 2.3 Verify CUDA Availability
Run this quick command to verify your 12 GB GPU is recognized:
```powershell
python -c "import torch; print('CUDA Available:', torch.cuda.is_available()); print('Device:', torch.cuda.get_device_name(0)); print('VRAM (GB):', round(torch.cuda.get_device_properties(0).total_memory / 1e9, 2))"
```
*Expected Output:* `CUDA Available: True`, Device name (e.g., `RTX 3060 / 4070`), and ~`12.0 GB` VRAM.

---

## 3. Dedicated Training Suite Architecture (`ai_training/`)

The training system is organized into modular scripts under `ai_training/`:

```text
ai_training/
├── README.md               # Quick-start instructions
├── config.py               # Tuned hyper-parameters for 12GB VRAM & 16kHz audio
├── dataset.py              # Download, extract, verify & cache LJSpeech-1.1
├── models.py               # HiFi-GAN V2 Generator & Multi-Period/Scale Discriminators
├── train_vocoder.py        # HiFi-GAN training loop (AMP, 0–100% progress, ETA)
├── export_native.py        # Exporter converting .pt checkpoints -> vocoder_weights.bin
└── run_all.py              # Master pipeline script
```

---

## 4. Step-by-Step Training Workflow (0% to 100%)

### Step 1: Automatic Dataset Setup (`dataset.py`)
Run the dataset preparation script:
```powershell
python ai_training/dataset.py --data-dir tezzmodel/LJSpeech-1.1
```
- **Local Check:** Checks if `tezzmodel/LJSpeech-1.1/wavs` already exists.
- **Auto-Download:** If missing, streams `https://data.keithito.com/data/speech/LJSpeech-1.1.tar.bz2` (2.6 GB) with progress tracking.
- **Auto-Extract:** Unpacks archives and verifies all 13,100 WAV audio files.

### Step 2: Launch Neural Vocoder Training (`train_vocoder.py`)
Launch the GPU training with mixed precision:
```powershell
python ai_training/train_vocoder.py --batch-size 16 --amp --max-steps 50000 --save-interval 2500
```

#### Training Milestones (0% to 100%):
- **0% – 5% (Initial Stabilization):**
  - Generator learns fundamental frame-level upsampling ($160\times$).
  - Mel loss drops rapidly from $>2.5$ down to $\sim 0.8$.
- **5% – 25% (Harmonics & Formants):**
  - Discriminators (MPD & MSD) penalize buzzy/metallic artifacts.
  - Vowels and voice resonance become distinct.
- **25% – 75% (Fine Detail & Clarity):**
  - Multi-receptive field ResBlocks sharpen consonants ($s, t, p, k$) and high frequencies (up to 8 kHz).
  - Mel loss stabilizes around $0.25$ – $0.35$.
- **75% – 100% (High-Fidelity Studio Convergence):**
  - Background noise drops below perceptual threshold.
  - Native weight exporter automatically generates `vocoder_weights.bin`.

### Step 3: Export Native Weights (`export_native.py`)
Checkpoints are automatically exported during training, or can be manually converted anytime:
```powershell
python ai_training/export_native.py --checkpoint ai_training/checkpoints/generator_step_50000.pt --output vocoder_weights.bin
```

---

## 5. Native TezzNative Testing on Desktop

Once `vocoder_weights.bin` is produced, test it directly with the native TezzNative engine:

### 5.1 Place Weights
Copy `vocoder_weights.bin` to the working directory or `build/speech/`:
```powershell
Copy-Item vocoder_weights.bin build\speech\vocoder_weights.bin
```

### 5.2 Execute Native Speech Synthesis
Run the native test script:
```powershell
.\bin\tezzc.exe run test_speech_rohit.tn
```

### 5.3 Listen to Generated Audio
Play the resulting `.wav` file on your desktop:
```powershell
powershell -c "(New-Object Media.SoundPlayer 'hi_rohit_kumar_mondal.wav').PlaySync()"
```

---

## 6. Performance & Monitoring Tips for 12 GB VRAM

1. **VRAM Headroom:**
   - With `batch_size=16` and AMP enabled, VRAM usage stays around 7.5–8.5 GB, preventing out-of-memory errors.
   - If you want even faster training, you can increase to `--batch-size 20` or `--batch-size 24`.
2. **Monitoring GPU Usage:**
   - Open a separate PowerShell window and run:
     ```powershell
     nvidia-smi -l 2
     ```
   - Target GPU Compute Utilization: **85% – 98%**.
3. **Resuming Training:**
   - If interrupted, simply pass `--resume` with the latest checkpoint:
     ```powershell
     python ai_training/train_vocoder.py --resume ai_training/checkpoints/generator_latest.pt
     ```
