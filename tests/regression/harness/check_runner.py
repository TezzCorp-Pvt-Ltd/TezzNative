"""Exercise CI failure handling independently of the language under test."""
import importlib.util
import os
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("verification_runner", ROOT / "tests/run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def process(exit_code=0, stdout="", stderr="", timeout=False):
    return {"exit": exit_code, "stdout": stdout, "stderr": stderr, "timeout": timeout}


class FailureContracts(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "build")
        self.base = Path(self.temp.name)
        self.source = self.base / "fixture.tn"
        self.source.write_text("fn main() -> int:\n  ret 0\n", encoding="utf-8")
        self.diag = self.source.with_suffix(".diag.txt")

    def tearDown(self):
        self.temp.cleanup()

    def run_fixture(self, steps, mode="runtime", backend="bc"):
        with patch.object(runner, "execute", side_effect=steps):
            return runner.run_case(self.source, mode, self.diag, backend,
                                   Path("fake-compiler"), self.base / "out", {}, 1)

    def test_zero_exit_cannot_hide_assertion_failure(self):
        for output in ("FAIL: numerical result\n", "ASSERTION_FAILURES 2\n"):
            with self.subTest(output=output):
                self.assertEqual(self.run_fixture([process(), process(stdout=output)])["status"], "fail")

    def test_stderr_failure_is_fatal(self):
        self.assertEqual(self.run_fixture([process(), process(stderr="FAIL invariant\n")])["status"], "fail")

    def test_recovering_sanitizer_is_fatal_even_with_zero_exit(self):
        record = runner.execute([sys.executable, "-c",
                                 "import sys; print('runtime error: signed integer overflow', file=sys.stderr)"],
                                self.base, os.environ.copy(), 5)
        self.assertEqual(record["exit"], 0)
        self.assertEqual(self.run_fixture([process(), record])["status"], "fail")

    def test_sanitizer_cannot_satisfy_expected_rejection(self):
        self.diag.write_text("expected diagnostic\n", encoding="utf-8")
        record = process(1, stderr="expected diagnostic\nERROR: AddressSanitizer: invalid access")
        record["sanitizer_failure"] = True
        self.assertEqual(self.run_fixture([record], "reject", "check")["status"], "fail")

    def test_crash_cannot_satisfy_rejection(self):
        self.diag.write_text("expected diagnostic\n", encoding="utf-8")
        for exit_code in (-11, 3221225477):
            with self.subTest(exit_code=exit_code):
                result = self.run_fixture([process(exit_code, "expected diagnostic")], "reject", "check")
                self.assertEqual(result["status"], "fail")

    def test_timeout_cannot_satisfy_rejection(self):
        self.diag.write_text("expected diagnostic\n", encoding="utf-8")
        result = self.run_fixture([process(None, "expected diagnostic", timeout=True)], "reject", "check")
        self.assertEqual(result["status"], "fail")

    def test_empty_diagnostic_expectation_is_invalid(self):
        self.diag.write_text("\n", encoding="utf-8")
        self.assertEqual(self.run_fixture([process(1)], "reject", "check")["status"], "fail")

    def test_controlled_diagnostic_rejection_passes(self):
        self.diag.write_text("expected diagnostic\n", encoding="utf-8")
        result = self.run_fixture([process(1, stderr="expected diagnostic\n")], "reject", "check")
        self.assertEqual(result["status"], "pass")

    def test_output_mismatch_is_fatal(self):
        self.source.with_suffix(".stdout.txt").write_text("expected\n", encoding="utf-8")
        self.assertEqual(self.run_fixture([process(), process(stdout="unexpected\n")])["status"], "fail")

    def test_successful_build_must_produce_executable(self):
        self.assertEqual(self.run_fixture([process(), process()], backend="native")["status"], "fail")

    def test_failed_build_cannot_run_stale_executable(self):
        folder = self.base / "out/cases" / self.source.relative_to(ROOT).with_suffix("")
        folder.mkdir(parents=True)
        executable = folder / ("program.exe" if os.name == "nt" else "program")
        executable.write_bytes(b"stale artifact")
        self.assertEqual(self.run_fixture([process(), process(1)], backend="native")["status"], "fail")
        self.assertFalse(executable.exists())


if __name__ == "__main__":
    unittest.main()
