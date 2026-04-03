// Core platform, debug, ELF helpers extracted from main_act.c.

#include "runner_shared.h"

int is_valid_ddr_addr(uint64_t addr)
{
    return (addr >= 0x40000000ULL && addr < 0x100000000ULL);
}

int is_valid_ddr_range(uint64_t begin, uint64_t end)
{
    if (begin == 0 || end == 0 || end <= begin) return 0;
    if ((begin & 0x3ULL) != 0 || (end & 0x3ULL) != 0) return 0;
    if (!is_valid_ddr_addr(begin) || !is_valid_ddr_addr(end - 1)) return 0;
    if ((end - begin) > 0x200000ULL) return 0;
    return 1;
}

void *memcpy_local(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memset_local(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint8_t v = (uint8_t)c;
    for (size_t i = 0; i < n; i++) d[i] = v;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) { return memcpy_local(dst, src, n); }
void *memset(void *dst, int c, size_t n) { return memset_local(dst, c, n); }

static uint32_t patch_wfi_to_ecall(uint64_t begin, uint64_t size)
{
    if (size < 4) return 0;
    if (!is_valid_ddr_range(begin, begin + size)) return 0;

    const uint32_t WFI_INSN = 0x10500073u;
    const uint32_t ECALL_INSN = 0x00000073u;
    uint32_t patched = 0;
    uint64_t end = begin + size;

    for (uint64_t a = begin; a + 4 <= end; a += 4) {
        volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)a;
        if (*p == WFI_INSN) {
            *p = ECALL_INSN;
            patched++;
        }
    }
    return patched;
}

void uart_putc(char c)
{
    while ((mmio_read8(UART_BASE + UART_LSR) & UART_LSR_THRE) == 0) {}
    mmio_write8(UART_BASE + UART_THR, (uint8_t)c);
}

void uart_puts(const char* s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_put_hex(uint64_t x)
{
    const char* h = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 15; i >= 0; --i) uart_putc(h[(x >> (i * 4)) & 0xF]);
}

void uart_put_dec_u64(uint64_t v)
{
    char buf[24];
    int i = 0;
    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v != 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i-- > 0) uart_putc(buf[i]);
}

void dbg_putc(char c) { if (c == '\n') uart_putc('\r'); uart_putc(c); }
void dbg_puts(const char *s) { while (*s) dbg_putc(*s++); }

void dbg_hex_u64(uint64_t v)
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 15; i >= 0; --i) dbg_putc(hex[(v >> (i * 4)) & 0xF]);
}

void dbg_hex_u32(uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 7; i >= 0; --i) dbg_putc(hex[(v >> (i * 4)) & 0xF]);
}

void dbg_nl(void) { dbg_putc('\n'); }

void dbg_put_u32_dec(uint32_t v)
{
    char buf[12];
    int i = 0;
    if (v == 0) {
        dbg_putc('0');
        return;
    }
    while (v != 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i-- > 0) dbg_putc(buf[i]);
}

void dbg_put_reg_name(uint32_t reg)
{
    static const char *const names[32] = {
        "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",
        "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x31"
    };
    if (reg < 32u) dbg_puts(names[reg]);
    else {
        dbg_putc('x');
        dbg_put_u32_dec(reg);
    }
}

int32_t sign_extend_32(uint32_t v, uint32_t bits)
{
    uint32_t shift = 32u - bits;
    return (int32_t)(v << shift) >> shift;
}

void dbg_put_imm_i32(int32_t v)
{
    if (v < 0) {
        dbg_putc('-');
        dbg_puts("0x");
        dbg_hex_u32((uint32_t)(-v));
    } else {
        dbg_puts("0x");
        dbg_hex_u32((uint32_t)v);
    }
}

void decode_riscv_insn(uint64_t pc, uint32_t insn)
{
    uint32_t opcode = insn & 0x7fu;
    uint32_t rd = (insn >> 7) & 0x1fu;
    uint32_t funct3 = (insn >> 12) & 0x7u;
    uint32_t rs1 = (insn >> 15) & 0x1fu;
    uint32_t rs2 = (insn >> 20) & 0x1fu;
    uint32_t funct7 = (insn >> 25) & 0x7fu;
    int32_t imm_i = sign_extend_32(insn >> 20, 12u);
    int32_t imm_s = sign_extend_32(((insn >> 25) << 5) | ((insn >> 7) & 0x1fu), 12u);
    int32_t imm_b = sign_extend_32(
        (((insn >> 31) & 0x1u) << 12) |
        (((insn >> 7) & 0x1u) << 11) |
        (((insn >> 25) & 0x3fu) << 5) |
        (((insn >> 8) & 0xfu) << 1), 13u);
    int32_t imm_u = (int32_t)(insn & 0xfffff000u);
    int32_t imm_j = sign_extend_32(
        (((insn >> 31) & 0x1u) << 20) |
        (((insn >> 12) & 0xffu) << 12) |
        (((insn >> 20) & 0x1u) << 11) |
        (((insn >> 21) & 0x3ffu) << 1), 21u);

    switch (opcode) {
        case 0x17u:
            dbg_puts("auipc ");
            dbg_put_reg_name(rd);
            dbg_puts(", ");
            dbg_put_imm_i32(imm_u >> 12);
            return;
        case 0x37u:
            dbg_puts("lui ");
            dbg_put_reg_name(rd);
            dbg_puts(", ");
            dbg_put_imm_i32(imm_u >> 12);
            return;
        case 0x6fu:
            dbg_puts("jal ");
            dbg_put_reg_name(rd);
            dbg_puts(", 0x");
            dbg_hex_u64((uint64_t)(pc + (int64_t)imm_j));
            return;
        case 0x67u:
            if (funct3 == 0u) {
                dbg_puts("jalr ");
                dbg_put_reg_name(rd);
                dbg_puts(", ");
                dbg_put_imm_i32(imm_i);
                dbg_puts("(");
                dbg_put_reg_name(rs1);
                dbg_puts(")");
                return;
            }
            break;
        case 0x63u:
            switch (funct3) {
                case 0u: dbg_puts("beq "); break;
                case 1u: dbg_puts("bne "); break;
                case 4u: dbg_puts("blt "); break;
                case 5u: dbg_puts("bge "); break;
                case 6u: dbg_puts("bltu "); break;
                case 7u: dbg_puts("bgeu "); break;
                default: dbg_puts("branch? "); break;
            }
            dbg_put_reg_name(rs1);
            dbg_puts(", ");
            dbg_put_reg_name(rs2);
            dbg_puts(", 0x");
            dbg_hex_u64((uint64_t)(pc + (int64_t)imm_b));
            return;
        case 0x03u:
            switch (funct3) {
                case 0u: dbg_puts("lb "); break;
                case 1u: dbg_puts("lh "); break;
                case 2u: dbg_puts("lw "); break;
                case 3u: dbg_puts("ld "); break;
                case 4u: dbg_puts("lbu "); break;
                case 5u: dbg_puts("lhu "); break;
                case 6u: dbg_puts("lwu "); break;
                default: dbg_puts("load? "); break;
            }
            dbg_put_reg_name(rd);
            dbg_puts(", ");
            dbg_put_imm_i32(imm_i);
            dbg_puts("(");
            dbg_put_reg_name(rs1);
            dbg_puts(")");
            return;
        case 0x23u:
            switch (funct3) {
                case 0u: dbg_puts("sb "); break;
                case 1u: dbg_puts("sh "); break;
                case 2u: dbg_puts("sw "); break;
                case 3u: dbg_puts("sd "); break;
                default: dbg_puts("store? "); break;
            }
            dbg_put_reg_name(rs2);
            dbg_puts(", ");
            dbg_put_imm_i32(imm_s);
            dbg_puts("(");
            dbg_put_reg_name(rs1);
            dbg_puts(")");
            return;
        case 0x13u:
            switch (funct3) {
                case 0u: dbg_puts("addi "); break;
                case 2u: dbg_puts("slti "); break;
                case 3u: dbg_puts("sltiu "); break;
                case 4u: dbg_puts("xori "); break;
                case 6u: dbg_puts("ori "); break;
                case 7u: dbg_puts("andi "); break;
                case 1u: dbg_puts("slli "); break;
                case 5u:
                    dbg_puts((funct7 == 0x20u) ? "srai " : "srli ");
                    break;
                default: dbg_puts("op-imm? "); break;
            }
            dbg_put_reg_name(rd);
            dbg_puts(", ");
            dbg_put_reg_name(rs1);
            dbg_puts(", ");
            dbg_put_imm_i32(imm_i);
            return;
        case 0x33u:
            if (funct7 == 0x00u && funct3 == 0u) dbg_puts("add ");
            else if (funct7 == 0x20u && funct3 == 0u) dbg_puts("sub ");
            else if (funct7 == 0x00u && funct3 == 1u) dbg_puts("sll ");
            else if (funct7 == 0x00u && funct3 == 2u) dbg_puts("slt ");
            else if (funct7 == 0x00u && funct3 == 3u) dbg_puts("sltu ");
            else if (funct7 == 0x00u && funct3 == 4u) dbg_puts("xor ");
            else if (funct7 == 0x00u && funct3 == 5u) dbg_puts("srl ");
            else if (funct7 == 0x20u && funct3 == 5u) dbg_puts("sra ");
            else if (funct7 == 0x00u && funct3 == 6u) dbg_puts("or ");
            else if (funct7 == 0x00u && funct3 == 7u) dbg_puts("and ");
            else dbg_puts("op? ");
            dbg_put_reg_name(rd);
            dbg_puts(", ");
            dbg_put_reg_name(rs1);
            dbg_puts(", ");
            dbg_put_reg_name(rs2);
            return;
        case 0x73u:
            if (insn == 0x00000073u) dbg_puts("ecall");
            else if (insn == 0x00100073u) dbg_puts("ebreak");
            else if (insn == 0x10500073u) dbg_puts("wfi");
            else dbg_puts("system");
            return;
        default:
            break;
    }

    dbg_puts("unknown");
}

void dump_words32(uint64_t addr)
{
    const int before_words = 10;
    const int after_words = 6;
    uint64_t aligned = addr & ~0x3ULL;
    uint64_t start = aligned;
    if (aligned >= ((uint64_t)before_words * 4ULL)) {
        start = aligned - ((uint64_t)before_words * 4ULL);
    }

    dbg_puts("[MEMDUMP] around 0x"); dbg_hex_u64(addr); dbg_nl();
    for (int i = 0; i < (before_words + 1 + after_words); i++) {
        uint64_t a = start + ((uint64_t)i * 4ULL);
        if (!is_valid_ddr_addr(a) || !is_valid_ddr_addr(a + 3ULL)) break;
        uint32_t w = *(volatile const uint32_t *)(uintptr_t)a;
        dbg_puts((a == aligned) ? "=> 0x" : "   0x"); dbg_hex_u64(a);
        dbg_puts(" : 0x"); dbg_hex_u32(w);
        dbg_puts("  ");
        decode_riscv_insn(a, w);
        dbg_nl();
    }
}

void uart_put_token_str(const char *s)
{
    if (!s || !s[0]) {
        uart_puts("unknown");
        return;
    }
    while (*s) {
        char c = *s++;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') c = '_';
        uart_putc(c);
    }
}

void emit_reg_line4(const char *n0, uint64_t v0,
                    const char *n1, uint64_t v1,
                    const char *n2, uint64_t v2,
                    const char *n3, uint64_t v3)
{
    uart_puts("[REG] ");
    uart_puts(n0); uart_puts("="); uart_put_hex(v0);
    uart_puts(" ");
    uart_puts(n1); uart_puts("="); uart_put_hex(v1);
    uart_puts(" ");
    uart_puts(n2); uart_puts("="); uart_put_hex(v2);
    uart_puts(" ");
    uart_puts(n3); uart_puts("="); uart_put_hex(v3);
    uart_puts("\n");
}

void emit_reg_line3(const char *n0, uint64_t v0,
                    const char *n1, uint64_t v1,
                    const char *n2, uint64_t v2)
{
    uart_puts("[REG] ");
    uart_puts(n0); uart_puts("="); uart_put_hex(v0);
    uart_puts(" ");
    uart_puts(n1); uart_puts("="); uart_put_hex(v1);
    uart_puts(" ");
    uart_puts(n2); uart_puts("="); uart_put_hex(v2);
    uart_puts("\n");
}

void trigger_watchdog_reset(void)
{
    if (g_reset_armed) return;
    g_reset_armed = 1;
    for (int t = 0; t < 3; t++) {
        mmio_write32(JH7110_WDT_BASE + JH7110_WDT_LOCK, JH7110_WDT_UNLOCK_KEY);
        mmio_write32(JH7110_WDT_BASE + JH7110_WDT_LOAD, 1u);
        mmio_write32(JH7110_WDT_BASE + JH7110_WDT_CTRL, 0x3u);
        mmio_write32(JH7110_WDT_BASE + JH7110_WDT_LOCK, 0u);
    }
    uart_puts("[RST] watchdog armed\n");
}

void monitor_irq_enable(void)
{
    const uint64_t MSTATUS_MIE = (1ULL << 3);
    const uint64_t MIE_MSIE = (1ULL << 3);
    const uint64_t MIE_MTIE = (1ULL << 7);
    write_mie(read_mie() | MIE_MSIE | MIE_MTIE);
    write_mstatus(read_mstatus() | MSTATUS_MIE);
}

void monitor_irq_disable(void)
{
    const uint64_t MSTATUS_MIE = (1ULL << 3);
    const uint64_t MIE_MSIE = (1ULL << 3);
    const uint64_t MIE_MTIE = (1ULL << 7);
    write_mie(read_mie() & ~(MIE_MSIE | MIE_MTIE));
    write_mstatus(read_mstatus() & ~MSTATUS_MIE);
}

void enable_fpu_set_fs_only(void)
{
    const uint64_t MSTATUS_FS_MASK  = (3ULL << 13);
    const uint64_t MSTATUS_FS_DIRTY = (3ULL << 13);
    uint64_t ms = read_mstatus();
    ms &= ~MSTATUS_FS_MASK;
    ms |= MSTATUS_FS_DIRTY;
    write_mstatus(ms);
}

void enable_fpu_try_clear_fcsr(void)
{
    uint64_t ms  = read_mstatus();
    uint64_t fs  = (ms >> 13) & 0x3;
    uint64_t isa = read_misa();
    uint64_t hasF = (isa >> 5) & 1;

    if (fs == 0 || hasF == 0) return;
    write_fcsr(0);
}

int streq(const char* a, const char* b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == 0 && *b == 0);
}

static int find_symbol_by_name(const uint8_t *blob, size_t blob_size, const Elf64_Ehdr *eh,
                               const char *target, uint64_t *value_out)
{
    if (value_out) *value_out = 0;
    if (!blob || !eh || !target || !value_out) return -1;

    uint64_t sh_end = eh->e_shoff + ((uint64_t)eh->e_shnum * (uint64_t)eh->e_shentsize);
    if (eh->e_shoff == 0 || eh->e_shentsize != sizeof(Elf64_Shdr)) return -2;
    if (eh->e_shoff > blob_size || sh_end > blob_size || sh_end < eh->e_shoff) return -3;

    const Elf64_Shdr *sh = (const Elf64_Shdr *)(const void *)(blob + eh->e_shoff);
    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        if (!(sh[i].sh_type == SHT_SYMTAB || sh[i].sh_type == SHT_DYNSYM)) continue;
        if (sh[i].sh_entsize != sizeof(Elf64_Sym) || sh[i].sh_entsize == 0) continue;
        if (sh[i].sh_link >= eh->e_shnum) continue;
        if (sh[i].sh_offset > blob_size || (sh[i].sh_offset + sh[i].sh_size) > blob_size) continue;

        const Elf64_Shdr *strsec = &sh[sh[i].sh_link];
        if (strsec->sh_offset > blob_size || (strsec->sh_offset + strsec->sh_size) > blob_size) continue;
        const char *strtab = (const char *)(const void *)(blob + strsec->sh_offset);

        const Elf64_Sym *sym = (const Elf64_Sym *)(const void *)(blob + sh[i].sh_offset);
        uint64_t n = sh[i].sh_size / sh[i].sh_entsize;
        for (uint64_t s = 0; s < n; s++) {
            if (sym[s].st_name >= strsec->sh_size) continue;
            if (!streq(strtab + sym[s].st_name, target)) continue;
            *value_out = sym[s].st_value;
            return 0;
        }
    }

    return -4;
}

int find_symbol_for_addr(const uint8_t *blob, size_t blob_size, uint64_t addr, SymbolInfo *out)
{
    if (!out) return -1;
    out->name = 0;
    out->value = 0;
    out->size = 0;

    if (!blob || blob_size < sizeof(Elf64_Ehdr)) return -2;

    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)(const void *)blob;
    uint64_t sh_end = eh->e_shoff + ((uint64_t)eh->e_shnum * (uint64_t)eh->e_shentsize);
    if (eh->e_shoff == 0 || eh->e_shentsize != sizeof(Elf64_Shdr)) return -3;
    if (eh->e_shoff > blob_size || sh_end > blob_size || sh_end < eh->e_shoff) return -4;

    const Elf64_Shdr *sh = (const Elf64_Shdr *)(const void *)(blob + eh->e_shoff);
    uint64_t best_delta = ~0ULL;

    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        if (!(sh[i].sh_type == SHT_SYMTAB || sh[i].sh_type == SHT_DYNSYM)) continue;
        if (sh[i].sh_entsize != sizeof(Elf64_Sym) || sh[i].sh_entsize == 0) continue;
        if (sh[i].sh_link >= eh->e_shnum) continue;
        if (sh[i].sh_offset > blob_size || (sh[i].sh_offset + sh[i].sh_size) > blob_size) continue;

        const Elf64_Shdr *strsec = &sh[sh[i].sh_link];
        if (strsec->sh_offset > blob_size || (strsec->sh_offset + strsec->sh_size) > blob_size) continue;

        const char *strtab = (const char *)(const void *)(blob + strsec->sh_offset);
        const Elf64_Sym *sym = (const Elf64_Sym *)(const void *)(blob + sh[i].sh_offset);
        uint64_t n = sh[i].sh_size / sh[i].sh_entsize;
        for (uint64_t s = 0; s < n; s++) {
            if (sym[s].st_name >= strsec->sh_size) continue;
            if (sym[s].st_value == 0 || sym[s].st_value > addr) continue;

            uint64_t delta = addr - sym[s].st_value;
            if (sym[s].st_size != 0 && delta >= sym[s].st_size) continue;
            if (delta > best_delta) continue;

            best_delta = delta;
            out->name = strtab + sym[s].st_name;
            out->value = sym[s].st_value;
            out->size = sym[s].st_size;
        }
    }

    return out->name ? 0 : -5;
}

void capture_last_trap(const TrapFrame *tf, uint64_t mcause, uint64_t mepc,
                       uint64_t mtval, uint64_t mstatus)
{
    if (tf) g_last_trap_frame = *tf;
    else memset_local(&g_last_trap_frame, 0, sizeof(g_last_trap_frame));
    g_last_trap_mcause = mcause;
    g_last_trap_mepc = mepc;
    g_last_trap_mtval = mtval;
    g_last_trap_mstatus = mstatus;
    g_last_trap_valid = 1u;
}

const char *trap_reason_name(uint64_t mcause)
{
    if (mcause & MCAUSE_INTERRUPT_BIT) {
        switch (mcause) {
            case MCAUSE_MSI: return "machine_software_interrupt";
            case MCAUSE_MTI: return "machine_timer_interrupt";
            default: return "interrupt_other";
        }
    }

    switch (mcause) {
        case MCAUSE_INST_MISALIGNED: return "instruction_misaligned";
        case MCAUSE_LOAD_MISALIGNED: return "load_misaligned";
        case MCAUSE_STORE_MISALIGNED: return "store_misaligned";
        case MCAUSE_ECALL_M: return "ecall_from_mmode";
        default: return "exception_other";
    }
}

uint32_t read_insn_word(uint64_t pc, int *valid_out)
{
    if (valid_out) *valid_out = 0;
    if ((pc & 0x3ULL) != 0) return 0;
    if (!is_valid_ddr_addr(pc) || !is_valid_ddr_addr(pc + 3)) return 0;

    if (valid_out) *valid_out = 1;
    return *(volatile const uint32_t *)(uintptr_t)pc;
}

static uint64_t find_tohost_addr(const uint8_t* blob, size_t blob_size, const Elf64_Ehdr* eh)
{
    uint64_t sh_end = eh->e_shoff + ((uint64_t)eh->e_shnum * (uint64_t)eh->e_shentsize);
    if (eh->e_shoff == 0 || eh->e_shentsize != sizeof(Elf64_Shdr)) return 0;
    if (eh->e_shoff > blob_size || sh_end > blob_size || sh_end < eh->e_shoff) return 0;
    if (eh->e_shstrndx >= eh->e_shnum) return 0;

    const Elf64_Shdr* sh = (const Elf64_Shdr*)(const void*)(blob + eh->e_shoff);
    const Elf64_Shdr* shstr = &sh[eh->e_shstrndx];

    if (shstr->sh_offset > blob_size || (shstr->sh_offset + shstr->sh_size) > blob_size) return 0;
    const char* strtab = (const char*)(const void*)(blob + shstr->sh_offset);

    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_name >= shstr->sh_size) continue;
        const char* name = strtab + sh[i].sh_name;
        if (streq(name, ".tohost")) return sh[i].sh_addr;
    }
    return 0;
}

void find_signature_range(const uint8_t* blob, size_t blob_size, const Elf64_Ehdr* eh,
                          uint64_t *sig_begin_out, uint64_t *sig_end_out)
{
    *sig_begin_out = 0;
    *sig_end_out = 0;

    uint64_t sh_end = eh->e_shoff + ((uint64_t)eh->e_shnum * (uint64_t)eh->e_shentsize);
    if (eh->e_shoff == 0 || eh->e_shentsize != sizeof(Elf64_Shdr)) return;
    if (eh->e_shoff > blob_size || sh_end > blob_size || sh_end < eh->e_shoff) return;

    const Elf64_Shdr* sh = (const Elf64_Shdr*)(const void*)(blob + eh->e_shoff);
    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        if (!(sh[i].sh_type == SHT_SYMTAB || sh[i].sh_type == SHT_DYNSYM)) continue;
        if (sh[i].sh_entsize != sizeof(Elf64_Sym) || sh[i].sh_entsize == 0) continue;
        if (sh[i].sh_link >= eh->e_shnum) continue;
        if (sh[i].sh_offset > blob_size || (sh[i].sh_offset + sh[i].sh_size) > blob_size) continue;

        const Elf64_Shdr* strsec = &sh[sh[i].sh_link];
        if (strsec->sh_offset > blob_size || (strsec->sh_offset + strsec->sh_size) > blob_size) continue;
        const char* strtab = (const char*)(const void*)(blob + strsec->sh_offset);

        const Elf64_Sym* sym = (const Elf64_Sym*)(const void*)(blob + sh[i].sh_offset);
        uint64_t n = sh[i].sh_size / sh[i].sh_entsize;
        for (uint64_t s = 0; s < n; s++) {
            if (sym[s].st_name >= strsec->sh_size) continue;
            const char* name = strtab + sym[s].st_name;
            if (streq(name, "begin_signature")) *sig_begin_out = sym[s].st_value;
            else if (streq(name, "end_signature")) *sig_end_out = sym[s].st_value;
            if (*sig_begin_out && *sig_end_out) return;
        }
    }
}

void find_failure_scratch_range(const uint8_t *blob, size_t blob_size, const Elf64_Ehdr *eh,
                                uint64_t *begin_out, uint64_t *end_out)
{
    if (begin_out) *begin_out = 0;
    if (end_out) *end_out = 0;
    if (!begin_out || !end_out) return;

    (void)find_symbol_by_name(blob, blob_size, eh, "begin_failure_scratch", begin_out);
    (void)find_symbol_by_name(blob, blob_size, eh, "end_failure_scratch", end_out);
}

void clear_signature_region(uint64_t begin, uint64_t end)
{
    if (!is_valid_ddr_range(begin, end)) return;
    memset_local((void *)(uintptr_t)begin, 0, (size_t)(end - begin));
    asm volatile ("fence rw, rw" ::: "memory");
}

void dump_signature_region(uint64_t begin, uint64_t end)
{
    if (!is_valid_ddr_range(begin, end)) {
        dbg_puts("[SIG] signature range unavailable\n");
        return;
    }

    uint64_t bytes = end - begin;
    dbg_puts("[SIG] begin=0x"); dbg_hex_u64(begin);
    dbg_puts(" end=0x"); dbg_hex_u64(end);
    dbg_puts(" bytes=0x"); dbg_hex_u64(bytes); dbg_nl();

    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)begin;
    uint64_t qwords = bytes / 8;
    uint64_t tail = bytes % 8;

    for (uint64_t i = 0; i < qwords; i++) {
        uint64_t a = begin + (i * 8);
        uint64_t v = 0;
        for (uint64_t j = 0; j < 8; j++) {
            v |= ((uint64_t)p[(i * 8) + j]) << (j * 8);
        }
        dbg_puts("[SIGQ] 0x"); dbg_hex_u64(a);
        dbg_puts(" : 0x"); dbg_hex_u64(v);
        dbg_nl();
    }

    for (uint64_t i = 0; i < tail; i++) {
        uint64_t a = begin + (qwords * 8) + i;
        uint8_t b = p[(qwords * 8) + i];
        dbg_puts("[SIGB] 0x"); dbg_hex_u64(a);
        dbg_puts(" : 0x");
        dbg_putc("0123456789abcdef"[(b >> 4) & 0xf]);
        dbg_putc("0123456789abcdef"[b & 0xf]);
        dbg_nl();
    }
}

void dump_failure_scratch_region(uint64_t begin, uint64_t end)
{
    if (begin == 0 || end == 0 || end <= begin || !is_valid_ddr_range(begin, end)) {
        dbg_puts("[FAILSCR] unavailable\n");
        return;
    }

    uint64_t bytes = end - begin;
    dbg_puts("[FAILSCR] begin=0x"); dbg_hex_u64(begin);
    dbg_puts(" end=0x"); dbg_hex_u64(end);
    dbg_puts(" bytes=0x"); dbg_hex_u64(bytes); dbg_nl();

    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)begin;
    uint64_t qwords = bytes / 8;
    uint64_t tail = bytes % 8;

    for (uint64_t i = 0; i < qwords; i++) {
        uint64_t a = begin + (i * 8);
        uint64_t v = 0;
        for (uint64_t j = 0; j < 8; j++) {
            v |= ((uint64_t)p[(i * 8) + j]) << (j * 8);
        }
        dbg_puts("[FAILQ] 0x"); dbg_hex_u64(a);
        dbg_puts(" : 0x"); dbg_hex_u64(v);
        dbg_nl();
    }

    for (uint64_t i = 0; i < tail; i++) {
        uint64_t a = begin + (qwords * 8) + i;
        uint8_t b = p[(qwords * 8) + i];
        dbg_puts("[FAILB] 0x"); dbg_hex_u64(a);
        dbg_puts(" : 0x");
        dbg_putc("0123456789abcdef"[(b >> 4) & 0xf]);
        dbg_putc("0123456789abcdef"[b & 0xf]);
        dbg_nl();
    }
}

int load_elf_blob(const uint8_t *blob, size_t blob_size, uint64_t *entry_out)
{
    if (blob_size < sizeof(Elf64_Ehdr)) return -1;

    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)(const void *)blob;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') return -2;
    if (eh->e_ident[4] != ELFCLASS64 || eh->e_ident[5] != ELFDATA2LSB) return -3;
    if (eh->e_machine != EM_RISCV) return -4;
    if (eh->e_phentsize != sizeof(Elf64_Phdr)) return -5;

    uint64_t ph_end = eh->e_phoff + ((uint64_t)eh->e_phnum * (uint64_t)eh->e_phentsize);
    if (eh->e_phoff > blob_size || ph_end > blob_size || ph_end < eh->e_phoff) return -6;

    g_loaded_blob = blob;
    g_loaded_blob_size = blob_size;
    g_tohost_addr = find_tohost_addr(blob, blob_size, eh);
    find_signature_range(blob, blob_size, eh, (uint64_t *)&g_sig_begin, (uint64_t *)&g_sig_end);
    find_failure_scratch_range(blob, blob_size, eh, (uint64_t *)&g_fail_begin, (uint64_t *)&g_fail_end);

    const Elf64_Phdr *ph = (const Elf64_Phdr *)(const void *)(blob + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;

        uint64_t seg_end = ph[i].p_offset + ph[i].p_filesz;
        if (ph[i].p_offset > blob_size || seg_end > blob_size || seg_end < ph[i].p_offset) return -7;
        if (ph[i].p_memsz < ph[i].p_filesz) return -8;

        uint64_t dst_addr = (ph[i].p_paddr ? ph[i].p_paddr : ph[i].p_vaddr);
        if (dst_addr == 0) return -9;
        if (!is_valid_ddr_addr(dst_addr)) return -10;
        if (ph[i].p_memsz > 0 && !is_valid_ddr_addr(dst_addr + ph[i].p_memsz - 1)) return -11;

        void *dst = (void *)(uintptr_t)dst_addr;
        const void *src = (const void *)(blob + ph[i].p_offset);

        memcpy_local(dst, src, (size_t)ph[i].p_filesz);
        if (ph[i].p_memsz > ph[i].p_filesz) {
            memset_local((uint8_t *)dst + ph[i].p_filesz, 0, (size_t)(ph[i].p_memsz - ph[i].p_filesz));
        }
        if (ph[i].p_flags & PF_X) {
            uint32_t patched = patch_wfi_to_ecall(dst_addr, ph[i].p_memsz);
            if (patched) {
                uart_puts("[ELF] patched wfi->ecall count=");
                uart_put_dec_u64(patched);
                uart_puts("\n");
            }
        }
    }

    if (g_tohost_addr && is_valid_ddr_addr(g_tohost_addr)) {
        g_tohost_ptr = (volatile uint64_t *)(uintptr_t)g_tohost_addr;
    } else {
        g_tohost_ptr = 0;
    }

    sync_icache();
    *entry_out = eh->e_entry;

    asm volatile ("fence rw, rw" ::: "memory");
    asm volatile ("fence.i" ::: "memory");
    return 0;
}
