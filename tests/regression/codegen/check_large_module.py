"""Execute modules beyond former fixed native call/branch relocation limits."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def sources():
    prefix = "let counter:int = 0\nfn bump() -> int:\n  counter += 1\n  i:int = 0\n  while i < counter:\n    if i == 2: break\n    i += 1\n  ret counter\n\n"
    for count in (512, 513, 1100):
        yield "calls_"+str(count), prefix+"fn main() -> int:\n"+"  bump()\n"*count+f"  if counter != {count}: ret 1\n  ret 0\n"
    body = prefix+"fn main() -> int:\n"
    body += "".join(f"  operation_{index}:fn() -> int = bump\n  operation_{index}()\n" for index in range(600))
    body += "  if counter != 600: ret 1\n  ret 0\n"
    yield "addresses_600", body
    body = "fn branches(value:int) -> int:\n  found:int = 0\n"
    body += "".join(f"  if value == {index}: found += 1\n" for index in range(600))
    body += "  ret found\nfn main() -> int:\n  if branches(599) != 1: ret 1\n  if branches(-1) != 0: ret 2\n  ret 0\n"
    yield "branches_600", body
    body = "fn returns(value:int) -> int:\n"
    body += "".join(f"  if value == {index}: ret {index+1}\n" for index in range(600))
    body += "  ret -1\nfn main() -> int:\n  if returns(599) != 600: ret 1\n  if returns(-1) != -1: ret 2\n  ret 0\n"
    yield "returns_600", body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT/"build/verification/large-module")
    args = parser.parse_args()
    compiler, output = args.compiler.resolve(), args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(ROOT/"TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"))
    records = []
    for name, source in sources():
        path = output/(name+".tn")
        path.write_text(source, encoding="utf-8")
        executable = output/(name+(".exe" if os.name == "nt" else ".out"))
        for lane in ("bc", "native"):
            record = {"case": name, "lane": lane, "status": "fail", "steps": []}
            commands = [[compiler, "run", path, "--bc"]] if lane == "bc" else [
                [compiler, "buildexe", path, executable, "--verify"], [executable]]
            try:
                for command in commands:
                    result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                                            capture_output=True, text=True, timeout=60)
                    step = {"command": list(map(str, command)), "exit": result.returncode,
                            "stdout": result.stdout, "stderr": result.stderr}
                    record["steps"].append(step)
                    if result.returncode:
                        raise RuntimeError(json.dumps(step))
                record["status"] = "pass"
            except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
                record["error"] = str(error)
            records.append(record)
            print(name, lane, record["status"], flush=True)
    report = {"compiler": str(compiler), "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
              "results": records, "summary": {"pass": sum(row["status"] == "pass" for row in records),
                                               "fail": sum(row["status"] != "pass" for row in records)}}
    (output/"results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print("LARGE_MODULE_SUMMARY", report["summary"])
    for row in records:
        if row["status"] != "pass": print(row["case"], row["lane"], row.get("error"))
    return bool(report["summary"]["fail"])


if __name__ == "__main__":
    raise SystemExit(main())
