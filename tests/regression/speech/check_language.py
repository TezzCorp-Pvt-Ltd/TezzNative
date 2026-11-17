"""Independent n-gram probabilities, exhaustive CTC fusion and checkpoint ABI."""
import argparse
from collections import Counter, defaultdict
import ctypes as C
import hashlib
import json
import math
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess

from check_decode import exact_distribution

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/speech-language")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cc = shutil.which("gcc") or shutil.which("cc")
    if not cc and os.name == "nt":
        cc = "C:/msys64/mingw64/bin/gcc.exe"
    if not cc or not Path(cc).is_file():
        parser.error("C compiler required")
    sources = [SDK / "runtime/speech" / name for name in ("language.c", "decode.c", "ctc.c", "nn.c", "stt_model.c")]
    library = out / ("language.dll" if os.name == "nt" else "language.so")
    command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared",
               *( ["-static-libgcc", "-Wl,--export-all-symbols"] if os.name == "nt" else ["-fPIC"]),
               "-I" + str(SDK / "include"), *map(str, sources), "-lm", "-o", str(library)]
    env = dict(os.environ)
    env["PATH"] = str(Path(cc).parent) + os.pathsep + env.get("PATH", "")
    built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    native = C.CDLL(str(library))
    ip, sp, fp, dp = C.POINTER(C.c_int64), C.POINTER(C.c_uint16), C.POINTER(C.c_float), C.POINTER(C.c_double)

    def bind(name, result, arguments):
        fn = getattr(native, name)
        fn.restype, fn.argtypes = result, arguments
        return fn

    create = bind("speech_language_create", C.c_void_p, [])
    free = bind("speech_language_free", None, [C.c_void_p])
    train = bind("speech_language_train", C.c_int, [C.c_void_p, ip, C.c_int])
    tokens = bind("speech_language_tokens", C.c_uint64, [C.c_void_p])
    logp = bind("speech_language_logp", C.c_double, [C.c_void_p, sp, C.c_int, C.c_int])
    decode = bind("speech_ctc_decode_language", C.c_int,
                  [fp, C.c_int, C.c_int, C.c_int, C.c_void_p, C.c_double, C.c_double,
                   C.POINTER(C.c_int), C.c_int, dp])
    make_model = bind("tn_speech_stt_create", C.c_int64, [C.c_int64] * 3)
    free_model = bind("tn_speech_stt_free", C.c_int64, [C.c_int64])
    train_model = bind("tn_speech_stt_train", C.c_int64, [C.c_int64, dp, C.c_int64, ip, C.c_int64, C.c_int64, dp])
    fit_model = bind("tn_speech_stt_text_train", C.c_int64, [C.c_int64, ip, C.c_int64])
    reset_model = bind("tn_speech_stt_text_reset", C.c_int64, [C.c_int64])
    configure = bind("tn_speech_stt_decoder", C.c_int64, [C.c_int64] * 4)
    save = bind("tn_speech_stt_save", C.c_int64, [C.c_int64, C.c_char_p])
    load = bind("tn_speech_stt_load", C.c_int64, [C.c_char_p])
    predict = bind("tn_speech_stt_predict", C.c_int64, [C.c_int64, dp, C.c_int64, C.c_char_p, C.c_int64])
    records = []

    def verify(name, function):
        record = {"case": name, "status": "pass"}
        try:
            function()
        except (AssertionError, ValueError) as error:
            record.update(status="fail", error=str(error))
        records.append(record)

    def require(value, message):
        if not value:
            raise AssertionError(message)

    corpus = [[3, 4, 1, 3, 3], [3, 4, 1, 4], [4, 3, 1, 3]] * 7
    oracle = defaultdict(Counter)
    model = create()
    require(model, "allocation")
    for text in corpus:
        require(train(model, (C.c_int64 * len(text))(*text), len(text)) == 0, "fit")
        for index, label in enumerate(text):
            previous = [0, 0, 0] + text[:index]
            for order in range(4):
                oracle[tuple(previous[-order:]) if order else ()][label] += 1

    def expected(prefix, label):
        previous = [0, 0, 0] + list(prefix)
        probability = 1 / 28
        for order in range(4):
            counts = oracle[tuple(previous[-order:]) if order else ()]
            probability = (counts[label] + 10 * probability) / (sum(counts.values()) + 10)
        return math.log(probability)

    def probabilities():
        for prefix in [[], [3], [3, 4], [4, 1, 3], [9, 10, 11], [7, 8, 4, 3]]:
            actual = [math.exp(logp(model, (C.c_uint16 * len(prefix))(*prefix), len(prefix), c)) for c in range(1, 29)]
            require(abs(sum(actual) - 1) < 1e-12, "probabilities do not normalize")
            for c in range(1, 29):
                require(abs(math.log(actual[c-1]) - expected(prefix, c)) < 1e-12, "independent count mismatch")
    verify("normalized_probabilities_and_unseen_contexts", probabilities)

    def invalid_training():
        before = tokens(model)
        require(train(model, (C.c_int64 * 3)(3, 0, 4), 3) < 0, "blank accepted")
        require(tokens(model) == before, "failed fit changed model")
        require(train(model, None, 3) < 0 and train(model, (C.c_int64 * 1)(29), 1) < 0, "invalid text accepted")
    verify("invalid_fit_is_atomic", invalid_training)

    rng = random.Random(917)
    for index in range(12):
        rows = [[C.c_float(rng.uniform(-3, 3)).value for _ in range(5)] for _ in range(3)]
        def fusion(rows=rows):
            distribution = exact_distribution(rows)
            scores = {}
            for labels, probability in distribution.items():
                language = sum(expected(labels[:i], label) for i, label in enumerate(labels))
                scores[labels] = math.log(probability) + .6 * language + .4 * len(labels)
            target = min(scores, key=lambda seq: (-scores[seq], seq))
            output = (C.c_int * 3)(-1, -1, -1)
            acoustic = C.c_double()
            count = decode((C.c_float * 15)(*(x for row in rows for x in row)), 3, 5, 128,
                           model, .6, .4, output, 3, C.byref(acoustic))
            actual = tuple(output[:count])
            require(count >= 0 and actual == target, f"fusion {actual} != {target}")
            require(abs(acoustic.value - math.log(distribution[target])) < 1e-10, "acoustic probability altered")
        verify("exhaustive_fusion_" + str(index), fusion)
    free(model)

    def checkpoint():
        handle = make_model(8, 0, 42)
        require(handle, "model allocation")
        loaded = legacy = 0
        try:
            mel = (C.c_double * (4 * 80))(*([-10.0] * (4 * 80)))
            labels, loss = (C.c_int64 * 1)(3), C.c_double()
            require(configure(handle, 16, 500000, 0) < 0, "unfitted LM selected")
            require(train_model(handle, mel, 4, labels, 1, 1000, C.byref(loss)) == 0, "acoustic training")
            plain = out / "plain.tns"
            require(save(handle, os.fsencode(plain)) == 0, "plain save")
            raw = bytearray(plain.read_bytes())
            net_count = struct.unpack_from("<I", raw, 56)[0]
            decoder_offset = 72 + 12 * net_count
            require(len(raw) == decoder_offset + 20, "version2 layout")
            # Materialize the old documented format to verify backward loading.
            raw[6] = ord("1")
            struct.pack_into("<I", raw, 8, 1)
            old_path = out / "legacy.tns"
            old_path.write_bytes(raw[:decoder_offset])
            legacy = load(os.fsencode(old_path))
            require(legacy, "TNSTT01 compatibility")
            for text in corpus:
                require(fit_model(handle, (C.c_int64 * len(text))(*text), len(text)) == 0, "fit model")
            require(configure(handle, 32, 600000, 400000) == 0, "decoder config")
            path = out / "with-language.tns"
            require(save(handle, os.fsencode(path)) == 0, "language save")
            raw = path.read_bytes()
            require(raw[:8] == b"TNSTT02\0", "version not explicit")
            loaded = load(os.fsencode(path))
            require(loaded, "language load")
            path.unlink()
            before, after = C.create_string_buffer(64), C.create_string_buffer(64)
            require(predict(handle, mel, 4, before, 64) >= 0 and predict(loaded, mel, 4, after, 64) >= 0, "standalone inference")
            require(before.value == after.value, "roundtrip prediction differs")
            for offset in [decoder_offset + 8, decoder_offset + 36, len(raw) - 1]:
                damaged = bytearray(raw)
                damaged[offset] ^= 1
                path.write_bytes(damaged)
                require(load(os.fsencode(path)) == 0, "corrupt decoder/counts accepted")
            path.write_bytes(raw[:-1])
            require(load(os.fsencode(path)) == 0, "truncated language model accepted")
            require(reset_model(handle) == 0 and configure(handle, 16, 500000, 0) < 0, "reset did not detach prior")
        finally:
            free_model(handle)
            if loaded: free_model(loaded)
            if legacy: free_model(legacy)
    verify("versioned_checkpoint_crc_legacy_and_standalone_inference", checkpoint)
    report = {"results": records, "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
              "summary": {"pass": sum(row["status"] == "pass" for row in records), "fail": sum(row["status"] != "pass" for row in records)}}
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return bool(report["summary"]["fail"])


if __name__ == "__main__":
    raise SystemExit(main())
