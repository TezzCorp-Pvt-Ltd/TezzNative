"""Compile emitted C and the CUDA host arithmetic lane against a Python oracle.

The CUDA shim exercises portable arithmetic expressions, not a GPU compiler.
"""
import argparse
import json
import os
import random
import subprocess
from pathlib import Path


def signed(value):
    value &= (1 << 64) - 1
    return value - (1 << 64) if value >= 1 << 63 else value


def expected(operation, a, b):
    if operation == 0: return signed(a + b)
    if operation == 1: return signed(a - b)
    if operation == 2: return signed(a * b)
    if operation in (3, 4):
        if not b or (a == -(1 << 63) and b == -1): return None
        quotient = abs(a) // abs(b)
        if (a < 0) != (b < 0): quotient = -quotient
        return quotient if operation == 3 else a - quotient * b
    if operation == 5: return signed(a & b)
    if operation == 6: return signed(a | b)
    if operation == 7: return signed(a ^ b)
    if operation == 8: return signed(a << (b & 63))
    if operation == 9: return a >> (b & 63)
    if operation == 10: return signed(a + (1 << 63) - 1)
    if operation == 11: return -(1 << 63)
    raise ValueError(operation)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--out", default="build/verification/integer-codegen")
    args = parser.parse_args()
    if os.name != "nt":
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    root = Path(__file__).resolve().parents[3]
    sdk = root / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
    output = Path(args.out).resolve()
    output.mkdir(parents=True, exist_ok=True)
    emitter = output / ("emitter.exe" if os.name == "nt" else "emitter")
    sources = [Path(__file__).with_name("emit_integer_oracle.c"),
               *(sdk / "src" / name for name in ("ir_codegen_c.c", "ir_codegen_cuda.c", "types.c", "util.c"))]

    def run(command):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError((command, result.returncode, result.stdout, result.stderr))
        return result

    run([args.cc, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-I" + str(sdk / "include"), *sources, "-o", emitter])
    rng = random.Random(37)
    pairs = [(a, b) for a in (-(1 << 63), -17, -1, 0, 1, 17, (1 << 63) - 1)
             for b in (-65, -64, -1, 0, 1, 63, 64, 65)]
    pairs += [(signed(rng.getrandbits(64)), signed(rng.getrandbits(64))) for _ in range(24)]
    records = []
    for lane, compiler, language in (("c", args.cc, "c"), ("cuda_host", args.cxx, "c++")):
        source = output / (lane + (".c" if lane == "c" else ".cu"))
        executable = output / (lane + (".exe" if os.name == "nt" else ".out"))
        run([emitter, "c" if lane == "c" else "cuda", source])
        run([compiler, "-x", language, "-O2", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
             "-fno-sanitize-recover=undefined", "-I" + str(Path(__file__).with_name("support")), source, "-o", executable])
        checks = 0
        for operation in range(12):
            for a, b in pairs:
                answer = expected(operation, a, b)
                result = subprocess.run([str(executable), str(operation), str(a), str(b)], capture_output=True, text=True, timeout=5)
                if answer is None:
                    assert result.returncode != 0 and "runtime error:" not in result.stderr, (lane, operation, a, b, result)
                else:
                    assert result.returncode == 0 and result.stdout.strip() == str(answer) and not result.stderr, (lane, operation, a, b, answer, result)
                checks += 1
        records.append({"lane": lane, "compiler": compiler, "checks": checks, "status": "pass"})
        print("integer_codegen_pass", lane, checks, flush=True)
    (output / "results.json").write_text(json.dumps({"results": records, "cuda_device_execution": "not tested; host lane only"}, indent=2) + "\n")


if __name__ == "__main__":
    main()
