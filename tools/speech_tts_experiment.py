"""Fine-tune native TTS with a fixed aligner and disjoint held-out measurements.

The metrics cover teacher-forced mel accuracy and generated duration, not
intelligibility. This creates a new experiment and never overwrites its inputs.
"""
import argparse
import json
import math
import os
from pathlib import Path
import re
import shutil
from speech_experiment import digest, manifest_rows, epoch_rows, run


def metrics(output, expected_items):
    mel = re.search(r"TTS_ITEMS\s+(\d+)\s+NORMALIZED_MEL_MSE\s+([\d.eE+-]+)\s+STEPS\s+(\d+)", output)
    duration = re.search(r"DURATION_REFERENCE_MEAN_SECONDS\s+([\d.eE+-]+)\s+GENERATED_MEAN_SECONDS\s+([\d.eE+-]+)\s+MAE_SECONDS\s+([\d.eE+-]+)", output)
    if not mel or not duration or int(mel[1])!=expected_items:
        raise ValueError("incomplete native TTS validation output")
    result = {"normalized_mel_mse":float(mel[2]), "steps":int(mel[3]),
              "reference_mean_seconds":float(duration[1]), "generated_mean_seconds":float(duration[2]),
              "duration_mae_seconds":float(duration[3])}
    if any(not math.isfinite(value) or value<0 for value in result.values()):
        raise ValueError("nonfinite or negative native TTS metric")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--dataset-directory", required=True, type=Path)
    parser.add_argument("--aligner", required=True, type=Path)
    parser.add_argument("--initial-checkpoint", required=True, type=Path)
    parser.add_argument("--epochs", type=int, default=10)
    parser.add_argument("--train-items", type=int, default=1000)
    parser.add_argument("--validation-items", type=int, default=32)
    parser.add_argument("--shuffle-seed", type=int, default=42)
    args = parser.parse_args()
    if not 1<=args.epochs<=1000 or not 1<=args.train_items<=11811 or not 1<=args.validation_items<=670:
        parser.error("experiment bounds exceeded")
    output, dataset = args.directory.resolve(), args.dataset_directory.resolve()
    compiler, aligner, initial = args.compiler.resolve(), args.aligner.resolve(), args.initial_checkpoint.resolve()
    checkpoint, best = output/"tts-expanded.tns", output/"tts-best.tns"
    if any(path.exists() for path in (checkpoint,best,output/"results.json",output/"provenance.json")):
        raise ValueError("experiment exists; choose a new directory")
    training = manifest_rows(dataset/"train.tsv", args.train_items)
    validation = manifest_rows(dataset/"validation.tsv", args.validation_items)
    train_paths = {os.path.normcase(str(Path(path).resolve())) for path,_ in training}
    validation_paths = {os.path.normcase(str(Path(path).resolve())) for path,_ in validation}
    if train_paths & validation_paths or {text for _,text in training} & {text for _,text in validation}:
        raise ValueError("training and validation overlap")
    output.mkdir(parents=True, exist_ok=True)
    executable = output/("speech_tts_train.exe" if os.name=="nt" else "speech_tts_train")
    env = dict(os.environ, TEZZ_SDK_ROOT=str(Path("TezzCorp_WebSites/tn_tezzcorp_com/download/sdk").resolve()))
    if os.name=="nt" and Path("C:/msys64/mingw64/bin").is_dir():
        env["PATH"]="C:/msys64/mingw64/bin"+os.pathsep+env.get("PATH", "")
    provenance = {"compiler_sha256":digest(compiler), "aligner_sha256":digest(aligner),
                  "initial_checkpoint_sha256":digest(initial), "driver_sha256":digest("tools/speech_tts_train.tn"),
                  "orchestration_sha256":digest(__file__), "train_items":args.train_items,
                  "validation_items":args.validation_items, "shuffle_seed":args.shuffle_seed,
                  "training_manifest_sha256":digest(dataset/"train.tsv"),
                  "validation_manifest_sha256":digest(dataset/"validation.tsv"),
                  "runtime_artifacts":{p.name:digest(p) for p in compiler.parent.iterdir()
                                       if p.suffix in (".dll", ".so", ".dylib")},
                  "selected_audio":{str(Path(path).resolve()):digest(path) for path,_ in training+validation}}
    (output/"provenance.json").write_text(json.dumps(provenance,indent=2)+"\n",encoding="utf-8")
    run([str(compiler),"buildexe","tools/speech_tts_train.tn",str(executable),"--verify"],env,120)
    validation_manifest = output/"validation.tsv"
    validation_manifest.write_text("".join(path+"\t"+text+"\n" for path,text in validation),encoding="utf-8")
    result = run([str(executable),"validate",str(validation_manifest),str(aligner),str(initial),"1",str(args.validation_items)],env,600)
    baseline = {"validation":result, **metrics(result["stdout"],args.validation_items)}
    report = {"status":"running", "baseline":baseline, "epochs":[], "best_epoch":0, "best":baseline,
              "intelligibility_evaluated":False, "test_split_used":False, "device":"cpu"}
    shutil.copyfile(initial,checkpoint)
    shutil.copyfile(initial,best)
    for epoch in range(1,args.epochs+1):
        training_manifest = output/"train-epoch.tsv"
        training_manifest.write_text("".join(path+"\t"+text+"\n" for path,text in epoch_rows(training,args.shuffle_seed,epoch)),encoding="utf-8")
        record = {"epoch":epoch,"training_order_sha256":digest(training_manifest)}
        record["train"] = run([str(executable),"resume",str(training_manifest),str(aligner),str(checkpoint),"1",str(args.train_items)],env,7200)
        record["validation"] = run([str(executable),"validate",str(validation_manifest),str(aligner),str(checkpoint),"1",str(args.validation_items)],env,600)
        record.update(metrics(record["validation"]["stdout"],args.validation_items))
        record["checkpoint_sha256"] = digest(checkpoint)
        if record["normalized_mel_mse"]<report["best"]["normalized_mel_mse"]:
            report["best"],report["best_epoch"] = record,epoch
            shutil.copyfile(checkpoint,best)
        report["epochs"].append(record)
        report["best_checkpoint_sha256"] = digest(best)
        (output/"results.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
        print(json.dumps({key:value for key,value in record.items() if key not in ("train","validation")}),flush=True)
    if digest(aligner)!=provenance["aligner_sha256"] or digest(initial)!=provenance["initial_checkpoint_sha256"]:
        raise ValueError("input model changed during experiment")
    if any(digest(path)!=expected for path,expected in provenance["selected_audio"].items()):
        raise ValueError("selected audio changed during experiment")
    report["status"]="complete"
    (output/"results.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
