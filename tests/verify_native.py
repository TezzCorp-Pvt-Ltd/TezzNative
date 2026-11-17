"""Compile and verify native math/speech kernels against independent oracles."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys

from run import ROOT, SDK, execute


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc")
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/native-oracles")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cc = args.cc or shutil.which("cc") or shutil.which("gcc")
    if not cc and os.name == "nt" and Path("C:/msys64/mingw64/bin/gcc.exe").is_file():
        cc = "C:/msys64/mingw64/bin/gcc.exe"
    if not cc:
        parser.error("C compiler required")
    env = os.environ.copy()
    env.pop("PYTHONOPTIMIZE", None)
    env["PATH"] = str(Path(shutil.which(cc) or cc).resolve().parent) + os.pathsep + env.get("PATH", "")
    report = {"results": [], "source_sha256": {}}

    def run(name, command, timeout=60):
        record = execute(command, out, env, timeout)
        record.update(case=name, status="pass" if record["exit"] == 0 else "fail")
        report["results"].append(record)
        (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(record["status"].upper(), name, flush=True)
        print(record["stdout"] + record["stderr"], end="", flush=True)
        return record["exit"] == 0

    flags = [cc, "-std=gnu11", "-O2", "-I" + str(SDK / "include")]
    if os.name == "nt":
        flags += ["-static-libgcc"]
    cpu_source = SDK / "lib/tzcpu_kernels.c"
    cpu_inputs = [cpu_source, SDK / "include/float_bits.h", *sorted((SDK / "lib/tzcpu").glob("*.h"))]
    for source in cpu_inputs:
        report["source_sha256"][str(source.relative_to(ROOT))] = hashlib.sha256(source.read_bytes()).hexdigest()
    cpu = out / ("cpu-oracle.dll" if os.name == "nt" else "libcpu-oracle.so")
    cpu_command = flags + ["-shared"] + ([] if os.name == "nt" else ["-fPIC"])
    cpu_command += [cpu_source, "-o", cpu, "-lm"]
    cpu_command += ["-luser32", "-lgdi32", "-lwinmm"] if os.name == "nt" else ["-lpthread"]
    if run("build_cpu_oracle", cpu_command, 240):
        run("cpu_numeric", [sys.executable, ROOT / "tests/kernel_numeric.py", "--library", cpu])
        run("gguf_boundaries", [sys.executable, ROOT / "tests/regression/modelio/check_gguf.py",
                               "--library", cpu, "--out", out / "gguf"])

    speech = SDK / "runtime/speech"
    suffix = ".exe" if os.name == "nt" else ""
    for source in speech.glob("*.c"):
        report["source_sha256"][str(source.relative_to(ROOT))] = hashlib.sha256(source.read_bytes()).hexdigest()
    ctc = out / ("ctc_oracle" + suffix)
    if run("build_ctc_oracle", flags + ["-I" + str(speech),
            ROOT / "tests/regression/speech/ctc_oracle.c", speech / "ctc.c", "-lm", "-o", ctc], 240):
        run("ctc_independent_gradient", [sys.executable, ROOT / "tests/regression/speech/check_ctc.py", ctc])
    model = out / ("stt_model_test" + suffix)
    if run("build_stt_model_test", flags + ["-I" + str(speech),
            ROOT / "tests/regression/speech/stt_model_test.c", *sorted(speech.glob("*.c")),
            "-lm", "-o", model], 240):
        run("stt_train_checkpoint", [model, out / "test-stt.tns"])
    for name, arguments in (("tts_model_test", [out / "test-tts.tns"]),
                            ("duration_expectation", []), ("duration_bounds", [])):
        executable = out / (name + suffix)
        if run("build_" + name, flags + ["-I" + str(speech),
                ROOT / "tests/regression/speech" / (name + ".c"),
                *sorted(speech.glob("*.c")), "-lm", "-o", executable], 240):
            run(name, [executable, *arguments])

    arguments = out / ("args_oracle" + suffix)
    args_source = SDK / "runtime/args.c"
    report["source_sha256"][str(args_source.relative_to(ROOT))] = hashlib.sha256(args_source.read_bytes()).hexdigest()
    if run("build_args_oracle", flags + ["-DTN_NATIVE_X64_ABI=1",
            ROOT / "tests/regression/speech/args_oracle.c", args_source, "-o", arguments], 240):
        run("native_argument_parsing", [sys.executable, ROOT / "tests/regression/speech/check_args.py", arguments])

    lexer = out / ("integer_oracle" + suffix)
    lexer_sources = [SDK / "src/lexer.c", SDK / "src/util.c"]
    for source in lexer_sources:
        report["source_sha256"][str(source.relative_to(ROOT))] = hashlib.sha256(source.read_bytes()).hexdigest()
    if run("build_lexer_oracle", flags + [ROOT / "tests/regression/lexer/integer_oracle.c",
            *lexer_sources, "-lm", "-o", lexer], 240):
        run("integer_float_tokens", [sys.executable, ROOT / "tests/regression/lexer/check_integer.py", lexer])
    report["summary"] = {status: sum(item["status"] == status for item in report["results"])
                         for status in ("pass", "fail")}
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return int(report["summary"]["fail"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
