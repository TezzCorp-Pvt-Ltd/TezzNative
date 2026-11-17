OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS {
  . = 0x80000;
  .text : { KEEP(*(.text.boot)) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  . = ALIGN(16);
  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss*) *(COMMON)
    . = ALIGN(8);
    __bss_end = .;
  }
  . = ALIGN(16);
  .stack (NOLOAD) : {
    . += 0x10000;
    __stack_top = .;
  }
  ASSERT(__stack_top < 0x1000000, "Pi3 image exceeds the bring-up memory budget")
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
