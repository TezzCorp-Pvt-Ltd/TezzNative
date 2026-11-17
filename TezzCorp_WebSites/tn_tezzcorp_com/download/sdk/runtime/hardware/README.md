# Explicit firmware profiles

These runtimes are for the source-checkout hardware workflow on Linux/WSL.
They execute target machine code in an explicitly selected emulator. They do
not provide a hosted fallback for missing hardware, and no physical board has
been tested or flashed.

| Profile | Runtime and public module | Verification target |
| --- | --- | --- |
| `arduino-uno` | `runtime/boards/arduino_uno.c`, `arduino.uno` | simavr ATmega328P, 16 MHz; integer-only |
| `raspi3b` | [Pi 3B runtime](raspi3b/README.md), `raspi.pi3` | QEMU Pi 3B, Cortex-A53 AArch64 |
| `pc-x86_64` | [PC runtime](x86_64/README.md), `hardware` | QEMU PC BIOS/Multiboot2, one x86-64 CPU |
| `riscv64-virt` | [RISC-V runtime](riscv64/README.md), `hardware` | QEMU virt, one RV64GC hart in M-mode |

The compiler's profile target emits C IR. The builder invokes the named
cross compiler and linker, produces ELF plus the profile's HEX/raw/ISO image,
and records the build commands and artifacts. This is native cross
compilation through C IR, rather than the hosted BC VM or an implicit switch
between compiler backends.

The shared `hardware` module exposes target identity, UART, microsecond time
and delay, and IRQ-state services. Its boundary uses int64 argument/result
slots; address slots are converted through `uintptr_t` by each runtime.
Generated Tezz memory retains eight-byte pointer storage, including on AVR.
This does not define a general ABI for arbitrary C structs or C libraries.

All imports need the matching explicit profile and `--freestanding`. The old
top-level `arduino`, `raspi` and `kernel` facades remain unavailable.
Only the listed services are supplied: these minimal runtimes have no hosted
filesystem/network/GUI or general heap library.

## Build and verify

Run from the repository root with the selected cross toolchain and emulator
installed. The tools currently live in the source checkout, not an installed
SDK executable release. The PC profile also needs GRUB and xorriso.

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

The central gate supplies UART input and checks expected failure cases as well
as successful firmware returns. Uno tests use the separate simavr observer;
the QEMU runner reads `TEZZ_HW_EXIT=<status>` only after the Tezz program
returns and checks the expected status. Missing tools, profile mismatches and
unsupported floating types fail explicitly.

## Boundaries

- Uno is an ATmega328P/UNO R3 profile, not all Arduino boards. Tezz runtime
  floats are rejected because this AVR toolchain uses 32-bit `double`.
  Application flash is limited to 32,256 bytes and static SRAM to 1,536 bytes,
  reserving 512 bytes for stack. Tested stack observations are not a guarantee
  for arbitrary applications. See [UNO details](../../docs/HARDWARE_UNO.md).
- Pi is a Pi 3B profile, not Pi 4/5, RP1, Pico, or ARM32. UART and GPIO register
  tests do not establish physical baud timing or electrical behavior.
- PC has actual PIC/PIT IRQ0 delivery. Pi and RISC-V currently expose interrupt
  mask state without interrupt-controller delivery or callbacks.
- Uno Timer0 and PC PIT clocks can lose elapsed periods during prolonged IRQ
  masking. Pi and RISC-V delays poll their hardware counters.
- Binary32 and binary64 are tested on the non-AVR profiles. Unsupported
  reduced-precision floating types are rejected by C lowering.
- Only the peripheral subset listed in each profile README is implemented.
  Device drivers, resource ownership, memory protection and scheduler work
  remain necessary before wider hardware claims.

See the [platform matrix](../../docs/PLATFORM_SUPPORT.md) for the supported
source workflow and current peripheral limits.

