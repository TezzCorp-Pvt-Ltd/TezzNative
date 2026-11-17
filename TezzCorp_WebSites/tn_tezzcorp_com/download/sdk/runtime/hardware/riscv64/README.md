# QEMU virt RV64 profile

This is a fixed, one-hart QEMU `virt` emulator profile, not a physical RISC-V
board port. Build for RV64GC/LP64D with `-mcmodel=medany`, freestanding/no-builtin,
no PIE, no stack protector, and no hosted runtime. Apply `-Dmain=tn_main` only
to the generated C object. Link this boot/HAL and the common minic runtime with
`kernel.ld`; run with `-machine virt -smp 1 -bios none -kernel kernel.elf` and
serial capture under a bounded timeout.

The entry remains in machine mode, initializes BSS, stacks, global pointer,
trap destination, and floating-point state, then calls the shared int64 hardware
services and the generated Tezz program. An unexpected trap reports failing
status 125 through a separate trap stack.

Implemented devices are the NS16550 UART at 0x10000000, CLINT `mtime` at
0x0200bff8 with the profile's fixed 10 MHz timebase, and the SiFive test finisher
at 0x00100000. UART reads are nonblocking and writes have bounded polling;
individual calls accept at most 1 MiB. Delays poll real `mtime`, so they also
work while global interrupts are disabled. Interrupt state services read/change
the actual machine-mode MIE bit. Individual interrupt sources remain disabled:
timer IRQ delivery, PLIC routing, and interrupt callbacks are not implemented.

Startup calls `tn_main` only after initialization. On return, it emits the exact
`TEZZ_HW_EXIT=<status>` UART marker and finishes QEMU with process status 0 for
success or 1 for a nonzero guest status. No physical GPIO, MMU, SMP, storage,
network, or additional RISC-V board support is claimed.

Profile addresses and device behavior are derived from QEMU's own
[virt board definition](https://github.com/qemu/qemu/blob/master/hw/riscv/virt.c),
[CLINT timer definitions](https://github.com/qemu/qemu/blob/master/include/hw/intc/riscv_aclint.h),
and [test finisher](https://github.com/qemu/qemu/blob/master/hw/misc/sifive_test.c).
The [QEMU virt documentation](https://www.qemu.org/docs/master/system/riscv/virt.html)
describes this virtual platform separately from physical boards. Custom DTBs,
ACLINT/AIA variants, and different timer timebases are outside this fixed profile.
