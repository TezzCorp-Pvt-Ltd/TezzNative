/* Raspberry Pi 3B, AArch64 firmware entry at 0x80000. */
  .section .text.boot, "ax"
  .global _start
  .extern __bss_start
  .extern __bss_end
  .extern __stack_top
  .extern tn_hw_init
  .extern tn_main
  .extern tn_hw_finish
  .extern tn_hw_exception

_start:
  msr daifset, #15
  mrs x1, mpidr_el1
  and x1, x1, #255
  cbnz x1, .Lpark
  mrs x1, CurrentEL
  cmp x1, #12
  b.eq .Lel3
  cmp x1, #8
  b.eq .Lel2
  cmp x1, #4
  b.eq .Lel1
  b .Lpark

.Lel3:
  mov x1, #0x501             /* NS, HCE, AArch64 lower ELs */
  msr scr_el3, x1
  msr cptr_el3, xzr
  adr x1, .Lel2
  msr elr_el3, x1
  mov x1, #0x3c9             /* EL2h, DAIF masked */
  msr spsr_el3, x1
  eret

.Lel2:
  mov x1, #0x80000000        /* HCR_EL2.RW: EL1 AArch64 */
  msr hcr_el2, x1
  mov x1, #0x33ff            /* architectural RES1, FP traps disabled */
  msr cptr_el2, x1
  mov x1, #3                /* EL1 physical counter/timer access */
  msr cnthctl_el2, x1
  msr cntvoff_el2, xzr
  mrs x1, midr_el1
  msr vpidr_el2, x1
  mrs x1, mpidr_el1
  msr vmpidr_el2, x1
  adr x1, .Lel1
  msr elr_el2, x1
  mov x1, #0x3c5             /* EL1h, DAIF masked */
  msr spsr_el2, x1
  eret

.Lel1:
  msr spsel, #1
  ldr x1, =0x30d00800        /* SCTLR_EL1 RES1; MMU and caches off */
  msr sctlr_el1, x1
  isb
  mov x1, #0x300000          /* CPACR_EL1.FPEN: enable FP and SIMD */
  msr cpacr_el1, x1
  isb
  msr fpcr, xzr             /* round-to-nearest, gradual underflow */
  msr fpsr, xzr
  adr x1, .Lvectors
  msr vbar_el1, x1
  isb
  ldr x1, =__bss_start
  ldr x2, =__bss_end
.Lclear:
  cmp x1, x2
  b.hs .Lstack
  str xzr, [x1], #8
  b .Lclear
.Lstack:
  ldr x1, =__stack_top
  mov sp, x1
  bl tn_hw_init
  cbnz x0, .Lfinish
  bl tn_main
.Lfinish:
  bl tn_hw_finish
.Lpark:
  wfe
  b .Lpark

  .balign 2048
.Lvectors:
  .rept 16
  b .Lexception
  .space 124
  .endr
.Lexception:
  msr daifset, #15
  mrs x0, esr_el1
  mrs x1, elr_el1
  b tn_hw_exception

  .section .note.GNU-stack, "", %progbits
