"""Dataset orchestration and independent CER/WER; model execution stays native."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import unicodedata
from pathlib import Path


def normalize(text):
    text = unicodedata.normalize("NFKD", text.replace("’", "'")).lower()
    text = "".join(c for c in text if not unicodedata.combining(c))
    return " ".join(re.sub("[^a-z' ]", " ", text).split())


def native_normalize(rows, dataset, executable, output):
    """Use the production TezzNative TTS frontend for fresh TTS manifests."""
    expected = {}
    for row in rows:
        path = str((dataset / "wavs" / (row["id"] + ".wav")).resolve())
        if path in expected or any(c in row["text"] for c in "\t\r\n"):
            raise ValueError("duplicate audio or invalid transcript in native normalization input")
        expected[path] = row
    with tempfile.TemporaryDirectory(prefix="tts-normalize-", dir=output) as temporary:
        source, destination = Path(temporary) / "raw.tsv", Path(temporary) / "canonical.tsv"
        source.write_text("".join(path + "\t" + row["text"] + "\n" for path, row in expected.items()), encoding="utf-8")
        subprocess.run([str(Path(executable).resolve()), str(source), str(destination)], check=True,
                       capture_output=True, text=True, timeout=300)
        seen = set()
        for line in destination.read_text(encoding="utf-8").splitlines():
            fields = line.split("\t")
            if len(fields) != 2 or fields[0] not in expected or fields[0] in seen:
                raise ValueError("native text normalizer returned unknown or duplicate audio")
            path, text = fields
            if not text or not re.fullmatch("[a-z' ]+", text) or text != " ".join(text.split()):
                raise ValueError("native text normalizer returned noncanonical text")
            seen.add(path)
            expected[path]["normalized"] = text
        if seen != set(expected):
            raise ValueError("native text normalizer omitted recordings")


def normalizer_hashes(executable):
    path = Path(executable).resolve()
    return {"text_normalizer_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "text_normalizer_runtime_sha256": {
                artifact.name: hashlib.sha256(artifact.read_bytes()).hexdigest()
                for artifact in sorted(path.parent.iterdir())
                if artifact.suffix in (".dll", ".so", ".dylib")}}


def prepare(args):
    source = Path(args.source)
    metadata = json.loads((source / "dataset.json").read_text(encoding="utf-8"))
    rows, ownership = {}, {}
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    normalizer = getattr(args, "tts_normalizer", None)
    if any((output / name).exists() for name in ("train.tsv", "validation.tsv", "test.tsv", "provenance.json")):
        raise ValueError("dataset preparation requires a fresh output directory")
    for split in ("train", "validation", "test"):
        rows[split] = []
        for line in (source / (split + ".jsonl")).read_text(encoding="utf-8").splitlines():
            row = json.loads(line)
            if not normalizer:
                row["normalized"] = normalize(row["text"])
            rows[split].append(row)
    if normalizer:
        normalizer_inputs = normalizer_hashes(normalizer)
        native_normalize([row for split_rows in rows.values() for row in split_rows],
                         Path(metadata["dataset"]), normalizer, output)
        try:
            if normalizer_hashes(normalizer) != normalizer_inputs:
                raise ValueError("native text normalizer artifacts changed during invocation")
        except OSError as error:
            raise ValueError("native text normalizer artifacts changed during invocation") from error
    for split, split_rows in rows.items():
        for row in split_rows:
            ownership.setdefault(row["normalized"], set()).add(split)
    excluded = {text for text, splits in ownership.items() if len(splits) > 1}
    provenance = {"dataset_fingerprint": metadata["fingerprint_sha256"],
                  "normalized_overlap_groups_excluded": len(excluded), "splits": {},
                  "normalization": "NFKD ASCII lowercase, apostrophe retained, punctuation becomes whitespace",
                  "subset_order": "shortest duration, then utterance id; timing/prototype subset only"}
    if normalizer:
        provenance["normalization"] = "native TezzNative tts_normalize_text; shared training/inference contract"
        provenance.update(normalizer_inputs)
    contents = {}
    for split, items in rows.items():
        accepted = [r for r in items if r["normalized"] and len(r["normalized"]) <= 448 and r["normalized"] not in excluded]
        accepted.sort(key=lambda r: (r["seconds"], r["id"]))
        lines = [str((Path(metadata["dataset"]) / "wavs" / (r["id"] + ".wav")).resolve()) + "\t" + r["normalized"] for r in accepted]
        content = ("\n".join(lines) + ("\n" if lines else "")).encode("utf-8")
        contents[split + ".tsv"] = content
        provenance["splits"][split] = {"count": len(accepted), "sha256": hashlib.sha256(content).hexdigest(),
                                       "first_ids": [r["id"] for r in accepted[:32]]}
    contents["provenance.json"] = (json.dumps(provenance, indent=2) + "\n").encode("utf-8")
    # Validate and stage every split before publishing; provenance is the last
    # committed file. Recoverable publication failures leave no partial dataset.
    with tempfile.TemporaryDirectory(prefix="prepare-", dir=output) as temporary:
        staged = Path(temporary)
        for name, content in contents.items():
            (staged / name).write_bytes(content)
        published = []
        try:
            for name in contents:
                (staged / name).replace(output / name)
                published.append(output / name)
        except OSError:
            for path in published:
                path.unlink()
            raise
    print(json.dumps(provenance, indent=2))


def distance(a, b):
    previous = list(range(len(b) + 1))
    for i, left in enumerate(a, 1):
        current = [i]
        for j, right in enumerate(b, 1):
            current.append(min(current[-1] + 1, previous[j] + 1, previous[j - 1] + (left != right)))
        previous = current
    return previous[-1]


def evaluate(args):
    references = dict(line.split("\t", 1) for line in Path(args.manifest).read_text(encoding="utf-8").splitlines())
    predictions, seen = [], set()
    for line in Path(args.predictions).read_text(encoding="utf-8").splitlines():
        path, text = line.split("\t", 1)
        if path not in references or path in seen:
            raise ValueError("unknown or repeated prediction path")
        seen.add(path)
        reference = references[path]
        predictions.append({"id": Path(path).stem, "reference": reference, "prediction": text,
                            "character_edits": distance(reference, text), "characters": len(reference),
                            "word_edits": distance(reference.split(), text.split()), "words": len(reference.split())})
    if not predictions:
        raise ValueError("no predictions")
    result = {"utterances": len(predictions),
              "cer": sum(p["character_edits"] for p in predictions) / sum(p["characters"] for p in predictions),
              "wer": sum(p["word_edits"] for p in predictions) / sum(p["words"] for p in predictions),
              "single_speaker_dataset": True, "examples": predictions}
    Path(args.output).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--source", default="build/speech/dataset")
    prep.add_argument("--output", default="build/speech/native")
    prep.add_argument("--tts-normalizer", type=Path,
                      help="native speech_text_normalize executable; create fresh TTS manifests")
    prep.set_defaults(run=prepare)
    score = sub.add_parser("evaluate")
    score.add_argument("manifest")
    score.add_argument("predictions")
    score.add_argument("output")
    score.set_defaults(run=evaluate)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
