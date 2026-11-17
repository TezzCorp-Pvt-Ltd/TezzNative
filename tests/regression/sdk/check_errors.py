"""Verify library errors actually stop VM and native programs with failure."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/sdk-errors")
    args = parser.parse_args()
    compiler, out = args.compiler.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"))
    source = ROOT / "tests/regression/sdk/support/result_expect_error.tn"
    executable = out / ("expect.exe" if os.name == "nt" else "expect")
    subprocess.run([str(compiler), "buildexe", str(source), str(executable), "--verify"],
                   env=env, check=True, timeout=60)
    results = []
    for backend, command in (("bc", [str(compiler), "run", str(source), "--bc"]),
                              ("native", [str(executable)])):
        result = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, timeout=15)
        assert result.returncode == 1, (backend, result.returncode, result.stdout, result.stderr)
        assert result.stdout.splitlines() == ["expected failure", "original error"], (backend, result.stdout)
        results.append({"backend": backend, "case": "result_expect error", "status": "pass"})
    (out / "results.json").write_text(json.dumps({"results": results, "pass": len(results)}, indent=2)+"\n")
    print("SDK_ERROR_SUMMARY", len(results), "passed")


if __name__ == "__main__":
    main()
