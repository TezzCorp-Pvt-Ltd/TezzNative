"""Failure-path tests for the build/emulator driver, independent of board tests."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from hardware.build import build
from hardware.run import run_qemu


class Process:
    def __init__(self, stdout=b"", stderr=b"", code=0):
        self.stdin, self.stdout, self.stderr = io.BytesIO(), io.BytesIO(stdout), io.BytesIO(stderr)
        self.returncode = code

    def poll(self):
        return self.returncode

    def wait(self, timeout=None):
        if self.returncode is None:
            raise subprocess.TimeoutExpired("emulator", timeout)
        return self.returncode

    def terminate(self):
        self.returncode = -15

    def kill(self):
        self.returncode = -9


class DriverFailures(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name)
        self.source, self.compiler = self.out / "source.tn", self.out / "compiler"
        self.source.write_text("fn main() -> int:\n  ret 0\n", encoding="utf-8")
        self.compiler.write_bytes(b"test fixture, never executed")
        self.image, self.elf = self.out / "firmware.iso", self.out / "firmware.elf"
        self.image.write_bytes(b"prior image")
        self.elf.write_bytes(b"prior elf")
        self.report = {"target": "pc-x86_64", "status": "pass", "image": str(self.image),
                       "elf": str(self.elf), "image_sha256": self.digest(self.image),
                       "elf_sha256": self.digest(self.elf)}
        self.save()

    @staticmethod
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def save(self):
        (self.out / "build.json").write_text(json.dumps(self.report), encoding="utf-8")

    def failed_rebuild(self, source=None):
        with patch("hardware.build.require_tool", side_effect=RuntimeError("missing tool")):
            with self.assertRaises(RuntimeError):
                build("pc-x86_64", source or self.source, self.compiler, self.out)
        self.assertFalse(self.image.exists(), "stale successful image survived failed rebuild")
        self.assertEqual(json.loads((self.out / "build.json").read_text())["status"], "fail")

    def test_missing_tool_invalidates_previous_build(self):
        self.failed_rebuild()

    def test_missing_source_invalidates_previous_build(self):
        self.failed_rebuild(self.out / "missing.tn")

    def test_changed_image_is_rejected_before_launch(self):
        self.image.write_bytes(b"modified")
        with patch("hardware.run.require_tool", side_effect=RuntimeError("emulator lookup reached")):
            with self.assertRaisesRegex(RuntimeError, "digest"):
                run_qemu("pc-x86_64", self.out)

    def test_changed_elf_is_rejected_before_launch(self):
        self.elf.write_bytes(b"modified")
        with patch("hardware.run.require_tool", side_effect=RuntimeError("emulator lookup reached")):
            with self.assertRaisesRegex(RuntimeError, "digest"):
                run_qemu("pc-x86_64", self.out)

    def execute(self, process, **kwargs):
        with patch("hardware.run.require_tool", return_value="test emulator"), \
             patch("hardware.run.subprocess.Popen", return_value=process):
            return run_qemu(self.report["target"], self.out, timeout=0.15, **kwargs)

    def test_zero_exit_without_marker_fails(self):
        self.assertEqual(self.execute(Process())["status"], "fail")

    def test_unexpected_guest_status_fails(self):
        self.assertEqual(self.execute(Process(b"TEZZ_HW_EXIT=7\n", code=35))["status"], "fail")

    def test_pc_wrong_test_device_exit_fails(self):
        self.assertEqual(self.execute(Process(b"TEZZ_HW_EXIT=0\n"))["status"], "fail")

    def test_pi_emulator_error_fails(self):
        self.report["target"] = "raspi3b"
        self.save()
        self.assertEqual(self.execute(Process(b"TEZZ_HW_EXIT=0\n", code=1))["status"], "fail")

    def test_output_limit_fails(self):
        self.assertEqual(self.execute(Process(b"x" * (1024 * 1024 + 4096)))["status"], "fail")

    def test_virt_wrong_test_device_exit_fails(self):
        self.report["target"] = "riscv64-virt"
        self.save()
        self.assertEqual(self.execute(Process(b"TEZZ_HW_EXIT=0\n", code=1))["status"], "fail")

    def test_missing_input_handshake_fails(self):
        self.assertEqual(self.execute(Process(b"TEZZ_HW_EXIT=0\n", code=33),
                                      input_after_ready=b"ping")["status"], "fail")

    def test_timeout_terminates_owned_process(self):
        process = Process(code=None)
        result = self.execute(process)
        self.assertEqual(result["status"], "fail")
        self.assertIn("timed out", result["reason"])
        self.assertEqual(process.returncode, -15)

    def test_invalid_timeout_rejected(self):
        for timeout in (0, -1, float("nan"), float("inf")):
            with self.subTest(timeout=timeout), self.assertRaisesRegex(ValueError, "timeout"):
                run_qemu("pc-x86_64", self.out, timeout=timeout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(DriverFailures)
    names = [test.id() for test in suite]
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    failures = {getattr(test, "test_case", test).id(): detail for test, detail in result.failures + result.errors}
    records = [{"case": name, "status": "fail" if name in failures else "pass",
                "error": failures.get(name)} for name in names]
    report = {"results": records, "summary": {"pass": result.testsRun - len(failures), "fail": len(failures)},
              "scope": "driver failure handling; test processes are protocol fixtures, not board emulation"}
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    raise SystemExit(not result.wasSuccessful())
