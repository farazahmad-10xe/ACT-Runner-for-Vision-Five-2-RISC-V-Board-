// Shared minimal M-mode UART ELF runner for VF2/JH7110 and BPI-F3/K1.

#include "runner_shared.h"

RunnerImageState g_runner_image = {0};
RunnerExecState g_runner_exec = {0};
LowerModeState g_lower_state = {
    .requested_exec_mode = EXEC_MODE_M,
    .active_exec_mode = EXEC_MODE_M,
};
RunnerSbiState g_runner_sbi = {0};
MachineEnvConfig g_machine_env_config;
uint8_t g_test_stack[TEST_STACK_BYTES] __attribute__((aligned(4096)));
uint8_t g_trap_stack[TRAP_STACK_BYTES] __attribute__((aligned(4096)));
LastTrapState g_last_trap = {0};
FirstTrapState g_first_trap = {0};

void main(void)
{
    uint64_t hart = read_csr_mhartid();
    uint64_t total = 0;
    uint64_t pass = 0;
    uint64_t fail = 0;

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
    k1_cci_init_own_cluster();
    k1_wakeup_monitor_hart();
    platform_prepare_exec_env(EXEC_MODE_M);

    uart_puts("\n=============================\n");
    uart_puts("Minimal M-mode UART ELF runner\n");
    uart_puts("board="); uart_puts(RUNNER_PLATFORM_NAME); uart_puts("\n");
    uart_puts("runner_build="); uart_puts(RUNNER_BUILD_ID); uart_puts("\n");
    uart_puts("exec_mode=M\ntransport=uart_stream\n");
    emit_machine_env_config(&g_machine_env_config);
    uart_puts("=============================\n");

    if (run_uart_stream_once(&total, &pass, &fail) != 0) {
        uart_puts("[UART_STREAM] ERROR no runnable ELF\n");
        fail += 1;
    }

    uart_puts("[SUITE] SUMMARY total="); uart_put_dec_u64(total);
    uart_puts(" pass="); uart_put_dec_u64(pass);
    uart_puts(" fail="); uart_put_dec_u64(fail);
    uart_puts("\n");
    uart_puts(fail == 0 ? "PASS\n" : "FAIL\n");

    while (1) { wfi(); }
}
