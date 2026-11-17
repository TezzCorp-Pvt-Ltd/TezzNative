"""Fit our native character prior using training text and score held-out audio."""
import argparse
import json
import os
from pathlib import Path
import shutil

from speech_experiment import digest, manifest_rows, run, score_predictions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--checkpoint", required=True, type=Path)
    parser.add_argument("--training-manifest", required=True, type=Path)
    parser.add_argument("--validation-manifest", required=True, type=Path)
    parser.add_argument("--train-items", type=int, default=11811)
    parser.add_argument("--validation-items", type=int, default=128)
    parser.add_argument("--test-manifest", type=Path,
                        help="evaluate the final validation-selected model once on this untouched split")
    parser.add_argument("--test-items", type=int, default=619)
    parser.add_argument("--width", type=int, default=32)
    parser.add_argument("--weights", type=int, nargs="+", default=[200000, 400000, 600000])
    parser.add_argument("--insertions", type=int, nargs="+", default=[0, 400000, 800000])
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if not 1 <= args.train_items <= 11811 or not 1 <= args.validation_items <= 670 or not 1 <= args.width <= 128:
        parser.error("dataset/beam bounds exceeded")
    if (len(args.weights) * len(args.insertions) > 36 or any(not 0 <= x <= 2000000 for x in args.weights)
            or any(not 0 <= x <= 5000000 for x in args.insertions)):
        parser.error("decoder grid bounds exceeded")
    out, compiler, checkpoint = args.out.resolve(), args.compiler.resolve(), args.checkpoint.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error("output must be a new or empty experiment directory")
    training = manifest_rows(args.training_manifest, args.train_items)
    validation = manifest_rows(args.validation_manifest, args.validation_items)
    train_paths = {os.path.normcase(str(Path(p).resolve())) for p, _ in training}
    valid_paths = {os.path.normcase(str(Path(p).resolve())) for p, _ in validation}
    if train_paths & valid_paths or {t for _, t in training} & {t for _, t in validation}:
        raise ValueError("training and validation audio/text overlap")
    test = []
    if args.test_manifest:
        if not 1 <= args.test_items <= 619:
            parser.error("test subset bounds exceeded")
        test = manifest_rows(args.test_manifest, args.test_items)
        test_paths = {os.path.normcase(str(Path(p).resolve())) for p, _ in test}
        if (test_paths & (train_paths | valid_paths) or
                {t for _, t in test} & {t for _, t in training + validation}):
            raise ValueError("test audio/text overlaps training or validation")
    out.mkdir(parents=True, exist_ok=True)
    train, valid = out / "training-only.tsv", out / "validation-selected.tsv"
    for path, rows in [(train, training), (valid, validation)]:
        path.write_text("".join(p + "\t" + t + "\n" for p, t in rows), encoding="utf-8")
    env = dict(os.environ, TEZZ_SDK_ROOT=str(Path("TezzCorp_WebSites/tn_tezzcorp_com/download/sdk").resolve()))
    if os.name == "nt" and Path("C:/msys64/mingw64/bin").is_dir():
        env["PATH"] = "C:/msys64/mingw64/bin" + os.pathsep + env.get("PATH", "")
    executable = out / ("speech_train.exe" if os.name == "nt" else "speech_train")
    run([str(compiler), "buildexe", "tools/speech_train.tn", str(executable), "--verify"], env, 120)
    inputs = {str(path): digest(path) for path in [compiler, checkpoint, args.training_manifest.resolve(), args.validation_manifest.resolve()]}
    if args.test_manifest:
        inputs[str(args.test_manifest.resolve())] = digest(args.test_manifest)
    # Text fitting never reads training WAVs; hash every audio file actually scored.
    for path, _ in validation + test:
        inputs[str(Path(path).resolve())] = digest(path)
    report = {"training_items": args.train_items, "validation_items": args.validation_items,
              "training_manifest_sha256": digest(train), "validation_manifest_sha256": digest(valid),
              "inputs": inputs, "inference_uses_training_manifest": False,
              "test_split_used": False, "external_language_model": False,
              "runtime_sha256": {p.name: digest(p) for p in out.iterdir() if p.suffix in (".dll", ".so")},
              "source_sha256": {str(p): digest(p) for p in [Path(__file__), Path("tools/speech_train.tn"),
                 *sorted(Path(env["TEZZ_SDK_ROOT"]).joinpath("runtime/speech").glob("*.c"))]},
              "candidates": []}
    prediction = out / "baseline.tsv"
    report["baseline_execution"] = run([str(executable), "evaluate", str(valid), str(checkpoint), str(prediction), str(args.validation_items)], env, 600)
    report["baseline"] = score_predictions(validation, prediction)
    best = (report["baseline"]["wer"], report["baseline"]["cer"])
    shutil.copyfile(checkpoint, out / "best.tns")
    for weight in args.weights:
        for insertion in args.insertions:
            name = f"w{weight}-i{insertion}"
            model, predictions = out / (name + ".tns"), out / (name + ".tsv")
            record = {"weight_ppm": weight, "insertion_ppm": insertion, "beam_width": args.width}
            record["fit"] = run([str(executable), "fit-text", str(train), str(checkpoint), str(model), str(args.train_items),
                                 str(args.width), str(weight), str(insertion)], env, 1200)
            record["evaluate"] = run([str(executable), "evaluate", str(valid), str(model), str(predictions), str(args.validation_items)], env, 600)
            record.update(score_predictions(validation, predictions))
            record["checkpoint_sha256"], record["predictions_sha256"] = digest(model), digest(predictions)
            quality = (record["wer"], record["cer"])
            if quality < best:
                best = quality
                shutil.copyfile(model, out / "best.tns")
                record["selected_best"] = True
            report["candidates"].append(record)
            report["best_checkpoint_sha256"] = digest(out / "best.tns")
            if any(digest(path) != expected for path, expected in inputs.items()):
                raise ValueError("source checkpoint/compiler/manifests changed during experiment")
            (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            print(json.dumps({k: v for k, v in record.items() if k not in ("fit", "evaluate")}), flush=True)
    if test:
        # Selection is complete. Test scores never choose weights or checkpoints.
        test_manifest = out / "test-selected.tsv"
        test_manifest.write_text("".join(p + "\t" + t + "\n" for p, t in test), encoding="utf-8")
        predictions = out / "test-predictions.tsv"
        selected = out / "best.tns"
        frozen = digest(selected)
        execution = run([str(executable), "evaluate", str(test_manifest), str(selected), str(predictions), str(len(test))], env, 1800)
        report["final_test"] = {"items": len(test), "selection_complete_before_test": True,
                                "checkpoint_sha256": frozen, "manifest_sha256": digest(test_manifest),
                                "predictions_sha256": digest(predictions), "execution": execution,
                                **score_predictions(test, predictions)}
        if digest(selected) != frozen or any(digest(path) != expected for path, expected in inputs.items()):
            raise ValueError("frozen model or experiment input changed during final test")
        report["test_split_used"] = True
        report["test_split_used_for_selection"] = False
        (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in report["final_test"].items() if k != "execution"}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
