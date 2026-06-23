# Board Presets

A board preset describes boot and memory facts for one SoC or board. Profiles
must not duplicate these values.

Minimum fields for a real board:

```bash
BOARD_NAME=my_board
BOARD_DESCRIPTION="Vendor board / SoC"
BOARD_ADAPTER=my_board
BOARD_LINKER_SCRIPT=cert_harness/boards/my_board/link.ld
BOARD_PAYLOAD_TRANSPORT=embedded_pack

BOARD_FW_LOAD_ADDR=0x...
BOARD_FW_STACK_BYTES=0x...
BOARD_RAM_BASE=0x...
BOARD_RAM_LIMIT=0x...

BOARD_UART_BASE=0x...
BOARD_UART_SIZE=0x1000
BOARD_CLINT_MSIP_BASE=0x...
BOARD_CLINT_MTIMECMP_BASE=0x...
BOARD_CLINT_MTIME_ADDR=0x...
BOARD_RUNNER_HART_ID=...
BOARD_MONITOR_HART_ID=...

BOARD_FIXED_TOHOST_ADDR=0x...
BOARD_EXT_PACK_ADDR=0x...
BOARD_EXT_PACK_MAX_BYTES=...

BOARD_SDIO1_BASE=0x...
BOARD_SD_ENABLE=0
BOARD_SD_BLOCK_SIZE=512

BOARD_WDT_ENABLE=0
```

The build wrapper converts these fields into compiler defines and linker
symbols, then stores the result under:

```text
cert_harness/build/<board>/<profile>/<payload_transport>/
```

Hardware actions such as flashing, reset, power control, UART capture, and pack
placement belong in `cert_harness/board_adapters/<board>.sh`.

`BOARD_PAYLOAD_TRANSPORT` controls how tests reach the target:

```text
sd_tail_pack   external ACT pack in SD tail footer area
embedded_pack  ACT pack linked into firmware image
uart_stream    reserved for UART payload streaming
jtag_load      reserved for debugger-loaded payloads
bootloader_ram reserved for bootloader-preloaded payloads
```
