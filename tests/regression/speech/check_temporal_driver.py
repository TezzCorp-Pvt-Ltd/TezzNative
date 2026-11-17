"""Temporal driver path identity, native CLI and checkpoint continuity."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import wave
import zlib

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
SOURCE = ROOT / "tools/speech_temporal_train.tn"
TRANSCRIBE = ROOT / "tools/speech_transcribe.tn"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/temporal-driver")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    report = {"compiler": str(compiler), "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
              "driver_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              "transcriber_sha256": hashlib.sha256(TRANSCRIBE.read_bytes()).hexdigest(), "commands": []}

    def run(command, cwd, status=0):
        result = subprocess.run(list(map(str, command)), cwd=cwd, env=env, capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=60)
        report["commands"].append({"command": list(map(str, command)), "cwd": str(cwd),
                                   "expected_exit": status, "exit": result.returncode,
                                   "stdout": result.stdout, "stderr": result.stderr})
        if result.returncode != status:
            raise AssertionError(f"expected {status}, got {result.returncode}: {command}\n{result.stdout}{result.stderr}")
        return result

    def checkpoint_state(path, steps):
        data = path.read_bytes()
        assert data[:8] == b"TNTC001\x00"
        header = struct.unpack("<24q", data[:192])
        assert header[3] == 8 and header[4] == steps and header[6] == 42
        assert len(data) == 216 + 12 * header[5]
        assert zlib.crc32(data[:-8]) == struct.unpack("<q", data[-8:])[0]
        return data

    try:
        with tempfile.TemporaryDirectory(prefix="driver-", dir=output) as temporary:
            directory = Path(temporary)
            run([compiler, "check", SOURCE.relative_to(ROOT)], ROOT)
            run([compiler, "check", SOURCE], ROOT)
            run([compiler, "check", SOURCE], directory)
            binary = directory / ("speech.exe" if os.name == "nt" else "speech")
            run([compiler, "buildexe", SOURCE, binary, "--verify"], directory)
            run([binary], directory, 2)

            audio = directory / "sample.wav"
            with wave.open(str(audio), "wb") as stream:
                stream.setparams((1, 2, 16000, 4000, "NONE", "not compressed"))
                samples = [int(9000 * math.sin(2 * math.pi * 220 * i / 16000)) for i in range(4000)]
                stream.writeframes(struct.pack("<4000h", *samples))
            manifest = directory / "train.tsv"
            manifest.write_bytes((str(audio) + "\ta\r\n").encode("utf-8"))
            checkpoint = directory / "model.tntc"
            trained = run([binary, "train", manifest, checkpoint, "1", "1", "1000", "8", "0", "42"], directory)
            assert "TRAIN_ITEMS 1" in trained.stdout and "STEPS 1" in trained.stdout
            checkpoint_state(checkpoint, 1)
            resumed = run([binary, "resume", manifest, checkpoint, "1", "1", "1000", "8", "0", "42"], directory)
            assert "STEPS 2" in resumed.stdout
            before = checkpoint_state(checkpoint, 2)
            predictions = directory / "predictions.tsv"
            run([binary, "evaluate", manifest, checkpoint, predictions, "1", "0", "16"], directory)
            first = predictions.read_bytes()
            assert first.startswith(str(audio).encode("utf-8") + b"\t") and first.endswith(b"\n")
            assert len(first.splitlines()) == 1

            # Inference consumes audio only, even when the unused reference is invalid.
            manifest.write_text(str(audio) + "\tINVALID-REFERENCE\n", encoding="utf-8")
            changed_reference = directory / "changed-reference.tsv"
            run([binary, "evaluate", manifest, checkpoint, changed_reference, "1", "0", "16"], directory)
            assert changed_reference.read_bytes() == first

            # Exclusive output creation protects equivalent paths and hardlinks.
            for checkpoint_argument, target, expected in (
                (checkpoint, checkpoint, 2),
                ("model.tntc", "./model.tntc", 1),
                ("model.tntc", checkpoint, 1),
            ):
                run([binary, "evaluate", manifest, checkpoint_argument, target, "1", "0", "16"], directory, expected)
                assert checkpoint.read_bytes() == before
            hardlink = directory / "checkpoint-link.tntc"
            os.link(checkpoint, hardlink)
            run([binary, "evaluate", manifest, checkpoint, hardlink, "1", "0", "16"], directory, 1)
            assert checkpoint.read_bytes() == before and hardlink.read_bytes() == before
            manifest_before = manifest.read_bytes()
            run([binary, "evaluate", manifest, checkpoint, manifest, "1", "0", "16"], directory, 1)
            assert manifest.read_bytes() == manifest_before
            run([binary, "evaluate", manifest, checkpoint, predictions, "1", "0", "16"], directory, 1)
            assert predictions.read_bytes() == first

            partial_manifest = directory / "partial.tsv"
            partial_manifest.write_text(str(audio) + "\ta\n" + str(directory / "missing.wav") + "\ta\n", encoding="utf-8")
            partial_output = directory / "partial-predictions.tsv"
            failed = run([binary, "evaluate", partial_manifest, checkpoint, partial_output, "2", "0", "16"], directory, 1)
            assert "AUDIO_ERROR" in failed.stdout
            assert not partial_output.exists() and checkpoint.read_bytes() == before
            run([binary, "resume", manifest, checkpoint, "1", "1", "1000", "8", "0", "42"], directory, 1)
            assert checkpoint.read_bytes() == before
            manifest.write_text(str(audio) + "\ta\n", encoding="utf-8")
            # An incomplete epoch must fail and preserve the previous checkpoint.
            run([binary, "resume", manifest, checkpoint, "1", "2", "1000", "8", "0", "42"], directory, 1)
            assert checkpoint.read_bytes() == before
            run([binary, "resume", manifest, checkpoint, "1", "1", "1000", "16", "0", "42"], directory, 2)
            run([binary, "train", manifest, checkpoint, "1", "1", "NaN", "8", "0", "42"], directory, 2)
            run([binary, "train", manifest, checkpoint, "1", "1", "1000", "8", "2", "42"], directory, 2)
            assert checkpoint.read_bytes() == before
            report["checkpoint_crc_verified"] = True
            report["reference_independent_inference"] = True
            report["failed_epochs_preserve_checkpoint"] = True
            report["prediction_aliases_preserve_existing_files"] = True
            report["failed_prediction_removes_owned_partial_output"] = True

            # Public offline inference takes only a checkpoint and audio path.
            transcriber = directory / ("transcribe.exe" if os.name == "nt" else "transcribe")
            run([compiler, "check", TRANSCRIBE], directory)
            run([compiler, "buildexe", TRANSCRIBE, transcriber, "--verify"], directory)
            expected_transcript = first.splitlines()[0].split(b"\t", 1)[1].decode("utf-8") + "\n"
            assert run([transcriber, checkpoint, audio], directory).stdout == expected_transcript
            manifest.unlink()
            partial_manifest.unlink()
            assert run([transcriber, checkpoint, audio, "0", "16"], directory).stdout == expected_transcript
            run([transcriber, checkpoint, directory / "missing.wav"], directory, 1)
            invalid_audio = directory / "invalid.wav"
            invalid_audio.write_bytes(b"not a WAV file")
            run([transcriber, checkpoint, invalid_audio], directory, 1)
            long_audio = directory / "too-long.wav"
            with wave.open(str(long_audio), "wb") as stream:
                stream.setparams((1, 2, 16000, 480001, "NONE", "not compressed"))
                stream.writeframes(b"\0" * (480001 * 2))
            too_long = run([transcriber, checkpoint, long_audio], directory, 1)
            assert "30 seconds" in too_long.stdout
            run([transcriber, directory / "missing.tntc", audio], directory, 4)
            invalid_model = directory / "invalid.tntc"
            invalid_model.write_bytes(b"not a checkpoint")
            run([transcriber, invalid_model, audio], directory, 4)
            for arguments in ([], [checkpoint], [checkpoint, audio, "2"],
                              [checkpoint, audio, "0", "0"], [checkpoint, audio, "0", "129"],
                              [checkpoint, audio, "x"], [checkpoint, audio, "0", "16", "extra"]):
                run([transcriber, *arguments], directory, 2)
            assert checkpoint.read_bytes() == before
            report["offline_inference_matches_evaluation_without_manifest"] = True
            report["offline_inference_rejects_bad_inputs_and_overlong_audio"] = True
        report["status"] = "pass"
    except Exception as error:
        report["status"] = "fail"
        report["error"] = str(error)
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("TEMPORAL_DRIVER_" + report["status"].upper(), len(report["commands"]), report.get("error", ""))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
