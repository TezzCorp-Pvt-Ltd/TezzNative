OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 1M;
  .text : ALIGN(4K) {
    KEEP(*(.multiboot2))
    *(.text.boot) *(.text*)
  } :text
  .rodata : ALIGN(4K) { *(.rodata*) } :rodata
  .data : ALIGN(4K) { *(.data*) } :data
  .bss (NOLOAD) : ALIGN(4K) {
    __bss_start = .;
    *(.bss*) *(COMMON)
    __bss_end = .;
  } :data
  __kernel_end = .;
  ASSERT(__kernel_end < 0x40000000, "kernel exceeds the initial identity map")
  /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
