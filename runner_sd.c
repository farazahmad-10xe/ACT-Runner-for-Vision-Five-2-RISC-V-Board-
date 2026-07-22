#include "runner_shared.h"
#include "runner_sd_sdhci_k1.h"

#if RUNNER_SD_BACKEND == RUNNER_SD_BACKEND_DWMCI
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
#endif

#define FOOTER_LEGACY_IDX16_MASK 0x0000ffffu
#define FOOTER_IDX_MASK          0x000003ffu
#define FOOTER_INFLIGHT_SHIFT    10u
#define FOOTER_RETRY_SHIFT       20u
#define FOOTER_RETRY_MASK        0x0000000fu
#define FOOTER_PROGRESS_MAGIC    0xa0000000u
#define FOOTER_PROGRESS_MAGIC_MASK 0xf0000000u
#ifndef RUNNER_MAX_TEST_RETRIES
#define RUNNER_MAX_TEST_RETRIES 3u
#endif
#define FOOTER_MAX_TEST_RETRIES  RUNNER_MAX_TEST_RETRIES

uint32_t g_pack_footer_lba = 0;
uint32_t g_ext_next_index = 0;
uint32_t g_ext_inflight_index = FOOTER_IDX_NONE;
uint32_t g_ext_active_index = FOOTER_IDX_NONE;
uint32_t g_ext_progress_persisted = 0u;
static uint32_t g_ext_recovery_retry_count = 0u;
int g_ext_pack_loaded = 0;

#if RUNNER_SD_BACKEND == RUNNER_SD_BACKEND_DWMCI
static uint32_t g_sd_rca = 0;
static uint8_t g_sd_high_capacity = 0;
#endif
static uint32_t g_sd_total_blocks = 0;
static uint32_t g_sd_tmp_block[SD_BLOCK_SIZE / sizeof(uint32_t)];
static uintptr_t g_sdio_base = SDIO1_BASE;
static uint64_t g_ext_pack_start_lba = 0;
static uint32_t g_ext_pack_num_blocks = 0;
uint32_t g_ext_pack_count = 0;
static ActPackEntry g_ext_entries[MAX_PACK_TESTS];
static uint8_t g_ext_table_buf[sizeof(ActPackHeader) + (MAX_PACK_TESTS * sizeof(ActPackEntry))];

#if RUNNER_SD_BACKEND == RUNNER_SD_BACKEND_DWMCI
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
    for (uint32_t i = 0; i < 8000000u; i++) {
        if ((dw_readl(DWMCI_STATUS) & DWMCI_BUSY) == 0) return 0;
    }
    return -1;
}

static int dw_wait_not_busy_after_write(void)
{
    for (uint32_t i = 0; i < 32000000u; i++) {
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

    {
        uint32_t flags = DWMCI_CMD_START | DWMCI_CMD_USE_HOLD_REG | DWMCI_CMD_PRV_DAT_WAIT | cmdidx;
        if (resp_flags & MMC_RSP_PRESENT) {
            flags |= DWMCI_CMD_RESP_EXP;
            if (resp_flags & MMC_RSP_136) flags |= DWMCI_CMD_RESP_LENGTH;
            if (resp_flags & MMC_RSP_CRC) flags |= DWMCI_CMD_CHECK_CRC;
        }
        dw_writel(DWMCI_CMD, flags);
    }

    {
        uint32_t mask = 0;
        if (dw_wait_cmd_done(&mask) != 0) return -2;
        if (mask & DWMCI_INTMSK_RTO) return -3;
        if (mask & DWMCI_INTMSK_RE) return -4;
        if ((resp_flags & MMC_RSP_CRC) && (mask & DWMCI_INTMSK_RCRC)) return -5;
        if (resp0) *resp0 = dw_readl(DWMCI_RESP0);
    }
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
                if (words_left == 0) return dw_wait_not_busy_after_write();
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
        uint32_t c_size = extract_bits_128(csd, 69, 48);
        uint64_t blocks;

        uart_puts("[SD] csd raw=");
        uart_put_hex(csd[3]); uart_putc(' ');
        uart_put_hex(csd[2]); uart_putc(' ');
        uart_put_hex(csd[1]); uart_putc(' ');
        uart_put_hex(csd[0]); uart_puts("\n");
        uart_puts("[SD] csd_structure="); uart_put_dec_u64(csd_structure); uart_puts("\n");
        if (csd_structure != 1u) return -2;

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
    for (volatile uint32_t i = 0; i < 100000; i++) { }
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
    if ((ocr & 0x80000000u) == 0) return -3;
    g_sd_high_capacity = (ocr & 0x40000000u) ? 1u : 0u;

    uart_puts("[SD] init: cid/rca/select\n");
    {
        int retries = 3;
        while (retries-- > 0) {
            if (sd_send_cmd(MMC_CMD_ALL_SEND_CID, 0, MMC_RSP_R2, 0) == 0) break;
            uart_puts("[SD] init: retry CMD2\n");
        }
        if (retries < 0) return -4;
    }

    {
        int retries = 3;
        while (retries-- > 0) {
            if (sd_send_cmd(MMC_CMD_SEND_RELATIVE_ADDR, 0, MMC_RSP_R6, &rca_resp) == 0) break;
            uart_puts("[SD] init: retry CMD3\n");
        }
        if (retries < 0) return -5;
    }
    g_sd_rca = (rca_resp >> 16) & 0xffffu;
    if (g_sd_rca == 0) return -6;

    g_sd_total_blocks = 0;
    if (sd_get_capacity_blocks(&g_sd_total_blocks) != 0) return -9;

    {
        int retries = 3;
        while (retries-- > 0) {
            if (sd_send_cmd(MMC_CMD_SELECT_CARD, g_sd_rca << 16, MMC_RSP_R1, 0) == 0) break;
            uart_puts("[SD] init: retry CMD7\n");
        }
        if (retries < 0) return -7;
    }
    if (!g_sd_high_capacity) {
        int retries = 3;
        while (retries-- > 0) {
            if (sd_send_cmd(MMC_CMD_SET_BLOCKLEN, SD_BLOCK_SIZE, MMC_RSP_R1, 0) == 0) break;
            uart_puts("[SD] init: retry CMD16\n");
        }
        if (retries < 0) return -8;
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

    {
        int retries = 3;
        while (retries-- > 0) {
            if (sd_send_cmd(MMC_CMD_SEND_RELATIVE_ADDR, 0, MMC_RSP_R6, &rca_resp) == 0) break;
            uart_puts("[SD] attach: retry CMD3\n");
        }
        if (retries < 0) return -1;
    }

    g_sd_rca = (rca_resp >> 16) & 0xffffu;
    if (g_sd_rca == 0) return -2;
    g_sd_high_capacity = 1u;

    {
        int retries = 3;
        while (retries-- > 0) {
            if (sd_send_cmd(MMC_CMD_SELECT_CARD, g_sd_rca << 16, MMC_RSP_R1, 0) == 0) break;
            uart_puts("[SD] attach: retry CMD7\n");
        }
        if (retries < 0) return -3;
    }

    g_sd_total_blocks = 0;
    uart_puts("[SD] attach: done rca="); uart_put_hex(g_sd_rca); uart_puts("\n");
    return 0;
}

static void sd_backend_reset_state(uintptr_t base)
{
    g_sdio_base = base;
    g_sd_rca = 0;
    g_sd_high_capacity = 0;
    g_sd_total_blocks = 0;
}

static void sd_backend_quiesce(void)
{
    int busy_rc;
    int idle_rc;
    int clk_rc;

    if (g_sdio_base == 0) return;
    busy_rc = dw_wait_not_busy();
    idle_rc = sd_send_cmd(MMC_CMD_GO_IDLE_STATE, 0, MMC_RSP_NONE, 0);
    clk_rc = dw_update_clock(0);
    dw_writel(DWMCI_INTMASK, 0);
    dw_writel(DWMCI_RINTSTS, DWMCI_INTMSK_ALL);
    dw_writel(DWMCI_PWREN, 0);

    uart_puts("[SD] quiesce busy_rc=");
    uart_put_hex((uint64_t)(int64_t)busy_rc);
    uart_puts(" idle_rc=");
    uart_put_hex((uint64_t)(int64_t)idle_rc);
    uart_puts(" clk_rc=");
    uart_put_hex((uint64_t)(int64_t)clk_rc);
    uart_puts("\n");
}

#elif RUNNER_SD_BACKEND == RUNNER_SD_BACKEND_K1_SDHCI

static int sd_read_block_words(uint32_t lba, uint32_t *dst_words)
{
    return k1_sdhci_read_block_words(lba, dst_words);
}

static int sd_write_block_words(uint32_t lba, const uint32_t *src_words)
{
    return k1_sdhci_write_block_words(lba, src_words);
}

static int sd_get_capacity_blocks(uint32_t *blocks_out)
{
    return k1_sdhci_get_capacity_blocks(blocks_out);
}

static int sd_card_init_minimal(void)
{
    return k1_sdhci_card_init();
}

static int sd_card_attach_from_spl(void)
{
    return k1_sdhci_attach_from_spl();
}

static void sd_backend_reset_state(uintptr_t base)
{
    g_sdio_base = base;
    g_sd_total_blocks = 0;
    k1_sdhci_reset_state(base);
}

static void sd_backend_quiesce(void)
{
    k1_sdhci_quiesce();
}

#else
#error "Unsupported RUNNER_SD_BACKEND"
#endif

static uint32_t footer_encode_progress(uint32_t next_index, uint32_t inflight_index_or_none,
                                       uint32_t retry_count)
{
    uint32_t next = next_index & FOOTER_IDX_MASK;
    uint32_t inflight = 0u;
    uint32_t retry = retry_count & FOOTER_RETRY_MASK;
    if (inflight_index_or_none != FOOTER_IDX_NONE) {
        uint32_t ip1 = inflight_index_or_none + 1u;
        if (ip1 > FOOTER_IDX_MASK) ip1 = FOOTER_IDX_MASK;
        inflight = ip1 & FOOTER_IDX_MASK;
    } else {
        retry = 0u;
    }
    return FOOTER_PROGRESS_MAGIC |
           next |
           (inflight << FOOTER_INFLIGHT_SHIFT) |
           (retry << FOOTER_RETRY_SHIFT);
}

static void footer_decode_progress(uint32_t reserved_raw, uint32_t *next_index,
                                   uint32_t *inflight_index_or_none, uint32_t *retry_count)
{
    if ((reserved_raw & FOOTER_PROGRESS_MAGIC_MASK) == FOOTER_PROGRESS_MAGIC) {
        uint32_t next = reserved_raw & FOOTER_IDX_MASK;
        uint32_t inflight = (reserved_raw >> FOOTER_INFLIGHT_SHIFT) & FOOTER_IDX_MASK;
        *next_index = next;
        *inflight_index_or_none = (inflight == 0u) ? FOOTER_IDX_NONE : (inflight - 1u);
        *retry_count = (reserved_raw >> FOOTER_RETRY_SHIFT) & FOOTER_RETRY_MASK;
        return;
    }

    {
        uint32_t next16 = reserved_raw & FOOTER_LEGACY_IDX16_MASK;
        uint32_t inflight16 = (reserved_raw >> 16u) & FOOTER_LEGACY_IDX16_MASK;
        *next_index = next16;
        *inflight_index_or_none = (inflight16 == 0u) ? FOOTER_IDX_NONE : (inflight16 - 1u);
        *retry_count = 0u;
    }
}

static int persist_footer_progress_retry(uint32_t next_index, uint32_t inflight_index_or_none,
                                         uint32_t retry_count)
{
    if (g_pack_footer_lba == 0u) return -1;

    {
        ActPackFooter *fw = (ActPackFooter *)(void *)g_sd_tmp_block;
        if (sd_read_block_words(g_pack_footer_lba, g_sd_tmp_block) != 0) return -2;
        if (fw->magic != PACK_FOOTER_MAGIC || fw->version != 1u) return -3;

        fw->reserved = footer_encode_progress(next_index, inflight_index_or_none, retry_count);
        for (int t = 0; t < 3; t++) {
            int wrc = sd_write_block_words(g_pack_footer_lba, g_sd_tmp_block);
            if (wrc == 0) {
                for (volatile uint32_t i = 0; i < 1000000; i++) { }
                return 0;
            }
        }
    }
    return -4;
}

int persist_footer_progress(uint32_t next_index, uint32_t inflight_index_or_none)
{
    return persist_footer_progress_retry(next_index, inflight_index_or_none, 0u);
}

void sd_quiesce_for_reset(void)
{
    sd_backend_quiesce();
}

int load_pack_from_sd_tail(void)
{
    int rc = -999;
    uint32_t footer_next_index = 0;
    uint32_t footer_inflight_index = FOOTER_IDX_NONE;
    uint32_t footer_retry_count = 0u;
    sd_backend_reset_state(SDIO1_BASE);
    uart_puts("[SD] probing base="); uart_put_hex((uint64_t)g_sdio_base); uart_puts("\n");
    g_ext_pack_loaded = 0;
    g_ext_active_index = FOOTER_IDX_NONE;
    g_ext_progress_persisted = 0u;
    g_ext_recovery_retry_count = 0u;
    rc = sd_card_attach_from_spl();
    if (rc != 0) {
        uart_puts("[SD] attach failed rc="); uart_put_hex((uint64_t)(int64_t)rc); uart_puts("\n");
        for (int attempt = 1; attempt <= 3; attempt++) {
            uart_puts("[SD] init attempt "); uart_put_dec_u64((uint64_t)attempt); uart_puts("\n");
            rc = sd_card_init_minimal();
            if (rc == 0) break;
            uart_puts("[SD] init failed rc="); uart_put_hex((uint64_t)(int64_t)rc); uart_puts("\n");
        }
        if (rc != 0) return -100 + rc;
    }

    {
        uint32_t total_blocks = g_sd_total_blocks;
        if (total_blocks == 0) {
            rc = sd_get_capacity_blocks(&total_blocks);
            if (rc != 0) return -200 + rc;
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
            footer_decode_progress(f->reserved, &footer_next_index, &footer_inflight_index,
                                   &footer_retry_count);
            g_ext_next_index = footer_next_index;
            g_ext_inflight_index = footer_inflight_index;
            g_ext_recovery_retry_count = footer_retry_count;
            g_ext_pack_start_lba = f->start_lba;
            g_ext_pack_num_blocks = f->num_blocks;
        }
    }

    uart_puts("[SD] next_index="); uart_put_dec_u64(g_ext_next_index);
    if (g_ext_inflight_index != FOOTER_IDX_NONE) {
        uart_puts(" in_progress_index="); uart_put_dec_u64(g_ext_inflight_index);
        uart_puts(" retry_count="); uart_put_dec_u64(g_ext_recovery_retry_count);
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
        if (g_ext_recovery_retry_count >= FOOTER_MAX_TEST_RETRIES) {
            int prc;
            g_ext_next_index = g_ext_inflight_index + 1u;
            uart_puts("[SD] skip in-progress test index=");
            uart_put_dec_u64(g_ext_inflight_index);
            uart_puts(" after_retries=");
            uart_put_dec_u64(g_ext_recovery_retry_count);
            uart_puts("\n");
            prc = persist_footer_progress(g_ext_next_index, FOOTER_IDX_NONE);
            g_ext_inflight_index = FOOTER_IDX_NONE;
            g_ext_recovery_retry_count = 0u;
            uart_puts("[SD] persist skipped next_index=");
            uart_put_dec_u64(g_ext_next_index);
            uart_puts(" clear_in_progress rc=");
            uart_put_hex((uint64_t)(int64_t)prc);
            uart_puts("\n");
        } else {
            g_ext_next_index = g_ext_inflight_index;
            uart_puts("[SD] recover in-progress test index=");
            uart_put_dec_u64(g_ext_next_index);
            uart_puts(" retry_next=");
            uart_put_dec_u64(g_ext_recovery_retry_count + 1u);
            uart_puts("/");
            uart_put_dec_u64(FOOTER_MAX_TEST_RETRIES);
            uart_puts("\n");
        }
    } else if (g_ext_next_index > g_ext_pack_count) {
        g_ext_next_index = g_ext_pack_count;
    }

    g_ext_pack_loaded = 1;
    asm volatile ("fence rw, rw" ::: "memory");
    return 0;
}

int run_pack_external(uint64_t *total, uint64_t *pass, uint64_t *fail)
{
    if (!g_ext_pack_loaded) return -99;

    {
        const size_t pack_size = (size_t)g_ext_pack_num_blocks * SD_BLOCK_SIZE;
        uint32_t start_idx = g_ext_next_index;

        uart_puts("[SUITE] external pack @ "); uart_put_hex(EXT_PACK_ADDR);
        uart_puts(" count="); uart_put_dec_u64(g_ext_pack_count); uart_puts("\n");

        if (start_idx >= g_ext_pack_count) {
            uart_puts("[SUITE] all external tests already dispatched\n");
            return 0;
        }

        for (uint32_t i = start_idx; i <= start_idx; i++) {
            const ActPackEntry *e = &g_ext_entries[i];
            const char *name = e->name[0] ? e->name : "unnamed";
            int prc;
            uint32_t attempt_count = g_ext_recovery_retry_count + 1u;
            if (attempt_count > FOOTER_MAX_TEST_RETRIES) attempt_count = FOOTER_MAX_TEST_RETRIES;

            g_ext_active_index = i;
            g_ext_progress_persisted = 0u;
            prc = persist_footer_progress_retry(start_idx, start_idx, attempt_count);
            uart_puts("[SD] mark in_progress_index="); uart_put_dec_u64(start_idx);
            uart_puts(" next_index="); uart_put_dec_u64(start_idx);
            uart_puts(" retry_count="); uart_put_dec_u64(attempt_count);
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
                if ((i + 1u) < g_ext_pack_count) request_deferred_reset();
            }
        }
    }

    return 0;
}
