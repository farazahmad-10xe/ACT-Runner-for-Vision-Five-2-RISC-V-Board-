#include "runner_shared.h"

void runner_reset_sbi_state(void)
{
    memset_local(&g_runner_sbi, 0, sizeof(g_runner_sbi));
}

void runner_prepare_smode_return_bridge(TrapFrame *tf) { (void)tf; }
int runner_prepare_smode_request_bridge(uint64_t pc, TrapFrame *tf)
{ (void)pc; (void)tf; return 0; }
int runner_prepare_smode_illegal_csr_bridge(uint64_t cause, uint64_t pc, TrapFrame *tf)
{ (void)cause; (void)pc; (void)tf; return 0; }
int runner_prepare_smode_tsbi_bridge(uint64_t pc, TrapFrame *tf)
{ (void)pc; (void)tf; return 0; }
int runner_handle_test_sbi(TrapFrame *tf, uint64_t pc, uint64_t mode, uint64_t *next)
{ (void)tf; (void)pc; (void)mode; (void)next; return 0; }
int runner_handle_tsbi(TrapFrame *tf, uint64_t pc, uint64_t mode, uint64_t *next)
{ (void)tf; (void)pc; (void)mode; (void)next; return 0; }
