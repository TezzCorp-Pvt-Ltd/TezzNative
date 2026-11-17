# TezzNative Platform Support

This matrix separates verified support from planned or experimental targets.
It should be updated whenever compiler, runtime, installer, or stdlib behavior
changes.

## Support Labels

| Label | Meaning |
| --- | --- |
| Primary | First-class target for current hardening and smoke tests. |
| Preview | Usable for testing, but not yet a release gate. |
| Experimental | Backend or runtime work exists, but behavior may be incomplete. |
| Planned | Design target with no public compatibility promise yet. |
| Unavailable | Required backend is absent; use produces an explicit diagnostic. |

## Compiler And Runtime

| Target | Status | Notes |
| --- | --- | --- |
| Windows x64 | Primary | Main packaged SDK and installer target. Native executable flow is the first production hardening target. |
| Linux x64 | Primary/Beta | Static SDK compiler archive and direct compiler download are published to avoid hosted-runner glibc drift. Stable-core, ABI header/dump/verify checks, benchmark fixture checks, native executable smoke, and native reliability checks pass under WSL and GitHub Actions. |
| macOS x64 | Planned | Listed as a target in metadata, but needs packaged toolchain validation. |
| Linux ARM64 | Experimental, CPU emulation gated | Source-built compiler, bytecode/CPU kernels and explicit generated-C fixtures pass under qemu-user; no physical device or packaged-release certification. |
| Linux RISC-V64 | Experimental, CPU emulation gated | RV64 Linux cross-build and the same focused hosted CPU checks pass under qemu-user; no physical device or packaged-release certification. |
| macOS ARM64 | Planned | Listed as a target in metadata, but not a release gate yet. |
| Windows x86 | Experimental | Backend status exists, but not part of the main release promise. |
| Windows ARM64 | Experimental | Verify-only or limited paths should be documented before public promotion. |

## Module Families

| Area | Windows x64 | Linux x64 | macOS | Notes |
| --- | --- | --- | --- | --- |
| Core language | Primary | Primary | Planned | Stable-core conformance runs on Windows and Linux SDKs. |
| Bytecode run | Primary | Preview | Planned | Compatibility path while native backend matures. |
| Native executable | Primary/Beta | Primary/Beta | Planned | Hello, loop/math, fixed-width integer truncation and signed/unsigned load extension, many-argument calls, math-module, stdlib math/collections edges, string, string-transform, struct-array, raw/wrapped/line/stream file IO, directory lifecycle, direct `dir_list`, raw/public recursive listing and glob, portable path, vector, arena, process run/output, time, deterministic net/HTTP parser, Windows/Linux TCP loopback, IPv4 literal socket bind hosts, localhost socket wrappers, keep-alive HTTP response reads, and local HTTP route smoke tests pass on the claimed x64 targets. Focused native fixtures are also byte-reproducibility gated on Windows/Linux x64, and unknown targets are fail-closed. |
| Python bridge scaffold | Beta | Beta | Planned | `tezzc pyext` emits CPython wrapper C, ABI declarations, setup metadata, ownership docs, and wrapped/skipped manifests for primitive returns and borrowed buffer parameters. The gate validates generated source on Windows/Linux; full Python packaging/runtime ownership compatibility remains a future promotion item. |
| Benchmark harness | Primary | Primary | Planned | Startup, numeric loop, string scan, and binary file IO fixtures are check-gated on Windows/Linux; optional Python, C, Node.js, Go, and Rust comparison fixtures run when local toolchains are available. |
| Time/date runtime | Beta | Beta | Planned | `time` imports and native clock/sleep/UTC/local-date execution are gated on Windows/Linux x64. |
| IO/path/process | Beta | Beta | Planned | Raw file read/write, File wrapper open/write/read-line/write-line/flush/seek/tell/close, BigFile chunk reads, StreamWriter flush/close behavior, portable `file_size_bytes`, file delete/rename, directory exists/make/remove, EOF/null guards, portable path helpers, direct `dir_list`, raw/public recursive listing and glob, and `proc_run`/`proc_out` have native smoke coverage on Windows/Linux x64. |
| Networking | Beta | Beta | Planned | URL parsing, DNS endpoint helpers, HTTP parser/routing/auth/cookie utilities, chunked response decoding, keep-alive `Content-Length`/chunked response reads, Windows/Linux x64 TCP loopback send/recv, IPv4 literal socket bind hosts, localhost TCP/UDP connect wrappers, socket timeout/blocking options, and local HTTP client/server route helpers are smoke gated. DNS-backed sockets and public-network HTTP tests are still needed. |
| Actor runtime | Beta foundation | Beta foundation | Planned | Local in-process actor systems, mailbox matching, supervisor restarts, node metadata with fail-closed remote sends, hot version tags, and OTP-style app helpers are native-smoke gated. Real scheduler-backed actors, node-to-node transport, and versioned module replacement are planned runtime gates. |
| TLS | Beta | Preview | Planned | Linux builds without OpenSSL development headers expose unsupported TLS stubs until linked with a TLS backend. |
| GUI | Beta | Experimental | Planned | Windows host modules are the clearest path today. |
| TezzDB | Beta | Preview | Planned | Needs database consistency and WAL tests. |
| CUDA GPU | Experimental | Experimental, device tested | Planned | Complete source-built kernels and Tezz BC/native integration tested on GTX 1650 through Linux/WSL. Windows CUDA build and other GPU families remain unverified. |
| Dedicated NPU | Experimental adapters | Experimental adapters | Unavailable | Opt-in Intel, AMD, Qualcomm and Hailo model adapters; header/contract checks do not establish device execution. See NPU_PROVIDERS.md. |
| OS/freestanding helpers | Experimental | Experimental | Experimental | Existing `os`/`sys` APIs use their separate freestanding build flow. |
| `kernel` API module | Unavailable | Unavailable | Unavailable | The broader proposed kernel facade remains unavailable. Explicit PC and RISC-V profiles use `hardware` with separate emulator gates below. |
| `arduino` API module | Unavailable | Unavailable | Unavailable | The broader proposed Arduino facade remains unavailable. Use `arduino.uno` with the explicit `arduino-uno` profile below. |
| `raspi` API module | Unavailable | Unavailable | Unavailable | The proposed hosted Linux peripheral facade remains unavailable. Use `raspi.pi3` with the separate `raspi3b` bare-metal profile; no Pi 4/5 or Pico support is implied. |

The distributed `arduino`, `kernel`, and `raspi` facades retain their broader
proposed interfaces and reject imports explicitly. Their unavailable markers
apply to those distributed source files, not to an application module with the
same name. Profile-specific modules below have separate target guards.

## Freestanding Hardware Profiles

These are experimental, emulator-verified source workflows on Linux/WSL.
The compiler emits C IR for an explicitly selected hardware profile; the
profile's cross compiler produces native AVR, AArch64, x86-64, or RISC-V machine
code and links its freestanding runtime. The test command then runs that
firmware in simavr or QEMU. An emulator is an explicit test environment, never
a fallback selected after a hardware operation fails.

No physical boards have been tested or flashed. This work does not promote
hosted Linux ARM64 support or make every board in an architecture family
compatible.

| Profile | Native target / image | Emulator and covered services | Current limits |
| --- | --- | --- | --- |
| `arduino-uno` | ATmega328P, 16 MHz / ELF + Intel HEX | simavr: GPIO direction/latch/input, external digital input, ADC voltages, EEPROM, UART TX/RX, Timer0 and global IRQ state | UNO R3 only; integer-only Tezz runtime. AVR `double` is 32-bit in this toolchain, so Tezz floating-point storage/operations are rejected. No PWM, SPI, I2C, servo, tone or callback IRQ API. |
| `raspi3b` | BCM2837 Cortex-A53 AArch64 / ELF + `kernel8.img` | QEMU `raspi3b`: PL011 UART TX/RX, BCM GPIO register/level state, architectural counter, FP/SIMD and IRQ mask | Pi 3B only; no Pi 4/5, RP1, Pico or ARM32 profile. No GPIO pulls/edge IRQs, PWM, SPI, I2C, camera, storage, network, MMU or multicore scheduling. |
| `pc-x86_64` | BIOS/Multiboot2 x86-64 / ELF + GRUB ISO | QEMU PC: COM1 UART, actual PIC/PIT IRQ0 delivery, monotonic clock and IRQ state | One CPU, initial first-GiB identity mapping; no APIC, USB, storage, network, display or physical GPIO driver. |
| `riscv64-virt` | RV64GC/LP64D, machine mode / ELF + raw image | QEMU `virt`: NS16550 UART, CLINT `mtime`, FP state, MIE mask and test finisher | Fixed virtual platform and timebase; no physical board, GPIO, timer IRQ delivery, PLIC routing, callbacks, SMP, MMU, storage or network. |

The shared `hardware` module provides target identity, counted/nonblocking UART
IO, microsecond time/delay and IRQ state. The PC and RISC-V profiles use this
common module; `arduino.uno` and `raspi.pi3` add board-specific APIs. Imports require both the
matching `--target` and `--freestanding`; legacy facades remain unavailable.
The non-AVR profiles test binary32 and binary64 operations, plus 310 boundary
checks per profile for f16, bf16, f8e4m3 and f8e5m2 conversions/arithmetic. The
normal compiler pipeline lowers reduced formats to integer storage and rounding
helpers before C emission. This does not establish native hardware instructions
for every format; AVR still rejects all floating-point code.

C IR uses signed 64-bit value slots and eight-byte Tezz pointer storage.
The profile adapters convert addresses at the native hardware boundary; this
does not establish a general struct ABI with arbitrary C libraries. Hosted
files, sockets, GUI and heap-backed libraries are not supplied by these runtimes.

The UNO builder limits application flash to 32,256 bytes and static SRAM to
1,536 bytes, reserving 512 bytes for the stack. Emulator stack observations
cover the tested programs, not arbitrary call depth or later applications.
Timer0 is reserved, has 4-us clock resolution, and loses elapsed overflows if
IRQs stay masked beyond its 1,024-us overflow period. PC clock accounting also
loses PIT periods across prolonged IRQ masking. Pi and RISC-V delays poll
hardware counters and work with IRQs masked; their IRQ APIs currently change
mask state only. See [UNO details](HARDWARE_UNO.md).

Run the source workflow from the repository root with a C11 host compiler,
the profile cross compilers, binutils, and the relevant emulator installed.
The PC ISO additionally needs GRUB's `grub-mkrescue`/`grub-file` and
`xorriso`. These tools are not supplied as an installed-SDK executable release.

```bash
python3 tests/run.py --build --suite core --backend bc \
  --out build/verification/hardware-compiler
python3 tools/tezz_hw.py list
python3 tools/tezz_hw.py build tests/hardware/pi3_gpio.tn \
  --target raspi3b \
  --compiler build/verification/hardware-compiler/compiler/tezzc \
  --out build/hardware/pi3-gpio
python3 tools/tezz_hw.py run --target raspi3b --out build/hardware/pi3-gpio
python3 tests/hardware/run.py \
  --compiler build/verification/hardware-compiler/compiler/tezzc \
  --out build/verification/hardware
```

The consolidated Linux/WSL gate reports individual checks and report groups
in `results.json`: QEMU runs, Uno peripheral/core checks, driver failures,
compiler rejections, formatter/linter checks and hosted C-IR execution.
Its report records `physical_hardware: false`.

The central gate builds actual firmware, checks positive and deliberate failing
guest statuses, injects UART data, and checks wrong-profile/unsupported-feature
diagnostics. UNO peripheral observations use the dedicated simavr harness;
`tools/tezz_hw.py run` applies to QEMU profiles. Build and execution reports
retain commands, artifacts and test results. Missing tools or mismatched
profiles are failures, not substitute execution paths.

## Current CPU and accelerator verification scope

The current milestone targets source-built Windows/Linux x64 bytecode and
native CPU execution plus the four emulator profiles above. Tensor f64/f32
conversion is explicit and owning; it does not join the two autograd tapes.
See [tensor compatibility](AI_COMPATIBILITY.md).

The current source-built broad gate reports 431 passed / 0 failed / 7 platform
skips on Windows x64 and 433 passed / 0 failed / 5 platform skips on Linux x64.
Additional focused native CPU oracles pass 17 groups per OS; the final SDK AI
regression passes 40 bytecode/native cases per OS. Package checks cover 197
language sources, with 118 supported imports and three expected legacy-module
rejections. These counts describe the exercised cases, not universal correctness.

The refreshed local SDK compiler/runtime binaries use the default CMake Release
build. Its compiler target enables `-march=native`, so local acceptance does not
certify the compiler binary for older x64 instruction sets. The verified Linux
CUDA artifact also uses this workspace's pinned CUDA runtime location. Release
archives have not been republished with these local artifacts; portable binary
packaging and additional machines require separate verification.

Generated C and CUDA-style host code are checked against independent reduced
float bit oracles. Generated CUDA kernels also undergo host execution with
CUDA qualifiers replaced by test shims. These checks prove emitted host
semantics, not GPU synchronization, memory behavior or GPU instruction support.
The complete CUDA source now builds with CUDA 12.6 and passes 46 numerical,
gradient, error and training groups through each of its host and native Tezz
ABIs on a physical GTX 1650 (SM 7.5) through Linux/WSL. All 64 exports are
exercised; Compute Sanitizer memcheck reports zero errors, including leak
checks. Tezz bytecode/native GPU inference, MLP updates and error propagation
also pass. This replaces the earlier extracted-cuBLAS-wrapper evidence.
Native tiled GEMM and device RNG are used; optimized cuBLAS performance parity
is not claimed. The Windows CUDA build still lacks its MSVC host compiler and
remains unverified. AMD/Intel GPUs, other NVIDIA architectures and NPU hardware
are not covered by this GPU result.

Intel, AMD and Qualcomm NPU adapters compile against official C APIs and pass
Windows/Linux real-runtime absence checks without fallback. Hailo has Linux
and emulated ARM64 official-header contract checks plus a source-built real
HailoRT 4.20.0 linkage/absence check on Linux x64. None has a physical NPU
execution result. See [dedicated NPU providers](NPU_PROVIDERS.md).

Legacy STT/TTS remain native CPU prototypes. Legacy STT validation on the same 128 LJSpeech
clips improved from 57.75%/96.44% to 51.72%/86.29% CER/WER. The frozen model
scored 51.61% CER and 86.32% WER on 619 untouched test clips; recognition is
still poor. TTS mel prediction and reference-mel vocoder reconstruction have
improved, but full text-to-speech intelligibility/naturalness remain
unestablished. See [speech measurements](SPEECH_TRAINING.md). Passing compiler,
tensor or signal-reconstruction tests does not certify speech usability or
universal device compatibility.

The new `stt_temporal` model is defined in `.tn` and supports CPU execution or
explicit CUDA encoder/backward/Adam with native CPU CTC and decoding. Temporal
convolution gradients, complete-model gradients/updates, cross-device checkpoints
and resumed training pass on the GTX 1650. Final model memcheck reports zero
errors and leaks. Small-set learning and backend parity do not establish
held-out recognition quality. Windows CUDA execution remains unverified.

## Hosted ARM64 and RISC-V64 CPU verification

The explicit Linux cross-build lane passed **88 checks, zero failures**: 44 on
ARM64 and 44 on RISC-V64. For each CPU it builds the complete compiler, native
runtime and CPU-kernel library from source and checks their ELF64 machine IDs.
It runs 35 bytecode fixtures covering float conversions and independent bit
oracles, math, NN losses/derivatives, tensor views and finite-difference autograd,
owning f64/f32 tensor conversion and CPU training, speech/frontend/WAV/model
training, and selected Linux file/time/string operations. Three generated-C
fixtures verify aggregates, indirect calls and UTF-8 under the selected CPU.
Five native C speech checks cover STT/TTS training/checkpoints, duration learning
and bounds, and vocoder input/output bounds.

This is Linux userspace CPU instruction emulation through `qemu-aarch64` and
`qemu-riscv64` with matching cross-libc sysroots. It does not verify physical
hardware, a full target kernel, GPU/NPU execution, GUI, TLS, installers, or the
packaged `buildexe` flow. The generated-C cases explicitly invoke the selected
cross GCC. The lane deliberately omits OpenSSL because no cross OpenSSL SDK is
provided; it makes no TLS compatibility claim.

On Debian/Ubuntu, install `gcc-aarch64-linux-gnu`, `gcc-riscv64-linux-gnu`,
`libc6-dev-arm64-cross`, `libc6-dev-riscv64-cross` and `qemu-user`, then run:

```bash
python3 tests/regression/platform/check_hosted.py
```

Missing tools, build failures, wrong ELF machine IDs, guest nonzero status,
timeouts and printed assertion failures fail this gate. Source and artifact
hashes, exact commands and output are retained in its `results.json`. The
measured run is `build/verification/hosted-cpu-final/results.json`. These focused
checks add CPU compatibility evidence; the primary release targets remain
Windows/Linux x64.

## Release Gates

A target should not move to Primary until:

- `tezzc check` passes the stable conformance corpus.
- bytecode run smoke tests pass.
- native smoke and native reliability tests pass where native support is claimed.
- stdlib stable module smoke tests pass.
- install and uninstall flows are tested.
- package checksums and version metadata are published.
- release manifests verify every public SDK archive before install scripts
  extract them.

## Immediate Improvements

1. Keep Windows/Linux x64 core gates and the Linux firmware-emulator gate passing.
2. Add DNS-backed socket and public-network HTTP smoke tests before promoting
   networking beyond Beta.
3. Publish exact binary names, hashes, and sizes for every download.
4. Add target-specific notes to docs and download pages.
5. Add macOS and ARM64 hosted gates before promoting those targets.
6. Add scheduler, remote actor transport, and service-scale benchmarks before
   promoting actor runtime claims beyond Beta foundation.
