#include "runner_shared.h"

const char *exec_mode_name(uint64_t mode)
{
    return mode == EXEC_MODE_M ? "M" : "UNSUPPORTED";
}

const char *payload_kind_name(void)
{
    return "ACT";
}

const char *delegation_policy_name(uint64_t policy)
{
    return policy == DELEGATION_POLICY_NONE ? "none" : "unsupported";
}

const char *smode_csr_policy_name(uint64_t policy)
{
    (void)policy;
    return "disabled";
}

uint64_t select_default_exec_mode(void)
{
    return EXEC_MODE_M;
}

uint64_t select_default_delegation_policy(uint64_t mode)
{
    (void)mode;
    return DELEGATION_POLICY_NONE;
}

void build_machine_env_config(uint64_t mode, MachineEnvConfig *cfg)
{
    if (!cfg) return;
    memset_local(cfg, 0, sizeof(*cfg));
    cfg->mode = EXEC_MODE_M;
    cfg->delegation_policy = DELEGATION_POLICY_NONE;
    cfg->mie = MIE_MSIE | MIE_MTIE;
    (void)mode;
}

static void write_pmpaddr_slot(uint32_t slot, uint64_t value)
{
    switch (slot) {
        case 0: write_csr_pmpaddr0(value); break;
        case 1: write_csr_pmpaddr1(value); break;
        case 2: write_csr_pmpaddr2(value); break;
        case 3: write_csr_pmpaddr3(value); break;
        case 4: write_csr_pmpaddr4(value); break;
        case 5: write_csr_pmpaddr5(value); break;
        case 6: write_csr_pmpaddr6(value); break;
        case 7: write_csr_pmpaddr7(value); break;
        default: break;
    }
}

void apply_machine_env_config(const MachineEnvConfig *cfg)
{
    if (!cfg) return;
    write_csr_medeleg(0);
    write_csr_mideleg(0);
    write_mie(cfg->mie);
    write_csr_stvec(0);
    write_csr_sscratch(0);
    write_csr_sie(0);
    write_csr_mcounteren(0);
    write_csr_mcountinhibit(0);
    write_csr_scounteren(0);
    for (uint32_t i = 0; i < 8; ++i) write_pmpaddr_slot(i, 0);
    write_csr_pmpcfg0(0);
    write_csr_satp(0);
    sfence_vma_all();
}

void emit_machine_env_config(const MachineEnvConfig *cfg)
{
    if (!cfg) return;
    uart_puts("[ENV] exec_mode=M\n");
    uart_puts("[ENV] deleg_policy=none medeleg=0x0000000000000000\n");
    uart_puts("[ENV] mideleg=0x0000000000000000\n");
    uart_puts("[ENV] mie="); uart_put_hex(cfg->mie); uart_puts("\n");
    uart_puts("[ENV] satp=0x0000000000000000\n");
    uart_puts("[ENV] lower_modes=disabled\n");
}

void platform_prepare_exec_env(uint64_t mode)
{
    build_machine_env_config(mode, &g_machine_env_config);
    apply_machine_env_config(&g_machine_env_config);
}
