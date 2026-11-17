# Raspberry Pi 3B emulator profile

This profile targets the BCM2837 Cortex-A53 in AArch64 mode. It is separate from
the proposed hosted Linux API in `lib/raspi.tn`. Its first verification target is
QEMU `raspi3b`; no physical board result is claimed.

The linker places the image at `0x80000`. The verified QEMU 8.2.2 command uses
the generic loader to load the raw image and set CPU0's PC explicitly:

```
-M raspi3b
-device loader,file=kernel8.img,addr=0x80000,cpu-num=0,force-raw=on
-device loader,addr=0x80000,cpu-num=1
-device loader,addr=0x80000,cpu-num=2
-device loader,addr=0x80000,cpu-num=3
```

Starting the other cores at the same entry also exercises our secondary-core
parking. In this QEMU version, `-bios kernel8.img` alone loads the image but
leaves CPU0's PC at zero; it does not boot this profile. This is a firmware
entry, not an ELF Linux userspace executable or a Linux kernel boot shortcut.

Startup parks secondary cores before clearing BSS, establishes a 64 KiB stack,
transitions from EL3/EL2 to EL1h when necessary, installs exception vectors, and
enables FP/SIMD with round-to-nearest and gradual underflow. Interrupts begin
masked. The MMU and caches remain disabled.
After `tn_main` returns, the runtime emits `TEZZ_HW_EXIT=<status>` on UART0 and
halts. The emulator runner checks that status; a printed marker alone does not
make a failing program pass.

## Implemented interface

- UART0 uses the PL011 at `0x3f201000`, GPIO14/15 ALT0, 8N1, and a 48 MHz
  reference-clock assumption. Initialization selects 115200 baud. QEMU serial
  tests verify bytes, not physical line voltage or baud timing.
- UART writes return a byte count or -1, accept at most 1 MiB per call, and have
  a one-second transmit wait bound. Reads are nonblocking and return the number
  of bytes available, zero when empty, or -1 on invalid input/device errors.
- `micros` and `delay_us` use the architectural physical counter and its actual
  `CNTFRQ_EL0` frequency. Negative delays are rejected.
- GPIO uses BCM numbers 0 through 27, with input/output modes and set/clear/read.
  GPIO14/15 are reserved after UART initialization. Writing an input pin, an
  invalid pin, or a value other than zero/one fails.
- IRQ functions read and modify the actual DAIF.I mask and return its previous
  enabled state. Interrupt-controller setup, callbacks, and delivery are not
  implemented.

QEMU can verify GPIO register behavior and its modeled output levels. That
does not establish electrical behavior on a board. Pull configuration, edge
interrupts, PWM, SPI, I2C, camera, networking, storage, MMU, and multicore
scheduling remain outside this profile.

Raspberry Pi 4/5, RP1, Pico, ARM32 models, and Linux device-node bindings require
their own profiles. They must not use these hard-coded Pi 3 peripheral addresses.

## Source workflow

From the repository root, using an AArch64 cross compiler and QEMU:

```bash
python3 tools/tezz_hw.py build tests/hardware/pi3_gpio.tn \
  --target raspi3b --compiler /path/to/source-built/tezzc \
  --out build/hardware/pi3-gpio
python3 tools/tezz_hw.py run --target raspi3b --out build/hardware/pi3-gpio
python3 tests/hardware/run.py --compiler /path/to/source-built/tezzc \
  --out build/verification/hardware
```

The central gate includes GPIO/timer/IRQ-mask assertions, UART reception with
injected `ping` data, and floating-point fixtures. It performs the UART input
handshake automatically; a plain CLI run of the input-dependent UART fixture
does not supply that data. This workflow explicitly cross-compiles native
AArch64 code through C IR and uses the emulator as its test environment.

## Primary references

- [QEMU Raspberry Pi boards](https://www.qemu.org/docs/master/system/arm/raspi.html)
- [QEMU 8.2.2 Pi firmware loading](https://github.com/qemu/qemu/blob/v8.2.2/hw/arm/raspi.c)
- [QEMU generic loader and CPU startup PCs](https://www.qemu.org/docs/master/system/generic-loader.html)
- [Raspberry Pi peripheral register manual](https://datasheets.raspberrypi.com/bcm2835/bcm2835-peripherals.pdf)
- [Arm exception-level transitions](https://developer.arm.com/community/arm-community-blogs/b/tools-software-ides-blog/posts/changing-exception-level-and-security-state-with-an-armv8a-fixed-virtual-platform)
- [Raspberry Pi UART configuration](https://www.raspberrypi.com/documentation/computers/configuration.html#configure-uarts)
