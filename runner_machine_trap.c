#include "runner_shared.h"

#define SBI_SUCCESS                 0LL
#define SBI_ERR_NOT_SUPPORTED      -2LL
#define SBI_ERR_INVALID_PARAM      -3LL
#define SBI_IMPL_ID_VF2_RUNNER 0x56463201ULL
#define SBI_IMPL_VER_VF2_RUNNER    1ULL
#define SBI_SPEC_VERSION_2_0       (2ULL << 24)

#define SBI_EXT_BASE               0x10ULL
#define SBI_EXT_TIME               0x54494d45ULL
#define SBI_EXT_IPI                0x735049ULL
#define SBI_EXT_RFNC               0x52464e43ULL

#define SBI_LEGACY_SET_TIMER       0ULL
#define SBI_LEGACY_CONSOLE_PUTCHAR 1ULL
#define SBI_LEGACY_CONSOLE_GETCHAR 2ULL
#define SBI_LEGACY_CLEAR_IPI       3ULL
#define SBI_LEGACY_SEND_IPI        4ULL
#define SBI_LEGACY_REMOTE_FENCE_I  5ULL
#define SBI_LEGACY_REMOTE_SFENCE_VMA 6ULL
#define SBI_LEGACY_REMOTE_SFENCE_VMA_ASID 7ULL
#define SBI_LEGACY_SHUTDOWN        8ULL

#define SBI_BASE_GET_SPEC_VERSION  0ULL
#define SBI_BASE_GET_IMPL_ID       1ULL
#define SBI_BASE_GET_IMPL_VERSION  2ULL
#define SBI_BASE_PROBE_EXTENSION   3ULL
#define SBI_BASE_GET_MVENDORID     4ULL
#define SBI_BASE_GET_MARCHID       5ULL
#define SBI_BASE_GET_MIMPID        6ULL

#define SBI_TIME_SET_TIMER         0ULL
#define SBI_IPI_SEND_IPI           0ULL

#define SBI_RFNC_REMOTE_FENCE_I        0ULL
#define SBI_RFNC_REMOTE_SFENCE_VMA     1ULL
#define SBI_RFNC_REMOTE_SFENCE_VMA_ASID 2ULL
#define SBI_RFNC_REMOTE_HFENCE_GVMA_VMID 3ULL
#define SBI_RFNC_REMOTE_HFENCE_GVMA    4ULL
#define SBI_RFNC_REMOTE_HFENCE_VVMA_ASID 5ULL
#define SBI_RFNC_REMOTE_HFENCE_VVMA    6ULL

static void sbi_return(TrapFrame *tf, int64_t err, uint64_t value)
{
    tf->a0 = (uint64_t)err;
    tf->a1 = value;
}

static int sbi_extension_supported(uint64_t ext)
{
    switch (ext) {
        case SBI_EXT_BASE:
        case SBI_EXT_TIME:
        case SBI_EXT_IPI:
        case SBI_EXT_RFNC:
            return 1;
        default:
            return 0;
    }
}

static int sbi_hartmask_includes(uint64_t hart_mask, uint64_t hart_mask_base, uint32_t hartid)
{
    if (hartid < hart_mask_base) return 0;
    {
        uint64_t shift = (uint64_t)hartid - hart_mask_base;
        if (shift >= 64ULL) return 0;
        return ((hart_mask >> shift) & 1ULL) != 0ULL;
    }
}

static int sbi_legacy_load_hart_mask(uint64_t mask_addr, uint64_t *mask_out)
{
    if (!mask_out) return -1;
    *mask_out = 0;
    if (mask_addr == 0) return 0;
    if ((mask_addr & 0x7ULL) != 0 || !is_valid_ddr_addr(mask_addr) || !is_valid_ddr_addr(mask_addr + 7ULL)) {
        return -1;
    }
    *mask_out = *(volatile const uint64_t *)(uintptr_t)mask_addr;
    return 0;
}

static void sbi_program_timer(uint64_t stime_value)
{
    *mtimecmp_ptr((uint32_t)read_csr_mhartid()) = stime_value;
    asm volatile ("fence rw, rw" ::: "memory");
}

static void sbi_clear_ipi_local(void)
{
    *msip_ptr((uint32_t)read_csr_mhartid()) = 0;
    asm volatile ("fence rw, rw" ::: "memory");
}

static void sbi_send_ipi_mask(uint64_t hart_mask, uint64_t hart_mask_base)
{
    if (sbi_hartmask_includes(hart_mask, hart_mask_base, RUNNER_HART_ID)) {
        *msip_ptr(RUNNER_HART_ID) = 1;
    }
    if (sbi_hartmask_includes(hart_mask, hart_mask_base, MONITOR_HART_ID)) {
        *msip_ptr(MONITOR_HART_ID) = 1;
    }
    asm volatile ("fence rw, rw" ::: "memory");
}

static void sbi_remote_fence_local(void)
{
    asm volatile ("fence rw, rw" ::: "memory");
    sync_icache();
}

static void capture_hart1_csr_snapshot(uint64_t reason, uint64_t mcause,
                                       uint64_t mepc, uint64_t mtval,
                                       uint64_t mstatus)
{
    g_runner_exec.hart1_csr_snapshot_valid = 0;
    asm volatile ("fence rw, rw" ::: "memory");

    g_runner_exec.hart1_csr_snapshot_reason = reason;
    g_runner_exec.hart1_mhartid = read_csr_mhartid();
    g_runner_exec.hart1_mstatus = mstatus;
    g_runner_exec.hart1_mepc = mepc;
    g_runner_exec.hart1_mcause = mcause;
    g_runner_exec.hart1_mtval = mtval;
    g_runner_exec.hart1_medeleg = read_csr_medeleg();
    g_runner_exec.hart1_mideleg = read_csr_mideleg();
    g_runner_exec.hart1_satp = read_csr_satp();
    g_runner_exec.hart1_pmpcfg0 = read_csr_pmpcfg0();
    g_runner_exec.hart1_pmpaddr[0] = read_csr_pmpaddr0();
    g_runner_exec.hart1_pmpaddr[1] = read_csr_pmpaddr1();
    g_runner_exec.hart1_pmpaddr[2] = read_csr_pmpaddr2();
    g_runner_exec.hart1_pmpaddr[3] = read_csr_pmpaddr3();
    g_runner_exec.hart1_pmpaddr[4] = read_csr_pmpaddr4();
    g_runner_exec.hart1_pmpaddr[5] = read_csr_pmpaddr5();
    g_runner_exec.hart1_pmpaddr[6] = read_csr_pmpaddr6();
    g_runner_exec.hart1_pmpaddr[7] = read_csr_pmpaddr7();

    asm volatile ("fence rw, rw" ::: "memory");
    g_runner_exec.hart1_csr_snapshot_valid = 1;
    asm volatile ("fence rw, rw" ::: "memory");
}

static int handle_test_ecall(TrapFrame *tf, uint64_t mepc)
{
    uint64_t ext = tf->a7;
    uint64_t fid = tf->a6;

    switch (ext) {
        case SBI_LEGACY_SET_TIMER:
            sbi_program_timer(tf->a0);
            tf->a0 = 0;
            return 1;
        case SBI_LEGACY_CONSOLE_PUTCHAR:
            uart_putc((char)tf->a0);
            tf->a0 = 0;
            return 1;
        case SBI_LEGACY_CONSOLE_GETCHAR:
            tf->a0 = (uint64_t)(int64_t)-1;
            return 1;
        case SBI_LEGACY_CLEAR_IPI:
            sbi_clear_ipi_local();
            tf->a0 = 0;
            return 1;
        case SBI_LEGACY_SEND_IPI: {
            uint64_t hart_mask = 0;
            if (sbi_legacy_load_hart_mask(tf->a0, &hart_mask) != 0) {
                tf->a0 = (uint64_t)(int64_t)SBI_ERR_INVALID_PARAM;
            } else {
                sbi_send_ipi_mask(hart_mask, 0);
                tf->a0 = 0;
            }
            return 1;
        }
        case SBI_LEGACY_REMOTE_FENCE_I:
        case SBI_LEGACY_REMOTE_SFENCE_VMA:
        case SBI_LEGACY_REMOTE_SFENCE_VMA_ASID: {
            uint64_t hart_mask = 0;
            if (sbi_legacy_load_hart_mask(tf->a0, &hart_mask) != 0) {
                tf->a0 = (uint64_t)(int64_t)SBI_ERR_INVALID_PARAM;
            } else {
                (void)hart_mask;
                sbi_remote_fence_local();
                tf->a0 = 0;
            }
            return 1;
        }
        case SBI_LEGACY_SHUTDOWN:
            request_deferred_reset();
            tf->a0 = 0;
            return 1;
        case SBI_EXT_BASE:
            switch (fid) {
                case SBI_BASE_GET_SPEC_VERSION:
                    sbi_return(tf, SBI_SUCCESS, SBI_SPEC_VERSION_2_0);
                    return 1;
                case SBI_BASE_GET_IMPL_ID:
                    sbi_return(tf, SBI_SUCCESS, SBI_IMPL_ID_VF2_RUNNER);
                    return 1;
                case SBI_BASE_GET_IMPL_VERSION:
                    sbi_return(tf, SBI_SUCCESS, SBI_IMPL_VER_VF2_RUNNER);
                    return 1;
                case SBI_BASE_PROBE_EXTENSION:
                    sbi_return(tf, SBI_SUCCESS, sbi_extension_supported(tf->a0) ? 1ULL : 0ULL);
                    return 1;
                case SBI_BASE_GET_MVENDORID:
                    sbi_return(tf, SBI_SUCCESS, read_mvendorid());
                    return 1;
                case SBI_BASE_GET_MARCHID:
                    sbi_return(tf, SBI_SUCCESS, read_marchid());
                    return 1;
                case SBI_BASE_GET_MIMPID:
                    sbi_return(tf, SBI_SUCCESS, read_mimpid());
                    return 1;
                default:
                    sbi_return(tf, SBI_ERR_NOT_SUPPORTED, 0);
                    return 1;
            }
        case SBI_EXT_TIME:
            if (fid == SBI_TIME_SET_TIMER) {
                sbi_program_timer(tf->a0);
                sbi_return(tf, SBI_SUCCESS, 0);
                return 1;
            }
            sbi_return(tf, SBI_ERR_NOT_SUPPORTED, 0);
            return 1;
        case SBI_EXT_IPI:
            if (fid == SBI_IPI_SEND_IPI) {
                sbi_send_ipi_mask(tf->a0, tf->a1);
                sbi_return(tf, SBI_SUCCESS, 0);
                return 1;
            }
            sbi_return(tf, SBI_ERR_NOT_SUPPORTED, 0);
            return 1;
        case SBI_EXT_RFNC:
            switch (fid) {
                case SBI_RFNC_REMOTE_FENCE_I:
                case SBI_RFNC_REMOTE_SFENCE_VMA:
                case SBI_RFNC_REMOTE_SFENCE_VMA_ASID:
                case SBI_RFNC_REMOTE_HFENCE_GVMA_VMID:
                case SBI_RFNC_REMOTE_HFENCE_GVMA:
                case SBI_RFNC_REMOTE_HFENCE_VVMA_ASID:
                case SBI_RFNC_REMOTE_HFENCE_VVMA:
                    sbi_remote_fence_local();
                    sbi_return(tf, SBI_SUCCESS, 0);
                    return 1;
                default:
                    sbi_return(tf, SBI_ERR_NOT_SUPPORTED, 0);
                    return 1;
            }
        default:
            break;
    }

    (void)mepc;
    return 0;
}

static void emit_trap_entry_log(uint64_t mcause, uint64_t mepc,
                                uint64_t mtval, uint64_t mstatus)
{
    uart_puts("[TRAP] mcause=");
    uart_put_hex(mcause);
    uart_puts(" mepc=");
    uart_put_hex(mepc);
    uart_puts(" mtval=");
    uart_put_hex(mtval);
    uart_puts(" mstatus=");
    uart_put_hex(mstatus);
    uart_puts("\n");

    if (((mstatus >> 13) & 3ULL) != 0) {
        uart_puts("[TRAP] fflags=");
        uart_put_hex(read_fflags());
        uart_puts(" frm=");
        uart_put_hex(read_frm());
        uart_puts(" fcsr=");
        uart_put_hex(read_fcsr());
        uart_puts("\n");
    }
}

static uint64_t emit_trap_return_log(uint64_t mcause, uint64_t mepc,
                                     uint64_t mtval, uint64_t mstatus,
                                     uint64_t return_pc,
                                     const char *reason)
{
    uart_puts("[ACTRET] mcause=");
    uart_put_hex(mcause);
    uart_puts(" mepc=");
    uart_put_hex(mepc);
    uart_puts(" mtval=");
    uart_put_hex(mtval);
    uart_puts(" mstatus=");
    uart_put_hex(mstatus);
    uart_puts(" return_pc=");
    uart_put_hex(return_pc);
    uart_puts(" runner_active=");
    uart_put_hex(g_runner_exec.runner_active ? 1ULL : 0ULL);
    uart_puts(" test_done=");
    uart_put_hex(g_runner_exec.test_done ? 1ULL : 0ULL);
    uart_puts(" reason=");
    uart_puts(reason);
    uart_puts("\n");
    return return_pc;
}

uint64_t trap_c(uint64_t mcause, uint64_t mepc, uint64_t mtval, uint64_t mstatus,
                TrapFrame *tf)
{
#if !RUNNER_DISABLE_TEST_TIMEOUT
    if (g_runner_exec.runner_active &&
        g_runner_exec.test_deadline_mtime != 0 &&
        *mtime_ptr() >= g_runner_exec.test_deadline_mtime) {
        g_runner_exec.test_tohost_value = TOHOST_TIMEOUT;
        g_runner_exec.test_done = 1;
        return emit_trap_return_log(mcause, mepc, mtval, mstatus,
                                    g_runner_exec.test_resume_pc, "timeout");
    }
#endif

    if (g_runner_exec.runner_active && g_runner_exec.test_done) {
        return emit_trap_return_log(mcause, mepc, mtval, mstatus,
                                    g_runner_exec.test_resume_pc, "already_done");
    }

    if (g_runner_exec.runner_active) {
        capture_first_trap_in_mode(tf, mcause, mepc, mtval, mstatus, EXEC_MODE_M);
    }

    if (mcause == MCAUSE_MSI) {
        if (g_runner_exec.runner_active) capture_last_trap_in_mode(tf, mcause, mepc, mtval, mstatus, EXEC_MODE_M);
        *msip_ptr(RUNNER_HART_ID) = 0;
        asm volatile ("fence rw, rw" ::: "memory");

        if (g_runner_exec.runner_active && g_runner_exec.monitor_seen_tohost) {
            capture_hart1_csr_snapshot(1, mcause, mepc, mtval, mstatus);
            g_runner_exec.test_done = 1;
            return emit_trap_return_log(mcause, mepc, mtval, mstatus,
                                        g_runner_exec.test_resume_pc, "msi_tohost");
        }
        return emit_trap_return_log(mcause, mepc, mtval, mstatus, mepc, "msi");
    }

    if (mcause == MCAUSE_MTI) {
        if (g_runner_exec.runner_active) capture_last_trap_in_mode(tf, mcause, mepc, mtval, mstatus, EXEC_MODE_M);
        *mtimecmp_ptr(0) = ~0ULL;
        *mtimecmp_ptr(1) = ~0ULL;
        *mtimecmp_ptr(2) = ~0ULL;
        asm volatile ("fence rw, rw" ::: "memory");
        if (g_runner_exec.runner_active) {
#if RUNNER_DISABLE_TEST_TIMEOUT
            return emit_trap_return_log(mcause, mepc, mtval, mstatus, mepc, "timer_ignored");
#else
            g_runner_exec.test_tohost_value = TOHOST_TIMEOUT;
            g_runner_exec.test_done = 1;
            return emit_trap_return_log(mcause, mepc, mtval, mstatus,
                                        g_runner_exec.test_resume_pc, "timer_timeout");
#endif
        }
        return emit_trap_return_log(mcause, mepc, mtval, mstatus, mepc, "timer");
    }

    if (mcause == MCAUSE_ECALL_S &&
        g_runner_exec.runner_active &&
        tf) {
        uint64_t next_pc = 0;
        if (runner_handle_tsbi(tf, mepc, EXEC_MODE_M, &next_pc)) {
            g_lower_state.smode_trap_bridge_to_m = 0;
            return emit_trap_return_log(mcause, mepc, mtval, mstatus, next_pc, "tsbi_s");
        }
        if (runner_handle_test_sbi(tf, mepc, EXEC_MODE_M, &next_pc)) {
            g_lower_state.smode_trap_bridge_to_m = 0;
            return emit_trap_return_log(mcause, mepc, mtval, mstatus, next_pc, "test_sbi_s");
        }
    }

    if (mcause == MCAUSE_ECALL_U ||
        mcause == MCAUSE_ECALL_S ||
        mcause == MCAUSE_ECALL_M) {
        if (g_runner_exec.runner_active) capture_last_trap_in_mode(tf, mcause, mepc, mtval, mstatus, EXEC_MODE_M);
        if (tf) {
            uint64_t next_pc = 0;
            if (runner_handle_tsbi(tf, mepc, EXEC_MODE_M, &next_pc)) {
                return emit_trap_return_log(mcause, mepc, mtval, mstatus, next_pc, "tsbi");
            }
        }
        if (g_runner_exec.runner_active &&
            mcause == MCAUSE_ECALL_M &&
            g_lower_state.requested_exec_mode == EXEC_MODE_M) {
            if (g_runner_image.tohost_ptr) {
                g_runner_exec.test_tohost_value = *g_runner_image.tohost_ptr;
            }
            g_runner_exec.test_done = 1;
            return emit_trap_return_log(mcause, mepc, mtval, mstatus,
                                        g_runner_exec.test_resume_pc, "ecall_m_tohost");
        }
        if (tf) {
            uint64_t next_pc = 0;
            if (runner_handle_test_sbi(tf, mepc, EXEC_MODE_M, &next_pc)) {
                return emit_trap_return_log(mcause, mepc, mtval, mstatus, next_pc, "test_sbi");
            }
        }
        if (g_runner_exec.runner_active &&
            (mcause == MCAUSE_ECALL_S || mcause == MCAUSE_ECALL_M) &&
            handle_test_ecall(tf, mepc)) {
            return emit_trap_return_log(mcause, mepc, mtval, mstatus, mepc + 4ULL, "test_ecall");
        }
        g_runner_exec.test_done = 1;
        return emit_trap_return_log(mcause, mepc, mtval, mstatus,
                                    g_runner_exec.test_resume_pc, "ecall_done");
    }

    if (g_runner_exec.runner_active &&
        (mcause == MCAUSE_INST_MISALIGNED ||
         mcause == MCAUSE_LOAD_MISALIGNED ||
         mcause == MCAUSE_STORE_MISALIGNED)) {
        if (!g_runner_exec.fault_reported) {
            emit_trap_entry_log(mcause, mepc, mtval, mstatus);
            g_runner_exec.fault_reported = 1;
        }
        handle_fatal_test_trap(tf, mcause, mepc, mtval, mstatus);
        return emit_trap_return_log(mcause, mepc, mtval, mstatus,
                                    g_runner_exec.test_resume_pc, "fatal_misaligned");
    }

    if (g_runner_exec.runner_active && ((mcause & MCAUSE_INTERRUPT_BIT) == 0)) {
        if (!g_runner_exec.fault_reported) {
            emit_trap_entry_log(mcause, mepc, mtval, mstatus);
            g_runner_exec.fault_reported = 1;
        }
        handle_fatal_test_trap(tf, mcause, mepc, mtval, mstatus);
        return emit_trap_return_log(mcause, mepc, mtval, mstatus,
                                    g_runner_exec.test_resume_pc, "fatal_sync_exception");
    }

    uart_puts("\n[TRAP] mcause=");
    uart_put_hex(mcause);
    uart_puts(" mepc=");
    uart_put_hex(mepc);
    uart_puts(" mtval=");
    uart_put_hex(mtval);
    uart_puts(" mstatus=");
    uart_put_hex(mstatus);
    uart_puts("\n");
    if (is_valid_ddr_addr(mepc)) dump_words32(mepc);

    while (1) { wfi(); }
}

__attribute__((naked)) void trap_entry(void)
{
    __asm__ volatile(
        "csrrw sp, mscratch, sp\n"
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
        "csrr t0, mscratch\n"
        "sd t0, 240(sp)\n"
        "la t0, g_runner_exec\n"
        "ld gp, %c0(t0)\n"
        "csrr a0, mcause\n"
        "csrr a1, mepc\n"
        "csrr a2, mtval\n"
        "csrr a3, mstatus\n"
        "mv a4, sp\n"
        "call trap_c\n"
        "csrw mepc, a0\n"
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
        "csrrw sp, mscratch, sp\n"
        "mret\n"
        :
        : "i"(offsetof(RunnerExecState, runner_saved_gp))
    );
}
