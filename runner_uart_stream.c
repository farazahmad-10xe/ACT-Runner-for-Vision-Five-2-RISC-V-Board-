// SPDX-License-Identifier: BSD-3-Clause
// Optional one-ELF UART transport. Disabled unless RUNNER_UART_STREAM=1.

#include "runner_shared.h"

#if RUNNER_UART_STREAM

_Static_assert(EXT_PACK_ADDR >= BOARD_RAM_BASE,
               "UART receive buffer starts below board RAM");
_Static_assert(EXT_PACK_ADDR < BOARD_RAM_LIMIT,
               "UART receive buffer starts beyond board RAM");
_Static_assert(EXT_PACK_MAX_BYTES <= BOARD_RAM_LIMIT - EXT_PACK_ADDR,
               "UART receive buffer exceeds board RAM");

static char g_uart_stream_name[UART_STREAM_NAME_BYTES + 1u];
static volatile uint32_t g_uart_stream_done_emitted;

void uart_stream_emit_done(const char *name, const char *status, uint64_t tohost)
{
    if (g_uart_stream_done_emitted) return;
    g_uart_stream_done_emitted = 1u;
    asm volatile ("fence rw, rw" ::: "memory");

    uart_puts("[UART_STREAM] DONE name=");
    uart_puts(name);
    uart_puts(" status=");
    uart_puts(status);
    uart_puts(" tohost=");
    uart_put_hex(tohost);
    uart_puts("\n");
}

static int uart_getc_timeout(uint8_t *out)
{
    uint64_t deadline = *mtime_ptr() + RUNNER_UART_RX_TIMEOUT_TICKS;
#if BOARD_UART_REG_IO_WIDTH == 4
    while ((mmio_read32(UART_BASE + UART_LSR) & UART_LSR_DR) == 0u) {
        if ((int64_t)(*mtime_ptr() - deadline) >= 0) return -1;
        cpu_relax();
    }
    *out = (uint8_t)mmio_read32(UART_BASE + UART_RBR);
#elif BOARD_UART_REG_IO_WIDTH == 1
    while ((mmio_read8(UART_BASE + UART_LSR) & UART_LSR_DR) == 0u) {
        if ((int64_t)(*mtime_ptr() - deadline) >= 0) return -1;
        cpu_relax();
    }
    *out = mmio_read8(UART_BASE + UART_RBR);
#else
#error "Unsupported BOARD_UART_REG_IO_WIDTH"
#endif
    return 0;
}

static int uart_receive_exact(uint8_t *dst, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        if (uart_getc_timeout(&dst[i]) != 0) return -1;
    }
    return 0;
}

static uint32_t crc32_update_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint32_t bit = 0; bit < 8u; bit++) {
        uint32_t mask = 0u - (crc & 1u);
        crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
    return crc;
}

static int receive_header(UartStreamHeader *header)
{
    if (uart_receive_exact((uint8_t *)(void *)header, sizeof(*header)) != 0) {
        uart_puts("[UART_STREAM] ERROR reason=rx_timeout phase=header\n");
        return -5;
    }

    if (header->magic != UART_STREAM_MAGIC) {
        uart_puts("[UART_STREAM] ERROR reason=bad_magic value=");
        uart_put_hex(header->magic);
        uart_puts("\n");
        return -1;
    }
    if (header->version != UART_STREAM_VERSION) {
        uart_puts("[UART_STREAM] ERROR reason=bad_version value=");
        uart_put_hex(header->version);
        uart_puts("\n");
        return -2;
    }
    if (header->elf_size < sizeof(Elf64_Ehdr) || header->elf_size > EXT_PACK_MAX_BYTES) {
        uart_puts("[UART_STREAM] ERROR reason=bad_size value=");
        uart_put_hex(header->elf_size);
        uart_puts(" max=");
        uart_put_hex(EXT_PACK_MAX_BYTES);
        uart_puts("\n");
        return -3;
    }
    if (header->name_len == 0u || header->name_len > UART_STREAM_NAME_BYTES) {
        uart_puts("[UART_STREAM] ERROR reason=bad_name_len value=");
        uart_put_hex(header->name_len);
        uart_puts("\n");
        return -4;
    }

    for (uint32_t i = 0; i < header->name_len; i++) {
        char c = header->name[i];
        g_uart_stream_name[i] = (c >= 0x20 && c <= 0x7e) ? c : '_';
    }
    g_uart_stream_name[header->name_len] = '\0';
    return 0;
}

int run_uart_stream_once(uint64_t *total, uint64_t *pass, uint64_t *fail)
{
    UartStreamHeader header;
    uint8_t *dst = (uint8_t *)(uintptr_t)EXT_PACK_ADDR;
    uint32_t crc = 0xffffffffu;
    int rc;
    TestResult tr;

    g_uart_stream_done_emitted = 0u;

    uart_puts("[UART_STREAM] READY version=");
    uart_put_dec_u64(UART_STREAM_VERSION);
    uart_puts(" header_bytes=");
    uart_put_dec_u64(sizeof(UartStreamHeader));
    uart_puts(" max_elf_bytes=");
    uart_put_dec_u64(EXT_PACK_MAX_BYTES);
    uart_puts(" buffer=");
    uart_put_hex(EXT_PACK_ADDR);
    uart_puts(" board=");
    uart_puts(RUNNER_PLATFORM_NAME);
    uart_puts(" runner_build=");
    uart_puts(RUNNER_BUILD_ID);
    uart_puts("\n");

    rc = receive_header(&header);
    if (rc != 0) return rc;

    uart_puts("[UART_STREAM] HEADER_OK name=");
    uart_puts(g_uart_stream_name);
    uart_puts(" size=");
    uart_put_dec_u64(header.elf_size);
    uart_puts(" crc32=");
    uart_put_hex(header.crc32);
    uart_puts("\n");

    for (uint64_t i = 0; i < header.elf_size; i++) {
        uint8_t byte;
        if (uart_getc_timeout(&byte) != 0) {
            uart_puts("[UART_STREAM] ERROR reason=rx_timeout phase=payload offset=");
            uart_put_dec_u64(i);
            uart_puts("\n");
            return -6;
        }
        dst[i] = byte;
        crc = crc32_update_byte(crc, byte);
    }
    crc ^= 0xffffffffu;
    asm volatile ("fence rw, rw" ::: "memory");

    if (crc != header.crc32) {
        uart_puts("[UART_STREAM] ERROR reason=crc_mismatch expected=");
        uart_put_hex(header.crc32);
        uart_puts(" actual=");
        uart_put_hex(crc);
        uart_puts("\n");
        return -5;
    }

    uart_puts("[UART_STREAM] RX_OK name=");
    uart_puts(g_uart_stream_name);
    uart_puts(" size=");
    uart_put_dec_u64(header.elf_size);
    uart_puts(" crc32=");
    uart_put_hex(crc);
    uart_puts("\n");

    (void)run_one_blob(g_uart_stream_name, dst, (size_t)header.elf_size, &tr);
    *total += 1u;
    if (case_is_pass(&tr)) *pass += 1u;
    else *fail += 1u;

    if (tr.status == CASE_STATUS_PASS) {
        uart_stream_emit_done(g_uart_stream_name, "PASS", tr.tohost);
    } else if (tr.status == CASE_STATUS_TIMEOUT) {
        uart_stream_emit_done(g_uart_stream_name, "TIMEOUT", tr.tohost);
    } else if (tr.status == CASE_STATUS_FAIL) {
        uart_stream_emit_done(g_uart_stream_name, "FAIL", tr.tohost);
    } else {
        uart_stream_emit_done(g_uart_stream_name, "ERROR", tr.tohost);
    }
    return 0;
}

#else

int run_uart_stream_once(uint64_t *total, uint64_t *pass, uint64_t *fail)
{
    (void)total;
    (void)pass;
    (void)fail;
    return -1;
}

void uart_stream_emit_done(const char *name, const char *status, uint64_t tohost)
{
    (void)name;
    (void)status;
    (void)tohost;
}

#endif
