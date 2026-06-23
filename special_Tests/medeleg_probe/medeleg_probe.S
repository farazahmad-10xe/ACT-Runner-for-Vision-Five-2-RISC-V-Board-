/* medeleg_probe.S
 * VF2/U74 M-mode test:
 * Probe medeleg writable bits and verify whether selected S-mode traps
 * are handled by S-mode or M-mode after setting one medeleg bit.
 */

.option norvc

#ifndef EXIT_CODE
#define EXIT_CODE 3
#endif

.equ UART_BASE, 0x10000000
.equ MODE_S, 1
.equ MODE_M, 3

.section .text, "ax", @progbits
.globl _start

_start:
    la      sp, stack_top

    csrr    t0, mtvec
    la      t1, saved_runner_mtvec
    sd      t0, 0(t1)

    csrr    t0, stvec
    la      t1, saved_runner_stvec
    sd      t0, 0(t1)

    csrr    t0, medeleg
    la      t1, saved_runner_medeleg
    sd      t0, 0(t1)

    csrr    t0, pmpcfg0
    la      t1, saved_runner_pmpcfg0
    sd      t0, 0(t1)

    csrr    t0, pmpaddr0
    la      t1, saved_runner_pmpaddr0
    sd      t0, 0(t1)

    /* Allow S-mode fetch/load/store from the probe image. */
    li      t0, -1
    csrw    pmpaddr0, t0
    li      t0, 0x1f              /* R/W/X + NAPOT */
    csrw    pmpcfg0, t0
    fence   rw, rw

    la      t0, m_trap_handler
    csrw    mtvec, t0
    la      t0, s_trap_handler
    csrw    stvec, t0

    la      t0, trap_count
    sd      zero, 0(t0)

    la      a1, msg_start
    call    uart_puts

    csrr    a0, mhartid
    la      a1, msg_mhartid
    call    print_label_hex

    csrr    a0, misa
    la      a1, msg_misa
    call    print_label_hex

    csrr    a0, medeleg
    la      a1, msg_initial_medeleg
    call    print_label_hex

    li      t0, -1
    csrw    medeleg, t0
    csrr    a0, medeleg
    la      t1, medeleg_warl_mask
    sd      a0, 0(t1)
    la      a1, msg_mask
    call    print_label_hex

    call    print_medeleg_warl_report

    /* Restore zero baseline before per-cause checks. */
    csrw    medeleg, zero

    li      s0, 1
    call    run_one_cause
    li      s0, 2
    call    run_one_cause
    li      s0, 3
    call    run_one_cause
    li      s0, 4
    call    run_one_cause
    li      s0, 6
    call    run_one_cause
    li      s0, 9
    call    run_one_cause

    call    run_auipc_plus2_fetch_probe
    call    run_act_jalr_fetch_probe

done:
    la      a1, msg_done
    call    uart_puts

    la      t0, saved_runner_pmpaddr0
    ld      t0, 0(t0)
    csrw    pmpaddr0, t0
    la      t0, saved_runner_pmpcfg0
    ld      t0, 0(t0)
    csrw    pmpcfg0, t0
    la      t0, saved_runner_medeleg
    ld      t0, 0(t0)
    csrw    medeleg, t0
    la      t0, saved_runner_stvec
    ld      t0, 0(t0)
    csrw    stvec, t0
    la      t0, saved_runner_mtvec
    ld      t0, 0(t0)
    csrw    mtvec, t0

    li      gp, EXIT_CODE
    la      t0, tohost
    sd      gp, 0(t0)
    fence   rw, rw

1:
    wfi
    j       1b


/* s0 = cause number to request and trigger */
run_one_cause:
    addi    sp, sp, -16
    sd      ra, 8(sp)
    sd      s0, 0(sp)

    la      a1, msg_case
    call    uart_puts
    mv      a0, s0
    la      a1, msg_cause
    call    print_label_hex

    li      t0, 1
    sll     t0, t0, s0
    la      t1, requested_medeleg
    sd      t0, 0(t1)

    csrw    medeleg, zero
    csrw    medeleg, t0
    csrr    t2, medeleg
    la      t1, observed_medeleg
    sd      t2, 0(t1)

    mv      a0, t0
    la      a1, msg_requested
    call    print_label_hex
    la      t1, observed_medeleg
    ld      a0, 0(t1)
    la      a1, msg_observed
    call    print_label_hex

    la      t1, current_cause
    sd      s0, 0(t1)
    la      t1, current_mode
    sd      zero, 0(t1)
    la      t1, current_mcause
    sd      zero, 0(t1)

    la      t0, s_entry
    csrw    mepc, t0

    csrr    t1, mstatus
    li      t2, 0x1800
    csrc    mstatus, t2
    li      t2, 0x0800
    csrs    mstatus, t2
    mret

m_after_probe:
    la      t1, current_mode
    ld      a0, 0(t1)
    la      a1, msg_result_mode
    call    print_label_hex

    la      t1, current_mcause
    ld      a0, 0(t1)
    la      a1, msg_result_cause
    call    print_label_hex

    la      t1, current_mepc
    ld      a0, 0(t1)
    la      a1, msg_result_epc
    call    print_label_hex

    csrr    a0, medeleg
    la      a1, msg_after_medeleg
    call    print_label_hex

    ld      s0, 0(sp)
    ld      ra, 8(sp)
    addi    sp, sp, 16
    ret


print_medeleg_warl_report:
    addi    sp, sp, -32
    sd      ra, 24(sp)
    sd      s0, 16(sp)
    sd      s1, 8(sp)

    la      a1, msg_warl_report
    call    uart_puts

    la      t0, medeleg_warl_mask
    ld      s1, 0(t0)
    li      s0, 0
1:
    mv      a0, s0
    la      a1, msg_warl_cause
    call    print_label_hex

    li      t0, 1
    sll     t0, t0, s0
    and     a0, s1, t0
    beqz    a0, 2f
    li      a0, 1
2:
    la      a1, msg_warl_writable
    call    print_label_hex

    addi    s0, s0, 1
    li      t0, 16
    blt     s0, t0, 1b

    la      a1, msg_warl_done
    call    uart_puts

    ld      s1, 8(sp)
    ld      s0, 16(sp)
    ld      ra, 24(sp)
    addi    sp, sp, 32
    ret


s_entry:
    la      t0, current_cause
    ld      t1, 0(t0)

    li      t2, 0x100
    beq     t1, t2, s_trigger_auipc_plus2_fetch
    li      t2, 0x101
    beq     t1, t2, s_trigger_act_jalr_fetch
    li      t2, 1
    beq     t1, t2, s_trigger_iaf
    li      t2, 2
    beq     t1, t2, s_trigger_illegal
    li      t2, 3
    beq     t1, t2, s_trigger_ebreak
    li      t2, 4
    beq     t1, t2, s_trigger_load_misaligned
    li      t2, 6
    beq     t1, t2, s_trigger_store_misaligned
    li      t2, 9
    beq     t1, t2, s_trigger_ecall_s
    j       s_return_ecall

s_trigger_iaf:
    li      t0, 0
    jalr    ra, 0(t0)
    j       s_return_ecall

s_trigger_auipc_plus2_fetch:
    la      t0, auipc_probe_inst
    addi    t0, t0, 2
    la      t1, fetch_probe_target
    sd      t0, 0(t1)
    lhu     t2, 0(t0)
    la      t1, fetch_probe_halfword
    sd      t2, 0(t1)
    jalr    zero, 0(t0)
    j       s_return_ecall

s_trigger_act_jalr_fetch:
    la      t0, act_jalr_probe_jalr
    la      t1, fetch_probe_inst
    sd      t0, 0(t1)
    la      t0, act_jalr_probe_target_inst
    addi    t0, t0, 2
    la      t1, fetch_probe_target
    sd      t0, 0(t1)
    lhu     t2, 0(t0)
    la      t1, fetch_probe_halfword
    sd      t2, 0(t1)
    j       act_jalr_probe_setup

s_trigger_illegal:
    .word   0
    j       s_return_ecall

s_trigger_ebreak:
    ebreak
    j       s_return_ecall

s_trigger_load_misaligned:
    la      t0, misalign_data
    ld      t1, 1(t0)
    j       s_return_ecall

s_trigger_store_misaligned:
    la      t0, misalign_data
    sd      t1, 1(t0)
    j       s_return_ecall

s_trigger_ecall_s:
    ecall
    j       s_return_ebreak

s_return_ecall:
    ecall
    j       s_return_ecall

s_return_ebreak:
    ebreak
    j       s_return_ebreak

.align 2
auipc_probe_inst:
    auipc   t0, 0
    addi    zero, zero, 0

.align 2
act_jalr_probe_setup:
    auipc   x11, 0
    addi    x11, x11, 9
act_jalr_probe_jalr:
    jalr    x1, 9(x11)
    addi    x0, x2, 0
act_jalr_probe_target_inst:
    auipc   x11, 0
    addi    zero, zero, 0


run_auipc_plus2_fetch_probe:
    addi    sp, sp, -16
    sd      ra, 8(sp)

    la      a1, msg_fetch_case
    call    uart_puts

    li      t0, 0x100
    la      t1, current_cause
    sd      t0, 0(t1)
    la      t1, current_mode
    sd      zero, 0(t1)
    la      t1, current_mcause
    sd      zero, 0(t1)
    la      t1, fetch_probe_target
    sd      zero, 0(t1)
    la      t1, fetch_probe_halfword
    sd      zero, 0(t1)

    li      t0, 0x6              /* Request delegation for cause 1 and 2. */
    csrw    medeleg, zero
    csrw    medeleg, t0
    csrr    a0, medeleg
    la      a1, msg_fetch_medeleg
    call    print_label_hex

    la      t0, auipc_probe_inst
    la      t1, fetch_probe_inst
    sd      t0, 0(t1)
    addi    t2, t0, 2
    la      t1, fetch_probe_target
    sd      t2, 0(t1)

    mv      a0, t0
    la      a1, msg_fetch_inst
    call    print_label_hex
    la      t1, fetch_probe_target
    ld      a0, 0(t1)
    la      a1, msg_fetch_target
    call    print_label_hex

    la      t0, s_entry
    csrw    mepc, t0

    csrr    t1, mstatus
    li      t2, 0x1800
    csrc    mstatus, t2
    li      t2, 0x0800
    csrs    mstatus, t2
    mret

m_after_fetch_probe:
    la      t1, fetch_probe_target
    ld      a0, 0(t1)
    la      a1, msg_fetch_target_seen
    call    print_label_hex

    la      t1, fetch_probe_halfword
    ld      a0, 0(t1)
    la      a1, msg_fetch_halfword
    call    print_label_hex

    la      t1, current_mode
    ld      a0, 0(t1)
    la      a1, msg_fetch_result_mode
    call    print_label_hex

    la      t1, current_mcause
    ld      a0, 0(t1)
    la      a1, msg_fetch_result_cause
    call    print_label_hex

    la      t1, current_mepc
    ld      a0, 0(t1)
    la      a1, msg_fetch_result_epc
    call    print_label_hex

    la      t1, current_mtval
    ld      a0, 0(t1)
    la      a1, msg_fetch_result_tval
    call    print_label_hex

    ld      ra, 8(sp)
    addi    sp, sp, 16
    ret


run_act_jalr_fetch_probe:
    addi    sp, sp, -16
    sd      ra, 8(sp)

    la      a1, msg_act_jalr_case
    call    uart_puts

    li      t0, 0x101
    la      t1, current_cause
    sd      t0, 0(t1)
    la      t1, current_mode
    sd      zero, 0(t1)
    la      t1, current_mcause
    sd      zero, 0(t1)
    la      t1, fetch_probe_target
    sd      zero, 0(t1)
    la      t1, fetch_probe_halfword
    sd      zero, 0(t1)

    li      t0, 0x6              /* Request delegation for cause 1 and 2. */
    csrw    medeleg, zero
    csrw    medeleg, t0
    csrr    a0, medeleg
    la      a1, msg_fetch_medeleg
    call    print_label_hex

    la      t0, act_jalr_probe_jalr
    la      t1, fetch_probe_inst
    sd      t0, 0(t1)
    la      t0, act_jalr_probe_target_inst
    addi    t0, t0, 2
    la      t1, fetch_probe_target
    sd      t0, 0(t1)

    la      t1, fetch_probe_inst
    ld      a0, 0(t1)
    la      a1, msg_act_jalr_inst
    call    print_label_hex
    la      t1, fetch_probe_target
    ld      a0, 0(t1)
    la      a1, msg_fetch_target
    call    print_label_hex

    la      t0, s_entry
    csrw    mepc, t0

    csrr    t1, mstatus
    li      t2, 0x1800
    csrc    mstatus, t2
    li      t2, 0x0800
    csrs    mstatus, t2
    mret

m_after_act_jalr_probe:
    la      t1, fetch_probe_target
    ld      a0, 0(t1)
    la      a1, msg_fetch_target_seen
    call    print_label_hex

    la      t1, fetch_probe_halfword
    ld      a0, 0(t1)
    la      a1, msg_fetch_halfword
    call    print_label_hex

    la      t1, current_mode
    ld      a0, 0(t1)
    la      a1, msg_fetch_result_mode
    call    print_label_hex

    la      t1, current_mcause
    ld      a0, 0(t1)
    la      a1, msg_fetch_result_cause
    call    print_label_hex

    la      t1, current_mepc
    ld      a0, 0(t1)
    la      a1, msg_fetch_result_epc
    call    print_label_hex

    la      t1, current_mtval
    ld      a0, 0(t1)
    la      a1, msg_fetch_result_tval
    call    print_label_hex

    ld      ra, 8(sp)
    addi    sp, sp, 16
    ret


m_trap_handler:
    csrr    t0, mcause
    csrr    t1, mepc
    csrr    t2, mtval
    csrr    t3, mstatus

    la      t4, current_cause
    ld      t5, 0(t4)

    /* If this is the cause currently under test, record it as the tested trap. */
    beq     t0, t5, m_record_tested_trap

    /* Return traps after a delegated S-handler path. */
    li      t6, 9
    beq     t0, t6, m_finish_from_return_trap
    li      t6, 3
    beq     t0, t6, m_finish_from_return_trap

    /* Non-delegated tested trap arrived in M-mode. */
m_record_tested_trap:
    la      t4, current_mode
    li      t6, MODE_M
    sd      t6, 0(t4)
    la      t4, current_mcause
    sd      t0, 0(t4)
    la      t4, current_mepc
    sd      t1, 0(t4)
    la      t4, current_mtval
    sd      t2, 0(t4)
    la      t4, current_xstatus
    sd      t3, 0(t4)
    call    print_trap_m
    j       m_resume_mmode

m_finish_from_return_trap:
    call    print_return_trap_m

m_resume_mmode:
    la      t0, current_cause
    ld      t1, 0(t0)
    li      t2, 0x100
    beq     t1, t2, m_resume_fetch_probe
    li      t2, 0x101
    beq     t1, t2, m_resume_act_jalr_probe
    la      t0, m_after_probe
    csrw    mepc, t0
    csrr    t1, mstatus
    li      t2, 0x1800
    csrc    mstatus, t2
    csrs    mstatus, t2
    mret

m_resume_act_jalr_probe:
    la      t0, m_after_act_jalr_probe
    csrw    mepc, t0
    csrr    t1, mstatus
    li      t2, 0x1800
    csrc    mstatus, t2
    csrs    mstatus, t2
    mret

m_resume_fetch_probe:
    la      t0, m_after_fetch_probe
    csrw    mepc, t0
    csrr    t1, mstatus
    li      t2, 0x1800
    csrc    mstatus, t2
    csrs    mstatus, t2
    mret


s_trap_handler:
    csrr    t0, scause
    csrr    t1, sepc
    csrr    t2, stval
    csrr    t3, sstatus

    la      t4, current_mode
    li      t5, MODE_S
    sd      t5, 0(t4)
    la      t4, current_mcause
    sd      t0, 0(t4)
    la      t4, current_mepc
    sd      t1, 0(t4)
    la      t4, current_mtval
    sd      t2, 0(t4)
    la      t4, current_xstatus
    sd      t3, 0(t4)
    call    print_trap_s

    la      t4, current_cause
    ld      t5, 0(t4)
    li      t6, 9
    beq     t5, t6, s_use_ebreak_return

    la      t0, s_return_ecall
    csrw    sepc, t0
    sret

s_use_ebreak_return:
    la      t0, s_return_ebreak
    csrw    sepc, t0
    sret


print_trap_m:
    addi    sp, sp, -16
    sd      ra, 8(sp)
    la      a1, msg_trap_m
    call    uart_puts
    j       print_trap_common

print_trap_s:
    addi    sp, sp, -16
    sd      ra, 8(sp)
    la      a1, msg_trap_s
    call    uart_puts
    j       print_trap_common

print_return_trap_m:
    addi    sp, sp, -16
    sd      ra, 8(sp)
    la      a1, msg_return_m
    call    uart_puts
    ld      ra, 8(sp)
    addi    sp, sp, 16
    ret

print_trap_common:
    la      t0, current_mcause
    ld      a0, 0(t0)
    la      a1, msg_trap_cause
    call    print_label_hex
    la      t0, current_mepc
    ld      a0, 0(t0)
    la      a1, msg_trap_epc
    call    print_label_hex
    la      t0, current_mtval
    ld      a0, 0(t0)
    la      a1, msg_trap_tval
    call    print_label_hex
    ld      ra, 8(sp)
    addi    sp, sp, 16
    ret


/**************** UART helpers ****************/

uart_putc:
    li      t6, UART_BASE
1:
    lb      t5, 20(t6)
    andi    t5, t5, 0x20
    beqz    t5, 1b
    sb      a0, 0(t6)
    ret

uart_puts:
    addi    sp, sp, -16
    sd      ra, 8(sp)
1:
    lb      a0, 0(a1)
    beqz    a0, 2f
    addi    a1, a1, 1
    call    uart_putc
    j       1b
2:
    ld      ra, 8(sp)
    addi    sp, sp, 16
    ret

uart_hex64:
    addi    sp, sp, -16
    sd      ra, 8(sp)
    mv      t3, a0
    li      t4, 16
1:
    srli    t5, t3, 60
    li      t6, 10
    blt     t5, t6, 2f
    addi    t5, t5, 87
    j       3f
2:
    addi    t5, t5, 48
3:
    mv      a0, t5
    call    uart_putc
    slli    t3, t3, 4
    addi    t4, t4, -1
    bnez    t4, 1b
    ld      ra, 8(sp)
    addi    sp, sp, 16
    ret

print_label_hex:
    addi    sp, sp, -32
    sd      ra, 24(sp)
    sd      s1, 16(sp)
    mv      s1, a0
    call    uart_puts
    mv      a0, s1
    call    uart_hex64
    la      a1, nl
    call    uart_puts
    ld      s1, 16(sp)
    ld      ra, 24(sp)
    addi    sp, sp, 32
    ret


.section .rodata, "a", @progbits
msg_start:
    .asciz "\r\n[DBG] medeleg_probe start\r\n"
msg_mhartid:
    .asciz "[DBG] mhartid=0x"
msg_misa:
    .asciz "[DBG] misa=0x"
msg_initial_medeleg:
    .asciz "[DBG] initial_medeleg=0x"
msg_mask:
    .asciz "[DBG] medeleg_warl_mask=0x"
msg_warl_report:
    .asciz "\r\n[WARLDBG] medeleg writable report for causes 0..15\r\n"
msg_warl_cause:
    .asciz "[WARLDBG] cause=0x"
msg_warl_writable:
    .asciz "[WARLDBG] writable 1=yes 0=no =0x"
msg_warl_done:
    .asciz "[WARLDBG] report done\r\n"
msg_case:
    .asciz "\r\n[CASEDBG] ----------------\r\n"
msg_cause:
    .asciz "[CASEDBG] requested_cause=0x"
msg_requested:
    .asciz "[CASEDBG] requested_medeleg=0x"
msg_observed:
    .asciz "[CASEDBG] observed_medeleg=0x"
msg_trap_m:
    .asciz "[TRAPDBG] handled_in=M\r\n"
msg_trap_s:
    .asciz "[TRAPDBG] handled_in=S\r\n"
msg_return_m:
    .asciz "[TRAPDBG] return_trap_to_M\r\n"
msg_trap_cause:
    .asciz "[TRAPDBG] cause=0x"
msg_trap_epc:
    .asciz "[TRAPDBG] epc=0x"
msg_trap_tval:
    .asciz "[TRAPDBG] tval=0x"
msg_result_mode:
    .asciz "[RESULT] trap_mode 1=S 3=M =0x"
msg_result_cause:
    .asciz "[RESULT] trap_cause=0x"
msg_result_epc:
    .asciz "[RESULT] trap_epc=0x"
msg_after_medeleg:
    .asciz "[RESULT] final_medeleg=0x"
msg_fetch_case:
    .asciz "\r\n[FETCHDBG] auipc_plus2_fetch_probe\r\n"
msg_act_jalr_case:
    .asciz "\r\n[FETCHDBG] act_jalr_upper_halfword_probe\r\n"
msg_fetch_medeleg:
    .asciz "[FETCHDBG] medeleg_after_request_bits_1_2=0x"
msg_fetch_inst:
    .asciz "[FETCHDBG] auipc_inst_addr=0x"
msg_act_jalr_inst:
    .asciz "[FETCHDBG] act_jalr_inst_addr=0x"
msg_fetch_target:
    .asciz "[FETCHDBG] requested_fetch_addr=0x"
msg_fetch_target_seen:
    .asciz "[FETCHDBG] recorded_fetch_addr=0x"
msg_fetch_halfword:
    .asciz "[FETCHDBG] halfword_at_target=0x"
msg_fetch_result_mode:
    .asciz "[FETCHDBG] trap_mode 1=S 3=M =0x"
msg_fetch_result_cause:
    .asciz "[FETCHDBG] trap_cause=0x"
msg_fetch_result_epc:
    .asciz "[FETCHDBG] trap_epc=0x"
msg_fetch_result_tval:
    .asciz "[FETCHDBG] trap_tval=0x"
msg_done:
    .asciz "\r\n[DBG] medeleg_probe done\r\n"
nl:
    .asciz "\r\n"


.section .tohost, "aw", @progbits
.align 3
.globl tohost
tohost:
    .dword 0
.globl fromhost
fromhost:
    .dword 0


.section .runner_fail_scratch, "aw", @progbits
.align 3
.globl begin_failure_scratch
begin_failure_scratch:
    .fill 64, 1, 0
.globl end_failure_scratch
end_failure_scratch:


.section .signature, "aw", @progbits
.align 3
.globl begin_signature
begin_signature:
saved_runner_mtvec:
    .dword 0
saved_runner_stvec:
    .dword 0
saved_runner_medeleg:
    .dword 0
saved_runner_pmpcfg0:
    .dword 0
saved_runner_pmpaddr0:
    .dword 0
medeleg_warl_mask:
    .dword 0
requested_medeleg:
    .dword 0
observed_medeleg:
    .dword 0
current_cause:
    .dword 0
current_mode:
    .dword 0
current_mcause:
    .dword 0
current_mepc:
    .dword 0
current_mtval:
    .dword 0
current_xstatus:
    .dword 0
fetch_probe_inst:
    .dword 0
fetch_probe_target:
    .dword 0
fetch_probe_halfword:
    .dword 0
trap_count:
    .dword 0
misalign_data:
    .dword 0x1122334455667788

.globl end_signature
end_signature:


.section .bss, "aw", @nobits
.align 12
.space 4096
stack_top:
