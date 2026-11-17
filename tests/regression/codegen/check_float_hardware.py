"""Check reduced-float boundary bits in actual QEMU firmware on three CPUs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from check_reduced import ROOT, SUPPORT, bits32, expected

sys.path.insert(0, str(ROOT/"tools"))
from hardware.build import build
from hardware.run import run_qemu


def fixture():
    # Reuse only Tezz conversion entry points; expected values come from the
    # independent mathematical/struct oracles, never the compiler's helpers.
    body = (SUPPORT/"reduced_float.tn").read_text(encoding="utf-8")
    helpers = body[body.index("fn decode("):body.index("fn main(")]
    lines, checks = [helpers, "fn main() -> int:"], []
    raw_values = {0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
                  0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000,
                  0x7f800001, 0xff800001, 0x7fc00000, 0xffc00000}
    # Positive/negative smallest subnormals, ties to zero, ties to odd/even
    # mantissas, finite limits and overflowing inputs for each representation.
    for value in (2**-25, 2**-24, 2**-17, 2**-16, 2**-10, 2**-9,
                  2**-134, 2**-133, 1.00048828125, 1.00146484375,
                  1.00390625, 1.01171875, 1.0625, 1.1875, 1.125, 1.375,
                  65504.0, 65520.0, 448.0, 464.0, 57344.0, 61440.0):
        raw = bits32(value)
        raw_values.update((raw, raw | 0x80000000))
    decode_patterns = ((0, 1, 2, 0x3ff, 0x400, 0x3c00, 0x7bff, 0x7c00, 0x7c01, 0x7e00),
                       (0, 1, 2, 0x7f, 0x80, 0x3f80, 0x7f7f, 0x7f80, 0x7f81, 0x7fc0),
                       (0, 1, 2, 7, 8, 0x38, 0x7d, 0x7e, 0x7f),
                       (0, 1, 2, 3, 4, 0x3c, 0x7b, 0x7c, 0x7d, 0x7f))
    for mode in range(4):
        sign = 0x8000 if mode < 2 else 0x80
        checks.extend((mode, value | negative) for value in decode_patterns[mode] for negative in (0, sign))
        checks.extend((mode+4, raw) for raw in sorted(raw_values))
    for index, (operation, raw) in enumerate(checks, 1):
        mode = operation if operation < 4 else operation-4
        function = "decode" if operation < 4 else "encode"
        answer = expected(operation, raw)
        lines.append(f"  actual_{index}:int = {function}({mode}, {raw})")
        actual = f"actual_{index}"
        if answer is not None:
            condition = f"{actual} != {answer}"
        elif operation < 4:
            condition = f"({actual} & 2139095040) != 2139095040 || ({actual} & 8388607) == 0"
        elif mode < 2:
            exponent, mantissa = (0x7c00, 0x3ff) if mode == 0 else (0x7f80, 0x7f)
            sign = (raw >> 16) & 0x8000
            condition = f"({actual} & 32768) != {sign} || ({actual} & {exponent}) != {exponent} || ({actual} & {mantissa}) == 0"
        else:
            condition = f"{actual} != {((raw >> 24) & 128) | 127}"
        lines.append(f"  if {condition}: ret {index}")
    lines.append("  ret 0")
    return "\n".join(lines)+"\n", len(checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT/"build/verification/float-hardware")
    args = parser.parse_args()
    compiler, output = args.compiler.resolve(), args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = output/"reduced_boundaries.tn"
    text, count = fixture()
    source.write_text(text, encoding="utf-8")
    records = []
    for target in ("pc-x86_64", "riscv64-virt", "raspi3b"):
        record = {"target": target, "status": "fail", "bit_checks": count}
        try:
            product = build(target, source, compiler, output/target)
            result = run_qemu(target, output/target)
            record.update(status=result["status"], build=product, run=result)
        except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
            record["error"] = str(error)
        records.append(record)
        print(target, record["status"], flush=True)
    record = {"target": "arduino-uno", "case": "explicit_float_rejection", "status": "fail"}
    try:
        artifact = output/"avr-must-not-exist.c"
        artifact.unlink(missing_ok=True)
        env = dict(os.environ, TEZZ_SDK_ROOT=str(ROOT/"TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"))
        process = subprocess.run([str(compiler), "buildir", str(source), str(artifact),
                                  "--target", "arduino-uno", "--freestanding"], cwd=ROOT, env=env,
                                 capture_output=True, text=True, timeout=60)
        passed = (process.returncode != 0 and not artifact.exists()
                  and "arduino-uno: floating-point runtime" in process.stderr)
        record.update(status="pass" if passed else "fail", exit=process.returncode,
                      stderr=process.stderr, artifact_absent=not artifact.exists())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        record["error"] = str(error)
    records.append(record)
    print("arduino-uno explicit_float_rejection", record["status"], flush=True)
    report = {"compiler": str(compiler), "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
              "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "physical_hardware": False, "results": records,
              "summary": {"pass": sum(row["status"] == "pass" for row in records),
                          "fail": sum(row["status"] != "pass" for row in records)}}
    (output/"results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    for row in records:
        if row["status"] != "pass": print(json.dumps(row, indent=2))
    print("FLOAT_HARDWARE_SUMMARY", report["summary"])
    return bool(report["summary"]["fail"])


if __name__ == "__main__":
    raise SystemExit(main())
