"""Compare the native password/session primitives with Python's hashlib oracle."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/security")
    parser.add_argument("--cc")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cc = args.cc or shutil.which("gcc") or shutil.which("cc")
    if not cc and os.name == "nt":
        cc = "C:/msys64/mingw64/bin/gcc.exe"
    library = out / ("security.dll" if os.name == "nt" else "security.so")
    command = [cc, "-shared", "-O1", "-std=gnu11", "-I" + str(SDK / "include"),
               str(SDK / "src/security.c"), str(SDK / "runtime/native_alloc.c"),
               "-o", str(library)]
    command += ["-static-libgcc", "-lbcrypt"] if os.name == "nt" else ["-fPIC", "-DTN_TLS_OPENSSL=1", "-lcrypto"]
    env = dict(os.environ)
    env["PATH"] = str(Path(cc).resolve().parent) + os.pathsep + env.get("PATH", "")
    build = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    native = ctypes.CDLL(str(library))
    free = native.tn_native_free
    free.argtypes = [ctypes.c_void_p]
    create = native.tn_security_password_hash
    create.argtypes, create.restype = [ctypes.c_char_p], ctypes.c_void_p
    verify = native.tn_security_password_verify
    verify.argtypes, verify.restype = [ctypes.c_char_p, ctypes.c_char_p], ctypes.c_longlong
    random = native.tn_security_random_token
    random.argtypes, random.restype = [], ctypes.c_void_p
    digest = native.tn_security_token_hash
    digest.argtypes, digest.restype = [ctypes.c_char_p], ctypes.c_void_p

    def owned(pointer):
        assert pointer, "unexpected allocation or cryptography failure"
        value = ctypes.string_at(pointer)
        free(pointer)
        return value

    password = b"regression password with UTF-8: \xc3\xa9"
    records = [owned(create(password)) for _ in range(2)]
    assert records[0] != records[1], "password salts must be random"
    for record in records:
        algorithm, iterations, salt, actual = record.split(b"$")
        assert algorithm == b"pbkdf2-sha256" and iterations == b"600000"
        expected = hashlib.pbkdf2_hmac("sha256", password, bytes.fromhex(salt.decode()), 600000)
        assert actual.decode() == expected.hex()
        assert verify(password, record) == 1
        assert verify(b"wrong password", record) == 0
    salt = bytes(range(16))
    expected = hashlib.pbkdf2_hmac("sha256", password, salt, 600000).hex()
    independent = f"pbkdf2-sha256$600000${salt.hex()}${expected}".encode()
    assert verify(password, independent) == 1
    invalid = [None, b"", b"1234567", independent[:-1], independent+b"0",
               independent.replace(b"600000", b"000001"),
               independent.replace(b"pbkdf2", b"PBKDF2"),
               independent[:20]+b"z"+independent[21:]]
    for record in invalid:
        assert verify(password, record) == 0
    for invalid_password in (None, b"", b"x"*1025):
        assert not create(invalid_password)
        assert verify(invalid_password, independent) == 0
    tokens = {owned(random()) for _ in range(100)}
    assert len(tokens) == 100
    for token in tokens:
        assert len(token) == 64 and len(bytes.fromhex(token.decode())) == 32
        assert owned(digest(token)).decode() == hashlib.sha256(token).hexdigest()
    for invalid_token in (None, b"", b"x"*64, b"0"*63, b"0"*65, b"A"*64):
        assert not digest(invalid_token)
    result = {"status": "pass", "password_oracle_records": 3,
              "invalid_records": len(invalid), "random_tokens": len(tokens),
              "source_sha256": hashlib.sha256((SDK / "src/security.c").read_bytes()).hexdigest()}
    (out / "results.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
