"""Verify library manifests, recursive archives, and isolated local installs."""
import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import library_package as package
import tezz_lib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/library-packaging")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    compiler = args.compiler.resolve()
    sources = package.verified_library_sources(ROOT)
    for library in (ROOT / "lib", ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk/lib"):
        for source in library.rglob("*"):
            if source.is_file() and source.suffix in {".tn", ".c", ".cu", ".cuh", ".h"}:
                assert len(source.read_text(encoding="utf-8").splitlines()) <= 1000, source
    assert "nn/core.tn" in sources and "tensor/state.tn" in sources
    assert "tztensor.tn" in sources, "SDK-only libraries must be distributed"
    checks = []
    with tempfile.TemporaryDirectory(prefix="library-package-", dir=out) as temporary:
        work = Path(temporary)
        archive = work / "libraries.zip"
        with zipfile.ZipFile(archive, "w") as writer:
            for name, source in sources.items():
                writer.write(source, "lib/" + name)
        with zipfile.ZipFile(archive) as reader:
            assert len(reader.namelist()) == len(sources)
            for name, source in sources.items():
                assert reader.read("lib/" + name) == source.read_bytes()
        checks.append("recursive archive contains every public and SDK-only source")
        project = work / "project"
        project.mkdir()
        for name in ("io", "nn", "tensor", "tensor_bridge", "net", "hardware", "arduino", "raspi"):
            assert tezz_lib.add_library(name, project)
        checks.append("local installation includes complete import closures")
        (project / "lib/nn/core.tn").unlink()
        assert tezz_lib.add_library("nn", project, overwrite=False)
        assert (project / "lib/nn/core.tn").is_file()
        checks.append("sync repairs missing components with matching source checksums")
        env = dict(os.environ)
        env["TEZZ_SDK_ROOT"] = str(project)
        env["TEZZ_SDK_DIR"] = str(project)
        for module, target in (("hardware", "pc-x86_64"), ("arduino.uno", "arduino-uno"), ("raspi.pi3", "raspi3b")):
            source = project / "hardware_import.tn"
            source.write_text(f'import "{module}"\nfn main() -> int:\n  ret 0\n', encoding="utf-8")
            result = subprocess.run([str(compiler), "check", str(source), "--target", target, "--freestanding"],
                                    cwd=project, env=env, capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, result.stdout + result.stderr
            checks.append("isolated hardware module installation: " + module)
        cases = ["tests/conformance/native/io_stream_api.tn",
                 "tests/conformance/native/net_http_url.tn",
                 "tests/regression/nn/cross_entropy_extreme.tn",
                  "tests/regression/tensor/gradient_finite_difference.tn",
                  "tests/regression/tensor_bridge/roundtrip.tn"]
        for index, case in enumerate(cases):
            source = project / f"case_{index}.tn"
            source.write_bytes((ROOT / case).read_bytes())
            executable = project / (f"case_{index}.exe" if os.name == "nt" else f"case_{index}")
            for command in ([compiler, "run", source, "--bc"],
                            [compiler, "buildexe", source, executable, "--verify"],
                            [executable]):
                result = subprocess.run([str(arg) for arg in command], cwd=project,
                                        env=env, capture_output=True, text=True, timeout=60)
                assert result.returncode == 0, result.stdout + result.stderr
            checks.append("isolated VM/native execution: " + case)
        changed = project / "lib/nn/core.tn"
        changed.write_text("// locally modified\n", encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()):
            assert not tezz_lib.add_library("nn", project, overwrite=False)
        assert changed.read_text(encoding="utf-8") == "// locally modified\n"
        checks.append("sync rejects modified components without overwriting them")
        for invalid in ("../escape", "x/y", "", "..", "C:\\escape"):
            with contextlib.redirect_stdout(io.StringIO()):
                assert not tezz_lib.add_library(invalid, project)
        checks.append("installation rejects path traversal names")
        bad = project / "lib/unsafe.tn"
        bad.write_text("// package-file unsafe/../../escape.tn 00000000\n", encoding="utf-8")
        try:
            package.read_components(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("component traversal accepted")
        checks.append("component manifests reject traversal")
    result = {"status": "pass", "library_sources": len(sources), "checks": checks}
    (out / "results.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
