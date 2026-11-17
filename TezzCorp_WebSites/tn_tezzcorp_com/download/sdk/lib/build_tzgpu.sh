#!/bin/sh
# Linux/WSL: supply --cuda-root, --arch and --out; never use Windows nvcc for ELF.
set -eu
exec python3 "$(dirname "$0")/build_tzgpu.py" "$@"
