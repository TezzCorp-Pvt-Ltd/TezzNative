# TezzNative Standard Library Inventory

This inventory gives each public module an explicit maturity label. The goal is
to keep the standard library useful without letting experimental APIs weaken
trust in the stable core.

## Labels

| Label | Meaning |
| --- | --- |
| Stable Candidate | Suitable for near-term hardening and compatibility tests. |
| Beta | Useful today, but needs more tests and platform notes. |
| Experimental | API or runtime backend may change. |
| Internal/Tooling | Used by tooling, packaging, or specialized workflows. |

## Stable Candidates

| Module | Purpose | Next Hardening Step |
| --- | --- | --- |
| `std` | Common prelude and helpers | Import smoke is gated; keep imports predictable and consider separating experimental imports later. |
| `io` | Files, paths, streams, basic OS IO | Raw file read/write, File wrapper open/write/read-line/write-line/flush/seek/tell/close, BigFile chunk reads, StreamWriter flush/close behavior, portable `file_size_bytes`, file exists/delete/rename, directory exists/make/remove, failed-open/null guards, EOF behavior, portable path helpers, direct `dir_list`, raw/public recursive listing and glob, and process run/output capture are native-smoke gated on Windows/Linux x64. VM/runtime gates cover sorted recursive listing and raw glob filters. |
| `str` | String helpers | Native search/prefix/suffix plus trim/case/slice/replace/repeat/pad/parse smoke is gated; empty `str_replace_first` behavior is edge-gated. |
| `math` | Numeric helpers | Native integer, float, divmod, aggregate, dot-product, divide-by-zero/null-output, `smoothstep`, and quadrant `atan2` smoke is gated. |
| `time` | Time and sleep helpers | Import smoke plus native clock/sleep/UTC/local-date smoke are gated on Windows/Linux x64; ownership of `date_now` backend output remains backend-defined. |
| `vec` | Dynamic vector utilities | Native integer vector push/get/set/pop/free smoke is gated; reserve/fill null guards and reserve growth are edge-gated. |
| `arena` | Arena allocation helpers | Native allocation/alignment/strdup/mark/release/reset smoke is gated; wrapped buffers, invalid alignment, and future-mark release failures are edge-gated. |

## Beta Modules

| Module | Purpose | Next Hardening Step |
| --- | --- | --- |
| `net` | TCP, UDP, HTTP helpers | URL parsing, DNS endpoint helpers, HTTP parser/routing/auth/cookie utilities, chunked response decoding, keep-alive `Content-Length`/chunked response reads, Windows/Linux x64 TCP loopback send/recv, IPv4 literal socket bind host handling, localhost TCP/UDP connect wrappers, socket timeout/blocking options, manual local HTTP request/response, and route-once server helpers are native-smoke gated; add DNS-backed socket and public-network HTTP tests next. |
| `tls` | TLS runtime wrappers | Certificate trust, DNS/IP identity, minimum versions, pins and timeouts have local-certificate regressions; see `TLS.md` for tested backends and limits. |
| `tezzserve` | HTTP/server helpers | Add route, JSON, static file, and websocket smoke tests. |
| `tezzapi` | REST API framework | Add request/response validation examples. |
| `tezzdb` | Embedded database | Regression coverage includes overflow pages, deep tree splits, corrupt pages, metadata capacity, transactions, rollback, write errors and sealed redo recovery. The format holds 102 tables. Logical transactions do not promise power-loss durability or concurrent-writer isolation. Secondary index column definitions are not persisted in v2; reopen disables those indexes and queries remain correct by scanning. |
| `tezzdbql` | Query layer for TezzDB | Parse the entire statement before mutation; reject malformed input and missing parameters. Regressions cover exact predicates, actual primary keys, JSON escaping, preserving untouched update fields and cursor failures. |
| `actor` | Local actors, mailboxes, supervision, node metadata, and OTP-like app helpers | Local in-process spawn/send/receive, mailbox matching, restart supervision, hot version tags, and fail-closed remote node sends are native-smoke gated; add real scheduler, network distribution, and hot module replacement before promotion. |
| `llm_core` | Dtype-explicit CPU transformer primitives, quantized matmul, KV/decode helpers, and fail-closed backend gates | f32 API shape, f16/bf16 storage lanes, int8/uint8/q4 matmul, tensor/KV descriptors, tokenizer byte fixtures, tiny decoder rules, and benchmark source are gated; add true packed/accelerated backends, mmap model format, real tokenizer compatibility, and published performance data before claiming production LLM support. |
| `mind`, `trainer` | TezzMind tiny on-device transformer and training prototype | Forward/generation/training/save-load, frozen checkpoint compatibility, and trained checkpoint evaluation are native gated on Windows/Linux x64. Add held-out quality metrics, tokenizer integration, and performance reports before promotion. |
| `gui`, `gui_win` | Host GUI APIs | Add Windows-only examples and platform notes. |
| `tzgui`, `tzui`, `tnui`, `tezzui`, `wm` | UI stacks | Retained `tzgui` now has absolute/row/column flex layout, reusable box styles, z-ordering, pointer capture, hover and focus state, keyboard activation, and inherited disabled/hidden state; `tzcss` applies box/flex and hover styles. Add scrolling/clipping, accessibility, and platform input coverage. |
| `mmap`, `sys`, `task`, `event`, `frame` | Systems/runtime helpers | Add platform matrix and failure behavior. |

## Experimental Modules

| Module | Purpose | Required Before Promotion |
| --- | --- | --- |
| `gpu` | GPU runtime hooks | Backend availability matrix and explicit unavailable-backend tests. |
| `npu` | Reserved dedicated NPU runtime interface | No provider is implemented. The typed x64 VM/native ABI reports unavailable and never substitutes CPU or GPU execution. |
| `tensor_bridge` | Explicit owning f64/f32 CPU copies | Preserves shapes and logical view order; rejects incompatible metadata/finite overflow and requires explicit gradient detachment. See `AI_COMPATIBILITY.md`. |
| `tztensor`, `tzautodiff`, `tzcpu`, `tzgpu` | Binary32 tensor storage, tape and kernel interfaces | CPU shape, gradient, conversion and vision-kernel regressions pass. CUDA execution needs separate complete-artifact validation; CPU CNN calls reject GPU handles. |
| `tensor` | Native tensor math, views and reverse-mode differentiation | Numerical and finite-difference regressions cover broadcasting, views, reductions, gradient accumulation, repeated backward and ownership. These checks do not establish every device backend or arbitrary model's correctness. |
| `nn`, `llm`, `tokenizer` | AI/LLM experiments | Byte-token known answers are gated; stable model/tokenizer formats, compatibility tests, and performance notes must build on the gated `llm_core` and TezzMind verification lanes instead of untested backend assumptions. |
| `tts`, `stt` | Native English speech prototypes | Native model training/checkpoint/inference paths use only LJSpeech for this prototype. No OS, remote or substitute-model fallback. STT accuracy is not suitable for general speech and TTS intelligibility is unverified; see `SPEECH_TRAINING.md` for data splits, commands and measured results. |
| `stt_temporal` | TezzNative-defined temporal convolution/CTC model | Model composition, backward pass, optimizer orchestration and checkpoints are `.tn`. CPU and explicit CUDA-encoder/CPU-CTC execution use native kernels; numerical checks do not establish recognition quality. See `SPEECH_TRAINING.md`. |
| `os` | Operating system helpers | Separate hosted and freestanding contracts and target matrix. |
| `hardware` | Shared freestanding UART, time/delay and IRQ-state services | Explicit native C-IR hardware profiles have emulator gates; hosted/default imports are rejected. Physical boards and broad peripheral coverage remain unverified. |
| `arduino.uno` | ATmega328P / UNO R3 profile | simavr checks GPIO, ADC, EEPROM, UART, timers and IRQ state. Integer-only; float storage/operations are rejected by this AVR toolchain. Respect flash/SRAM/stack budgets and Timer0 ownership; see `HARDWARE_UNO.md`. |
| `raspi.pi3` | Pi 3B AArch64 GPIO and UART setup | QEMU checks GPIO register state, UART TX/RX, counters and FP operations. No Pi 4/5, RP1, Pico, hosted Linux device-node API, or broad peripheral promise. |
| `kernel`, `arduino`, `raspi` | Unavailable legacy hardware facades | Their broader proposed interfaces still reject imports. New profile modules do not enable these old facades; see `PLATFORM_SUPPORT.md` for exact targets and gaps. |
| `cyber`, `intrin`, `simd` | Specialized acceleration/security helpers | Capability checks and fallback behavior. |

## Internal Or Tooling Modules

| Module | Purpose |
| --- | --- |
| `tnx` | Package/metadata style helpers. |
| `tezzinstall`, `tezzsetup` | Installer and setup flows. |
| `tnauto`, `tsm` | Automation and service management helpers. |
| `data`, `color`, `tzimage` | Supporting utility modules that need examples before stable promotion. |

## Compatibility and deployment notes

The hardware profiles cross-compile C IR into actual target firmware, then run
an explicitly selected simavr/QEMU test environment. This is a source-checkout
Linux/WSL workflow; no physical-board or all-architecture compatibility claim
is made. The non-AVR profiles test binary32/binary64 plus reduced-format
conversion/arithmetic boundaries through C lowering. All AVR runtime floats
are rejected by its integer-only profile. Profile
imports require the matching target plus `--freestanding`. See
`PLATFORM_SUPPORT.md` for commands, peripheral limits and memory budgets.

Large library implementations live in package subfolders with public facades.
Package-file manifests cover each component's checksum; distribution and both
installers preserve the complete dependency tree. The packaging gate rejects
missing or mismatched mirrors and library source files exceeding 1,000 lines.

The SDK `result` module uses concrete `ResultInt` and `ResultF64` types, with
`ok`/`err` factories and a typed mapping callback. Its former `Result(T)` syntax
was never supported by the parser. Error strings are borrowed; failed
`result_expect` calls terminate with a nonzero status.

The raw `fread` and `fwrite` interfaces return byte counts in the VM and hosted
native runtime. Their names resemble C functions, but C element-count return
values are not this language's contract.

TezzDB password records use salted PBKDF2-HMAC-SHA256, and sessions use random
tokens stored as hashes with a one-hour expiry. Old unsalted password records
and predictable session tokens are rejected. Applications must reset those
passwords through their own verified recovery flow and require a new login;
there is no automatic acceptance of the old credential formats.

The SDK `tezz_http_server` is a loopback static-file development server. It
checks local Host/Origin values, escapes generated HTML, rejects private paths,
and resolves existing symlinks inside the document root. Keep that root under
trusted control; it is not a sandbox for concurrent untrusted filesystem
writers. Its old unauthenticated database and OTP demonstration endpoints now
return explicit errors. Application authorization, OTP enrollment and replay
state must be designed before those endpoints can be enabled.

## Promotion Rules

A module can move toward Stable Candidate only when:

- public functions have signatures and examples.
- ownership and error behavior are documented.
- platform support is explicit.
- smoke tests exist.
- experimental backend dependencies are isolated or clearly optional.

## Immediate Improvements

1. Keep adding deeper edge smoke tests as APIs graduate; the current stable
   candidate slice has native `io`/`str`/`math`/`time`/`vec`/`arena` coverage,
   including chunked stream IO and focused math/string/vector/arena edge gates.
2. Document backend availability and explicit error behavior for `gpu`, `npu`,
   `tls`, and GUI modules.
3. Reduce default prelude risk by separating stable and experimental imports.
4. Keep the curated `examples/dx` programs aligned with stable-candidate module
   behavior and the developer-experience gate.
5. Keep this inventory synchronized with `tezz.mod` and `lib/`.
6. Grow `llm_core` from CPU correctness primitives into real model IO,
   tokenizer compatibility, packed/accelerated dtype backends, and published
   performance gates before any production LLM claim.
7. Grow `actor` from local deterministic actors into scheduler-backed actors,
   remote distribution, and benchmarked service-runtime gates before any
   Erlang/OTP-scale claim.
