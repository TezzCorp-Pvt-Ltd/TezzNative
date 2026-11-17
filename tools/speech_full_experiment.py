"""Run TezzNative speech executables on representative/full training coverage.

Python manages manifests, provenance and independent metrics only. Model creation,
forward/backward computation, optimization and checkpoint IO execute in TezzNative
and its native kernels. The test split is never opened by this runner.
"""
import argparse
import json
import os
from pathlib import Path
import shutil

from speech_coverage import (assert_disjoint, atomic_json, audio_hashes, fingerprint,
                             score, select_rows, selection_key, verify_audio, write_manifest)
from speech_experiment import digest, epoch_rate, epoch_rows, run

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
if not SDK.is_dir():
    SDK = ROOT  # Installed SDK layout: bin/, lib/ and tools/ share this root.


def deployment_hashes(executable):
    return {"executable_sha256": digest(executable),
            "deployed_libraries_sha256": {p.name: digest(p) for p in executable.parent.iterdir()
                                          if p.suffix in (".dll", ".so", ".dylib")}}


def verify_deployment(executable, expected):
    try:
        current = deployment_hashes(executable)
    except OSError as error:
        raise ValueError("deployed executable or runtime library is missing") from error
    if current != expected:
        raise ValueError("deployed executable or runtime library changed")


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--dataset-directory", type=Path, default=ROOT / "build/speech/native")
    parser.add_argument("--model", choices=("legacy", "temporal"), default="temporal")
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    parser.add_argument("--gpu-library", type=Path,
                        help="explicit native-ABI CUDA library; required with --device cuda")
    parser.add_argument("--epochs", type=int, default=1, help="additional complete epochs")
    parser.add_argument("--train-items", type=int, default=0, help="0 selects all eligible training audio")
    parser.add_argument("--validation-items", type=int, default=0, help="0 selects the complete validation split")
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--learning-rate-ppm", type=int, default=1000)
    parser.add_argument("--minimum-rate-ppm", type=int, default=100)
    parser.add_argument("--rate-decay", type=float, default=.95)
    parser.add_argument("--initial-checkpoint", type=Path)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--timeout", type=int, default=14400, help="seconds per native training/evaluation command")
    args = parser.parse_args()
    if (not 1 <= args.epochs <= 1000 or not 8 <= args.hidden <= 256
            or not 0 <= args.seed <= 0x7fffffff or args.train_items < 0 or args.validation_items < 0
            or not 1 <= args.minimum_rate_ppm <= args.learning_rate_ppm <= 100000
            or not 0 < args.rate_decay <= 1 or args.timeout <= 0):
        parser.error("invalid experiment bounds")
    if args.model == "legacy" and args.device != "cpu":
        parser.error("legacy speech supports CPU execution only")
    if args.device == "cuda" and (not args.gpu_library or not args.gpu_library.is_file()):
        parser.error("CUDA execution requires an existing explicit --gpu-library")
    if args.device == "cpu" and args.gpu_library:
        parser.error("--gpu-library requires --device cuda")
    return args


def main():
    args = arguments()
    directory = args.directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    compiler = args.compiler.resolve()
    dataset = args.dataset_directory.resolve()
    state_path, history_path = directory / "state.json", directory / "history.json"
    executable = directory / ("speech_train.exe" if os.name == "nt" else "speech_train")
    extension = ".tns" if args.model == "legacy" else ".tntc"
    checkpoint, best = directory / ("current" + extension), directory / ("best" + extension)
    if not args.resume and any(p.exists() for p in (state_path, history_path, checkpoint, best, directory / "run.json")):
        raise ValueError("experiment already exists; choose a new directory or --resume")
    if args.resume and not all(p.is_file() for p in (state_path, history_path, checkpoint, best)):
        raise ValueError("resume requires complete history, provenance and checkpoints")
    training, train_coverage, _ = select_rows(dataset / "train.tsv", args.train_items, args.seed)
    validation, validation_coverage, durations = select_rows(dataset / "validation.tsv", args.validation_items, args.seed)
    assert_disjoint(training, validation)
    print(json.dumps({"training": train_coverage, "validation": validation_coverage}), flush=True)
    hashes = audio_hashes(training + validation)
    if {hashes[p] for p, _ in training} & {hashes[p] for p, _ in validation}:
        raise ValueError("training and validation overlap in audio content")
    driver = ROOT / "tools" / ("speech_train.tn" if args.model == "legacy" else "speech_temporal_train.tn")
    source_paths = [driver, Path(__file__), ROOT / "tools/speech_coverage.py", ROOT / "tools/speech_experiment.py", ROOT / "tools/speech_dataset.py"]
    source_paths += list((SDK / "runtime/speech").glob("*.c")) + list((SDK / "runtime/speech").glob("*.h"))
    source_paths += [ROOT / "lib/stt.tn"]
    if args.model == "temporal":
        source_paths += [ROOT / "tools/speech_train.tn"]
        source_paths += [ROOT / "lib/stt_temporal.tn"] + list((ROOT / "lib/stt_temporal").glob("*.tn"))
        source_paths += [SDK / "lib/tzcpu.tn", SDK / "lib/tzgpu.tn"]
    artifacts = {p.name: digest(p) for p in compiler.parent.iterdir() if p.suffix in (".dll", ".so", ".dylib")}
    configuration = {"version": 2, "model": args.model, "hidden": args.hidden, "device": args.device,
                     "seed": args.seed, "learning_rate_ppm": args.learning_rate_ppm,
                     "minimum_rate_ppm": args.minimum_rate_ppm, "rate_decay": args.rate_decay,
                     "initial_checkpoint_sha256": digest(args.initial_checkpoint) if args.initial_checkpoint else None,
                     "compiler_sha256": digest(compiler), "runtime_artifacts": artifacts,
                     "gpu_library_sha256": digest(args.gpu_library) if args.gpu_library else None,
                     "sources": {str(p.relative_to(ROOT)): digest(p) for p in source_paths},
                     "train_manifest_sha256": digest(dataset / "train.tsv"),
                     "validation_manifest_sha256": digest(dataset / "validation.tsv"),
                     "training_selection_sha256": fingerprint(training),
                     "validation_selection_sha256": fingerprint(validation),
                     "audio_sha256": hashes, "selection_metric": "validation WER, then CER",
                     "decoder": "checkpoint configuration" if args.model == "legacy" else "CTC beam 16, no language prior"}
    history = []
    if args.resume:
        state = json.loads(state_path.read_text(encoding="utf-8"))
        if state["configuration"] != configuration:
            raise ValueError("resume source, data, binary or configuration changed")
        if (digest(history_path) != state["history_sha256"] or digest(checkpoint) != state["checkpoint_sha256"]
                or digest(best) != state["best_sha256"]):
            raise ValueError("resume checkpoint or history changed")
        history = json.loads(history_path.read_text(encoding="utf-8"))
        if not history or len(history) != state["epochs"]:
            raise ValueError("resume history is incomplete")
        if not isinstance(state.get("deployment"), dict):
            raise ValueError("resume requires executable and runtime deployment provenance")
        verify_deployment(executable, state["deployment"])
        if args.initial_checkpoint and digest(directory / "initial.json") != state.get("initial_sha256"):
            raise ValueError("initial evaluation changed")
    env = os.environ.copy()
    env["TEZZ_SDK_ROOT"] = str(SDK)
    if os.name == "nt" and Path("C:/msys64/mingw64/bin").is_dir():
        env["PATH"] = "C:/msys64/mingw64/bin" + os.pathsep + env["PATH"]
    run([str(compiler), "buildexe", str(driver), str(executable), "--verify", "--repro"], env, 180)
    if args.gpu_library:
        gpu_destination = directory / ("tzgpu.dll" if os.name == "nt" else "libtzgpu-native.so")
        if args.gpu_library.resolve() != gpu_destination:
            shutil.copyfile(args.gpu_library, gpu_destination)
        if digest(gpu_destination) != configuration["gpu_library_sha256"]:
            raise ValueError("CUDA library changed while deploying")
    deployed = deployment_hashes(executable)
    if args.resume:
        verify_deployment(executable, state["deployment"])
    selected_validation = directory / "validation-selected.tsv"
    write_manifest(selected_validation, validation)
    write_manifest(directory / "training-selected.tsv", training)
    run_info = {"configuration": configuration, "training": train_coverage, "validation": validation_coverage,
                **deployed, "test_split_opened": False,
                "scope": "single-speaker LJSpeech; validation-selected experimental models",
                "execution": "cpu" if args.device == "cpu" else "CUDA encoder and optimizer; native CPU CTC"}
    atomic_json(directory / "run.json", run_info)
    device = "0" if args.device == "cpu" else "1"

    def evaluate(model, prediction):
        command = [str(executable), "evaluate", str(selected_validation), str(model), str(prediction), str(len(validation))]
        if args.model == "temporal":
            command += [device, "16"]
        execution = run(command, env, args.timeout)
        return {"execution": execution, **score(validation, prediction, durations)}

    best_metrics = min(history, key=selection_key) if history else None
    initial_path = directory / "initial.json"
    if args.initial_checkpoint and not args.resume:
        initial = evaluate(args.initial_checkpoint.resolve(), directory / "initial-predictions.tsv")
        atomic_json(initial_path, initial)
        shutil.copyfile(args.initial_checkpoint, checkpoint)
        shutil.copyfile(args.initial_checkpoint, best)
        shutil.copyfile(directory / "initial-predictions.tsv", directory / "best-predictions.tsv")
        best_metrics = initial
    elif args.initial_checkpoint:
        initial = json.loads(initial_path.read_text(encoding="utf-8"))
        if selection_key(initial) < selection_key(best_metrics):
            best_metrics = initial
    for _ in range(args.epochs):
        epoch = len(history) + 1
        ordered = epoch_rows(training, args.seed, epoch)
        epoch_manifest = directory / "train-epoch.tsv"
        write_manifest(epoch_manifest, ordered)
        rate = epoch_rate(args.learning_rate_ppm, args.rate_decay, args.minimum_rate_ppm, epoch)
        mode = "resume" if history or args.initial_checkpoint else "train"
        command = [str(executable), mode, str(epoch_manifest), str(checkpoint), "1", str(len(training)), str(rate), str(args.hidden)]
        command += ["5"] if args.model == "legacy" else [device, str(args.seed)]
        training_result = run(command, env, args.timeout)
        predictions = directory / f"validation-{epoch:04d}.tsv"
        metrics = evaluate(checkpoint, predictions)
        verify_audio(hashes)
        verify_deployment(executable, deployed)
        record = {"epoch": epoch, "rate_ppm": rate, "training": training_result,
                  "training_order_sha256": digest(epoch_manifest), **metrics}
        if best_metrics is None or selection_key(record) < selection_key(best_metrics):
            shutil.copyfile(checkpoint, best)
            shutil.copyfile(predictions, directory / "best-predictions.tsv")
            best_metrics = record
            record["selected_best"] = True
        history.append(record)
        atomic_json(history_path, history)
        state = {"configuration": configuration, "epochs": len(history),
                 "history_sha256": digest(history_path), "checkpoint_sha256": digest(checkpoint),
                 "best_sha256": digest(best), "audio_hashes_rechecked": True,
                 "deployment": deployed}
        if args.initial_checkpoint:
            state["initial_sha256"] = digest(initial_path)
        atomic_json(state_path, state)
        print(json.dumps({k: v for k, v in record.items() if k not in ("training", "execution")}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
