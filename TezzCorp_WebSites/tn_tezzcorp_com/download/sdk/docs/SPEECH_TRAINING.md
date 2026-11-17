# Native speech prototypes

The STT and TTS modules train their own parameters from paired WAV/text data.
They use TezzNative orchestration and the SDK's C kernels, with no external
speech model, OS speech service, canned transcript, or recorded-audio retrieval.
These are small research baselines. A checkpoint loading successfully does not
establish recognition accuracy or intelligibility.

The legacy MLP training lane explicitly uses CPU execution. The new temporal
STT model is defined in TezzNative source and has an explicit CPU mode and a
CUDA-encoder/CPU-CTC mode, described below. Selecting CUDA never silently selects
CPU execution for the encoder.

## Architecture and checkpoints

Audio is validated PCM WAV, resampled to 16 kHz, using a 400-sample Hann window,
512-point FFT, 160-sample hop, and 80 triangular HTK mel bands from 20 Hz to 8 kHz.
Legacy STT uses eleven adjacent feature frames, a 64-unit ReLU hidden layer, and 29
character classes: CTC blank, space, apostrophe, and lower-case English letters.
CTC uses log-space forward/backward probabilities and native prefix beam
inference with a default beam width of 16. It sums alternative CTC paths for each text
prefix, including the separate blank/nonblank states needed for repeated
characters. An optional native character four-gram model learns aggregate
counts from the training split's text only. Its interpolated probabilities
contribute a configurable score when a prefix gains a character; repeated CTC
paths do not repeatedly add that score. An explicit insertion score controls
length bias. No external language model, lexicon, transcript lookup, or dataset
access is used during inference. Acoustic parameters and Adam state are
float32; CTC sums and decoder scores are float64.

TTS learns character context, within-character position, and utterance position
to predict log-mel frames. A separate network learns character durations from
the native STT model's monotonic forced alignment. Native nonnegative least
squares approximates linear power spectra; 64 bounded fast Griffin-Lim iterations reconstruct
phase. This is a trained acoustic/duration model with an algorithmic vocoder.
Its small capacity and alignment errors can limit intelligibility substantially.
The synthetic voice presets do not represent separately trained speakers.

STT `TNSTT02` and TTS `TNTTS01` checkpoints have separate magic values, version,
frontend dimensions, finite bounded parameters, optimizer state, trained-step
counts, and CRC32 checks. Loading rejects corrupt, incompatible, empty, or
untrained files. Initialized training states cannot recognize or synthesize.
Checkpoints contain no dataset audio or transcript lookup table. STT still
loads legacy `TNSTT01` models with the original width-16 decoder and no language
prior. New saves use `TNSTT02`, which adds CRC-protected decoder settings and,
when trained, about 2.93 MB of aggregate four-gram counts with a separate CRC.
Text-model fitting leaves acoustic parameters, optimizer state, and trained
acoustic step counts unchanged. It does not change TTS forced alignment.

## Reproducible LJSpeech experiment

The dataset verifier first creates disjoint manifests in `build/speech/dataset`.
Run `python tools/speech_dataset.py prepare` to create native-driver TSV files
and provenance hashes. Text normalization folds accents, keeps apostrophes,
and replaces punctuation with spaces. Any normalized transcript present in
multiple splits is excluded from every split. The measured prototype subsets
are ordered by duration, then utterance ID; these are not random population
estimates. LJSpeech's single speaker cannot establish general speaker accuracy.

Start a new experiment in a new directory, preparing the audio paths on the
same host that will execute training. Use `PATH/tezzc` on Linux:

```
python tools/speech_dataset.py prepare --output build/speech/experiment-v2
python tools/speech_experiment.py --compiler PATH/tezzc.exe --directory build/speech/experiment-v2 --epochs 20 --train-items 1000 --validation-items 128
```

This builds the TN training executable, deterministically shuffles the selected
training rows each epoch, trains one epoch at a time, independently
computes validation CER/WER, and retains the checkpoint with the lowest CER.
The test split is not used for training or checkpoint selection. A fresh run
refuses to overwrite an existing checkpoint or experiment history. Add
`--resume` with the same directory, compiler, and subset sizes to continue a
recorded experiment, preserving optimizer state. Its `experiment-state.json`
checks the compiler/runtime artifacts, native speech sources, training driver,
manifests, completed history, current checkpoint, and best checkpoint against
their recorded hashes. Missing or changed provenance fails before training.
Historical experiments created before this provenance file cannot silently
resume through this orchestrator; their checkpoints still load through the
individual native `speech_train resume` command below. No old checkpoint or
history is migrated or overwritten automatically.

The default acoustic recipe starts at 1,000 learning-rate ppm, multiplies it
by 0.95 each epoch, and stops decreasing at 100 ppm. Configure these with
`--learning-rate-ppm`, `--rate-decay`, and `--minimum-rate-ppm`; `--shuffle-seed`
controls the reproducible per-epoch order. `--initial-checkpoint` starts a new
fine-tuning experiment, evaluates that checkpoint before updating it, and
records its hash and baseline metrics. The original file is preserved.
`--hidden` and `--context` control new-model dimensions; resumed dimensions must
match the checkpoint. Per-epoch order hashes and rates are recorded.

Selected training and validation audio paths and normalized transcripts must
be disjoint. Scoring requires exactly the selected validation utterances, with
no duplicates, omitted rows, or extra predictions. Windows and Linux use their
own executable suffixes and path separators.

For individual jobs, build `tools/speech_train.tn`, then invoke:

```
speech_train train train.tsv stt.tns 20 1000
speech_train resume train.tsv stt.tns 1 1000
speech_train resume train.tsv stt.tns 1 1000 500 64 5
speech_train fit-text train.tsv stt.tns stt-with-text.tns 11811 32 400000 400000
speech_train evaluate validation.tsv stt.tns predictions.tsv 128
python tools/speech_dataset.py evaluate validation.tsv predictions.tsv metrics.json
```

The three optional train/resume arguments are learning-rate ppm, hidden width,
and context radius. `fit-text` resets and fits the native text prior, then saves
a separate checkpoint with beam width, language weight ppm, and insertion ppm.
To compare a bounded parameter grid on validation audio, use:

```
python tools/speech_language.py --compiler PATH/tezzc.exe --checkpoint PATH/stt-best.tns --training-manifest PATH/train.tsv --validation-manifest PATH/validation.tsv --train-items 11811 --validation-items 128 --width 32 --weights 200000 400000 --insertions 400000 800000 --out build/speech/new-language-experiment
```

The grid selects by validation WER, then CER for a tie, while reporting both.
Its output directory must be new or empty. Add `--test-manifest PATH/test.tsv
--test-items 619` only for the final experiment: after validation selection
finishes, the frozen model is evaluated once on that disjoint split. Test
scores cannot select a checkpoint or change decoder settings. Training,
validation, and test paths and normalized texts are checked for overlap.
Compiler, runtime, source, manifest, checkpoint, and prediction hashes are
recorded, and all validation/test audio hashes are checked before and after
scoring. Aggregate language-model fitting reads only training text.

Build `tools/speech_tts_train.tn` for real aligned acoustic training and synthesis:

```
speech_tts_train train train.tsv stt.tns tts.tns 20 128
speech_tts_train validate validation.tsv stt.tns tts.tns 1 32
speech_tts_train synthesize tts.tns "hello world" output.wav
```

Validation reports teacher-forced normalized log-mel MSE, using validation audio
only for alignment and scoring. It does not update either model. This metric
does not measure generated speech intelligibility; generated WAV validity and
nonzero samples likewise do not prove intelligibility. No audio hardware is
required for training, inference to file, or these checks.

The initial measured STT run used 128 training clips for 20 epochs: 2,560 updates
in 87.46 seconds, mean CTC loss 3.9056 to 2.5090. On 32 disjoint validation clips
CER was 93.53% and WER was 100%. This establishes functioning training and a
poor baseline, not usable speech recognition. Expanded experiment results are
written to `build/speech/native/expanded-experiment.json` after each epoch.

The expanded CPU run completed 1,000 training clips for 20 epochs (20,000
updates). On the fixed 128-clip validation subset, the selected epoch-20 model
originally measured 64.22% CER and 98.22% WER with greedy decoding. Selection
used validation CER; these are not test-set results. Each training epoch took
about 60–63 seconds, plus about 9.8 seconds for validation on this machine.

The native prefix beam decoder was compared against that exact unchanged
checkpoint and validation subset, without training or a language model:

| Decoder | Character error | Word error | CPU evaluation, 128 clips |
| --- | ---: | ---: | ---: |
| Previous greedy decoder | 64.22% | 98.22% | 10.52 s |
| Prefix beam, width 16 | 57.75% | 96.44% | 13.01 s |

Recognition remains poor despite the improvement. These are validation results
from one checkpoint and one timed execution per decoder, not a test-set or
latency benchmark. The comparison records compiler, executable, deployed
runtime, checkpoint, manifest, and prediction hashes and checks that its input
files remain unchanged. Before/after executables use separate directories so
their runtime libraries cannot overwrite each other. Reproduce with:

```
python tools/speech_compare.py --before-executable PATH/archived/speech_train.exe --after-compiler PATH/new/tezzc.exe --checkpoint build/speech/native/stt-best.tns --manifest build/speech/native/validation.tsv --items 128
```

The measured report is `build/speech/decoder-comparison-final/results.json`.
An archived executable runs beside its original runtime libraries; this avoids
linking the current driver against an old runtime without the new text-model
ABI. The historical result was reproduced with the current runtime in
`build/speech/language-legacy-parity/results.json`.

### Acoustic fine-tuning and native text prior

Five additional shuffled epochs on the same 1,000 training clips used learning
rates 500, 450, 405, 365, and 328 ppm. The resulting 25,000-update acoustic model
improved validation CER but worsened WER. That alone is not an improvement on
both recognition measures. The native character prior was then fitted from all
11,811 eligible training transcripts, with no validation or test text included.
Validation selected beam width 32, language weight 0.4, and insertion score 0.4
from a bounded grid, using WER first and CER for ties.

| Model on the same 128 validation clips | Character error | Word error |
| --- | ---: | ---: |
| Original 20,000 updates, beam 16 | 57.75% | 96.44% |
| Fine-tuned 25,000 updates, beam 16 | 53.28% | 97.77% |
| Selected 25,000 updates, native prior, beam 32 | 51.72% | 86.29% |
| Expanded 35,000 updates, native prior, beam 32 | 55.44% | 86.46% |

The expanded run added five shuffled epochs on 2,000 training clips, starting
from the 25,000-update checkpoint and decaying 500 ppm by 0.95 each epoch. It
completed all 10,000 updates, but did not outperform the selected model. Its
acoustic-only final CER/WER was 56.37%/97.15%; the last table row uses the same
already-chosen prior settings. Neither failed improvement nor extra updates
were discarded from the recorded history.

These are validation-tuned measurements, not unbiased test estimates. They
still describe poor recognition. Acoustic histories are in
`build/speech/shuffled-pilot/` and `build/speech/expanded-acoustic-v2/`; prior
comparisons are in `build/speech/finetuned-language-grid/` and
`build/speech/expanded-acoustic-language-validation/`. The selected combined
model contains 25,000 acoustic updates and aggregate counts from training text;
it cannot retrieve training recordings or stored sentences at inference time.

After that selection was frozen, the model was evaluated once on all 619
eligible, previously unused test clips: **CER 51.61%, WER 86.32%**. That experiment
performed no further training, decoder tuning or checkpoint selection after its
test. Subsequent experiments below use training/validation only. All 747 audio files
used for validation and test were hashed before scoring and rechecked afterward,
along with the input manifests and source acoustic checkpoint. Final artifacts
are in `build/speech/final-native-stt-v2/`, including `results.json`,
`test-metrics.json`, predictions, and `best.tns`. The selected checkpoint's
SHA256 is `8ce93ce0d966d95240d210355434971f17f1201b68a93a53f92403989a4d8372`.
These held-out results confirm poor single-speaker prototype recognition; they
do not establish usable transcription or generalization to other speakers.

TTS trained on 128 training clips for 20 epochs, then continued ten epochs with
duration error optimized in frames. Normalized training mel MSE fell from
0.252811 to 0.073485. On 32 disjoint validation clips, teacher-forced normalized
mel MSE was 0.121847. Reference duration averaged 2.1741 seconds, generated
duration 2.2025 seconds, and duration MAE was 0.3591 seconds. The duration-space
objective corrected the earlier geometric-mean bias (MAE 0.9478 seconds).
Native synthesis of `hello world` produced a valid 0.675-second mono 16 kHz WAV.
Audio intelligibility has not been established; these results do not justify a
claim of usable TTS. These measurements describe the initial checkpoint.
STT beam inference does not change the separate forced alignment used to train
TTS, so the decoder result does not establish a TTS improvement.

### Expanded TTS experiment and independent vocoder measurements

The next CPU experiment fine-tuned that initial TTS checkpoint on 1,000 training
clips for ten shuffled epochs (10,000 additional updates). It used the stronger
`stt-best.tns` checkpoint as a fixed forced aligner. Because this changes the
alignment targets, the historical 0.121847 mel MSE is not a comparable baseline.
The new experiment remeasured the original TTS checkpoint with exactly the same
aligner and held-out subset before training. Selection used only the 32-clip
validation mel MSE; epoch ten was selected. The first 64 duration-ordered test
clips were evaluated once after selection and did not drive another update.

| Measurement | Initial TTS | Selected TTS |
| --- | ---: | ---: |
| Validation normalized mel MSE, 32 clips | 0.145506 | 0.062889 |
| Validation duration MAE, seconds | 0.359063 | 0.210000 |
| Final test normalized mel MSE, 64 clips | 0.145051 | 0.064406 |
| Final test duration MAE, seconds | 0.433438 | 0.206094 |

These are teacher-forced acoustic and free-duration measurements, not speech
recognition accuracy or listener intelligibility. The selected model contains
13,840 total updates and retains the `TNTTS01` checkpoint format. Artifacts,
exact executed orchestration, hashes, per-epoch metrics, and final test output
are in `build/speech/tts-expanded-v2/`. The final evaluation rechecked the 1,032
training/validation audio hashes and input checkpoint hashes. Four synthesized
WAV samples are included, but WAV validity does not establish understandable speech.

Reproduce the training in a new directory with:

```
python tools/speech_tts_experiment.py --compiler PATH/tezzc.exe --directory build/speech/tts-new --dataset-directory build/speech/native --aligner build/speech/native/stt-best.tns --initial-checkpoint build/speech/native/tts-prototype.tns --epochs 10 --train-items 1000 --validation-items 32
```

The vocoder now uses 64 fast Griffin-Lim updates with momentum 0.9, following
the update described in [the primary algorithm discussion, Algorithm 2](https://ltfat.org/notes/ltfatnote059.pdf).
This is a bounded empirical setting, not a convergence guarantee. An independent
Python FFT/mel oracle measured reference-mel reconstruction on the same 16
validation recordings: mean relative mel-power error improved from **0.194683
to 0.040970** (worst clip 0.528806 to 0.219337). Total native synthesis time for
those clips increased from 1.26 to 2.90 seconds in one Windows execution; this
is not a general latency benchmark. The synthetic tone, harmonics, and chirp
regressions each require error below 0.1. Boundary checks include invalid
buffers, nonfinite inputs, deterministic output, and Linux ASan/UBSan leaks.

Independent [STOI](https://ceestaal.nl/code/) and
[ESTOI](https://doi.org/10.1109/TASLP.2016.2585878) reconstruction proxies were
also measured using the reviewed, pinned `pystoi==0.4.1` evaluation package.
For those 16 reference-mel recordings, mean STOI improved **0.969544 to 0.990010**
and ESTOI **0.943034 to 0.979913**. Identity, constant-gain and noise controls
were checked on every recording. Both reconstructed signals preserve the
reference mel's frame times; only the incomplete final analysis hop (less
than 10 ms) is omitted from the recording. There is no delay search, time
warping, or duration fitting. These scores cover the vocoder supplied with
reference mel frames, **not the full text-to-speech system**. Full TTS predicts
different character durations, so raw time-aligned STOI would be invalid.
No listening-test result, MOS, percentage of words understood, or established
full-TTS intelligibility is claimed.

`tools/speech_vocoder_quality.py` records dependency versions, evaluator/source
hashes, library/audio hashes, controls, and individual scores. Its optional
NumPy/SciPy/pystoi dependencies are measurement tools only; native production
training and synthesis do not call them. Reproduce with separately saved
before/after vocoder libraries:

```
python -m pip install --target build/verification/speech-metrics-deps pystoi==0.4.1 scipy==1.16.3
python tools/speech_vocoder_quality.py --before-library PATH/old/vocoder.dll --after-library PATH/new/vocoder.dll --manifest build/speech/native/validation.tsv --items 16 --metric-deps build/verification/speech-metrics-deps --out build/verification/vocoder-proxy-new/results.json
python tests/regression/speech/check_vocoder.py
python tests/regression/speech/check_tts_experiment.py
```

The measured proxy report is `build/verification/vocoder-intelligibility-proxy/results.json`.
The independent signal oracle and four experiment-contract tests pass on Windows
and Linux. A reproduced duration bug is also fixed: synthesis used to truncate
learned durations at 100 frames even though training accepted 3,000. A native
150-frame regression now reconstructs all 150 frames and rejects a 100-frame
output buffer instead of silently truncating it.

CTC follows the original forward/backward formulation in [Graves et al. (2006)](https://www.cs.toronto.edu/~graves/icml_2006.pdf).
The independent exhaustive-path and finite-difference oracle is in
`tests/regression/speech/check_ctc.py`; the native STT/TTS tests verify learning,
checkpoint identity, malformed-file rejection, and bounded output.

Prefix beam decoding follows [Hannun et al. (2014)](https://arxiv.org/abs/1408.2873).
`tests/regression/speech/check_decode.py` compares native results with an
independent exhaustive CTC-path enumeration, checks repeated characters,
invalid inputs and output capacity, and runs ASan/UBSan boundary and leak
checks on Linux. It also reproduces and guards a CTC log-softmax precision bug:
adding a large common offset to equal logits previously changed a positive
loss into a negative loss. Subtracting the maximum before the log-sum preserves
the loss and its gradients under that offset.

```
python tests/regression/speech/check_decode.py
python tests/regression/speech/check_language.py
python tests/regression/speech/check_experiment.py
python tests/verify_native.py
```

The decoder/precision checks passed 43 cases on Windows and 44 on Linux
(including the Linux sanitizer executable). Experiment contract checks passed
18 cases on each host; native numerical, speech training, checkpoint, duration,
and argument checks passed in the broader native oracle runner on both hosts.
An actual two-clip training run followed by resume completed two epochs and
four updates with its history and checkpoint provenance intact. These checks
establish the tested mechanics, not production speech quality.

The separate native language-model oracle passes 15 checks on each host:
normalized/backoff probabilities against independently counted text,
exhaustive CTC-path and language-score comparisons, invalid-input atomicity,
legacy checkpoint loading, CRC rejection, and inference after the checkpoint
file has been removed. Linux sanitizer checks also cover native text fitting,
prefix bounds, decoding, and memory release. Public `.tn` wrappers are exercised
through both bytecode and native executables. Experiment tests verify that test
audio/text overlap fails before fitting and final test scoring cannot choose
another checkpoint.

## TezzNative temporal STT and full-data experiments

`lib/stt_temporal.tn` has four submodules: `state`, `ops`, `checkpoint`, and
`manifest`. The first three define the model in `.tn`: parameter layout and
initialization, layer composition, backward composition, gradient clipping,
Adam updates, and checkpoint encoding.
The `manifest` submodule provides bounded manifest reading and character-label
encoding. The driver imports these libraries by stable module names, so relative
and absolute driver paths work from other working directories.
`tools/speech_temporal_train.tn` performs audio loading, training and inference.
Generic convolution/optimizer kernels and CTC dynamic programming execute in
the native runtime. No external ML framework, pretrained model, speech service,
or transcript retrieval implements the model.

The initial architecture has three centered temporal convolutions with kernel
width five and dilations 1, 4, and 16, each followed by ReLU, then a width-one
29-class output convolution. Hidden width 64 gives 68,637 parameters. The input
receptive field is 85 mel frames, or 865 ms including the 25 ms analysis window.
This is an experimental offline encoder; it is not a streaming recognizer.

Parameters, activations and Adam buffers use float32. CTC probabilities and
scores accumulate in float64. The objective and its gradients are divided by
the target-label count, and the parameter-gradient norm is clipped to five.
The initial driver updates once per utterance; it does not claim variable-length
minibatching, gradient accumulation, mixed precision or general GPU autograd.

Device 0 is CPU. Device 1 explicitly runs the encoder, backward convolutions and
Adam on CUDA, with native CPU CTC, beam decoding and a TezzNative host gradient
norm calculation. Logits and gradients cross the host/device boundary. GPU
absence or execution failure is an error. This is a heterogeneous implementation,
not a claim that the entire speech pipeline executes on the GPU.

The separate `TNTC001` checkpoint stores the architecture/frontend contract,
parameters, Adam moments, update count, initialization seed and bias-correction
powers under CRC32. Existing `TNSTT01`, `TNSTT02` and `TNTTS01` loaders remain
available through their original modules. The temporal model has no character
language prior; its initial evaluation uses native CTC beam width 16.

`tools/speech_full_experiment.py` is orchestration and independent scoring only.
Model computation and checkpoint IO execute in the TezzNative program. By default
the runner selects all 11,811 training recordings (21.563 hours) and all 670
validation recordings (1.218 hours). Optional smaller subsets sample across
duration strata, instead of taking only the shortest prefix. The earlier selected
1,000-clip models used 0.673 hours of distinct training audio.

The runner checks audio-path, transcript and file-content separation, hashes all
selected audio, records source/compiler/runtime/checkpoint identities, shuffles
training deterministically, and selects by validation WER followed by CER. It
reports word-error components and duration groups. It never opens the test
manifest. A changed source, binary, data file, configuration or checkpoint
prevents an unnoticed resume. Failed/incomplete first epochs require a fresh
experiment directory; they are not reported as completed training.

Build the checked-out runtime before compiling the new model:

```
python tests/run.py --build --suite regression --filter temporal --out build/verification/speech-next
python tools/speech_full_experiment.py --compiler build/verification/speech-next/compiler/tezzc.exe --dataset-directory build/speech/native --directory build/speech/temporal-new --model temporal --epochs 1
```

On Linux use `tezzc` without `.exe`, and prepare manifests on that host so WAV
paths are valid. CUDA runs additionally require `--device cuda --gpu-library
PATH/libtzgpu.so`, pointing to a verified complete native-ABI CUDA build. The
runner deploys it beside the executable and records its hash; there is no implicit
provider selection. Use a new output directory for each changed experiment, or
`--resume` with unchanged inputs to add complete epochs.

The temporal driver's `evaluate` command requires a new prediction output path.
Existing files, including alternate paths or hardlinks to the checkpoint or
manifest, are preserved and rejected as output. Failed evaluation removes only
the partial output it created.

For offline transcription of one WAV file, build the standalone command:

```
tezzc buildexe tools/speech_transcribe.tn build/speech_transcribe.exe --verify
build/speech_transcribe.exe path/to/model.tntc path/to/audio.wav
```

On Linux omit `.exe` from the executable name. The command accepts
`checkpoint wav [device=0 beam=16]` and prints the transcript. It needs a trained
temporal `TNTC001` checkpoint and no transcript manifest. Device 0 runs on CPU;
device 1 explicitly uses the CUDA encoder and CPU CTC/beam decoder and fails if
CUDA is unavailable. Beam width must be 1–128. Input must be PCM WAV, 8- or
16-bit, mono or stereo, at 8–192 kHz, with a maximum duration of 30 seconds.
Longer files, unsupported or malformed audio, invalid checkpoints and bad
arguments fail with a nonzero exit status. No speech service or fallback is used.
Transcription accuracy still depends on the checkpoint's measured quality.

Correct gradients, decreasing synthetic loss and successful checkpoint reloads
establish implementation mechanics. Speech quality requires separately reported
validation measurements. The historical 619-clip test remains a previously
reported benchmark and must not drive model or decoder selection.

### Implementation verification

The source-built broad suites passed 431 checks on Windows x64 and 433 on Linux
x64, with zero failures and seven/five platform skips respectively. Separate
checks cover numerical convolution gradients, all eight model parameter-gradient
blocks, loss reduction, checkpoint corruption, optimizer resume, and CPU/CUDA
checkpoint transfer. The CUDA run used the available GTX 1650 under WSL; its
encoder gradients and 20 Adam updates matched the CPU reference within the
declared tolerances, and CUDA memcheck reported no errors or leaks. This does
not establish support for other GPUs or operating systems.

A small real-audio learning check used two training-split clips and hidden width
16. After 880 updates, mean CTC loss fell from 16.905569 to 0.003554, and both
recordings decoded exactly as `two young women` and `they go on to say` on
Windows and Linux from the same checkpoint. This deliberately overfits two
training examples; it is a mechanics check, not a validation accuracy result.
Its report is `build/verification/temporal-real-smoke-results.json`.

The driver and standalone transcriber have 37 command-level checks on each OS,
including absolute/relative
source paths, training/resume, inference independent of reference text, and
checkpoint preservation. An output alias used to truncate a checkpoint after
loading it; prediction output now uses exclusive creation, protecting existing
files including hard links, and removes newly created partial output on failure.
The formatter and VM pointer-conversion regressions guard compiler bugs exposed
by this implementation. The full-data orchestrator has 26 checks per OS covering
selection, disjointness, independent scoring, deployment/source provenance,
installed-SDK layout and transactional dataset preparation.

### First full-data temporal run

The first temporal run trained from seed 42 at hidden width 64 and 1,000
learning-rate ppm for all 11,811 training recordings, using the explicit CUDA
encoder/CPU-CTC path. It completed 11,811 updates with mean training CTC loss
4.465567. On all 670 validation recordings it measured **86.64% CER and 96.93%
WER**, including 3,229 word substitutions, 7,848 deletions and nine insertions.
This unsuccessful quality experiment must not replace the better legacy model.
Its `best.tntc` is simply the sole completed checkpoint within that run.

An independent audit verified all predictions, metrics, duration groups, split
coverage, source/deployment hashes and checkpoint integrity. The checkpoint has
68,637 finite parameters and valid Adam state; Windows and Linux load it and
produce matching outputs. Its SHA256 is
`37b56dcf5f7c67f6bf59e392f9b25c88d18e6c2552f8083a929d175333fa2672`.
Artifacts are in `build/speech/temporal-full-v1/`; exact source, executable,
runtime and experiment artifacts are also preserved in
`build/verification/temporal-full-v1-reproduction.zip` (dataset audio is excluded).

Diagnostics on five duration-spread training clips found first-layer ReLU
activity falling from roughly 52% at initialization to 5–7% after training.
The letter `h` dominated many frame predictions, and repeated labels collapsed
to very short CTC outputs. This was not an entirely zeroed encoder or a purely
blank prediction. Real-shape CPU/GPU logits and gradients agreed; real-audio
parameter finite differences and a 1,008-frame CTC gradient check passed.
No numerical implementation defect was established. Initialization and training
schedule changes require controlled experiments before production changes.

### Controlled output-head initialization pilot

A matched comparison changed only the initial 1,856 output-head weights,
scaling them by 1.0 or 0.1 in a TezzNative initializer. Every other parameter,
zero bias, zero Adam buffer, seed, executable and runtime was identical. Both
models used the same 1,000 duration-stratified training clips (1.8256 hours),
128 validation clips, three epoch shuffles and constant 1,000-ppm learning rate.
All 1,128 selected audio hashes and split separation were independently checked.

| Epoch | Original head CER / WER | Smaller head CER / WER |
| --- | ---: | ---: |
| 1 | 76.39% / 120.30% | 98.92% / 99.95% |
| 2 | 89.95% / 99.37% | 96.69% / 99.64% |
| 3 | 86.48% / 100.95% | 83.98% / 99.41% |

WER can exceed 100% because insertions count as errors. At epoch three, smaller
initialization reduced mean training CTC loss from 4.703696 to 2.513837 and
improved both fixed-epoch validation metrics. However, WER-first checkpoint
selection retained original-head epoch two versus smaller-head epoch three:
2,193 versus 2,194 word errors out of 2,207 reference words. The smaller head
did not improve the best achieved WER, and neither model is usable.

This single-seed, small-subset pilot does not establish a reliable quality fix;
the production initializer remains unchanged. Artifacts are in
`build/speech/head-init-pilot-v1/`; the exact TezzNative initializer is
`build/verification/head_init_probe.tn`. These 128-clip results are
separate from the 670-clip full-data experiment. The test split stayed closed,
and concurrent pilot timings are not a performance comparison.

A read-only probe on the same five training clips found that the smaller head
retained more first-layer activity (34–41% versus 15–20%), but its outputs became
blank-dominated: 84–88% mean blank probability and 92–98% top-one blank frames.
The original head remained dominated by `h` on longer clips. These observations
explain different failure modes; they do not establish a reliable training recipe.

### Full-coverage legacy control

The existing 25,000-update model was evaluated on all 670 validation recordings,
then trained for one shuffled pass over all 11,811 training recordings at 500
learning-rate ppm. The prior/decoder settings were unchanged. Training completed
11,811 updates, reaching 36,811 total, with mean training CTC loss 1.731767.

| Same 670 validation recordings | Character error | Word error |
| --- | ---: | ---: |
| Existing model before full-data training | 51.35% | 86.05% |
| After one full-data pass | 54.10% | 85.45% |

WER improved slightly while CER worsened; this is not an improvement on both
measures. Word substitutions/deletions/insertions changed from 7,230/2,487/125 to
5,956/3,790/27. Recognition remains poor. The runner selected the later checkpoint
by its declared WER-first criterion, preserving the original checkpoint and
initial metrics. The 619-clip test split was not opened.

The report and checkpoints are in `build/speech/full-coverage-control-v3/`.
The run used the earlier captured compiler/runtime and orchestrator revision;
its recorded hashes identify those artifacts. Current source changes intentionally
prevent silently resuming that historical run through the newer orchestrator.
These validation results do not establish general speaker accuracy.

Because omissions increased, a separate validation comparison raised only the
decoder insertion score from 0.4 to 0.8, retaining beam width 32 and language
weight 0.4. On the same 670 clips, CER fell from 54.10% to **48.57%** and WER
from 85.45% to **83.30%**. Independent scoring verified all predictions; binary
comparison verified identical acoustic weights, Adam state, trained-step count
and character-prior counts. Only the insertion setting and its checksum changed.
The prior was fitted from training text only, and the test split stayed closed.
The selected checkpoint is
`build/speech/full-coverage-control-decoder-v3/best.tns`, SHA256
`925b9d460864b8dd58b59e84e220c993ef657aad4434202e1c0b3bdfe348e525`.
This is a validation-selected legacy-model improvement, not a new temporal-model
or test-set result. Recognition remains poor.

## Shared native TTS text processing

`lib/tts/text.tn` supplies `tts_normalize_text` and `tts_encode_text` to both
synthesis and the training driver. It handles bounded English abbreviation,
number, decimal, Latin-1 accent and curly-quote normalization; unsupported input
fails explicitly. The existing lowercase-character/apostrophe/space alphabet is
preserved. Punctuation still maps to spacing, so this change does not establish
learned sentence prosody or naturalness.

The LJSpeech source already expands numeric digits, but contains abbreviations
and accents. Fresh TTS manifests can use the production native normalizer:

```
tezzc buildexe tools/speech_text_normalize.tn build/speech_text_normalize.exe --verify
python tools/speech_dataset.py prepare --source build/speech/dataset --output build/speech/tts-new --tts-normalizer build/speech_text_normalize.exe
```

The CLI refuses overwrite, rejects malformed rows, and does not leave partial
output after failure. Preparation requires a fresh directory, recomputes
cross-split normalized-text exclusion and records executable/runtime hashes.
All 13,100 LJSpeech transcripts passed native normalization and idempotence checks
on Windows and Linux; existing canonical manifest paths and transcript strings
remain unchanged. Keep historical manifests and checkpoints for comparisons.
These preprocessing checks do not measure generated-speech intelligibility.
