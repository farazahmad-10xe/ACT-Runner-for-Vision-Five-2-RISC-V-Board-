#ifndef VF2_RUNNER_RVMODEL_H
#define VF2_RUNNER_RVMODEL_H

/*
 * Minimal ACT model adapter for the VF2 runner.
 *
 * This file is intended to be included by ACT assembly tests through the
 * RVMODEL include path. It exposes the symbols and halt contract that the
 * runner firmware already consumes:
 *   - tohost
 *   - begin_signature / end_signature
 *   - begin_failure_scratch / end_failure_scratch
 *
 * Halt behavior:
 *   - write gp to tohost
 *   - execute ECALL
 *
 * The runner interprets the resulting ECALL according to the current
 * privilege mode:
 *   - M-mode: handled directly by the M trap path
 *   - S/U-mode: handled by the delegated S trap path and bridged back to M
 */

#define RVMODEL_BOOT
#define RVMODEL_IO_INIT
#define RVMODEL_IO_WRITE_STR(_STR)
#define RVMODEL_IO_CHECK
#define RVMODEL_SET_MSW_INT
#define RVMODEL_CLEAR_MSW_INT
#define RVMODEL_CLEAR_MTIMER_INT

#define RVMODEL_HALT                                              \
    la t0, tohost;                                                \
    sd gp, 0(t0);                                                 \
    fence rw, rw;                                                 \
    ecall

#define RVMODEL_DATA_BEGIN                                        \
    .pushsection .tohost, "aw", @progbits;                        \
    .align 3;                                                     \
    .global tohost;                                               \
tohost:                                                           \
    .dword 0;                                                     \
    .popsection;                                                  \
    .pushsection .runner_fail_scratch, "aw", @progbits;           \
    .align 3;                                                     \
    .global begin_failure_scratch;                                \
begin_failure_scratch:                                            \
    .fill 64, 1, 0;                                               \
    .global end_failure_scratch;                                  \
end_failure_scratch:                                              \
    .popsection;                                                  \
    .pushsection .signature, "aw", @progbits;                     \
    .align 4;                                                     \
    .global begin_signature;                                      \
begin_signature:

#define RVMODEL_DATA_END                                          \
    .global end_signature;                                        \
end_signature:                                                    \
    .popsection

#endif
