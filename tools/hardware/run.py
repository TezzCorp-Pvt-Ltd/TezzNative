"""Run a real QEMU board image and validate its explicit completion status."""
import hashlib
import json
import math
from pathlib import Path
import queue
import re
import subprocess
import threading
import time

from .build import require_tool
from .profiles import PROFILES


def run_qemu(target, output, expected=0, timeout=30, input_after_ready=None):
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("emulator timeout must be finite and positive")
    profile, output = PROFILES[target], Path(output).resolve()
    if "emulator" not in profile:
        raise RuntimeError("This target requires the simavr peripheral test harness")
    build = json.loads((output / "build.json").read_text(encoding="utf-8"))
    if build.get("status") != "pass" or build.get("target") != target:
        raise RuntimeError("A successful build of the selected target is required")
    for artifact in ("image", "elf"):
        path = Path(build[artifact])
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != build.get(artifact + "_sha256"):
            raise RuntimeError(f"Built {artifact} digest differs; rebuild before running")
    command = [require_tool(profile["emulator"])]
    command += [argument.format(image=build["image"], elf=build["elf"]) for argument in profile["qemu"]]
    command += ["-display", "none", "-monitor", "none", "-serial", "stdio", "-no-reboot"]
    started = time.monotonic()
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    messages = queue.Queue(maxsize=64)
    stopping = threading.Event()

    def read(stream, label):
        while not stopping.is_set():
            data = (stream.read1(4096) if hasattr(stream, "read1") else stream.read(4096)) or None
            while not stopping.is_set():
                try:
                    messages.put((label, data), timeout=0.1)
                    break
                except queue.Full:
                    continue
            if data is None:
                break

    readers = [threading.Thread(target=read, args=(stream, label), daemon=True)
               for stream, label in ((process.stdout, "stdout"), (process.stderr, "stderr"))]
    for reader in readers:
        reader.start()
    captured = {"stdout": bytearray(), "stderr": bytearray()}
    done, sent, status, reason = set(), False, None, "completion marker missing"
    try:
        while time.monotonic() - started < timeout:
            try:
                label, data = messages.get(timeout=0.1)
            except queue.Empty:
                if process.poll() is not None and len(done) == 2:
                    break
                continue
            if data is None:
                done.add(label)
                continue
            captured[label].extend(data)
            if sum(map(len, captured.values())) > 1024 * 1024:
                reason = "emulator output exceeded limit"
                break
            if input_after_ready is not None and not sent and b"TEZZ_HW_READY\n" in captured["stdout"]:
                process.stdin.write(input_after_ready)
                process.stdin.flush()
                sent = True
            match = re.search(rb"TEZZ_HW_EXIT=(-?[0-9]+)\r?\n", captured["stdout"])
            if match:
                status = int(match.group(1))
                reason = "target returned expected status" if status == expected else "target returned unexpected status"
                break
        else:
            reason = "emulator timed out"
        # PC and virt runtimes may exit through a documented emulator test
        # device. Pi firmware halts; stop only the emulator this function owns.
        if status is not None:
            try:
                process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                pass
    finally:
        stopping.set()
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
        for reader in readers:
            reader.join(timeout=1)
        while not messages.empty():
            label, data = messages.get_nowait()
            if data is not None:
                remaining = max(0, 1024 * 1024 - sum(map(len, captured.values())))
                captured[label].extend(data[:remaining])
        for stream in (process.stdin, process.stdout, process.stderr):
            stream.close()
    passed = status == expected and (input_after_ready is None or sent)
    if target == "pc-x86_64" and status is not None and process.returncode != (33 if status == 0 else 35):
        passed, reason = False, "PC did not exit through its expected test device"
    if target == "riscv64-virt" and status is not None and process.returncode != (0 if status == 0 else 1):
        passed, reason = False, "RISC-V did not exit through its expected test device"
    if target == "raspi3b" and process.returncode not in (0, -15):
        passed, reason = False, "Pi emulator exited with an error"
    result = {"target": target, "status": "pass" if passed else "fail", "reason": reason,
              "command": command, "exit": process.returncode, "firmware_status": status,
              "image_sha256": build["image_sha256"], "elf_sha256": build["elf_sha256"],
              "expected_status": expected, "seconds": round(time.monotonic() - started, 3),
              "input_sent": sent, **{key: data.decode("utf-8", errors="replace") for key, data in captured.items()}}
    (output / "run.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result
