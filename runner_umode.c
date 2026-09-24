#include "runner_shared.h"

void reset_lower_mode_state(void)
{
    g_lower_state.requested_exec_mode = EXEC_MODE_M;
    g_lower_state.active_exec_mode = EXEC_MODE_M;
    g_lower_state.smode_trap_bridge_to_m = 0;
    g_lower_state.lower_entry_pc = 0;
    g_lower_state.lower_user_sp = 0;
    g_lower_state.uflow_marker = 0;
}

void run_test_in_requested_mode(uint64_t entry, uintptr_t test_sp)
{
    __asm__ volatile ("mv sp, %0" :: "r"(test_sp) : "memory");
    g_lower_state.active_exec_mode = EXEC_MODE_M;
    ((void (*)(void))(uintptr_t)entry)();
}
