#include "runner_shared.h"

#if !RUNNER_ENABLE_LOWER_MODES
void reset_lower_mode_state(void)
{
    g_lower_state.lower_entry_pc = 0;
    g_lower_state.lower_user_sp = 0;
    g_lower_state.smode_trap_bridge_to_m = 0;
    g_lower_state.uflow_marker = 0;
}
#endif

#if RUNNER_ENABLE_LOWER_MODES
static __attribute__((noreturn)) void enter_user_mode_from_supervisor(uint64_t entry,
                                                                      uintptr_t user_sp)
{
    const uint64_t sstatus_sie = (1ULL << 1);
    const uint64_t sstatus_spie = (1ULL << 5);
    const uint64_t sstatus_spp = (1ULL << 8);
    uint64_t sstatus = read_sstatus();

    sstatus &= ~(sstatus_spp | sstatus_sie);
    sstatus |= sstatus_spie;
    g_lower_state.uflow_marker = 3;

    write_sstatus(sstatus);
    write_csr_sepc(entry);
    g_lower_state.active_exec_mode = EXEC_MODE_U;
    
    asm volatile ("fence rw, rw" ::: "memory");
    __asm__ volatile(
        "mv sp, %0\n"
        "sret\n"
        :
        : "r"(user_sp)
        : "memory"
    );
    __builtin_unreachable();
}

static void stage_umode_context(uint64_t entry, uintptr_t user_sp)
{
    g_lower_state.lower_entry_pc = entry;
    g_lower_state.lower_user_sp = user_sp;
}

static __attribute__((used)) void u_supervisor_entry_c(void)
{
    g_lower_state.uflow_marker = 2;
    g_lower_state.active_exec_mode = EXEC_MODE_S;
    enter_user_mode_from_supervisor(g_lower_state.lower_entry_pc, (uintptr_t)g_lower_state.lower_user_sp);
}

__attribute__((naked)) static void u_supervisor_entry(void)
{
    __asm__ volatile(
        "la t0, g_lower_state\n"
        "li t1, 1\n"
        "sd t1, %c0(t0)\n"
        "tail u_supervisor_entry_c\n"
        :
        : "i"(offsetof(LowerModeState, uflow_marker))
        :
    );
}
#endif

void run_test_in_requested_mode(uint64_t entry, uintptr_t test_sp)
{
#if !RUNNER_ENABLE_LOWER_MODES
    (void)test_sp;
    __asm__ volatile ("mv sp, %0" :: "r"(test_sp) : "memory");
    g_lower_state.active_exec_mode = EXEC_MODE_M;
    ((void (*)(void))(uintptr_t)entry)();
    return;
#else
    if (g_lower_state.requested_exec_mode == EXEC_MODE_M) {
        __asm__ volatile ("mv sp, %0" :: "r"(test_sp) : "memory");
        g_lower_state.active_exec_mode = EXEC_MODE_M;
        ((void (*)(void))(uintptr_t)entry)();
        return;
    }

    if (g_lower_state.requested_exec_mode == EXEC_MODE_S) {
        enter_smode_from_m(entry, test_sp);
        return;
    }

    {
        uintptr_t smode_sp =
            (((uintptr_t)g_smode_runtime_stack + (uintptr_t)sizeof(g_smode_runtime_stack)) &
             ~(uintptr_t)0xFULL);
        stage_umode_context(entry, test_sp);
        enter_smode_from_m((uint64_t)(uintptr_t)u_supervisor_entry, smode_sp);
    }
#endif
}
