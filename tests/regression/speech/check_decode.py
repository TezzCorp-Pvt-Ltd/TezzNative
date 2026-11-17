"""Independent exhaustive-path oracle for native CTC prefix decoding."""
import argparse
import ctypes
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import random
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def collapse(path):
    result = []
    previous = 0
    for symbol in path:
        if symbol and symbol != previous:
            result.append(symbol)
        previous = symbol
    return tuple(result)


def exact_distribution(rows):
    probabilities = []
    for row in rows:
        largest = max(row)
        values = [math.exp(value - largest) for value in row]
        probabilities.append([value / sum(values) for value in values])
    distribution = {}
    for path in itertools.product(range(len(rows[0])), repeat=len(rows)):
        label = collapse(path)
        probability = math.prod(probabilities[t][c] for t, c in enumerate(path))
        distribution[label] = distribution.get(label, 0.0) + probability
    return distribution


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc")
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/speech-decoder")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cc = args.cc or shutil.which("gcc") or shutil.which("cc")
    if not cc and os.name == "nt" and Path("C:/msys64/mingw64/bin/gcc.exe").is_file():
        cc = "C:/msys64/mingw64/bin/gcc.exe"
    if not cc:
        parser.error("A C11 compiler is required")
    sources = [SDK / "runtime/speech" / name for name in ("decode.c", "ctc.c", "language.c")]
    library = out / ("decoder.dll" if os.name == "nt" else "decoder.so")
    command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared",
               *( ["-static-libgcc"] if os.name == "nt" else ["-fPIC"]),
               "-I" + str(SDK / "include"), *map(str, sources), "-lm", "-o", str(library)]
    env = dict(os.environ)
    env["PATH"] = str(Path(shutil.which(cc) or cc).resolve().parent) + os.pathsep + env.get("PATH", "")
    built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
    if built.returncode:
        raise RuntimeError(f"C build exited {built.returncode}: " + built.stdout + built.stderr)
    native = ctypes.CDLL(str(library))
    fp, ip = ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_int)
    native.speech_ctc_decode.argtypes = [fp, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                         ip, ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
    native.speech_ctc_decode.restype = ctypes.c_int
    native.speech_ctc.argtypes = [fp, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int64),
                                 ctypes.c_int, fp, ip, ctypes.POINTER(ctypes.c_double)]
    native.speech_ctc.restype = ctypes.c_int
    records = []

    def verify(name, action):
        record = {"case": name, "status": "pass"}
        try:
            record.update(action() or {})
        except (AssertionError, RuntimeError, ValueError) as error:
            record.update(status="fail", error=str(error))
        records.append(record)

    def require(condition, detail):
        if not condition:
            raise AssertionError(detail)

    def exhaustive(rows):
        rows = [[ctypes.c_float(x).value for x in row] for row in rows]
        frames, classes = len(rows), len(rows[0])
        values = (ctypes.c_float * (frames * classes))(*(x for row in rows for x in row))
        labels = (ctypes.c_int * frames)(*([-777] * frames))
        score = ctypes.c_double()
        size = native.speech_ctc_decode(values, frames, classes, 128, labels, frames, ctypes.byref(score))
        require(size >= 0, "decoder rejected valid logits")
        distribution = exact_distribution(rows)
        best = max(distribution.values())
        chosen = tuple(labels[:size])
        require(chosen in distribution and abs(distribution[chosen] - best) < 1e-12,
                f"decoded {chosen}, expected most probable transcript {max(distribution, key=distribution.get)}")
        require(abs(score.value - math.log(best)) < 2e-12, (score.value, math.log(best)))
        return {"frames": frames, "classes": classes, "decoded": chosen,
                "log_probability": score.value, "enumerated_paths": classes ** frames}

    verify("marginal_beats_greedy_blank", lambda: exhaustive([[math.log(.6), math.log(.4)]] * 2))
    verify("blank_separates_repeated_labels", lambda: exhaustive([[-20, 10], [10, -20], [-20, 10]]))
    verify("repeat_without_blank_collapses", lambda: exhaustive([[-20, 10]] * 4))
    verify("uniform_scores", lambda: exhaustive([[0, 0, 0]] * 5))
    verify("large_equal_logits", lambda: exhaustive([[1e30, 1e30, 1e30]] * 5))
    randomizer = random.Random(839)
    for index in range(36):
        rows = [[randomizer.uniform(-8, 8) for _ in range(3)] for _ in range(index % 6 + 1)]
        verify(f"exhaustive_{index}", lambda rows=rows: exhaustive(rows))

    def invalid_inputs():
        values = (ctypes.c_float * 6)(0, 5, 0, 5, 0, 5)
        labels = (ctypes.c_int * 4)(*([-777] * 4))
        for frames, classes, width, capacity in ((0, 2, 16, 4), (3001, 2, 16, 4),
                                                (3, 1, 16, 4), (3, 30, 16, 4),
                                                (3, 2, 0, 4), (3, 2, 129, 4),
                                                (3, 2, 16, -1), (3, 2, 16, 0)):
            status = native.speech_ctc_decode(values, frames, classes, width, labels, capacity, None)
            require(status == -1 and list(labels) == [-777] * 4, "invalid input changed output")
        for value in (float("nan"), float("inf"), -float("inf")):
            values[0] = value
            status = native.speech_ctc_decode(values, 3, 2, 16, labels, 4, None)
            require(status == -1 and list(labels) == [-777] * 4, "nonfinite logits accepted")
        return {"checks": 11}
    verify("invalid_inputs_leave_output_untouched", invalid_inputs)

    def ctc_shift_invariance():
        losses, gradients = [], []
        for shift in (0, 1e30, -1e30):
            values = (ctypes.c_float * 12)(*([shift] * 12))
            target = (ctypes.c_int64 * 2)(1, 2)
            gradient = (ctypes.c_float * 12)()
            loss = ctypes.c_double()
            status = native.speech_ctc(values, 4, 3, target, 2, gradient, None, ctypes.byref(loss))
            require(status == 0, "CTC rejected finite logits")
            losses.append(loss.value)
            gradients.append(list(gradient))
        require(max(losses) - min(losses) < 1e-12, f"logit shift changed CTC loss: {losses}")
        require(all(abs(a-b) < 1e-7 for row in gradients[1:] for a,b in zip(gradients[0],row)),
                "logit shift changed gradients")
        return {"losses": losses}
    verify("ctc_logit_shift_invariance", ctc_shift_invariance)
    if os.name != "nt":
        def sanitizer_bounds():
            executable = out / "decode-bounds"
            command = [cc, "-std=c11", "-O1", "-g", "-fsanitize=address,undefined",
                       "-fno-sanitize-recover=all", "-I" + str(SDK / "include"),
                       "-I" + str(SDK / "runtime/speech"),
                       str(Path(__file__).with_name("decode_bounds.c")), str(sources[0]), str(sources[2]),
                       "-lm", "-o", str(executable)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
            require(built.returncode == 0, built.stdout + built.stderr)
            checked = subprocess.run([str(executable)], env={**env, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                                                           "UBSAN_OPTIONS": "halt_on_error=1"},
                                     capture_output=True, text=True, timeout=60)
            require(checked.returncode == 0 and not checked.stderr, checked.stdout + checked.stderr)
            return {"leak_detection": True, "maximum_frames": 3000, "maximum_beam": 128}
        verify("sanitizer_memory_bounds", sanitizer_bounds)
    report = {"results": records, "summary": {key: sum(r["status"] == key for r in records)
                                               for key in ("pass", "fail")},
              "build": {"command": command, "stdout": built.stdout, "stderr": built.stderr},
              "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
              "external_language_model": False}
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"summary": report["summary"], "failures": [r for r in records if r["status"] == "fail"]}, indent=2))
    return int(report["summary"]["fail"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
