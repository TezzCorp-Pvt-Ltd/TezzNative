/* One-hart QEMU virt, RV64GC, -bios none, M-mode entry. */
  .section .text.boot,"ax",@progbits
  .balign 4
  .global _start
_start:
  csrw mie, zero
  csrci mstatus, 8
  bnez a0, .Lpark
  .option push
  .option norelax
  la gp, __global_pointer$
  .option pop
  la sp, __stack_top
  la t0, tn_hw_rv_trap_entry
  csrw mtvec, t0
  la t0, __bss_start
  la t1, __bss_end
.Lbss:
  bgeu t0, t1, .Lbss_done
  sd zero, 0(t0)
  addi t0, t0, 8
  j .Lbss
.Lbss_done:
  /* The generated C backend may use the RV64GC double-precision FPU. */
  li t0, 0x6000
  csrs mstatus, t0
  csrw fcsr, zero
  fence.i
  call tn_hw_rv_start
.Lpark:
  wfi
  j .Lpark

  .balign 4
  .global tn_hw_rv_trap_entry
tn_hw_rv_trap_entry:
  csrw mie, zero
  csrci mstatus, 8
  la sp, __trap_stack_top
  li a0, 125
  call tn_hw_finish
  j .Lpark

  .section .bss.stack,"aw",@nobits
  .balign 16
  .skip 65536
  .global __stack_top
__stack_top:
  .skip 4096
  .global __trap_stack_top
__trap_stack_top:

  .section .note.GNU-stack,"",@progbits
