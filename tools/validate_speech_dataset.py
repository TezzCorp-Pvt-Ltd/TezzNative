"""Validate local LJSpeech data and make deterministic, transcript-grouped splits.

This is dataset preparation only. It does not train or claim a speech model.
The original dataset is never modified. No third-party packages are required.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
import unicodedata
import wave


def normalize_text(text):
    return " ".join(unicodedata.normalize("NFKC", text).split())


def inspect_dataset(dataset, output):
    metadata = dataset / "metadata.csv"
    if not metadata.is_file():
        raise ValueError(f"Missing {metadata}")
    # LJSpeech uses literal pipe-separated fields, not CSV quote escaping.
    lines = metadata.read_text(encoding="utf-8-sig").splitlines()
    seen = set()
    errors = []
    records = []
    text_counts = Counter()
    formats = Counter()
    chars = Counter()
    fingerprint = hashlib.sha256()
    total_frames = 0
    for index, line in enumerate(lines, 1):
        fields = line.split("|")
        if len(fields) != 3:
            errors.append(f"line {index}: expected exactly three pipe-separated fields")
            continue
        clip_id, original, normalized = fields
        if not re.fullmatch(r"[A-Za-z0-9_-]+", clip_id) or clip_id in seen:
            errors.append(f"line {index}: duplicate or invalid clip identifier")
            continue
        seen.add(clip_id)
        text = normalize_text(normalized)
        if not original.strip() or not text:
            errors.append(f"{clip_id}: empty transcript")
            continue
        audio = dataset / "wavs" / (clip_id + ".wav")
        try:
            with wave.open(str(audio), "rb") as wav:
                channels, width, rate, frames, compression, _ = wav.getparams()
                formats[f"{channels}ch/{width * 8}bit/{rate}Hz/{compression}"] += 1
                if (channels, width, rate, compression) != (1, 2, 22050, "NONE") or frames <= 0:
                    raise ValueError("expected nonempty mono 16-bit 22050 Hz PCM")
                # Verify actual data length as well as the declared RIFF length.
                remaining = frames
                audio_hash = hashlib.sha256()
                while remaining:
                    count = min(remaining, 65536)
                    chunk = wav.readframes(count)
                    if len(chunk) != count * channels * width:
                        raise ValueError("truncated PCM data")
                    audio_hash.update(chunk)
                    remaining -= count
        except (OSError, EOFError, wave.Error, ValueError) as error:
            errors.append(f"{clip_id}: {error}")
            continue
        # Identical normalized transcripts always stay in the same split.
        group = text.casefold()
        bucket = int.from_bytes(hashlib.sha256(group.encode("utf-8")).digest()[:8], "big") % 1000
        split = "test" if bucket < 50 else "validation" if bucket < 100 else "train"
        digest = audio_hash.hexdigest()
        records.append({"id": clip_id, "text": text, "frames": frames,
                        "seconds": frames / rate, "split": split, "pcm_sha256": digest})
        total_frames += frames
        text_counts[group] += 1
        chars.update(text)
        fingerprint.update(f"{clip_id}|{text}|{digest}\n".encode("utf-8"))
        if index % 1000 == 0:
            print(f"DATASET_PROGRESS {index}/{len(lines)}", flush=True)
    wav_ids = {path.stem for path in (dataset / "wavs").glob("*.wav")}
    orphans = sorted(wav_ids - seen)
    if orphans:
        errors.append(f"{len(orphans)} WAV files have no metadata entry")
    counts = Counter(record["split"] for record in records)
    summary = {
        "dataset": str(dataset.resolve()), "metadata_rows": len(lines),
        "verified_clips": len(records), "duration_hours": total_frames / 22050 / 3600,
        "formats": dict(formats), "splits": dict(counts),
        "duplicate_transcript_groups": sum(count > 1 for count in text_counts.values()),
        "transcript_overlap_between_splits": 0,
        "characters": sorted(chars), "fingerprint_sha256": fingerprint.hexdigest(),
        "speaker_count": 1,
        "stt_evaluation_limit": "These splits do not test recognition of unseen speakers or accents.",
        "errors": errors, "orphan_wavs": orphans,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "dataset.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    if errors:
        # A failed validation must not leave old manifests looking valid.
        for split in ("train", "validation", "test"):
            (output / f"{split}.jsonl").unlink(missing_ok=True)
        return summary
    for split in ("train", "validation", "test"):
        with (output / f"{split}.jsonl").open("w", encoding="utf-8", newline="\n") as stream:
            for record in records:
                if record["split"] == split:
                    stream.write(json.dumps(record, ensure_ascii=False) + "\n")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--out", type=Path, default=Path("build/speech/dataset"))
    args = parser.parse_args()
    summary = inspect_dataset(args.dataset, args.out)
    print(json.dumps({key: summary[key] for key in ("verified_clips", "duration_hours", "splits", "errors")}, indent=2))
    return 1 if summary["errors"] else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"Dataset validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
