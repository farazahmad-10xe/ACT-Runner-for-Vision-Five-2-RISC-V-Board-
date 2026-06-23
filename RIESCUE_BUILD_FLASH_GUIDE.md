# Riescue Profile Build & Flash Quick Reference

## ⚠️ IMPORTANT: M-Mode Only

**Riescue MUST run in M-mode only.** S-mode execution is **NOT supported** because Riescue is a bare-metal M-mode runtime that requires direct access to machine-level CSRs (mtvec, mstatus, medeleg, mideleg), which are inaccessible from S-mode.

Attempting S-mode will result in:
```
exit_diagnosis=illegal_machine_csr_access_from_s_mode
exit_insn_decode=csrrw x0, mtvec, x5
```

See `docs/runner_profiles_modes.md` section 5.5 for detailed explanation.

---

## Overview

The Riescue profile is designed for **M-mode execution only**.

| Aspect | Riescue |
|--------|---------|
| **Privilege Mode** | M-mode (only) |
| **Profile Type** | Bare-metal runtime framework |
| **CSR Management** | Direct M-mode control (mtvec, mstatus, etc.) |
| **Isolation Level** | None (M-mode is highest privilege) |
| **Use Case** | Full ISA/framework validation |

---

## Quick Commands

### 📦 Build Riescue (M-Mode)

**One-line build:**
```bash
make -f Makefile.act PAYLOAD_PROFILE=RIESCUE RIESCUE_ELF=special_Tests/tp_zicsr_1_updated_mmp \
  RUNNER_TOUCH_MENVCFG=0 RUNNER_ASSUME_RVA23_PRIV_FEATURES=0 RUNNER_USE_SV39=0
```

**Output**: `firmware_riescue.bin` (291 KB)

---

## Complete Build → Package → Flash Workflow

### Step-by-Step

#### **Step 1: Clean**
```bash
make -f Makefile.act clean
```

#### **Step 2: Build**
```bash
make -f Makefile.act \
  PAYLOAD_PROFILE=RIESCUE \
  RIESCUE_ELF=special_Tests/tp_zicsr_1_updated_mmp \
  RUNNER_TOUCH_MENVCFG=0 \
  RUNNER_ASSUME_RVA23_PRIV_FEATURES=0 \
  RUNNER_USE_SV39=0
```

**Verify output:**
```bash
ls -lh firmware_riescue.{bin,elf,dis}
```

#### **Step 3: Package (Create FIT Image)**
```bash
cp -f firmware_riescue.bin firmware_act.bin
mkimage -f vf2_act4.its uboot_part2_new.bin
truncate -s 4M uboot_part2_new.bin
```

**Verify output:**
```bash
ls -lh uboot_part2_new.bin
file uboot_part2_new.bin
```

#### **Step 4: Flash**
```bash
# ⚠️ IMPORTANT: Verify your SD device first!
lsblk | grep sda

# Flash (adjust /dev/sda if needed)
./vf2_act_flash.sh --image uboot_part2_new.bin --sd-dev /dev/sda
```

#### **Step 5: Verify on Hardware**
```bash
./serial_auto_rebooter.sh \
  --serial-dev /dev/ttyUSB0 \
  --log ./logs/riescue_uart.log \
  --start-cycle \
  --cooldown 20 \
  --boot-cooldown 5 \
  --boot-retries 10 \
  --boot-backoff 45 \
  --cycle-delay 12
```

**Expected output in logs:**
```
[MON] hart2 online
M-mode (VisionFive2) payload runner
payload_kind=RIESCUE
exec_mode=M
[SUITE] Riescue profile: single embedded ELF
[CASE] START name=embedded
[PAYLOAD] kind=RIESCUE entry=0x0000000041000000 tohost=0x0000000041040000
```

---

## 🎯 All-in-One Command

**Fastest way: Build + Package + Flash in one command**

```bash
make -f Makefile.act clean && \
make -f Makefile.act \
  PAYLOAD_PROFILE=RIESCUE \
  RIESCUE_ELF=special_Tests/tp_zicsr_1_updated_mmp \
  RUNNER_TOUCH_MENVCFG=0 \
  RUNNER_ASSUME_RVA23_PRIV_FEATURES=0 \
  RUNNER_USE_SV39=0 && \
cp -f firmware_riescue.bin firmware_act.bin && \
mkimage -f vf2_act4.its uboot_part2_new.bin && \
truncate -s 4M uboot_part2_new.bin && \
./vf2_act_flash.sh --image uboot_part2_new.bin --sd-dev /dev/sda
```

---

## 📊 Configuration Reference

### Riescue M-Mode (Standard)
```make
PAYLOAD_PROFILE=RIESCUE
RIESCUE_ELF=special_Tests/tp_zicsr_1_updated_mmp
RUNNER_TOUCH_MENVCFG=0                          # Don't write menvcfg
RUNNER_USE_SV39=0                              # Disable Sv39 paging
RUNNER_ASSUME_RVA23_PRIV_FEATURES=0            # Don't assume RVA23
```

**Note**: `RUN_PRIV` is fixed to `M` for Riescue and cannot be changed.

---

## 🔍 Verify Build Output

```bash
# Check generated binaries
file firmware_riescue.bin
hexdump -C firmware_riescue.bin | head -20

# Check disassembly for entry point
grep -A 5 "_start\|entry" firmware_riescue.dis | head -20

# Check ELF sections
riscv64-unknown-elf-readelf -l firmware_riescue.elf | head -30
```

---

## ⚡ Troubleshooting

### Build Fails: "PAYLOAD_PROFILE=RIESCUE currently supports RUN_PRIV=M only"
This is **expected and correct**. Riescue only works in M-mode.
- ✅ Use: `PAYLOAD_PROFILE=RIESCUE` (M-mode is default)
- ❌ Do not use: `RUN_PRIV=S` or `RUN_PRIV=U`

### Build Fails: "Make: not found"
```bash
sudo apt-get install build-essential
```

### Build Fails: "riscv64-unknown-elf-gcc: not found"
```bash
# Ensure RISC-V toolchain is in PATH
export PATH=$PATH:~/riscv64/bin
echo $PATH | grep riscv64
```

### FIT Image Creation Fails: "mkimage: command not found"
```bash
sudo apt-get install u-boot-tools
```

### Flash Fails: "Permission denied"
```bash
# Check SD device
ls -l /dev/sda
# Run with sudo if needed
sudo ./vf2_act_flash.sh --image uboot_part2_new.bin --sd-dev /dev/sda
```

### UART Output Shows: "illegal_machine_csr_access_from_s_mode"
This means you somehow enabled S-mode for Riescue:
- Make sure you're NOT using `RUN_PRIV=S` 
- Rebuild with standard M-mode configuration (see above)
- See section 5.5 in `docs/runner_profiles_modes.md` for why

### UART Output: Test timeouts immediately
Riescue may be trying to access unsupported CSRs:
- Use build flags: `RUNNER_TOUCH_MENVCFG=0 RUNNER_USE_SV39=0`
- Consider regenerating Riescue for VF2/U74 platform

---

## 📋 File Outputs Reference

| File | Size | Purpose | Location |
|------|------|---------|----------|
| `firmware_riescue.elf` | ~304 KB | Executable (debugging symbols) | Root |
| `firmware_riescue.bin` | ~291 KB | Raw binary (for embedding) | Root |
| `firmware_riescue.dis` | ~443 KB | Disassembly listing | Root |
| `uboot_part2_new.bin` | 4 MB | FIT image (flash to SD) | Root |

---

## 🎓 Detailed Documentation

For complete information, see main documentation in `docs/runner_profiles_modes.md`:
- **Architecture constraints**: Section 5.5 (why S-mode doesn't work)
- **Configuration**: Section 3.1-3.3
- **Riescue M-Mode**: Section 5.2
- **Build procedures**: Section 7.4
- **Troubleshooting**: Section 9

---

## 💡 Tips

1. **Always verify SD device before flashing:**
   ```bash
   lsblk | grep -E "sda|sdb"
   ```

2. **Keep a backup of working firmware:**
   ```bash
   cp -v firmware_riescue.bin firmware_riescue.bin.backup
   ```

3. **Test build without hardware first:**
   ```bash
   make -f Makefile.act && echo "BUILD SUCCESS"
   ```

4. **Monitor UART output in real-time:**
   ```bash
   screen /dev/ttyUSB0 115200
   # Exit: Ctrl-A, then Ctrl-\
   ```

5. **Remember: Riescue = M-mode only**
   - Don't try S-mode
   - Don't try U-mode
   - Riescue manages its own trap handlers

---

**Last Updated**: May 5, 2026  
**Status**: ✅ M-mode fully operational | ❌ S-mode NOT supported  
**Platform**: VisionFive2 / JH7110



#### **Step 1: Clean**
```bash
make -f Makefile.act clean
```

#### **Step 2: Build**
```bash
make -f Makefile.act \
  PAYLOAD_PROFILE=RIESCUE \
  RUN_PRIV=S \
  RUNNER_DELEG_POLICY=DELEGATION_POLICY_S_BASE \
  RIESCUE_ELF=special_Tests/tp_zicsr_1_updated_mmp \
  RUNNER_TOUCH_MENVCFG=0 \
  RUNNER_ASSUME_RVA23_PRIV_FEATURES=0 \
  RUNNER_USE_SV39=0
```

**Verify output:**
```bash
ls -lh firmware_riescue.{bin,elf,dis}
```

#### **Step 3: Package (Create FIT Image)**
```bash
cp -f firmware_riescue.bin firmware_act.bin
mkimage -f vf2_act4.its uboot_part2_new.bin
truncate -s 4M uboot_part2_new.bin
```

**Verify output:**
```bash
ls -lh uboot_part2_new.bin
file uboot_part2_new.bin
```

#### **Step 4: Flash**
```bash
# ⚠️ IMPORTANT: Verify your SD device first!
lsblk | grep sda

# Flash (adjust /dev/sda if needed)
./vf2_act_flash.sh --image uboot_part2_new.bin --sd-dev /dev/sda
```

#### **Step 5: Verify on Hardware**
```bash
./serial_auto_rebooter.sh \
  --serial-dev /dev/ttyUSB0 \
  --log ./logs/riescue_uart.log \
  --start-cycle \
  --cooldown 20 \
  --boot-cooldown 5 \
  --boot-retries 10 \
  --boot-backoff 45 \
  --cycle-delay 12
```

**Expected output in logs:**
```
[MON] hart2 online
M-mode (VisionFive2) payload runner
payload_kind=RIESCUE
exec_mode=S
[SUITE] Riescue profile: single embedded ELF
[FLOW] isolated_env_prepare from_mode=M target_mode=S
[PAYLOAD] kind=RIESCUE entry=0x0000000041000000 tohost=0x0000000041040000
```

---

## 🎯 All-in-One Command

**Fastest way: Build + Package + Flash in one command**

```bash
make -f Makefile.act clean && \
make -f Makefile.act \
  PAYLOAD_PROFILE=RIESCUE \
  RUN_PRIV=S \
  RUNNER_DELEG_POLICY=DELEGATION_POLICY_S_BASE \
  RIESCUE_ELF=special_Tests/tp_zicsr_1_updated_mmp \
  RUNNER_TOUCH_MENVCFG=0 \
  RUNNER_ASSUME_RVA23_PRIV_FEATURES=0 \
  RUNNER_USE_SV39=0 && \
cp -f firmware_riescue.bin firmware_act.bin && \
mkimage -f vf2_act4.its uboot_part2_new.bin && \
truncate -s 4M uboot_part2_new.bin && \
./vf2_act_flash.sh --image uboot_part2_new.bin --sd-dev /dev/sda
```

---

## 📊 Build Configuration Reference

### Riescue M-Mode (Standard)
```make
PAYLOAD_PROFILE=RIESCUE
RIESCUE_ELF=special_Tests/tp_zicsr_1_updated_mmp
RUNNER_TOUCH_MENVCFG=0                          # Don't write menvcfg
RUNNER_USE_SV39=0                              # Disable Sv39 paging
RUNNER_ASSUME_RVA23_PRIV_FEATURES=0            # Don't assume RVA23
```

**Note**: `RUN_PRIV` is fixed to `M` for Riescue and cannot be changed.

---

## 🔍 Verify Build Output

```bash
# Check generated binaries
file firmware_riescue.bin
hexdump -C firmware_riescue.bin | head -20

# Check disassembly for entry point
grep -A 5 "_start\|entry" firmware_riescue.dis | head -20

# Check ELF sections
riscv64-unknown-elf-readelf -l firmware_riescue.elf | head -30
```

---

## ⚡ Troubleshooting

### Build Fails: "Make: not found"
```bash
sudo apt-get install build-essential
```

### Build Fails: "riscv64-unknown-elf-gcc: not found"
```bash
# Ensure RISC-V toolchain is in PATH
export PATH=$PATH:~/riscv64/bin
echo $PATH | grep riscv64
```

### FIT Image Creation Fails: "mkimage: command not found"
```bash
sudo apt-get install u-boot-tools
```

### Flash Fails: "Permission denied"
```bash
# Check SD device
ls -l /dev/sda
# Run with sudo if needed
sudo ./vf2_act_flash.sh --image uboot_part2_new.bin --sd-dev /dev/sda
```

### UART Output Garbled
```bash
# Verify serial device and speed
ls -l /dev/ttyUSB*
# Check with minicom/picocom
picocom -b 115200 /dev/ttyUSB0
```

---

## 📋 File Outputs Reference

| File | Size | Purpose | Location |
|------|------|---------|----------|
| `firmware_riescue.elf` | ~304 KB | Executable (debugging symbols) | Root |
| `firmware_riescue.bin` | ~291 KB | Raw binary (for embedding) | Root |
| `firmware_riescue.dis` | ~443 KB | Disassembly listing | Root |
| `uboot_part2_new.bin` | 4 MB | FIT image (flash to SD) | Root |

---

## 🎓 Detailed Documentation

For complete information, see main documentation:
- **Architecture**: Section 2 in `docs/runner_profiles_modes.md`
- **Configuration**: Section 3 in `docs/runner_profiles_modes.md`
- **Riescue M-Mode**: Section 5.2 in `docs/runner_profiles_modes.md`
- **Riescue S-Mode**: Section 5.5 in `docs/runner_profiles_modes.md`
- **Build/Flash**: Section 7 in `docs/runner_profiles_modes.md`

---

## 💡 Tips

1. **Always verify SD device before flashing:**
   ```bash
   lsblk | grep -E "sda|sdb"
   ```

2. **Keep a backup of working firmware:**
   ```bash
   cp -v firmware_riescue.bin firmware_riescue.bin.backup
   ```

3. **Test build without hardware first:**
   ```bash
   make -f Makefile.act && echo "BUILD SUCCESS"
   ```

4. **Monitor UART output in real-time:**
   ```bash
   screen /dev/ttyUSB0 115200
   # Exit: Ctrl-A, then Ctrl-\
   ```

5. **Check flash tool compatibility:**
   ```bash
   ./vf2_act_flash.sh --help
   ```

---

**Last Updated**: May 5, 2026  
**Makefile Version**: Supports M and S modes  
**Status**: ✅ Fully operational for VisionFive2 / JH7110
