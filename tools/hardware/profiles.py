"""Explicit firmware targets; each profile names its actual toolchain and runtime."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
RUNTIME = SDK / "runtime/hardware"

PROFILES = {
    "arduino-uno": {
        "id": 1, "cc": "avr-gcc", "objcopy": "avr-objcopy", "size": "avr-size",
        "flags": ["-mmcu=atmega328p", "-DF_CPU=16000000UL"],
        "sources": [SDK / "runtime/boards/arduino_uno.c"],
        "link": ["-mmcu=atmega328p"], "format": "ihex", "image": "firmware.hex",
        "description": "Uno ATmega328P, 16 MHz; integer-only runtime; simavr verification",
    },
    "raspi3b": {
        "id": 2, "cc": "aarch64-linux-gnu-gcc", "objcopy": "aarch64-linux-gnu-objcopy",
        "size": "aarch64-linux-gnu-size",
        "flags": ["-mcpu=cortex-a53", "-mno-outline-atomics"],
        "sources": [RUNTIME / "raspi3b/entry.S", RUNTIME / "raspi3b/runtime.c", RUNTIME / "common/minic.c"],
        "linker": RUNTIME / "raspi3b/boot.ld", "link": [],
        "format": "binary", "image": "kernel8.img",
        "emulator": "qemu-system-aarch64",
        "qemu": ["-M", "raspi3b",
                 "-device", "loader,file={image},addr=0x80000,cpu-num=0,force-raw=on",
                 "-device", "loader,addr=0x80000,cpu-num=1",
                 "-device", "loader,addr=0x80000,cpu-num=2",
                 "-device", "loader,addr=0x80000,cpu-num=3"],
        "description": "Raspberry Pi 3B AArch64 bare metal; QEMU board model",
    },
    "pc-x86_64": {
        "id": 3, "cc": "gcc", "objcopy": "objcopy", "size": "size",
        "flags": ["-m64", "-mno-red-zone", "-mcmodel=kernel"],
        "sources": [RUNTIME / "x86_64/boot.S", RUNTIME / "x86_64/interrupts.S",
                    RUNTIME / "x86_64/hal.c", RUNTIME / "common/minic.c"],
        "linker": RUNTIME / "x86_64/kernel.ld", "link": ["-m64", "-mno-red-zone"],
        "format": "iso", "image": "firmware.iso",
        "emulator": "qemu-system-x86_64",
        "qemu": ["-machine", "pc", "-m", "128M", "-smp", "1", "-cdrom", "{image}",
                 "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"],
        "description": "x86-64 PC BIOS, Multiboot2/GRUB, single CPU; QEMU",
    },
    "riscv64-virt": {
        "id": 4, "cc": "riscv64-linux-gnu-gcc", "objcopy": "riscv64-linux-gnu-objcopy",
        "size": "riscv64-linux-gnu-size",
        "flags": ["-march=rv64gc", "-mabi=lp64d", "-mcmodel=medany", "-msmall-data-limit=0"],
        "sources": [RUNTIME / "riscv64/boot.S", RUNTIME / "riscv64/hal.c", RUNTIME / "common/minic.c"],
        "linker": RUNTIME / "riscv64/kernel.ld", "link": ["-march=rv64gc", "-mabi=lp64d", "-mcmodel=medany"],
        "format": "binary", "image": "firmware.bin",
        "emulator": "qemu-system-riscv64",
        "qemu": ["-M", "virt", "-m", "128M", "-smp", "1", "-bios", "none", "-kernel", "{elf}"],
        "description": "QEMU RISC-V64 virt machine, M-mode; not a physical-board profile",
    },
}
