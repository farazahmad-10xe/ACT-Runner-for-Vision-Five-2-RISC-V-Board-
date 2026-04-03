// main_act.c - Multi-test M-mode firmware for VisionFive2 (JH7110), RV64
// Supports either one embedded ACT ELF or a packed list of many ELFs.

#include "runner_shared.h"

volatile uint64_t* g_tohost_ptr = 0;
volatile uint64_t g_tohost_addr = 0;
volatile uint64_t g_sig_begin = 0;
volatile uint64_t g_sig_end = 0;
volatile uint64_t g_fail_begin = 0;
volatile uint64_t g_fail_end = 0;

volatile uint64_t g_runner_active = 0;
volatile uint64_t g_monitor_seen_tohost = 0;
volatile uint64_t g_test_done = 0;
volatile uint64_t g_test_tohost_value = 0;
volatile uint64_t g_test_resume_pc = 0;
volatile uint64_t g_runner_saved_sp = 0;
volatile uint64_t g_runner_saved_gp = 0;
volatile uint64_t g_reset_armed = 0;
volatile uint64_t g_test_deadline_mtime = 0;
volatile uint64_t g_reset_request_mtime = 0;
volatile uint64_t g_sig_dump_in_progress = 0;
volatile uint64_t g_case_report_ready = 0;
volatile uint64_t g_monitor_report_done = 0;
volatile uint64_t g_fault_reported = 0;
volatile const char *g_active_case_name = 0;
uint8_t g_test_stack[TEST_STACK_BYTES] __attribute__((aligned(16)));
uint8_t g_trap_stack[TRAP_STACK_BYTES] __attribute__((aligned(16)));
const uint8_t *g_loaded_blob = 0;
size_t g_loaded_blob_size = 0;

TrapFrame g_last_trap_frame;
uint64_t g_last_trap_mcause = 0;
uint64_t g_last_trap_mepc = 0;
uint64_t g_last_trap_mtval = 0;
uint64_t g_last_trap_mstatus = 0;
uint8_t g_last_trap_valid = 0;

void main(void)
{
    uint64_t hart = read_csr_mhartid();
    uint64_t total = 0;
    uint64_t pass = 0;
    uint64_t fail = 0;
    int load_ext_rc = -1;

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

    uart_puts("\n=============================\n");
    uart_puts("M-mode (VisionFive2) + ACT SUITE\n");
    uart_puts("hartid="); uart_put_hex(hart); uart_puts("\n");
    uart_puts("=============================\n");

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
    }

    if (run_pack_external(&total, &pass, &fail) != 0) {
        if (run_pack_embedded(&total, &pass, &fail) != 0) {
            uart_puts("[SUITE] no valid external/embedded pack, fallback to single embedded ELF\n");
            if (run_single_embedded(&total, &pass, &fail) != 0) {
                uart_puts("[SUITE] ERROR: no runnable test payload\n");
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
