// SD, trap, monitor, and test-runner logic extracted from main_act.c.

#include "runner_shared.h"

/* DW-MSHC register offsets */
#define DWMCI_CTRL      0x000
#define DWMCI_PWREN     0x004
#define DWMCI_CLKDIV    0x008
#define DWMCI_CLKSRC    0x00c
#define DWMCI_CLKENA    0x010
#define DWMCI_TMOUT     0x014
#define DWMCI_CTYPE     0x018
#define DWMCI_BLKSIZ    0x01c
#define DWMCI_BYTCNT    0x020
#define DWMCI_INTMASK   0x024
#define DWMCI_CMDARG    0x028
#define DWMCI_CMD       0x02c
#define DWMCI_RESP0     0x030
#define DWMCI_RESP1     0x034
#define DWMCI_RESP2     0x038
#define DWMCI_RESP3     0x03c
#define DWMCI_RINTSTS   0x044
#define DWMCI_STATUS    0x048
#define DWMCI_FIFOTH    0x04c
#define DWMCI_DATA      0x200

#define DWMCI_INTMSK_ALL    0xffffffffu
#define DWMCI_INTMSK_RE     (1u << 1)
#define DWMCI_INTMSK_CDONE  (1u << 2)
#define DWMCI_INTMSK_DTO    (1u << 3)
#define DWMCI_INTMSK_RXDR   (1u << 5)
#define DWMCI_INTMSK_RCRC   (1u << 6)
#define DWMCI_INTMSK_RTO    (1u << 8)
#define DWMCI_INTMSK_DRTO   (1u << 9)
#define DWMCI_INTMSK_HTO    (1u << 10)
#define DWMCI_INTMSK_FRUN   (1u << 11)
#define DWMCI_INTMSK_HLE    (1u << 12)
#define DWMCI_INTMSK_SBE    (1u << 13)
#define DWMCI_INTMSK_EBE    (1u << 15)

#define DWMCI_DATA_ERR      (DWMCI_INTMSK_EBE | DWMCI_INTMSK_SBE | DWMCI_INTMSK_HLE | DWMCI_INTMSK_FRUN)
#define DWMCI_DATA_TOUT     (DWMCI_INTMSK_HTO | DWMCI_INTMSK_DRTO)

#define DWMCI_CTRL_RESET        (1u << 0)
#define DWMCI_CTRL_FIFO_RESET   (1u << 1)
#define DWMCI_CTRL_DMA_RESET    (1u << 2)
#define DWMCI_RESET_ALL         (DWMCI_CTRL_RESET | DWMCI_CTRL_FIFO_RESET | DWMCI_CTRL_DMA_RESET)

#define DWMCI_CMD_RESP_EXP      (1u << 6)
#define DWMCI_CMD_RESP_LENGTH   (1u << 7)
#define DWMCI_CMD_CHECK_CRC     (1u << 8)
#define DWMCI_CMD_DATA_EXP      (1u << 9)
#define DWMCI_CMD_RW            (1u << 10)
#define DWMCI_CMD_PRV_DAT_WAIT  (1u << 13)
#define DWMCI_CMD_UPD_CLK       (1u << 21)
#define DWMCI_CMD_USE_HOLD_REG  (1u << 29)
#define DWMCI_CMD_START         (1u << 31)

#define DWMCI_CLKEN_ENABLE      (1u << 0)
#define DWMCI_CLKEN_LOW_PWR     (1u << 16)

#define DWMCI_CTYPE_1BIT        0u

#define DWMCI_FIFO_FULL         (1u << 3)
#define DWMCI_BUSY              (1u << 9)
#define DWMCI_FIFO_MASK         0x1fffu
#define DWMCI_FIFO_SHIFT        17

#define DWMCI_MSIZE(x)          ((uint32_t)(x) << 28)
#define DWMCI_RX_WMARK(x)       ((uint32_t)(x) << 16)
#define DWMCI_TX_WMARK(x)       ((uint32_t)(x))

#define MMC_CMD_GO_IDLE_STATE       0u
#define MMC_CMD_ALL_SEND_CID        2u
#define MMC_CMD_SEND_RELATIVE_ADDR  3u
#define MMC_CMD_SELECT_CARD         7u
#define MMC_CMD_SEND_CSD            9u
#define MMC_CMD_SET_BLOCKLEN        16u
#define MMC_CMD_READ_SINGLE_BLOCK   17u
#define MMC_CMD_APP_CMD             55u

#define SD_CMD_SEND_IF_COND         8u
#define SD_CMD_APP_SEND_OP_COND     41u

#define MMC_RSP_PRESENT (1u << 0)
#define MMC_RSP_136     (1u << 1)
#define MMC_RSP_CRC     (1u << 2)
#define MMC_RSP_OPCODE  (1u << 4)

#define MMC_RSP_NONE    0u
#define MMC_RSP_R1      (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)
#define MMC_RSP_R2      (MMC_RSP_PRESENT | MMC_RSP_136 | MMC_RSP_CRC)
#define MMC_RSP_R3      (MMC_RSP_PRESENT)
#define MMC_RSP_R6      (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)
#define MMC_RSP_R7      (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)

static uint32_t g_sd_rca = 0;
static uint8_t g_sd_high_capacity = 0;
static uint32_t g_sd_total_blocks = 0;
static uint32_t g_sd_tmp_block[SD_BLOCK_SIZE / sizeof(uint32_t)];
static uintptr_t g_sdio_base = SDIO1_BASE;
static uint32_t g_pack_footer_lba = 0;
static uint32_t g_ext_next_index = 0;
static uint32_t g_ext_inflight_index = 0xffffffffu;
static uint32_t g_ext_active_index = 0xffffffffu;
static uint32_t g_ext_progress_persisted = 0u;
static int g_ext_pack_loaded = 0;
static uint64_t g_ext_pack_start_lba = 0;
static uint32_t g_ext_pack_num_blocks = 0;
static uint32_t g_ext_pack_count = 0;

#define FOOTER_IDX_NONE      0xffffffffu
#define FOOTER_IDX16_MASK    0x0000ffffu
#define FOOTER_INFLIGHT_SHIFT 16u

static int sd_read_block_words(uint32_t lba, uint32_t *dst_words);
static int sd_write_block_words(uint32_t lba, const uint32_t *src_words);

static uint32_t footer_encode_progress(uint32_t next_index, uint32_t inflight_index_or_none)
{
    uint32_t next16 = next_index & FOOTER_IDX16_MASK;
    uint32_t inflight16 = 0u;
    if (inflight_index_or_none != FOOTER_IDX_NONE) {
        uint32_t ip1 = inflight_index_or_none + 1u;
        if (ip1 > FOOTER_IDX16_MASK) ip1 = FOOTER_IDX16_MASK;
        inflight16 = ip1 & FOOTER_IDX16_MASK;
    }
    return next16 | (inflight16 << FOOTER_INFLIGHT_SHIFT);
}

static void footer_decode_progress(uint32_t reserved_raw, uint32_t *next_index, uint32_t *inflight_index_or_none)
{
    uint32_t next16 = reserved_raw & FOOTER_IDX16_MASK;
    uint32_t inflight16 = (reserved_raw >> FOOTER_INFLIGHT_SHIFT) & FOOTER_IDX16_MASK;
    *next_index = next16;
    *inflight_index_or_none = (inflight16 == 0u) ? FOOTER_IDX_NONE : (inflight16 - 1u);
}

static int persist_footer_progress(uint32_t next_index, uint32_t inflight_index_or_none)
{
    if (g_pack_footer_lba == 0u) return -1;

    ActPackFooter *fw = (ActPackFooter *)(void *)g_sd_tmp_block;
    if (sd_read_block_words(g_pack_footer_lba, g_sd_tmp_block) != 0) return -2;
    if (fw->magic != PACK_FOOTER_MAGIC || fw->version != 1u) return -3;

    fw->reserved = footer_encode_progress(next_index, inflight_index_or_none);
    for (int t = 0; t < 3; t++) {
        int wrc = sd_write_block_words(g_pack_footer_lba, g_sd_tmp_block);
        if (wrc == 0) return 0;
    }
    return -4;
}

void emit_execution_context(const char *reason)
{
    SymbolInfo sym;
    int have_sym = 0;
    int have_insn = 0;
    uint32_t insn = 0;

    if (g_last_trap_valid) {
        have_sym = (find_symbol_for_addr(g_loaded_blob, g_loaded_blob_size, g_last_trap_mepc, &sym) == 0);
        insn = read_insn_word(g_last_trap_mepc, &have_insn);
    }

    uart_puts("[CTX] reason=");
    uart_puts(reason ? reason : "unknown");
    uart_puts(" trap_valid=");
    uart_puts(g_last_trap_valid ? "1" : "0");
    uart_puts(" exit_mcause="); uart_put_hex(g_last_trap_mcause);
    uart_puts(" exit_reason=");
    uart_puts(g_last_trap_valid ? trap_reason_name(g_last_trap_mcause) : "no_trap");
    uart_puts(" exit_mepc="); uart_put_hex(g_last_trap_mepc);
    uart_puts(" exit_insn=");
    uart_put_hex((uint64_t)insn);
    uart_puts(" exit_insn_valid=");
    uart_puts(have_insn ? "1" : "0");
    uart_puts(" exit_mtval="); uart_put_hex(g_last_trap_mtval);
    uart_puts(" exit_mstatus="); uart_put_hex(g_last_trap_mstatus);
    uart_puts(" exit_symbol=");
    if (have_sym) uart_put_token_str(sym.name);
    else uart_puts("unknown");
    uart_puts(" exit_symbol_addr=");
    if (have_sym) uart_put_hex(sym.value);
    else uart_put_hex(0);
    uart_puts("\n");

    if (!g_last_trap_valid) {
        uart_puts("[REG] unavailable=no_trap_frame\n");
        return;
    }

    emit_reg_line4("ra", g_last_trap_frame.ra, "sp", g_last_trap_frame.sp,
                   "gp", g_last_trap_frame.gp, "tp", g_last_trap_frame.tp);
    emit_reg_line4("t0", g_last_trap_frame.t0, "t1", g_last_trap_frame.t1,
                   "t2", g_last_trap_frame.t2, "t3", g_last_trap_frame.t3);
    emit_reg_line4("t4", g_last_trap_frame.t4, "t5", g_last_trap_frame.t5,
                   "t6", g_last_trap_frame.t6, "s0", g_last_trap_frame.s0);
    emit_reg_line4("s1", g_last_trap_frame.s1, "s2", g_last_trap_frame.s2,
                   "s3", g_last_trap_frame.s3, "s4", g_last_trap_frame.s4);
    emit_reg_line4("s5", g_last_trap_frame.s5, "s6", g_last_trap_frame.s6,
                   "s7", g_last_trap_frame.s7, "s8", g_last_trap_frame.s8);
    emit_reg_line4("s9", g_last_trap_frame.s9, "s10", g_last_trap_frame.s10,
                   "s11", g_last_trap_frame.s11, "a0", g_last_trap_frame.a0);
    emit_reg_line4("a1", g_last_trap_frame.a1, "a2", g_last_trap_frame.a2,
                   "a3", g_last_trap_frame.a3, "a4", g_last_trap_frame.a4);
    emit_reg_line3("a5", g_last_trap_frame.a5, "a6", g_last_trap_frame.a6,
                   "a7", g_last_trap_frame.a7);
    uart_puts("[CSR] mepc="); uart_put_hex(g_last_trap_mepc);
    uart_puts(" mcause="); uart_put_hex(g_last_trap_mcause);
    uart_puts(" mtval="); uart_put_hex(g_last_trap_mtval);
    uart_puts(" mstatus="); uart_put_hex(g_last_trap_mstatus);
    uart_puts("\n");

    if (is_valid_ddr_addr(g_last_trap_mepc)) dump_words32(g_last_trap_mepc);
}

uint64_t get_case_exit_pc(void)
{
    if (g_last_trap_valid) return g_last_trap_mepc;
    return 0;
}

void emit_trap_failure_report(uint64_t mcause, uint64_t mepc)
{
    uint64_t sig_bytes = 0;
    const char *name = (const char *)g_active_case_name;

    if (g_sig_end > g_sig_begin) sig_bytes = g_sig_end - g_sig_begin;

    uart_puts("[CASE] REPORT name=");
    uart_puts((name && name[0]) ? name : "unknown");
    uart_puts(" status=FAIL");
    uart_puts(" rc="); uart_put_hex((uint64_t)(int64_t)-1);
    uart_puts(" tohost_addr="); uart_put_hex(g_tohost_addr);
    uart_puts(" tohost_value="); uart_put_hex(TOHOST_TRAP);
    uart_puts(" sig_begin="); uart_put_hex(g_sig_begin);
    uart_puts(" sig_end="); uart_put_hex(g_sig_end);
    uart_puts(" sig_bytes="); uart_put_hex(sig_bytes);
    uart_puts(" trap_mcause="); uart_put_hex(mcause);
    uart_puts(" trap_mepc="); uart_put_hex(mepc);
    uart_puts(" exit_pc="); uart_put_hex(get_case_exit_pc());
    uart_puts("\n");

    uart_puts("RVCP-RESULT: Test File \"");
    uart_puts((name && name[0]) ? name : "unknown");
    uart_puts(".S\": FAILED\n");
    emit_execution_context("TRAP");

    g_sig_dump_in_progress = 1;
    asm volatile ("fence rw, rw" ::: "memory");
    dump_failure_scratch_region(g_fail_begin, g_fail_end);
    dump_signature_region(g_sig_begin, g_sig_end);
    asm volatile ("fence rw, rw" ::: "memory");
    g_sig_dump_in_progress = 0;
}

void handle_fatal_test_trap(const TrapFrame *tf, uint64_t mcause, uint64_t mepc,
                            uint64_t mtval, uint64_t mstatus)
{
    capture_last_trap(tf, mcause, mepc, mtval, mstatus);
    g_test_tohost_value = TOHOST_TRAP;
    g_test_done = 1;
    g_runner_active = 0;
    g_test_deadline_mtime = 0;

    emit_trap_failure_report(mcause, mepc);

    if (g_ext_pack_loaded &&
        g_ext_active_index != FOOTER_IDX_NONE &&
        g_ext_progress_persisted == 0u) {
        int prc = persist_footer_progress(g_ext_active_index + 1u, FOOTER_IDX_NONE);
        g_ext_next_index = g_ext_active_index + 1u;
        g_ext_inflight_index = FOOTER_IDX_NONE;
        if (prc == 0) g_ext_progress_persisted = 1u;
        uart_puts("[SD] trap persist next_index=");
        uart_put_dec_u64(g_ext_active_index + 1u);
        uart_puts(" clear_in_progress rc=");
        uart_put_hex((uint64_t)(int64_t)prc);
        uart_puts("\n");
    }

    g_case_report_ready = 1;
    g_monitor_report_done = 1;
    request_deferred_reset();

    while (1) { wfi(); }
}

void request_deferred_reset(void)
{
    g_reset_request_mtime = *mtime_ptr() + RESET_DELAY_TICKS;
    asm volatile ("fence rw, rw" ::: "memory");
}

int wait_for_monitor_report(void)
{
    uint64_t deadline = *mtime_ptr() + 600000000ULL;
    while (*mtime_ptr() < deadline) {
        if (g_monitor_report_done) {
            return 0;
        }
        cpu_relax();
    }
    return -1;
}

static ActPackEntry g_ext_entries[MAX_PACK_TESTS];
static uint8_t g_ext_table_buf[sizeof(ActPackHeader) + (MAX_PACK_TESTS * sizeof(ActPackEntry))];

static inline uint32_t dw_readl(uint32_t reg)
{
    return mmio_read32(g_sdio_base + reg);
}

static inline void dw_writel(uint32_t reg, uint32_t v)
{
    mmio_write32(g_sdio_base + reg, v);
}

static int dw_wait_cmd_done(uint32_t *mask_out)
{
    for (uint32_t i = 0; i < 100000u; i++) {
        uint32_t m = dw_readl(DWMCI_RINTSTS);
        if (m & DWMCI_INTMSK_CDONE) {
            if (mask_out) *mask_out = m;
            return 0;
        }
    }
    return -1;
}

static int dw_wait_not_busy(void)
{
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((dw_readl(DWMCI_STATUS) & DWMCI_BUSY) == 0) return 0;
    }
    return -1;
}

static int dw_wait_reset(uint32_t bits)
{
    dw_writel(DWMCI_CTRL, bits);
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((dw_readl(DWMCI_CTRL) & bits) == 0) return 0;
    }
    return -1;
}

static int dw_update_clock(uint8_t enable)
{
    dw_writel(DWMCI_CLKENA, enable ? (DWMCI_CLKEN_ENABLE | DWMCI_CLKEN_LOW_PWR) : 0);
    dw_writel(DWMCI_CLKSRC, 0);
    dw_writel(DWMCI_CMDARG, 0);
    dw_writel(DWMCI_CMD, DWMCI_CMD_START | DWMCI_CMD_UPD_CLK | DWMCI_CMD_PRV_DAT_WAIT);
    return dw_wait_cmd_done(0);
}

static int dw_setup_clock(uint32_t target_hz)
{
    const uint32_t src_hz = 50000000u;
    uint32_t div = 0;
    if (target_hz == 0) return -1;
    if (src_hz > target_hz) {
        div = (src_hz + (2u * target_hz - 1u)) / (2u * target_hz);
    }
    if (dw_update_clock(0) != 0) return -1;
    dw_writel(DWMCI_CLKDIV, div);
    return dw_update_clock(1);
}

static int sd_send_cmd(uint32_t cmdidx, uint32_t arg, uint32_t resp_flags, uint32_t *resp0)
{
    if (dw_wait_not_busy() != 0) return -1;
    dw_writel(DWMCI_RINTSTS, DWMCI_INTMSK_ALL);
    dw_writel(DWMCI_CMDARG, arg);

    uint32_t flags = DWMCI_CMD_START | DWMCI_CMD_USE_HOLD_REG | DWMCI_CMD_PRV_DAT_WAIT | cmdidx;
    if (resp_flags & MMC_RSP_PRESENT) {
        flags |= DWMCI_CMD_RESP_EXP;
        if (resp_flags & MMC_RSP_136) flags |= DWMCI_CMD_RESP_LENGTH;
        if (resp_flags & MMC_RSP_CRC) flags |= DWMCI_CMD_CHECK_CRC;
    }
    dw_writel(DWMCI_CMD, flags);

    uint32_t mask = 0;
    if (dw_wait_cmd_done(&mask) != 0) return -2;
    if (mask & DWMCI_INTMSK_RTO) return -3;
    if (mask & DWMCI_INTMSK_RE) return -4;
    if ((resp_flags & MMC_RSP_CRC) && (mask & DWMCI_INTMSK_RCRC)) return -5;
    if (resp0) *resp0 = dw_readl(DWMCI_RESP0);
    return 0;
}

static int sd_read_block_words(uint32_t lba, uint32_t *dst_words)
{
    if (dw_wait_not_busy() != 0) return -1;

    dw_writel(DWMCI_RINTSTS, DWMCI_INTMSK_ALL);
    dw_writel(DWMCI_BLKSIZ, SD_BLOCK_SIZE);
    dw_writel(DWMCI_BYTCNT, SD_BLOCK_SIZE);
    if (dw_wait_reset(DWMCI_CTRL_FIFO_RESET) != 0) return -2;

    {
        const uint32_t arg = g_sd_high_capacity ? lba : (lba * SD_BLOCK_SIZE);
        const uint32_t flags = DWMCI_CMD_START | DWMCI_CMD_USE_HOLD_REG |
                               DWMCI_CMD_PRV_DAT_WAIT | DWMCI_CMD_RESP_EXP |
                               DWMCI_CMD_CHECK_CRC | DWMCI_CMD_DATA_EXP |
                               MMC_CMD_READ_SINGLE_BLOCK;
        dw_writel(DWMCI_CMDARG, arg);
        dw_writel(DWMCI_CMD, flags);
    }

    {
        uint32_t words_left = SD_BLOCK_SIZE / 4u;
        for (uint32_t loop = 0; loop < 2000000u; loop++) {
            uint32_t mask = dw_readl(DWMCI_RINTSTS);
            if (mask & (DWMCI_DATA_ERR | DWMCI_DATA_TOUT)) return -3;
            if (mask & DWMCI_INTMSK_RTO) return -4;

            if (mask & (DWMCI_INTMSK_RXDR | DWMCI_INTMSK_DTO)) {
                dw_writel(DWMCI_RINTSTS, mask & (DWMCI_INTMSK_RXDR | DWMCI_INTMSK_DTO));
                while (words_left) {
                    uint32_t st = dw_readl(DWMCI_STATUS);
                    uint32_t avail = (st >> DWMCI_FIFO_SHIFT) & DWMCI_FIFO_MASK;
                    if (avail == 0) break;
                    if (avail > words_left) avail = words_left;
                    for (uint32_t i = 0; i < avail; i++) {
                        *dst_words++ = dw_readl(DWMCI_DATA);
                    }
                    words_left -= avail;
                }
            }

            if ((mask & DWMCI_INTMSK_DTO) && words_left == 0) {
                dw_writel(DWMCI_RINTSTS, mask);
                return 0;
            }
        }
    }

    return -5;
}

static int sd_write_block_words(uint32_t lba, const uint32_t *src_words)
{
    if (dw_wait_not_busy() != 0) return -1;

    dw_writel(DWMCI_RINTSTS, DWMCI_INTMSK_ALL);
    dw_writel(DWMCI_BLKSIZ, SD_BLOCK_SIZE);
    dw_writel(DWMCI_BYTCNT, SD_BLOCK_SIZE);
    if (dw_wait_reset(DWMCI_CTRL_FIFO_RESET) != 0) return -2;

    {
        const uint32_t arg = g_sd_high_capacity ? lba : (lba * SD_BLOCK_SIZE);
        const uint32_t flags = DWMCI_CMD_START | DWMCI_CMD_USE_HOLD_REG |
                               DWMCI_CMD_PRV_DAT_WAIT | DWMCI_CMD_RESP_EXP |
                               DWMCI_CMD_CHECK_CRC | DWMCI_CMD_DATA_EXP |
                               DWMCI_CMD_RW | 24u;
        dw_writel(DWMCI_CMDARG, arg);
        dw_writel(DWMCI_CMD, flags);
    }

    {
        uint32_t words_left = SD_BLOCK_SIZE / 4u;
        const uint32_t *src = src_words;
        for (uint32_t loop = 0; loop < 8000000u; loop++) {
            uint32_t mask = dw_readl(DWMCI_RINTSTS);
            if (mask & (DWMCI_DATA_ERR | DWMCI_DATA_TOUT)) return -3;
            if (mask & DWMCI_INTMSK_RTO) return -4;

            while (words_left) {
                uint32_t st = dw_readl(DWMCI_STATUS);
                if (st & DWMCI_FIFO_FULL) break;
                dw_writel(DWMCI_DATA, *src++);
                words_left--;
            }

            if (mask & DWMCI_INTMSK_DTO) {
                dw_writel(DWMCI_RINTSTS, mask);
                if (words_left == 0) return 0;
                return -5;
            }
        }
    }

    return -6;
}

static uint32_t extract_bits_128(const uint32_t r[4], uint32_t msb, uint32_t lsb)
{
    uint32_t ret = 0;
    uint32_t out = 0;
    for (uint32_t b = lsb; b <= msb; b++) {
        uint32_t word = (b / 32u);
        uint32_t bit = b % 32u;
        ret |= ((r[word] >> bit) & 1u) << out++;
    }
    return ret;
}

static int sd_get_capacity_blocks(uint32_t *blocks_out)
{
    uint32_t csd[4] = {0, 0, 0, 0};
    if (sd_send_cmd(MMC_CMD_SEND_CSD, g_sd_rca << 16, MMC_RSP_R2, 0) != 0) return -1;
    csd[0] = dw_readl(DWMCI_RESP0);
    csd[1] = dw_readl(DWMCI_RESP1);
    csd[2] = dw_readl(DWMCI_RESP2);
    csd[3] = dw_readl(DWMCI_RESP3);

    {
        uint32_t csd_structure = extract_bits_128(csd, 127, 126);
        uint32_t c_size;
        uint64_t blocks;

        uart_puts("[SD] csd raw=");
        uart_put_hex(csd[3]); uart_putc(' ');
        uart_put_hex(csd[2]); uart_putc(' ');
        uart_put_hex(csd[1]); uart_putc(' ');
        uart_put_hex(csd[0]); uart_puts("\n");
        uart_puts("[SD] csd_structure="); uart_put_dec_u64(csd_structure); uart_puts("\n");
        if (csd_structure != 1u) return -2;

        c_size = extract_bits_128(csd, 69, 48);
        uart_puts("[SD] c_size="); uart_put_dec_u64(c_size); uart_puts("\n");
        blocks = ((uint64_t)c_size + 1ULL) * 1024ULL;
        uart_puts("[SD] blocks="); uart_put_dec_u64(blocks); uart_puts("\n");
        if (blocks == 0 || blocks > 0xffffffffULL) return -3;
        *blocks_out = (uint32_t)blocks;
    }
    return 0;
}

static int sd_card_init_minimal(void)
{
    uint32_t ocr = 0;
    uint32_t rca_resp = 0;

    uart_puts("[SD] init: pwren\n");
    dw_writel(DWMCI_PWREN, 1);
    uart_puts("[SD] init: reset\n");
    if (dw_wait_reset(DWMCI_RESET_ALL) != 0) return -1;
    uart_puts("[SD] init: clk400k\n");
    (void)dw_setup_clock(400000);

    uart_puts("[SD] init: regs\n");
    dw_writel(DWMCI_TMOUT, 0xffffffffu);
    dw_writel(DWMCI_INTMASK, 0);
    dw_writel(DWMCI_CTYPE, DWMCI_CTYPE_1BIT);
    dw_writel(DWMCI_FIFOTH, DWMCI_MSIZE(0x2) | DWMCI_RX_WMARK(15) | DWMCI_TX_WMARK(16));

    uart_puts("[SD] init: cmd0/cmd8\n");
    (void)sd_send_cmd(MMC_CMD_GO_IDLE_STATE, 0, MMC_RSP_NONE, 0);
    (void)sd_send_cmd(SD_CMD_SEND_IF_COND, 0x1aa, MMC_RSP_R7, 0);

    uart_puts("[SD] init: acmd41\n");
    for (uint32_t i = 0; i < 1000; i++) {
        if (sd_send_cmd(MMC_CMD_APP_CMD, 0, MMC_RSP_R1, 0) != 0) continue;
        if (sd_send_cmd(SD_CMD_APP_SEND_OP_COND, 0x40ff8000u, MMC_RSP_R3, &ocr) != 0) continue;
        if (ocr & 0x80000000u) break;
    }
    if ((ocr & 0x80000000u) == 0) {
        uart_puts("[SD] init: acmd41 timeout\n");
        return -3;
    }
    g_sd_high_capacity = (ocr & 0x40000000u) ? 1u : 0u;

    uart_puts("[SD] init: cid/rca/select\n");
    if (sd_send_cmd(MMC_CMD_ALL_SEND_CID, 0, MMC_RSP_R2, 0) != 0) return -4;
    if (sd_send_cmd(MMC_CMD_SEND_RELATIVE_ADDR, 0, MMC_RSP_R6, &rca_resp) != 0) return -5;
    g_sd_rca = (rca_resp >> 16) & 0xffffu;
    if (g_sd_rca == 0) return -6;

    g_sd_total_blocks = 0;
    if (sd_get_capacity_blocks(&g_sd_total_blocks) != 0) return -9;

    if (sd_send_cmd(MMC_CMD_SELECT_CARD, g_sd_rca << 16, MMC_RSP_R1, 0) != 0) return -7;
    if (!g_sd_high_capacity) {
        if (sd_send_cmd(MMC_CMD_SET_BLOCKLEN, SD_BLOCK_SIZE, MMC_RSP_R1, 0) != 0) return -8;
    }

    uart_puts("[SD] init: clk25m\n");
    (void)dw_setup_clock(25000000);
    uart_puts("[SD] init: done\n");
    return 0;
}

static int sd_card_attach_from_spl(void)
{
    uint32_t rca_resp = 0;

    uart_puts("[SD] attach: use SPL-initialized card\n");
    dw_writel(DWMCI_PWREN, 1);
    dw_writel(DWMCI_TMOUT, 0xffffffffu);
    dw_writel(DWMCI_INTMASK, 0);
    dw_writel(DWMCI_CTYPE, DWMCI_CTYPE_1BIT);
    dw_writel(DWMCI_FIFOTH, DWMCI_MSIZE(0x2) | DWMCI_RX_WMARK(15) | DWMCI_TX_WMARK(16));

    if (sd_send_cmd(MMC_CMD_SEND_RELATIVE_ADDR, 0, MMC_RSP_R6, &rca_resp) != 0) return -1;
    g_sd_rca = (rca_resp >> 16) & 0xffffu;
    if (g_sd_rca == 0) return -2;
    g_sd_high_capacity = 1u;

    if (sd_send_cmd(MMC_CMD_SELECT_CARD, g_sd_rca << 16, MMC_RSP_R1, 0) != 0) return -3;
    g_sd_total_blocks = 0;
    uart_puts("[SD] attach: done rca="); uart_put_hex(g_sd_rca); uart_puts("\n");
    return 0;
}

int load_pack_from_sd_tail(void)
{
    int rc = -999;
    uint32_t footer_next_index = 0;
    uint32_t footer_inflight_index = FOOTER_IDX_NONE;
    g_sdio_base = SDIO1_BASE;
    uart_puts("[SD] probing base="); uart_put_hex((uint64_t)g_sdio_base); uart_puts("\n");
    g_sd_rca = 0;
    g_sd_high_capacity = 0;
    g_sd_total_blocks = 0;
    g_ext_pack_loaded = 0;
    g_ext_active_index = FOOTER_IDX_NONE;
    g_ext_progress_persisted = 0u;
    rc = sd_card_attach_from_spl();
    if (rc != 0) {
        uart_puts("[SD] attach failed rc="); uart_put_hex((uint64_t)(int64_t)rc); uart_puts("\n");
        rc = sd_card_init_minimal();
        if (rc != 0) return -100 + rc;
    }

    {
        uint32_t total_blocks = g_sd_total_blocks;
        if (total_blocks == 0) {
            rc = sd_get_capacity_blocks(&total_blocks);
            if (rc != 0) {
                uart_puts("[SD] capacity rc="); uart_put_hex((uint64_t)(int64_t)rc); uart_puts("\n");
                return -200 + rc;
            }
        }
        uart_puts("[SD] total_blocks="); uart_put_dec_u64(total_blocks); uart_puts("\n");
        if (total_blocks < 2) return -201;

        rc = sd_read_block_words(total_blocks - 1u, g_sd_tmp_block);
        if (rc != 0) return -300 + rc;

        {
            const ActPackFooter *f = (const ActPackFooter *)(const void *)g_sd_tmp_block;
            uart_puts("[SD] footer magic="); uart_put_hex(f->magic); uart_puts(" ver="); uart_put_dec_u64(f->version);
            uart_puts(" start="); uart_put_dec_u64(f->start_lba); uart_puts(" blocks="); uart_put_dec_u64(f->num_blocks); uart_puts("\n");
            if (f->magic != PACK_FOOTER_MAGIC || f->version != 1u) return -301;
            if (f->num_blocks == 0) return -302;

            {
                uint64_t start = f->start_lba;
                uint64_t end = start + (uint64_t)f->num_blocks;
                uint64_t total_bytes = (uint64_t)f->num_blocks * SD_BLOCK_SIZE;
                if (start >= total_blocks || end > (uint64_t)(total_blocks - 1u) || end < start) return -303;
                if (total_bytes > EXT_PACK_MAX_BYTES) return -304;
            }

            g_pack_footer_lba = total_blocks - 1u;
            footer_decode_progress(f->reserved, &footer_next_index, &footer_inflight_index);
            g_ext_next_index = footer_next_index;
            g_ext_inflight_index = footer_inflight_index;
            g_ext_pack_start_lba = f->start_lba;
            g_ext_pack_num_blocks = f->num_blocks;
        }
    }

    uart_puts("[SD] next_index="); uart_put_dec_u64(g_ext_next_index);
    if (g_ext_inflight_index != FOOTER_IDX_NONE) {
        uart_puts(" in_progress_index="); uart_put_dec_u64(g_ext_inflight_index);
    }
    uart_puts("\n");

    rc = sd_read_block_words((uint32_t)g_ext_pack_start_lba, g_sd_tmp_block);
    if (rc != 0) return -500 + rc;
    memcpy_local(g_ext_table_buf, g_sd_tmp_block, SD_BLOCK_SIZE);

    {
        const ActPackHeader *hdr = (const ActPackHeader *)(const void *)g_ext_table_buf;
        size_t table_bytes;
        size_t table_blocks;
        if (hdr->magic != PACK_MAGIC || hdr->version != PACK_VERSION) return -501;
        if (hdr->count == 0 || hdr->count > MAX_PACK_TESTS) return -502;

        table_bytes = sizeof(ActPackHeader) + ((size_t)hdr->count * sizeof(ActPackEntry));
        table_blocks = (table_bytes + SD_BLOCK_SIZE - 1u) / SD_BLOCK_SIZE;
        if (table_blocks * SD_BLOCK_SIZE > sizeof(g_ext_table_buf)) return -503;

        for (size_t b = 1; b < table_blocks; b++) {
            rc = sd_read_block_words((uint32_t)g_ext_pack_start_lba + (uint32_t)b, g_sd_tmp_block);
            if (rc != 0) return -504 + rc;
            memcpy_local(g_ext_table_buf + (b * SD_BLOCK_SIZE), g_sd_tmp_block, SD_BLOCK_SIZE);
        }

        g_ext_pack_count = hdr->count;
        {
            const ActPackEntry *entries = (const ActPackEntry *)(const void *)(g_ext_table_buf + sizeof(ActPackHeader));
            for (uint32_t i = 0; i < g_ext_pack_count; i++) {
                g_ext_entries[i] = entries[i];
            }
        }
    }

    uart_puts("[SD] table_count="); uart_put_dec_u64(g_ext_pack_count); uart_puts("\n");

    if (g_ext_inflight_index != FOOTER_IDX_NONE && g_ext_inflight_index < g_ext_pack_count) {
        g_ext_next_index = g_ext_inflight_index;
        uart_puts("[SD] recover in-progress test index=");
        uart_put_dec_u64(g_ext_next_index);
        uart_puts("\n");
    } else if (g_ext_next_index > g_ext_pack_count) {
        g_ext_next_index = g_ext_pack_count;
    }

    g_ext_pack_loaded = 1;

    asm volatile ("fence rw, rw" ::: "memory");
    return 0;
}

uint64_t trap_c(uint64_t mcause, uint64_t mepc, uint64_t mtval, uint64_t mstatus,
                TrapFrame *tf)
{
    if (g_runner_active && g_test_done) {
        return g_test_resume_pc;
    }

    if (mcause == MCAUSE_MSI) {
        if (g_runner_active) capture_last_trap(tf, mcause, mepc, mtval, mstatus);
        *msip_ptr(RUNNER_HART_ID) = 0;
        asm volatile ("fence rw, rw" ::: "memory");

        if (g_runner_active && g_monitor_seen_tohost) {
            g_test_done = 1;
            return g_test_resume_pc;
        }
        return mepc;
    }

    if (mcause == MCAUSE_MTI) {
        if (g_runner_active) capture_last_trap(tf, mcause, mepc, mtval, mstatus);
        *mtimecmp_ptr(0) = ~0ULL;
        *mtimecmp_ptr(1) = ~0ULL;
        *mtimecmp_ptr(2) = ~0ULL;
        asm volatile ("fence rw, rw" ::: "memory");
        if (g_runner_active) {
            g_test_tohost_value = TOHOST_TIMEOUT;
            g_test_done = 1;
            return g_test_resume_pc;
        }
        return mepc;
    }

    if (mcause == MCAUSE_ECALL_M) {
        if (g_runner_active) capture_last_trap(tf, mcause, mepc, mtval, mstatus);
        g_test_done = 1;
        return g_test_resume_pc;
    }

    if (g_runner_active &&
        (mcause == MCAUSE_INST_MISALIGNED ||
         mcause == MCAUSE_LOAD_MISALIGNED ||
         mcause == MCAUSE_STORE_MISALIGNED)) {
        if (!g_fault_reported) {
            uart_puts("[TRAP] MISALIGNED during test mcause=");
            uart_put_hex(mcause);
            uart_puts(" mepc=");
            uart_put_hex(mepc);
            uart_puts("\n");
            g_fault_reported = 1;
        }
        handle_fatal_test_trap(tf, mcause, mepc, mtval, mstatus);
    }

    if (g_runner_active && ((mcause & MCAUSE_INTERRUPT_BIT) == 0)) {
        if (!g_fault_reported) {
            uart_puts("[TRAP] SYNC during test mcause=");
            uart_put_hex(mcause);
            uart_puts(" mepc=");
            uart_put_hex(mepc);
            uart_puts("\n");
            g_fault_reported = 1;
        }
        handle_fatal_test_trap(tf, mcause, mepc, mtval, mstatus);
    }

    uart_puts("\n[TRAP] mcause=");
    uart_put_hex(mcause);
    uart_puts(" mepc=");
    uart_put_hex(mepc);
    uart_puts(" mtval=");
    uart_put_hex(mtval);
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
    );
}

void monitor_hart_loop(void)
{
    uart_puts("[MON] hart2 online\n");
    while (1) {
        if (!g_reset_armed && g_reset_request_mtime != 0 && g_sig_dump_in_progress == 0) {
            uint64_t now = *mtime_ptr();
            if (now >= g_reset_request_mtime) {
                uart_puts("[RST] trigger watchdog reset\n");
                trigger_watchdog_reset();
            }
        }

        if (g_runner_active && !g_reset_armed && g_reset_request_mtime == 0) {
            uint64_t now = *mtime_ptr();
            if (g_test_deadline_mtime != 0 && now >= g_test_deadline_mtime) {
                uart_puts("[RST] test timeout\n");
                g_reset_request_mtime = now + RESET_DELAY_TICKS;
            }
        }
        if (g_runner_active && g_tohost_ptr != 0 && g_monitor_seen_tohost == 0) {
            uint64_t v = *g_tohost_ptr;
            if (v != 0) {
                uint64_t sig_bytes = 0;
                const char *st = "FAIL";
                const char *name = (const char *)g_active_case_name;
                if (v == 1) st = "PASS";
                else if (v == TOHOST_TIMEOUT) st = "TIMEOUT";
                if (g_sig_end > g_sig_begin) sig_bytes = g_sig_end - g_sig_begin;

                g_test_tohost_value = v;
                asm volatile ("fence rw, rw" ::: "memory");
                g_monitor_seen_tohost = 1;
                *msip_ptr(RUNNER_HART_ID) = 1;
                asm volatile ("fence rw, rw" ::: "memory");

                uart_puts("[MON] tohost addr="); uart_put_hex(g_tohost_addr);
                uart_puts(" value="); uart_put_hex(v); uart_puts("\n");
                uart_puts("[MON] sig_begin="); uart_put_hex(g_sig_begin);
                uart_puts(" sig_end="); uart_put_hex(g_sig_end);
                uart_puts(" sig_bytes="); uart_put_hex(sig_bytes); uart_puts("\n");
                uart_puts("[CASE] REPORT name=");
                uart_puts((name && name[0]) ? name : "unknown");
                uart_puts(" status="); uart_puts(st);
                uart_puts(" rc=0x0000000000000000");
                uart_puts(" tohost_addr="); uart_put_hex(g_tohost_addr);
                uart_puts(" tohost_value="); uart_put_hex(v);
                uart_puts(" sig_begin="); uart_put_hex(g_sig_begin);
                uart_puts(" sig_end="); uart_put_hex(g_sig_end);
                uart_puts(" sig_bytes="); uart_put_hex(sig_bytes);
                uart_puts(" exit_pc="); uart_put_hex(get_case_exit_pc());
                uart_puts("\n");

                if (v == 1) emit_execution_context("PASS");
                else if (v == TOHOST_TIMEOUT) emit_execution_context("TIMEOUT");
                else emit_execution_context("FAIL");

                g_sig_dump_in_progress = 1;
                asm volatile ("fence rw, rw" ::: "memory");
                dump_failure_scratch_region(g_fail_begin, g_fail_end);
                dump_signature_region(g_sig_begin, g_sig_end);
                asm volatile ("fence rw, rw" ::: "memory");
                g_sig_dump_in_progress = 0;
                if (g_ext_pack_loaded &&
                    g_ext_active_index != FOOTER_IDX_NONE &&
                    g_ext_progress_persisted == 0u) {
                    int prc = persist_footer_progress(g_ext_active_index + 1u, FOOTER_IDX_NONE);
                    g_ext_next_index = g_ext_active_index + 1u;
                    g_ext_inflight_index = FOOTER_IDX_NONE;
                    if (prc == 0) {
                        g_ext_progress_persisted = 1u;
                    }
                    uart_puts("[SD] monitor persist next_index=");
                    uart_put_dec_u64(g_ext_active_index + 1u);
                    uart_puts(" clear_in_progress rc=");
                    uart_put_hex((uint64_t)(int64_t)prc);
                    uart_puts("\n");
                }
                g_case_report_ready = 1;
                g_monitor_report_done = 1;
                g_reset_request_mtime = *mtime_ptr() + RESET_DELAY_TICKS;
            }
        }
        cpu_relax();
    }
}

const char *case_status_name(int status)
{
    switch (status) {
        case CASE_STATUS_PASS: return "PASS";
        case CASE_STATUS_FAIL: return "FAIL";
        case CASE_STATUS_TIMEOUT: return "TIMEOUT";
        default: return "ERROR";
    }
}

int case_is_pass(const TestResult *tr)
{
    return tr->status == CASE_STATUS_PASS;
}

void emit_case_report(const TestResult *tr)
{
    uint64_t sig_bytes = 0;
    if (tr->sig_end > tr->sig_begin) sig_bytes = tr->sig_end - tr->sig_begin;

    uart_puts("[CASE] REPORT name="); uart_puts(tr->name);
    uart_puts(" status="); uart_puts(case_status_name(tr->status));
    uart_puts(" rc="); uart_put_hex((uint64_t)(int64_t)tr->rc);
    uart_puts(" tohost_addr="); uart_put_hex(tr->tohost_addr);
    uart_puts(" tohost_value="); uart_put_hex(tr->tohost);
    uart_puts(" sig_begin="); uart_put_hex(tr->sig_begin);
    uart_puts(" sig_end="); uart_put_hex(tr->sig_end);
    uart_puts(" sig_bytes="); uart_put_hex(sig_bytes);
    uart_puts(" exit_pc="); uart_put_hex(get_case_exit_pc());
    uart_puts("\n");

    uart_puts("RVCP-RESULT: Test File \""); uart_puts(tr->name);
    uart_puts(".S\": ");
    if (tr->status == CASE_STATUS_PASS) uart_puts("PASSED");
    else uart_puts("FAILED");
    uart_puts("\n");
}

uint64_t run_loaded_entry(uint64_t entry)
{
    uint64_t sp_snapshot = 0;
    uint64_t gp_snapshot = 0;
    uintptr_t test_sp = 0;
    __asm__ volatile ("mv %0, sp" : "=r"(sp_snapshot));
    __asm__ volatile ("mv %0, gp" : "=r"(gp_snapshot));
    g_runner_saved_sp = sp_snapshot;
    g_runner_saved_gp = gp_snapshot;

    g_test_done = 0;
    g_monitor_seen_tohost = 0;
    g_test_tohost_value = 0;
    g_fault_reported = 0;
    g_reset_armed = 0;
    g_reset_request_mtime = 0;
    g_case_report_ready = 0;
    g_monitor_report_done = 0;
    g_test_deadline_mtime = *mtime_ptr() + 200000000ULL;
    g_last_trap_valid = 0;
    g_last_trap_mcause = 0;
    g_last_trap_mepc = 0;
    g_last_trap_mtval = 0;
    g_last_trap_mstatus = 0;
    memset_local(&g_last_trap_frame, 0, sizeof(g_last_trap_frame));

    monitor_irq_enable();
    g_runner_active = 1;
    asm volatile ("fence rw, rw" ::: "memory");
    {
        uint64_t deadline = *mtime_ptr() + TEST_TIMEOUT_TICKS;
        *mtimecmp_ptr(0) = deadline;
        *mtimecmp_ptr(1) = deadline;
        *mtimecmp_ptr(2) = deadline;
    }
    asm volatile ("fence rw, rw" ::: "memory");

    if (g_tohost_ptr) *g_tohost_ptr = 0;

    g_test_resume_pc = (uint64_t)(uintptr_t)&&after_entry;
    test_sp = (((uintptr_t)g_test_stack + (uintptr_t)sizeof(g_test_stack)) & ~(uintptr_t)0xFULL);
    __asm__ volatile ("mv sp, %0" :: "r"(test_sp) : "memory");

    ((void (*)(void))(uintptr_t)entry)();

after_entry:
    __asm__ volatile ("mv sp, %0" :: "r"(g_runner_saved_sp) : "memory");
    __asm__ volatile ("mv gp, %0" :: "r"(g_runner_saved_gp) : "memory");
    *mtimecmp_ptr(0) = ~0ULL;
    *mtimecmp_ptr(1) = ~0ULL;
    *mtimecmp_ptr(2) = ~0ULL;
    asm volatile ("fence rw, rw" ::: "memory");
    g_runner_active = 0;
    g_test_deadline_mtime = 0;
    monitor_irq_disable();

    if (g_test_tohost_value != 0) return g_test_tohost_value;
    if (g_tohost_ptr) return *g_tohost_ptr;
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

    out->tohost_addr = g_tohost_addr;
    out->sig_begin = g_sig_begin;
    out->sig_end = g_sig_end;

    enable_fpu_set_fs_only();
    enable_fpu_try_clear_fcsr();

    dbg_puts("[ACT] entry=0x"); dbg_hex_u64(entry); dbg_nl();
    dbg_puts("[ACT] tohost=0x"); dbg_hex_u64(g_tohost_addr); dbg_nl();
    dbg_puts("[ACT] sig_begin=0x"); dbg_hex_u64(g_sig_begin);
    dbg_puts(" sig_end=0x"); dbg_hex_u64(g_sig_end); dbg_nl();

    g_active_case_name = name;
    out->tohost = run_loaded_entry(entry);
    g_active_case_name = 0;

    if (out->tohost != 0 && !g_monitor_report_done) {
        (void)wait_for_monitor_report();
    }

    if (g_monitor_report_done) {
        g_reset_request_mtime = 0;
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
    g_sig_dump_in_progress = 1;
    asm volatile ("fence rw, rw" ::: "memory");
    dump_failure_scratch_region(g_fail_begin, g_fail_end);
    dump_signature_region(g_sig_begin, g_sig_end);
    asm volatile ("fence rw, rw" ::: "memory");
    g_sig_dump_in_progress = 0;
    g_case_report_ready = 1;
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

int run_pack_external(uint64_t *total, uint64_t *pass, uint64_t *fail)
{
    if (!g_ext_pack_loaded) return -99;

    {
        const size_t pack_size = (size_t)g_ext_pack_num_blocks * SD_BLOCK_SIZE;
        const ActPackEntry *entries = g_ext_entries;
        uint32_t start_idx = g_ext_next_index;

        uart_puts("[SUITE] external pack @ "); uart_put_hex(EXT_PACK_ADDR);
        uart_puts(" count="); uart_put_dec_u64(g_ext_pack_count); uart_puts("\n");

        if (start_idx >= g_ext_pack_count) {
            uart_puts("[SUITE] all external tests already dispatched\n");
            return 0;
        }

        for (uint32_t i = start_idx; i <= start_idx; i++) {
            const ActPackEntry *e = &entries[i];
            const char *name = e->name[0] ? e->name : "unnamed";
            int prc;

            g_ext_active_index = i;
            g_ext_progress_persisted = 0u;
            prc = persist_footer_progress(start_idx, start_idx);
            uart_puts("[SD] mark in_progress_index="); uart_put_dec_u64(start_idx);
            uart_puts(" next_index="); uart_put_dec_u64(start_idx);
            uart_puts(" rc="); uart_put_hex((uint64_t)(int64_t)prc); uart_puts("\n");

            if (e->offset > pack_size || e->size > pack_size || (e->offset + e->size) > pack_size || (e->offset + e->size) < e->offset) {
                uart_puts("[CASE] RESULT name="); uart_puts(name);
                uart_puts(" status=ERROR reason=bad_external_range\n");
                *total += 1;
                *fail += 1;
                if (g_ext_progress_persisted == 0u) {
                    prc = persist_footer_progress(i + 1u, FOOTER_IDX_NONE);
                    uart_puts("[SD] persist next_index="); uart_put_dec_u64(i + 1u);
                    uart_puts(" clear_in_progress rc="); uart_put_hex((uint64_t)(int64_t)prc); uart_puts("\n");
                }
                continue;
            }

            {
                uint64_t end_off = e->offset + e->size;
                uint32_t first_block = (uint32_t)(e->offset / SD_BLOCK_SIZE);
                uint32_t first_block_off = (uint32_t)(e->offset % SD_BLOCK_SIZE);
                uint32_t last_excl_block = (uint32_t)((end_off + SD_BLOCK_SIZE - 1u) / SD_BLOCK_SIZE);
                uint32_t num_blocks = last_excl_block - first_block;
                uint64_t read_bytes = (uint64_t)num_blocks * SD_BLOCK_SIZE;
                uint32_t *dst;
                int lrc = 0;
                TestResult tr;

                if (read_bytes > EXT_PACK_MAX_BYTES) {
                    uart_puts("[CASE] RESULT name="); uart_puts(name);
                    uart_puts(" status=ERROR reason=elf_too_large\n");
                    *total += 1;
                    *fail += 1;
                    if (g_ext_progress_persisted == 0u) {
                        prc = persist_footer_progress(i + 1u, FOOTER_IDX_NONE);
                        uart_puts("[SD] persist next_index="); uart_put_dec_u64(i + 1u);
                        uart_puts(" clear_in_progress rc="); uart_put_hex((uint64_t)(int64_t)prc); uart_puts("\n");
                    }
                    continue;
                }

                dst = (uint32_t *)(uintptr_t)EXT_PACK_ADDR;
                for (uint32_t b = 0; b < num_blocks; b++) {
                    lrc = sd_read_block_words((uint32_t)g_ext_pack_start_lba + first_block + b,
                                              dst + (b * (SD_BLOCK_SIZE / 4u)));
                    if (lrc != 0) break;
                }
                if (lrc != 0) {
                    uart_puts("[CASE] RESULT name="); uart_puts(name);
                    uart_puts(" status=ERROR reason=elf_read_fail rc=");
                    uart_put_hex((uint64_t)(int64_t)lrc); uart_puts("\n");
                    *total += 1;
                    *fail += 1;
                    if (g_ext_progress_persisted == 0u) {
                        prc = persist_footer_progress(i + 1u, FOOTER_IDX_NONE);
                        uart_puts("[SD] persist next_index="); uart_put_dec_u64(i + 1u);
                        uart_puts(" clear_in_progress rc="); uart_put_hex((uint64_t)(int64_t)prc); uart_puts("\n");
                    }
                    continue;
                }

                (void)run_one_blob(name, (const uint8_t *)(uintptr_t)(EXT_PACK_ADDR + first_block_off), (size_t)e->size, &tr);
                *total += 1;
                if (case_is_pass(&tr)) *pass += 1;
                else *fail += 1;

                if (g_ext_progress_persisted == 0u) {
                    prc = persist_footer_progress(i + 1u, FOOTER_IDX_NONE);
                    uart_puts("[SD] persist next_index="); uart_put_dec_u64(i + 1u);
                    uart_puts(" clear_in_progress rc="); uart_put_hex((uint64_t)(int64_t)prc); uart_puts("\n");
                }
                g_ext_active_index = FOOTER_IDX_NONE;

                if ((i + 1u) < g_ext_pack_count) {
                    request_deferred_reset();
                }
            }
        }
    }

    return 0;
}
