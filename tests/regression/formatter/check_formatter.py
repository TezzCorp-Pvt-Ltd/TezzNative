"""Formatter control-flow preservation, non-mutating checks, and rejected options."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/formatter")
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    results = []

    def run(*arguments, status=0):
        result = subprocess.run([str(args.compiler.resolve()), *map(str, arguments)],
                                cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        if result.returncode != status:
            raise AssertionError(f"{arguments}: expected {status}, got {result.returncode}\n{result.stdout}{result.stderr}")
        return result

    def verify(name, action):
        try:
            action()
            results.append({"name": name, "status": "pass"})
        except Exception as error:
            results.append({"name": name, "status": "fail", "error": str(error)})

    with tempfile.TemporaryDirectory(prefix="formatter-", dir=output) as temporary:
        directory = Path(temporary)
        original = (ROOT / "tests/regression/formatter/else_if.tn").read_bytes()

        def branches():
            path = directory / "branches.tn"
            path.write_bytes(original)
            run("run", path, "--bc")
            run("fmt", path)
            formatted = path.read_bytes()
            run("check", path)
            run("run", path, "--bc")
            run("fmt", path)
            assert path.read_bytes() == formatted, "formatter is not idempotent"
        verify("else_if_all_paths_preserved_and_idempotent", branches)

        def check_mode():
            path = directory / "check.tn"
            path.write_bytes(original)
            modified = path.stat().st_mtime_ns
            run("fmt", path, "--check", status=1)
            assert path.read_bytes() == original, "--check modified the unformatted source"
            assert path.stat().st_mtime_ns == modified, "--check rewrote the unformatted source"
            run("fmt", path)
            formatted = path.read_bytes()
            modified = path.stat().st_mtime_ns
            run("fmt", path, "--check")
            assert path.read_bytes() == formatted, "--check modified the formatted source"
            assert path.stat().st_mtime_ns == modified, "--check rewrote the formatted source"
        verify("check_mode_reports_changes_without_writing", check_mode)

        for option in ("--chek", "--unknown", "unexpected.tn"):
            def reject(flag=option):
                path = directory / "unknown.tn"
                path.write_bytes(original)
                run("fmt", path, flag, status=1)
                assert path.read_bytes() == original, "unknown option modified the source"
            verify("reject_option_" + option, reject)

        def malformed():
            path = directory / "invalid.tn"
            data = b"fn main() -> int:\n  else if:\n"
            path.write_bytes(data)
            run("fmt", path, status=1)
            assert path.read_bytes() == data, "parse failure modified the source"
        verify("invalid_source_preserved", malformed)

        def existing_format_contract():
            path = directory / "existing.tn"
            source = ROOT / "tests/conformance/dx/fmt/control_flow.input.tn"
            expected = ROOT / "tests/conformance/dx/fmt/control_flow.formatted.tn"
            path.write_bytes(source.read_bytes())
            run("fmt", path)
            assert path.read_text() == expected.read_text(), "existing formatting contract changed"
            run("fmt", path, "--check")
        verify("existing_control_flow_format_contract", existing_format_contract)

        def speech_module():
            path = directory / "speech_text.tn"
            path.write_bytes((ROOT / "lib/tts/text.tn").read_bytes())
            run("fmt", path)
            formatted = path.read_bytes()
            run("fmt", path, "--check")
            assert path.read_bytes() == formatted
        verify("actual_speech_module_format_is_valid_and_idempotent", speech_module)

        def pointer_casts():
            path = directory / "cast_precedence.tn"
            path.write_bytes((ROOT / "tests/regression/formatter/cast_precedence.tn").read_bytes())
            run("check", path)
            run("run", path, "--bc")
            run("fmt", path)
            formatted = path.read_bytes()
            run("check", path)
            run("run", path, "--bc")
            executable = directory / ("cast_precedence.exe" if os.name == "nt" else "cast_precedence")
            run("buildexe", path, executable, "--verify")
            subprocess.run([str(executable)], cwd=ROOT, env=env, check=True, capture_output=True, timeout=60)
            run("fmt", path, "--check")
            assert path.read_bytes() == formatted
        verify("pointer_address_dereference_and_unary_casts_preserve_behavior", pointer_casts)

    report = {"results": results, "passed": sum(r["status"] == "pass" for r in results),
              "failed": sum(r["status"] == "fail" for r in results)}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if report["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
