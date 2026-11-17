"""Independent bit-level oracle for the locale-C decimal runtime primitive."""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import locale
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc and os.name == "nt":
        cc = "C:/msys64/mingw64/bin/gcc.exe"
    library = args.out / ("decimal.dll" if os.name == "nt" else "decimal.so")
    command = [cc, "-shared", "-std=gnu11", "-O1", "-I" + str(SDK / "include"),
               str(SDK / "src/decimal.c"), "-o", str(library)]
    command += ["-static-libgcc"] if os.name == "nt" else ["-fPIC"]
    env = dict(os.environ)
    env["PATH"] = str(Path(cc).resolve().parent) + os.pathsep + env.get("PATH", "")
    built = subprocess.run(command, capture_output=True, text=True, timeout=60, env=env)
    if built.returncode:
        raise RuntimeError(str(command) + " exit=" + str(built.returncode) + "\n" + built.stdout + built.stderr)
    dll = ctypes.CDLL(str(library.resolve()))
    convert = dll.tn_decimal_f64
    convert.argtypes = [ctypes.c_void_p, ctypes.c_longlong, ctypes.POINTER(ctypes.c_double)]
    convert.restype = ctypes.c_longlong
    cases = []
    failures = []

    def check(text, valid, expected=None, length=None):
        raw = text.encode("ascii")
        output = ctypes.c_double(123.5)
        size = len(raw) if length is None else length
        status = convert(raw, size, ctypes.byref(output))
        passed = status == 0 if valid else status == -1 and output.value == 123.5
        if valid:
            expected = float(text) if expected is None else expected
            passed = passed and struct.pack("<d", output.value) == struct.pack("<d", expected)
        label = text if len(text) < 100 else text[:40] + "...[" + str(len(text)) + " chars]"
        record = {"input": label, "length": size, "status": int(status),
                  "bits": struct.pack("<d", output.value).hex(), "pass": bool(passed)}
        cases.append(record)
        if not passed:
            failures.append(record)

    for text in ["0", "-0", "1.25", "-2.5e2", "100e-310", "5e-324", "-5e-324",
                 "2.2250738585072014e-308", "1.7976931348623157e308",
                 "9007199254740993.0", "0.1000000000000000055511151231257827021181583404541015625",
                 "1.00000000000000011102230246251565404236316680908203125",
                 "1.00000000000000033306690738754696212708950042724609375",
                 "0.000000000000000000123000", "-0.00000e+99999999999999999",
                 "1e-99999999999999999999999", "-1e-99999999999999999999999",
                 "1" + "0" * 10000 + "e-10000"]:
        check(text, True)
    for text in ["", "-", "+1", "01", ".1", "1.", "1e", "1e+", "0x1p0", "nan", "Inf",
                 "1,25", " 1", "1 ", "1\x002", "1e309", "-1e309",
                 "1e9999999999999999999999999999999999999999999"]:
        check(text, False)
    check("123trailing", True, 123.0, 3)
    check("1", False, length=-1)
    check("1", False, length=1024 * 1024 + 1)
    output = ctypes.c_double(123.5)
    if convert(None, 1, ctypes.byref(output)) != -1 or output.value != 123.5:
        failures.append({"null_input": True})
    if convert(b"1", 1, None) != -1:
        failures.append({"null_output": True})

    previous = locale.setlocale(locale.LC_NUMERIC)
    changed = None
    try:
        for candidate in ("de_DE.UTF-8", "fr_FR.UTF-8", "German_Germany.1252", "French_France.1252"):
            try:
                locale.setlocale(locale.LC_NUMERIC, candidate)
                changed = candidate
                break
            except locale.Error:
                pass
        check("1.25", True)
        check("1,25", False)
    finally:
        locale.setlocale(locale.LC_NUMERIC, previous)
    report = {"source": str(SDK / "src/decimal.c"),
              "source_sha256": hashlib.sha256((SDK / "src/decimal.c").read_bytes()).hexdigest(),
              "build": command,
              "non_c_locale": changed, "cases": cases,
              "summary": {"pass": len(cases) - sum(not case["pass"] for case in cases),
                          "fail": len(failures)}}
    (args.out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("DECIMAL_ORACLE", report["summary"])
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())

