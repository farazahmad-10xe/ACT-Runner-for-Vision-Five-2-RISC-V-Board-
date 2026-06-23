# ExceptionsSm-00 Regenerated Current Artifacts

Generated from:

`/home/lpt-10xe/riscv-arch-test/work-vf2-exceptionssm-regenerated-collection`

Files:

- `ExceptionsSm-00.S` - regenerated ACT assembly source.
- `ExceptionsSm-00.elf` - regenerated ELF.
- `ExceptionsSm-00.elf.objdump` - dump/disassembly for the regenerated ELF.
- `ExceptionsSm-00.sig.trace` - Sail execution trace.
- `ExceptionsSm-00.sig.log` - Sail run log.
- `ExceptionsSm_regenerated_current_uart.log` - VF2 UART hardware run log.
- `ExceptionsSm_regenerated_current_rerun_uart.log` - VF2 UART hardware rerun log using the same regenerated ELF.
- `sail.json` - VF2 Sail configuration copied with this artifact bundle.

Root cause summary:

The hardware failure is at `MEPC=0x800005a4`. The objdump maps that PC to:

```asm
800005a4: 0007c583  lbu x11,0(x15)
```

under label:

```text
ExceptionsSm_cg_cp_load_address_misaligned_lbu_off1
```

The UART log reports:

```text
Expected cause: Load address misaligned
Actual cause:   Load access fault
MCAUSE: 5
MTVAL:  1
```

The rerun UART log reports the same failure:

```text
Expected cause: Load address misaligned
Actual cause:   Load access fault
MEPC:   0x00000000800005a4
MCAUSE: 0x0000000000000005
MTVAL:  0x0000000000000001
```

The Sail trace reports the same actual behavior:

```text
0x00000000800005A4 (0x0007C583) lbu x11, 0x0(x15)
trapping from M to M to handle load-access-fault
CSR mcause <- 0x0000000000000005
CSR mtval  <- 0x0000000000000001
CSR mepc   <- 0x00000000800005A4
```

Conclusion:

This is an ACT/testgen expected-result issue. `lbu` is a byte load and cannot raise a load-address-misaligned exception. With effective address `0x1`, the valid observed exception is load access fault. Sail and VF2 hardware agree.
