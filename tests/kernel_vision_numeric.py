"""Compile production vision kernels and check numeric and rejection contracts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
CASES = ["maxpool_extreme", "maxpool_backward", "topk_extreme", "masked_softmax", "kv_negative",
         "conv_stride_zero", "conv_backward_stride_zero", "pool_stride_zero", "convolution_oracle",
         "conv_dimensions", "conv_backward_dimensions", "pool_dimensions", "pool_backward_dimensions",
         "topk_dimensions", "masked_dimensions", "kv_dimensions"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc")
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/kernel-vision")
    parser.add_argument("--sanitize", action="store_true", help="Enable undefined-behavior sanitizer")
    parser.add_argument("--case", choices=CASES, action="append")
    args = parser.parse_args()
    cc = args.cc or shutil.which("cc") or shutil.which("gcc")
    if not cc and os.name == "nt":
        cc = "C:/msys64/mingw64/bin/gcc.exe"
    if not cc:
        parser.error("a C compiler is required")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT / "tests/regression/sdk_ai/support/vision_oracle.c"
    inputs = [Path(__file__), source, SDK / "lib/tzcpu/vision.h", SDK / "include/float_bits.h"]
    report = {"scope": "production CPU vision kernels; scalar double and finite-difference oracle",
              "sanitizer": "undefined" if args.sanitize else None,
              "source_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs},
              "results": []}
    env = os.environ.copy()
    env["PATH"] = str(Path(shutil.which(cc) or cc).resolve().parent) + os.pathsep + env.get("PATH", "")
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    executable = out / ("vision.exe" if os.name == "nt" else "vision")
    flags = ["-fsanitize=undefined", "-fno-sanitize-recover=all"] if args.sanitize else []
    if os.name == "nt":
        flags.append("-static-libgcc")
    command = [cc, "-std=c99", "-O1", "-g", *flags, "-I" + str(SDK / "include"),
               "-I" + str(SDK / "lib"), str(source), "-o", str(executable), "-lm"]
    build = subprocess.run(command, env=env, capture_output=True, text=True, timeout=90)
    report["build"] = {"command": command, "exit": build.returncode, "stdout": build.stdout, "stderr": build.stderr}
    if build.returncode == 0:
        for case in args.case or CASES:
            command = [str(executable), case]
            try:
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=15)
                record = {"case": case, "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
            except (OSError, subprocess.TimeoutExpired) as error:
                record = {"case": case, "exit": None, "error": str(error)}
            record["status"] = "pass" if record["exit"] == 0 else "fail"
            report["results"].append(record)
            print(record["status"].upper(), case, flush=True)
            if record["status"] == "fail":
                print(json.dumps(record, indent=2))
    else:
        print(build.stdout + build.stderr)
    report["summary"] = {status: sum(row["status"] == status for row in report["results"]) for status in ("pass", "fail")}
    report["status"] = "pass" if build.returncode == 0 and report["summary"]["fail"] == 0 else "fail"
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("VISION_SUMMARY", report["summary"], "build", build.returncode)
    return int(report["status"] != "pass")


if __name__ == "__main__":
    raise SystemExit(main())
