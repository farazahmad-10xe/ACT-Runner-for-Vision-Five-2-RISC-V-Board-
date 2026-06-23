// Test execution flow and embedded-pack handling.

#include "runner_shared.h"

uint64_t run_loaded_entry(uint64_t entry)
{
    uint64_t sp_snapshot = 0;
    uint64_t gp_snapshot = 0;
    uintptr_t test_sp = 0;

    __asm__ volatile ("mv %0, sp" : "=r"(sp_snapshot));
    __asm__ volatile ("mv %0, gp" : "=r"(gp_snapshot));
    g_runner_exec.runner_saved_sp = sp_snapshot;
    g_runner_exec.runner_saved_gp = gp_snapshot;

    g_runner_exec.test_done = 0;
    g_runner_exec.monitor_seen_tohost = 0;
    g_runner_exec.test_tohost_value = 0;
    g_runner_exec.fault_reported = 0;
    g_runner_exec.reset_armed = 0;
    g_runner_exec.reset_request_mtime = 0;
    g_runner_exec.case_report_ready = 0;
    g_runner_exec.monitor_report_done = 0;
    g_runner_exec.test_deadline_mtime = *mtime_ptr() + TEST_TIMEOUT_TICKS;
    g_runner_exec.hart1_csr_snapshot_valid = 0;
    g_runner_exec.hart1_csr_snapshot_reason = 0;
    g_runner_exec.hart1_mhartid = 0;
    g_runner_exec.hart1_mstatus = 0;
    g_runner_exec.hart1_mepc = 0;
    g_runner_exec.hart1_mcause = 0;
    g_runner_exec.hart1_mtval = 0;
    g_runner_exec.hart1_medeleg = 0;
    g_runner_exec.hart1_mideleg = 0;
    g_runner_exec.hart1_satp = 0;
    g_runner_exec.hart1_pmpcfg0 = 0;
    for (uint32_t i = 0; i < 8; i++) {
        g_runner_exec.hart1_pmpaddr[i] = 0;
    }
    g_last_trap.valid = 0;
    g_last_trap.mcause = 0;
    g_last_trap.mepc = 0;
    g_last_trap.mtval = 0;
    g_last_trap.mstatus = 0;
    g_last_trap.mode = EXEC_MODE_M;
    g_lower_state.active_exec_mode = EXEC_MODE_M;
    reset_lower_mode_state();
    runner_reset_sbi_state();
    memset_local(&g_last_trap.frame, 0, sizeof(g_last_trap.frame));
#if RUNNER_VERBOSE_FLOW
    uart_log_lock();
    uart_puts("[FLOW] isolated_env_prepare from_mode=");
    uart_puts(exec_mode_name(EXEC_MODE_M));
    uart_puts(" target_mode=");
    uart_puts(exec_mode_name(g_lower_state.requested_exec_mode));
    uart_puts("\n");
    uart_log_unlock();
#endif
    platform_prepare_exec_env(g_lower_state.requested_exec_mode);
#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_RIESCUE
    platform_prepare_riescue_payload_env();
#endif
#if RUNNER_VERBOSE_FLOW
    uart_log_lock();
    emit_machine_env_config(&g_machine_env_config);
    uart_log_unlock();
#endif
#if RUNNER_FORCE_MEDELEG_CAUSE1
    {
        uint64_t medeleg_before = read_csr_medeleg();
        uint64_t medeleg_forced = medeleg_before | (1ULL << MCAUSE_INST_ACCESS_FAULT);
        uint64_t medeleg_after;
        uint64_t medeleg_mask;

        write_csr_medeleg(~0ULL);
        medeleg_mask = read_csr_medeleg();
        write_csr_medeleg(medeleg_forced);
        medeleg_after = read_csr_medeleg();

        uart_log_lock();
        uart_puts("[DBG] force_medeleg_cause1 before=");
        uart_put_hex(medeleg_before);
        uart_puts(" mask_allones=");
        uart_put_hex(medeleg_mask);
        uart_puts(" wanted=");
        uart_put_hex(medeleg_forced);
        uart_puts(" after=");
        uart_put_hex(medeleg_after);
        uart_puts(" bit1=");
        uart_put_hex((medeleg_after >> MCAUSE_INST_ACCESS_FAULT) & 1ULL);
        uart_puts("\n");
        uart_log_unlock();
    }
#endif

    monitor_irq_enable();
    g_runner_exec.runner_active = 1;
    asm volatile ("fence rw, rw" ::: "memory");
    {
        uint64_t deadline = *mtime_ptr() + TEST_TIMEOUT_TICKS;
        *mtimecmp_ptr(0) = deadline;
        *mtimecmp_ptr(1) = deadline;
        *mtimecmp_ptr(2) = deadline;
    }
    asm volatile ("fence rw, rw" ::: "memory");

    if (g_runner_image.tohost_ptr) *g_runner_image.tohost_ptr = 0;

    g_runner_exec.test_resume_pc = (uint64_t)(uintptr_t)&&resume_from_test;
    test_sp = (((uintptr_t)g_test_stack + (uintptr_t)sizeof(g_test_stack)) & ~(uintptr_t)0xFULL);
#if RUNNER_VERBOSE_FLOW
    if (g_lower_state.requested_exec_mode == EXEC_MODE_U) {
        uart_log_lock();
        uart_puts("[UFLOW] run_loaded_entry entry=");
        uart_put_hex(entry);
        uart_puts(" test_sp=");
        uart_put_hex((uint64_t)test_sp);
        uart_puts(" tohost=");
        uart_put_hex(g_runner_image.tohost_addr);
        uart_puts("\n");
        uart_log_unlock();
    }
    uart_log_lock();
    uart_puts("[FLOW] isolated_env_enter\n");
    uart_puts("       from_mode=");
    uart_puts(exec_mode_name(EXEC_MODE_M));
    uart_puts("\n");
    uart_puts("       target_mode=");
    uart_puts(exec_mode_name(g_lower_state.requested_exec_mode));
    uart_puts("\n");
    uart_puts("       entry=");
    uart_put_hex(entry);
    uart_puts("\n");
    uart_puts("       stack=");
    uart_put_hex((uint64_t)test_sp);
    uart_puts("\n");
    uart_log_unlock();
#endif
    run_test_in_requested_mode(entry, test_sp);

resume_from_test:
    __asm__ volatile (
        "la t0, g_runner_exec\n"
        "ld sp, %c0(t0)\n"
        "ld gp, %c1(t0)\n"
        :
        : "i"(offsetof(RunnerExecState, runner_saved_sp)),
          "i"(offsetof(RunnerExecState, runner_saved_gp))
        : "t0", "memory"
    );
    *mtimecmp_ptr(0) = ~0ULL;
    *mtimecmp_ptr(1) = ~0ULL;
    *mtimecmp_ptr(2) = ~0ULL;
    asm volatile ("fence rw, rw" ::: "memory");
    g_runner_exec.runner_active = 0;
    g_lower_state.active_exec_mode = EXEC_MODE_M;
    g_runner_exec.test_deadline_mtime = 0;
    monitor_irq_disable();
#if RUNNER_VERBOSE_FLOW
    uart_log_lock();
    uart_puts("[FLOW] isolated_env_exit from_mode=");
    if (g_last_trap.valid) uart_puts(exec_mode_name(g_last_trap.mode));
    else uart_puts(exec_mode_name(g_lower_state.requested_exec_mode));
    uart_puts(" to_mode=");
    uart_puts(exec_mode_name(EXEC_MODE_M));
    uart_puts(" tohost=");
    uart_put_hex(g_runner_exec.test_tohost_value);
    uart_puts(" trap_valid=");
    uart_puts(g_last_trap.valid ? "1" : "0");
    uart_puts("\n");
    uart_log_unlock();
#endif

    if (g_runner_exec.test_tohost_value != 0) return g_runner_exec.test_tohost_value;
    if (g_runner_image.tohost_ptr) return *g_runner_image.tohost_ptr;
    return 0;
}

int run_one_blob(const char *name, const uint8_t *blob, size_t blob_size, TestResult *out)
{
    uint64_t entry = 0;
    int rc = load_elf_blob(blob, blob_size, &entry);

    out->name = name;
    out->rc = rc;
    out->tohost = 0;
    out->tohost_addr = 0;
    out->sig_begin = 0;
    out->sig_end = 0;
    out->status = CASE_STATUS_ERROR;

    uart_puts("[CASE] START name="); uart_puts(name); uart_puts("\n");

    if (rc != 0) {
        uart_puts("[CASE] LOAD_FAIL name="); uart_puts(name);
        uart_puts(" rc="); uart_put_hex((uint64_t)rc); uart_puts("\n");
        emit_case_report(out);
        return rc;
    }

    out->tohost_addr = g_runner_image.tohost_addr;
    out->sig_begin = g_runner_image.sig_begin;
    out->sig_end = g_runner_image.sig_end;
    enable_fpu_set_fs_only();
    enable_fpu_try_clear_fcsr();

    dbg_puts("[PAYLOAD] kind="); dbg_puts(payload_kind_name());
    dbg_puts(" entry=0x"); dbg_hex_u64(entry);
    dbg_puts(" tohost=0x"); dbg_hex_u64(g_runner_image.tohost_addr);
    dbg_puts(" load_segments=0x"); dbg_hex_u64(g_runner_image.load_segment_count);
    dbg_nl();
#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_ACT
    dbg_puts("[ACT] sig_begin=0x"); dbg_hex_u64(g_runner_image.sig_begin); dbg_nl();
    dbg_puts("[ACT] sig_end=0x"); dbg_hex_u64(g_runner_image.sig_end); dbg_nl();
#endif

    g_runner_exec.active_case_name = name;
    out->tohost = run_loaded_entry(entry);
    g_runner_exec.active_case_name = 0;

    if (out->tohost != 0 && out->tohost != TOHOST_TIMEOUT && !g_runner_exec.monitor_report_done) {
        (void)wait_for_monitor_report();
    }

    if (g_runner_exec.monitor_report_done) {
        g_runner_exec.reset_request_mtime = 0;
        asm volatile ("fence rw, rw" ::: "memory");
        if (out->tohost == TOHOST_TIMEOUT) out->status = CASE_STATUS_TIMEOUT;
        else if (out->tohost == 1) out->status = CASE_STATUS_PASS;
        else out->status = CASE_STATUS_FAIL;
        return 0;
    }

    if (out->tohost == TOHOST_TIMEOUT) {
        out->status = CASE_STATUS_TIMEOUT;
        uart_puts("[CASE] RESULT name="); uart_puts(name);
        uart_puts(" status=TIMEOUT tohost="); uart_put_hex(out->tohost); uart_puts("\n");
    } else if (out->tohost == 1) {
        out->status = CASE_STATUS_PASS;
        uart_puts("[CASE] RESULT name="); uart_puts(name);
        uart_puts(" status=PASS tohost="); uart_put_hex(out->tohost); uart_puts("\n");
    } else {
        out->status = CASE_STATUS_FAIL;
        uart_puts("[CASE] RESULT name="); uart_puts(name);
        uart_puts(" status=FAIL tohost="); uart_put_hex(out->tohost); uart_puts("\n");
    }

    emit_case_report(out);
    if (out->status == CASE_STATUS_PASS) emit_execution_context("PASS");
    else if (out->status == CASE_STATUS_TIMEOUT) emit_execution_context("TIMEOUT");
    else emit_execution_context("FAIL");
    if (out->status != CASE_STATUS_PASS) {
#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_ACT
        g_runner_exec.sig_dump_in_progress = 1;
        asm volatile ("fence rw, rw" ::: "memory");
        dump_failure_scratch_region(g_runner_image.fail_begin, g_runner_image.fail_end);
        dump_act_failure_context();
        dump_signature_region(g_runner_image.sig_begin, g_runner_image.sig_end);
        asm volatile ("fence rw, rw" ::: "memory");
        g_runner_exec.sig_dump_in_progress = 0;
#endif
    }
    g_runner_exec.case_report_ready = 1;
    return 0;
}

int run_single_embedded(uint64_t *total, uint64_t *pass, uint64_t *fail)
{
    size_t blob_size = (size_t)(_act_elf_end - _act_elf_start);
    TestResult tr;
    if (blob_size < sizeof(Elf64_Ehdr)) return -1;

    (void)run_one_blob("embedded", _act_elf_start, blob_size, &tr);
    *total += 1;
    if (case_is_pass(&tr)) *pass += 1;
    else *fail += 1;

    return 0;
}

int run_pack_embedded(uint64_t *total, uint64_t *pass, uint64_t *fail)
{
    const uint8_t *pack = _act_pack_start;
    size_t pack_size = (size_t)(_act_pack_end - _act_pack_start);
    if (pack_size < sizeof(ActPackHeader)) return -1;

    {
        const ActPackHeader *hdr = (const ActPackHeader *)(const void *)pack;
        if (hdr->magic != PACK_MAGIC || hdr->version != PACK_VERSION) return -2;
        if (hdr->count == 0 || hdr->count > MAX_PACK_TESTS) return -3;

        {
            size_t entries_size = (size_t)hdr->count * sizeof(ActPackEntry);
            size_t table_end = sizeof(ActPackHeader) + entries_size;
            if (table_end > pack_size) return -4;
        }

        {
            const ActPackEntry *entries = (const ActPackEntry *)(const void *)(pack + sizeof(ActPackHeader));
            uart_puts("[SUITE] detected packed tests count=");
            uart_put_dec_u64(hdr->count);
            uart_puts("\n");

            for (uint32_t i = 0; i < hdr->count; i++) {
                const ActPackEntry *e = &entries[i];
                const char *name;
                TestResult tr;

                if (e->offset > pack_size || e->size > pack_size || (e->offset + e->size) > pack_size || (e->offset + e->size) < e->offset) {
                    uart_puts("[CASE] RESULT name=");
                    uart_puts(e->name[0] ? e->name : "unnamed");
                    uart_puts(" status=ERROR reason=bad_pack_range\n");
                    *total += 1;
                    *fail += 1;
                    continue;
                }

                name = e->name[0] ? e->name : "unnamed";
                (void)run_one_blob(name, pack + e->offset, (size_t)e->size, &tr);

                *total += 1;
                if (case_is_pass(&tr)) *pass += 1;
                else *fail += 1;
            }
        }
    }

    return 0;
}
