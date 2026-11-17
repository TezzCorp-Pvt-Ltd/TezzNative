"""Build the Intel adapter, test its contract, and check actual NPU discovery.

Install the pinned vendor C SDK with:
  python -m pip install --target build/deps/openvino openvino==2025.4.1
Pass --require-device on an Intel NPU runner. Absence then FAILS the gate.
The contract test double never counts as device execution.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=ROOT / "build/deps/openvino/openvino")
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/npu-openvino")
    parser.add_argument("--contract-only", action="store_true")
    parser.add_argument("--require-device", action="store_true")
    args = parser.parse_args()
    if args.contract_only and args.require_device:
        parser.error("--require-device cannot be combined with --contract-only")
    output, vendor = args.out.resolve(), args.sdk.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not (vendor/"include/openvino/c/openvino.h").is_file():
        parser.error("OpenVINO C SDK headers are required")
    cc = shutil.which("gcc") or shutil.which("cc")
    if not cc and os.name == "nt":
        cc = "C:/msys64/mingw64/bin/gcc.exe"
    if not cc:
        parser.error("C compiler required")
    env = {**os.environ, "PATH": str(Path(cc).parent)+os.pathsep+os.environ.get("PATH", "")}
    common = [cc, "-std=gnu11", "-O1", "-g", "-I"+str(SDK), "-I"+str(vendor/"include")]
    report = {"contract": "pending", "device_execution": "not_run", "commands": []}

    def command(argv, run_env=env):
        result = subprocess.run(list(map(str, argv)), env=run_env, capture_output=True, text=True, timeout=180)
        report["commands"].append({"argv": list(map(str, argv)), "returncode": result.returncode,
                                   "stdout": result.stdout, "stderr": result.stderr})
        if result.returncode:
            raise RuntimeError(result.stdout+result.stderr)
        return result

    try:
        command(common+["-I"+str(SDK/"include"), "-DTN_ENABLE_HOST_GUI=1", "-DTN_NATIVE_X64_ABI=1",
                        "-DTN_NPU_OPENVINO=1", "-fsyntax-only", SDK/"src/tnrt.c"])
        report["production_runtime_compile"] = "pass"
        fixture = Path(__file__).with_name("openvino_contract.c")
        executable = output/("contract.exe" if os.name == "nt" else "contract")
        flags = ["-static-libgcc"] if os.name == "nt" else ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-pthread", "-lm"]
        command(common+[fixture, "-o", executable]+flags)
        result = command([executable], {**env, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1", "UBSAN_OPTIONS": "halt_on_error=1"})
        if "PASS OpenVINO provider contract" not in result.stdout:
            raise AssertionError("contract marker missing")
        report["contract"] = "pass_test_double"
        if args.contract_only:
            return 0
        library = output/("npu.dll" if os.name == "nt" else "npu.so")
        libraries = sorted((vendor/"libs").glob("libopenvino_c.so*"))
        link = [vendor/"libs/openvino_c.lib", "-static-libgcc"] if os.name == "nt" else ["-fPIC", "-pthread", "-lm", libraries[0], "-Wl,-rpath,"+str(vendor/"libs")]
        command(common+["-shared", fixture.with_name("openvino_bridge.c"), "-o", library]+link)
        directory = os.add_dll_directory(str(vendor/"libs")) if os.name == "nt" else None
        runtime = ctypes.CDLL(str(library))
        integer, pointer = ctypes.c_longlong, ctypes.POINTER(ctypes.c_double)
        for name, arguments in {
            "has_backend": [], "device_count": [], "init": [], "shutdown": [],
            "set_device": [integer], "model_load": [ctypes.c_char_p], "model_free": [integer],
            "model_run_f64_1d": [integer, pointer, integer, pointer, integer],
        }.items():
            function = getattr(runtime, "tn_npu_"+name)
            function.argtypes, function.restype = arguments, integer
        count = runtime.tn_npu_device_count()
        report["dedicated_devices"] = count
        assert runtime.tn_npu_has_backend() == int(count > 0)
        data = (ctypes.c_double*4)(-3, 0, 1.25, 4)
        result = (ctypes.c_double*4)(41, 42, 43, 44)
        path = str(fixture.with_name("relu.xml").resolve()).encode()
        if not count:
            assert runtime.tn_npu_init() == -1
            assert runtime.tn_npu_model_load(path) == -1
            assert runtime.tn_npu_set_device(0) == -1
            assert runtime.tn_npu_model_run_f64_1d(1, data, 4, result, 4) == -1
            assert list(result) == [41, 42, 43, 44]
            report["absence_contract"] = "pass_no_fallback"
            report["device_execution"] = "unverified_no_hardware"
            if args.require_device:
                raise RuntimeError("Required Intel NPU is absent; no device verification was performed")
        else:
            assert runtime.tn_npu_init() == 0
            handle = runtime.tn_npu_model_load(path)
            assert handle > 0, "NPU could not compile the owned ReLU fixture"
            assert runtime.tn_npu_model_run_f64_1d(handle, data, 4, result, 4) == 0
            assert list(result) == [0, 0, 1.25, 4]
            assert runtime.tn_npu_model_free(handle) == 0
            report["device_execution"] = "pass_relu_f32_only"
        assert runtime.tn_npu_shutdown() == 0
        report["adapter_sha256"] = hashlib.sha256(library.read_bytes()).hexdigest()
        if directory:
            directory.close()
        return 0
    except (AssertionError, RuntimeError, OSError, IndexError) as error:
        report["error"] = str(error)
        return 1
    finally:
        (output/"results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in report.items() if k != "commands"}, indent=2))


if __name__ == "__main__":
    raise SystemExit(main())
