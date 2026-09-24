# UART ELF transport

Protocol version 1 transfers one ELF per runner boot:

1. Target emits `READY` with board ID, runner build ID, and maximum size.
2. Host sends an 88-byte header containing size, name, and CRC32.
3. Target acknowledges `HEADER_OK`.
4. Host sends the ELF bytes.
5. Target validates CRC32, loads PT_LOAD segments, and executes in M-mode.
6. Target emits `DONE` with PASS, FAIL, TIMEOUT, or ERROR.

Use `send_elf.py` for a manual case or `run_elf_batch.py` for power-controlled
automation. Host tools can require both the expected board identity and runner
build ID, preventing execution on the wrong target revision.

Run protocol unit tests with:

```bash
python3 -m unittest cert_harness.uart_stream.test_uart_stream
```
