"""Verify dedicated AMD/Qualcomm adapters against the official ORT C API.

--fetch-headers downloads only pinned, SHA256-checked vendor headers.
--require-device fails unless the selected physical NPU executes the fixture.
Contract test doubles and real-runtime absence checks are reported separately.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
HEADERS = {
    "onnxruntime_c_api.h": "71125e66180a991d65c9bdbad4aa20daaa1f7a48c7a5c0fa5f18f250ac839a02",
    "onnxruntime_ep_c_api.h": "b92778f50c36ecdf53d0344e0129a78b306afed653f81bba8a9597e3e6e9546f",
}


def relu_model():
    """Owned one-node f32 ONNX fixture; no weights or model download."""
    def varint(number):
        result = bytearray()
        while number > 127:
            result.append((number & 127) | 128)
            number >>= 7
        return bytes(result)+bytes([number])

    def number(field, value):
        return varint(field*8)+varint(value)

    def message(field, value):
        if isinstance(value, str):
            value = value.encode()
        return varint(field*8+2)+varint(len(value))+value

    shape = message(1, number(1, 1))+message(1, number(1, 4))
    tensor_type = message(1, number(1, 1)+message(2, shape))
    value = lambda name: message(1, name)+message(2, tensor_type)
    node = message(1, "input")+message(2, "output")+message(4, "Relu")
    graph = message(1, node)+message(2, "tezz_npu_relu")+message(11, value("input"))+message(12, value("output"))
    return number(1, 9)+message(7, graph)+message(8, number(2, 13))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider", choices=["AMD", "QNN"], required=True)
    parser.add_argument("--headers", type=Path, default=ROOT/"build/deps/onnxruntime-headers")
    parser.add_argument("--runtime", type=Path, default=ROOT/"build/deps/onnxruntime/onnxruntime/capi")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--fetch-headers", action="store_true")
    parser.add_argument("--contract-only", action="store_true")
    parser.add_argument("--require-device", action="store_true")
    args = parser.parse_args()
    if args.contract_only and args.require_device:
        parser.error("--require-device cannot be combined with --contract-only")
    output, headers, vendor = args.out.resolve(), args.headers.resolve(), args.runtime.resolve()
    output.mkdir(parents=True, exist_ok=True)
    headers.mkdir(parents=True, exist_ok=True)
    report = {"provider": args.provider, "contract": "pending", "device_execution": "not_run", "commands": []}
    try:
        for name, digest in HEADERS.items():
            path = headers/name
            if not path.is_file() and args.fetch_headers:
                url = "https://raw.githubusercontent.com/microsoft/onnxruntime/v1.23.2/include/onnxruntime/core/session/"+name
                data = urllib.request.urlopen(url, timeout=60).read()
                if hashlib.sha256(data).hexdigest() != digest:
                    raise RuntimeError("Header checksum mismatch: "+name)
                path.write_bytes(data)
            if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise RuntimeError("Expected pinned ORT1.23.2 header: "+name)
        cc = shutil.which("gcc") or shutil.which("cc")
        if not cc and os.name == "nt":
            cc = "C:/msys64/mingw64/bin/gcc.exe"
        if not cc:
            raise RuntimeError("C compiler required")
        env = {**os.environ, "PATH": str(Path(cc).parent)+os.pathsep+os.environ.get("PATH", "")}
        common = [cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                  "-DTN_NPU_ORT_"+args.provider+"=1", "-I"+str(SDK), "-isystem", str(headers)]

        def command(argv, run_env=env):
            argv = list(map(str, argv))
            result = subprocess.run(argv, env=run_env, capture_output=True, text=True, timeout=180)
            report["commands"].append({"argv": argv, "returncode": result.returncode,
                                       "stdout": result.stdout, "stderr": result.stderr})
            if result.returncode:
                raise RuntimeError(result.stdout+result.stderr)
            return result

        command(common+["-I"+str(SDK/"include"), "-DTN_ENABLE_HOST_GUI=1", "-DTN_NATIVE_X64_ABI=1",
                        "-Wno-error", "-fsyntax-only", SDK/"src/tnrt.c"])
        report["production_runtime_compile"] = "pass"
        fixture = Path(__file__).with_name("ort_contract.c")
        executable = output/("contract.exe" if os.name == "nt" else "contract")
        flags = ["-static-libgcc"] if os.name == "nt" else ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-pthread", "-lm"]
        command(common+[fixture, "-o", executable]+flags)
        result = command([executable], {**env, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1", "UBSAN_OPTIONS": "halt_on_error=1"})
        if "PASS " not in result.stdout or "test double; no device" not in result.stdout:
            raise AssertionError("contract marker missing")
        report["contract"] = "pass_test_double"
        if args.contract_only:
            return 0
        libraries = [vendor/"onnxruntime.dll"] if os.name == "nt" else sorted(vendor.glob("libonnxruntime.so*"))
        if not libraries or not libraries[0].is_file():
            raise RuntimeError("Real vendor C runtime library is required")
        # Windows searches System32 before PATH for an executable's imports.
        # Pip's Linux wheel also omits the public SONAME symlink. Stage the
        # pinned C library beside the test binaries to make identity explicit.
        staged_runtime = output/("onnxruntime.dll" if os.name == "nt" else "libonnxruntime.so.1")
        shutil.copyfile(libraries[0], staged_runtime)
        library = output/("npu.dll" if os.name == "nt" else "npu.so")
        flags = ["-static-libgcc"] if os.name == "nt" else ["-fPIC", "-pthread", "-lm", "-Wl,-rpath,"+str(output)]
        command(common+["-shared", fixture.with_name("ort_bridge.c"), libraries[0], "-o", library]+flags)
        directory = os.add_dll_directory(str(vendor)) if os.name == "nt" else None
        runtime = ctypes.CDLL(str(library))
        integer, pointer = ctypes.c_longlong, ctypes.POINTER(ctypes.c_double)
        for name, arguments in {
            "has_backend": [], "device_count": [], "init": [], "shutdown": [],
            "set_device": [integer], "model_load": [ctypes.c_char_p], "model_free": [integer],
            "model_run_f64_1d": [integer, pointer, integer, pointer, integer],
        }.items():
            function = getattr(runtime, "tn_npu_"+name)
            function.argtypes, function.restype = arguments, integer
        model = output/"relu.onnx"
        model.write_bytes(relu_model())
        parser_test = output/("fixture-parser.exe" if os.name == "nt" else "fixture-parser")
        command(common+[fixture.with_name("onnx_fixture.c"), libraries[0], "-o", parser_test]+flags)
        command([parser_test, model], {**env, "PATH": str(vendor)+os.pathsep+env["PATH"]})
        report["owned_fixture_parser"] = "pass_not_device_execution"
        count = runtime.tn_npu_device_count()
        report["dedicated_devices"] = count
        assert runtime.tn_npu_has_backend() == int(count > 0)
        data = (ctypes.c_double*4)(-3, 0, 1.25, 4)
        result = (ctypes.c_double*4)(41, 42, 43, 44)
        if not count:
            assert runtime.tn_npu_init() == -1
            assert runtime.tn_npu_set_device(0) == -1
            assert runtime.tn_npu_model_load(str(model).encode()) == -1
            assert runtime.tn_npu_model_run_f64_1d(1, data, 4, result, 4) == -1
            assert list(result) == [41, 42, 43, 44]
            report["absence_contract"] = "pass_no_fallback"
            report["device_execution"] = "unverified_no_hardware"
            if args.require_device:
                raise RuntimeError("Required dedicated NPU is absent; no device execution verified")
        else:
            assert runtime.tn_npu_init() == 0
            handle = runtime.tn_npu_model_load(str(model).encode())
            assert handle > 0, "NPU could not compile the owned ReLU fixture"
            assert runtime.tn_npu_model_run_f64_1d(handle, data, 4, result, 4) == 0
            assert list(result) == [0, 0, 1.25, 4]
            assert runtime.tn_npu_model_free(handle) == 0
            report["device_execution"] = "pass_relu_f32_only"
        assert runtime.tn_npu_shutdown() == 0
        report["adapter_sha256"] = hashlib.sha256(library.read_bytes()).hexdigest()
        report["vendor_runtime_sha256"] = hashlib.sha256(libraries[0].read_bytes()).hexdigest()
        if directory:
            directory.close()
        return 0
    except (AssertionError, RuntimeError, OSError) as error:
        report["error"] = str(error)
        return 1
    finally:
        (output/"results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in report.items() if k != "commands"}, indent=2))


if __name__ == "__main__":
    raise SystemExit(main())
