"""Small numerical contracts for a source-built CPU kernel library.

Run: python tests/kernel_numeric.py --library /path/to/tzcpu.dll
Each case runs in an isolated process so an invalid native access is a failure.
"""
import argparse
import ctypes as c
import math
import os
from pathlib import Path
import subprocess
import struct
import sys


def run_case(library, case):
    if os.name == "nt":
        os.add_dll_directory("C:/msys64/mingw64/bin")
    lib = c.CDLL(str(Path(library).resolve()))
    i, d = c.c_int64, c.c_double

    def fn(name, types):
        f = getattr(lib, "tz_cpu_" + name)
        f.argtypes, f.restype = types, i
        return f

    def floats(*values):
        return (c.c_float * len(values))(*values)

    def ptr(value):
        return c.addressof(value)

    def close(actual, expected, tolerance=0.00002):
        assert len(actual) == len(expected)
        for got, want in zip(actual, expected):
            assert math.isfinite(got) and abs(got - want) <= tolerance * max(1, abs(want)), (got, want)

    if case == "stub_unavailable":
        assert lib.tz_cuda_device_count() == 0
        lib.tz_cuda_malloc.argtypes, lib.tz_cuda_malloc.restype = [i], i
        assert lib.tz_cuda_malloc(4) == 0
        lib.tz_gpu_fill_f32.argtypes, lib.tz_gpu_fill_f32.restype = [i, i, d], i
        assert lib.tz_gpu_fill_f32(0, 1, 1.0) != 0, "GPU stub reported successful computation"
        lib.tz_cuda_memcpy.argtypes, lib.tz_cuda_memcpy.restype = [i] * 4, i
        assert lib.tz_cuda_memcpy(0, 0, 4, 1) != 0
        assert lib.tz_cuda_sync() != 0
    elif case == "gguf_scale":
        values = floats(*([0.00001] * 32))
        block = (c.c_ubyte * 34)()
        quantize = lib.tz_gguf_quantize_q8_0
        quantize.argtypes, quantize.restype = [c.POINTER(c.c_float), c.c_void_p, i], c.c_int
        assert quantize(values, c.addressof(block), 32) == 0
        assert bytes(block[:2]) == struct.pack("<e", float(values[0]) / 127), "quantized scale lost subnormal precision"
    elif case == "half_precision":
        raw = (c.c_uint16 * 65536)(*range(65536))
        decoded = (c.c_float * 65536)()
        fn("f16_to_f32", [i] * 3)(ptr(raw), ptr(decoded), len(raw))
        for bits, actual in enumerate(decoded):
            expected = struct.unpack("<e", struct.pack("<H", bits))[0]
            if math.isnan(expected):
                assert math.isnan(actual), ("decode NaN", bits, actual)
            else:
                assert struct.pack("<f", actual) == struct.pack("<f", expected), ("decode", bits, actual, expected)
        raw32 = (c.c_uint32 * 65536)(*(index * 65537 for index in range(65536)))
        encoded = (c.c_uint16 * 65536)()
        fn("f32_to_f16", [i] * 3)(ptr(raw32), ptr(encoded), len(raw32))
        for bits, actual in zip(raw32, encoded):
            value = struct.unpack("<f", struct.pack("<I", bits))[0]
            if math.isnan(value):
                assert actual & 0x7C00 == 0x7C00 and actual & 1023, ("encode NaN", bits, actual)
            else:
                try:
                    expected = struct.unpack("<H", struct.pack("<e", value))[0]
                except OverflowError:
                    expected = ((bits >> 16) & 0x8000) | 0x7C00
                assert actual == expected, ("encode", bits, actual, expected)
    elif case == "cross_entropy_extreme":
        logits, labels, loss = floats(0, -40), (c.c_int * 1)(1), floats(0)
        fn("cross_entropy_f32", [i] * 5)(ptr(logits), ptr(labels), ptr(loss), 1, 2)
        close(loss, [40])
    elif case == "gemm_variants":
        a, b, out = floats(1, 2, 3, 4, 5, 6), floats(7, 8, 9, 10, 11, 12), floats(0, 0, 0, 0)
        fn("gemm_f32", [i] * 6 + [d, d])(ptr(a), ptr(b), ptr(out), 2, 3, 2, 1, 0)
        close(out, [58, 64, 139, 154])
        bt = floats(7, 9, 11, 8, 10, 12)
        fn("gemm_nt_f32", [i] * 6 + [d, d])(ptr(a), ptr(bt), ptr(out), 2, 3, 2, 1, 0)
        close(out, [58, 64, 139, 154])
        at = floats(1, 4, 2, 5, 3, 6)
        fn("gemm_tn_f32", [i] * 6 + [d, d])(ptr(at), ptr(b), ptr(out), 2, 3, 2, 1, 0)
        close(out, [58, 64, 139, 154])
    elif case in ("rmsnorm_weight_only", "rmsnorm_input_only"):
        x, w, gout, dx, dw = floats(1, 2), floats(1, 1), floats(1, 1), floats(0, 0), floats(0, 0)
        dxp = 0 if case == "rmsnorm_weight_only" else ptr(dx)
        dwp = 0 if case == "rmsnorm_input_only" else ptr(dw)
        fn("rmsnorm_bwd_f32", [i] * 8)(ptr(gout), ptr(x), ptr(w), dxp, dwp, 1, 2, 0)
        inv = 1 / math.sqrt(2.5 + 0.000001)
        if dxp:
            close(dx, [(1 - 0.6) * inv, (1 - 1.2) * inv])
        if dwp:
            close(dw, [inv, 2 * inv])
    elif case == "adam_two_steps":
        p, g, m, v = floats(1), floats(2), floats(0), floats(0)
        adam = fn("adam_f32", [i] * 4 + [d] * 6 + [i])
        for step in (1, 2):
            adam(ptr(p), ptr(g), ptr(m), ptr(v), 0.1, 0.9, 0.999, 1e-8, 0.9 ** step, 0.999 ** step, 1)
        close(p, [0.8])
    elif case == "gelu_gradient":
        values = [-2, -0.5, 0, 0.5, 2]
        x, gout, dx = floats(*values), floats(*([1] * 5)), floats(*([0] * 5))
        fn("gelu_bwd_f32", [i] * 4)(ptr(gout), ptr(x), ptr(dx), 5)
        def gelu(v):
            return 0.5 * v * (1 + math.tanh(math.sqrt(2 / math.pi) * (v + 0.044715 * v ** 3)))
        h = 0.0001
        close(dx, [(gelu(v + h) - gelu(v - h)) / (2 * h) for v in values])
    else:
        raise AssertionError("unknown case " + case)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True)
    parser.add_argument("--case")
    parser.add_argument("--stub", action="store_true")
    args = parser.parse_args()
    if args.case:
        run_case(args.library, args.case)
        return 0
    cases = ["half_precision", "gguf_scale", "cross_entropy_extreme", "gemm_variants", "rmsnorm_weight_only", "rmsnorm_input_only", "adam_two_steps", "gelu_gradient"]
    if args.stub:
        cases = ["stub_unavailable"]
    failures = 0
    for case in cases:
        result = subprocess.run([sys.executable, __file__, "--library", args.library, "--case", case], capture_output=True, text=True, timeout=15)
        print(("PASS " if result.returncode == 0 else "FAIL ") + case)
        if result.returncode:
            failures += 1
            print(result.stdout + result.stderr or "native exit " + str(result.returncode))
    print(f"KERNEL_SUMMARY pass={len(cases) - failures} fail={failures}")
    return int(failures != 0)


if __name__ == "__main__":
    sys.exit(main())
