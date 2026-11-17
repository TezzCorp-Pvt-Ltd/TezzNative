"""Bounded GGUF parsing and payload checks against tiny generated files."""
import argparse
import ctypes
import json
from pathlib import Path
import struct
import subprocess
import sys


def string(value):
    return struct.pack("<Q", len(value)) + value


def model(*, version=3, shape=(2,), offset=0, name=b"weights", metadata=(), alignment=32):
    header = b"GGUF" + struct.pack("<IQQ", version, 1, len(metadata)) + b"".join(metadata)
    header += string(name) + struct.pack("<I", len(shape))
    header += b"".join(struct.pack("<Q", value) for value in shape)
    header += struct.pack("<IQ", 0, offset)
    return header + b"\0" * (-len(header) % alignment) + struct.pack("<ff", 1.25, -2.5)


def fixtures():
    align = string(b"general.alignment") + struct.pack("<II", 4, 128)
    return {
        "valid": (model(), True),
        "alignment": (model(metadata=(align,), alignment=128), True),
        "truncated_header": (b"GGUF", False),
        "unsupported_version": (model(version=99), False),
        "truncated_tensor": (b"GGUF" + struct.pack("<IQQ", 3, 1, 0), False),
        "truncated_payload": (model()[:-1], False),
        "oversized_rank": (model(shape=(1,) * 9), False),
        "overflow_dimensions": (model(shape=(2**63, 2)), False),
        "zero_dimension": (model(shape=(0,)), False),
        "unaligned_offset": (model(offset=1), False),
        "out_of_file_offset": (model(offset=2**63), False),
        "long_name": (model(name=b"a" * 128), False),
        "unknown_metadata": (model(metadata=(string(b"test") + struct.pack("<I", 99),)), False),
        "zero_alignment": (model(metadata=(string(b"general.alignment") + struct.pack("<II", 4, 0),)), False),
        "huge_array": (model(metadata=(string(b"test") + struct.pack("<IIQ", 9, 0, 2**40),)), False),
    }


def case(library, folder, name):
    raw, valid = fixtures()[name]
    path = folder / (name + ".gguf")
    path.write_bytes(raw)
    lib = ctypes.CDLL(str(library))
    lib.tz_gguf_open.argtypes = [ctypes.c_char_p]
    lib.tz_gguf_open.restype = ctypes.c_int64
    lib.tz_gguf_close.argtypes = [ctypes.c_int64]
    lib.tz_gguf_close.restype = ctypes.c_int
    lib.tz_gguf_read_tensor_f32.argtypes = [ctypes.c_int64, ctypes.c_int64, ctypes.POINTER(ctypes.c_float)]
    lib.tz_gguf_read_tensor_f32.restype = ctypes.c_int
    handle = lib.tz_gguf_open(str(path).encode())
    if not valid:
        if handle:
            lib.tz_gguf_close(handle)
            raise AssertionError("malformed model was accepted: " + name)
        return
    if not handle:
        raise AssertionError("valid model was rejected: " + name)
    try:
        values = (ctypes.c_float * 2)()
        if lib.tz_gguf_read_tensor_f32(handle, 0, values) != 0 or list(values) != [1.25, -2.5]:
            raise AssertionError("tensor data mismatch: " + repr(list(values)))
    finally:
        lib.tz_gguf_close(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case")
    args = parser.parse_args()
    folder = args.out.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    library = args.library.resolve()
    if args.case:
        case(library, folder, args.case)
        return 0
    results = []
    for name in fixtures():
        try:
            run = subprocess.run([sys.executable, __file__, "--library", str(library),
                                  "--out", str(folder), "--case", name],
                                 capture_output=True, text=True, timeout=5)
            result = {"case": name, "status": "pass" if run.returncode == 0 else "fail",
                      "exit": run.returncode, "output": run.stdout + run.stderr}
        except subprocess.TimeoutExpired:
            result = {"case": name, "status": "fail", "output": "parser timed out"}
        results.append(result)
        print(result["status"].upper(), name, flush=True)
    (folder / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return int(any(item["status"] != "pass" for item in results))


if __name__ == "__main__":
    raise SystemExit(main())
