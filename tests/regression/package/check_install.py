"""Exercise the actual TN package CLI with isolated cached, offline packages."""
import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path


def checksum(data):
    value = 5381
    for byte in data.replace(b"\r\n", b"\n"):
        value = (value * 33 + byte) & 0x7fffffff
    return f"{value:08X}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", nargs="?")
    parser.add_argument("--compiler", dest="compiler_option")
    parser.add_argument("--out")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    compiler_arg = args.compiler_option or args.compiler
    if not compiler_arg: parser.error("a compiler is required")
    compiler = str(Path(compiler_arg).resolve())
    results = []
    output = Path(args.out).resolve() if args.out else None
    if output: output.mkdir(parents=True, exist_ok=True)
    tool = str(root / "tools/tezz.tn")
    env = os.environ.copy()
    env["TEZZ_SDK_ROOT"] = str(root / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk")
    bounds_source = (Path(__file__).with_name("support") / "name_version_bounds.tn").relative_to(root)
    bounds = subprocess.run([compiler, "run", str(bounds_source), "--bc"],
                            cwd=root, env=env, capture_output=True, text=True, timeout=45)
    assert bounds.returncode == 0, (bounds.stdout, bounds.stderr)
    version = "1.2.3-alpha+build.7"
    content = {"sample/core.tn": b"fn value() -> int:\n  ret 7\n",
               "sample/ops.tn": b"fn double(x:int) -> int:\n  ret x * 2\n"}
    standard = "".join(f"// package-file {name} {checksum(data)}\n" for name, data in content.items())
    cases = ["happy", "corrupt", "missing", "traversal", "foreign", "duplicate", "modified_remove", "oversized_name"]
    for case in cases:
        with tempfile.TemporaryDirectory(prefix="tezz-package-") as temp:
            project = Path(temp)
            sdk = project / "sdk"
            cache = project / ".tezz/cache"
            cache.mkdir(parents=True)
            metadata = "name = package_fixture\nversion = 0.1.0\n"
            (project / "tezz.mod").write_text(metadata)
            manifest = standard
            if case == "traversal": manifest = f"// package-file sample/../escape.tn {checksum(b'x')}\n"
            if case == "foreign": manifest = f"// package-file other/core.tn {checksum(b'x')}\n"
            if case == "duplicate": manifest += standard.splitlines()[0] + "\n"
            facade = (manifest + "// offline package fixture\n").encode()
            (cache / f"sample-{version}.tn").write_bytes(facade)
            for name, data in content.items():
                if case == "missing" and name.endswith("ops.tn"): continue
                target = sdk / "lib" / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data + (b"corrupt" if case == "corrupt" and name.endswith("ops.tn") else b""))

            def run(command):
                process = subprocess.run([compiler, "run", tool, "--bc", "--", *command,
                                          "--sdk-root", str(sdk)], cwd=project, env=env,
                                         capture_output=True, text=True, timeout=45)
                return process

            package = "a" * 200 + "@1.0.0" if case == "oversized_name" else "sample@" + version
            result = run(["get", package, "offline", checksum(facade)])
            if case not in ("happy", "modified_remove"):
                assert result.returncode != 0, (case, result.stdout, result.stderr)
                assert (project / "tezz.mod").read_text() == metadata, case
                assert not (project / "lib/sample.tn").exists(), case
                assert not (project / "lib/sample/core.tn").exists(), case
                assert not (project / "escape.tn").exists(), case
            else:
                assert result.returncode == 0, (case, result.stdout, result.stderr)
                assert (project / "lib/sample.tn").read_bytes() == facade
                for name, data in content.items(): assert (project / "lib" / name).read_bytes() == data
                assert "dep.sample" in (project / "tezz.mod").read_text()
                assert checksum(facade) in (project / "tezz.lock").read_text()
                unrelated = project / "lib/sample/keep.txt"
                unrelated.write_text("user data")
                if case == "modified_remove":
                    (project / "lib/sample/core.tn").write_text("user modifications")
                    before = (project / "tezz.mod").read_text()
                result = run(["remove", "sample"])
                if case == "modified_remove":
                    assert result.returncode != 0, (result.stdout, result.stderr)
                    assert (project / "tezz.mod").read_text() == before
                    assert (project / "lib/sample/ops.tn").exists()
                else:
                    assert result.returncode == 0, (result.stdout, result.stderr)
                    assert not (project / "lib/sample.tn").exists()
                    for name in content: assert not (project / "lib" / name).exists()
                    assert "dep.sample" not in (project / "tezz.mod").read_text()
                assert unrelated.read_text() == "user data"
            print("package_case_pass", case, flush=True)
            results.append({"case": case, "status": "pass"})
    print("package_install_pass", len(cases))
    if output:
        (output / "results.json").write_text(json.dumps({"compiler": compiler, "results": results,
                                                         "summary": {"pass": len(results), "fail": 0}}, indent=2) + "\n")


if __name__ == "__main__":
    main()
