"""Bounded TLS checks against generated, local-only certificate fixtures."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import socket
import ssl
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
CLIENT = Path(__file__).resolve().parent / "support/client.tn"


def run(command, env, cwd, timeout=15):
    started = time.monotonic()
    try:
        result = subprocess.run([str(x) for x in command], env=env, cwd=cwd,
                                capture_output=True, text=True, timeout=timeout)
        return {"exit": result.returncode, "stdout": result.stdout,
                "stderr": result.stderr, "seconds": round(time.monotonic() - started, 3)}
    except subprocess.TimeoutExpired:
        return {"exit": None, "stdout": "", "stderr": "timeout",
                "seconds": round(time.monotonic() - started, 3)}


def certificate(out, name):
    key, cert = out / (name + ".key"), out / (name + ".pem")
    openssl = shutil.which("openssl")
    if not openssl and os.name == "nt":
        candidate = Path("C:/Program Files/Git/usr/bin/openssl.exe")
        if candidate.is_file():
            openssl = str(candidate)
    if not openssl:
        raise RuntimeError("The OpenSSL command-line tool is required for local TLS certificates")
    command = [openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
               "-keyout", key, "-out", cert, "-subj", "/CN=" + name,
               "-addext", "subjectAltName=DNS:" + name]
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=20)
    if result.returncode:
        raise RuntimeError(result.stderr)
    der = ssl.PEM_cert_to_DER_cert(cert.read_text())
    return key, cert, hashlib.sha256(der).hexdigest()


def serve(key, cert, maximum, stalled):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    if maximum == 12:
        context.maximum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, key)
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(4)
    port = listener.getsockname()[1]
    timing = {}

    def worker():
        try:
            with listener:
                raw, _ = listener.accept()
                started = time.monotonic()
                with raw:
                    raw.settimeout(3)
                    if stalled:
                        raw.settimeout(1.2)
                        try:
                            while raw.recv(65536):
                                pass
                        except OSError:
                            pass
                        timing["network_seconds"] = round(time.monotonic() - started, 3)
                        return
                    try:
                        with context.wrap_socket(raw, server_side=True) as secured:
                            secured.sendall(b"okay")
                            secured.recv(1)
                    except (ssl.SSLError, OSError):
                        pass  # Expected when the client rejects certificate or policy.
        except OSError:
            pass

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    return port, thread, timing


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--backend", choices=("bc", "native", "both"), default="both")
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/tls")
    args = parser.parse_args()
    compiler, out = args.compiler.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["TEZZ_SDK_ROOT"] = str(SDK)
    checked = run([compiler, "check", CLIENT], env, out)
    if checked["exit"] != 0:
        raise RuntimeError("TLS client type check failed:\n" + checked["stdout"] + checked["stderr"])
    key, cert, pin = certificate(out, "localhost")
    wrong_key, wrong_cert, _ = certificate(out, "wrong.invalid")
    empty = out / "empty-trust"
    empty.mkdir(exist_ok=True)
    backends = ("bc", "native") if args.backend == "both" else (args.backend,)
    executable = out / ("client.exe" if os.name == "nt" else "client")
    if "native" in backends:
        result = run([compiler, "buildexe", CLIENT, executable, "--verify"], env, out)
        if result["exit"] != 0:
            raise RuntimeError("native TLS client build failed:\n" + result["stdout"] + result["stderr"])
    # Windows' Schannel uses the OS trust store; these fixtures never modify it.
    cases = [
        ("untrusted", False, "localhost", 0, 0, 0, "", 0, 0, False),
        ("trusted", True, "localhost", 0, 0, 0, "", 1, 0, False),
        ("wrong_dns", True, "localhost", 0, 0, 0, "", 0, 0, False),
        ("wrong_ip", True, "127.0.0.1", 0, 0, 0, "", 0, 0, False),
        ("pin", True, "localhost", 1, 12, 500, pin, 1, 0, False),
        ("bad_pin", True, "localhost", 1, 12, 500, "0" * 64, 0, 0, False),
        ("global_pin", True, "localhost", 2, 12, 500, "0" * 64, 0, 0, False),
        ("global_minimum", True, "localhost", 2, 13, 500, "", 0, 12, False),
        ("request_minimum", True, "localhost", 1, 13, 500, "", 0, 12, False),
        ("request_overrides", True, "localhost", 3, 12, 500, pin, 1, 12, False),
        ("global_timeout", True, "localhost", 2, 12, 100, "", 0, 0, True),
        ("request_timeout", True, "localhost", 1, 12, 100, "", 0, 0, True),
    ]
    results = []
    for backend in backends:
        for name, trusted, host, mode, minimum, timeout, fingerprint, expected, maximum, stalled in cases:
            if os.name == "nt" and name != "untrusted":
                results.append({"backend": backend, "case": name, "status": "skip",
                                "reason": "isolated CA trust is OpenSSL-only; system trust store stays unchanged"})
                continue
            server_key, server_cert = (wrong_key, wrong_cert) if name == "wrong_dns" else (key, cert)
            port, thread, timing = serve(server_key, server_cert, maximum, stalled)
            config = env.copy()
            config.update(SSL_CERT_FILE=str(server_cert if trusted else empty / "missing.pem"),
                          SSL_CERT_DIR=str(empty), TEZZ_TLS_HOST=host, TEZZ_TLS_PORT=str(port),
                          TEZZ_TLS_MODE=str(mode), TEZZ_TLS_MIN=str(minimum),
                          TEZZ_TLS_TIMEOUT=str(timeout), TEZZ_TLS_PIN=fingerprint,
                          TEZZ_TLS_EXPECT=str(expected))
            command = [compiler, "run", CLIENT, "--bc"] if backend == "bc" else [executable]
            result = run(command, config, out, timeout=8)
            thread.join(timeout=1.5)
            result.update(timing)
            passed = result["exit"] == 0 and (not stalled or timing.get("network_seconds", 99) < 0.9)
            result.update(backend=backend, case=name, status="pass" if passed else "fail")
            results.append(result)
            print(result["status"].upper(), backend, name, result["exit"], result["seconds"], flush=True)
    summary = {status: sum(r["status"] == status for r in results) for status in ("pass", "fail", "skip")}
    (out / "results.json").write_text(json.dumps({"compiler": str(compiler), "results": results,
                                                "summary": summary}, indent=2) + "\n")
    print("TLS_SUMMARY", summary)
    return 1 if summary["fail"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
