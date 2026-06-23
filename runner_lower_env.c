#include "runner_shared.h"

#ifndef RUNNER_DELEG_POLICY
#define RUNNER_DELEG_POLICY DELEGATION_POLICY_NONE
#endif

#ifndef RUNNER_TOUCH_MENVCFG
#define RUNNER_TOUCH_MENVCFG 0
#endif

#ifndef RUNNER_ASSUME_RVA23_PRIV_FEATURES
#define RUNNER_ASSUME_RVA23_PRIV_FEATURES 1
#endif

#ifndef RUNNER_USE_SV39
#define RUNNER_USE_SV39 1
#endif

#define SV39_PAGE_SIZE           4096ULL
#define SV39_L1_SPAN             (2ULL * 1024ULL * 1024ULL)
#define SV39_ROOT_SPAN           (1ULL << 30)
#define SV39_ROOT_SLOT_LIMIT     4ULL

#define SV39_PTE_V               (1ULL << 0)
#define SV39_PTE_R               (1ULL << 1)
#define SV39_PTE_W               (1ULL << 2)
#define SV39_PTE_X               (1ULL << 3)
#define SV39_PTE_U               (1ULL << 4)
#define SV39_PTE_G               (1ULL << 5)
#define SV39_PTE_A               (1ULL << 6)
#define SV39_PTE_D               (1ULL << 7)
#define SV39_PTE_N               (1ULL << 63)
#define SV39_PTE_PBMT_SHIFT      61ULL
#define SV39_PTE_PBMT_IO         (2ULL << SV39_PTE_PBMT_SHIFT)
#define SV39_PTE_LOW_FLAG_MASK   0x3ffULL
#define SV39_PTE_HIGH_FLAG_MASK  ((7ULL << 61) | SV39_PTE_N)
#define SV39_PTE_FLAG_MASK       (SV39_PTE_LOW_FLAG_MASK | SV39_PTE_HIGH_FLAG_MASK)

#define ENVCFG_STCE              (1ULL << 63)
#define ENVCFG_PBMTE             (1ULL << 62)
#define ENVCFG_ADUE              (1ULL << 61)
#define ENVCFG_PMM_PMLEN16       (3ULL << 32)

#define PMP_R                    (1ULL << 0)
#define PMP_W                    (1ULL << 1)
#define PMP_X                    (1ULL << 2)
#define PMP_A_TOR                (1ULL << 3)
#define PMP_RWX_TOR              (PMP_R | PMP_W | PMP_X | PMP_A_TOR)

typedef struct RunnerPmpRegion {
    uint64_t start;
    uint64_t end;
    uint8_t cfg;
} RunnerPmpRegion;

static uint32_t g_sv39_l0_page_table_count;

const char *exec_mode_name(uint64_t mode)
{
    switch (mode) {
        case EXEC_MODE_M: return "M";
        case EXEC_MODE_S: return "S";
        case EXEC_MODE_U: return "U";
        default: return "UNKNOWN";
    }
}

const char *payload_kind_name(void)
{
#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_RIESCUE
    return "RIESCUE";
#else
    return "ACT";
#endif
}

const char *delegation_policy_name(uint64_t policy)
{
    switch (policy) {
        case DELEGATION_POLICY_NONE: return "none";
        case DELEGATION_POLICY_S_BASE: return "s_base";
        default: return "unknown";
    }
}

const char *smode_csr_policy_name(uint64_t policy)
{
    switch (policy) {
        case RUNNER_SMODE_CSR_POLICY_STRICT: return "strict";
        case RUNNER_SMODE_CSR_POLICY_COMPAT: return "compat";
        default: return "unknown";
    }
}

#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_RIESCUE
static const char *riescue_counter_policy_name(uint64_t policy)
{
    switch (policy) {
        case RUNNER_RIESCUE_COUNTER_POLICY_NONE: return "none";
        case RUNNER_RIESCUE_COUNTER_POLICY_BASE: return "base";
        case RUNNER_RIESCUE_COUNTER_POLICY_ALL: return "all";
        default: return "unknown";
    }
}
#endif

uint64_t select_default_exec_mode(void)
{
#ifdef RUNNER_EXEC_MODE
    return (uint64_t)RUNNER_EXEC_MODE;
#else
    return EXEC_MODE_M;
#endif
}

uint64_t select_default_delegation_policy(uint64_t mode)
{
    if (mode == EXEC_MODE_M) return DELEGATION_POLICY_NONE;
    return (uint64_t)RUNNER_DELEG_POLICY;
}

static void apply_riescue_counter_policy(MachineEnvConfig *cfg)
{
#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_RIESCUE
    if (!cfg) return;

#if RUNNER_RIESCUE_COUNTER_POLICY == RUNNER_RIESCUE_COUNTER_POLICY_BASE
    cfg->mcounteren = COUNTEREN_BASE;
    cfg->scounteren = COUNTEREN_BASE;
    cfg->mcountinhibit = 0;
#elif RUNNER_RIESCUE_COUNTER_POLICY == RUNNER_RIESCUE_COUNTER_POLICY_ALL
    cfg->mcounteren = COUNTEREN_ALL;
    cfg->scounteren = COUNTEREN_ALL;
    cfg->mcountinhibit = 0;
#else
    (void)cfg;
#endif
#else
    (void)cfg;
#endif
}

static uint64_t align_down_u64(uint64_t value, uint64_t align)
{
    return value & ~(align - 1ULL);
}

static uint64_t align_up_u64(uint64_t value, uint64_t align)
{
    return (value + align - 1ULL) & ~(align - 1ULL);
}

static uint64_t sv39_leaf_pte(uint64_t pa, uint64_t flags)
{
    return ((pa >> 12) << 10) | flags;
}

static uint64_t sv39_next_table_pte(uint64_t next_pa)
{
    return ((next_pa >> 12) << 10) | SV39_PTE_V;
}

static __attribute__((unused)) uint64_t sv39_satp_for_root(uint64_t root_pa)
{
    return (SATP_MODE_SV39 << 60) | (root_pa >> 12);
}

static uint64_t runner_supervisor_leaf_flags(uint64_t rwx_flags, uint64_t extra_flags)
{
    uint64_t flags = SV39_PTE_V | SV39_PTE_G | SV39_PTE_A | extra_flags;
    if (rwx_flags & SV39_PTE_R) flags |= SV39_PTE_R;
    if (rwx_flags & SV39_PTE_W) flags |= SV39_PTE_W | SV39_PTE_D;
    if (rwx_flags & SV39_PTE_X) flags |= SV39_PTE_X;
    return flags;
}

static uint64_t runner_user_leaf_flags(uint64_t rwx_flags, uint64_t extra_flags)
{
    return runner_supervisor_leaf_flags(rwx_flags, extra_flags) | SV39_PTE_U;
}

static uint64_t pmpaddr_tor_boundary(uint64_t addr)
{
    return addr >> 2;
}

static uint64_t runner_region_start(void)
{
    return align_down_u64((uint64_t)(uintptr_t)__text_start, SV39_PAGE_SIZE);
}

static uint64_t runner_region_end(void)
{
    return align_up_u64((uint64_t)(uintptr_t)__stack_top, SV39_PAGE_SIZE);
}

static void sv39_reset_tables(void)
{
    memset_local(g_sv39_root_page_table, 0, sizeof(g_sv39_root_page_table));
    memset_local(g_sv39_l1_page_tables, 0, sizeof(g_sv39_l1_page_tables));
    memset_local(g_sv39_l0_page_tables, 0, sizeof(g_sv39_l0_page_tables));
    g_sv39_l0_page_table_count = 0;
}

static uint64_t *sv39_ensure_l0_table(uint64_t root_index, uint64_t l1_index)
{
    uint64_t l1_pte;

    if (root_index >= SV39_ROOT_SLOT_LIMIT || l1_index >= 512ULL) return 0;

    if ((g_sv39_root_page_table[root_index] & SV39_PTE_V) == 0) {
        g_sv39_root_page_table[root_index] =
            sv39_next_table_pte((uint64_t)(uintptr_t)g_sv39_l1_page_tables[root_index]);
    }

    l1_pte = g_sv39_l1_page_tables[root_index][l1_index];
    if (l1_pte & SV39_PTE_V) {
        uint64_t next_pa = ((l1_pte >> 10) << 12);
        for (uint32_t i = 0; i < g_sv39_l0_page_table_count; i++) {
            if ((uint64_t)(uintptr_t)g_sv39_l0_page_tables[i] == next_pa) {
                return g_sv39_l0_page_tables[i];
            }
        }
        return 0;
    }

    if (g_sv39_l0_page_table_count >= SV39_L0_TABLE_COUNT) return 0;

    {
        uint64_t *l0 = g_sv39_l0_page_tables[g_sv39_l0_page_table_count++];
        memset_local(l0, 0, SV39_PAGE_SIZE);
        g_sv39_l1_page_tables[root_index][l1_index] =
            sv39_next_table_pte((uint64_t)(uintptr_t)l0);
        return l0;
    }
}

static void sv39_map_identity_page(uint64_t addr, uint64_t flags)
{
    uint64_t root_index = (addr >> 30) & 0x1ffULL;
    uint64_t l1_index = (addr >> 21) & 0x1ffULL;
    uint64_t l0_index = (addr >> 12) & 0x1ffULL;
    uint64_t *l0 = sv39_ensure_l0_table(root_index, l1_index);

    if (!l0) return;

    {
        uint64_t combined_flags = flags;
        if (l0[l0_index] & SV39_PTE_V) {
            combined_flags |= (l0[l0_index] & SV39_PTE_FLAG_MASK);
        }
        l0[l0_index] = sv39_leaf_pte(addr, combined_flags);
    }
}

static void sv39_map_identity_region(uint64_t start, uint64_t end, uint64_t flags)
{
    uint64_t page = align_down_u64(start, SV39_PAGE_SIZE);
    uint64_t limit = align_up_u64(end, SV39_PAGE_SIZE);

    if (start == 0 || end == 0 || end <= start) return;

    while (page < limit) {
        sv39_map_identity_page(page, flags);
        page += SV39_PAGE_SIZE;
    }
}

static void map_runner_private_regions(uint64_t user_accessible)
{
    const uint64_t sup_rx = runner_supervisor_leaf_flags(SV39_PTE_R | SV39_PTE_X, 0);
    const uint64_t sup_r = runner_supervisor_leaf_flags(SV39_PTE_R, 0);
    const uint64_t sup_rw = runner_supervisor_leaf_flags(SV39_PTE_R | SV39_PTE_W, 0);
    const uint64_t user_rw = runner_user_leaf_flags(SV39_PTE_R | SV39_PTE_W, 0);

    sv39_map_identity_region((uint64_t)(uintptr_t)__text_start,
                             (uint64_t)(uintptr_t)__text_end,
                             sup_rx);
    sv39_map_identity_region((uint64_t)(uintptr_t)__rodata_start,
                             (uint64_t)(uintptr_t)__rodata_end,
                             sup_r);
    sv39_map_identity_region((uint64_t)(uintptr_t)__data_start,
                             (uint64_t)(uintptr_t)__data_end,
                             sup_rw);
    sv39_map_identity_region((uint64_t)(uintptr_t)__bss_end,
                             (uint64_t)(uintptr_t)__stack_top,
                             sup_rw);
    sv39_map_identity_region((uint64_t)(uintptr_t)__data_end,
                             (uint64_t)(uintptr_t)__bss_end,
                             sup_rw);

    if (user_accessible) {
        sv39_map_identity_region((uint64_t)(uintptr_t)g_test_stack,
                                 (uint64_t)(uintptr_t)g_test_stack + sizeof(g_test_stack),
                                 user_rw);
    }
}

static uint64_t segment_leaf_flags(uint32_t segment_flags, uint64_t user_accessible)
{
    uint64_t rwx = 0;

    if (segment_flags & PF_R) rwx |= SV39_PTE_R;
    if (segment_flags & PF_W) rwx |= SV39_PTE_W;
    if (segment_flags & PF_X) rwx |= SV39_PTE_X;
    if ((segment_flags & (PF_R | PF_W | PF_X)) == 0) rwx = SV39_PTE_R;

    if (user_accessible) return runner_user_leaf_flags(rwx, 0);
    return runner_supervisor_leaf_flags(rwx, 0);
}

static void map_loaded_payload(uint64_t user_accessible)
{
    for (uint32_t i = 0; i < g_runner_image.load_segment_count; i++) {
        const RunnerLoadSegment *seg = &g_runner_image.load_segments[i];
        sv39_map_identity_region(seg->start, seg->end,
                                 segment_leaf_flags(seg->flags, user_accessible));
    }
}

static __attribute__((unused)) void build_sv39_runner_map(uint64_t mode)
{
#if RUNNER_TOUCH_MENVCFG
    const uint64_t sup_uart =
        runner_supervisor_leaf_flags(SV39_PTE_R | SV39_PTE_W, SV39_PTE_PBMT_IO);
#else
    const uint64_t sup_uart =
        runner_supervisor_leaf_flags(SV39_PTE_R | SV39_PTE_W, 0);
#endif

    sv39_reset_tables();
    map_runner_private_regions(mode == EXEC_MODE_U);
    map_loaded_payload(mode == EXEC_MODE_U);
    sv39_map_identity_region(UART_BASE, UART_BASE + SV39_PAGE_SIZE, sup_uart);
}

static uint32_t add_pmp_tor_region(MachineEnvConfig *cfg, uint32_t slot,
                                   uint64_t start, uint64_t end, uint8_t region_cfg)
{
    if (!cfg || slot >= 8U || start >= end || end == 0) return slot;
    if ((slot & 1U) != 0U || (slot + 1U) >= 8U) return slot;

    cfg->pmpaddr[slot] = pmpaddr_tor_boundary(start);
    cfg->pmpaddr[slot + 1U] = pmpaddr_tor_boundary(end);
    cfg->pmpcfg0 |= ((uint64_t)region_cfg) << (8U * (slot + 1U));
    return slot + 2U;
}

static void build_pmp_layout(uint64_t mode, MachineEnvConfig *cfg)
{
    uint32_t slot = 0;
    uint64_t region_start;
    uint64_t region_end;

    if (!cfg) return;

    if (mode == EXEC_MODE_M) {
        memset_local(cfg->pmpaddr, 0, sizeof(cfg->pmpaddr));
        cfg->pmpcfg0 = 0;
        return;
    }

    region_start = runner_region_start();
    region_end = runner_region_end();
    slot = add_pmp_tor_region(cfg, slot, region_start, region_end, (uint8_t)PMP_RWX_TOR);

    if (g_runner_image.loaded_region_start != 0 &&
        g_runner_image.loaded_region_end > g_runner_image.loaded_region_start) {
        uint64_t payload_start = align_down_u64(g_runner_image.loaded_region_start, SV39_PAGE_SIZE);
        uint64_t payload_end = align_up_u64(g_runner_image.loaded_region_end, SV39_PAGE_SIZE);
        if (payload_start < region_start || payload_end > region_end) {
            slot = add_pmp_tor_region(cfg, slot, payload_start, payload_end, (uint8_t)PMP_RWX_TOR);
        }
    }

    (void)add_pmp_tor_region(cfg, slot,
                             align_down_u64(UART_BASE, SV39_PAGE_SIZE),
                             align_up_u64(UART_BASE + SV39_PAGE_SIZE, SV39_PAGE_SIZE),
                             (uint8_t)(PMP_R | PMP_W | PMP_A_TOR));
}

#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_RIESCUE
static void build_riescue_payload_pmp_layout(MachineEnvConfig *cfg)
{
    uint32_t slot = 0;

    if (!cfg) return;
    memset_local(cfg->pmpaddr, 0, sizeof(cfg->pmpaddr));
    cfg->pmpcfg0 = 0;

    slot = add_pmp_tor_region(cfg, slot,
                              align_down_u64(BOARD_RAM_BASE, SV39_PAGE_SIZE),
                              align_up_u64(BOARD_RAM_LIMIT, SV39_PAGE_SIZE),
                              (uint8_t)PMP_RWX_TOR);

    (void)add_pmp_tor_region(cfg, slot,
                             align_down_u64(UART_BASE, SV39_PAGE_SIZE),
                             align_up_u64(UART_BASE + SV39_PAGE_SIZE, SV39_PAGE_SIZE),
                             (uint8_t)(PMP_R | PMP_W | PMP_A_TOR));
}
#endif

void build_machine_env_config(uint64_t mode, MachineEnvConfig *cfg)
{
#if RUNNER_ENABLE_LOWER_MODES
    const uint64_t deleg_sync_base =
        (1ULL << MCAUSE_INST_MISALIGNED) |
        (1ULL << MCAUSE_INST_ACCESS_FAULT) |
        (1ULL << MCAUSE_ILLEGAL_INSN) |
        (1ULL << MCAUSE_BREAKPOINT) |
        (1ULL << MCAUSE_LOAD_MISALIGNED) |
        (1ULL << MCAUSE_LOAD_ACCESS_FAULT) |
        (1ULL << MCAUSE_STORE_MISALIGNED) |
        (1ULL << MCAUSE_STORE_ACCESS_FAULT) |
        (1ULL << MCAUSE_ECALL_U) |
        (1ULL << MCAUSE_INST_PAGE_FAULT) |
        (1ULL << MCAUSE_LOAD_PAGE_FAULT) |
        (1ULL << MCAUSE_STORE_PAGE_FAULT);
    const uint64_t deleg_int_base = 0;
#endif

    if (!cfg) return;
    memset_local(cfg, 0, sizeof(*cfg));
    cfg->mode = mode;
    cfg->delegation_policy = select_default_delegation_policy(mode);
    cfg->mie = MIE_MSIE | MIE_MTIE;

    if (mode == EXEC_MODE_M) {
        apply_riescue_counter_policy(cfg);
        build_pmp_layout(mode, cfg);
        return;
    }

#if !RUNNER_ENABLE_LOWER_MODES
    build_pmp_layout(mode, cfg);
    return;
#else
    cfg->mcounteren = ~0ULL;
    cfg->mcountinhibit = 0;
    cfg->scounteren = ~0ULL;
    apply_riescue_counter_policy(cfg);
#if RUNNER_USE_SV39
    build_sv39_runner_map(mode);
    cfg->satp = sv39_satp_for_root((uint64_t)(uintptr_t)g_sv39_root_page_table);
#else
    cfg->satp = 0;
#endif
    cfg->stvec = ((uint64_t)(uintptr_t)s_trap_entry) & ~3ULL;
    cfg->sscratch = (uint64_t)(uintptr_t)(g_strap_stack + sizeof(g_strap_stack));

#if RUNNER_ASSUME_RVA23_PRIV_FEATURES
    cfg->menvcfg = ENVCFG_STCE | ENVCFG_PBMTE | ENVCFG_ADUE | ENVCFG_PMM_PMLEN16;
    if (mode == EXEC_MODE_U) {
        cfg->senvcfg = ENVCFG_PMM_PMLEN16;
    }
#endif

    if (cfg->delegation_policy == DELEGATION_POLICY_S_BASE) {
        cfg->medeleg = deleg_sync_base;
        cfg->mideleg = deleg_int_base;
        cfg->sie = SIE_SSIE | SIE_STIE | SIE_SEIE | SIE_LCOFIE;
    }

    build_pmp_layout(mode, cfg);
#endif
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

    write_csr_medeleg(cfg->medeleg);
    write_csr_mideleg(cfg->mideleg);
    write_mie(cfg->mie);
    write_csr_stvec(cfg->stvec);
    write_csr_sscratch(cfg->sscratch);
    write_csr_sie(cfg->sie);
    write_csr_mcounteren(cfg->mcounteren);
    write_csr_mcountinhibit(cfg->mcountinhibit);
    write_csr_scounteren(cfg->scounteren);
#if RUNNER_TOUCH_MENVCFG
    write_csr_menvcfg(cfg->menvcfg);
    write_csr_senvcfg(cfg->senvcfg);
#endif
    for (uint32_t i = 0; i < 8U; i++) {
        write_pmpaddr_slot(i, cfg->pmpaddr[i]);
    }
    write_csr_pmpcfg0(cfg->pmpcfg0);
    write_csr_satp(cfg->satp);
    sfence_vma_all();
}

void emit_machine_env_config(const MachineEnvConfig *cfg)
{
    if (!cfg) return;

    uart_puts("[ENV] exec_mode="); uart_puts(exec_mode_name(cfg->mode)); uart_puts("\n");
    uart_puts("[ENV] deleg_policy="); uart_puts(delegation_policy_name(cfg->delegation_policy));
    uart_puts(" medeleg="); uart_put_hex(cfg->medeleg); uart_puts("\n");
    uart_puts("[ENV] mideleg="); uart_put_hex(cfg->mideleg); uart_puts("\n");
    uart_puts("[ENV] mie="); uart_put_hex(cfg->mie); uart_puts("\n");
    uart_puts("[ENV] mcounteren="); uart_put_hex(cfg->mcounteren); uart_puts("\n");
    uart_puts("[ENV] mcountinhibit="); uart_put_hex(cfg->mcountinhibit); uart_puts("\n");
    uart_puts("[ENV] menvcfg="); uart_put_hex(cfg->menvcfg); uart_puts("\n");
    uart_puts("[ENV] scounteren="); uart_put_hex(cfg->scounteren); uart_puts("\n");
    uart_puts("[ENV] senvcfg="); uart_put_hex(cfg->senvcfg); uart_puts("\n");
    uart_puts("[ENV] satp="); uart_put_hex(cfg->satp); uart_puts("\n");
    uart_puts("[ENV] stvec="); uart_put_hex(cfg->stvec); uart_puts("\n");
    uart_puts("[ENV] sscratch="); uart_put_hex(cfg->sscratch); uart_puts("\n");
    uart_puts("[ENV] sie="); uart_put_hex(cfg->sie); uart_puts("\n");
    uart_puts("[ENV] pmpcfg0="); uart_put_hex(cfg->pmpcfg0); uart_puts("\n");
    for (uint32_t i = 0; i < 8U; i++) {
        uart_puts("[ENV] pmpaddr");
        uart_put_dec_u64(i);
        uart_puts("=");
        uart_put_hex(cfg->pmpaddr[i]);
        uart_puts("\n");
    }
    uart_puts("[ENV] touch_menvcfg=");
#if RUNNER_TOUCH_MENVCFG
    uart_puts("1");
#else
    uart_puts("0");
#endif
    uart_puts("\n");
    uart_puts("[ENV] use_sv39=");
#if RUNNER_USE_SV39
    uart_puts("1");
#else
    uart_puts("0");
#endif
    uart_puts("\n");
    uart_puts("[ENV] smode_csr_policy=");
    uart_puts(smode_csr_policy_name((uint64_t)RUNNER_SMODE_CSR_POLICY));
    uart_puts("\n");
#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_RIESCUE
    uart_puts("[ENV] riescue_counter_policy=");
    uart_puts(riescue_counter_policy_name((uint64_t)RUNNER_RIESCUE_COUNTER_POLICY));
    uart_puts("\n");
#endif
}

void platform_prepare_exec_env(uint64_t mode)
{
    build_machine_env_config(mode, &g_machine_env_config);
    apply_machine_env_config(&g_machine_env_config);
}

void platform_prepare_riescue_payload_env(void)
{
#if RUNNER_PAYLOAD_KIND == PAYLOAD_KIND_RIESCUE
    build_riescue_payload_pmp_layout(&g_machine_env_config);
    apply_machine_env_config(&g_machine_env_config);
    uart_puts("[ENV] riescue_payload_pmp=1 pmpcfg0=");
    uart_put_hex(g_machine_env_config.pmpcfg0);
    uart_puts(" pmpcfg0_rd=");
    uart_put_hex(read_csr_pmpcfg0());
    uart_puts(" pmpaddr0_rd=");
    uart_put_hex(read_csr_pmpaddr0());
    uart_puts(" pmpaddr1_rd=");
    uart_put_hex(read_csr_pmpaddr1());
    uart_puts(" pmpaddr2_rd=");
    uart_put_hex(read_csr_pmpaddr2());
    uart_puts(" pmpaddr3_rd=");
    uart_put_hex(read_csr_pmpaddr3());
    uart_puts(" payload_start=");
    uart_put_hex(g_runner_image.loaded_region_start);
    uart_puts(" payload_end=");
    uart_put_hex(g_runner_image.loaded_region_end);
    uart_puts("\n");
#endif
}
