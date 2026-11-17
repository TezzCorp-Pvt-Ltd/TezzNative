OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY { ram (rwx) : ORIGIN = 0x80000000, LENGTH = 128M }
PHDRS
{
  text PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  .text : ALIGN(16) { KEEP(*(.text.boot)) *(.text*) } >ram :text
  .rodata : ALIGN(16) { *(.rodata*) *(.srodata*) } >ram :rodata
  .data : ALIGN(16) {
    __global_pointer$ = . + 0x800;
    *(.sdata*) *(.data*)
  } >ram :data
  .bss (NOLOAD) : ALIGN(16) {
    __bss_start = .;
    *(.sbss*) *(.bss*) *(COMMON)
    . = ALIGN(8);
    __bss_end = .;
  } >ram :data
  __kernel_end = .;
  /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
