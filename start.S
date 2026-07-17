/* start.S - minimal reset entry, hart roles set by BOARD_RUNNER_HART_ID/BOARD_MONITOR_HART_ID */

#ifndef BOARD_RUNNER_HART_ID
#define BOARD_RUNNER_HART_ID 1
#endif
#ifndef BOARD_MONITOR_HART_ID
#define BOARD_MONITOR_HART_ID 2
#endif

.section .text
.globl _start
.type _start, @function
.globl g_boot_sync

.section .data
g_boot_sync:
    .dword 0

.section .text

_start:
    /* ---- Use hart1 as runner, hart2 as monitor; park others ---- */
    csrr t0, mhartid
    li   t1, BOARD_RUNNER_HART_ID
    beq  t0, t1, .Lhart1
    li   t1, BOARD_MONITOR_HART_ID
    beq  t0, t1, .Lhart2
    j    .Lpark

.Lhart1:
    /* ---- Hart1 stack ---- */
    la   sp, __stack_top
    li   t2, 4096
    sub  sp, sp, t2

    /* ---- Clear .bss ---- */
    la   t0, __bss_start
    la   t1, __bss_end
.Lbss:
    bgeu t0, t1, .Lbss_done
    sd   zero, 0(t0)
    addi t0, t0, 8
    j    .Lbss
.Lbss_done:
    /* ---- Release hart2 after BSS init is complete ---- */
    la   t2, g_boot_sync
    li   t3, 1
    sd   t3, 0(t2)
    fence rw, rw

    /* ---- Call C entry ---- */
    call main
    j    .Lhang

.Lhart2:
    /* ---- Hart2 stack ---- */
    la   sp, __stack_top
    li   t2, 8192
    sub  sp, sp, t2

    /* ---- Wait until hart1 finished BSS clear ---- */
.Lwait_boot:
    la   t2, g_boot_sync
    ld   t3, 0(t2)
    beqz t3, .Lwait_boot

    /* ---- Call C entry ---- */
    call main

    /* If main returns, hang */
.Lhang:
    wfi
    j .Lhang

/* ---- Other harts park here forever ---- */
.Lpark:
    wfi
    j .Lpark
.size _start, . - _start
