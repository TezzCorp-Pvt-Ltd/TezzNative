"""Verify native executables use deployed libraries after the compiler exits."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
SOURCE = ROOT / "tests/regression/runtime/host_imports.tn"


def command(arguments, cwd, env):
    completed = subprocess.run([str(x) for x in arguments], cwd=cwd, env=env,
                               capture_output=True, text=True, timeout=20)
    return {"exit": completed.returncode, "stdout": completed.stdout, "stderr": completed.stderr}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/native-import-artifacts")
    args = parser.parse_args()
    compiler, out = args.compiler.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["TEZZ_SDK_ROOT"] = str(SDK)
    executable = out / ("program.exe" if os.name == "nt" else "program")
    results = []
    built = command([compiler, "buildexe", SOURCE, executable, "--verify"], out, env)
    assert built["exit"] == 0, built
    runtime_name = "tnrt.dll" if os.name == "nt" else "libtnrt-native.so"
    assert (out / runtime_name).is_file(), "runtime was not copied beside the executable"
    binary = executable.read_bytes()
    for symbol in (b"tn_getenv_str\0", b"tn_native_malloc\0", b"tn_native_free\0"):
        assert symbol in binary, "runtime symbol is absent from executable imports: " + repr(symbol)
    executed = command([executable], out, env)
    assert executed["exit"] == 0, executed
    results.append({"case": "compiler_exited_runtime_deployed", "status": "pass", "run": executed})
    # Isolate the compiler without its libraries or any SDK binary fallback.
    with tempfile.TemporaryDirectory(prefix="tezz-native-imports-") as temporary:
        isolated = Path(temporary)
        copied = isolated / compiler.name
        shutil.copy2(compiler, copied)
        sdk = isolated / "sdk"
        (sdk / "lib").mkdir(parents=True)
        # Keep language imports usable while withholding all native artifacts.
        for module in (SDK / "lib").rglob("*.tn"):
            target = sdk / "lib" / module.relative_to(SDK / "lib")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(module, target)
        negative_env = env.copy()
        negative_env["TEZZ_SDK_ROOT"] = str(sdk)
        failed = command([copied, "buildexe", SOURCE, isolated / executable.name], isolated, negative_env)
        diagnostic = failed["stdout"] + failed["stderr"]
        assert failed["exit"] != 0 and "required native library" in diagnostic and runtime_name in diagnostic, failed
        results.append({"case": "missing_runtime_is_build_error", "status": "pass", "run": failed})
        cpu_name = "tzcpu.dll" if os.name == "nt" else "libtzcpu-native.so"
        for library in (runtime_name, cpu_name):
            shutil.copy2(compiler.parent / library, isolated / library)
        cpu_program = isolated / ("cpu.exe" if os.name == "nt" else "cpu")
        cpu_source = ROOT / "tests/regression/sdk_ai/softmax.tn"
        built_cpu = command([copied, "buildexe", cpu_source, cpu_program, "--verify"], isolated, negative_env)
        assert built_cpu["exit"] == 0, built_cpu
        gpu_name = "tzgpu.dll" if os.name == "nt" else "libtzgpu-native.so"
        assert not (isolated / gpu_name).exists(), "CPU-only deployment unexpectedly copied a GPU backend"
        assert gpu_name.encode() + b"\0" not in cpu_program.read_bytes(), "CPU executable eagerly imports the GPU library"
        cpu_result = command([cpu_program], isolated, negative_env)
        assert cpu_result["exit"] == 0, cpu_result
        results.append({"case": "CPU_tensor_without_GPU_artifact", "status": "pass", "run": cpu_result})
        gpu_program = isolated / ("gpu.exe" if os.name == "nt" else "gpu")
        gpu_source = ROOT / "tests/regression/runtime/support/gpu_unavailable.tn"
        built_gpu = command([copied, "buildexe", gpu_source, gpu_program, "--verify"], isolated, negative_env)
        assert built_gpu["exit"] == 0, built_gpu
        gpu_result = command([gpu_program], isolated, negative_env)
        assert gpu_result["exit"] == 0 and "CUDA backend unavailable" in gpu_result["stderr"], gpu_result
        results.append({"case": "explicit_GPU_request_reports_unavailable", "status": "pass", "run": gpu_result})
    (out / "results.json").write_text(json.dumps({"compiler": str(compiler), "results": results}, indent=2) + "\n")
    print("NATIVE_IMPORT_ARTIFACTS pass=" + str(len(results)) + " fail=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
