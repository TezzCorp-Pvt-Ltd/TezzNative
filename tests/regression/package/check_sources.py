"""Type-check every distributed library through its public module import."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
UNAVAILABLE = {
    "arduino": "module 'arduino' is unavailable: its legacy API is not implemented; use arduino.uno with --target arduino-uno --freestanding",
    "kernel": "module 'kernel' is unavailable: its legacy API is not implemented; use hardware with an explicit freestanding target",
    "raspi": "module 'raspi' is unavailable: its legacy API is not implemented; use raspi.pi3 with --target raspi3b --freestanding",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/library-sources")
    parser.add_argument("--filter", default="")
    args = parser.parse_args()
    compiler, out = args.compiler.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    names = sorted(source.stem for source in (SDK / "lib").glob("*.tn"))
    selected = [name for name in names if args.filter in name]
    if not selected:
        parser.error("no matching library modules")
    env = dict(os.environ)
    env["TEZZ_SDK_ROOT"] = str(SDK)
    with tempfile.TemporaryDirectory(prefix="library-sources-", dir=out) as temporary:
        project = Path(temporary)
        manifest = "name = library_verification\nversion = 0.1.0\nmodule_root = "
        manifest += os.path.relpath(SDK / "lib", project).replace("\\", "/") + "\n"
        manifest += "".join(f"dep.{name} = 0.1.0\n" for name in names)
        (project / "tezz.mod").write_text(manifest, encoding="utf-8")

        def check(name):
            source = project / f"check_{name}.tn"
            source.write_text(f'import "{name}"\n\nfn main() -> int:\n  ret 0\n', encoding="utf-8")
            try:
                target_args = ["--target", "pc-x86_64", "--freestanding"] if name == "hardware" else []
                result = subprocess.run([str(compiler), "check", str(source), *target_args], cwd=project,
                                        env=env, capture_output=True, text=True, timeout=45)
                expected = UNAVAILABLE.get(name)
                status = "pass" if result.returncode == 0 else "fail"
                if expected:
                    diagnostic_lines = [line for line in result.stderr.splitlines() if "tezzc error:" in line]
                    status = "unsupported" if result.returncode != 0 and len(diagnostic_lines) == 1 and diagnostic_lines[0].endswith("tezzc error: " + expected) else "fail"
                return {"module": name, "status": status,
                        "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
            except subprocess.TimeoutExpired:
                return {"module": name, "status": "fail", "reason": "type checking timed out"}

        with ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(check, selected))
    failures = [result for result in results if result["status"] == "fail"]
    report = {"compiler": str(compiler), "results": results,
              "summary": {"pass": sum(result["status"] == "pass" for result in results), "fail": len(failures),
                          "unsupported": sum(result["status"] == "unsupported" for result in results)}}
    (out / "results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    for failure in failures:
        print(failure["module"], failure.get("stderr", failure.get("reason", "")))
    print("LIBRARY_SOURCE_SUMMARY", report["summary"])
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
