#!/bin/sh
# Windows Python + MSVC developer environment; output is a DLL, never ELF.
set -eu
exec python "$(dirname "$0")/build_tzgpu.py" "$@"
