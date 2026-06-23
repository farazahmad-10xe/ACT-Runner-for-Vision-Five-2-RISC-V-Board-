#include "runner_shared.h"

static __attribute__((unused)) uint64_t trapframe_get_reg(const TrapFrame *tf, uint32_t reg)
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

static __attribute__((unused)) void trapframe_set_reg(TrapFrame *tf, uint32_t reg, uint64_t value)
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

static int decode_csr_request(uint32_t insn, uint32_t *rd_out, uint32_t *rs1_out,
                              uint32_t *funct3_out, uint32_t *csr_out)
{
    if ((insn & 0x7fu) != 0x73u) return -1;
    if (((insn >> 12) & 0x7u) == 0u) return -1;
    if (rd_out) *rd_out = (insn >> 7) & 0x1fu;
    if (rs1_out) *rs1_out = (insn >> 15) & 0x1fu;
    if (funct3_out) *funct3_out = (insn >> 12) & 0x7u;
    if (csr_out) *csr_out = (insn >> 20) & 0xfffu;
    return 0;
}

static int csr_is_machine_level(uint32_t csr)
{
    return ((csr >> 8) & 0x3u) == 3u;
}

static int csr_is_supervisor_level(uint32_t csr)
{
    return ((csr >> 8) & 0x3u) == 1u;
}

static int csr_is_compat_safe(uint32_t csr)
{
    switch (csr) {
        case 0x300u:
        case 0x301u:
        case 0x306u:
        case 0x320u:
#if RUNNER_TOUCH_MENVCFG
        case 0x30au:
#endif
            return 1;
        default:
            return 0;
    }
}

static uint64_t compat_mstatus_mask(void)
{
    const uint64_t mstatus_sie = (1ULL << 1);
    const uint64_t mstatus_fs = (3ULL << 13);
    const uint64_t mstatus_vs = (3ULL << 9);
    const uint64_t mstatus_sum = (1ULL << 18);
    const uint64_t mstatus_mxr = (1ULL << 19);
    return mstatus_sie | mstatus_fs | mstatus_vs | mstatus_sum | mstatus_mxr;
}

static __attribute__((unused)) int compat_try_skip_lower_mstatus_write(uint32_t rd, uint32_t funct3,
                                                                       uint64_t src_value, TrapFrame *tf)
{
    if (!tf || rd != 0u) return 0;
    if (funct3 != 2u && funct3 != 6u) return 0;
    if ((src_value & ~compat_mstatus_mask()) != 0) return 0;

    g_runner_sbi.compat_forward_count++;
    return 1;
}

static int read_machine_csr(uint32_t csr, uint64_t *value_out)
{
    uint64_t value = 0;

    if (!value_out) return -1;
    switch (csr) {
        case 0x300u: asm volatile ("csrr %0, mstatus" : "=r"(value)); break;
        case 0x301u: asm volatile ("csrr %0, misa" : "=r"(value)); break;
        case 0x302u: asm volatile ("csrr %0, medeleg" : "=r"(value)); break;
        case 0x303u: asm volatile ("csrr %0, mideleg" : "=r"(value)); break;
        case 0x304u: asm volatile ("csrr %0, mie" : "=r"(value)); break;
        case 0x305u: asm volatile ("csrr %0, mtvec" : "=r"(value)); break;
        case 0x306u: asm volatile ("csrr %0, mcounteren" : "=r"(value)); break;
#if RUNNER_TOUCH_MENVCFG
        case 0x30au: asm volatile ("csrr %0, menvcfg" : "=r"(value)); break;
#endif
        case 0x320u: asm volatile ("csrr %0, mcountinhibit" : "=r"(value)); break;
        case 0x340u: asm volatile ("csrr %0, mscratch" : "=r"(value)); break;
        case 0x341u: asm volatile ("csrr %0, mepc" : "=r"(value)); break;
        case 0x344u: asm volatile ("csrr %0, mip" : "=r"(value)); break;
        case 0x3a0u: asm volatile ("csrr %0, pmpcfg0" : "=r"(value)); break;
        case 0x3b0u: asm volatile ("csrr %0, pmpaddr0" : "=r"(value)); break;
        case 0x3b1u: asm volatile ("csrr %0, pmpaddr1" : "=r"(value)); break;
        case 0x3b2u: asm volatile ("csrr %0, pmpaddr2" : "=r"(value)); break;
        case 0x3b3u: asm volatile ("csrr %0, pmpaddr3" : "=r"(value)); break;
        case 0x3b4u: asm volatile ("csrr %0, pmpaddr4" : "=r"(value)); break;
        case 0x3b5u: asm volatile ("csrr %0, pmpaddr5" : "=r"(value)); break;
        case 0x3b6u: asm volatile ("csrr %0, pmpaddr6" : "=r"(value)); break;
        case 0x3b7u: asm volatile ("csrr %0, pmpaddr7" : "=r"(value)); break;
        default: return -1;
    }
    *value_out = value;
    return 0;
}

static int write_machine_csr(uint32_t csr, uint64_t value)
{
    switch (csr) {
        case 0x300u: asm volatile ("csrw mstatus, %0" :: "r"(value)); return 0;
        case 0x301u: asm volatile ("csrw misa, %0" :: "r"(value)); return 0;
        case 0x302u: asm volatile ("csrw medeleg, %0" :: "r"(value)); return 0;
        case 0x303u: asm volatile ("csrw mideleg, %0" :: "r"(value)); return 0;
        case 0x304u: asm volatile ("csrw mie, %0" :: "r"(value)); return 0;
        case 0x305u: asm volatile ("csrw mtvec, %0" :: "r"(value)); return 0;
        case 0x306u: asm volatile ("csrw mcounteren, %0" :: "r"(value)); return 0;
#if RUNNER_TOUCH_MENVCFG
        case 0x30au: asm volatile ("csrw menvcfg, %0" :: "r"(value)); return 0;
#endif
        case 0x320u: asm volatile ("csrw mcountinhibit, %0" :: "r"(value)); return 0;
        case 0x340u: asm volatile ("csrw mscratch, %0" :: "r"(value)); return 0;
        case 0x341u: asm volatile ("csrw mepc, %0" :: "r"(value)); return 0;
        case 0x344u: asm volatile ("csrw mip, %0" :: "r"(value)); return 0;
        case 0x3a0u: asm volatile ("csrw pmpcfg0, %0" :: "r"(value)); return 0;
        case 0x3b0u: asm volatile ("csrw pmpaddr0, %0" :: "r"(value)); return 0;
        case 0x3b1u: asm volatile ("csrw pmpaddr1, %0" :: "r"(value)); return 0;
        case 0x3b2u: asm volatile ("csrw pmpaddr2, %0" :: "r"(value)); return 0;
        case 0x3b3u: asm volatile ("csrw pmpaddr3, %0" :: "r"(value)); return 0;
        case 0x3b4u: asm volatile ("csrw pmpaddr4, %0" :: "r"(value)); return 0;
        case 0x3b5u: asm volatile ("csrw pmpaddr5, %0" :: "r"(value)); return 0;
        case 0x3b6u: asm volatile ("csrw pmpaddr6, %0" :: "r"(value)); return 0;
        case 0x3b7u: asm volatile ("csrw pmpaddr7, %0" :: "r"(value)); return 0;
        default: return -1;
    }
}

static int read_supervisor_csr(uint32_t csr, uint64_t *value_out)
{
    uint64_t value = 0;

    if (!value_out) return -1;
    switch (csr) {
        case 0x100u: asm volatile ("csrr %0, sstatus" : "=r"(value)); break;
        case 0x104u: asm volatile ("csrr %0, sie" : "=r"(value)); break;
        case 0x105u: asm volatile ("csrr %0, stvec" : "=r"(value)); break;
        case 0x106u: asm volatile ("csrr %0, scounteren" : "=r"(value)); break;
#if RUNNER_TOUCH_MENVCFG
        case 0x10au: asm volatile ("csrr %0, senvcfg" : "=r"(value)); break;
#endif
        case 0x140u: asm volatile ("csrr %0, sscratch" : "=r"(value)); break;
        case 0x141u: asm volatile ("csrr %0, sepc" : "=r"(value)); break;
        case 0x142u: asm volatile ("csrr %0, scause" : "=r"(value)); break;
        case 0x143u: asm volatile ("csrr %0, stval" : "=r"(value)); break;
        case 0x144u: asm volatile ("csrr %0, sip" : "=r"(value)); break;
        case 0x180u: asm volatile ("csrr %0, satp" : "=r"(value)); break;
        default: return -1;
    }
    *value_out = value;
    return 0;
}

static int write_supervisor_csr(uint32_t csr, uint64_t value)
{
    switch (csr) {
        case 0x100u: asm volatile ("csrw sstatus, %0" :: "r"(value)); return 0;
        case 0x104u: asm volatile ("csrw sie, %0" :: "r"(value)); return 0;
        case 0x105u: asm volatile ("csrw stvec, %0" :: "r"(value)); return 0;
        case 0x106u: asm volatile ("csrw scounteren, %0" :: "r"(value)); return 0;
#if RUNNER_TOUCH_MENVCFG
        case 0x10au: asm volatile ("csrw senvcfg, %0" :: "r"(value)); return 0;
#endif
        case 0x140u: asm volatile ("csrw sscratch, %0" :: "r"(value)); return 0;
        case 0x141u: asm volatile ("csrw sepc, %0" :: "r"(value)); return 0;
        case 0x142u: asm volatile ("csrw scause, %0" :: "r"(value)); return 0;
        case 0x143u: asm volatile ("csrw stval, %0" :: "r"(value)); return 0;
        case 0x144u: asm volatile ("csrw sip, %0" :: "r"(value)); return 0;
        case 0x180u: asm volatile ("csrw satp, %0" :: "r"(value)); sfence_vma_all(); return 0;
        default: return -1;
    }
}

static int emulate_machine_csr(uint32_t insn, uint64_t src_value, uint64_t *result_out)
{
    uint32_t rd;
    uint32_t rs1;
    uint32_t funct3;
    uint32_t csr;
    uint64_t old_value;
    uint64_t write_value;

    if (!result_out) return -1;
    if (decode_csr_request(insn, &rd, &rs1, &funct3, &csr) != 0) return -1;
    if (!csr_is_machine_level(csr)) return -1;
    if (read_machine_csr(csr, &old_value) != 0) return -1;

    write_value = old_value;
    switch (funct3) {
        case 1u:
        case 5u:
            write_value = src_value;
            break;
        case 2u:
        case 6u:
            if (src_value != 0) write_value = old_value | src_value;
            break;
        case 3u:
        case 7u:
            if (src_value != 0) write_value = old_value & ~src_value;
            break;
        default:
            return -1;
    }

    if ((funct3 == 1u) || (funct3 == 2u) || (funct3 == 3u) ||
        ((funct3 >= 5u) && rs1 != 0u)) {
        if (write_value != old_value && write_machine_csr(csr, write_value) != 0) return -1;
    }

    *result_out = old_value;
    (void)rd;
    return 0;
}

static int emulate_supervisor_csr(uint32_t insn, uint64_t src_value, uint64_t *result_out)
{
    uint32_t rd;
    uint32_t rs1;
    uint32_t funct3;
    uint32_t csr;
    uint64_t old_value;
    uint64_t write_value;

    if (!result_out) return -1;
    if (decode_csr_request(insn, &rd, &rs1, &funct3, &csr) != 0) return -1;
    if (!csr_is_supervisor_level(csr)) return -1;
    if (read_supervisor_csr(csr, &old_value) != 0) return -1;

    write_value = old_value;
    switch (funct3) {
        case 1u:
        case 5u:
            write_value = src_value;
            break;
        case 2u:
        case 6u:
            if (src_value != 0) write_value = old_value | src_value;
            break;
        case 3u:
        case 7u:
            if (src_value != 0) write_value = old_value & ~src_value;
            break;
        default:
            return -1;
    }

    if ((funct3 == 1u) || (funct3 == 2u) || (funct3 == 3u) ||
        ((funct3 >= 5u) && rs1 != 0u)) {
        if (write_value != old_value && write_supervisor_csr(csr, write_value) != 0) return -1;
    }

    *result_out = old_value;
    (void)rd;
    return 0;
}

static __attribute__((unused)) int emulate_machine_csr_compat(uint32_t insn, uint64_t src_value, uint64_t *result_out)
{
    uint32_t rd;
    uint32_t rs1;
    uint32_t funct3;
    uint32_t csr;
    uint64_t old_value;
    uint64_t write_value;
    uint64_t mask;

    if (!result_out) return -1;
    if (decode_csr_request(insn, &rd, &rs1, &funct3, &csr) != 0) return -1;
    if (!csr_is_machine_level(csr)) return -1;
    if (!csr_is_compat_safe(csr)) return -2;
    if (read_machine_csr(csr, &old_value) != 0) return -1;

    write_value = old_value;
    switch (funct3) {
        case 1u:
        case 5u:
            write_value = src_value;
            break;
        case 2u:
        case 6u:
            if (src_value != 0) write_value = old_value | src_value;
            break;
        case 3u:
        case 7u:
            if (src_value != 0) write_value = old_value & ~src_value;
            break;
        default:
            return -1;
    }

    if (csr == 0x300u) {
        mask = compat_mstatus_mask();
        write_value = (old_value & ~mask) | (write_value & mask);
    }

    if ((funct3 == 1u) || (funct3 == 2u) || (funct3 == 3u) ||
        ((funct3 >= 5u) && rs1 != 0u)) {
        if (write_value != old_value && write_machine_csr(csr, write_value) != 0) return -1;
    }

    *result_out = old_value;
    (void)rd;
    return 0;
}

static __attribute__((unused)) void write_sbi_result(TrapFrame *tf, int64_t err, uint64_t value)
{
    if (!tf) return;
    tf->a0 = (uint64_t)err;
    tf->a1 = value;
}

void runner_reset_sbi_state(void)
{
    memset_local(&g_runner_sbi, 0, sizeof(g_runner_sbi));
}

void runner_prepare_smode_return_bridge(TrapFrame *tf)
{
    if (!tf) return;
    g_runner_sbi.pending_kind = RUNNER_SBI_PENDING_NONE;
    g_runner_sbi.lower_tf = 0;
    tf->a0 = RUNNER_TEST_OP_RETURN_TO_M;
    tf->a1 = 0;
    tf->a2 = 0;
    tf->a7 = RUNNER_TEST_SBI_EXT;
    g_lower_state.smode_trap_bridge_to_m = RUNNER_SMODE_BRIDGE_RETURN_TO_M;
}

int runner_prepare_smode_request_bridge(uint64_t sepc, TrapFrame *tf)
{
#if RUNNER_ENABLE_PRIVATE_SBI
    if (!tf || tf->a7 != RUNNER_TEST_SBI_EXT) return 0;
    g_runner_sbi.pending_kind = RUNNER_SBI_PENDING_FORWARDED_ECALL;
    g_runner_sbi.lower_tf = tf;
    g_runner_sbi.lower_sepc = sepc;
    g_runner_sbi.lower_insn = 0;
    g_runner_sbi.lower_src_value = 0;
    g_lower_state.smode_trap_bridge_to_m = RUNNER_SMODE_BRIDGE_REQUEST;
    return 1;
#else
    (void)sepc;
    (void)tf;
    return 0;
#endif
}

int runner_prepare_smode_illegal_csr_bridge(uint64_t scause, uint64_t sepc, TrapFrame *tf)
{
#if RUNNER_ENABLE_PRIVATE_SBI
    uint32_t rd;
    uint32_t rs1;
    uint32_t funct3;
    uint32_t csr;
    uint32_t insn;
    uint64_t src_value;
    (void)rd;

    if (scause != MCAUSE_ILLEGAL_INSN) return 0;
#if RUNNER_SMODE_CSR_POLICY != RUNNER_SMODE_CSR_POLICY_COMPAT
    (void)sepc;
    (void)tf;
    return 0;
#else
    if (!tf) return 0;
    insn = (uint32_t)g_last_trap.mtval;
    if (decode_csr_request(insn, &rd, &rs1, &funct3, &csr) != 0) return 0;
    if (!csr_is_machine_level(csr)) return 0;

    src_value = (funct3 >= 5u) ? (uint64_t)rs1 : trapframe_get_reg(tf, rs1);
    if (csr == 0x300u && compat_try_skip_lower_mstatus_write(rd, funct3, src_value, tf)) {
        return 1;
    }

    g_runner_sbi.pending_kind = RUNNER_SBI_PENDING_COMPAT_CSR;
    g_runner_sbi.lower_tf = tf;
    g_runner_sbi.lower_sepc = sepc;
    g_runner_sbi.lower_insn = insn;
    g_runner_sbi.lower_src_value = src_value;
    g_runner_sbi.lower_saved_a0 = tf->a0;
    g_runner_sbi.lower_saved_a1 = tf->a1;
    g_runner_sbi.lower_saved_a2 = tf->a2;
    g_runner_sbi.lower_saved_a7 = tf->a7;
    g_runner_sbi.compat_forward_count++;
    tf->a0 = RUNNER_TEST_OP_ACCESS_CSR;
    tf->a1 = (uint64_t)insn;
    tf->a2 = src_value;
    tf->a7 = RUNNER_TEST_SBI_EXT;
    g_lower_state.smode_trap_bridge_to_m = RUNNER_SMODE_BRIDGE_REQUEST;
    return 1;
#endif
#else
    (void)scause;
    (void)sepc;
    (void)tf;
    return 0;
#endif
}

static __attribute__((unused)) int tsbi_decode_csr_op(uint64_t op, uint32_t *csr_out, uint32_t *kind_out)
{
    uint32_t rd;
    uint32_t rs1;
    uint32_t funct3;
    uint32_t csr;
    uint32_t insn = (uint32_t)op;

    if ((op >> 32) != 0) return 0;
    if (decode_csr_request(insn, &rd, &rs1, &funct3, &csr) != 0) return 0;

    if (funct3 == 1u && rd == 0u && rs1 == 11u) {
        if (csr_out) *csr_out = csr;
        if (kind_out) *kind_out = 1u;
        return 1;
    }
    if (funct3 == 2u && rd == 0u && rs1 == 11u) {
        if (csr_out) *csr_out = csr;
        if (kind_out) *kind_out = 2u;
        return 1;
    }
    if (funct3 == 3u && rd == 0u && rs1 == 11u) {
        if (csr_out) *csr_out = csr;
        if (kind_out) *kind_out = 3u;
        return 1;
    }
    if (funct3 == 2u && rd == 10u && rs1 == 0u) {
        if (csr_out) *csr_out = csr;
        if (kind_out) *kind_out = 4u;
        return 1;
    }

    return 0;
}

static __attribute__((unused)) int tsbi_is_recognized_op(uint64_t op)
{
    uint32_t csr;

    if (op == TSBI_ECALL_TEST ||
        op == TSBI_GOTO_MMODE ||
        op == TSBI_GOTO_SMODE ||
        op == TSBI_GOTO_UMODE ||
        op == TSBI_GOTO_VSMODE ||
        op == TSBI_GOTO_VUMODE) {
        return 1;
    }

    return tsbi_decode_csr_op(op, &csr, 0);
}

int runner_prepare_smode_tsbi_bridge(uint64_t sepc, TrapFrame *tf)
{
#if RUNNER_ENABLE_TSBI
    uint32_t csr;
    uint64_t op;

    if (!tf) return 0;
    op = tf->a0;

    if (!tsbi_is_recognized_op(op)) return 0;
    if (op == TSBI_ECALL_TEST) return 0;
    if (tsbi_decode_csr_op(op, &csr, 0) && !csr_is_machine_level(csr)) return 0;

    g_runner_sbi.pending_kind = RUNNER_SBI_PENDING_TSBI;
    g_runner_sbi.lower_tf = tf;
    g_runner_sbi.lower_sepc = sepc;
    g_runner_sbi.lower_insn = (uint32_t)op;
    g_runner_sbi.lower_src_value = tf->a1;
    g_runner_sbi.lower_saved_a0 = tf->a0;
    g_runner_sbi.lower_saved_a1 = tf->a1;
    g_runner_sbi.lower_saved_a2 = tf->a2;
    g_runner_sbi.lower_saved_a7 = tf->a7;
    g_lower_state.smode_trap_bridge_to_m = RUNNER_SMODE_BRIDGE_REQUEST;
    return 1;
#else
    (void)sepc;
    (void)tf;
    return 0;
#endif
}

static __attribute__((unused)) int runner_tsbi_emulate_csr(uint64_t op, uint64_t src_value, uint64_t trap_mode,
                                                           uint64_t *value_out, int *has_value_out)
{
    uint32_t csr;
    uint32_t kind;
    uint64_t old_value = 0;
    int rc;

    if (!value_out || !has_value_out) return -1;
    if (!tsbi_decode_csr_op(op, &csr, &kind)) return -1;

    if (csr_is_machine_level(csr)) {
        if (trap_mode != EXEC_MODE_M) return -1;
        rc = emulate_machine_csr((uint32_t)op, src_value, &old_value);
    } else if (csr_is_supervisor_level(csr)) {
        rc = emulate_supervisor_csr((uint32_t)op, src_value, &old_value);
    } else {
        return -1;
    }

    if (rc != 0) return rc;
    *has_value_out = (kind == 4u);
    *value_out = (kind == 4u) ? old_value : 0;
    return 0;
}

int runner_handle_tsbi(TrapFrame *tf, uint64_t trap_pc, uint64_t trap_mode, uint64_t *next_pc_out)
{
#if RUNNER_ENABLE_TSBI
    uint64_t op;
    uint64_t value = 0;
    int has_value = 0;
    int rc;

    if (!tf || !next_pc_out) return 0;

    op = tf->a0;
    if (!tsbi_is_recognized_op(op)) return 0;

    if (op == TSBI_ECALL_TEST) {
        value = trap_pc;
        rc = 0;
    } else if (op == TSBI_GOTO_MMODE ||
               op == TSBI_GOTO_SMODE ||
               op == TSBI_GOTO_UMODE ||
               op == TSBI_GOTO_VSMODE ||
               op == TSBI_GOTO_VUMODE) {
        rc = -1;
    } else {
        rc = runner_tsbi_emulate_csr(op, tf->a1, trap_mode, &value, &has_value);
    }

    if (g_runner_sbi.pending_kind == RUNNER_SBI_PENDING_TSBI && g_runner_sbi.lower_tf) {
        g_runner_sbi.lower_tf->a0 = (rc == 0) ? value : (uint64_t)(int64_t)-1;
        g_runner_sbi.lower_tf->a1 = g_runner_sbi.lower_saved_a1;
        g_runner_sbi.lower_tf->a2 = g_runner_sbi.lower_saved_a2;
        g_runner_sbi.lower_tf->a7 = g_runner_sbi.lower_saved_a7;
        g_runner_sbi.pending_kind = RUNNER_SBI_PENDING_NONE;
        g_runner_sbi.lower_tf = 0;
    }

    g_runner_sbi.last_error = (uint64_t)(int64_t)rc;
    g_runner_sbi.direct_request_count++;
    if (rc == 0) {
        tf->a0 = value;
        (void)has_value;
    } else {
        tf->a0 = (uint64_t)(int64_t)-1;
    }
    *next_pc_out = trap_pc + 4ULL;
    return 1;
#else
    (void)tf;
    (void)trap_pc;
    (void)trap_mode;
    (void)next_pc_out;
    return 0;
#endif
}

int runner_handle_test_sbi(TrapFrame *tf, uint64_t trap_pc, uint64_t trap_mode, uint64_t *next_pc_out)
{
    uint64_t op;
#if RUNNER_ENABLE_PRIVATE_SBI
    uint64_t result_value = 0;
    int rc;
    uint32_t rd;
    uint32_t rs1;
    uint32_t funct3;
    uint32_t csr;
    uint32_t insn;
#endif
    (void)trap_mode;

    if (!tf || !next_pc_out) return 0;
    if (tf->a7 != RUNNER_TEST_SBI_EXT) return 0;

    op = tf->a0;
    if (op == RUNNER_TEST_OP_RETURN_TO_M) {
        *next_pc_out = g_runner_exec.test_resume_pc;
        return 1;
    }

#if !RUNNER_ENABLE_PRIVATE_SBI
    (void)trap_pc;
    (void)trap_mode;
    return 0;
#else
    switch (op) {
        case RUNNER_TEST_OP_ECALL_TEST:
            write_sbi_result(tf, 0, 0);
            *next_pc_out = trap_pc + 4ULL;
            return 1;
        case RUNNER_TEST_OP_GOTO_M_MODE:
        case RUNNER_TEST_OP_GOTO_S_MODE:
            g_runner_sbi.last_error = (uint64_t)(int64_t)-2;
            write_sbi_result(tf, -2, 0);
            *next_pc_out = trap_pc + 4ULL;
            return 1;
        case RUNNER_TEST_OP_ACCESS_CSR:
            insn = (uint32_t)tf->a1;
            if (g_runner_sbi.pending_kind == RUNNER_SBI_PENDING_COMPAT_CSR) {
                rc = emulate_machine_csr_compat(insn, tf->a2, &result_value);
            } else {
                rc = emulate_machine_csr(insn, tf->a2, &result_value);
            }
            g_runner_sbi.last_error = (uint64_t)(int64_t)rc;
            g_runner_sbi.direct_request_count++;
            if (g_runner_sbi.pending_kind == RUNNER_SBI_PENDING_COMPAT_CSR && g_runner_sbi.lower_tf) {
                if (rc == 0 && decode_csr_request(insn, &rd, &rs1, &funct3, &csr) == 0) {
                    g_runner_sbi.lower_tf->a0 = g_runner_sbi.lower_saved_a0;
                    g_runner_sbi.lower_tf->a1 = g_runner_sbi.lower_saved_a1;
                    g_runner_sbi.lower_tf->a2 = g_runner_sbi.lower_saved_a2;
                    g_runner_sbi.lower_tf->a7 = g_runner_sbi.lower_saved_a7;
                    trapframe_set_reg(g_runner_sbi.lower_tf, rd, result_value);
                    g_runner_sbi.pending_kind = RUNNER_SBI_PENDING_NONE;
                    g_runner_sbi.lower_tf = 0;
                    write_sbi_result(tf, 0, result_value);
                    *next_pc_out = trap_pc + 4ULL;
                    return 1;
                }
                g_runner_sbi.lower_tf->a0 = g_runner_sbi.lower_saved_a0;
                g_runner_sbi.lower_tf->a1 = g_runner_sbi.lower_saved_a1;
                g_runner_sbi.lower_tf->a2 = g_runner_sbi.lower_saved_a2;
                g_runner_sbi.lower_tf->a7 = g_runner_sbi.lower_saved_a7;
                g_runner_exec.test_tohost_value = TOHOST_TRAP;
                g_runner_exec.test_done = 1;
                *next_pc_out = g_runner_exec.test_resume_pc;
                return 1;
            }
            if (g_runner_sbi.pending_kind == RUNNER_SBI_PENDING_FORWARDED_ECALL && g_runner_sbi.lower_tf) {
                write_sbi_result(g_runner_sbi.lower_tf, (int64_t)rc, result_value);
                g_runner_sbi.pending_kind = RUNNER_SBI_PENDING_NONE;
                g_runner_sbi.lower_tf = 0;
                write_sbi_result(tf, (int64_t)rc, result_value);
                *next_pc_out = trap_pc + 4ULL;
                return 1;
            }
            write_sbi_result(tf, (int64_t)rc, result_value);
            *next_pc_out = trap_pc + 4ULL;
            return 1;
        default:
            if (g_runner_sbi.pending_kind == RUNNER_SBI_PENDING_FORWARDED_ECALL && g_runner_sbi.lower_tf) {
                write_sbi_result(g_runner_sbi.lower_tf, -2, 0);
                g_runner_sbi.pending_kind = RUNNER_SBI_PENDING_NONE;
                g_runner_sbi.lower_tf = 0;
                *next_pc_out = trap_pc + 4ULL;
                return 1;
            }
            write_sbi_result(tf, -2, 0);
            *next_pc_out = trap_pc + 4ULL;
            return 1;
    }
#endif
}
