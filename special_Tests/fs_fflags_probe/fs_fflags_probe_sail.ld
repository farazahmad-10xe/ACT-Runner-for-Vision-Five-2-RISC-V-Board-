OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
  . = 0x80000000;

  .text : ALIGN(4) {
    *(.text .text.*)
  }

  .rodata : ALIGN(8) {
    *(.rodata .rodata.*)
  }

  .tohost : ALIGN(8) {
    *(.tohost)
  }

  .runner_fail_scratch : ALIGN(8) {
    *(.runner_fail_scratch)
  }

  .signature : ALIGN(8) {
    *(.signature)
  }

  .data : ALIGN(8) {
    *(.data .data.*)
  }

  .bss : ALIGN(8) {
    *(.bss .bss.* COMMON)
  }
}
