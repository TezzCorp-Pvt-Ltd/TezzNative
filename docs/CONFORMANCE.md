# TezzNative Conformance

The conformance corpus is the first compatibility contract for the stable
language core. It is intentionally small, organized, and deterministic so
compiler regressions can be diagnosed quickly.

## Source-built verification

The unified runner builds the checked-out compiler and its native runtime, then
runs the new regression suite and the existing compatibility fixtures:

```powershell
python tests/run.py --build --suite all --out build/verification
```

Use `--cc C:/msys64/mingw64/bin/gcc.exe` when GCC is not on the Windows PATH.
Linux needs a C11 compiler and OpenSSL development headers. Each regression runs
through type checking and both bytecode and native execution. `--filter` selects
one path; `--backend bc` or `--backend native` selects one execution mode.
Type-check-only and diagnostic fixtures retain their explicit contracts.

The runner rejects timeouts, crashes, failed native builds, missing artifacts,
unexpected diagnostics, incorrect stdout, and assertion-failure text even when
a program exits zero. Results are written incrementally to `results.json` with
the compiler SHA-256 and command output. An empty selection is an error.

On Linux, run the instrumented VM/compiler gate with:

```sh
python3 tests/run.py --build --sanitize --backend bc --out build/verification/sanitizers
```

ASan and UBSan diagnostics fail this gate even if a process returns zero.
Leak detection is disabled because the compiler retains AST allocations until
process exit; passing this gate is not a claim of leak freedom. Native programs
are exercised separately and are not instrumented by this VM-only gate.

CI also runs independent numerical oracles for float formats, CPU kernels,
emitted C and CUDA-host code, training arithmetic, cryptography,
database recovery logs, local HTTP behavior, recursive package installation,
and every distributed module import. CUDA-host tests do not establish CUDA
device correctness. Platform-only cases report skips explicitly.

Native speech checks include STT and TTS learning, checkpoint round trips,
duration bounds, exhaustive CTC/language-prior comparisons, vocoder signal
reconstruction, test-set isolation and argument parsing. These deterministic
checks do not establish usable speech quality.

Temporal convolution checks independently verify input/weight/bias gradients,
stride, dilation, centered padding and accumulation on CPU and physical CUDA.
Both 11- and 13-argument interfaces execute through bytecode and native programs.
The TezzNative temporal speech model additionally checks composed gradients,
learning, state and CPU/CUDA agreement. Full-data experiment contracts verify
representative selection, held-out isolation, audio identity and resume rules.
TTS text checks exercise the shared native normalizer and transactional manifest
CLI. Formatter regressions preserve every else-if branch and prove that
`fmt --check` and rejected arguments never modify input files.

`tests/regression/cuda/check_device.py` builds the complete CUDA source and
executes independent real-device numerical/gradient/error checks through host
or native Tezz ABI (`--native-abi`). Missing toolkit/device fails. The 46-group
GTX 1650 run covers all 64 exports and a 200-step GPU linear fit. Separate
language fixtures execute GPU operations, MLP updates and error propagation
through bytecode and generated native programs. Compute Sanitizer and an
isolated deliberate device trap verify memory and asynchronous failure paths.
The manual `cuda-device.yml` workflow requires a provisioned NVIDIA runner;
hosted CPU CI does not replace this gate. Windows CUDA and other GPUs are not
verified by the Linux/WSL result.

Dedicated NPU CI compiles against pinned official C APIs, injects provider
failures and checks actual no-device behavior. Reports distinguish test doubles
from device execution. Intel/AMD/Qualcomm `--require-device` gates fail when
hardware is absent. Hailo header-contract checks run on Linux and emulated
ARM64; they cannot certify physical execution. See [NPU contracts](NPU_PROVIDERS.md).
The hosted CPU job separately source-builds and runs ARM64/RISC-V64 under
QEMU user emulation. Linux native termination also has a worker-thread fixture
that rejects the former main-thread-only exit behavior without requiring a GPU.

Existing compatibility fixtures remain under `tests/conformance` so that the
replacement runner preserves their coverage. Developer tooling, ABI, package
trust, Python bridge, reproducibility, and benchmark-check gates remain active.
Do not delete a legacy fixture without migrating its distinct contract.

## Public Suites

| Suite | Path | Purpose |
| --- | --- | --- |
| Stable core | `tests/conformance/valid` | Arithmetic, control flow, structs, arrays, indexing, fixed-width integers, `sizeof`, `alignof`, and unsafe pointer basics. |
| Parser valid | `tests/conformance/parser/valid` | Comments, literals, nested blocks, struct syntax, and fixed-array syntax. |
| Parser invalid | `tests/conformance/parser/invalid` | Bad indentation, missing block markers, and unterminated strings. |
| Typecheck valid | `tests/conformance/typecheck/valid` | Function calls/returns, pointer-array roundtrip, and struct value flow. |
| Typecheck invalid | `tests/conformance/typecheck/invalid` | Array index type errors, return value mismatch, and struct field assignment mismatch. |
| Diagnostics | `tests/conformance/invalid` plus `tests/conformance/diagnostics` | Named unknown symbols, wrong arity, unsafe address-of, field errors, and stable diagnostic snippets. |
| Stdlib import | `tests/conformance/stdlib` | Stable-candidate module import smoke plus focused `llm_core` f64 and int8-quantized primitive checking. |
| Developer experience | `tests/conformance/run-dx.*` plus `tests/conformance/dx` | Actionable diagnostics, formatter idempotence, lint rule IDs, examples, LSP source health, and VS Code snippet drift checks. |
| Package trust | `tests/conformance/run-package-trust.*` | Public `tezz` launchers/tool source, SemVer package metadata, lock/registry parity, package checksums, generated package docs, and first-party package target docs. |
| Python bridge | `tests/conformance/run-python-bridge.*` plus `tests/conformance/python_bridge` | `tezzc pyext` command generation, CPython wrapper contents, primitive/buffer mapping, ownership docs, and wrapped/skipped manifests. |
| C ABI | `tests/conformance/run-abi.*` plus `tests/conformance/abi` | C header emission, structured ABI dump/verify, struct layout, fixed-width integer mappings, and extern signatures. |
| Native smoke | `tests/conformance/run-native-smoke.*` | Native build, verify, run, and stdout comparison on Windows/Linux x64. |
| Native reliability | `tests/conformance/run-native-reliability.*` | Byte-reproducible native output and fail-closed unsupported-target behavior. |

The current stable-core runner gates 29 checks across Windows and Linux:

- 16 valid programs must pass `tezzc check`.
- 13 invalid programs must fail with deterministic diagnostic snippets.

## Commands

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\conformance\run.ps1
```

Linux or WSL:

```bash
bash tests/conformance/run.sh ./TezzNative-language/bin/tezzc-linux-x64
```

Both runners print suite-qualified result names and end with:

```text
CONFORMANCE_SUMMARY passed=29 failed=0
```

Run the developer experience gate:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\conformance\run-dx.ps1
```

On Linux or WSL:

```bash
bash tests/conformance/run-dx.sh ./TezzNative-language/bin/tezzc-linux-x64
```

The DX runner ends with:

```text
DX_SUMMARY passed=19 failed=0
```

Run the package trust gate:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\conformance\run-package-trust.ps1 -Tezzc .\TezzNative-language\build\tezzc.exe
```

On Linux or WSL:

```bash
bash tests/conformance/run-package-trust.sh ./TezzNative-language/bin/tezzc-linux-x64
```

The package trust runner ends with:

```text
PACKAGE_TRUST_SUMMARY passed=12 failed=0
```

Run the Python bridge gate:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\conformance\run-python-bridge.ps1 -Tezzc .\TezzNative-language\build\tezzc.exe
```

On Linux or WSL:

```bash
bash tests/conformance/run-python-bridge.sh ./TezzNative-language/bin/tezzc-linux-x64
```

The Python bridge runner ends with:

```text
PYBRIDGE_SUMMARY passed=8 failed=0
```

## Rules For New Tests

- A valid fixture must be small and deterministic.
- An invalid fixture must have one primary failure reason.
- If a diagnostic snippet exists, it must be stable across Windows/Linux paths.
- Runtime, native backend, ABI, benchmark, and stdlib expansion tests should
  stay in their dedicated suites instead of weakening the stable-core signal.
- LLM fixtures must state dtype and backend expectations explicitly; do not use
  a tiny primitive check to imply full model training or serving readiness.
- DX fixtures should prove first-user workflows and editor/tooling behavior
  without depending on public network access or machine-specific paths.
- Package-trust fixtures should validate metadata, deterministic ordering, and
  checksum/provenance behavior without requiring public network access.

## Native Backend Gates

Native smoke cases live under `tests/conformance/native`. They build native
executables with `buildexe`, verify the output format where supported, run the
executable, and compare stdout when a `.stdout.txt` file exists.

Current native smoke coverage includes hello output, loop/math lowering, string
helpers and transforms, file IO, directory lifecycle/listing/glob, portable
paths, fixed-width integer truncation and signed/unsigned load extension,
vectors, arenas, time helpers, process output, many-argument calls, local TCP
loopback, socket options, IPv4 literal bind hosts, localhost wrappers, local
HTTP route helpers, keep-alive HTTP response reads, and stable-candidate stdlib
edge fixtures for math, strings, vectors, and arenas.

Native reliability cases build focused host-safe fixtures twice and compare the
resulting executable SHA-256 hashes. They also assert that an unknown
`buildexe --target` exits non-zero and does not emit an artifact. This protects
the Milestone 2 release gate for deterministic output and fail-closed target
handling.

## Hardware Emulator Gate

`tests/hardware/run.py` builds firmware from the checked-out compiler and runs
the explicit Uno, Pi 3B, PC x86-64 and RISC-V64 profiles. Run it on Linux/WSL:

```bash
python3 tests/hardware/run.py --compiler /path/to/source-built/tezzc \
  --out build/verification/hardware
```

The gate includes simavr peripheral observations, QEMU UART input/output,
timer and floating-point checks, deliberate firmware failures, compiler
rejections, formatter metadata preservation, target-aware linting, and
build/emulator failure handling. Missing tools, timeouts,
unexpected statuses and changed artifact digests fail the gate. Driver protocol
fixtures are reported separately from actual emulated firmware execution.
The `hardware-emulators` CI job installs the required tools, builds the compiler,
runs this gate and retains its reports and firmware artifacts on failure.

See [platform support](PLATFORM_SUPPORT.md#freestanding-hardware-profiles) for
the exact supported peripherals and restrictions. Emulator success does not
establish physical-board support.
