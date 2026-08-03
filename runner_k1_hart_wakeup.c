// SpacemiT K1 secondary-hart wakeup (bpif3_k1 only, BOARD_K1_HART_WAKEUP_ENABLE).
//
// Every register address/bit here is cross-checked against real sources, never
// against AI-generated summaries of them:
//   - K1 User Manual (~/Documents/BPIF3/K1_User_Manual_en.pdf), section 9.9
//     "Power Management & Lower Power Mode Control" - documents
//     PMU_CORE_STATUS and PMU_CAP_COREX_WAKEUP_X in full.
//   - BPI-SINOVOIP/pi-opensbi, branch v1.3-k1 - specifically the REAL runtime
//     wakeup path used for SBI HSM hart_start (lib/utils/psci/spacemit/plat/
//     plat_pm.c:spacemit_pwr_domain_on(), plat/k1x/underly_implement.c:
//     spacemit_wakeup_cpu()/spacemit_core_enter_c2()), not just the cold-boot-
//     only wakeup_other_core() in platform/generic/spacemit/spacemit_k1.c.
//     spacemit_core_enter_c2()'s hardcoded per-hart bit list (0xd4282890,
//     bits 6/9/12/15/22/25/28/31 for harts 0-7) is a byte-for-byte match to
//     this file's own k1_core_c2_bit[] table, independently confirming it.
//
// Sequencing mirrors spacemit_pwr_domain_on() exactly:
//     if (spacemit_core_enter_c2(hart)) spacemit_wakeup_cpu(hart);
//     else                              sbi_ipi_raw_send(hart);
// with one deliberate deviation: real OpenSBI's IPI fallback is only safe
// because by the time Linux calls hart_start, the target hart is guaranteed
// to already be parked in OpenSBI's own cooperative software wait loop - not
// raw BootROM code. We have no such guarantee here, and an ordinary CLINT
// interrupt to a hart still executing BootROM is exactly the action already
// proven (earlier this session) to disturb it into a real BootROM panic path.
// So the non-C2 branch here does not send anything - it just reports the
// state and stops, safely, rather than copying a fallback whose safety
// precondition we can't verify.
//
// Also notably absent: any attempt to program PMU_CAP_COREX_IDLE_CFG_X for
// hart1 to force C2 entry. An earlier draft of this file tried exactly that
// (write CORE_IDLE/CORE_PWRDWN into hart1's own IDLE_CFG instance, then poll
// for C2), but there is no precedent for it anywhere in real OpenSBI - every
// call site that touches IDLE_CFG operates on the CURRENT hart's own
// register, never another hart's. Given the manual itself frames these as
// "Core X software" registers with no documented cross-hart write semantics,
// that was an unproven, unnecessary risk for what the C2 check + real
// wakeup/no-op branches below already settle safely.

#include "runner_shared.h"

#if RUNNER_K1_HART_WAKEUP_ENABLE

#define K1_PMU_BASE 0xD4282800u

// PMU_CORE_STATUS - manual 9.9.4.9.3 / spacemit_core_enter_c2(), offset
// PMU_BASE+0x90 (== 0xD4282890, the literal address hardcoded in OpenSBI).
#define K1_CORE_STATUS_ADDR (K1_PMU_BASE + 0x90u)

// PMU_CAP_COREX_WAKEUP_X - manual 9.9.4.9.12 / spacemit_wakeup_cpu(). Each of
// the 8 cores gets its own private instance of this register to write from
// ("other cores in the MP subsystem should not write to this register" -
// avoids write races), and every instance carries the full
// WAKEUP_CORE0..WAKEUP_CORE7 bit set so any core can wake any other by
// writing its own instance.
static const uint32_t k1_wakeup_reg_offset[8] = {
    0x12Cu, 0x130u, 0x134u, 0x138u,
    0x324u, 0x328u, 0x32Cu, 0x330u,
};

// AP_COREn_C2 bit position within PMU_CORE_STATUS - matches both manual
// 9.9.4.9.3 and spacemit_core_enter_c2()'s hardcoded per-hart checks. Not a
// linear function of core index: cluster1's cores sit 4 bits further up than
// naively extrapolating cluster0's 3-bit spacing, because of a 4-bit
// cluster-level M1/M2/idle-flag block at bits 16:19 in between.
static const uint32_t k1_core_c2_bit[8] = {
    6u, 9u, 12u, 15u, 22u, 25u, 28u, 31u,
};

#define K1_CPUS_PER_CLUSTER 4u

// Cluster reset-vector (RVBADDR) registers - undocumented in the manual,
// confirmed from pi-opensbi platform/generic/include/spacemit/k1x/k1x_evb.h.
// Real OpenSBI's wakeup_other_core() writes BOTH clusters' registers
// unconditionally at cold boot (it doesn't yet know which core will be woken
// later) - mirrored here rather than trusting our own hart-to-cluster
// arithmetic alone for a single write.
#define K1_C0_RVBADDR_LO_ADDR 0xD4282DB0u
#define K1_C0_RVBADDR_HI_ADDR 0xD4282DB4u
#define K1_C1_RVBADDR_LO_ADDR (0xD4282C00u + 0x2B0u)
#define K1_C1_RVBADDR_HI_ADDR (0xD4282C00u + 0x2B4u)

// CCI-500 snoop/DVM enable - undocumented in the manual, confirmed from
// pi-opensbi lib/utils/cci/bus-cci.c + k1x_evb.h (PLATFORM_CCI_ADDR, and
// cci_map[] which maps cluster index directly to CCI slave-interface index).
#define K1_CCI_BASE 0xD8500000u
// SLAVE_IFACE_OFFSET(index) in bus-cci.c is SLAVE_IFACE0_OFFSET(0x1000) +
// 0x1000*index - i.e. slave interface 0 sits at CCI_BASE+0x1000, not
// CCI_BASE+0. An earlier version of this file omitted the base offset
// (computed cluster*0x1000 directly, giving 0 for cluster0) - caught by
// re-deriving this from the macro expansion instead of trusting the
// per-cluster "step" framing.
#define K1_CCI_SLAVE_IFACE0_OFFSET 0x1000u
#define K1_CCI_SLAVE_IFACE_STEP 0x1000u
#define K1_CCI_SNOOP_CTRL_REG 0x0u
#define K1_CCI_STATUS_REG 0xCu
#define K1_CCI_DVM_EN_BIT (1u << 1)
#define K1_CCI_SNOOP_EN_BIT (1u << 0)
#define K1_CCI_CHANGE_PENDING_BIT (1u << 0)

#define K1_POLL_MAX_SPINS 2000000u

extern char _start[];

static inline uint32_t k1_read32(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline void k1_write32(uint32_t addr, uint32_t val)
{
    *(volatile uint32_t *)(uintptr_t)addr = val;
}

static int k1_core_in_c2(uint32_t hart)
{
    uint32_t status = k1_read32(K1_CORE_STATUS_ADDR);
    return (int)((status >> k1_core_c2_bit[hart]) & 1u);
}

// CCI snoop/DVM enable for the cluster the CALLING hart (always hart0, i.e.
// RUNNER_HART_ID) is already running in. Deliberately NOT part of
// k1_wakeup_monitor_hart(): real OpenSBI's actual runtime wakeup path
// (spacemit_pwr_domain_on -> spacemit_wakeup_cpu) never touches CCI at all -
// it's either done once, unconditionally, at cold boot for whichever cluster
// is already active (wakeup_other_core()), or by the newly-woken hart itself
// post-wake (spacemit_pwr_domain_on_finish(), via current_hartid()). Neither
// case ties it to a specific wakeup event the way this was first written.
// It's also not even the mechanism hart0/hart1 need for their own mutual
// coherency in the first place - that's CSR_ML2SETUP (intra-cluster, see
// start.S), since CCI-500 only covers cross-cluster/L2 coherency. Kept here
// (called once from hart0's own early init in main_act.c, independent of
// whether/when hart1 is ever woken) mainly for parity with real cold-boot
// behavior in case broader cluster0 coherency ever matters later.
void k1_cci_init_own_cluster(void)
{
    const uint32_t cluster = RUNNER_HART_ID / K1_CPUS_PER_CLUSTER;
    uint32_t snoop_addr = K1_CCI_BASE + K1_CCI_SLAVE_IFACE0_OFFSET + (cluster * K1_CCI_SLAVE_IFACE_STEP) + K1_CCI_SNOOP_CTRL_REG;
    uint32_t status_addr = K1_CCI_BASE + K1_CCI_STATUS_REG;
    uint32_t spins = 0;

    uart_log_lock();
    k1_write32(snoop_addr, K1_CCI_DVM_EN_BIT | K1_CCI_SNOOP_EN_BIT);
    asm volatile ("fence rw, rw" ::: "memory");
    while ((k1_read32(status_addr) & K1_CCI_CHANGE_PENDING_BIT) != 0 && spins < K1_POLL_MAX_SPINS) spins++;
    if ((k1_read32(status_addr) & K1_CCI_CHANGE_PENDING_BIT) != 0) {
        uart_puts("[K1CCI] snoop enable: giving up waiting for change_pending, spins=");
        uart_put_dec_u64(spins);
        uart_puts("\n");
    } else {
        uart_puts("[K1CCI] snoop enabled for cluster="); uart_put_dec_u64(cluster);
        uart_puts(" spins="); uart_put_dec_u64(spins); uart_puts("\n");
    }
    uart_log_unlock();
}

void k1_wakeup_monitor_hart(void)
{
    const uint32_t hart = MONITOR_HART_ID;
    const uint64_t reset_target = (uint64_t)(uintptr_t)_start;

    uart_log_lock();
    uart_puts("[K1WAKE] target hart="); uart_put_dec_u64(hart);
    uart_puts("\n");

    // RVBADDR is set unconditionally and first, before the C2 check even
    // matters - matches wakeup_other_core()'s own cold-boot ordering (reset
    // vectors prepared before it's known whether/when a wakeup will follow).
    k1_write32(K1_C0_RVBADDR_LO_ADDR, (uint32_t)(reset_target & 0xffffffffu));
    k1_write32(K1_C0_RVBADDR_HI_ADDR, (uint32_t)(reset_target >> 32));
    k1_write32(K1_C1_RVBADDR_LO_ADDR, (uint32_t)(reset_target & 0xffffffffu));
    k1_write32(K1_C1_RVBADDR_HI_ADDR, (uint32_t)(reset_target >> 32));
    asm volatile ("fence rw, rw" ::: "memory");

    {
        uint64_t c0 = ((uint64_t)k1_read32(K1_C0_RVBADDR_HI_ADDR) << 32) | k1_read32(K1_C0_RVBADDR_LO_ADDR);
        uint64_t c1 = ((uint64_t)k1_read32(K1_C1_RVBADDR_HI_ADDR) << 32) | k1_read32(K1_C1_RVBADDR_LO_ADDR);
        uart_puts("[K1WAKE] RVBADDR target="); uart_put_hex(reset_target);
        uart_puts(" C0_readback="); uart_put_hex(c0);
        uart_puts(" C1_readback="); uart_put_hex(c1);
        uart_puts("\n");
    }

    {
        uint32_t status = k1_read32(K1_CORE_STATUS_ADDR);
        int in_c2 = k1_core_in_c2(hart);
        uart_puts("[K1WAKE] PMU_CORE_STATUS="); uart_put_hex(status);
        uart_puts(" in_c2="); uart_put_dec_u64((uint64_t)in_c2);
        uart_puts("\n");

        if (!in_c2) {
            uart_puts("[K1WAKE] hart not in C2 - real OpenSBI's runtime wakeup\n");
            uart_puts("[K1WAKE] (spacemit_pwr_domain_on) falls back to a plain IPI here,\n");
            uart_puts("[K1WAKE] but that assumes the target is already parked in OpenSBI's\n");
            uart_puts("[K1WAKE] own cooperative wait loop - not raw BootROM code, which is\n");
            uart_puts("[K1WAKE] what we have. An IPI in that state is exactly what caused\n");
            uart_puts("[K1WAKE] the earlier ROM panic, so skipping the wakeup entirely.\n");
            uart_log_unlock();
            return;
        }
        uart_puts("[K1WAKE] hart already in C2, proceeding with wakeup\n");
    }

    {
        uint32_t wakeup_addr = K1_PMU_BASE + k1_wakeup_reg_offset[RUNNER_HART_ID];
        uart_puts("[K1WAKE] writing WAKEUP_CORE"); uart_put_dec_u64(hart);
        uart_puts(" via own instance at "); uart_put_hex(wakeup_addr);
        uart_puts("\n");
        k1_write32(wakeup_addr, 1u << hart);
        asm volatile ("fence rw, rw" ::: "memory");
    }

    uart_puts("[K1WAKE] wakeup sequence complete\n");
    uart_log_unlock();
}

#else

void k1_cci_init_own_cluster(void) { }
void k1_wakeup_monitor_hart(void) { }

#endif
