"""Verify the Hailo adapter against official headers and a provider test double.

No Hailo hardware, driver or HEF model is substituted by this contract gate.
Use the optional ARM64 cross lane to verify the same API contract on that CPU.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT/"TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
HEADER_COMMIT = "542ba8f3cd95ed85175083ee4add00167c50f668"
HEADER_HASHES = {
    "hailort.h": "37a445b450de7453aa8bd3bcb2013b4c57c3a098792dce0ba9b9b0f6ea1a4e6d",
    "platform.h": "0ec5a4e74cb7289eed5d64d477907e4318ab6e4ad57e77f87a55c30362849dc5",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headers", type=Path,
                        default=ROOT/"build/verification/hailort-sdk/hailort/libhailort/include")
    parser.add_argument("--out", type=Path, default=ROOT/"build/verification/npu-hailo")
    parser.add_argument("--cross-aarch64", action="store_true")
    parser.add_argument("--fetch-headers", action="store_true",
                        help="fetch and SHA-256 verify the two official pinned C headers into the output directory")
    parser.add_argument("--require-device", action="store_true",
                        help="fail: this contract gate cannot certify device execution")
    parser.add_argument("--library", type=Path,
                        help="also link the real libhailort shared library and verify a no-device host")
    args = parser.parse_args()
    output, headers = args.out.resolve(), args.headers.resolve()
    if args.fetch_headers:
        headers = output/"headers"
    output.mkdir(parents=True, exist_ok=True)
    report = {"host": platform.platform(), "provider_contract": "pending",
              "device_execution": "not_run", "hardware_verified": False, "commands": []}
    fixture = Path(__file__).with_name("hailo_contract.c")
    files = [Path(__file__), fixture, fixture.with_name("hailo_bridge.c"),
             SDK/"runtime/npu/hailo.h", SDK/"runtime/npu/raw_unsupported.h",
             SDK/"include/native_abi.h", headers/"hailo/hailort.h", headers/"hailo/platform.h"]

    def command(argv, env=None):
        value = subprocess.run(list(map(str,argv)), capture_output=True, text=True,
                               timeout=180, cwd=output, env=env)
        report["commands"].append({"argv":list(map(str,argv)), "exit":value.returncode,
                                   "stdout":value.stdout, "stderr":value.stderr})
        if value.returncode:
            raise RuntimeError(value.stdout+value.stderr)
        return value

    try:
        if platform.system() != "Linux":
            raise RuntimeError("this GCC contract gate requires Linux/WSL; Windows HailoRT requires MSVC")
        if args.fetch_headers:
            report["official_headers_commit"] = HEADER_COMMIT
            for name, expected in HEADER_HASHES.items():
                path = headers/"hailo"/name
                if not path.is_file() or digest(path) != expected:
                    url = ("https://raw.githubusercontent.com/hailo-ai/hailort/"+HEADER_COMMIT+
                           "/hailort/libhailort/include/hailo/"+name)
                    with urllib.request.urlopen(url, timeout=60) as response:
                        data = response.read(1024*1024+1)
                    if hashlib.sha256(data).hexdigest() != expected:
                        raise ValueError("official Hailo header checksum mismatch: "+name)
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
        if any(not path.is_file() for path in files):
            raise RuntimeError("production sources and official hailo/hailort.h + platform.h are required")
        report["source_sha256"] = {str(path):digest(path) for path in files}
        cc = shutil.which("gcc")
        if not cc:
            raise RuntimeError("GCC required")
        flags = ["-std=gnu11","-O1","-g","-Werror=implicit-function-declaration",
                 "-Werror=incompatible-pointer-types","-I"+str(SDK),"-I"+str(SDK/"include"),
                 "-I"+str(headers)]
        executable = output/"hailo-contract"
        command([cc,*flags,"-fsanitize=address,undefined","-fno-sanitize-recover=all",
                 fixture,"-pthread","-lm","-o",executable])
        value = command([executable], {**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1",
                                      "UBSAN_OPTIONS":"halt_on_error=1"})
        if "PASS Hailo official-header provider contract" not in value.stdout or value.stderr:
            raise AssertionError("provider contract marker missing or sanitizer diagnostics emitted")
        report["provider_contract"] = "pass_test_double_with_sanitizers"
        if args.cross_aarch64:
            cross,qemu = shutil.which("aarch64-linux-gnu-gcc"),shutil.which("qemu-aarch64")
            sysroot = Path("/usr/aarch64-linux-gnu")
            if not cross or not qemu or not sysroot.is_dir():
                raise RuntimeError("ARM64 cross GCC/libc and qemu-user required")
            executable = output/"hailo-contract-aarch64"
            command([cross,*flags,fixture,"-pthread","-lm","-o",executable])
            header = executable.read_bytes()[:20]
            if header[:6]!=b"\x7fELF\x02\x01" or int.from_bytes(header[18:20],"little")!=183:
                raise AssertionError("ARM64 artifact identity failed")
            value = command([qemu,"-L",sysroot,executable])
            if "PASS Hailo official-header provider contract" not in value.stdout:
                raise AssertionError("ARM64 contract marker missing")
            report["arm64_contract"] = "pass_qemu_cpu_test_double_not_npu"
        if any(digest(path)!=report["source_sha256"][str(path)] for path in files):
            raise AssertionError("contract inputs changed during verification")
        if args.library:
            vendor = args.library.resolve()
            # Keep the verification run's vendor diagnostics on stderr only.
            os.environ["HAILORT_LOGGER_PATH"] = "NONE"
            class Version(ctypes.Structure):
                _fields_ = [(name,ctypes.c_uint32) for name in ("major","minor","revision")]
            vendor_runtime = ctypes.CDLL(str(vendor))
            vendor_runtime.hailo_get_library_version.argtypes = [ctypes.POINTER(Version)]
            vendor_runtime.hailo_get_library_version.restype = ctypes.c_int
            version = Version()
            if vendor_runtime.hailo_get_library_version(ctypes.byref(version)) != 0:
                raise AssertionError("HailoRT library version query failed")
            report["vendor_library_version"] = [version.major,version.minor,version.revision]
            library = output/"hailo-adapter.so"
            command([cc,*flags,"-shared","-fPIC",fixture.with_name("hailo_bridge.c"),
                     vendor,"-Wl,-rpath,"+str(vendor.parent),"-pthread","-lm","-o",library])
            runtime = ctypes.CDLL(str(library))
            integer, pointer = ctypes.c_longlong, ctypes.POINTER(ctypes.c_double)
            for name, arguments in {
                "has_backend":[], "device_count":[], "init":[], "shutdown":[],
                "set_device":[integer], "model_load":[ctypes.c_char_p], "model_free":[integer],
                "model_run_f64_1d":[integer,pointer,integer,pointer,integer],
            }.items():
                function = getattr(runtime,"tn_npu_"+name)
                function.argtypes, function.restype = arguments, integer
            count = runtime.tn_npu_device_count()
            report["dedicated_devices"] = count
            if count != 0:
                raise RuntimeError("real-library absence check requires a host without Hailo devices; device inference is a separate gate")
            if runtime.tn_npu_has_backend()!=0 or runtime.tn_npu_init()!=-1 or runtime.tn_npu_set_device(0)!=-1:
                raise AssertionError("absent real Hailo device must fail explicitly")
            if runtime.tn_npu_model_load(b"nonexistent-owned-test.hef")!=-1:
                raise AssertionError("model load substituted execution for an absent Hailo device")
            data = (ctypes.c_double*4)(-3,0,1.25,4)
            result = (ctypes.c_double*4)(41,42,43,44)
            if runtime.tn_npu_model_run_f64_1d(1,data,4,result,4)!=-1 or list(result)!=[41,42,43,44]:
                raise AssertionError("absent-device inference modified caller output")
            if runtime.tn_npu_shutdown()!=0:
                raise AssertionError("real-library shutdown failed")
            report["real_library_absence"] = "pass_no_fallback"
            report["vendor_library_sha256"] = digest(vendor)
            report["adapter_library_sha256"] = digest(library)
        if args.require_device:
            raise RuntimeError("Hailo device execution is unverified: this is an official-header contract test only")
        report["status"] = "pass"
        return 0
    except (AssertionError, RuntimeError, ValueError, OSError, subprocess.TimeoutExpired) as error:
        report.update(status="fail", error=str(error))
        return 1
    finally:
        (output/"results.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
        print(json.dumps({key:value for key,value in report.items() if key not in ("commands","source_sha256")},indent=2))


if __name__=="__main__":
    raise SystemExit(main())
