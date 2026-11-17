"""Format disposable copies and lint the original hardware-profile fixtures."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
PROFILES = ("arduino-uno", "raspi3b", "pc-x86_64", "riscv64-virt")
PREFIXES = ("// package-file ", "// tezz-hardware-module: ", "// tezz-unavailable-module: ")
FACADES = (("lib/hardware.tn", "pc-x86_64"),
           ("lib/arduino/uno.tn", "arduino-uno"),
           ("lib/raspi/pi3.tn", "raspi3b"))
# These real package facades also exercise checksummed inventory and legacy
# unavailable markers. They are formatted as copies, never semantically enabled.
METADATA_FACADES = (("lib/arduino.tn", "arduino-uno"), ("lib/raspi.tn", "raspi3b"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/hardware-tooling")
    args = parser.parse_args()
    compiler, output = args.compiler.resolve(), args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    results, excluded, originals = [], [], {}

    def invoke(command, source, target):
        process = subprocess.run([str(compiler), command, str(source), "--target", target, "--freestanding"],
                                 cwd=ROOT, env=env, capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", timeout=45)
        return {"command": [str(compiler), command, str(source), "--target", target, "--freestanding"],
                "exit": process.returncode, "stdout": process.stdout, "stderr": process.stderr}

    def check(name, action):
        record = {"case": name, "status": "fail"}
        try:
            record.update(action())
        except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
            record["error"] = str(error)
        results.append(record)
        print(name, record["status"], flush=True)

    def snapshot(source):
        data = source.read_bytes()
        originals[source] = hashlib.sha256(data).hexdigest()
        return data

    def metadata(data):
        return [line for line in data.decode("utf-8").splitlines() if line.startswith(PREFIXES)]

    def format_copy(source, target):
        original = snapshot(source)
        copy = output / "formatted" / source.relative_to(ROOT)
        copy.parent.mkdir(parents=True, exist_ok=True)
        copy.write_bytes(original)
        first = invoke("fmt", copy, target)
        formatted = copy.read_bytes()
        expected = metadata(original)
        retained = metadata(formatted) == expected
        if expected:
            retained = retained and formatted.decode("utf-8").splitlines()[:len(expected)] == expected
        second = invoke("fmt", copy, target)
        stable = copy.read_bytes() == formatted
        passed = first["exit"] == 0 and second["exit"] == 0 and retained and stable
        return {"status": "pass" if passed else "fail", "source": str(source), "copy": str(copy),
                "target": target, "first": first, "second": second,
                "critical_metadata": expected, "metadata_retained": retained, "idempotent": stable,
                "formatted_sha256": hashlib.sha256(formatted).hexdigest()}

    def lint_original(source, target):
        result = invoke("lint", source, target)
        warnings = re.findall(r"tezzc warning: lint\[([^]]+)\]", result["stderr"])
        summary = re.fullmatch(r"WARN: .+ \(([0-9]+) warnings\)\r?\n?", result["stdout"])
        diagnostics = [line for line in result["stderr"].splitlines() if "tezzc " in line]
        # Assertion values and imports that select a board are deliberate in
        # these fixtures. Accept only the linter's structured warning result;
        # semantic errors, missing guard support and new warning IDs still fail.
        known_warnings = (result["exit"] == 1 and summary is not None
                          and int(summary.group(1)) == len(warnings) > 0
                          and len(diagnostics) == len(warnings)
                          and set(warnings) <= {"magic-number", "unused-import"})
        clean = result["exit"] == 0 and not warnings and not diagnostics
        return {"status": "pass" if clean or known_warnings else "fail", "result": result,
                "warning_count": len(warnings), "warning_ids": sorted(set(warnings))}

    def targets(source):
        if source.name == "core.tn":
            return PROFILES
        if source.name == "floating.tn":
            return PROFILES[1:]
        for prefix, profile in (("uno_", "arduino-uno"), ("pi3_", "raspi3b"),
                                ("pc_", "pc-x86_64"), ("rv64_", "riscv64-virt")):
            if source.name.startswith(prefix):
                return (profile,)
        raise RuntimeError("Hardware tooling fixture has no explicit profile: " + str(source))

    try:
        if not compiler.is_file():
            raise RuntimeError("A source-built compiler file is required")
        fixtures = sorted((ROOT / "tests/hardware").glob("*.tn"))
        if len(fixtures) < 19:
            raise RuntimeError("Hardware tooling coverage requires all 19 existing TN fixtures")
        for relative, target in (*FACADES, *METADATA_FACADES):
            source = ROOT / relative
            check("format:" + relative, lambda s=source, t=target: format_copy(s, t))
        for source in fixtures:
            profiles = targets(source)
            check("format:" + source.name, lambda s=source, t=profiles[0]: format_copy(s, t))
            if source.name == "uno_float_rejected.tn":
                excluded.append({"source": str(source), "operation": "lint",
                                 "reason": "Intentional unsupported-float diagnostic is asserted by check_uno.py"})
                continue
            for target in profiles:
                check("lint:" + source.name + ":" + target,
                      lambda s=source, t=target: lint_original(s, t))

        def originals_unchanged():
            changed = [str(source) for source, digest in originals.items()
                       if hashlib.sha256(source.read_bytes()).hexdigest() != digest]
            return {"status": "fail" if changed else "pass", "changed": changed,
                    "original_sha256": {str(source): digest for source, digest in originals.items()}}
        check("repository_sources_unchanged", originals_unchanged)
    except (OSError, ValueError, RuntimeError) as error:
        results.append({"case": "setup", "status": "fail", "error": str(error)})

    report = {"compiler": str(compiler), "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest()
              if compiler.is_file() else None, "results": results, "excluded": excluded,
              "summary": {"pass": sum(row["status"] == "pass" for row in results),
                          "fail": sum(row["status"] != "pass" for row in results)}}
    (output / "results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print("HARDWARE_TOOLING_SUMMARY", report["summary"])
    for row in results:
        if row["status"] != "pass":
            print(json.dumps(row, indent=2))
    return int(not results or report["summary"]["fail"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
