"""Check SDK .tn training arithmetic against a test-only CPU kernel adapter.

This does not validate GPU execution. The adapter stays in an isolated build
directory and must never be distributed or selected by production code.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/sdk-training-oracle")
    parser.add_argument("--cc")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    cc = args.cc or shutil.which("cc") or shutil.which("gcc")
    if not cc and os.name == "nt":
        cc = "C:/msys64/mingw64/bin/gcc.exe"
    if not cc:
        parser.error("a C compiler is required")
    env["TEZZ_SDK_ROOT"] = str(SDK)
    env["PATH"] = str(Path(shutil.which(cc) or cc).resolve().parent) + os.pathsep + str(compiler.parent) + os.pathsep + env.get("PATH", "")
    env["LD_LIBRARY_PATH"] = str(compiler.parent) + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    inputs = [SDK / "lib/tzcpu_kernels.c", SDK / "include/float_bits.h", SDK / "lib/tznn.tn", SDK / "lib/tzgpu.tn", *sorted((SDK / "lib/tzcpu").glob("*.h")), ROOT / "tests/regression/sdk_ai/support/tzgpu_cpu_oracle.c"]
    report = {"scope": "SDK .tn training arithmetic with test-only CPU kernel adapter", "gpu_execution": False, "steps": [],
              "source_sha256": {str(source.relative_to(ROOT)): hashlib.sha256(source.read_bytes()).hexdigest() for source in inputs}}

    def execute(command):
        result = subprocess.run([str(part) for part in command], cwd=out, env=env, capture_output=True, text=True, timeout=60)
        report["steps"].append({"command": [str(part) for part in command], "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
        return result.returncode

    fixture = ROOT / "tests/regression/sdk_ai/support/oracle_training.tn"
    executable = out / ("training.exe" if os.name == "nt" else "training")
    status = execute([compiler, "buildexe", fixture, executable, "--verify"])
    if status == 0:
        gpu_library = out / ("tzgpu.dll" if os.name == "nt" else "libtzgpu-native.so")
        flags = ["-static-libgcc"] if os.name == "nt" else ["-fPIC", "-DTN_NATIVE_X64_ABI"]
        libraries = ["-luser32", "-lgdi32", "-lwinmm"] if os.name == "nt" else ["-lpthread"]
        status = execute([cc, "-O1", "-shared", *flags, "-I" + str(SDK / "lib"), ROOT / "tests/regression/sdk_ai/support/tzgpu_cpu_oracle.c", "-o", gpu_library, "-lm", *libraries])
    if status == 0:
        status = execute([executable])
    report.update(status="pass" if status == 0 else "fail", compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest())
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for step in report["steps"]:
        if step["exit"]:
            print(step["stdout"] + step["stderr"])
    print(("PASS" if status == 0 else "FAIL") + ": SDK training CPU oracle (GPU execution not tested)")
    return int(status != 0)


if __name__ == "__main__":
    sys.exit(main())
