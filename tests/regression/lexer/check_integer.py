"""Independent boundary oracle for integer/float token conversion."""
import math
import subprocess
import sys

integers = {"0": 0, "9223372036854775807": 2**63-1,
            "9223372036854775808": -(2**63), "18446744073709551615": -1,
            "0xFFFFFFFFFFFFFFFF": -1, "0x8000000000000000": -(2**63),
            "0b" + "1"*64: -1, "18_446_744_073_709_551_615": -1}
floats = {"1_000.5": 1000.5, "1.5e1_0": 1.5e10,
          "100000000000000000000000000000000000000.0": 1e38,
          "0." + "0"*140 + "1e140": 0.1}
invalid = ["18446744073709551616", "0x10000000000000000", "0b1" + "0"*64,
           "9"*400, "0x", "0b___"]
for token, expected in integers.items():
    p = subprocess.run([sys.argv[1], token], capture_output=True, text=True, timeout=5)
    assert p.returncode == 0 and p.stdout.strip() == f"integer {expected}", (token, p.stdout, p.stderr)
for token, expected in floats.items():
    p = subprocess.run([sys.argv[1], token], capture_output=True, text=True, timeout=5)
    assert p.returncode == 0 and p.stdout.startswith("float "), (token, p.stdout, p.stderr)
    assert math.isclose(float(p.stdout.split()[1]), expected, rel_tol=2e-15), (token, p.stdout)
for token in invalid:
    p = subprocess.run([sys.argv[1], token], capture_output=True, text=True, timeout=5)
    assert p.returncode != 0, (token, p.stdout, p.stderr)
print("lexer_integer_float_pass", len(integers)+len(floats)+len(invalid))
