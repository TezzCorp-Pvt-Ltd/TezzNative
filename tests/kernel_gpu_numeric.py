"""Compatibility entry point for complete real-device CUDA verification.

Unlike the retired extracted-wrapper probe this compiles the entire CUDA
library. Missing hardware or toolkit is a failure, including without the
legacy --require-gpu flag. Supply --cuda-root explicitly.
"""
from pathlib import Path
import runpy
import sys

if __name__ == "__main__":
    sys.argv = [arg for arg in sys.argv if arg != "--require-gpu"]
    runpy.run_path(str(Path(__file__).parent/"regression/cuda/check_device.py"), run_name="__main__")
