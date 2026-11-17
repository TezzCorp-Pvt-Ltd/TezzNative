"""Measure aligned reference-mel reconstruction with independent STOI/ESTOI.

The optional pinned pystoi package is an evaluation tool, not a speech model
or a native-runtime dependency. These scores do not establish TTS intelligibility.
"""
import argparse
import ctypes
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import sys
import warnings

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/regression/speech"))
from check_vocoder import features, read_wave, synthesize


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_vocoder(path):
    runtime = ctypes.CDLL(str(path))
    function = runtime.speech_vocode
    function.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.c_int,
                        ctypes.POINTER(ctypes.c_int64), ctypes.c_int,
                        ctypes.c_double, ctypes.c_double]
    function.restype = ctypes.c_int
    return runtime, function


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-library", required=True, type=Path)
    parser.add_argument("--after-library", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--items", type=int, default=16)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--metric-deps", type=Path,
                        help="optional isolated directory containing pystoi 0.4.1 and scipy")
    args = parser.parse_args()
    if not 1 <= args.items <= 128:
        parser.error("items must be between 1 and 128")
    if args.out.exists():
        parser.error("refusing to overwrite an existing measurement")
    if args.metric_deps:
        sys.path.insert(0, str(args.metric_deps.resolve()))
    import numpy as np
    import pystoi
    import scipy
    from pystoi import stoi
    if importlib.metadata.version("pystoi") != "0.4.1":
        raise RuntimeError("this evaluator requires the reviewed pystoi 0.4.1 implementation")

    def score(reference, processed):
        reference = np.asarray(reference, dtype=np.float64)
        processed = np.asarray(processed, dtype=np.float64)
        if reference.shape != processed.shape or reference.ndim != 1:
            raise ValueError("STOI requires equal-length time-aligned waveforms")
        if (not np.all(np.isfinite(reference)) or not np.all(np.isfinite(processed))
                or np.dot(reference, reference) <= 0):
            raise ValueError("nonfinite signal or silent reference")
        # ESTOI uses machine-epsilon dithering in its normalization. Fix the
        # evaluator seed so that repeated measurements are reproducible.
        result = {}
        for name, extended in (("stoi", False), ("estoi", True)):
            np.random.seed(0)
            with warnings.catch_warnings():
                warnings.simplefilter("error")
                value = float(stoi(reference, processed, 16000, extended=extended))
            if not math.isfinite(value) or not -1.000000001 <= value <= 1.000000001:
                raise ValueError("invalid intelligibility proxy")
            result[name] = value
        return result

    libraries = {"before": args.before_library.resolve(), "after": args.after_library.resolve()}
    loaded = {name: load_vocoder(path) for name, path in libraries.items()}
    hashes = {name: digest(path) for name, path in libraries.items()}
    manifest_hash = digest(args.manifest)
    rows = args.manifest.read_text(encoding="utf-8").splitlines()
    if len(rows) < args.items:
        raise ValueError("manifest has fewer rows than requested")
    records, seen = [], set()
    for row in rows[:args.items]:
        path, transcript = row.split("\t", 1)
        if not transcript.strip():
            raise ValueError("empty reference transcript")
        if os.name != "nt" and len(path) > 2 and path[1] == ":":
            path = "/mnt/" + path[0].lower() + path[2:].replace("\\", "/")
        path = Path(path).resolve()
        if path in seen:
            raise ValueError("duplicate reference audio")
        seen.add(path)
        audio_hash = digest(path)
        reference = read_wave(path)
        mel = features(reference)
        samples = (len(mel)-1)*160+400
        # Both reconstructions retain the reference mel's exact frame times.
        # Discard only the unused incomplete final analysis hop, at most 159
        # samples (9.94 ms). No delay search, time warping or duration fitting.
        aligned = np.asarray(reference[:samples])
        identity = score(aligned, aligned)
        gain = score(aligned, 0.3*aligned)
        noise = np.random.default_rng(0).standard_normal(samples)*np.std(aligned)
        noisy = score(aligned, aligned+noise)
        if any(identity[key] < 0.999999 or gain[key] < 0.999999
               or noisy[key] >= identity[key]-0.05 for key in identity):
            raise AssertionError("independent metric identity/gain/noise controls failed")
        record = {"utterance": path.stem, "audio_sha256": audio_hash,
                  "reference_samples": len(reference), "compared_samples": samples,
                  "omitted_tail_samples": len(reference)-samples,
                  "controls": {"identity": identity, "gain_0_3": gain, "noise_0db": noisy}}
        for name, (_, vocode) in loaded.items():
            pcm = synthesize(vocode, mel)
            record[name] = score(aligned, np.asarray(pcm, dtype=np.float64)/32768)
        if digest(path) != audio_hash:
            raise ValueError("reference audio changed during evaluation")
        records.append(record)
        print(json.dumps({"utterance": path.stem, "before": record["before"],
                          "after": record["after"]}), flush=True)
    if digest(args.manifest) != manifest_hash or any(digest(path) != hashes[name]
                                                   for name, path in libraries.items()):
        raise ValueError("measurement inputs changed")
    package = Path(pystoi.__file__).parent
    report = {
        "status": "measured", "scope": "aligned_reference_mel_vocoder_reconstruction",
        "items": len(records), "sample_rate": 16000, "alignment": "unchanged reference frame times",
        "tail_handling": "discard incomplete final analysis hop; no delay search or time warping",
        "manifest_sha256": manifest_hash, "library_sha256": hashes,
        "evaluator_sha256": digest(Path(__file__)),
        "analysis_sha256": digest(ROOT/"tests/regression/speech/check_vocoder.py"),
        "metric": {"implementation": "pystoi", "version": "0.4.1",
                   "numpy": np.__version__, "scipy": scipy.__version__, "seed": 0,
                   "source_sha256": {path.name: digest(path) for path in package.glob("*.py")},
                   "stoi_definition": "https://doi.org/10.1109/TASL.2011.2114881",
                   "estoi_definition": "https://doi.org/10.1109/TASLP.2016.2585878"},
        "mean": {name: {key: sum(row[name][key] for row in records)/len(records)
                        for key in ("stoi", "estoi")} for name in libraries},
        "results": records,
        "full_text_to_speech_stoi": {"status": "not_evaluated",
                                     "reason": "learned durations are not aligned with reference audio"},
        "intelligibility_established": False,
        "limitations": "objective reconstruction proxies; no listening test, MOS or percent words understood",
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(report["mean"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
