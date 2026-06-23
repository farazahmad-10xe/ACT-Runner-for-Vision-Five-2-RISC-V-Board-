#include "runner_shared.h"

void reset_lower_mode_state(void)
{
    g_lower_state.lower_entry_pc = 0;
    g_lower_state.lower_user_sp = 0;
    g_lower_state.smode_trap_bridge_to_m = 0;
    g_lower_state.uflow_marker = 0;
}

void enter_smode_from_m(uint64_t entry, uintptr_t smode_sp)
{
    const uint64_t mstatus_mie = (1ULL << 3);
    const uint64_t mstatus_mpie = (1ULL << 7);
    const uint64_t mstatus_mpp_mask = (3ULL << 11);
    const uint64_t mstatus_mpp_s = (1ULL << 11);
    const uint64_t mstatus_sie = (1ULL << 1);
    uint64_t mstatus = read_mstatus();

    mstatus &= ~(mstatus_mpp_mask | mstatus_mie | mstatus_sie);
    mstatus |= mstatus_mpie | mstatus_mpp_s;
    (void)mstatus_sie;

    g_lower_state.active_exec_mode = EXEC_MODE_S;
    uart_puts("[FLOW] privilege_transition\n");
    uart_puts("       from_mode=M to_mode=S\n");
    uart_puts("       entry=");
    uart_put_hex(entry);
    uart_puts("\n");
    uart_puts("       stack=");
    uart_put_hex((uint64_t)smode_sp);
    uart_puts("\n");
    uart_puts("[LFLOW] m_to_s\n");
    uart_puts("        entry=");
    uart_put_hex(entry);
    uart_puts("\n");
    uart_puts("        sp=");
    uart_put_hex((uint64_t)smode_sp);
    uart_puts("\n");
    uart_puts("        mstatus=");
    uart_put_hex(mstatus);
    uart_puts("\n");

    write_mstatus(mstatus);
    write_csr_mepc(entry);
    asm volatile ("fence rw, rw" ::: "memory");
    __asm__ volatile(
        "mv sp, %0\n"
        "mret\n"
        :
        : "r"(smode_sp)
        : "memory"
    );
    __builtin_unreachable();
}

static uint64_t trapframe_get_reg_local(const TrapFrame *tf, uint32_t reg)
{
    if (!tf) return 0;
    switch (reg) {
        case 0: return 0;
        case 1: return tf->ra;
        case 2: return tf->sp;
        case 3: return tf->gp;
        case 4: return tf->tp;
        case 5: return tf->t0;
        case 6: return tf->t1;
        case 7: return tf->t2;
        case 8: return tf->s0;
        case 9: return tf->s1;
        case 10: return tf->a0;
        case 11: return tf->a1;
        case 12: return tf->a2;
        case 13: return tf->a3;
        case 14: return tf->a4;
        case 15: return tf->a5;
        case 16: return tf->a6;
        case 17: return tf->a7;
        case 18: return tf->s2;
        case 19: return tf->s3;
        case 20: return tf->s4;
        case 21: return tf->s5;
        case 22: return tf->s6;
        case 23: return tf->s7;
        case 24: return tf->s8;
        case 25: return tf->s9;
        case 26: return tf->s10;
        case 27: return tf->s11;
        case 28: return tf->t3;
        case 29: return tf->t4;
        case 30: return tf->t5;
        case 31: return tf->t6;
        default: return 0;
    }
}

static void trapframe_set_reg_local(TrapFrame *tf, uint32_t reg, uint64_t value)
{
    if (!tf || reg == 0) return;
    switch (reg) {
        case 1: tf->ra = value; break;
        case 2: tf->sp = value; break;
        case 3: tf->gp = value; break;
        case 4: tf->tp = value; break;
        case 5: tf->t0 = value; break;
        case 6: tf->t1 = value; break;
        case 7: tf->t2 = value; break;
        case 8: tf->s0 = value; break;
        case 9: tf->s1 = value; break;
        case 10: tf->a0 = value; break;
        case 11: tf->a1 = value; break;
        case 12: tf->a2 = value; break;
        case 13: tf->a3 = value; break;
        case 14: tf->a4 = value; break;
        case 15: tf->a5 = value; break;
        case 16: tf->a6 = value; break;
        case 17: tf->a7 = value; break;
        case 18: tf->s2 = value; break;
        case 19: tf->s3 = value; break;
        case 20: tf->s4 = value; break;
        case 21: tf->s5 = value; break;
        case 22: tf->s6 = value; break;
        case 23: tf->s7 = value; break;
        case 24: tf->s8 = value; break;
        case 25: tf->s9 = value; break;
        case 26: tf->s10 = value; break;
        case 27: tf->s11 = value; break;
        case 28: tf->t3 = value; break;
        case 29: tf->t4 = value; break;
        case 30: tf->t5 = value; break;
        case 31: tf->t6 = value; break;
        default: break;
    }
}

static int32_t sign_extend_local(uint32_t value, uint32_t bits)
{
    uint32_t shift = 32u - bits;
    return (int32_t)(value << shift) >> shift;
}

static void smode_uart_putc_raw(char c)
{
    while ((mmio_read8(UART_BASE + UART_LSR) & UART_LSR_THRE) == 0) {}
    mmio_write8(UART_BASE + UART_THR, (uint8_t)c);
}

static int emulate_umode_uart_access(uint64_t scause, uint64_t sepc, uint64_t stval, TrapFrame *tf)
{
    const uint64_t sstatus_sum = (1ULL << 18);
    int valid = 0;
    uint32_t insn;
    uint32_t opcode;
    uint32_t funct3;
    uint32_t rd;
    uint32_t rs1;
    uint32_t rs2;
    uint64_t addr;

    if (!tf || g_lower_state.requested_exec_mode != EXEC_MODE_U) return 0;
    if (stval < UART_BASE || stval >= UART_BASE + UART_SIZE) return 0;
    if (scause != MCAUSE_LOAD_PAGE_FAULT && scause != MCAUSE_STORE_PAGE_FAULT) return 0;

    {
        uint64_t saved_sstatus = read_sstatus();
        write_sstatus(saved_sstatus | sstatus_sum);
        insn = read_insn_word(sepc, &valid);
        write_sstatus(saved_sstatus);
    }
    if (!valid || (insn & 0x3u) != 0x3u) return 0;

    opcode = insn & 0x7fu;
    funct3 = (insn >> 12) & 0x7u;
    rd = (insn >> 7) & 0x1fu;
    rs1 = (insn >> 15) & 0x1fu;
    rs2 = (insn >> 20) & 0x1fu;

    if (scause == MCAUSE_LOAD_PAGE_FAULT && opcode == 0x03u && funct3 == 0x4u) {
        int32_t imm = sign_extend_local(insn >> 20, 12u);
        addr = trapframe_get_reg_local(tf, rs1) + (uint64_t)(int64_t)imm;
        if (addr != stval) return 0;
        if (addr == UART_BASE + UART_LSR) {
            trapframe_set_reg_local(tf, rd, UART_LSR_THRE);
            return 1;
        }
        if (addr == UART_BASE + UART_RBR) {
            trapframe_set_reg_local(tf, rd, 0);
            return 1;
        }
        return 0;
    }

    if (scause == MCAUSE_STORE_PAGE_FAULT && opcode == 0x23u && funct3 == 0x0u) {
        uint32_t imm_raw = ((insn >> 25) << 5) | ((insn >> 7) & 0x1fu);
        int32_t imm = sign_extend_local(imm_raw, 12u);
        addr = trapframe_get_reg_local(tf, rs1) + (uint64_t)(int64_t)imm;
        if (addr != stval) return 0;
        if (addr == UART_BASE + UART_THR) {
            smode_uart_putc_raw((char)(trapframe_get_reg_local(tf, rs2) & 0xffu));
            return 1;
        }
        return 0;
    }

    return 0;
}

uint64_t s_trap_c(uint64_t scause, uint64_t sepc, uint64_t stval, uint64_t sstatus,
                  TrapFrame *tf)
{
    if (!g_runner_exec.runner_active) {
        return sepc;
    }

    capture_last_trap_in_mode(tf, scause, sepc, stval, sstatus, EXEC_MODE_S);
    g_lower_state.uflow_marker = 4;

    if (scause == MCAUSE_ECALL_U && tf && tf->a7 == RUNNER_TEST_SBI_EXT) {
        if (runner_prepare_smode_request_bridge(sepc, tf)) {
            g_lower_state.uflow_marker = 0x11;
            return sepc + 4ULL;
        }
    }

    if ((scause == MCAUSE_ECALL_U || scause == MCAUSE_ECALL_S) && tf) {
        uint64_t next_pc = 0;
        if (runner_prepare_smode_tsbi_bridge(sepc, tf)) {
            g_lower_state.uflow_marker = 0x13;
            return sepc + 4ULL;
        }
        if (runner_handle_tsbi(tf, sepc, EXEC_MODE_S, &next_pc)) {
            g_lower_state.uflow_marker = 0x14;
            return next_pc;
        }
    }

    if (scause == MCAUSE_ECALL_U || scause == MCAUSE_ECALL_S) {
        g_runner_exec.test_done = 1;
        g_lower_state.uflow_marker = 5;
        runner_prepare_smode_return_bridge(tf);
        return sepc + 4ULL;
    }

    if (scause == MCAUSE_STI) {
        g_runner_exec.test_tohost_value = TOHOST_TIMEOUT;
        g_runner_exec.test_done = 1;
        g_lower_state.uflow_marker = 6;
        runner_prepare_smode_return_bridge(tf);
        return sepc;
    }

    if (scause == MCAUSE_SSI || scause == MCAUSE_SEI) {
        g_runner_exec.test_tohost_value = TOHOST_TRAP;
        g_runner_exec.test_done = 1;
        g_lower_state.uflow_marker = 7;
        runner_prepare_smode_return_bridge(tf);
        return sepc;
    }

    if (runner_prepare_smode_illegal_csr_bridge(scause, sepc, tf)) {
        g_lower_state.uflow_marker = 0x12;
        return sepc + 4ULL;
    }

    if (emulate_umode_uart_access(scause, sepc, stval, tf)) {
        g_lower_state.uflow_marker = 0x15;
        return sepc + 4ULL;
    }

    if (scause == MCAUSE_INST_MISALIGNED ||
        scause == MCAUSE_LOAD_MISALIGNED ||
        scause == MCAUSE_STORE_MISALIGNED ||
        ((scause & MCAUSE_INTERRUPT_BIT) == 0)) {
        g_runner_exec.test_tohost_value = TOHOST_TRAP;
        g_runner_exec.test_done = 1;
        g_lower_state.uflow_marker = 8;
        runner_prepare_smode_return_bridge(tf);
        return sepc;
    }

    if (scause == MCAUSE_LCOFI) {
        g_runner_exec.test_tohost_value = TOHOST_TRAP;
        g_runner_exec.test_done = 1;
        g_lower_state.uflow_marker = 9;
        runner_prepare_smode_return_bridge(tf);
        return sepc;
    }

    g_runner_exec.test_done = 1;
    g_runner_exec.test_tohost_value = TOHOST_TRAP;
    runner_prepare_smode_return_bridge(tf);
    return sepc;
}

__attribute__((naked, aligned(4))) void s_trap_entry(void)
{
    __asm__ volatile(
        "csrrw sp, sscratch, sp\n"
        "addi sp, sp, -256\n"
        "sd ra, 0(sp)\n"
        "sd gp, 8(sp)\n"
        "sd tp, 16(sp)\n"
        "sd t0, 24(sp)\n"
        "sd t1, 32(sp)\n"
        "sd t2, 40(sp)\n"
        "sd s0, 48(sp)\n"
        "sd s1, 56(sp)\n"
        "sd a0, 64(sp)\n"
        "sd a1, 72(sp)\n"
        "sd a2, 80(sp)\n"
        "sd a3, 88(sp)\n"
        "sd a4, 96(sp)\n"
        "sd a5, 104(sp)\n"
        "sd a6, 112(sp)\n"
        "sd a7, 120(sp)\n"
        "sd s2, 128(sp)\n"
        "sd s3, 136(sp)\n"
        "sd s4, 144(sp)\n"
        "sd s5, 152(sp)\n"
        "sd s6, 160(sp)\n"
        "sd s7, 168(sp)\n"
        "sd s8, 176(sp)\n"
        "sd s9, 184(sp)\n"
        "sd s10, 192(sp)\n"
        "sd s11, 200(sp)\n"
        "sd t3, 208(sp)\n"
        "sd t4, 216(sp)\n"
        "sd t5, 224(sp)\n"
        "sd t6, 232(sp)\n"
        "csrr t0, sscratch\n"
        "sd t0, 240(sp)\n"
        "csrw sscratch, sp\n"
        "la t0, g_runner_exec\n"
        "ld gp, %c0(t0)\n"
        "csrr a0, scause\n"
        "csrr a1, sepc\n"
        "csrr a2, stval\n"
        "csrr a3, sstatus\n"
        "mv a4, sp\n"
        "call s_trap_c\n"
        "csrw sepc, a0\n"
        "la t0, g_lower_state\n"
        "ld t1, %c1(t0)\n"
        "beqz t1, 1f\n"
        "sd zero, %c1(t0)\n"
        "ld a0, 64(sp)\n"
        "ld a1, 72(sp)\n"
        "ld a2, 80(sp)\n"
        "ld a7, 120(sp)\n"
        "ecall\n"
        "1:\n"
        "ld t0, 240(sp)\n"
        "csrw sscratch, t0\n"
        "ld ra, 0(sp)\n"
        "ld gp, 8(sp)\n"
        "ld tp, 16(sp)\n"
        "ld t0, 24(sp)\n"
        "ld t1, 32(sp)\n"
        "ld t2, 40(sp)\n"
        "ld s0, 48(sp)\n"
        "ld s1, 56(sp)\n"
        "ld a0, 64(sp)\n"
        "ld a1, 72(sp)\n"
        "ld a2, 80(sp)\n"
        "ld a3, 88(sp)\n"
        "ld a4, 96(sp)\n"
        "ld a5, 104(sp)\n"
        "ld a6, 112(sp)\n"
        "ld a7, 120(sp)\n"
        "ld s2, 128(sp)\n"
        "ld s3, 136(sp)\n"
        "ld s4, 144(sp)\n"
        "ld s5, 152(sp)\n"
        "ld s6, 160(sp)\n"
        "ld s7, 168(sp)\n"
        "ld s8, 176(sp)\n"
        "ld s9, 184(sp)\n"
        "ld s10, 192(sp)\n"
        "ld s11, 200(sp)\n"
        "ld t3, 208(sp)\n"
        "ld t4, 216(sp)\n"
        "ld t5, 224(sp)\n"
        "ld t6, 232(sp)\n"
        "addi sp, sp, 256\n"
        "csrrw sp, sscratch, sp\n"
        "sret\n"
        :
        : "i"(offsetof(RunnerExecState, runner_saved_gp)),
          "i"(offsetof(LowerModeState, smode_trap_bridge_to_m))
    );
}
