"""Verify TTS experiment metrics and protection of existing/data-overlap runs."""
import argparse
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/"tools"))
import speech_tts_experiment as experiment


class TtsExperimentContracts(unittest.TestCase):
    def test_complete_metrics(self):
        text="TTS_ITEMS 32 NORMALIZED_MEL_MSE 0.125 STEPS 100\nDURATION_REFERENCE_MEAN_SECONDS 2.5 GENERATED_MEAN_SECONDS 2.6 MAE_SECONDS 0.3\n"
        result=experiment.metrics(text,32)
        self.assertEqual(result["normalized_mel_mse"],0.125)
        self.assertEqual(result["duration_mae_seconds"],0.3)
        self.assertEqual(result["steps"],100)

    def test_partial_and_invalid_metrics_fail(self):
        text="TTS_ITEMS 32 NORMALIZED_MEL_MSE 0.125 STEPS 100\nDURATION_REFERENCE_MEAN_SECONDS 2.5 GENERATED_MEAN_SECONDS 2.6 MAE_SECONDS 0.3\n"
        for invalid in [text.splitlines()[0],text.replace("32","31"),text.replace("0.125","nan"),
                        text.replace("0.125","-0.125"),text.replace("0.3","1e999")]:
            with self.subTest(invalid=invalid),self.assertRaises(ValueError):
                experiment.metrics(invalid,32)

    def test_existing_experiment_is_unchanged(self):
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary)
            checkpoint=output/"tts-expanded.tns"
            checkpoint.write_bytes(b"original model")
            argv=["speech_tts_experiment.py","--compiler","missing","--directory",str(output),
                  "--dataset-directory","missing","--aligner","missing","--initial-checkpoint","missing"]
            with patch.object(sys,"argv",argv),self.assertRaisesRegex(ValueError,"experiment exists"):
                experiment.main()
            self.assertEqual(checkpoint.read_bytes(),b"original model")
            self.assertEqual([p.name for p in output.iterdir()],["tts-expanded.tns"])

    def test_overlap_rejected_before_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            dataset=Path(temporary)
            source=dataset/"sample.wav"
            source.write_bytes(b"existence fixture only")
            for name in ["train.tsv","validation.tsv"]:
                (dataset/name).write_text(str(source)+"\tsame text\n",encoding="utf-8")
            output=dataset/"new"
            argv=["speech_tts_experiment.py","--compiler","missing","--directory",str(output),
                  "--dataset-directory",str(dataset),"--aligner","missing","--initial-checkpoint","missing",
                  "--train-items","1","--validation-items","1"]
            with patch.object(sys,"argv",argv),self.assertRaisesRegex(ValueError,"overlap"):
                experiment.main()
            self.assertFalse(output.exists())


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,default=ROOT/"build/verification/tts-experiment-contracts")
    args=parser.parse_args()
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(TtsExperimentContracts))
    args.out.mkdir(parents=True,exist_ok=True)
    summary={"pass":result.testsRun-len(result.failures)-len(result.errors),"fail":len(result.failures)+len(result.errors)}
    (args.out/"results.json").write_text(json.dumps({"summary":summary},indent=2)+"\n",encoding="utf-8")
    raise SystemExit(not result.wasSuccessful())
