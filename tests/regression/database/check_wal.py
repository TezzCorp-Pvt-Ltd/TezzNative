"""Independently construct sealed, incomplete and malformed database redo logs."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
PAGE = 8192
MAGIC = 1464421428


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/database-wal")
    args = parser.parse_args()
    compiler, out = args.compiler.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT / "tests/regression/database/support/wal_probe.tn"
    executable = out / ("wal.exe" if os.name == "nt" else "wal")
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    subprocess.run([str(compiler), "buildexe", str(source), str(executable), "--verify"],
                   cwd=ROOT, env=env, check=True, timeout=60)
    results = []
    with tempfile.TemporaryDirectory(prefix="wal-", dir=out) as temporary:
        project = Path(temporary)
        for backend in ("bc", "native"):
            def run(mode, directory):
                command = ([str(compiler), "run", str(source), "--bc", "--", mode]
                           if backend == "bc" else [str(executable), mode])
                result = subprocess.run(command, cwd=directory, env=env, capture_output=True,
                                        text=True, timeout=45)
                assert result.returncode == 0, (backend, mode, result.returncode, result.stdout, result.stderr)

            seed = project / f"{backend}-seed"
            seed.mkdir()
            run("seed", seed)
            original = (seed / "data.tdb").read_bytes()
            assert len(original) % PAGE == 0
            leaf = struct.unpack_from("<I", original, PAGE + 20 + 56)[0]
            assert original[leaf*PAGE+28:leaf*PAGE+31] == b"old"
            changed = bytearray(original)
            changed[leaf*PAGE+28:leaf*PAGE+31] = b"new"

            def records(data):
                return b"".join(struct.pack("<II", MAGIC, page) + data[page*PAGE:(page+1)*PAGE]
                                for page in range(len(data)//PAGE))

            footer = struct.pack("<II", MAGIC, 0xFFFFFFFF)
            redo = records(changed)
            malformed = bytearray(changed)
            struct.pack_into("<I", malformed, leaf*PAGE+12, 0x7FFFFFFF)
            cases = [
                ("sealed", redo + footer, "new"),
                ("unsealed", redo, "old"),
                ("short-record", redo[:-5], "old"),
                ("short-footer", redo + footer[:5], "old"),
                ("empty", b"", "old"),
                ("invalid-page", records(malformed) + footer, "reject"),
                ("missing-header", redo[PAGE+8:] + footer, "reject"),
            ]
            for name, wal, expected in cases:
                directory = project / f"{backend}-{name}"
                directory.mkdir()
                (directory / "data.tdb").write_bytes(original)
                (directory / "data.tdb.wal").write_bytes(wal)
                run(expected, directory)
                persisted = (directory / "data.tdb").read_bytes()
                assert persisted == (bytes(changed) if expected == "new" else original), (backend, name)
                results.append({"backend": backend, "case": name, "status": "pass"})
    (out / "results.json").write_text(json.dumps({"results": results, "pass": len(results)}, indent=2)+"\n")
    print("DATABASE_WAL_SUMMARY", len(results), "passed")


if __name__ == "__main__":
    main()
