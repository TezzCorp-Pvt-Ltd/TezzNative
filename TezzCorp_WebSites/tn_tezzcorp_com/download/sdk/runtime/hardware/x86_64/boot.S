/* BIOS/Multiboot2 entry for the QEMU PC x86_64 profile. */
  .section .multiboot2,"a"
  .balign 8
.Lmb2:
  .long 0xe85250d6
  .long 0
  .long .Lmb2_end - .Lmb2
  .long -(0xe85250d6 + (.Lmb2_end - .Lmb2))
  .short 0, 0
  .long 8
.Lmb2_end:

  .section .text.boot,"ax"
  .code32
  .global _start
_start:
  cli
  cld
  cmpl $0x36d76289, %eax
  jne .Lboot_fail
  movl %ebx, %esi

  /* Clear the entire BSS, including the page tables and private stack. */
  movl $__bss_start, %edi
  movl $__bss_end, %ecx
  subl %edi, %ecx
  xorl %eax, %eax
  rep stosb
  movl %esi, tn_hw_x86_boot_info
  movl $tn_hw_x86_stack_top, %esp

  /* Require the long-mode capability advertised by our selected profile. */
  movl $0x80000000, %eax
  cpuid
  cmpl $0x80000001, %eax
  jb .Lboot_fail
  movl $0x80000001, %eax
  cpuid
  testl $0x20000000, %edx
  jz .Lboot_fail

  /* Identity-map the first GiB with 2 MiB pages. */
  movl $tn_hw_x86_pdpt, %eax
  orl $3, %eax
  movl %eax, tn_hw_x86_pml4
  movl $tn_hw_x86_pd, %eax
  orl $3, %eax
  movl %eax, tn_hw_x86_pdpt
  movl $tn_hw_x86_pd, %edi
  movl $0x83, %eax
  movl $512, %ecx
.Lmap:
  movl %eax, (%edi)
  movl $0, 4(%edi)
  addl $0x200000, %eax
  addl $8, %edi
  loop .Lmap

  movl %cr4, %eax
  orl $0x620, %eax       /* PAE, OSFXSR and OSXMMEXCPT */
  movl %eax, %cr4
  movl $tn_hw_x86_pml4, %eax
  movl %eax, %cr3
  movl $0xc0000080, %ecx
  rdmsr
  orl $0x100, %eax       /* EFER.LME */
  wrmsr
  lgdt .Lgdt_ptr
  movl %cr0, %eax
  andl $0xfffffffb, %eax /* clear EM */
  orl $0x80000002, %eax  /* paging and MP */
  movl %eax, %cr0
  ljmp $8, $.Llong_mode

.Lboot_fail:
  movb $'B', %al
  outb %al, $0xe9
  movw $0xf4, %dx
  movl $127, %eax
  outl %eax, %dx
  cli
.Lfailed_halt:
  hlt
  jmp .Lfailed_halt

  .code64
.Llong_mode:
  movw $0x10, %ax
  movw %ax, %ds
  movw %ax, %es
  movw %ax, %ss
  xorw %ax, %ax
  movw %ax, %fs
  movw %ax, %gs
  leaq tn_hw_x86_stack_top(%rip), %rsp
  andq $-16, %rsp        /* SysV C ABI: aligned before CALL. */
  xorq %rbp, %rbp
  fninit
  ldmxcsr .Lmxcsr(%rip)
  call tn_hw_x86_start
  cli
.Lreturned_halt:
  hlt
  jmp .Lreturned_halt

  .section .rodata,"a"
  .balign 8
.Lgdt:
  .quad 0
  .quad 0x00af9a000000ffff
  .quad 0x00cf92000000ffff
.Lgdt_end:
.Lgdt_ptr:
  .word .Lgdt_end - .Lgdt - 1
  .long .Lgdt
.Lmxcsr:
  .long 0x1f80

  .section .bss,"aw",@nobits
  .balign 8
  .global tn_hw_x86_boot_info
tn_hw_x86_boot_info:
  .skip 8
  .balign 4096
tn_hw_x86_pml4:
  .skip 4096
tn_hw_x86_pdpt:
  .skip 4096
tn_hw_x86_pd:
  .skip 4096
  .balign 16
tn_hw_x86_stack:
  .skip 65536
tn_hw_x86_stack_top:

  .section .note.GNU-stack,"",@progbits
