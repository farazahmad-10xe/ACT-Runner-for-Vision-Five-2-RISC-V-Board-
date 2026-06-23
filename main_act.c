// main_act.c - Multi-test M-mode firmware for VisionFive2 (JH7110), RV64
// Supports either one embedded ACT ELF or a packed list of many ELFs.

#include "runner_shared.h"

RunnerImageState g_runner_image = {0};
RunnerExecState g_runner_exec = {0};
LowerModeState g_lower_state = {
    .requested_exec_mode = EXEC_MODE_M,
    .active_exec_mode = EXEC_MODE_M,
};
RunnerSbiState g_runner_sbi = {0};
MachineEnvConfig g_machine_env_config;
#if RUNNER_ENABLE_LOWER_MODES
uint64_t g_sv39_root_page_table[512] __attribute__((aligned(4096)));
uint64_t g_sv39_l1_page_tables[4][512] __attribute__((aligned(4096)));
uint64_t g_sv39_l0_page_tables[SV39_L0_TABLE_COUNT][512] __attribute__((aligned(4096)));
#endif
uint8_t g_test_stack[TEST_STACK_BYTES] __attribute__((aligned(4096)));
#if RUNNER_ENABLE_LOWER_MODES
uint8_t g_smode_runtime_stack[SMODE_RUNTIME_STACK_BYTES] __attribute__((aligned(4096)));
#endif
uint8_t g_trap_stack[TRAP_STACK_BYTES] __attribute__((aligned(4096)));
#if RUNNER_ENABLE_LOWER_MODES
uint8_t g_strap_stack[STRAP_STACK_BYTES] __attribute__((aligned(4096)));
#endif
LastTrapState g_last_trap = {0};

void main(void)
{
    uint64_t hart = read_csr_mhartid();
    uint64_t total = 0;
    uint64_t pass = 0;
    uint64_t fail = 0;
#if RUNNER_SD_ENABLE
    int load_ext_rc = -1;
#endif

    if (hart == MONITOR_HART_ID) {
        while (g_boot_sync == 0) { cpu_relax(); }
        monitor_hart_loop();
    }
    if (hart != RUNNER_HART_ID) {
        while (1) { wfi(); }
    }

    write_csr_mtvec((uint64_t)(uintptr_t)trap_entry);
    write_csr_mscratch((uint64_t)(uintptr_t)(g_trap_stack + sizeof(g_trap_stack)));
    *msip_ptr(RUNNER_HART_ID) = 0;
    g_lower_state.requested_exec_mode = select_default_exec_mode();
    g_lower_state.active_exec_mode = EXEC_MODE_M;
    platform_prepare_exec_env(g_lower_state.requested_exec_mode);

    uart_puts("\n=============================\n");
    uart_puts("M-mode (VisionFive2) payload runner\n");
    uart_puts("hartid="); uart_put_hex(hart); uart_puts("\n");
    uart_puts("payload_kind="); uart_puts(payload_kind_name()); uart_puts("\n");
    uart_puts("exec_mode="); uart_puts(exec_mode_name(g_lower_state.requested_exec_mode)); uart_puts("\n");
    emit_machine_env_config(&g_machine_env_config);
    uart_puts("=============================\n");

#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_RIESCUE
    uart_puts("[SUITE] Riescue profile: pack or single embedded ELF\n");
#else
    uart_puts("[SUITE] ACT profile: pack or single embedded ELF\n");
#endif

#if RUNNER_SD_ENABLE
    for (int attempt = 1; attempt <= 3; attempt++) {
        load_ext_rc = load_pack_from_sd_tail();
        if (load_ext_rc == 0) {
            uart_puts("[SUITE] loaded external pack from SD tail\n");
            break;
        }
        uart_puts("[SUITE] external SD load attempt ");
        uart_put_dec_u64((uint64_t)attempt);
        uart_puts(" failed rc=");
        uart_put_hex((uint64_t)(int64_t)load_ext_rc);
        uart_puts("\n");
        if (attempt < 3) {
            for (volatile uint64_t i = 0; i < 10000000; i++) { }
        }
    }
#else
    uart_puts("[SUITE] SD transport disabled by board configuration\n");
#endif

    if (run_pack_external(&total, &pass, &fail) != 0) {
        if (run_pack_embedded(&total, &pass, &fail) != 0) {
#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_RIESCUE
            uart_puts("[SUITE] no valid external/embedded pack, fallback to single embedded Riescue ELF\n");
#else
            uart_puts("[SUITE] no valid external/embedded pack, fallback to single embedded ELF\n");
#endif
            if (run_single_embedded(&total, &pass, &fail) != 0) {
#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_RIESCUE
                uart_puts("[SUITE] ERROR: no runnable Riescue payload\n");
#else
                uart_puts("[SUITE] ERROR: no runnable test payload\n");
#endif
                while (1) { wfi(); }
            }
        }
    }

    uart_puts("[SUITE] SUMMARY total="); uart_put_dec_u64(total);
    uart_puts(" pass="); uart_put_dec_u64(pass);
    uart_puts(" fail="); uart_put_dec_u64(fail);
    uart_puts("\n");

    if (fail == 0) uart_puts("PASS\n");
    else uart_puts("FAIL\n");

    while (1) { wfi(); }
}
