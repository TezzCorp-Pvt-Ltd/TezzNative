"""Compare real CPU tensor conversions to Python's independent IEEE packer."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def cases():
    values = [0.0, -0.0, 1.0, -1.0, math.inf, -math.inf, math.nan,
              1 + 2**-24, 1 + 3 * 2**-24, 2**-149, 2**-150,
              3 * 2**-150, -2**-150, 2**-126, 2**-126 - 2**-150,
              2**128 - 2**104, 2**128 - 2**103,
              math.nextafter(2**128 - 2**103, 0), 1e300, -1e300,
              math.nextafter(0.0, 1.0), -math.nextafter(0.0, 1.0)]
    raw = [struct.unpack("<Q", struct.pack("<d", value))[0] for value in values]
    rng = random.Random(8237)
    raw.extend(rng.getrandbits(64) for _ in range(1024))
    rows = []
    for bits in raw:
        value = struct.unpack("<d", struct.pack("<Q", bits))[0]
        try:
            encoded = struct.pack("<f", value)
        except OverflowError:
            rows.append((bits, 0, 0, 0))
            continue
        rounded = struct.unpack("<f", encoded)[0]
        expected32 = struct.unpack("<I", encoded)[0]
        expected64 = struct.unpack("<Q", struct.pack("<d", rounded))[0]
        rows.append((bits, expected32, expected64, 2 if math.isnan(value) else 1))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/tensor-interop-numeric")
    args = parser.parse_args()
    compiler, output = args.compiler.resolve(), args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    env["LD_LIBRARY_PATH"] = str(compiler.parent) + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    rows = cases()
    corpus = struct.pack("<Q", len(rows)) + b"".join(struct.pack("<QQQQ", *row) for row in rows)
    source = Path(__file__).with_name("support") / "numeric.tn"
    inputs = [Path(__file__), source, ROOT / "lib/tensor_bridge.tn", SDK / "lib/tensor_bridge.tn",
              SDK / "lib/tztensor.tn", SDK / "lib/tzautodiff.tn"]
    cpu_library = compiler.parent / ("tzcpu.dll" if os.name == "nt" else "libtzcpu-native.so")
    results = []
    report = {"compiler": str(compiler), "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
              "scope": "real CPU binary64/binary32 owning conversions; no GPU adapter",
              "oracle": "Python struct IEEE float packing, deterministic seed 8237",
              "cases_per_backend": len(rows), "corpus_sha256": hashlib.sha256(corpus).hexdigest(),
              "expected_per_backend": {"finite_overflow_rejections": sum(row[3] == 0 for row in rows),
                                       "numeric_roundtrips": sum(row[3] == 1 for row in rows),
                                       "nan_roundtrips": sum(row[3] == 2 for row in rows)},
              "source_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs},
              "cpu_runtime_sha256": hashlib.sha256(cpu_library.read_bytes()).hexdigest() if cpu_library.is_file() else None,
              "results": results}
    for backend in ("bc", "native"):
        folder = output / backend
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "cases.bin").write_bytes(corpus)
        executable = folder / ("numeric.exe" if os.name == "nt" else "numeric")
        commands = ([[compiler, "run", source, "--bc"]] if backend == "bc" else
                    [[compiler, "buildexe", source, executable, "--verify"], [executable]])
        record = {"backend": backend, "status": "pass", "steps": []}
        for command in commands:
            try:
                result = subprocess.run(list(map(str, command)), cwd=folder, env=env,
                                        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90)
                step = {"command": list(map(str, command)), "exit": result.returncode,
                        "stdout": result.stdout, "stderr": result.stderr}
            except (OSError, subprocess.TimeoutExpired) as error:
                step = {"command": list(map(str, command)), "exit": None, "error": str(error)}
            record["steps"].append(step)
            if step["exit"] != 0:
                record["status"] = "fail"
                break
        if f"INTEROP_NUMERIC_CHECKS {len(rows)}" not in record["steps"][-1].get("stdout", ""):
            record["status"] = "fail"
        results.append(record)
        print(backend, record["status"], "cases", len(rows), flush=True)
        if record["status"] != "pass":
            print(json.dumps(record, indent=2))
        report["summary"] = {status: sum(row["status"] == status for row in results) for status in ("pass", "fail")}
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return int(report["summary"]["fail"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
