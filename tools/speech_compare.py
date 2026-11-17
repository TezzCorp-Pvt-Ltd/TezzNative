"""Compare native decoder revisions on one fixed checkpoint and validation subset."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from speech_experiment import manifest_rows, score_predictions

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    before = parser.add_mutually_exclusive_group(required=True)
    before.add_argument("--before-compiler", type=Path)
    before.add_argument("--before-executable", type=Path,
                        help="use an archived native executable and its adjacent runtime without rebuilding")
    after = parser.add_mutually_exclusive_group(required=True)
    after.add_argument("--after-compiler", type=Path)
    after.add_argument("--after-executable", type=Path)
    parser.add_argument("--checkpoint", type=Path, default=ROOT / "build/speech/native/stt-best.tns")
    parser.add_argument("--manifest", type=Path, default=ROOT / "build/speech/native/validation.tsv")
    parser.add_argument("--items", type=int, default=128)
    parser.add_argument("--out", type=Path, default=ROOT / "build/speech/decoder-comparison")
    args = parser.parse_args()
    if not 1 <= args.items <= 670:
        parser.error("--items must be within 1..670")
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    checkpoint, manifest = args.checkpoint.resolve(), args.manifest.resolve()
    references = manifest_rows(manifest, args.items)
    original_model, original_manifest = digest(checkpoint), digest(manifest)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    if os.name == "nt" and Path("C:/msys64/mingw64/bin").is_dir():
        env["PATH"] = "C:/msys64/mingw64/bin" + os.pathsep + env.get("PATH", "")
    report = {"checkpoint": str(checkpoint), "checkpoint_sha256": original_model,
              "manifest": str(manifest), "manifest_sha256": original_manifest,
              "items": args.items, "selection": "first N validation rows; identical for both executions",
              "test_set_used": False, "training_performed": False, "external_language_model": False,
              "results": []}
    archived_inputs = {}
    try:
        for label, source_compiler, archived in (("before", args.before_compiler, args.before_executable),
                                                 ("after", args.after_compiler, args.after_executable)):
            compiler = source_compiler.resolve() if source_compiler else None
            folder = output / label
            folder.mkdir(parents=True, exist_ok=True)
            program = archived.resolve() if archived else folder / ("speech_train.exe" if os.name == "nt" else "speech_train")
            predictions = folder / "predictions.tsv"
            commands = [] if archived else [[compiler, "buildexe", ROOT / "tools/speech_train.tn", program, "--verify"]]
            commands.append([program, "evaluate", manifest, checkpoint, predictions, str(args.items)])
            record = {"revision": label, "compiler": str(compiler) if compiler else None,
                      "compiler_sha256": digest(compiler) if compiler else None, "archived_executable": bool(archived), "steps": []}
            if archived:
                for path in [program, *[p for p in program.parent.iterdir() if p.suffix in (".dll", ".so", ".dylib")]]:
                    archived_inputs[path] = digest(path)
            for command in commands:
                start = time.perf_counter()
                command = [str(argument) for argument in command]
                result = subprocess.run(command, env=env, cwd=ROOT, capture_output=True, text=True,
                                        encoding="utf-8", errors="replace", timeout=300)
                record["steps"].append({"command": command, "seconds": time.perf_counter() - start,
                                        "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
                if result.returncode:
                    raise RuntimeError(f"{label} exited {result.returncode}: {result.stdout}{result.stderr}")
            record.update(score_predictions(references, predictions))
            record["predictions_sha256"] = digest(predictions)
            record["runtime_artifacts"] = {path.name: digest(path) for path in sorted(program.parent.iterdir())
                                             if path.suffix in (".dll", ".so", ".dylib")}
            record["executable_sha256"] = digest(program)
            report["results"].append(record)
            print(label, "CER", record["cer"], "WER", record["wer"], flush=True)
        if digest(checkpoint) != original_model or digest(manifest) != original_manifest:
            raise RuntimeError("comparison modified the checkpoint or validation manifest")
        if any(digest(path) != value for path, value in archived_inputs.items()):
            raise RuntimeError("comparison modified an archived executable/runtime artifact")
        report["status"] = "pass"
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        report.update(status="fail", error=str(error))
        raise
    finally:
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
