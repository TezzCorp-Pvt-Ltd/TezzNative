# QEMU PC x86_64 profile

This target boots a freestanding TezzNative program through a BIOS/Multiboot2
loader. It uses the C IR backend, an ordinary x86_64 SysV C ABI, and the shared
hardware API's int64 service arguments. It is a single-CPU emulator profile;
physical motherboard support has not been verified.

`boot.S` validates Multiboot2 entry, clears BSS, creates an identity mapping of
the first GiB, enters long mode, enables x87/SSE state, and calls the C startup.
The startup initializes hardware and calls the generated `tn_main` function.
Success is emitted only after that function returns zero.

Implemented services:

- COM1 16550 UART at 115200 baud, including a local loopback initialization
  check, bounded polling writes, and nonblocking reads. A call accepts at most
  1 MiB; a timed-out write returns its partial count, or -1 if no byte was sent.
- Legacy PIC and PIT channel 0 IRQs. The timer combines an IRQ counter with a
  latched PIT count using its nominal 1193182 Hz input and divisor 1193.
- Interrupt enable, disable, and prior-state reporting. Positive delays require
  enabled interrupts; zero delays succeed and negative delays are rejected.
- A terminal `TEZZ_HW_EXIT=<status>` serial/debug-port marker and the QEMU
  `isa-debug-exit` device at port 0xf4: process status 33 indicates guest success,
  status 35 indicates a nonzero guest return.

Keep interrupts enabled for clock operation. Masking them for more than one PIT
period can lose elapsed periods because a legacy PIC retains only one pending
IRQ. The clock remains monotonic, but does not promise elapsed-time accuracy in
that condition. No SMP, APIC, USB, storage, network, display, or physical GPIO
support is claimed by this profile. Unexpected CPU interrupts/exceptions stop
the guest with a distinct failing emulator status.

Compile all C objects with freestanding/no-builtin/no-PIE/no-stack-protector and
`-mno-red-zone`; apply `-Dmain=tn_main` only to the generated C object. Link the
boot and interrupt assembly, this HAL, and the shared freestanding minic runtime
with `kernel.ld`, without a hosted C runtime. Package it with GRUB's
`multiboot2` command and run a BIOS QEMU PC with one CPU, serial capture, and
`isa-debug-exit,iobase=0xf4,iosize=0x04` under a bounded test timeout.
