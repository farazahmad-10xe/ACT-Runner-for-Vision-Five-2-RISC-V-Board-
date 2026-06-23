# Riescue S-Mode Analysis: Why It Doesn't Work

## Executive Summary

**Riescue CANNOT run in S-mode.** This is a fundamental architectural constraint, not a bug.

**Actual Error from Hardware:**
```
exit_diagnosis=illegal_machine_csr_access_from_s_mode
exit_insn_decode=csrrw x0, mtvec, x5
exit_illegal_csr=0x0000000000000305  (mtvec - Machine Trap Vector)
```

---

## Root Cause Analysis

### What Happened

1. **Attempted Build**: `make PAYLOAD_PROFILE=RIESCUE RUN_PRIV=S ...`
2. **Makefile allowed it**: (erroneously) accepted S-mode for Riescue
3. **Build succeeded**: Firmware was created with EXEC_MODE_S
4. **Boot and load**: Hart1 loaded Riescue into memory, set up S-mode entry
5. **Immediate failure**: Riescue loader tried to write `mtvec` at address 0x41000084
6. **Illegal instruction**: CPU raised `mcause=0x2` (illegal instruction)

### Why This Happened

The Riescue firmware was compiled with `-DRUNNER_EXEC_MODE=EXEC_MODE_S`, causing the runner to:
```c
enter_smode_from_m(entry, smode_sp);  // Transition to S-mode
// Then Riescue runs in S-mode...
// But Riescue's loader immediately tries:
csrrw x0, mtvec, x5  // ILLEGAL! mtvec is M-mode only
```

---

## RISC-V Privilege Model Constraint

### The Problem

In RISC-V, each privilege level has **separate CSR spaces**:

| CSR | M-Mode | S-Mode | U-Mode |
|-----|--------|--------|--------|
| `mtvec` (Machine trap vector) | ✅ Read/Write | ❌ **ILLEGAL** | ❌ **ILLEGAL** |
| `mstatus` (Machine status) | ✅ Read/Write | ❌ **ILLEGAL** | ❌ **ILLEGAL** |
| `mscratch` (Machine scratch) | ✅ Read/Write | ❌ **ILLEGAL** | ❌ **ILLEGAL** |
| `medeleg` (Machine exception delegation) | ✅ Read/Write | ❌ **ILLEGAL** | ❌ **ILLEGAL** |
| `mideleg` (Machine interrupt delegation) | ✅ Read/Write | ❌ **ILLEGAL** | ❌ **ILLEGAL** |
| `stvec` (Supervisor trap vector) | ✅ Read/Write | ✅ Read/Write | ❌ **ILLEGAL** |
| `sstatus` (Supervisor status) | ✅ Read/Write | ✅ Read/Write | ❌ **ILLEGAL** |

**When S-mode code tries to access M-mode CSRs, the CPU automatically raises `mcause=2` (Illegal Instruction).**

### Why Riescue Needs M-Mode CSRs

Riescue is a **bare-metal test framework** that must:

1. **Initialize trap handlers** → Write `mtvec` (M-mode only)
2. **Configure exception delegation** → Write `medeleg`/`mideleg` (M-mode only)
3. **Manage interrupt state** → Modify `mstatus` (M-mode only)
4. **Read machine ISA** → Query `misa` (M-mode only)
5. **Save/restore state** → Use `mscratch` (M-mode only)

These are **fundamental M-mode responsibilities**. There is no S-mode equivalent.

---

## UART Output Breakdown

```
[MON] hart2 online
M-mode (VisionFive2) payload runner
exec_mode=S                                # ← User tried S-mode
[FLOW] isolated_env_prepare from_mode=M target_mode=S
[FLOW] isolated_env_enter from_mode=M target_mode=S entry=0x0000000041000000 stack=0x000000004008f000
[FLOW] privilege_transition from_mode=M to_mode=S entry=0x0000000041000000 stack=0x000000004008f000
[LFLOW] m_to_s entry=0x0000000041000000 sp=0x000000004008f000 mstatus=0x8000000a00006880
[RST] test timeout
[RST] monitor-local deadline expired
```

**What happened:**
1. Runner successfully transitioned M-mode → S-mode
2. Riescue entry point executed in S-mode
3. Riescue loader at `0x41000084` tried: `csrrw x0, mtvec, x5`
4. CPU raised **Illegal Instruction** exception
5. No trap handler installed yet (Riescue never got that far)
6. Test timed out

```
exit_trap_mode=S
exit_mcause=0x0000000000000002
exit_reason=illegal_instruction
exit_mepc=0x0000000041000084
exit_insn=0x0000000030529073
exit_insn_decode=csrrw x0, mtvec, x5
exit_illegal_csr=0x0000000000000305
exit_illegal_csr_name=mtvec
exit_diagnosis=illegal_machine_csr_access_from_s_mode
```

**Clear diagnosis:** S-mode cannot access `mtvec`. This is correct behavior by the CPU.

---

## Comparison: ACT vs Riescue

### ACT Profile (Supports M/S/U)
```
Runner (M-mode) manages:
  - Trap vectors (mtvec, stvec)
  - Delegation (medeleg, mideleg)
  - Counters and interrupts
  
Test payload runs at requested level:
  - M-mode: Direct execution
  - S-mode: Delegated exceptions come back to runner
  - U-mode: Via S-mode trampoline
  
Result: Isolation works because runner controls CSRs
```

### Riescue Profile (M-Mode Only)
```
Riescue (payload) itself manages:
  - Trap vectors (writes mtvec directly)
  - Delegation (writes medeleg/mideleg)
  - Interrupts and state
  
If run in S-mode:
  - Cannot write M-mode CSRs
  - Crashes immediately
  - Cannot recover

Result: Must run in M-mode
```

---

## The Fix

### Makefile Constraint (Restored)

```makefile
ifeq ($(strip $(PAYLOAD_PROFILE)),RIESCUE)
  ifneq ($(strip $(RUN_PRIV)),M)
    $(error PAYLOAD_PROFILE=RIESCUE currently supports RUN_PRIV=M only)
  endif
  CFLAGS += -DRUNNER_EXEC_MODE=EXEC_MODE_M
  PAYLOAD_OBJ=riescue_elf.o
```

**Result:** Makefile now correctly rejects S-mode for Riescue with clear error message.

### Documentation Updates

1. **Section 1 (Overview)**: Changed "M only" constraint
2. **Section 3.2 (Defaults)**: Clarified M-mode only with design rationale
3. **Section 5.5 (Architecture Constraints)**: Detailed explanation with RISC-V privilege model
4. **Section 7.4 (Build)**: Removed S-mode instructions
5. **RIESCUE_BUILD_FLASH_GUIDE.md**: Updated to M-mode only

---

## Lessons Learned

| Aspect | Lesson |
|--------|--------|
| **Architecture** | Payload profiles have different CSR requirements; ACT is isolated, Riescue manages its own |
| **Privilege model** | M-mode CSRs cannot be accessed from lower privilege; this is by design |
| **Isolation** | Running M-mode runtime in S-mode violates fundamental security model |
| **Testing** | Hardware testing revealed the constraint; simulation alone wouldn't catch this |
| **Documentation** | Clear architectural constraints are essential; don't allow invalid configurations |

---

## Recommended Build Commands

### ✅ Correct (M-Mode)

```bash
make -f Makefile.act clean
make -f Makefile.act \
  PAYLOAD_PROFILE=RIESCUE \
  RIESCUE_ELF=special_Tests/tp_zicsr_1_updated_mmp \
  RUNNER_TOUCH_MENVCFG=0 \
  RUNNER_ASSUME_RVA23_PRIV_FEATURES=0 \
  RUNNER_USE_SV39=0
```

### ❌ Incorrect (S-Mode) - Will Fail

```bash
make -f Makefile.act \
  PAYLOAD_PROFILE=RIESCUE \
  RUN_PRIV=S \  # ← NOT SUPPORTED
  RUNNER_DELEG_POLICY=DELEGATION_POLICY_S_BASE \  # ← IGNORED
  ...
```

**Error message:**
```
Makefile.act:39: *** PAYLOAD_PROFILE=RIESCUE currently supports RUN_PRIV=M only.  Stop.
```

---

## References

1. **RISC-V Privilege Spec**: Machine mode can write mtvec; Supervisor cannot
2. **Riescue Framework**: Bare-metal test scheduler with M-mode runtime
3. **StarFive VF2**: JH7110 SoC supports full M/S/U privilege hierarchy
4. **Documentation**: See `docs/runner_profiles_modes.md` section 5.5

---

## Summary

**Riescue is fundamentally an M-mode runtime.** It is architecturally incompatible with S-mode execution because:

1. **RISC-V privilege model** prevents S-mode from accessing M-mode CSRs
2. **Riescue design** requires direct M-mode CSR control for trap handling
3. **No alternative** exists: there is no S-mode equivalent for machine-level trap setup

**Solution**: Use **M-mode only** (default) for Riescue, and **S/U-mode** for ACT profiles if isolation is desired.

---

**Created**: May 5, 2026  
**Status**: ✅ Constraint understood and documented  
**Makefile**: Reverted to M-mode only for Riescue  
**Documentation**: Updated with full architectural explanation
