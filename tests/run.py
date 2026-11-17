"""Build from source and run deterministic compiler/runtime regressions.

Examples:
  python tests/run.py --build --suite regression
  python tests/run.py --compiler path/to/tezzc --suite all
  python tests/run.py --suite regression --filter floats --backend bc

Artifacts and complete subprocess output stay in build/verification by default.
Regression programs must return nonzero on failure. Diagnostic fixtures have a
matching .diag.txt file; helper modules belong in a support/ directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
import struct
import wave

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
FAILURE = re.compile(r"^\s*(?:FAIL\b|ASSERTION_FAILURES\s*[=:]?\s*[1-9]\d*)", re.M)
SANITIZER_FAILURE = re.compile(r"ERROR: (?:AddressSanitizer|LeakSanitizer)|runtime error:|UndefinedBehaviorSanitizer")


def execute(command, cwd, env, timeout):
    started = time.monotonic()
    try:
        completed = subprocess.run(
            [str(arg) for arg in command], cwd=cwd, env=env,
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            timeout=timeout,
        )
        result = {"exit": completed.returncode, "stdout": completed.stdout,
                  "stderr": completed.stderr, "timeout": False}
    except subprocess.TimeoutExpired as error:
        def decode(value):
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else (value or "")
        result = {"exit": None, "stdout": decode(error.stdout),
                  "stderr": decode(error.stderr), "timeout": True}
    except OSError as error:
        result = {"exit": None, "stdout": "", "stderr": str(error), "timeout": False}
    result.update(command=[str(arg) for arg in command], seconds=round(time.monotonic() - started, 3))
    result["sanitizer_failure"] = bool(SANITIZER_FAILURE.search(result["stdout"] + result["stderr"]))
    return result


def build_compiler(target, cc, env, timeout, sanitize=False):
    compiler = cc or shutil.which("cc") or shutil.which("gcc")
    if not compiler and os.name == "nt":
        fallback = Path("C:/msys64/mingw64/bin/gcc.exe")
        if fallback.is_file():
            compiler = str(fallback)
    if not compiler:
        raise RuntimeError("A C11 compiler is required; supply --cc or put gcc/clang on PATH")
    resolved = shutil.which(compiler) or compiler
    env["PATH"] = str(Path(resolved).resolve().parent) + os.pathsep + env.get("PATH", "")
    target.parent.mkdir(parents=True, exist_ok=True)
    # Match CMake's C11-with-extensions mode, including POSIX declarations.
    speech_sources = sorted((SDK / "runtime/speech").glob("*.c"))
    command = [compiler, "-std=gnu11", "-O1", "-DTN_ENABLE_HOST_GUI=1",
               "-I" + str(SDK / "include"), *sorted((SDK / "src").glob("*.c")),
               *speech_sources, "-o", target]
    sanitizer_flags = ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if sanitize else []
    command += sanitizer_flags
    if os.name == "nt":
        command += ["-lws2_32", "-lsecur32", "-lcrypt32", "-lwinhttp", "-luser32", "-lgdi32", "-lbcrypt"]
    else:
        command += ["-DTN_TLS_OPENSSL=1"]
        command += ["-lssl", "-lcrypto", "-lpthread", "-lm", "-ldl"]
    result = execute(command, ROOT, env, max(timeout, 240))
    if result["exit"] != 0:
        raise RuntimeError("Compiler source build failed:\n" + result["stdout"] + result["stderr"])
    flags = [compiler, "-shared", "-std=gnu11", "-O1", "-DTN_NATIVE_X64_ABI=1"]
    flags += sanitizer_flags
    if os.name == "nt":
        flags += ["-static-libgcc"]
    else:
        flags += ["-fPIC"]
    runtime = flags + ["-DTN_ENABLE_HOST_GUI=1", "-I" + str(SDK / "include"),
                       SDK / "src/tnrt.c", SDK / "src/host_gui_backend.c", SDK / "src/security.c", SDK / "src/decimal.c",
                       SDK / "runtime/native_alloc.c", SDK / "runtime/args.c",
                       SDK / "runtime/gpu_bridge.c", *speech_sources, "-o",
                       target.parent / ("tnrt.dll" if os.name == "nt" else "libtnrt-native.so")]
    cpu = flags + [SDK / "lib/tzcpu_kernels.c", "-o",
                   target.parent / ("tzcpu.dll" if os.name == "nt" else "libtzcpu-native.so")]
    if os.name == "nt":
        runtime += ["-lws2_32", "-lsecur32", "-lcrypt32", "-lwinhttp", "-luser32", "-lgdi32", "-lbcrypt"]
        cpu += ["-lm", "-luser32", "-lgdi32", "-lwinmm"]
    else:
        runtime += ["-DTN_TLS_OPENSSL=1", "-lssl", "-lcrypto", "-lpthread", "-lm", "-ldl"]
        cpu += ["-lpthread", "-lm"]
    result["libraries"] = []
    for name, library_command in (("runtime", runtime), ("CPU kernels", cpu)):
        library = execute(library_command, ROOT, env, max(timeout, 240))
        result["libraries"].append(library)
        if library["exit"] != 0:
            raise RuntimeError(name + " source build failed:\n" + library["stdout"] + library["stderr"])
    return result


def discover(suite):
    cases = []
    if suite in ("regression", "all"):
        for source in sorted((ROOT / "tests/regression").rglob("*.tn")):
            if "support" in source.parts:
                continue
            diag = source.with_suffix(".diag.txt")
            cases.append((source, "reject" if diag.exists() else "runtime", diag if diag.exists() else None))
    if suite in ("core", "all"):
        base = ROOT / "tests/conformance"
        for category in ("", "parser", "typecheck"):
            for source in sorted((base / category / "valid").glob("*.tn")):
                cases.append((source, "check", None))
            for source in sorted((base / category / "invalid").glob("*.tn")):
                diag = base / "diagnostics" / category / source.with_suffix(".diag.txt").name
                cases.append((source, "reject", diag))
        for source in sorted((base / "stdlib").glob("*.tn")):
            cases.append((source, "check", None))
    if suite in ("native", "all"):
        for source in sorted((ROOT / "tests/conformance/native").glob("*.tn")):
            cases.append((source, "native", None))
    return cases


def run_case(source, mode, diag, backend, compiler, out, env, timeout):
    label = source.relative_to(ROOT).as_posix()
    case_dir = out / "cases" / source.relative_to(ROOT).with_suffix("")
    case_dir.mkdir(parents=True, exist_ok=True)
    result = {"case": label, "backend": backend, "status": "fail", "steps": []}
    if (source.name.startswith("linux_") and platform.system() != "Linux") or (
        source.name.startswith("windows_") and os.name != "nt"
    ):
        result.update(status="skip", reason="fixture requires another operating system")
        return result

    def step(command):
        record = execute(command, case_dir, env, timeout)
        result["steps"].append(record)
        return record

    check = step([compiler, "check", source])
    if check.get("sanitizer_failure"):
        result["reason"] = "sanitizer reported invalid memory access or undefined behavior"
        return result
    if mode == "reject":
        if not diag or not diag.is_file():
            result["reason"] = "negative fixture is missing required diagnostic expectations"
        else:
            expected = [line.strip() for line in diag.read_text(encoding="utf-8").splitlines() if line.strip()]
            output = check["stdout"] + check["stderr"]
            if check["exit"] is not None and 0 < check["exit"] < 256 and expected and all(s in output for s in expected):
                result["status"] = "pass"
            else:
                result["reason"] = "expected controlled rejection with matching diagnostics"
        return result
    if check["exit"] != 0:
        result["reason"] = "type checking failed or timed out"
        return result
    if mode == "check":
        result["status"] = "pass"
        return result
    if "speech" in source.relative_to(ROOT).parts:
        prepare = step([sys.executable, ROOT / "tests/regression/speech/prepare_fixtures.py",
                        case_dir / "build/speech-regression/fixtures"])
        if prepare["exit"] != 0:
            result["reason"] = "speech fixture preparation failed"
            return result
    if backend == "bc":
        runtime = step([compiler, "run", source, "--bc"])
    else:
        executable = case_dir / ("program.exe" if os.name == "nt" else "program")
        # A stale executable must never make a failed build appear successful.
        executable.unlink(missing_ok=True)
        build = step([compiler, "buildexe", source, executable, "--verify"])
        if build["exit"] != 0 or build.get("sanitizer_failure") or not executable.is_file():
            result["reason"] = "native build/verification failed or timed out"
            return result
        runtime = step([executable])
    if runtime.get("sanitizer_failure"):
        result["reason"] = "sanitizer reported invalid memory access or undefined behavior"
    elif runtime["exit"] != 0:
        result["reason"] = "program failed, crashed, or timed out"
    elif FAILURE.search(runtime["stdout"] + runtime["stderr"]):
        result["reason"] = "program reported a failed assertion despite a zero exit code"
    else:
        expected_file = source.with_suffix(".stdout.txt")
        if expected_file.is_file() and runtime["stdout"].rstrip("\r\n") != expected_file.read_text(encoding="utf-8").rstrip("\r\n"):
            result["reason"] = "stdout does not exactly match the expected output"
        else:
            result["status"] = "pass"
    if result["status"] == "pass" and label == "tests/regression/speech/wav.tn":
        try:
            with wave.open(str(case_dir / "build/speech-regression/fixtures/output.wav"), "rb") as wav:
                if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) != (1, 2, 16000, 3):
                    raise ValueError("incorrect WAV header")
                if struct.unpack("<hhh", wav.readframes(3)) != (-32768, 0, 32767):
                    raise ValueError("incorrect PCM sample values")
        except (OSError, ValueError, EOFError, wave.Error, struct.error) as error:
            result.update(status="fail", reason="independent WAV validation failed: " + str(error))
    if result["status"] == "pass" and label == "tests/regression/floats/differential.tn":
        oracle = step([sys.executable, ROOT / "tests/regression/floats/check_bits.py", case_dir])
        if oracle["exit"] != 0:
            result.update(status="fail", reason="floating-point differential oracle failed")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--build", action="store_true", help="build the checked-out compiler C sources first")
    parser.add_argument("--cc", help="C11 compiler for --build")
    parser.add_argument("--sanitize", action="store_true", help="instrument Linux source builds with ASan and UBSan")
    parser.add_argument("--suite", choices=("regression", "core", "native", "all"), default="regression")
    parser.add_argument("--backend", choices=("both", "bc", "native"), default="both")
    parser.add_argument("--filter", default="", help="run only cases whose path contains this text")
    parser.add_argument("--timeout", type=float, default=45)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    compiler = (args.compiler or out / "compiler" / ("tezzc.exe" if os.name == "nt" else "tezzc")).resolve()
    env = os.environ.copy()
    env["TEZZ_SDK_ROOT"] = str(SDK)
    if args.sanitize:
        if os.name == "nt" or not args.build or args.backend != "bc" or args.suite not in ("regression", "core"):
            parser.error("--sanitize requires a Linux --build with --backend bc")
        # The compiler retains its AST until exit; this gate checks invalid accesses
        # and undefined behavior. It does not claim to verify compiler leak freedom.
        env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    report = {"host": platform.platform(), "compiler": str(compiler), "suite": args.suite, "results": []}
    if args.build:
        report["build"] = {"status": "running", "sanitizers": args.sanitize}
        (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        try:
            report["build"] = build_compiler(compiler, args.cc, env, args.timeout, args.sanitize)
        except RuntimeError as error:
            report["build"] = {"status": "fail", "reason": str(error)}
            report["summary"] = {"pass": 0, "fail": 1, "skip": 0}
            (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            raise
    if not compiler.is_file():
        parser.error("compiler does not exist; use --build or --compiler")
    report["compiler_sha256"] = hashlib.sha256(compiler.read_bytes()).hexdigest()
    cases = [case for case in discover(args.suite) if args.filter in case[0].relative_to(ROOT).as_posix()]
    if not cases:
        parser.error("no matching tests; refusing to report an empty suite as successful")
    for source, mode, diag in cases:
        backends = ("check",) if mode in ("check", "reject") else (("native",) if mode == "native" else (("bc", "native") if args.backend == "both" else (args.backend,)))
        for backend in backends:
            result = run_case(source, mode, diag, backend, compiler, out, env, args.timeout)
            report["results"].append(result)
            print(f"{result['status'].upper()} {backend} {result['case']} {result.get('reason', '')}", flush=True)
            if result["status"] == "fail":
                for step in result["steps"]:
                    print(step["stdout"] + step["stderr"], end="", flush=True)
            # Keep evidence even if a later test or the outer process is interrupted.
            (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    counts = {status: sum(result["status"] == status for result in report["results"]) for status in ("pass", "fail", "skip")}
    report["summary"] = counts
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("VERIFICATION_SUMMARY " + " ".join(f"{key}={value}" for key, value in counts.items()), flush=True)
    return 1 if counts["fail"] else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
