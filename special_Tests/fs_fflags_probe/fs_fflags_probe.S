/* fs_fflags_probe.S
 * VF2/U74 M-mode test:
 * Check if fflags access with mstatus.FS=00 changes FS automatically.
 */

.option norvc

#ifndef EXIT_CODE
#define EXIT_CODE 3
#endif

.equ UART_BASE, 0x10000000

.section .text, "ax", @progbits
.globl _start

_start:
    la      sp, stack_top

    csrr    t0, mtvec
    la      t1, saved_runner_mtvec
    sd      t0, 0(t1)

    la      t0, trap_handler
    csrw    mtvec, t0

    la      t0, trap_count
    sd      zero, 0(t0)

    la      a1, msg_start
    call    uart_puts

    /* Clear FS bits: mstatus[14:13] = 00 */
    li      t0, 0x6000
    csrc    mstatus, t0

    csrr    t1, mstatus
    la      t2, before_mstatus
    sd      t1, 0(t2)

    mv      a0, t1
    la      a1, msg_before_test1
    call    print_label_hex

    li      a2, -1

test1:
    csrrc   a2, fflags, a2

after_test1:
    csrr    t1, mstatus
    la      t2, after_first_trap_mstatus
    sd      t1, 0(t2)

    mv      a0, t1
    la      a1, msg_after_test1
    call    print_label_hex

    /* Force FS back to 00 before read-only fflags access */
    li      t0, 0x6000
    csrc    mstatus, t0

    csrr    t1, mstatus
    la      t2, before_read_mstatus
    sd      t1, 0(t2)

    mv      a0, t1
    la      a1, msg_before_test2
    call    print_label_hex

test2:
    csrrs   a2, fflags, x0

after_test2:
    csrr    t1, mstatus
    la      t2, after_second_trap_mstatus
    sd      t1, 0(t2)

    mv      a0, t1
    la      a1, msg_after_test2
    call    print_label_hex

done:
    la      a1, msg_done
    call    uart_puts

    la      t0, saved_runner_mtvec
    ld      t0, 0(t0)
    csrw    mtvec, t0

    li      gp, EXIT_CODE
    la      t0, tohost
    sd      gp, 0(t0)
    fence   rw, rw

1:
    wfi
    j       1b


trap_handler:
    csrr    t0, mcause
    csrr    t1, mepc
    csrr    t2, mtval
    csrr    t3, mstatus

    /* Save trap record */
    la      t4, trap_count
    ld      t5, 0(t4)
    addi    t6, t5, 1
    sd      t6, 0(t4)

    la      t4, trap_log
    slli    t6, t5, 6
    add     t4, t4, t6

    sd      t0, 0(t4)      /* mcause */
    sd      t1, 8(t4)      /* mepc */
    sd      t2, 16(t4)     /* mtval */
    sd      t3, 24(t4)     /* mstatus */

    srli    t5, t3, 13
    andi    t5, t5, 3
    sd      t5, 32(t4)     /* decoded FS */

    csrr    t6, mtvec
    sd      t6, 40(t4)
    mv      s2, t4

    /* UART prints inside trap. Reload from memory because helpers clobber t regs. */
    ld      a0, 0(s2)
    la      a1, msg_trap_mcause
    call    print_label_hex

    ld      a0, 8(s2)
    la      a1, msg_trap_mepc
    call    print_label_hex

    ld      a0, 24(s2)
    la      a1, msg_trap_mstatus
    call    print_label_hex

    ld      a0, 32(s2)
    la      a1, msg_trap_fs
    call    print_label_hex

    /* Skip the 4-byte faulting instruction */
    ld      t1, 8(s2)
    addi    t1, t1, 4
    csrw    mepc, t1
    mret


/**************** UART helpers ****************/

uart_putc:
    li      t6, UART_BASE
1:
    lb      t5, 20(t6)         # LSR = 5 << 2
    andi    t5, t5, 0x20       # THR empty
    beqz    t5, 1b
    sb      a0, 0(t6)          # THR
    ret
    
uart_puts:
    addi    sp, sp, -16
    sd      ra, 8(sp)
1:
    lb      a0, 0(a1)
    beqz    a0, 2f
    addi    a1, a1, 1
    call    uart_putc
    j       1b
2:
    ld      ra, 8(sp)
    addi    sp, sp, 16
    ret

uart_hex64:
    addi    sp, sp, -16
    sd      ra, 8(sp)
    mv      t3, a0
    li      t4, 16
1:
    srli    t5, t3, 60
    li      t6, 10
    blt     t5, t6, 2f
    addi    t5, t5, 87         /* a-f */
    j       3f
2:
    addi    t5, t5, 48         /* 0-9 */
3:
    mv      a0, t5
    call    uart_putc
    slli    t3, t3, 4
    addi    t4, t4, -1
    bnez    t4, 1b
    ld      ra, 8(sp)
    addi    sp, sp, 16
    ret

print_label_hex:
    /* input: a1 = label string, a0 = value */
    addi    sp, sp, -32
    sd      ra, 24(sp)
    sd      s1, 16(sp)
    mv      s1, a0
    call    uart_puts
    mv      a0, s1
    call    uart_hex64
    la      a1, nl
    call    uart_puts
    ld      s1, 16(sp)
    ld      ra, 24(sp)
    addi    sp, sp, 32
    ret


.section .rodata, "a", @progbits
msg_start:
    .asciz "\r\n[DBG] fs_fflags_probe start\r\n"
msg_before_test1:
    .asciz "[DBG] before CSRRC fflags mstatus=0x"
msg_after_test1:
    .asciz "[DBG] after CSRRC trap mstatus=0x"
msg_before_test2:
    .asciz "[DBG] before CSRRS fflags mstatus=0x"
msg_after_test2:
    .asciz "[DBG] after CSRRS trap mstatus=0x"
msg_trap_mcause:
    .asciz "[TRAP] mcause=0x"
msg_trap_mepc:
    .asciz "[TRAP] mepc=0x"
msg_trap_mstatus:
    .asciz "[TRAP] mstatus=0x"
msg_trap_fs:
    .asciz "[TRAP] decoded FS=0x"
msg_done:
    .asciz "[DBG] fs_fflags_probe done\r\n"
nl:
    .asciz "\r\n"


.section .tohost, "aw", @progbits
.align 3
.globl tohost
tohost:
    .dword 0
.globl fromhost
fromhost:
    .dword 0


.section .runner_fail_scratch, "aw", @progbits
.align 3
.globl begin_failure_scratch
begin_failure_scratch:
    .fill 64, 1, 0
.globl end_failure_scratch
end_failure_scratch:


.section .signature, "aw", @progbits
.align 3
.globl begin_signature
begin_signature:
before_mstatus:
    .dword 0
after_first_trap_mstatus:
    .dword 0
before_read_mstatus:
    .dword 0
after_second_trap_mstatus:
    .dword 0
trap_count:
    .dword 0
saved_runner_mtvec:
    .dword 0

/* Two 64-byte trap records:
 * +0  mcause
 * +8  mepc
 * +16 mtval
 * +24 mstatus
 * +32 decoded FS
 * +40 mtvec
 */
trap_log:
    .fill 16, 8, 0

.globl end_signature
end_signature:


.section .bss, "aw", @nobits
.align 12
.space 4096
stack_top:
