# medeleg_probe

Purpose:

Probe which synchronous exception causes can be delegated by `medeleg` on VF2/U74.

The test now prints:

```text
[DBG] medeleg_warl_mask=0x...
[WARLDBG] cause=0x...
[WARLDBG] writable 1=yes 0=no =0x...
```

Interpretation:

- `writable=1`: the corresponding `medeleg[cause]` bit can be set.
- `writable=0`: the corresponding trap cause cannot be delegated to S-mode on this hardware.

Cause mapping for bits 0..15:

```text
0  instruction address misaligned
1  instruction access fault
2  illegal instruction
3  breakpoint
4  load address misaligned
5  load access fault
6  store/AMO address misaligned
7  store/AMO access fault
8  environment call from U-mode
9  environment call from S-mode
10 reserved
11 environment call from M-mode
12 instruction page fault
13 load page fault
14 reserved
15 store/AMO page fault
```

Previous VF2 observation:

```text
medeleg_warl_mask = 0x000000000000b15d
```

This means writable causes:

```text
0, 2, 3, 4, 6, 8, 12, 13, 15
```

and not writable / not delegatable causes:

```text
1, 5, 7, 9, 10, 11, 14
```

Latest hardware run:

`medeleg_probe_all_faults_uart.log`

Observed:

```text
medeleg_warl_mask = 0x000000000000b15d
```

Per-cause result from `[WARLDBG]`:

```text
cause 0  writable=1  instruction address misaligned
cause 1  writable=0  instruction access fault
cause 2  writable=1  illegal instruction
cause 3  writable=1  breakpoint
cause 4  writable=1  load address misaligned
cause 5  writable=0  load access fault
cause 6  writable=1  store/AMO address misaligned
cause 7  writable=0  store/AMO access fault
cause 8  writable=1  environment call from U-mode
cause 9  writable=0  environment call from S-mode
cause 10 writable=0  reserved
cause 11 writable=0  environment call from M-mode
cause 12 writable=1  instruction page fault
cause 13 writable=1  load page fault
cause 14 writable=0  reserved
cause 15 writable=1  store/AMO page fault
```

The explicit trap-routing checks also confirm:

```text
cause 1  observed_medeleg=0 -> handled in M
cause 2  observed_medeleg=4 -> handled in S
cause 3  observed_medeleg=8 -> handled in S
cause 4  observed_medeleg=0x10 -> handled in S
cause 6  observed_medeleg=0x40 -> handled in S
cause 9  observed_medeleg=0 -> handled in M
```
