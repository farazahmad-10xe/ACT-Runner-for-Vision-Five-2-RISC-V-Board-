# Runner harness

The harness has two board presets and one execution profile:

- `boards/vf2_jh7110.env`
- `boards/bpif3_k1.env`
- `profiles/UART_M_MODE.env`

Build with `tools/build_runner.sh --board <board>`. UART host protocol and batch
tools live in `uart_stream/`. There is no payload-pack or SD-card adapter in
this branch.
