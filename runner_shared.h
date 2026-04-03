#ifndef RUNNER_SHARED_H
#define RUNNER_SHARED_H

#include <stdint.h>
#include <stddef.h>

#define UART_BASE       0x10000000UL

#define UART_THR        0x00
#define UART_LSR        0x14
#define UART_LSR_THRE   (1u << 5)

#define CLINT_MSIP_BASE 0x02000000UL
#define CLINT_MTIMECMP_BASE 0x02004000UL
#define CLINT_MTIME_ADDR    0x0200bff8UL
#define RUNNER_HART_ID  1u
#define MONITOR_HART_ID 2u

#define PT_LOAD         1u
#define PF_X            1u
#define EM_RISCV        243u
#define EI_NIDENT       16u
#define ELFCLASS64      2u
#define ELFDATA2LSB     1u
#define SHT_SYMTAB      2u
#define SHT_DYNSYM      11u

#define PACK_MAGIC      0x4b504341u
#define PACK_VERSION    1u
#define MAX_PACK_TESTS  512u
#define EXT_PACK_ADDR   0x88000000ULL
#define EXT_PACK_MAX_BYTES (128ULL * 1024ULL * 1024ULL)
#define PACK_FOOTER_MAGIC 0x464b5041u
#define TEST_STACK_BYTES (256u * 1024u)
#define TRAP_STACK_BYTES 1024u

#define SDIO0_BASE      0x16010000UL
#define SDIO1_BASE      0x16020000UL
#define SD_BLOCK_SIZE   512u
#define JH7110_WDT_BASE 0x13070000UL
#define JH7110_WDT_LOAD 0x000
#define JH7110_WDT_CTRL 0x008
#define JH7110_WDT_LOCK 0xc00
#define JH7110_WDT_UNLOCK_KEY 0x1acce551u

#define MCAUSE_INTERRUPT_BIT (1ULL << 63)
#define MCAUSE_MSI      (MCAUSE_INTERRUPT_BIT | 3ULL)
#define MCAUSE_MTI      (MCAUSE_INTERRUPT_BIT | 7ULL)
#define MCAUSE_ECALL_M  11ULL
#define MCAUSE_INST_MISALIGNED 0ULL
#define MCAUSE_LOAD_MISALIGNED 4ULL
#define MCAUSE_STORE_MISALIGNED 6ULL
#define TOHOST_TIMEOUT  0xdead000000000001ULL
#define TOHOST_TRAP     0xdead000000000002ULL
#define TEST_TIMEOUT_TICKS 50000000ULL
#define RESET_DELAY_TICKS 20000000ULL

extern uint8_t _act_elf_start[];
extern uint8_t _act_elf_end[];
extern uint8_t _act_pack_start[];
extern uint8_t _act_pack_end[];
extern volatile uint64_t g_boot_sync;

extern volatile uint64_t* g_tohost_ptr;
extern volatile uint64_t g_tohost_addr;
extern volatile uint64_t g_sig_begin;
extern volatile uint64_t g_sig_end;
extern volatile uint64_t g_fail_begin;
extern volatile uint64_t g_fail_end;

extern volatile uint64_t g_runner_active;
extern volatile uint64_t g_monitor_seen_tohost;
extern volatile uint64_t g_test_done;
extern volatile uint64_t g_test_tohost_value;
extern volatile uint64_t g_test_resume_pc;
extern volatile uint64_t g_runner_saved_sp;
extern volatile uint64_t g_runner_saved_gp;
extern volatile uint64_t g_reset_armed;
extern volatile uint64_t g_test_deadline_mtime;
extern volatile uint64_t g_reset_request_mtime;
extern volatile uint64_t g_sig_dump_in_progress;
extern volatile uint64_t g_case_report_ready;
extern volatile uint64_t g_monitor_report_done;
extern volatile uint64_t g_fault_reported;
extern volatile const char *g_active_case_name;
extern uint8_t g_test_stack[TEST_STACK_BYTES];
extern uint8_t g_trap_stack[TRAP_STACK_BYTES];
extern const uint8_t *g_loaded_blob;
extern size_t g_loaded_blob_size;

typedef struct {
    uint64_t ra;
    uint64_t gp;
    uint64_t tp;
    uint64_t t0;
    uint64_t t1;
    uint64_t t2;
    uint64_t s0;
    uint64_t s1;
    uint64_t a0;
    uint64_t a1;
    uint64_t a2;
    uint64_t a3;
    uint64_t a4;
    uint64_t a5;
    uint64_t a6;
    uint64_t a7;
    uint64_t s2;
    uint64_t s3;
    uint64_t s4;
    uint64_t s5;
    uint64_t s6;
    uint64_t s7;
    uint64_t s8;
    uint64_t s9;
    uint64_t s10;
    uint64_t s11;
    uint64_t t3;
    uint64_t t4;
    uint64_t t5;
    uint64_t t6;
    uint64_t sp;
} TrapFrame;

extern TrapFrame g_last_trap_frame;
extern uint64_t g_last_trap_mcause;
extern uint64_t g_last_trap_mepc;
extern uint64_t g_last_trap_mtval;
extern uint64_t g_last_trap_mstatus;
extern uint8_t g_last_trap_valid;

static inline void mmio_write8(uintptr_t addr, uint8_t v) { *(volatile uint8_t*)addr = v; }
static inline uint8_t mmio_read8(uintptr_t addr) { return *(volatile uint8_t*)addr; }
static inline void mmio_write32(uintptr_t addr, uint32_t v) { *(volatile uint32_t*)addr = v; }
static inline uint32_t mmio_read32(uintptr_t addr) { return *(volatile uint32_t*)addr; }

static inline uint64_t read_csr_mhartid(void) { uint64_t v; __asm__ volatile ("csrr %0, mhartid" : "=r"(v)); return v; }
static inline void write_csr_mtvec(uint64_t v) { __asm__ volatile ("csrw mtvec, %0" :: "r"(v)); }
static inline void write_csr_mscratch(uint64_t v) { __asm__ volatile ("csrw mscratch, %0" :: "r"(v)); }
static inline void wfi(void) { __asm__ volatile ("wfi"); }
static inline void cpu_relax(void) { __asm__ volatile ("nop" ::: "memory"); }
static inline uint64_t read_mstatus(void){ uint64_t x; asm volatile("csrr %0, mstatus":"=r"(x)); return x; }
static inline uint64_t read_misa(void){ uint64_t x; asm volatile("csrr %0, misa":"=r"(x)); return x; }
static inline void write_mstatus(uint64_t x){ asm volatile("csrw mstatus, %0"::"r"(x)); }
static inline uint64_t read_mie(void){ uint64_t x; asm volatile("csrr %0, mie":"=r"(x)); return x; }
static inline void write_mie(uint64_t x){ asm volatile("csrw mie, %0"::"r"(x)); }
static inline void write_fcsr(uint64_t x){ asm volatile("csrw fcsr, %0"::"r"(x)); }
static inline volatile uint32_t *msip_ptr(uint32_t hartid)
{
    return (volatile uint32_t *)(uintptr_t)(CLINT_MSIP_BASE + (hartid * 4u));
}
static inline volatile uint64_t *mtimecmp_ptr(uint32_t hartid)
{
    return (volatile uint64_t *)(uintptr_t)(CLINT_MTIMECMP_BASE + ((uint64_t)hartid * 8ULL));
}
static inline volatile uint64_t *mtime_ptr(void)
{
    return (volatile uint64_t *)(uintptr_t)CLINT_MTIME_ADDR;
}
static inline void sync_icache(void) { asm volatile ("fence.i" ::: "memory"); }

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
} ActPackHeader;

typedef struct __attribute__((packed)) {
    char name[64];
    uint64_t offset;
    uint64_t size;
} ActPackEntry;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint64_t start_lba;
    uint32_t num_blocks;
    uint32_t reserved;
} ActPackFooter;

typedef struct {
    const char *name;
    uint64_t value;
    uint64_t size;
} SymbolInfo;

typedef struct {
    const char *name;
    int rc;
    uint64_t tohost;
    uint64_t tohost_addr;
    uint64_t sig_begin;
    uint64_t sig_end;
    int status;
} TestResult;

enum {
    CASE_STATUS_PASS = 0,
    CASE_STATUS_FAIL = 1,
    CASE_STATUS_TIMEOUT = 2,
    CASE_STATUS_ERROR = 3,
};

int is_valid_ddr_addr(uint64_t addr);
int is_valid_ddr_range(uint64_t begin, uint64_t end);

void *memcpy_local(void *dst, const void *src, size_t n);
void *memset_local(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

void uart_putc(char c);
void uart_puts(const char* s);
void uart_put_hex(uint64_t x);
void uart_put_dec_u64(uint64_t v);

void dbg_putc(char c);
void dbg_puts(const char *s);
void dbg_hex_u64(uint64_t v);
void dbg_hex_u32(uint32_t v);
void dbg_nl(void);
void dbg_put_u32_dec(uint32_t v);
void dbg_put_reg_name(uint32_t reg);
int32_t sign_extend_32(uint32_t v, uint32_t bits);
void dbg_put_imm_i32(int32_t v);
void decode_riscv_insn(uint64_t pc, uint32_t insn);
void dump_words32(uint64_t addr);
void uart_put_token_str(const char *s);
void emit_reg_line4(const char *n0, uint64_t v0,
                    const char *n1, uint64_t v1,
                    const char *n2, uint64_t v2,
                    const char *n3, uint64_t v3);
void emit_reg_line3(const char *n0, uint64_t v0,
                    const char *n1, uint64_t v1,
                    const char *n2, uint64_t v2);

void trigger_watchdog_reset(void);
void monitor_irq_enable(void);
void monitor_irq_disable(void);
void enable_fpu_set_fs_only(void);
void enable_fpu_try_clear_fcsr(void);

int streq(const char* a, const char* b);
int find_symbol_for_addr(const uint8_t *blob, size_t blob_size, uint64_t addr, SymbolInfo *out);
void capture_last_trap(const TrapFrame *tf, uint64_t mcause, uint64_t mepc,
                       uint64_t mtval, uint64_t mstatus);
const char *trap_reason_name(uint64_t mcause);
uint32_t read_insn_word(uint64_t pc, int *valid_out);
void find_signature_range(const uint8_t* blob, size_t blob_size, const Elf64_Ehdr* eh,
                          uint64_t *sig_begin_out, uint64_t *sig_end_out);
void find_failure_scratch_range(const uint8_t *blob, size_t blob_size, const Elf64_Ehdr *eh,
                                uint64_t *begin_out, uint64_t *end_out);
void clear_signature_region(uint64_t begin, uint64_t end);
void dump_signature_region(uint64_t begin, uint64_t end);
void dump_failure_scratch_region(uint64_t begin, uint64_t end);
int load_elf_blob(const uint8_t *blob, size_t blob_size, uint64_t *entry_out);

int load_pack_from_sd_tail(void);
void emit_execution_context(const char *reason);
uint64_t get_case_exit_pc(void);
void emit_trap_failure_report(uint64_t mcause, uint64_t mepc);
void handle_fatal_test_trap(const TrapFrame *tf, uint64_t mcause, uint64_t mepc,
                            uint64_t mtval, uint64_t mstatus);
void request_deferred_reset(void);
int wait_for_monitor_report(void);
uint64_t trap_c(uint64_t mcause, uint64_t mepc, uint64_t mtval, uint64_t mstatus,
                TrapFrame *tf);
void trap_entry(void);
void monitor_hart_loop(void);
const char *case_status_name(int status);
int case_is_pass(const TestResult *tr);
void emit_case_report(const TestResult *tr);
uint64_t run_loaded_entry(uint64_t entry);
int run_one_blob(const char *name, const uint8_t *blob, size_t blob_size, TestResult *out);
int run_single_embedded(uint64_t *total, uint64_t *pass, uint64_t *fail);
int run_pack_embedded(uint64_t *total, uint64_t *pass, uint64_t *fail);
int run_pack_external(uint64_t *total, uint64_t *pass, uint64_t *fail);

#endif
