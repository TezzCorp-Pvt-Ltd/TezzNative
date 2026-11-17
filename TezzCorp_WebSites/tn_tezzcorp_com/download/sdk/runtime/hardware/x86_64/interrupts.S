/* IRQ0 touches no C state except its aligned counter and preserves registers. */
  .text
  .global tn_hw_x86_timer_entry
tn_hw_x86_timer_entry:
  pushq %rax
  incq tn_hw_x86_ticks(%rip)
  movb $0x20, %al
  outb %al, $0x20
  popq %rax
  iretq

/* All unexpected vectors fail closed instead of returning corrupt CPU state. */
  .global tn_hw_x86_fault_entry
tn_hw_x86_fault_entry:
  cli
  movb $'X', %al
  outb %al, $0xe9
  movw $0xf4, %dx
  movl $126, %eax
  outl %eax, %dx
1:
  hlt
  jmp 1b

  .section .note.GNU-stack,"",@progbits
