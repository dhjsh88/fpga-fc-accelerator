# Architecture

## Baseline (course design)

```
                 AXI4-Lite (Zynq GP0)
  Cortex-A9  ──────────────────────────▶  Register file ──▶ BRAM0/1 (port 1)
  (bare-metal)   8,192 single-word writes                     │ (port 0)
                                                              ▼
                                              data mover ──▶ 4x MAC cores ──▶ result regs
```

Two 4,096 x 32-bit operand arrays (node, weight; four 8-bit lanes packed per
word) are loaded by the CPU one word at a time through the AXI4-Lite register
file. Each write carries a full address/data/response transaction plus CPU
loop overhead. The four MAC cores then each accumulate 4,096 products
(one per clock) and expose four 32-bit results over AXI4-Lite.

Measured (baseline bitstream, -O2): loading 1,667 µs = 97.5% of the
1,710 µs end-to-end, compute 41.5 µs. On the later DMA-enabled bitstream the
same PIO path measures 1,846 µs — see the measurement notes in
`results/benchmark_summary.md`.

## After the DMA redesign (my extension)

```
                 AXI4-Lite (GP0): control only
  Cortex-A9  ──────────────────────────▶  Register file ──┐ slv_reg10: mode/target
      │                                                    ▼
      │ one descriptor per array              ┌──────── mux (PIO / DMA) ──▶ BRAM0/1 (port 1)
      ▼                                       │
  AXI DMA (MM2S) ──▶ AXI-Stream ──▶ axis_to_bram
      ▲
      └── S_AXI_HP0 ◀── DDR  (burst reads; CPU flushes D-cache first)
```

- Control stays on GP0 (narrow, low-latency); bulk data moves through HP0
  (wide, burst-capable). Separating command and data paths is the standard
  SoC pattern this design converges to.
- `axis_to_bram` (my RTL) converts the address-less stream into sequential
  BRAM writes: count accepted beats, reset on `TLAST`. `tready` is held high
  because the BRAM sinks one word per clock — no backpressure needed.
- The original PIO path is retained behind a runtime mux (`slv_reg10[0]`),
  which enabled same-bitstream A/B benchmarking and served as a known-good
  regression path during DMA bring-up.
- The DMA path touches only the BRAM *write* port (port 1); the compute-side
  read port (port 0) and the cores are unchanged, which is why compute time
  is identical on both paths (41.6 µs).

## Compute core (course design, unmodified)

Four parallel MAC lanes; each accumulates node[i] x weight[i] over 4,096
iterations, one product per clock — BRAM read → 8-bit multiply → 32-bit
accumulate in a single cycle. Compute time is identical on both data paths;
the DMA redesign touches only data movement, which is the point.

## Verification

The course application's golden model (same computation on the A9) and CHECK
comparison are reused unchanged. Every configuration measured in `results/`
passes bit-exact comparison across all four accumulators.

## Timing status (known limitation)

Baseline implementation misses 100 MHz timing: WNS −0.905 ns, 51 failing
endpoints, all of them the cores' accumulate registers — a single-cycle
BRAM-read → 8-bit multiply → 32-bit accumulate path implemented in LUT/carry
logic (0 of 80 DSP48s used). The identified fix (pipeline register between
multiply and accumulate, +1 cycle latency at unchanged throughput; map
multiplies to DSP48) has not yet been applied; the design operates correctly
at the effective ~92 MHz margin in practice. Details: `results/timing_baseline.md`.
