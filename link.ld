/* link.ld - board-parameterized minimal M-mode UART runner */
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
  . = DEFINED(BOARD_FW_LOAD_ADDR) ? BOARD_FW_LOAD_ADDR : 0x40000000;

  __text_start = .;
  .text : ALIGN(16) { *(.text .text.*) }
  __text_end = .;

  __rodata_start = .;
  .rodata : ALIGN(16) { *(.rodata .rodata.*) *(.srodata .srodata.*) }
  __rodata_end = .;

  __data_start = .;
  .data : ALIGN(16) { *(.data .data.*) *(.sdata .sdata.*) }
  __data_end = .;

  __bss_start = .;
  .bss : ALIGN(16) {
    *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
    __bss_end = .;
  }

  .stack (NOLOAD) : ALIGN(16) {
    __stack_bottom = .;
    . = . + (DEFINED(BOARD_FW_STACK_BYTES) ? BOARD_FW_STACK_BYTES : 0x4000);
    __stack_top = .;
  }

  /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
