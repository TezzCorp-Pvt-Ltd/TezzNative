"""Compare SDK BC/native HOTP calls with an independent Python HMAC oracle."""
import argparse
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import random
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def expected_code(key, counter, digits):
    digest = hmac.new(key, struct.pack(">Q", counter), hashlib.sha1).digest()
    offset = digest[-1] & 15
    value = int.from_bytes(digest[offset:offset + 4], "big") & 0x7fffffff
    return str(value % (10 ** digits)).zfill(digits)


def corpus():
    rng = random.Random(42266238)
    keys = [b"12345678901234567890"]
    keys += [bytes(rng.randrange(256) for _ in range(size)) for size in (16, 17, 19, 31, 32, 64, 80)]
    counters = (0, 1, 9, 2147483648, 4294967303, 9223372036854775807)
    cases = []
    for index, key in enumerate(keys):
        secret = base64.b32encode(key).decode("ascii")
        if index % 3 == 1:
            secret = secret.rstrip("=").lower()
        elif index % 3 == 2:
            secret = secret.rstrip("=")
        for counter in counters:
            for digits in (6, 7, 8):
                cases.append((secret, counter, digits, expected_code(key, counter, digits)))
    return cases


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/otp-oracle")
    args = parser.parse_args()
    compiler, out = args.compiler.resolve(), args.out.resolve()
    project = out / "project"
    project.mkdir(parents=True, exist_ok=True)
    names = sorted(path.stem for path in (SDK / "lib").glob("*.tn"))
    manifest = "name = otp_oracle\nversion = 0.1.0\nmodule_root = "
    manifest += os.path.relpath(SDK / "lib", project).replace("\\", "/") + "\n"
    manifest += "".join(f"dep.{name} = 0.1.0\n" for name in names)
    (project / "tezz.mod").write_text(manifest, encoding="utf-8")
    cases = corpus()
    source = '''import "otp"

fn check(secret:str, counter:int, digits:int, expected:str) -> int:
  actual:str = otp.generate_hotp(secret, counter, digits)
  if actual == 0 || strcmp(actual, expected) != 0:
    say "FAIL: HOTP differential", counter, digits
    ret 1
  // The exact counter must also survive the 5-argument verification ABI.
  matched:int = otp.tn_security_otp_match(secret, actual, counter, 0, digits)
  unsafe:
    free(actual)
  if matched != counter: ret 2
  ret 0

fn main() -> int:
'''
    # Every expected value is produced independently by Python's HMAC-SHA1.
    for secret, counter, digits, expected in cases:
        source += f'  if check("{secret}", {counter}, {digits}, "{expected}") != 0: ret 1\n'
    source += '  say "OTP_ORACLE_OK", ' + str(len(cases)) + '\n  ret 0\n'
    entry = project / "oracle.tn"
    entry.write_text(source, encoding="utf-8")
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    results = []

    def execute(command):
        try:
            result = subprocess.run([str(value) for value in command], cwd=project, env=env,
                                    capture_output=True, text=True, timeout=90)
            return {"command": [str(value) for value in command], "exit": result.returncode,
                    "stdout": result.stdout, "stderr": result.stderr}
        except subprocess.TimeoutExpired:
            return {"command": [str(value) for value in command], "exit": None, "timeout": True}

    for backend in ("bc", "native"):
        steps = [execute([compiler, "check", entry])]
        if steps[-1]["exit"] == 0:
            if backend == "bc":
                steps.append(execute([compiler, "run", entry, "--bc"]))
            else:
                binary = project / ("oracle.exe" if os.name == "nt" else "oracle")
                steps.append(execute([compiler, "buildexe", entry, binary, "--verify"]))
                if steps[-1]["exit"] == 0:
                    steps.append(execute([binary]))
        passed = all(step["exit"] == 0 for step in steps)
        passed = passed and f"OTP_ORACLE_OK {len(cases)}" in steps[-1].get("stdout", "")
        results.append({"backend": backend, "status": "pass" if passed else "fail", "steps": steps})
    report = {"compiler": str(compiler), "cases_per_backend": len(cases), "results": results,
              "summary": {"pass": sum(item["status"] == "pass" for item in results),
                          "fail": sum(item["status"] == "fail" for item in results)}}
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("OTP_ORACLE_SUMMARY", report["summary"], "cases", len(cases))
    for item in results:
        if item["status"] != "pass":
            print(item["backend"], item["steps"][-1])
    return bool(report["summary"]["fail"])


if __name__ == "__main__":
    raise SystemExit(main())
