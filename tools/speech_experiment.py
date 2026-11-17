"""Run native TN speech jobs, recording held-out metrics after every epoch."""
import argparse
import hashlib
import json
import os
import random
import shutil
import subprocess
import time
from pathlib import Path
from speech_dataset import distance


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def manifest_rows(path, limit):
    rows = []
    seen = set()
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) != 2 or not fields[0] or not fields[1]:
            raise ValueError("manifest requires a WAV path and a nonempty normalized transcript")
        source, transcript = fields
        canonical = os.path.normcase(str(Path(source).resolve()))
        if canonical in seen:
            raise ValueError("duplicate audio path in manifest")
        seen.add(canonical)
        rows.append((source, transcript))
    if len(rows) < limit:
        raise ValueError("manifest has fewer utterances than the requested limit")
    selected = rows[:limit]
    if any(not Path(path).is_file() for path, _ in selected):
        raise ValueError("selected manifest audio is missing on this host; prepare manifests on this host")
    return selected


def score_predictions(references, predictions):
    expected = dict(references)
    seen = set()
    edits = words = chars = word_edits = 0
    for line in Path(predictions).read_text(encoding="utf-8").splitlines():
        path, prediction = line.split("\t", 1)
        if path in seen or path not in expected:
            raise ValueError("duplicate or unexpected validation utterance")
        seen.add(path)
        reference = expected[path]
        chars += len(reference)
        words += len(reference.split())
        edits += distance(reference, prediction)
        word_edits += distance(reference.split(), prediction.split())
    if seen != set(expected) or not chars or not words:
        raise ValueError("incomplete validation")
    return {"cer": edits / chars, "wer": word_edits / words}


def validate_resume(directory, configuration, resume):
    checkpoint = directory / "stt-expanded.tns"
    history_file = directory / "expanded-experiment.json"
    state_file = directory / "experiment-state.json"
    if not resume:
        if any(path.exists() for path in (checkpoint, history_file, state_file, directory / "stt-best.tns")):
            raise ValueError("experiment already exists; use --resume or a new experiment directory")
        return []
    if not all(path.is_file() for path in (checkpoint, history_file, state_file)):
        raise ValueError("resume requires the checkpoint, history, and experiment-state.json provenance")
    state = json.loads(state_file.read_text(encoding="utf-8"))
    history = json.loads(history_file.read_text(encoding="utf-8"))
    if state.get("configuration") != configuration:
        raise ValueError("resume configuration or source/manifest hashes changed")
    if not history or state.get("epochs") != len(history) or state.get("history_sha256") != digest(history_file):
        raise ValueError("resume history is incomplete or changed")
    if state.get("checkpoint_sha256") != digest(checkpoint):
        raise ValueError("resume checkpoint differs from its recorded completed epoch")
    best = directory / "stt-best.tns"
    if not best.is_file() or state.get("best_checkpoint_sha256") != digest(best):
        raise ValueError("resume best checkpoint is missing or changed")
    if configuration.get("initial_checkpoint_sha256"):
        baseline = directory / "initial-evaluation.json"
        if not baseline.is_file() or state.get("initial_evaluation_sha256") != digest(baseline):
            raise ValueError("resume initial evaluation is missing or changed")
    return history


def run(command, env, timeout):
    start = time.perf_counter()
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=timeout)
    print(result.stdout, end="", flush=True)
    if result.returncode:
        raise RuntimeError(f"exit {result.returncode}: {result.stderr}")
    return {"seconds": time.perf_counter() - start, "stdout": result.stdout}


def epoch_rows(training, seed, epoch):
    """Shuffle only the selected training rows; never include held-out rows."""
    rows = list(training)
    random.Random(seed + epoch).shuffle(rows)
    return rows


def epoch_rate(initial, decay, minimum, epoch):
    return max(minimum, round(initial * decay ** (epoch - 1)))


def checkpoint_dimensions(path):
    """Inspect the existing checkpoint contract before native loading verifies CRC."""
    import struct
    with Path(path).open("rb") as stream:
        header = stream.read(44)
    if len(header) != 44 or header[:8] not in (b"TNSTT01\0", b"TNSTT02\0"):
        raise ValueError("initial checkpoint is not a TNSTT01/TNSTT02 model")
    config = struct.unpack("<9I", header[8:])
    if config[:7] != (header[6] - ord("0"), 0x01020304, 16000, 400, 512, 160, 80):
        raise ValueError("initial checkpoint frontend is incompatible")
    return {"context": config[7], "hidden": config[8]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--train-items", type=int, default=1000)
    parser.add_argument("--validation-items", type=int, default=128)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--initial-checkpoint", type=Path,
                        help="start a new recorded fine-tuning experiment from an existing model")
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--context", type=int, default=5)
    parser.add_argument("--learning-rate-ppm", type=int, default=1000)
    parser.add_argument("--minimum-rate-ppm", type=int, default=100)
    parser.add_argument("--rate-decay", type=float, default=0.95)
    parser.add_argument("--shuffle-seed", type=int, default=42)
    parser.add_argument("--directory", default="build/speech/native")
    args = parser.parse_args()
    if not 1 <= args.epochs <= 1000 or not 1 <= args.train_items <= 11811 or not 1 <= args.validation_items <= 670:
        parser.error("experiment bounds exceeded")
    if (not 8 <= args.hidden <= 256 or not 1 <= args.context <= 10 or
            not 1 <= args.minimum_rate_ppm <= args.learning_rate_ppm <= 100000 or
            not 0 < args.rate_decay <= 1 or not 0 <= args.shuffle_seed <= 0x7fffffff):
        parser.error("invalid model, learning-rate schedule or shuffle seed")
    if args.initial_checkpoint and checkpoint_dimensions(args.initial_checkpoint) != {"context": args.context, "hidden": args.hidden}:
        parser.error("initial checkpoint dimensions differ from --hidden/--context")
    directory = Path(args.directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / ("speech_train.exe" if os.name == "nt" else "speech_train")
    compiler = str(Path(args.compiler).resolve())
    env = os.environ.copy()
    env["TEZZ_SDK_ROOT"] = str(Path("TezzCorp_WebSites/tn_tezzcorp_com/download/sdk").resolve())
    if os.name == "nt" and Path("C:/msys64/mingw64/bin").is_dir():
        env["PATH"] = "C:/msys64/mingw64/bin" + os.pathsep + env["PATH"]
    checkpoint = directory / "stt-expanded.tns"
    best = directory / "stt-best.tns"
    predictions = directory / "validation-expanded.tsv"
    train_manifest, validation_manifest = directory / "train.tsv", directory / "validation.tsv"
    training = manifest_rows(train_manifest, args.train_items)
    references = manifest_rows(validation_manifest, args.validation_items)
    train_paths = {os.path.normcase(str(Path(path).resolve())) for path, _ in training}
    validation_paths = {os.path.normcase(str(Path(path).resolve())) for path, _ in references}
    if train_paths & validation_paths or {text for _, text in training} & {text for _, text in references}:
        raise ValueError("training and validation overlap in audio paths or normalized transcripts")
    configuration = {"version": 2, "train_items": args.train_items, "validation_items": args.validation_items,
                     "hidden": args.hidden, "context": args.context,
                     "learning_rate_ppm": args.learning_rate_ppm, "minimum_rate_ppm": args.minimum_rate_ppm,
                     "rate_decay": args.rate_decay, "shuffle_seed": args.shuffle_seed,
                     "initial_checkpoint_sha256": digest(args.initial_checkpoint) if args.initial_checkpoint else None,
                     "orchestration_sha256": digest(__file__),
                     "train_manifest_sha256": digest(train_manifest), "validation_manifest_sha256": digest(validation_manifest),
                     "compiler_sha256": digest(compiler), "training_source_sha256": digest("tools/speech_train.tn"),
                     "runtime_sources": {path.name: digest(path) for path in sorted(Path(env["TEZZ_SDK_ROOT"]).joinpath("runtime/speech").glob("*"))
                                         if path.suffix in (".c", ".h")}}
    configuration["compiler_runtime_artifacts"] = {path.name: digest(path) for path in sorted(Path(compiler).parent.iterdir())
                                                     if path.suffix in (".dll", ".so", ".dylib")}
    result_path = directory / "expanded-experiment.json"
    history = validate_resume(directory, configuration, args.resume)
    run([compiler, "buildexe", "tools/speech_train.tn", str(executable), "--verify"], env, 120)
    best_cer = min((r["cer"] for r in history), default=float("inf"))
    if args.initial_checkpoint and not args.resume:
        # Validate and measure the exact starting model before allowing updates.
        baseline_predictions = directory / "validation-initial.tsv"
        result = run([str(executable), "evaluate", str(validation_manifest), str(args.initial_checkpoint.resolve()),
                      str(baseline_predictions), str(args.validation_items)], env, 600)
        baseline = {"checkpoint_sha256": digest(args.initial_checkpoint), "validation": result,
                    **score_predictions(references, baseline_predictions)}
        (directory / "initial-evaluation.json").write_text(json.dumps(baseline, indent=2) + "\n", encoding="utf-8")
        best_cer = baseline["cer"]
        shutil.copyfile(args.initial_checkpoint, checkpoint)
        shutil.copyfile(args.initial_checkpoint, best)
        shutil.copyfile(baseline_predictions, directory / "validation-best.tsv")
    elif args.initial_checkpoint:
        baseline = json.loads((directory / "initial-evaluation.json").read_text(encoding="utf-8"))
        best_cer = min(best_cer, baseline["cer"])
    for epoch in range(args.epochs):
        record = {"epoch": len(history) + 1, "train_items": args.train_items,
                  "validation_items": args.validation_items, "device": "cpu"}
        order = epoch_rows(training, args.shuffle_seed, record["epoch"])
        epoch_manifest = directory / "train-epoch.tsv"
        epoch_manifest.write_text("".join(path + "\t" + text + "\n" for path, text in order), encoding="utf-8")
        record["training_order_sha256"] = digest(epoch_manifest)
        record["learning_rate_ppm"] = epoch_rate(args.learning_rate_ppm, args.rate_decay, args.minimum_rate_ppm, record["epoch"])
        command = "resume" if args.initial_checkpoint or args.resume or epoch else "train"
        record["train"] = run([str(executable), command, str(epoch_manifest), str(checkpoint), "1", str(args.train_items),
                               str(record["learning_rate_ppm"]), str(args.hidden), str(args.context)], env, 7200)
        record["validation"] = run([str(executable), "evaluate", str(directory / "validation.tsv"), str(checkpoint), str(predictions), str(args.validation_items)], env, 300)
        record.update(score_predictions(references, predictions))
        if record["cer"] < best_cer:
            best_cer = record["cer"]
            shutil.copyfile(checkpoint, best)
            shutil.copyfile(predictions, directory / "validation-best.tsv")
            record["selected_best"] = True
        history.append(record)
        result_path.write_text(json.dumps(history, indent=2) + "\n", encoding="utf-8")
        state = {"configuration": configuration, "epochs": len(history),
                 "checkpoint_sha256": digest(checkpoint), "best_checkpoint_sha256": digest(best),
                 "history_sha256": digest(result_path)}
        if args.initial_checkpoint:
            state["initial_evaluation_sha256"] = digest(directory / "initial-evaluation.json")
        (directory / "experiment-state.json").write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in record.items() if k not in ("train", "validation")}), flush=True)


if __name__ == "__main__":
    main()
