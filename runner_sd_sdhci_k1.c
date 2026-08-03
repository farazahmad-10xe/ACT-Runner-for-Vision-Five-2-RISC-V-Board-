#include "runner_shared.h"
#include "runner_sd_sdhci_k1.h"

#if RUNNER_SD_BACKEND == RUNNER_SD_BACKEND_K1_SDHCI

/* Standard SDHCI register map. */
#define SDHCI_DMA_ADDRESS        0x00u
#define SDHCI_BLOCK_SIZE         0x04u
#define SDHCI_BLOCK_COUNT        0x06u
#define SDHCI_ARGUMENT           0x08u
#define SDHCI_TRANSFER_MODE      0x0cu
#define SDHCI_COMMAND            0x0eu
#define SDHCI_RESPONSE           0x10u
#define SDHCI_BUFFER             0x20u
#define SDHCI_PRESENT_STATE      0x24u
#define SDHCI_HOST_CONTROL       0x28u
#define SDHCI_POWER_CONTROL      0x29u
#define SDHCI_CLOCK_CONTROL      0x2cu
#define SDHCI_TIMEOUT_CONTROL    0x2eu
#define SDHCI_SOFTWARE_RESET     0x2fu
#define SDHCI_INT_STATUS         0x30u
#define SDHCI_INT_ENABLE         0x34u
#define SDHCI_SIGNAL_ENABLE      0x38u
#define SDHCI_CAPABILITIES       0x40u
#define SDHCI_HOST_VERSION       0xfeu

/* Present-state bits. */
#define SDHCI_CMD_INHIBIT        (1u << 0)
#define SDHCI_DATA_INHIBIT       (1u << 1)
#define SDHCI_SPACE_AVAILABLE    (1u << 10)
#define SDHCI_DATA_AVAILABLE     (1u << 11)

/* Software reset bits.  Avoid RESET_ALL: K1 needs PHY reinitialization. */
#define SDHCI_RESET_CMD          (1u << 1)
#define SDHCI_RESET_DATA         (1u << 2)

/* Normal and error interrupt-status bits. */
#define SDHCI_INT_RESPONSE       (1u << 0)
#define SDHCI_INT_DATA_END       (1u << 1)
#define SDHCI_INT_SPACE_AVAIL    (1u << 4)
#define SDHCI_INT_DATA_AVAIL     (1u << 5)
#define SDHCI_INT_ERROR          (1u << 15)
#define SDHCI_INT_CMD_TIMEOUT    (1u << 16)
#define SDHCI_INT_CMD_CRC        (1u << 17)
#define SDHCI_INT_CMD_END_BIT    (1u << 18)
#define SDHCI_INT_CMD_INDEX      (1u << 19)
#define SDHCI_INT_DATA_TIMEOUT   (1u << 20)
#define SDHCI_INT_DATA_CRC       (1u << 21)
#define SDHCI_INT_DATA_END_BIT   (1u << 22)
#define SDHCI_INT_ERROR_MASK     0xffff0000u
#define SDHCI_INT_ALL            0xffffffffu

/* Transfer-mode and command encodings. */
#define SDHCI_TRNS_BLK_CNT_EN    (1u << 1)
#define SDHCI_TRNS_READ          (1u << 4)

#define SDHCI_CMD_RESP_NONE      0x00u
#define SDHCI_CMD_RESP_LONG      0x01u
#define SDHCI_CMD_RESP_SHORT     0x02u
#define SDHCI_CMD_RESP_SHORT_BUSY 0x03u
#define SDHCI_CMD_CRC            0x08u
#define SDHCI_CMD_INDEX          0x10u
#define SDHCI_CMD_DATA           0x20u

/* SD protocol command numbers. */
#define MMC_CMD_GO_IDLE_STATE       0u
#define MMC_CMD_ALL_SEND_CID        2u
#define MMC_CMD_SEND_RELATIVE_ADDR  3u
#define MMC_CMD_SELECT_CARD         7u
#define MMC_CMD_SEND_CSD            9u
#define MMC_CMD_SET_BLOCKLEN        16u
#define MMC_CMD_READ_SINGLE_BLOCK   17u
#define MMC_CMD_WRITE_SINGLE_BLOCK  24u
#define MMC_CMD_APP_CMD             55u
#define SD_CMD_SEND_IF_COND         8u
#define SD_CMD_APP_SEND_OP_COND     41u

/* Internal response description. */
#define K1_RSP_PRESENT  (1u << 0)
#define K1_RSP_136      (1u << 1)
#define K1_RSP_CRC      (1u << 2)
#define K1_RSP_BUSY     (1u << 3)
#define K1_RSP_OPCODE   (1u << 4)
#define K1_RSP_NONE     0u
#define K1_RSP_R1       (K1_RSP_PRESENT | K1_RSP_CRC | K1_RSP_OPCODE)
#define K1_RSP_R1B      (K1_RSP_R1 | K1_RSP_BUSY)
#define K1_RSP_R2       (K1_RSP_PRESENT | K1_RSP_136 | K1_RSP_CRC)
#define K1_RSP_R3       K1_RSP_PRESENT
#define K1_RSP_R6       K1_RSP_R1
#define K1_RSP_R7       K1_RSP_R1

#define K1_SDHCI_POLL_LOOPS      4000000u
#define K1_SDHCI_DATA_POLL_LOOPS 16000000u

static uintptr_t g_k1_sdhci_base;
static uint32_t g_k1_sd_rca;
static uint8_t g_k1_sd_high_capacity;
static uint32_t g_k1_sd_total_blocks;

static inline uint8_t k1_readb(uint32_t reg)
{
    return mmio_read8(g_k1_sdhci_base + reg);
}

static inline uint16_t k1_readw(uint32_t reg)
{
    return mmio_read16(g_k1_sdhci_base + reg);
}

static inline uint32_t k1_readl(uint32_t reg)
{
    return mmio_read32(g_k1_sdhci_base + reg);
}

static inline void k1_writeb(uint32_t reg, uint8_t value)
{
    mmio_write8(g_k1_sdhci_base + reg, value);
}

static inline void k1_writew(uint32_t reg, uint16_t value)
{
    mmio_write16(g_k1_sdhci_base + reg, value);
}

static inline void k1_writel(uint32_t reg, uint32_t value)
{
    mmio_write32(g_k1_sdhci_base + reg, value);
}

static void k1_io_fence(void)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static void k1_short_delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++) { }
}

static void k1_log_error(const char *where, uint32_t status)
{
    uart_puts("[SDHCI-K1] ");
    uart_puts(where);
    uart_puts(" int_status=");
    uart_put_hex(status);
    uart_puts(" present=");
    uart_put_hex(k1_readl(SDHCI_PRESENT_STATE));
    uart_puts("\n");
}

static int k1_wait_present_clear(uint32_t mask, uint32_t loops)
{
    for (uint32_t i = 0; i < loops; i++) {
        if ((k1_readl(SDHCI_PRESENT_STATE) & mask) == 0u) return 0;
    }
    return -1;
}

static int k1_wait_interrupt(uint32_t wanted, uint32_t loops, uint32_t *status_out)
{
    for (uint32_t i = 0; i < loops; i++) {
        uint32_t status = k1_readl(SDHCI_INT_STATUS);
        if (status & (SDHCI_INT_ERROR | SDHCI_INT_ERROR_MASK)) {
            if (status_out) *status_out = status;
            return -2;
        }
        if (status & wanted) {
            if (status_out) *status_out = status;
            return 0;
        }
    }
    if (status_out) *status_out = k1_readl(SDHCI_INT_STATUS);
    return -1;
}

static int k1_reset_cmd_data(void)
{
    uint8_t mask = SDHCI_RESET_CMD | SDHCI_RESET_DATA;
    k1_writeb(SDHCI_SOFTWARE_RESET, mask);
    k1_io_fence();
    for (uint32_t i = 0; i < K1_SDHCI_POLL_LOOPS; i++) {
        if ((k1_readb(SDHCI_SOFTWARE_RESET) & mask) == 0u) return 0;
    }
    return -1;
}

static uint16_t k1_command_flags(uint32_t resp_flags, uint8_t data_present)
{
    uint16_t flags = 0;

    if (resp_flags & K1_RSP_PRESENT) {
        if (resp_flags & K1_RSP_136) flags |= SDHCI_CMD_RESP_LONG;
        else if (resp_flags & K1_RSP_BUSY) flags |= SDHCI_CMD_RESP_SHORT_BUSY;
        else flags |= SDHCI_CMD_RESP_SHORT;
    } else {
        flags |= SDHCI_CMD_RESP_NONE;
    }
    if (resp_flags & K1_RSP_CRC) flags |= SDHCI_CMD_CRC;
    if (resp_flags & K1_RSP_OPCODE) flags |= SDHCI_CMD_INDEX;
    if (data_present) flags |= SDHCI_CMD_DATA;
    return flags;
}

static int k1_send_command(uint32_t cmdidx, uint32_t arg, uint32_t resp_flags,
                           uint16_t transfer_mode, uint8_t data_present,
                           uint32_t *resp0)
{
    uint32_t inhibit = SDHCI_CMD_INHIBIT;
    uint32_t status = 0;
    uint16_t flags;

    if (data_present || (resp_flags & K1_RSP_BUSY)) inhibit |= SDHCI_DATA_INHIBIT;
    if (k1_wait_present_clear(inhibit, K1_SDHCI_POLL_LOOPS) != 0) {
        k1_log_error("command inhibit timeout", k1_readl(SDHCI_INT_STATUS));
        return -1;
    }

    k1_writel(SDHCI_INT_STATUS, SDHCI_INT_ALL);
    k1_writel(SDHCI_ARGUMENT, arg);
    flags = k1_command_flags(resp_flags, data_present);

    /* Transfer mode and command are adjacent and are written atomically. */
    k1_writel(SDHCI_TRANSFER_MODE,
              (uint32_t)transfer_mode | ((uint32_t)(cmdidx << 8 | flags) << 16));
    k1_io_fence();

    if (k1_wait_interrupt(SDHCI_INT_RESPONSE, K1_SDHCI_POLL_LOOPS, &status) != 0) {
        uart_puts("[SDHCI-K1] command failed cmd=");
        uart_put_dec_u64(cmdidx);
        uart_puts("\n");
        k1_log_error("command completion", status);
        return -2;
    }
    k1_writel(SDHCI_INT_STATUS, SDHCI_INT_RESPONSE);
    if (resp0) *resp0 = k1_readl(SDHCI_RESPONSE);
    return 0;
}

static uint32_t k1_extract_bits_128(const uint32_t r[4], uint32_t msb, uint32_t lsb)
{
    uint32_t ret = 0;
    uint32_t out = 0;
    for (uint32_t bit_num = lsb; bit_num <= msb; bit_num++) {
        uint32_t word = bit_num / 32u;
        uint32_t bit = bit_num % 32u;
        ret |= ((r[word] >> bit) & 1u) << out++;
    }
    return ret;
}

static int k1_read_csd(uint32_t csd[4])
{
    uint32_t raw[4];
    int rc = k1_send_command(MMC_CMD_SEND_CSD, g_k1_sd_rca << 16,
                             K1_RSP_R2, 0, 0, 0);
    if (rc != 0) return rc;

    /* SDHCI response registers are ordered from least to most significant. */
    raw[0] = k1_readl(SDHCI_RESPONSE + 0u);
    raw[1] = k1_readl(SDHCI_RESPONSE + 4u);
    raw[2] = k1_readl(SDHCI_RESPONSE + 8u);
    raw[3] = k1_readl(SDHCI_RESPONSE + 12u);

    /*
     * For R2 (136-bit) responses the controller strips the leading start
     * bit/transmission bit/command byte before storing it in the response
     * registers, so the raw value is the true CSD shifted right by 8 bits.
     * Reconstruct the real 128-bit CSD by shifting left 8 and pulling in
     * the top byte of the next-lower word (standard SDHCI R2 handling).
     */
    csd[3] = (raw[3] << 8) | (raw[2] >> 24);
    csd[2] = (raw[2] << 8) | (raw[1] >> 24);
    csd[1] = (raw[1] << 8) | (raw[0] >> 24);
    csd[0] = (raw[0] << 8);
    return 0;
}

int k1_sdhci_get_capacity_blocks(uint32_t *blocks_out)
{
    uint32_t csd[4] = {0, 0, 0, 0};
    uint32_t csd_structure;
    uint32_t c_size;
    uint64_t blocks;

    if (!blocks_out) return -1;
    if (g_k1_sd_total_blocks != 0u) {
        *blocks_out = g_k1_sd_total_blocks;
        return 0;
    }
    if (k1_read_csd(csd) != 0) return -2;

    csd_structure = k1_extract_bits_128(csd, 127, 126);
    c_size = k1_extract_bits_128(csd, 69, 48);
    uart_puts("[SDHCI-K1] csd raw=");
    uart_put_hex(csd[3]); uart_putc(' ');
    uart_put_hex(csd[2]); uart_putc(' ');
    uart_put_hex(csd[1]); uart_putc(' ');
    uart_put_hex(csd[0]); uart_puts("\n");
    uart_puts("[SDHCI-K1] csd_structure="); uart_put_dec_u64(csd_structure); uart_puts("\n");
    if (csd_structure != 1u) return -3;

    blocks = ((uint64_t)c_size + 1ULL) * 1024ULL;
    if (blocks == 0u || blocks > 0xffffffffULL) return -4;
    g_k1_sd_total_blocks = (uint32_t)blocks;
    *blocks_out = g_k1_sd_total_blocks;
    uart_puts("[SDHCI-K1] blocks="); uart_put_dec_u64(blocks); uart_puts("\n");
    return 0;
}

static int k1_protocol_init(void)
{
    uint32_t ocr = 0;
    uint32_t rca_response = 0;
    uint8_t host_control;

    /*
     * SPL may have left the link in 4-bit or 8-bit mode.  CMD0 returns the
     * card to its 1-bit reset state, so put the host in 1-bit mode first.
     */
    host_control = k1_readb(SDHCI_HOST_CONTROL);
    host_control &= (uint8_t)~((1u << 1) | (1u << 5));
    k1_writeb(SDHCI_HOST_CONTROL, host_control);

    /* CMD0 returns the SPL-selected card to the idle/identification state. */
    (void)k1_send_command(MMC_CMD_GO_IDLE_STATE, 0, K1_RSP_NONE, 0, 0, 0);
    k1_short_delay();
    (void)k1_send_command(SD_CMD_SEND_IF_COND, 0x1aau, K1_RSP_R7, 0, 0, 0);

    for (uint32_t attempt = 0; attempt < 2000u; attempt++) {
        if (k1_send_command(MMC_CMD_APP_CMD, 0, K1_RSP_R1, 0, 0, 0) != 0) continue;
        if (k1_send_command(SD_CMD_APP_SEND_OP_COND, 0x40ff8000u,
                            K1_RSP_R3, 0, 0, &ocr) != 0) continue;
        if (ocr & 0x80000000u) break;
        k1_short_delay();
    }
    if ((ocr & 0x80000000u) == 0u) return -1;
    g_k1_sd_high_capacity = (ocr & 0x40000000u) ? 1u : 0u;

    if (k1_send_command(MMC_CMD_ALL_SEND_CID, 0, K1_RSP_R2, 0, 0, 0) != 0) return -2;
    if (k1_send_command(MMC_CMD_SEND_RELATIVE_ADDR, 0, K1_RSP_R6,
                        0, 0, &rca_response) != 0) return -3;
    g_k1_sd_rca = (rca_response >> 16) & 0xffffu;
    if (g_k1_sd_rca == 0u) return -4;

    if (k1_sdhci_get_capacity_blocks(&g_k1_sd_total_blocks) != 0) return -5;
    if (k1_send_command(MMC_CMD_SELECT_CARD, g_k1_sd_rca << 16,
                        K1_RSP_R1B, 0, 0, 0) != 0) return -6;
    if (k1_wait_present_clear(SDHCI_DATA_INHIBIT,
                              K1_SDHCI_DATA_POLL_LOOPS) != 0) return -7;

    if (!g_k1_sd_high_capacity) {
        if (k1_send_command(MMC_CMD_SET_BLOCKLEN, SD_BLOCK_SIZE,
                            K1_RSP_R1, 0, 0, 0) != 0) return -8;
    }
    uart_puts("[SDHCI-K1] card ready rca="); uart_put_hex(g_k1_sd_rca);
    uart_puts(" high_capacity="); uart_put_dec_u64(g_k1_sd_high_capacity); uart_puts("\n");
    return 0;
}

static int k1_prepare_controller(void)
{
    uint16_t version = k1_readw(SDHCI_HOST_VERSION);
    uint32_t caps = k1_readl(SDHCI_CAPABILITIES);

    uart_puts("[SDHCI-K1] host_version="); uart_put_hex(version);
    uart_puts(" caps="); uart_put_hex(caps);
    uart_puts(" clock="); uart_put_hex(k1_readw(SDHCI_CLOCK_CONTROL));
    uart_puts(" power="); uart_put_hex(k1_readb(SDHCI_POWER_CONTROL));
    uart_puts("\n");
    if ((version == 0u || version == 0xffffu) && (caps == 0u || caps == 0xffffffffu)) return -1;

    /* SPL already enabled clocks, power, reset lines and the K1 PHY. */
    k1_writel(SDHCI_SIGNAL_ENABLE, 0u);
    k1_writel(SDHCI_INT_ENABLE, SDHCI_INT_ALL);
    k1_writel(SDHCI_INT_STATUS, SDHCI_INT_ALL);
    k1_writeb(SDHCI_TIMEOUT_CONTROL, 0x0eu);
    if (k1_reset_cmd_data() != 0) return -2;
    k1_io_fence();
    return 0;
}

void k1_sdhci_reset_state(uintptr_t base)
{
    g_k1_sdhci_base = base;
    g_k1_sd_rca = 0;
    g_k1_sd_high_capacity = 0;
    g_k1_sd_total_blocks = 0;
}

int k1_sdhci_attach_from_spl(void)
{
    int rc;
    uart_puts("[SDHCI-K1] attach: reuse SPL clocks/PHY, reidentify card\n");
    rc = k1_prepare_controller();
    if (rc != 0) return -10 + rc;
    rc = k1_protocol_init();
    if (rc != 0) return -20 + rc;
    return 0;
}

int k1_sdhci_card_init(void)
{
    int rc;
    uart_puts("[SDHCI-K1] init: command/data reset, preserve SPL PHY\n");
    rc = k1_prepare_controller();
    if (rc != 0) return -10 + rc;
    rc = k1_protocol_init();
    if (rc != 0) return -20 + rc;
    return 0;
}

int k1_sdhci_read_block_words(uint32_t lba, uint32_t *dst_words)
{
    uint32_t status = 0;
    uint32_t arg;
    uint32_t words_left = SD_BLOCK_SIZE / sizeof(uint32_t);

    if (!dst_words) return -1;
    k1_writew(SDHCI_BLOCK_SIZE, (uint16_t)SD_BLOCK_SIZE);
    k1_writew(SDHCI_BLOCK_COUNT, 1u);
    arg = g_k1_sd_high_capacity ? lba : (lba * SD_BLOCK_SIZE);
    if (k1_send_command(MMC_CMD_READ_SINGLE_BLOCK, arg, K1_RSP_R1,
                        SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN, 1, 0) != 0) return -2;

    if (k1_wait_interrupt(SDHCI_INT_DATA_AVAIL, K1_SDHCI_DATA_POLL_LOOPS, &status) != 0) {
        k1_log_error("read buffer ready", status);
        return -3;
    }
    k1_writel(SDHCI_INT_STATUS, SDHCI_INT_DATA_AVAIL);
    while (words_left != 0u) {
        *dst_words++ = k1_readl(SDHCI_BUFFER);
        words_left--;
    }
    if (k1_wait_interrupt(SDHCI_INT_DATA_END, K1_SDHCI_DATA_POLL_LOOPS, &status) != 0) {
        k1_log_error("read transfer complete", status);
        return -4;
    }
    k1_writel(SDHCI_INT_STATUS, SDHCI_INT_DATA_END);
    return 0;
}

int k1_sdhci_write_block_words(uint32_t lba, const uint32_t *src_words)
{
    uint32_t status = 0;
    uint32_t arg;
    uint32_t words_left = SD_BLOCK_SIZE / sizeof(uint32_t);

    if (!src_words) return -1;
    k1_writew(SDHCI_BLOCK_SIZE, (uint16_t)SD_BLOCK_SIZE);
    k1_writew(SDHCI_BLOCK_COUNT, 1u);
    arg = g_k1_sd_high_capacity ? lba : (lba * SD_BLOCK_SIZE);
    if (k1_send_command(MMC_CMD_WRITE_SINGLE_BLOCK, arg, K1_RSP_R1,
                        SDHCI_TRNS_BLK_CNT_EN, 1, 0) != 0) return -2;

    if (k1_wait_interrupt(SDHCI_INT_SPACE_AVAIL, K1_SDHCI_DATA_POLL_LOOPS, &status) != 0) {
        k1_log_error("write buffer ready", status);
        return -3;
    }
    k1_writel(SDHCI_INT_STATUS, SDHCI_INT_SPACE_AVAIL);
    while (words_left != 0u) {
        k1_writel(SDHCI_BUFFER, *src_words++);
        words_left--;
    }
    if (k1_wait_interrupt(SDHCI_INT_DATA_END, K1_SDHCI_DATA_POLL_LOOPS, &status) != 0) {
        k1_log_error("write transfer complete", status);
        return -4;
    }
    k1_writel(SDHCI_INT_STATUS, SDHCI_INT_DATA_END);
    if (k1_wait_present_clear(SDHCI_DATA_INHIBIT, K1_SDHCI_DATA_POLL_LOOPS) != 0) return -5;
    return 0;
}

void k1_sdhci_quiesce(void)
{
    if (g_k1_sdhci_base == 0u) return;
    (void)k1_wait_present_clear(SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT,
                                K1_SDHCI_DATA_POLL_LOOPS);
    k1_writel(SDHCI_SIGNAL_ENABLE, 0u);
    k1_writel(SDHCI_INT_ENABLE, 0u);
    k1_writel(SDHCI_INT_STATUS, SDHCI_INT_ALL);
    k1_io_fence();
    uart_puts("[SDHCI-K1] quiesce done\n");
}

#endif
