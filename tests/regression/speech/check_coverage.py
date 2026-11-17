"""Independent contracts for representative native speech experiment orchestration."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import wave

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import speech_coverage as coverage
import speech_full_experiment as experiment
import speech_dataset as dataset


class CoverageContracts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.rows = []
        for i in range(12):
            audio = self.directory / f"{i}.wav"
            with wave.open(str(audio), "wb") as stream:
                stream.setparams((1, 2, 100, 0, "NONE", "not compressed"))
                stream.writeframes(struct.pack("<h", i) * (100 * (i + 1)))
            self.rows.append((str(audio), "words " + chr(97 + i)))
        self.manifest = self.directory / "train.tsv"
        coverage.write_manifest(self.manifest, self.rows)

    def test_full_selection_preserves_every_recording(self):
        selected, report, _ = coverage.select_rows(self.manifest)
        self.assertEqual(selected, self.rows)
        self.assertEqual(report["selected_items"], 12)
        self.assertAlmostEqual(report["hours"], 78 / 3600)

    def test_subset_spans_duration_distribution_reproducibly(self):
        selected, _, durations = coverage.select_rows(self.manifest, 3, 42)
        self.assertEqual(selected, coverage.select_rows(self.manifest, 3, 42)[0])
        lengths = [durations[p] for p, _ in selected]
        self.assertTrue(1 <= lengths[0] <= 4)
        self.assertTrue(5 <= lengths[1] <= 8)
        self.assertTrue(9 <= lengths[2] <= 12)

    def test_selection_does_not_depend_on_manifest_order(self):
        selected = coverage.select_rows(self.manifest, 4, 73)[0]
        coverage.write_manifest(self.manifest, list(reversed(self.rows)))
        self.assertEqual(selected, coverage.select_rows(self.manifest, 4, 73)[0])

    def test_invalid_selection_fails(self):
        for count in (-1, 13):
            with self.assertRaises(ValueError):
                coverage.select_rows(self.manifest, count)
        self.manifest.write_text("", encoding="utf-8")
        with self.assertRaises(ValueError):
            coverage.select_rows(self.manifest)

    def test_disjointness_checks_audio_and_text(self):
        coverage.assert_disjoint(self.rows[:6], self.rows[6:])
        for validation in ([self.rows[0]], [(self.rows[8][0], self.rows[0][1])]):
            with self.assertRaisesRegex(ValueError, "overlap"):
                coverage.assert_disjoint(self.rows[:6], validation)

    def test_audio_mutation_is_detected(self):
        hashes = coverage.audio_hashes(self.rows[:1])
        coverage.verify_audio(hashes)
        Path(self.rows[0][0]).write_bytes(b"changed audio")
        with self.assertRaisesRegex(ValueError, "audio changed"):
            coverage.verify_audio(hashes)

    def test_word_error_components_and_empty_prediction(self):
        self.assertEqual(coverage.edit_counts(["a", "cat"], ["a", "dog", "today"]),
                         {"substitutions": 1, "deletions": 0, "insertions": 1})
        self.assertEqual(coverage.edit_counts(["a", "cat"], []),
                         {"substitutions": 0, "deletions": 2, "insertions": 0})
        self.assertEqual(coverage.edit_counts([], ["hello"])["insertions"], 1)

    def test_scoring_accounts_for_all_durations_and_rejects_omissions(self):
        prediction = self.directory / "predictions.tsv"
        coverage.write_manifest(prediction, self.rows)
        durations = {p: i + 1 for i, (p, _) in enumerate(self.rows)}
        result = coverage.score(self.rows, prediction, durations)
        self.assertEqual(result["wer"], 0)
        self.assertEqual(sum(g["utterances"] for g in result["duration_groups"].values()), 12)
        coverage.write_manifest(prediction, self.rows[:2])
        with self.assertRaisesRegex(ValueError, "incomplete"):
            coverage.score(self.rows, prediction, durations)

    def test_selection_prioritizes_word_error(self):
        a, b = {"cer": .1, "wer": .8}, {"cer": .2, "wer": .7}
        self.assertEqual(min([a, b], key=coverage.selection_key), b)

    def test_native_normalization_requires_complete_exact_output(self):
        rows = [{"id": "sample", "text": "Mr. Fox"}]
        expected_path = str((self.directory / "wavs/sample.wav").resolve())
        for content in ("", "unknown.wav\tmister fox\n", expected_path + "\tMister Fox\n",
                        (expected_path + "\tmister fox\n") * 2):
            def execute(command, **_):
                Path(command[2]).write_text(content, encoding="utf-8")
            with mock.patch.object(dataset.subprocess, "run", side_effect=execute):
                with self.assertRaises(ValueError):
                    dataset.native_normalize(rows, self.directory, "native-normalizer", self.directory)

    def test_native_normalization_uses_the_executable_result(self):
        rows = [{"id": "sample", "text": "Mr. Fox"}]
        expected_path = str((self.directory / "wavs/sample.wav").resolve())
        def execute(command, **_):
            self.assertEqual(Path(command[1]).read_text(encoding="utf-8"), expected_path + "\tMr. Fox\n")
            Path(command[2]).write_text(expected_path + "\tmister fox\n", encoding="utf-8")
        with mock.patch.object(dataset.subprocess, "run", side_effect=execute):
            dataset.native_normalize(rows, self.directory, "native-normalizer", self.directory)
        self.assertEqual(rows[0]["normalized"], "mister fox")

    def native_double(self, command, _env, _timeout):
        mode = command[1]
        if mode == "buildexe":
            Path(command[3]).write_bytes(b"native executable test double")
        elif mode in ("train", "resume"):
            self.assertNotIn("test", Path(command[2]).name)
            Path(command[3]).write_bytes(b"checkpoint fixture")
        elif mode == "evaluate":
            rows = [line.split("\t") for line in Path(command[2]).read_text(encoding="utf-8").splitlines()]
            coverage.write_manifest(command[4], rows)
        return {"seconds": 0, "stdout": "ORCHESTRATION TEST DOUBLE; NOT MODEL QUALITY"}

    def runner_arguments(self):
        coverage.write_manifest(self.manifest, self.rows[:8])
        coverage.write_manifest(self.directory / "validation.tsv", self.rows[8:])
        (self.directory / "test.tsv").write_text("invalid: test data must never be opened", encoding="utf-8")
        compiler = self.directory / "compiler"
        compiler.write_bytes(b"compiler fixture")
        return ["speech_full_experiment", "--compiler", str(compiler), "--directory", str(self.directory / "run"),
                "--dataset-directory", str(self.directory), "--model", "legacy", "--epochs", "1"]

    def test_runner_covers_complete_split_and_resumes_with_provenance(self):
        argv = self.runner_arguments()
        with mock.patch.object(experiment, "run", side_effect=self.native_double), mock.patch("builtins.print"):
            with mock.patch.object(sys, "argv", argv):
                self.assertEqual(experiment.main(), 0)
            with mock.patch.object(sys, "argv", argv + ["--resume"]):
                self.assertEqual(experiment.main(), 0)
        state = json.loads((self.directory / "run/state.json").read_text())
        report = json.loads((self.directory / "run/run.json").read_text())
        self.assertEqual(state["epochs"], 2)
        self.assertEqual(report["training"]["selected_items"], 8)
        self.assertEqual(report["validation"]["selected_items"], 4)
        self.assertFalse(report["test_split_opened"])

    def test_runner_rejects_overwrite_changed_audio_and_checkpoint(self):
        argv = self.runner_arguments()
        with mock.patch.object(experiment, "run", side_effect=self.native_double), mock.patch("builtins.print"):
            with mock.patch.object(sys, "argv", argv):
                experiment.main()
                with self.assertRaisesRegex(ValueError, "already exists"):
                    experiment.main()
            checkpoint = self.directory / "run/current.tns"
            checkpoint.write_bytes(b"changed checkpoint")
            with mock.patch.object(sys, "argv", argv + ["--resume"]):
                with self.assertRaisesRegex(ValueError, "checkpoint or history changed"):
                    experiment.main()
            checkpoint.write_bytes(b"checkpoint fixture")
            with wave.open(self.rows[0][0], "wb") as stream:
                stream.setparams((1, 2, 100, 0, "NONE", "not compressed"))
                stream.writeframes(b"\x05\x00" * 100)
            with mock.patch.object(sys, "argv", argv + ["--resume"]):
                with self.assertRaisesRegex(ValueError, "configuration changed"):
                    experiment.main()

    def test_legacy_normalization_preserves_curly_apostrophes_only(self):
        self.assertEqual(dataset.normalize("M\u00fcller\u2019s"), "muller's")
        self.assertEqual(dataset.normalize("M\u00fcller\ufffds"), "muller s")

    def preparation_arguments(self, invalid_validation=False):
        source = self.directory / "source"
        source.mkdir()
        (source / "dataset.json").write_text(json.dumps({"dataset": str(self.directory),
            "fingerprint_sha256": "fixture"}), encoding="utf-8")
        for split in ("train", "validation", "test"):
            row = {"id": split, "text": "words " + split, "seconds": 1}
            if invalid_validation and split == "validation":
                del row["seconds"]
            (source / (split + ".jsonl")).write_text(json.dumps(row) + "\n", encoding="utf-8")
        executable = self.directory / "normalizer"
        executable.write_bytes(b"native normalizer fixture")
        return argparse.Namespace(source=source, output=self.directory / "prepared", tts_normalizer=executable)

    @staticmethod
    def normalization_double(rows, *_):
        for row in rows:
            row["normalized"] = row["text"]

    def test_prepare_validates_every_split_before_publication(self):
        args = self.preparation_arguments(invalid_validation=True)
        with mock.patch.object(dataset, "native_normalize", side_effect=self.normalization_double):
            with self.assertRaises((KeyError, ValueError)):
                dataset.prepare(args)
        for name in ("train.tsv", "validation.tsv", "test.tsv", "provenance.json"):
            self.assertFalse((args.output / name).exists(), "partial preparation published " + name)

    def test_prepared_hashes_match_actual_bytes(self):
        args = self.preparation_arguments()
        with mock.patch.object(dataset, "native_normalize", side_effect=self.normalization_double), mock.patch("builtins.print"):
            dataset.prepare(args)
        provenance = json.loads((args.output / "provenance.json").read_text())
        for split in ("train", "validation", "test"):
            actual = hashlib.sha256((args.output / (split + ".tsv")).read_bytes()).hexdigest()
            self.assertEqual(provenance["splits"][split]["sha256"], actual)

    def test_prepare_rolls_back_publication_failure(self):
        args = self.preparation_arguments()
        replace = Path.replace
        def fail_validation(path, target):
            if Path(target).name == "validation.tsv":
                raise OSError("injected publication failure")
            return replace(path, target)
        with mock.patch.object(dataset, "native_normalize", side_effect=self.normalization_double), mock.patch.object(Path, "replace", fail_validation):
            with self.assertRaisesRegex(OSError, "publication failure"):
                dataset.prepare(args)
        self.assertEqual(list(args.output.iterdir()), [])

    def test_prepare_never_overwrites_native_or_legacy_manifests(self):
        args = self.preparation_arguments()
        with mock.patch.object(dataset, "native_normalize", side_effect=self.normalization_double), mock.patch("builtins.print"):
            dataset.prepare(args)
        before = {p.name: p.read_bytes() for p in args.output.iterdir()}
        for normalizer in (args.tts_normalizer, None):
            args.tts_normalizer = normalizer
            with self.assertRaisesRegex(ValueError, "fresh output"):
                dataset.prepare(args)
            self.assertEqual(before, {p.name: p.read_bytes() for p in args.output.iterdir()})

    def test_resume_rejects_changed_deployed_executable_before_rebuilding(self):
        argv = self.runner_arguments()
        with mock.patch.object(experiment, "run", side_effect=self.native_double), mock.patch("builtins.print"):
            with mock.patch.object(sys, "argv", argv):
                experiment.main()
            executable = next((self.directory / "run").glob("speech_train*"))
            executable.write_bytes(b"changed deployed executable")
            with mock.patch.object(sys, "argv", argv + ["--resume"]):
                with self.assertRaisesRegex(ValueError, "executable|deployment|binary"):
                    experiment.main()

    def test_resume_rejects_changed_deployed_library_before_rebuilding(self):
        argv = self.runner_arguments()
        def execute(command, *arguments):
            result = self.native_double(command, *arguments)
            if command[1] == "buildexe":
                (Path(command[3]).parent / "tnrt.dll").write_bytes(b"deployed runtime fixture")
            return result
        with mock.patch.object(experiment, "run", side_effect=execute), mock.patch("builtins.print"):
            with mock.patch.object(sys, "argv", argv):
                experiment.main()
            (self.directory / "run/tnrt.dll").write_bytes(b"changed deployed runtime")
            with mock.patch.object(sys, "argv", argv + ["--resume"]):
                with self.assertRaisesRegex(ValueError, "library|deployment|runtime"):
                    experiment.main()

    def test_executable_mutation_during_epoch_is_not_committed(self):
        argv = self.runner_arguments()
        def execute(command, *arguments):
            result = self.native_double(command, *arguments)
            if command[1] in ("train", "resume"):
                Path(command[0]).write_bytes(b"changed during execution")
            return result
        with mock.patch.object(experiment, "run", side_effect=execute), mock.patch("builtins.print"), mock.patch.object(sys, "argv", argv):
            with self.assertRaisesRegex(ValueError, "executable|deployment|binary"):
                experiment.main()
        self.assertFalse((self.directory / "run/state.json").exists())

    def test_resume_rejects_different_runtime_redeployed_by_compiler(self):
        argv = self.runner_arguments()
        runtime = [b"first runtime"]
        def execute(command, *arguments):
            result = self.native_double(command, *arguments)
            if command[1] == "buildexe":
                self.assertIn("--repro", command)
                (Path(command[3]).parent / "tnrt.dll").write_bytes(runtime[0])
            return result
        with mock.patch.object(experiment, "run", side_effect=execute), mock.patch("builtins.print"):
            with mock.patch.object(sys, "argv", argv):
                experiment.main()
            runtime[0] = b"different runtime from SDK lookup"
            with mock.patch.object(sys, "argv", argv + ["--resume"]):
                with self.assertRaisesRegex(ValueError, "runtime library changed"):
                    experiment.main()

    def test_temporal_provenance_and_explicit_gpu_deployment(self):
        argv = self.runner_arguments()
        argv[argv.index("--model") + 1] = "temporal"
        library = self.directory / "explicit-gpu.bin"
        library.write_bytes(b"GPU orchestration test double, not a device library")
        argv += ["--device", "cuda", "--gpu-library", str(library)]
        with mock.patch.object(experiment, "run", side_effect=self.native_double), mock.patch("builtins.print"), mock.patch.object(sys, "argv", argv):
            experiment.main()
        state = json.loads((self.directory / "run/state.json").read_text())
        self.assertIn(str(Path("tools/speech_train.tn")), state["configuration"]["sources"])
        name = "tzgpu.dll" if os.name == "nt" else "libtzgpu-native.so"
        self.assertEqual((self.directory / "run" / name).read_bytes(), library.read_bytes())
        self.assertEqual(state["deployment"]["deployed_libraries_sha256"][name],
                         state["configuration"]["gpu_library_sha256"])

    def test_installed_sdk_layout_uses_its_own_root_and_sources(self):
        argv = self.runner_arguments()
        installed_root = self.directory / "installed-sdk"
        (installed_root / "tools").mkdir(parents=True)
        (installed_root / "lib").mkdir()
        for name in ("speech_full_experiment.py", "speech_coverage.py", "speech_experiment.py",
                     "speech_dataset.py", "speech_train.tn"):
            (installed_root / "tools" / name).write_bytes((ROOT / "tools" / name).read_bytes())
        (installed_root / "lib/stt.tn").write_bytes((ROOT / "lib/stt.tn").read_bytes())
        path = installed_root / "tools/speech_full_experiment.py"
        spec = importlib.util.spec_from_file_location("installed_speech_experiment_contract", path)
        installed = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(installed)
        self.assertEqual(installed.SDK, installed_root)
        def execute(command, environment, timeout):
            self.assertEqual(environment["TEZZ_SDK_ROOT"], str(installed_root))
            return self.native_double(command, environment, timeout)
        with mock.patch.object(installed, "run", side_effect=execute), mock.patch("builtins.print"), mock.patch.object(sys, "argv", argv):
            installed.main()
        state = json.loads((self.directory / "run/state.json").read_text())
        self.assertIn(str(Path("lib/stt.tn")), state["configuration"]["sources"])

    def test_prepare_rejects_normalizer_changed_during_invocation(self):
        args = self.preparation_arguments()
        def changed_normalizer(rows, *arguments):
            self.normalization_double(rows, *arguments)
            args.tts_normalizer.write_bytes(b"different normalizer after execution")
        with mock.patch.object(dataset, "native_normalize", side_effect=changed_normalizer):
            with self.assertRaisesRegex(ValueError, "normalizer.*changed"):
                dataset.prepare(args)
        self.assertFalse((args.output / "provenance.json").exists())

    def test_prepare_rejects_normalizer_runtime_changed_during_invocation(self):
        args = self.preparation_arguments()
        library = args.tts_normalizer.parent / "tnrt.dll"
        library.write_bytes(b"runtime before invocation")
        def changed_runtime(rows, *arguments):
            self.normalization_double(rows, *arguments)
            library.write_bytes(b"different runtime after execution")
        with mock.patch.object(dataset, "native_normalize", side_effect=changed_runtime):
            with self.assertRaisesRegex(ValueError, "normalizer.*changed"):
                dataset.prepare(args)
        self.assertFalse((args.output / "provenance.json").exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/speech-coverage")
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(CoverageContracts))
    args.out.mkdir(parents=True, exist_ok=True)
    coverage.atomic_json(args.out / "results.json", {"tests": result.testsRun,
                         "failures": len(result.failures), "errors": len(result.errors),
                         "model_quality_measured": False})
    raise SystemExit(not result.wasSuccessful())
