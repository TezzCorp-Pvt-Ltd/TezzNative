"""Exercise speech experiment provenance and exact held-out subset checks."""
import argparse
import json
from pathlib import Path
import sys
import struct
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import speech_experiment as experiment
import speech_language as language


class ExperimentContracts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.configuration = {"train_manifest_sha256": "known", "train_items": 1, "validation_items": 1}
        self.checkpoint = self.directory / "stt-expanded.tns"
        self.best = self.directory / "stt-best.tns"
        self.history = self.directory / "expanded-experiment.json"
        self.state = self.directory / "experiment-state.json"

    def completed_epoch(self):
        self.checkpoint.write_bytes(b"model fixture")
        self.best.write_bytes(b"best fixture")
        self.history.write_text(json.dumps([{"epoch": 1, "cer": .5, "wer": .75}]), encoding="utf-8")
        state = {"configuration": self.configuration, "epochs": 1,
                 "checkpoint_sha256": experiment.digest(self.checkpoint),
                 "best_checkpoint_sha256": experiment.digest(self.best),
                 "history_sha256": experiment.digest(self.history)}
        self.state.write_text(json.dumps(state), encoding="utf-8")

    def test_new_experiment_is_empty(self):
        self.assertEqual(experiment.validate_resume(self.directory, self.configuration, False), [])

    def test_fresh_training_cannot_overwrite_checkpoint(self):
        self.completed_epoch()
        with self.assertRaisesRegex(ValueError, "already exists"):
            experiment.validate_resume(self.directory, self.configuration, False)

    def test_resume_requires_provenance(self):
        self.completed_epoch()
        self.state.unlink()
        with self.assertRaisesRegex(ValueError, "provenance"):
            experiment.validate_resume(self.directory, self.configuration, True)

    def test_recorded_epoch_can_resume(self):
        self.completed_epoch()
        history = experiment.validate_resume(self.directory, self.configuration, True)
        self.assertEqual(history[0]["epoch"], 1)

    def test_resume_rejects_changed_configuration(self):
        self.completed_epoch()
        with self.assertRaisesRegex(ValueError, "configuration"):
            experiment.validate_resume(self.directory, {**self.configuration, "train_items": 2}, True)

    def test_resume_rejects_changed_checkpoint(self):
        self.completed_epoch()
        self.checkpoint.write_bytes(b"another model")
        with self.assertRaisesRegex(ValueError, "checkpoint differs"):
            experiment.validate_resume(self.directory, self.configuration, True)

    def test_resume_rejects_changed_best_model(self):
        self.completed_epoch()
        self.best.write_bytes(b"another model")
        with self.assertRaisesRegex(ValueError, "best checkpoint"):
            experiment.validate_resume(self.directory, self.configuration, True)

    def test_resume_rejects_changed_history(self):
        self.completed_epoch()
        self.history.write_text('[{"epoch": 1, "cer": 0}]', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "history"):
            experiment.validate_resume(self.directory, self.configuration, True)

    def test_manifest_count_audio_and_uniqueness(self):
        audio = self.directory / "sample.wav"
        audio.write_bytes(b"existence fixture only")
        manifest = self.directory / "train.tsv"
        manifest.write_text(f"{audio}\tfirst text\n", encoding="utf-8")
        self.assertEqual(experiment.manifest_rows(manifest, 1), [(str(audio), "first text")])
        with self.assertRaisesRegex(ValueError, "fewer"):
            experiment.manifest_rows(manifest, 2)
        manifest.write_text(f"{audio}\tfirst text\n{audio}\tother text\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            experiment.manifest_rows(manifest, 1)
        audio.unlink()
        manifest.write_text(f"{audio}\tfirst text\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "missing"):
            experiment.manifest_rows(manifest, 1)

    def test_exact_validation_subset_is_required(self):
        predictions = self.directory / "predictions.tsv"
        predictions.write_text("different.wav\ttext\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "unexpected"):
            experiment.score_predictions([("selected.wav", "text")], predictions)

    def test_incomplete_and_duplicate_predictions_fail(self):
        predictions = self.directory / "predictions.tsv"
        predictions.write_text("first.wav\ttext\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "incomplete"):
            experiment.score_predictions([("first.wav", "text"), ("second.wav", "other")], predictions)
        predictions.write_text("first.wav\ttext\nfirst.wav\ttext\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            experiment.score_predictions([("first.wav", "text")], predictions)

    def test_metrics_are_true_edit_rates(self):
        predictions = self.directory / "predictions.tsv"
        predictions.write_text("first.wav\tcat sat\nsecond.wav\ta dox\n", encoding="utf-8")
        scores = experiment.score_predictions([("first.wav", "cat sat"), ("second.wav", "a dog")], predictions)
        self.assertAlmostEqual(scores["cer"], 1 / 12)
        self.assertAlmostEqual(scores["wer"], 1 / 4)

    def test_epoch_shuffle_is_reproducible_and_training_only(self):
        rows = [(f"train-{i}.wav", f"text {i}") for i in range(20)]
        first = experiment.epoch_rows(rows, 42, 1)
        self.assertEqual(first, experiment.epoch_rows(rows, 42, 1))
        self.assertCountEqual(first, rows)
        self.assertNotEqual(first, rows)
        self.assertNotEqual(first, experiment.epoch_rows(rows, 42, 2))
        self.assertEqual(rows[0], ("train-0.wav", "text 0"))

    def test_learning_rate_decay_has_a_floor(self):
        self.assertEqual(experiment.epoch_rate(1000, .5, 100, 1), 1000)
        self.assertEqual(experiment.epoch_rate(1000, .5, 100, 2), 500)
        self.assertEqual(experiment.epoch_rate(1000, .5, 100, 5), 100)
        self.assertEqual(experiment.epoch_rate(1000, .5, 100, 1000), 100)

    def test_initial_checkpoint_header(self):
        source = self.directory / "initial.tns"
        source.write_bytes(b"TNSTT01\0" + struct.pack("<9I", 1, 0x01020304, 16000, 400, 512, 160, 80, 5, 64))
        self.assertEqual(experiment.checkpoint_dimensions(source), {"context": 5, "hidden": 64})
        source.write_bytes(b"wrong")
        with self.assertRaisesRegex(ValueError, "TNSTT01"):
            experiment.checkpoint_dimensions(source)

    def test_fine_tuning_resume_checks_initial_metrics(self):
        self.configuration["initial_checkpoint_sha256"] = "initial"
        self.completed_epoch()
        baseline = self.directory / "initial-evaluation.json"
        baseline.write_text('{"cer": 0.5}', encoding="utf-8")
        state = json.loads(self.state.read_text())
        state["initial_evaluation_sha256"] = experiment.digest(baseline)
        self.state.write_text(json.dumps(state))
        self.assertEqual(len(experiment.validate_resume(self.directory, self.configuration, True)), 1)
        baseline.write_text('{"cer": 0}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "initial evaluation"):
            experiment.validate_resume(self.directory, self.configuration, True)

    def language_arguments(self):
        for split, text in [("train", "training words"), ("validation", "validation words"), ("test", "test words")]:
            audio = self.directory / (split + ".wav")
            audio.write_bytes(b"audio existence fixture")
            (self.directory / (split + ".tsv")).write_text(f"{audio}\t{text}\n", encoding="utf-8")
        self.checkpoint.write_bytes(b"initial")
        compiler = self.directory / "compiler"
        compiler.write_bytes(b"compiler fixture")
        return ["speech_language", "--compiler", str(compiler), "--checkpoint", str(self.checkpoint),
                "--training-manifest", str(self.directory / "train.tsv"),
                "--validation-manifest", str(self.directory / "validation.tsv"),
                "--test-manifest", str(self.directory / "test.tsv"),
                "--train-items", "1", "--validation-items", "1", "--test-items", "1",
                "--weights", "200000", "400000", "--insertions", "400000", "--out", str(self.directory / "output")]

    def test_text_fitting_rejects_test_audio_and_text_overlap_before_build(self):
        argv = self.language_arguments()
        with mock.patch.object(language, "run") as execute, mock.patch.object(sys, "argv", argv):
            target = self.directory / "test.tsv"
            target.write_text(f"{self.directory / 'train.wav'}\tunique text\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "test audio/text overlaps"):
                language.main()
            target.write_text(f"{self.directory / 'test.wav'}\ttraining words\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "test audio/text overlaps"):
                language.main()
            execute.assert_not_called()

    def test_final_test_runs_once_after_selection_and_cannot_select_model(self):
        argv = self.language_arguments()
        calls = []

        def execute(command, _env, _timeout):
            mode = command[1]
            calls.append(mode)
            if mode == "fit-text":
                self.assertNotIn("test", Path(command[2]).name)
                Path(command[4]).write_bytes(command[7].encode())
            elif mode == "evaluate":
                manifest, model, prediction = map(Path, command[2:5])
                rows = [row.split("\t") for row in manifest.read_text().splitlines()]
                is_test = manifest.name == "test-selected.tsv"
                if is_test:
                    self.assertEqual(calls.count("fit-text"), 2)
                    self.assertEqual(model.read_bytes(), b"200000")
                    calls.append("test")
                text = rows[0][1] if model.read_bytes() == b"200000" and not is_test else "wrong"
                prediction.write_text(rows[0][0] + "\t" + text + "\n", encoding="utf-8")
            return {"seconds": 0, "stdout": "test double; no native quality measurement"}

        with mock.patch.object(language, "run", side_effect=execute), mock.patch.object(sys, "argv", argv), mock.patch("builtins.print"):
            self.assertEqual(language.main(), 0)
        report = json.loads((self.directory / "output/results.json").read_text())
        self.assertEqual(calls.count("test"), 1)
        self.assertEqual(calls[-1], "test")
        self.assertFalse(report["test_split_used_for_selection"])
        self.assertEqual(report["final_test"]["wer"], 1)
        self.assertEqual((self.directory / "output/best.tns").read_bytes(), b"200000")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/verification/speech-experiment")
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ExperimentContracts)
    names = [case.id() for case in suite]
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    failed = {getattr(case, "test_case", case).id(): message for case, message in result.failures + result.errors}
    records = [{"case": name, "status": "fail" if name in failed else "pass", "error": failed.get(name)} for name in names]
    report = {"results": records, "summary": {"pass": result.testsRun - len(failed), "fail": len(failed)}}
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    raise SystemExit(not result.wasSuccessful())
