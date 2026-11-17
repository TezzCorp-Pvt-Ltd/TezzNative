"""Exercise the local development server through real HTTP on both backends."""
import argparse
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/http-server")
    args = parser.parse_args()
    compiler, out = args.compiler.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    source = ROOT / "tests/regression/sdk/support/server_main.tn"
    executable = out / ("server.exe" if os.name == "nt" else "server")
    subprocess.run([str(compiler), "buildexe", str(source), str(executable), "--verify"],
                   cwd=ROOT, env=env, check=True, timeout=60)
    results = []
    with tempfile.TemporaryDirectory(prefix="http-", dir=out) as temporary:
        base = Path(temporary)
        public = base / "public"
        public.mkdir()
        profile = base / "profile"
        (profile / ".tezznative").mkdir(parents=True)
        env["USERPROFILE"] = str(profile)
        payload = bytes(range(256)) * 600
        (public / "stream.bin").write_bytes(payload)
        (public / "a'&b.txt").write_text("safe content", encoding="utf-8")
        (public / ".env").write_text("PRIVATE", encoding="utf-8")
        (public / "secret.TDB").write_text("PRIVATE", encoding="utf-8")
        (base / "outside.txt").write_text("OUTSIDE", encoding="utf-8")
        linked = True
        try:
            (public / "outside-link.txt").symlink_to(base / "outside.txt")
            (public / "hidden-link.txt").symlink_to(public / ".env")
        except OSError:
            linked = False
        for backend in ("bc", "native"):
            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", 0))
                port = reservation.getsockname()[1]
            command = ([str(compiler), "run", str(source), "--bc", "--", str(port)]
                       if backend == "bc" else [str(executable), str(port)])
            with (out / f"{backend}.log").open("w", encoding="utf-8") as log:
                process = subprocess.Popen(command, cwd=public, env=env, stdout=log, stderr=log)
                try:
                    deadline = time.monotonic() + 15
                    while True:
                        if process.poll() is not None:
                            raise AssertionError(f"{backend} server exited {process.returncode}: see {backend}.log")
                        try:
                            with socket.create_connection(("127.0.0.1", port), timeout=.2):
                                break
                        except OSError:
                            if time.monotonic() >= deadline:
                                raise AssertionError(f"{backend} server did not listen")
                            time.sleep(.05)

                    def request(path, expected, headers=None, method="GET"):
                        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                        try:
                            connection.request(method, path, headers=headers or {})
                            response = connection.getresponse()
                            body = response.read()
                            assert response.status == expected, (backend, path, response.status, body)
                            results.append({"backend": backend, "case": path, "status": "pass"})
                            return body
                        finally:
                            connection.close()

                    assert request("/stream.bin", 200) == payload
                    listing = request("/", 200).decode()
                    assert "a&#39;&amp;b.txt" in listing and "a%27%26b.txt" in listing, (backend, listing)
                    assert "secret.TDB" not in listing and ".env" not in listing
                    assert request("/" + quote("a'&b.txt"), 200) == b"safe content"
                    assert json.loads(request("/health", 200))["port"] == port
                    for path in ("/../outside.txt", "/%2e%2e/outside.txt", "/x%00", "/.env", "/x%5cy", "/x%gg"):
                        request(path, 400)
                    request("/missing.txt", 404)
                    request("/secret.TDB", 404)
                    request("/api/db?table=users", 403)
                    request("/api/otp?action=generate", 503)
                    request("/health", 403, {"Host": "external.example"})
                    request("/health", 403, {"Origin": "https://external.example"})
                    request("/health", 405, method="POST")
                    if linked:
                        request("/outside-link.txt", 404)
                        request("/hidden-link.txt", 404)
                    else:
                        results.append({"backend": backend, "case": "symlink containment", "status": "skip",
                                        "reason": "host does not permit creating symlinks"})
                finally:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=10)
    report = {"compiler": str(compiler), "results": results,
              "pass": sum(item["status"] == "pass" for item in results),
              "skip": sum(item["status"] == "skip" for item in results)}
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("HTTP_SERVER_SUMMARY", report["pass"], "passed;", report["skip"], "skipped")


if __name__ == "__main__":
    main()
