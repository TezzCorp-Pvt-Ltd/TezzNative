"""Exercise the native .tn TTS normalizer and transactional manifest CLI."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/tts-text")
    parser.add_argument("--dataset", type=Path, help="Optional local LJSpeech split JSONLs for full-corpus normalization")
    parser.add_argument("--legacy-manifests", type=Path, help="Optional existing canonical train/validation/test TSVs; read only")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEZZ_SDK_ROOT=str(SDK))
    results = []

    def run(argv, success=True):
        result = subprocess.run([str(arg) for arg in argv], cwd=ROOT, env=env,
                                text=True, encoding="utf-8", capture_output=True, timeout=120)
        if (result.returncode == 0) != success:
            raise AssertionError(f"{argv}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
        return result

    def verify(name, action):
        try:
            action()
            results.append({"name": name, "status": "pass"})
        except Exception as error:
            results.append({"name": name, "status": "fail", "error": str(error)})

    executable = output / ("speech_text_normalize.exe" if os.name == "nt" else "speech_text_normalize")
    run([compiler, "buildexe", ROOT / "tools/speech_text_normalize.tn", executable, "--verify"])
    fixture = ROOT / "tests/regression/speech/text_normalization.tn"
    verify("native_language_label_contract_bytecode", lambda: run([compiler, "run", fixture, "--bc"]))
    native_fixture = output / ("text_normalization.exe" if os.name == "nt" else "text_normalization")
    run([compiler, "buildexe", fixture, native_fixture, "--verify"])
    verify("native_language_label_contract_executable", lambda: run([native_fixture]))

    with tempfile.TemporaryDirectory(prefix="tts-text-", dir=output) as directory:
        folder = Path(directory)

        def successful_manifest():
            source, target, repeated = (folder / name for name in ("input.tsv", "output.tsv", "again.tsv"))
            source.write_bytes(('C:\\speech data\\a.wav\tMr. Müller’s “café”, 3.14!\r\n'
                                'relative/b.wav\tmrs. Fox and DR. Neild\n'
                                'c.wav\tplain words don\'t change').encode("utf-8"))
            expected = ("C:\\speech data\\a.wav\tmister muller's cafe three point one four\n"
                        "relative/b.wav\tmissus fox and doctor neild\n"
                        "c.wav\tplain words don't change\n").encode("utf-8")
            result = run([executable, source, target])
            assert "TTS_NORMALIZED_ITEMS 3" in result.stdout
            assert target.read_bytes() == expected
            run([executable, target, repeated])
            assert repeated.read_bytes() == expected
            run([executable, source, target], success=False)
            assert target.read_bytes() == expected
            run([executable, source, source], success=False)
            assert not Path(str(target) + ".tmp").exists()

        verify("manifest_paths_crlf_last_line_idempotence_no_overwrite", successful_manifest)

        invalid_rows = {
            "unsupported_unicode": "x.wav\t你好\n".encode("utf-8"),
            "unsupported_symbol": b"x.wav\tprice $5\n",
            "malformed_utf8": b"x.wav\tbad\xc3\n",
            "embedded_nul": b"x.wav\tone\x00two\n",
            "missing_tab": b"not a manifest row\n",
            "empty_path": b"\thello\n",
            "empty_text": b"x.wav\t\n",
            "blank_text": b"x.wav\t  \n",
            "extra_tab": b"x.wav\thello\tworld\n",
            "oversized_text": b"x.wav\t" + b"a" * 8193 + b"\n",
            "oversized_row": b"a" * 16385 + b"\thello\n",
            "blank_row": b"\n",
        }
        for name, invalid in invalid_rows.items():
            def reject(data=invalid, case=name):
                source, target = folder / (case + ".tsv"), folder / (case + ".out.tsv")
                source.write_bytes(b"valid.wav\thello world\n" + data)
                run([executable, source, target], success=False)
                assert not target.exists(), "partial normalized manifest was published"
                assert not Path(str(target) + ".tmp").exists(), "temporary output leaked"
            verify(name + "_rejects_without_partial_output", reject)

        if args.dataset:
            def full_corpus():
                rows = [json.loads(line) for split in ("train", "validation", "test")
                        for line in (args.dataset / (split + ".jsonl")).read_text(encoding="utf-8").splitlines()]
                source, target, repeated = (folder / name for name in ("raw-corpus.tsv", "corpus.tsv", "corpus-again.tsv"))
                source.write_text("".join(row["id"] + ".wav\t" + row["text"] + "\n" for row in rows), encoding="utf-8")
                result = run([executable, source, target])
                assert f"TTS_NORMALIZED_ITEMS {len(rows)}" in result.stdout
                normalized = target.read_text(encoding="utf-8").splitlines()
                assert len(normalized) == len(rows)
                for row, line in zip(rows, normalized):
                    path, text = line.split("\t")
                    assert path == row["id"] + ".wav"
                    assert text and all("a" <= c <= "z" or c in "' " for c in text)
                run([executable, target, repeated])
                assert target.read_bytes() == repeated.read_bytes()
            verify("full_ljspeech_corpus_normalizes_idempotently", full_corpus)

        if args.legacy_manifests:
            def legacy_manifests():
                for split in ("train", "validation", "test"):
                    source = args.legacy_manifests / (split + ".tsv")
                    target = folder / ("legacy-" + split + ".tsv")
                    run([executable, source, target])
                    assert source.read_text(encoding="utf-8").splitlines() == target.read_text(encoding="utf-8").splitlines(), split + " paths or transcripts changed"
            verify("existing_canonical_manifest_paths_and_text_remain_identical", legacy_manifests)

    report = {"scope": "native TezzNative text preprocessing; no speech quality claim",
              "results": results, "passed": sum(r["status"] == "pass" for r in results),
              "failed": sum(r["status"] == "fail" for r in results)}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if report["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
