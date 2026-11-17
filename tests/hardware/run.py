"""Gate explicit hardware profiles using real QEMU/simavr execution only."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
sys.path.insert(0, str(ROOT / "tools"))
from hardware.build import build
from hardware.profiles import PROFILES
from hardware.run import run_qemu

# Explicit manifests keep deliberate failure fixtures separate from success runs.
QEMU_CASES = [
    ("pc_core", "pc-x86_64", "pc_core.tn", 0, None, "TEZZ_PC_CORE_OK\n"),
    ("pc_failure", "pc-x86_64", "pc_failure.tn", 7, None, "TEZZ_HW_EXIT=7\n"),
    ("pc_uart", "pc-x86_64", "pc_uart.tn", 0, b"A\0z", "A\0z\nTEZZ_HW_UART_OK\n"),
    ("rv64_core", "riscv64-virt", "rv64_core.tn", 0, None, "TEZZ_RV64_CORE_OK\n"),
    ("rv64_failure", "riscv64-virt", "rv64_failure.tn", 7, None, "TEZZ_HW_EXIT=7\n"),
    ("rv64_uart", "riscv64-virt", "rv64_uart.tn", 0, b"A\0z", "A\0z\nTEZZ_HW_UART_OK\n"),
    ("pi3_gpio", "raspi3b", "pi3_gpio.tn", 0, None, "PI3_GPIO_TIMER_IRQ_OK\n"),
    ("pi3_uart", "raspi3b", "pi3_uart.tn", 0, b"ping", "ping\n"),
    ("pi3_float", "raspi3b", "pi3_float.tn", 0, None, "PI3_FLOAT_OK\n"),
    *[(target + "_generic_core", target, "core.tn", 0, None, "TEZZ_HW_CORE_OK\n")
      for target in ("pc-x86_64", "riscv64-virt", "raspi3b")],
    *[(target + "_generic_float", target, "floating.tn", 0, None, "TEZZ_HW_FLOAT_OK\n")
      for target in ("pc-x86_64", "riscv64-virt", "raspi3b")],
]


def execute(command, env, timeout, cwd=ROOT):
    started = time.monotonic()
    try:
        process = subprocess.run([str(arg) for arg in command], cwd=cwd, env=env,
                                 capture_output=True, text=True, encoding="utf-8",
                                 errors="replace", timeout=timeout)
        result = {"exit": process.returncode, "stdout": process.stdout, "stderr": process.stderr}
    except subprocess.TimeoutExpired as error:
        def decode(value):
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else (value or "")
        result = {"exit": None, "timeout": True,
                  "stdout": decode(error.stdout), "stderr": decode(error.stderr)}
    result.update(command=[str(arg) for arg in command], seconds=round(time.monotonic()-started, 3))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/hardware")
    parser.add_argument("--timeout", type=float, default=20, help="per-guest execution limit in seconds")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    compiler, output = args.compiler.resolve(), args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    records = []
    report = {"compiler": str(compiler), "host": platform.platform(),
              "physical_hardware": False, "execution": "QEMU and simavr instruction/peripheral emulation",
              "results": records}

    def record(name, category, function):
        item = {"case": name, "category": category, "status": "fail"}
        try:
            detail = function()
            item.update(detail)
        except (RuntimeError, OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
            item["error"] = str(error)
        records.append(item)
        print(name, item["status"], flush=True)
        return item

    def dependencies():
        if not compiler.is_file():
            raise RuntimeError("A source-built compiler file is required")
        names = {"gcc", "grub-mkrescue", "grub-file", "xorriso"}
        for profile in PROFILES.values():
            names.update(profile[key] for key in ("cc", "objcopy", "size") if key in profile)
            if "emulator" in profile:
                names.add(profile["emulator"])
        tools = {name: shutil.which(name) for name in sorted(names)}
        missing = [name for name, path in tools.items() if path is None]
        if missing:
            raise RuntimeError("Required hardware dependencies are missing: " + ", ".join(missing))
        report["compiler_sha256"] = hashlib.sha256(compiler.read_bytes()).hexdigest()
        return {"status": "pass", "tools": tools}

    def helper(name, minimum):
        folder = output / name
        result = execute([sys.executable, ROOT / f"tests/hardware/{name}.py",
                          *(["--compiler", compiler] if name in ("check_uno", "check_tooling") else []),
                          "--out", folder], env, 240)
        evidence = folder / "results.json"
        nested = json.loads(evidence.read_text(encoding="utf-8")) if evidence.is_file() else {}
        rows, summary = nested.get("results", []), nested.get("summary", {})
        passed = (result["exit"] == 0 and len(rows) >= minimum
                  and summary.get("fail") == 0 and summary.get("pass") == len(rows)
                  and all(row.get("status") == "pass" for row in rows))
        return {"status": "pass" if passed else "fail", "result": result,
                "evidence": str(evidence), "summary": summary, "checks": rows}

    def guest(target, source, expected, data, marker, folder):
        product = build(target, ROOT / "tests/hardware" / source, compiler, folder)
        result = run_qemu(target, folder, expected, args.timeout, data)
        passed = result["status"] == "pass" and marker in result["stdout"]
        return {"status": "pass" if passed else "fail", "target": target,
                "build": product, "run": result}

    def aggregate_c():
        folder = output / "hosted_c_aggregate"
        folder.mkdir(parents=True, exist_ok=True)
        generated, obj, executable = folder / "aggregate.c", folder / "aggregate.o", folder / "aggregate"
        fixture = ROOT / "tests/regression/codegen/support/aggregate_c.tn"
        entry = ROOT / "tests/regression/codegen/support/c_entry.c"
        commands = [
            [compiler, "buildir", fixture, generated, "--target", "c", "--freestanding"],
            [shutil.which("gcc"), "-std=c11", "-O2", "-Dmain=tezz_entry", "-c", generated, "-o", obj],
            [shutil.which("gcc"), obj, entry, "-o", executable],
            [executable],
        ]
        steps = []
        for command in commands:
            result = execute(command, env, max(args.timeout, 60))
            steps.append(result)
            if result["exit"] != 0:
                return {"status": "fail", "steps": steps}
        return {"status": "pass", "steps": steps,
                "fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
                "generated_sha256": hashlib.sha256(generated.read_bytes()).hexdigest(),
                "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest()}

    def rejection(arguments, diagnostic, artifact=None):
        if artifact is not None:
            artifact.unlink(missing_ok=True)
        result = execute([compiler, *arguments], env, 45)
        passed = result["exit"] is not None and result["exit"] != 0
        passed = passed and diagnostic in result["stderr"]
        passed = passed and (artifact is None or not artifact.exists())
        return {"status": "pass" if passed else "fail", "result": result,
                "diagnostic": diagnostic, "artifact_absent": artifact is None or not artifact.exists()}

    try:
        setup = record("dependencies", "setup", dependencies)
        if setup["status"] == "pass":
            record("uno_peripherals_and_core", "simavr", lambda: helper("check_uno", 9))
            record("driver_failure_paths", "driver", lambda: helper("check_driver", 13))
            record("hardware_tooling", "tooling", lambda: helper("check_tooling", 48))
            if len(QEMU_CASES) != 15:
                raise RuntimeError("The QEMU execution manifest must contain all 15 required cases")
            for name, target, source, expected, data, marker in QEMU_CASES:
                record(name, "qemu", lambda t=target, s=source, e=expected, d=data, m=marker, n=name:
                       guest(t, s, e, d, m, output / n))
            record("hosted_c_aggregate", "hosted_c", aggregate_c)

            negative = output / "compiler_rejections"
            negative.mkdir(parents=True, exist_ok=True)
            core = ROOT / "tests/hardware/core.tn"
            simple = ROOT / "tests/hardware/pc_failure.tn"
            artifact = negative / "unknown_profile.c"
            record("unknown_profile", "compiler_rejection", lambda:
                   rejection(["buildir", simple, artifact, "--target", "pc-x86_65", "--freestanding"],
                             "target not supported", artifact))
            for target in PROFILES:
                artifact = negative / (target + "_without_freestanding.c")
                record(target + "_requires_freestanding", "compiler_rejection", lambda t=target, a=artifact:
                       rejection(["buildir", core, a, "--target", t],
                                 "hardware targets require --freestanding", a))
                for backend in ("--bc", "--native"):
                    record(target + "_rejects_hosted_run_" + backend[2:], "compiler_rejection", lambda t=target, b=backend:
                           rejection(["run", core, b, "--target", t, "--freestanding"],
                                     "not hosted execution"))
                artifact = negative / (target + "_hosted_executable")
                record(target + "_rejects_buildexe", "compiler_rejection", lambda t=target, a=artifact:
                       rejection(["buildexe", core, a, "--target", t, "--freestanding"],
                                 "not hosted execution", a))
            artifact = negative / "wrong_board_module.c"
            record("wrong_board_module", "compiler_rejection", lambda:
                   rejection(["buildir", ROOT / "tests/hardware/pi3_gpio.tn", artifact,
                              "--target", "pc-x86_64", "--freestanding"],
                             "hardware module requires its matching --target profile", artifact))
            record("hardware_module_without_profile", "compiler_rejection", lambda:
                   rejection(["check", core, "--freestanding"],
                             "hardware module requires its matching --target profile"))
    except (RuntimeError, OSError, ValueError) as error:
        records.append({"case": "gate_setup", "category": "setup", "status": "fail", "error": str(error)})

    report["summary"] = {"pass": sum(row["status"] == "pass" for row in records),
                         "fail": sum(row["status"] != "pass" for row in records)}
    leaves = []
    for row in records:
        checks = row.get("checks") or []
        leaves.extend(checks or [row])
        if checks and row["status"] != "pass" and all(check.get("status") == "pass" for check in checks):
            leaves.append(row)
    report["individual_checks"] = {"pass": sum(row.get("status") == "pass" for row in leaves),
                                   "fail": sum(row.get("status") != "pass" for row in leaves)}
    report["categories"] = {}
    for row in records:
        count = report["categories"].setdefault(row["category"], {"pass": 0, "fail": 0})
        count["pass" if row["status"] == "pass" else "fail"] += 1
    (output / "results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print("HARDWARE_SUMMARY", report["summary"], report["categories"])
    for row in records:
        if row["status"] != "pass":
            print(json.dumps(row, indent=2))
    return int(not records or report["summary"]["fail"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
