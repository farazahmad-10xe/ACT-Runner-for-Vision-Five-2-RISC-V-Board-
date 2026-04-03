/* link.ld - VF2 tiny M-mode firmware + embedded ACT payload(s) */
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
  . = 0x40000000;

  .text : ALIGN(16) { *(.text .text.*) }

  .rodata : ALIGN(16) { *(.rodata .rodata.*) *(.srodata .srodata.*) }

  .act_elf : ALIGN(16) {
    _act_elf_start = .;
    KEEP(*(.act_elf))
    KEEP(*(.act_elf.*))
    _act_elf_end = .;
  }

  .act_pack : ALIGN(16) {
    _act_pack_start = .;
    KEEP(*(.act_pack))
    KEEP(*(.act_pack.*))
    _act_pack_end = .;
  }

  .data : ALIGN(16) { *(.data .data.*) *(.sdata .sdata.*) }

  .bss : ALIGN(16) {
    __bss_start = .;
    *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
    __bss_end = .;
  }

  .stack (NOLOAD) : ALIGN(16) {
    __stack_bottom = .;
    . = . + 0x4000;
    __stack_top = .;
  }

  /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
