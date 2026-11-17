"""Independent signal reconstruction oracle for the native mel vocoder.

Uses only Python's standard library. Spectral reconstruction is a vocoder
check, not a measurement of generated speech intelligibility.
"""
import argparse
import cmath
import ctypes
import functools
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
import wave

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
WINDOW = [0.5 - 0.5 * math.cos(2 * math.pi * i / 399) for i in range(400)]
POINTS = [700 * (10 ** ((2595 * math.log10(1 + 20 / 700)
    + (2595 * math.log10(1 + 8000 / 700) - 2595 * math.log10(1 + 20 / 700)) * i / 81) / 2595) - 1)
    for i in range(82)]
FILTERS = [[max(0.0, min((k * 16000 / 512 - POINTS[m]) / (POINTS[m+1] - POINTS[m]),
    (POINTS[m+2] - k * 16000 / 512) / (POINTS[m+2] - POINTS[m+1]))) for k in range(257)]
    for m in range(80)]


@functools.lru_cache(None)
def roots(count):
    return [cmath.exp(-2j * math.pi * k / count) for k in range(count // 2)]


def fft(values):
    if len(values) == 1:
        return values
    even, odd = fft(values[::2]), fft(values[1::2])
    products = [w * value for w, value in zip(roots(len(values)), odd)]
    return [a + b for a, b in zip(even, products)] + [a - b for a, b in zip(even, products)]


def features(signal):
    result = []
    for start in range(0, len(signal)-399, 160):
        spectrum = fft([signal[start+i] * WINDOW[i] for i in range(400)] + [0.0] * 112)
        power = [abs(value)**2 for value in spectrum[:257]]
        result.append([math.log(max(1e-10, sum(a*b for a, b in zip(weights, power))))
                       for weights in FILTERS])
    return result


def synthesize(function, mel, energy=1.0):
    samples = (len(mel)-1)*160+400
    source = (ctypes.c_double * (len(mel)*80))(*(value for row in mel for value in row))
    pcm = (ctypes.c_int64 * samples)()
    count = function(source, len(mel), pcm, samples, 1.0, energy)
    if count != samples:
        raise AssertionError((count, samples))
    return list(pcm)


def spectral_error(reference, actual):
    target = [math.exp(value) for row in reference[2:-2] for value in row]
    observed = [math.exp(value) for row in actual[2:-2] for value in row]
    return math.sqrt(sum((a-b)**2 for a, b in zip(target, observed)) / sum(a*a for a in target))


def read_wave(path):
    with wave.open(str(path), "rb") as source:
        if source.getnchannels()!=1 or source.getsampwidth()!=2:
            raise ValueError("oracle dataset requires mono signed16 PCM")
        rate = source.getframerate()
        data = struct.unpack("<"+"h"*source.getnframes(), source.readframes(source.getnframes()))
    result = []
    for i in range(len(data)*16000//rate):
        index, fraction = divmod(i*rate, 16000)
        right = data[min(index+1, len(data)-1)]
        result.append(int((data[index]*(16000-fraction)+right*fraction)/16000)/32768)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/vocoder")
    parser.add_argument("--library", type=Path)
    parser.add_argument("--measure-only", action="store_true")
    parser.add_argument("--manifest", type=Path, help="optional held-out PCM WAV/TSV reconstruction measurement")
    parser.add_argument("--items", type=int, default=16)
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    library = args.library.resolve() if args.library else output / ("vocoder.dll" if os.name == "nt" else "vocoder.so")
    env = dict(os.environ)
    if not args.library:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler and os.name == "nt":
            compiler = "C:/msys64/mingw64/bin/gcc.exe"
        if not compiler:
            raise RuntimeError("C compiler required")
        env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
        command = [compiler, "-std=c11", "-O2", "-shared", "-I"+str(SDK/"include"),
                   str(SDK/"runtime/speech/vocoder.c"), "-o", str(library), "-lm"]
        command += ["-static-libgcc"] if os.name == "nt" else ["-fPIC"]
        subprocess.run(command, env=env, check=True, capture_output=True, text=True)
    if not args.measure_only and not args.library:
        executable = output / ("vocoder-bounds.exe" if os.name=="nt" else "vocoder-bounds")
        command = [compiler, "-std=c11", "-O1", "-g", "-I"+str(SDK/"include"),
                   "-I"+str(SDK/"runtime/speech"), str(Path(__file__).with_name("vocoder_bounds.c")),
                   str(SDK/"runtime/speech/vocoder.c"), "-o", str(executable), "-lm"]
        if os.name!="nt":
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
        subprocess.run(command, env=env, check=True, capture_output=True, text=True, timeout=120)
        checked = subprocess.run([str(executable)], env={**env, "ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1",
                                 "UBSAN_OPTIONS":"halt_on_error=1"}, capture_output=True, text=True, timeout=60)
        if checked.returncode or checked.stderr:
            raise AssertionError(checked.stdout+checked.stderr)
        print(checked.stdout, end="", flush=True)
    runtime = ctypes.CDLL(str(library))
    vocode = runtime.speech_vocode
    vocode.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.c_int,
                      ctypes.POINTER(ctypes.c_int64), ctypes.c_int, ctypes.c_double, ctypes.c_double]
    vocode.restype = ctypes.c_int
    records = []
    # An impulse has an all-one DFT; this checks the independent analysis FFT.
    if any(abs(value-1) > 1e-12 for value in fft([1.0]+[0.0]*511)):
        raise AssertionError("analysis FFT impulse")
    size = 12400
    envelope = [min(1, i/1200, (size-1-i)/1200) for i in range(size)]
    signals = {
        "tone": [envelope[i]*0.15*math.sin(2*math.pi*440*i/16000) for i in range(size)],
        "harmonics": [envelope[i]*sum(0.12/k*math.sin(2*math.pi*120*k*i/16000)
                                      for k in range(1, 26)) for i in range(size)],
        "chirp": [envelope[i]*0.15*math.sin(2*math.pi*(300*i/16000 + 1000*(i/16000)**2))
                  for i in range(size)],
    }
    audio_hashes = {}
    if args.manifest:
        if args.items<1 or args.items>128:
            parser.error("items must be between1 and128")
        for row in args.manifest.read_text(encoding="utf-8").splitlines()[:args.items]:
            path = row.split("\t", 1)[0]
            if os.name!="nt" and len(path)>2 and path[1]==":":
                path = "/mnt/"+path[0].lower()+path[2:].replace("\\", "/")
            path = Path(path)
            name = "heldout_"+path.stem
            if name in signals:
                raise ValueError("duplicate held-out utterance")
            signals[name] = read_wave(path)
            audio_hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name, signal in signals.items():
        mel = features(signal)
        started = time.monotonic()
        pcm = synthesize(vocode, mel)
        seconds = time.monotonic()-started
        reconstructed = features([value/32768 for value in pcm])
        error = spectral_error(mel, reconstructed)
        records.append({"case": name, "relative_mel_power_error": error,
                        "peak": max(abs(value) for value in pcm), "samples": len(pcm),
                        "synthesis_seconds": seconds})
        print(name, "relative_mel_power_error", error, flush=True)
        if not args.measure_only and (not math.isfinite(error) or error > 0.1):
            raise AssertionError(records[-1])
    silence = synthesize(vocode, [[math.log(1e-10)]*80 for _ in range(8)])
    if max(abs(value) for value in silence)>1:
        raise AssertionError("frontend silence floor exceeds one PCM quantization unit")
    silent_gain = synthesize(vocode, features(signals["tone"]), energy=0)
    if any(silent_gain):
        raise AssertionError("zero energy")
    report = {"status": "measured" if args.measure_only else "pass", "results": records,
              "library_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
              "reference_audio_sha256": audio_hashes,
              "manifest_sha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest() if args.manifest else None,
              "intelligibility_evaluated": False}
    (output/"results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
