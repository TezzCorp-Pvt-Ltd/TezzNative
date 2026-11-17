"""Dataset selection and independent scoring; no model computation lives here."""
import hashlib
import json
import os
import random
import wave
from pathlib import Path

from speech_experiment import digest, manifest_rows, score_predictions


def select_rows(manifest, limit=0, seed=42):
    """Select across duration strata instead of taking the shortest prefix."""
    path = Path(manifest)
    count = len(path.read_text(encoding="utf-8").splitlines())
    if count == 0 or limit < 0 or limit > count:
        raise ValueError("invalid selection count")
    rows = manifest_rows(path, count)
    durations = {}
    for audio, _ in rows:
        with wave.open(audio, "rb") as stream:
            if stream.getframerate() <= 0 or stream.getnframes() <= 0:
                raise ValueError("empty or invalid audio")
            durations[audio] = stream.getnframes() / stream.getframerate()
    rows.sort(key=lambda row: (durations[row[0]], Path(row[0]).stem))
    limit = limit or count
    rng = random.Random(seed)
    chosen = [rows[rng.randrange(i * count // limit, (i + 1) * count // limit)]
              for i in range(limit)]
    seconds = [durations[audio] for audio, _ in chosen]
    return chosen, {"available_items": count, "selected_items": limit,
                    "selection": "one seeded sample per equal-count duration stratum",
                    "hours": sum(seconds) / 3600,
                    "minimum_seconds": min(seconds), "maximum_seconds": max(seconds)}, durations


def assert_disjoint(training, validation):
    canonical = lambda p: os.path.normcase(str(Path(p).resolve()))
    if ({canonical(p) for p, _ in training} & {canonical(p) for p, _ in validation}
            or {t for _, t in training} & {t for _, t in validation}):
        raise ValueError("training and validation overlap in audio paths or transcripts")


def audio_hashes(rows):
    return {path: digest(path) for path, _ in rows}


def verify_audio(hashes):
    for path, expected in hashes.items():
        if digest(path) != expected:
            raise ValueError("audio changed during experiment: " + path)


def write_manifest(path, rows):
    Path(path).write_text("".join(p + "\t" + t + "\n" for p, t in rows), encoding="utf-8")


def edit_counts(reference, prediction):
    """Unit-cost alignment with deterministic substitution/deletion/insertion ties."""
    previous = [(j, 0, 0, j) for j in range(len(prediction) + 1)]
    for i, left in enumerate(reference, 1):
        current = [(i, 0, i, 0)]
        for j, right in enumerate(prediction, 1):
            cost, subs, deletions, insertions = previous[j - 1]
            diagonal = (cost + (left != right), subs + (left != right), deletions, insertions)
            cost, subs, deletions, insertions = previous[j]
            deletion = (cost + 1, subs, deletions + 1, insertions)
            cost, subs, deletions, insertions = current[-1]
            insertion = (cost + 1, subs, deletions, insertions + 1)
            current.append(min((diagonal, deletion, insertion), key=lambda x: x[0]))
        previous = current
    _, substitutions, deletions, insertions = previous[-1]
    return {"substitutions": substitutions, "deletions": deletions, "insertions": insertions}


def score(references, predictions, durations):
    result = score_predictions(references, predictions)
    predicted = dict(line.split("\t", 1) for line in Path(predictions).read_text(encoding="utf-8").splitlines())
    counts = {key: 0 for key in ("substitutions", "deletions", "insertions")}
    groups = {}
    for audio, text in references:
        edits = edit_counts(text.split(), predicted[audio].split())
        for key, value in edits.items():
            counts[key] += value
        bucket = "under_3_seconds" if durations[audio] < 3 else "3_to_6_seconds" if durations[audio] < 6 else "6_seconds_and_longer"
        group = groups.setdefault(bucket, {"utterances": 0, "words": 0, "word_edits": 0})
        group["utterances"] += 1
        group["words"] += len(text.split())
        group["word_edits"] += sum(edits.values())
    for group in groups.values():
        group["wer"] = group["word_edits"] / group["words"]
    return {**result, "utterances": len(references), "word_errors": counts, "duration_groups": groups}


def selection_key(metrics):
    return metrics["wer"], metrics["cer"]


def atomic_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()
