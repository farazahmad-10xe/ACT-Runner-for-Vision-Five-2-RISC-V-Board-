# RVA23 S/U Execution-Environment Notes

Reference spec:

- `RVA23 Profiles`, Version `1.0`, ratified on `2024-10-17`
- Source: <https://docs.riscv.org/reference/profiles/rva23/_attachments/rva23-profile.pdf>

What matters for this runner:

- `RVA23U64` defines the user-mode execution-environment contract.
- `RVA23S64` defines the supervisor-mode execution-environment contract.
- `RVA23S64` inherits the mandatory unprivileged requirements from `RVA23U64`.

Key execution-environment requirements pulled from the spec:

- `ECALL` from `U` must trap to the supervisor execution environment.
- `ECALL` from `S` must trap to the outer execution environment.
- `Zifencei` must be available for `RVA23S64`.
- `Sv39` support is mandatory in the supervisor execution environment.
- `Sstvecd` requires direct-mode `stvec` handling.
- `Sstvala` requires fault information to be reflected in `stval`.
- `Sscounterenw` requires writable `scounteren` bits for implemented counters.
- `Sstc`, `Sscofpmf`, `Ssnpm`, `Svnapot`, `Svpbmt`, `Svinval`, and `Sha` are
  mandatory for a full `RVA23S64` hardware claim.

Runner implementation status:

- The runner now provides `M -> S` and `S -> U` entry paths.
- The runner installs `mtvec`, `stvec`, `mscratch`, and `sscratch`.
- The runner programs `medeleg`, `mideleg`, `mcounteren`, `scounteren`,
  `menvcfg`, `senvcfg`, `satp`, `sie`, and explicit PMP regions for runner and
  payload memory.
- The Sv39 map is still identity-based, but now only covers the runner image,
  loaded payload segments, and execution stacks with per-region permissions.
- The runner bridges lower-privilege completion and fatal traps back to the
  M-mode harness for reporting.
- The runner provides a minimal SBI surface for timer, IPI, and remote-fence
  calls that commonly appear in supervisor test payloads.

Important limitation:

- The runner can supply the execution-environment behavior around the test.
- It cannot emulate the full mandatory `RVA23S64` hardware feature set for a
  core that lacks those ISA extensions or CSRs.
- In particular, profile claims that depend on hardware support for extensions
  such as `V`, `Sstc`, `Svnapot`, `Svinval`, `Sscofpmf`, `Ssnpm`, or `Sha`
  remain dependent on the underlying SoC.

## VisionFive 2 / JH7110 Matrix

Primary hardware sources used for this matrix:

- StarFive JH7110 Product Brief `v1.5`, dated `2024-08-29`
  - <https://doc-en.rvspace.org/JH7110/Product_Brief/JH7110_DS/feature_pb.html>
- StarFive JH7110 Datasheet `U74 MC` page
  - <https://doc-en.rvspace.org/JH7110/Datasheet/JH7110_DS/c_u74_quad_core.html>
- SiFive U74-MC Core Complex Manual
  - <https://starfivetech.com/uploads/u74mc_core_complex_manual_21G1.pdf>

Status legend:

- `yes`: documented as supported by the VF2 hardware
- `runner`: hardware exists and the runner now provides the surrounding execution-environment plumbing
- `no evidence`: not documented in the available VF2/JH7110 sources, so do not claim it
- `no`: documented absent

### RVA23U64 base view

| Requirement | VF2 hardware | Runner role | Notes |
| --- | --- | --- | --- |
| `RV64I` | yes | none | Implied by `RV64GC` in StarFive docs |
| `M` | yes | none | Implied by `RV64GC` |
| `A` | yes | none | Implied by `RV64GC` |
| `F` | yes | none | Implied by `RV64GC` |
| `D` | yes | none | Implied by `RV64GC` |
| `C` | yes | none | Implied by `RV64GC` |
| `Zicsr` | yes | none | Present in the U74 ISA description |
| `Zifencei` | yes | none | Present in the U74 ISA description |
| `B` / `Zba` / `Zbb` | partial yes | none | U74 docs show `Zba` and `Zbb`; I do not see proof of the full ratified `B` bundle in the StarFive docs |
| `Zicntr` | yes | runner | Hardware counters exist; runner exposes them through `mcounteren/scounteren` in lower-privilege modes |
| `Zihpm` | likely yes | runner | U74 has HPM support; runner exposes the access path |
| `Ziccif` / `Ziccrse` / `Ziccamoa` / `Za64rs` | likely yes | none | These are platform memory-model/profile properties, not features the runner can emulate |
| `Zicclsm` | unknown | none | Need hardware validation; not clearly documented in JH7110 product material |
| `V` | no | impossible | VF2 uses SiFive U74; StarFive forum staff explicitly said JH7110 does not have vector |

### RVA23S64 supervisor view

| Requirement | VF2 hardware | Runner role | Notes |
| --- | --- | --- | --- |
| `S` privilege mode | yes | runner | U74 supports `M/S/U`; runner enters `S` and `U` explicitly |
| `Ss1p13` | unknown | partial | Runner follows the expected trap/delegation model, but spec-version compliance is a hardware/firmware property |
| `Svbare` | yes | runner | Bare mode exists; runner can leave translation off or program `satp` |
| `Sv39` | yes | runner | JH7110 docs explicitly list `Sv39`; runner now installs a constrained Sv39 map derived from the runner image and loaded test |
| `Svade` | unknown | no | Depends on MMU fault behavior of the core |
| `Ssccptr` | likely yes | no | Hardware page-table walker property; runner cannot emulate it |
| `Sstvecd` | yes | runner | Runner installs direct-mode `stvec`; hardware must still accept aligned direct bases |
| `Sstvala` | unknown | partial | Runner captures `stval`, but correctness of fault values is hardware behavior |
| `Sscounterenw` | likely yes | runner | Runner writes `scounteren`; writability still depends on the implementation |
| `Svpbmt` | no evidence | no | Not documented in JH7110/U74 sources I found |
| `Svinval` | no evidence | no | Not documented in JH7110/U74 sources I found |
| `Svnapot` | no evidence | no | Not documented in JH7110/U74 sources I found |
| `Sstc` | no evidence | no | Not documented in JH7110/U74 sources I found |
| `Sscofpmf` | yes | runner | U74 manual explicitly lists `Sscofpmf`; runner can expose counters but does not create this feature |
| `Ssnpm` | no evidence | no | Pointer masking support is not documented for VF2 |
| `Ssu64xl` | likely yes | runner | VF2 is a 64-bit U74 core and runner enters 64-bit U-mode, but this should still be validated on hardware |
| `Sha` | no evidence | no | No evidence of hypervisor extension support in JH7110/U74 sources used here |

### Practical conclusion

- The VisionFive 2 looks suitable for a broad `RVA23U64`-like and partial
  `RVA23S64` execution environment.
- The strongest documented hardware positives are `RV64GC`, `M/S/U`, `Sv39`,
  `Zifencei`, `Zba`, `Zbb`, and `Sscofpmf`.
- The biggest blockers to claiming full `RVA23S64` on VF2 are lack of evidence
  for `Sstc`, `Svnapot`, `Svpbmt`, `Svinval`, `Ssnpm`, and `Sha`, plus the
  explicit absence of `V`.
- The runner can make lower-privilege ACT execution possible on VF2, but it
  cannot change those hardware facts.

## PMP And Virtual Memory In This Runner

### Current runner behavior

#### PMP

Current implementation:

- In `M` mode, the runner leaves PMP effectively unused.
- In lower-privilege modes, the runner installs explicit TOR PMP regions for:
  - the runner image and its private state/stacks
  - the loaded test payload span, when it sits outside the runner image

Code:

- [runner_lower_env.c](/home/lpt-10xe/vf2_mmode_fw_Final_version_Verified/runner_lower_env.c:284)
- [runner_lower_env.c](/home/lpt-10xe/vf2_mmode_fw_Final_version_Verified/runner_lower_env.c:332)

Practical meaning:

- PMP is now coarse but intentional.
- It constrains lower-privilege physical access to runner and payload regions,
  instead of opening essentially all DDR.
- Final `U` vs `S` separation still comes from Sv39 page permissions, because
  PMP cannot distinguish those two privilege levels.

#### Virtual memory

Current implementation:

- The runner only programs `Sv39`.
- It still uses identity mapping, but only for regions it explicitly needs.
- Runner text/rodata/data/stack pages are mapped supervisor-only.
- Loaded payload segments are mapped with per-segment `R/W/X` permissions.
- The user test stack gets `PTE_U` only for `RUN_PRIV=U`.
- `menvcfg`/`senvcfg` now assume and enable runner-side support for:
  - `Sstc` via `STCE`
  - `Svpbmt` via `PBMTE`
  - `Svadu`-style hardware A/D update via `ADUE`
  - `Ssnpm`/user pointer masking plumbing via `PMM`

Code:

- [runner_lower_env.c](/home/lpt-10xe/vf2_mmode_fw_Final_version_Verified/runner_lower_env.c:207)
- [runner_lower_env.c](/home/lpt-10xe/vf2_mmode_fw_Final_version_Verified/runner_lower_env.c:265)
- [runner_lower_env.c](/home/lpt-10xe/vf2_mmode_fw_Final_version_Verified/runner_lower_env.c:375)

Practical meaning:

- This is enough to execute tests that require `satp`, page translation, and a
  meaningful split between supervisor-only pages and user-visible pages.
- This is still not an OS-style VM subsystem:
  - no multiple address spaces
  - no non-identity user/kernel split
  - no ASID management
  - no demand paging
  - no page replacement
  - no copy-on-write
  - no fine-grained invalidation management beyond a global `sfence.vma`

### Gap table: current runner vs RVA23S64 expectations

| Area | Current runner | RVA23 relevance | Is it enough? |
| --- | --- | --- | --- |
| PMP | Explicit runner/payload TOR regions | Needed so lower-privilege code can legally touch only the intended physical ranges | Better than the old broad aperture, still not process-grade isolation |
| `Sv39` paging | Yes, constrained identity map with per-region permissions | `Sv39` is mandatory in `RVA23S64` | Enough for realistic runner-side `Sv39` execution |
| `satp` programming | Yes | Required for S-mode VM execution | Yes |
| `sfence.vma` | Global flush only | Required ordering primitive, but `Svinval` is a separate feature | Enough for basic bring-up, not equivalent to `Svinval` |
| `stvec` direct mode | Yes | Matches `Sstvecd` expectation | Likely enough for runner plumbing |
| `stval` capture | Captured and reported | `Sstvala` depends on hardware writing correct values | Partial only |
| Hardware A/D behavior | Not managed by runner | `Svade` depends on hardware MMU behavior | Not something runner can guarantee |
| Page-table walker behavior | Uses hardware walker | `Ssccptr` depends on hardware | Runner cannot create this |
| `Svnapot` | Not implemented | Mandatory in full `RVA23S64` | No |
| `Svpbmt` | Not implemented | Mandatory in full `RVA23S64` | No |
| `Svinval` | Not implemented | Mandatory in full `RVA23S64` | No |
| `Sstc` | Runner now enables `menvcfg.STCE` when that path is assumed | Mandatory in full `RVA23S64` | Runner side yes, hardware still required |
| Pointer masking (`Ssnpm`) | Runner now enables `menvcfg/senvcfg.PMM` when that path is assumed | Mandatory in full `RVA23S64` | Runner side yes, hardware still required |

### What to change if you want a stronger execution environment

#### PMP hardening

If the goal is stronger isolation than the current runner:

- The runner already separates runner-private and payload physical regions in PMP.
- The next PMP step would be splitting payload subregions further instead of
  treating the loaded image span as one coarse physical aperture.

Expected outcome:

- Better containment of broken tests.
- Easier debugging of real privilege violations.
- Closer to a real execution environment instead of a permissive harness.

#### Better virtual-memory model

If the goal is to get even closer to realistic `RVA23S64` execution behavior:

- Stop using identity VA=PA and move to distinct supervisor/user virtual layouts.
- Add optional MMIO mappings only when a test explicitly needs them.
- Keep extending per-segment/page permissioning instead of one address space for
  the whole run.
- Track and expose page-table roots per mode instead of one global static map.
- Add targeted `sfence.vma` use around page-table changes.

Expected outcome:

- Better fidelity for privilege and permission tests.
- More realistic `U`/`S` fault behavior.
- Easier future extension if multiple test contexts are needed.

#### Feature gaps the runner cannot solve alone

Even with the changes above, a stronger runner still cannot manufacture missing
hardware/profile features:

- `Svnapot`
- `Svpbmt`
- `Svinval`
- `Sstc`
- `Ssnpm`
- `Sha`

Those remain hardware capability questions on VF2/JH7110.




The only remaining build note is the existing linker warning about an RWX LOAD segment. The envcfg assumptions I used match the official privileged ISA docs for menvcfg/senvcfg and Sstc:

https://docs.riscv.org/reference/isa/priv/machine.html
https://docs.riscv.org/reference/isa/priv/supervisor.html
https://docs.riscv.org/reference/isa/extensions/sstc/_attachments/Sstc.pdf