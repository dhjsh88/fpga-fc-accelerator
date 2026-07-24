# Benchmark Summary

- Board: Zybo Z7-10 (XC7Z010), PL @ 100 MHz; Cortex-A9 bare-metal
- Workload: 4 cores x 4,096 iterations of 8-bit x 8-bit MAC accumulation
  (four packed lanes per 32-bit word; four 32-bit results)
- Verification: bit-exact match against SW golden reference in all reported
  hardware runs
- Raw logs: `serial_logs_baseline.txt`, `serial_logs_dma.txt`

## 1. Headline: PIO vs DMA (same bitstream, -O2, single session)

| Stage | PIO (baseline) | DMA (this work) | Change |
|---|---|---|---|
| BRAM0 load | 923.22 us | 42.43 us | |
| BRAM1 load | 922.83 us | 42.42 us | |
| Loading subtotal | 1846.05 us | **84.85 us** | **21.8x** |
| Core compute | 41.67 us | 41.69 us | unchanged |
| Result readback | 0.83 us | 0.83 us | unchanged |
| **End-to-end** | **1888.54 us** | **127.37 us** | **14.8x** |

Against SW (-O2, 519.26 us): end-to-end flips from **3.6x slower** to
**4.1x faster**.

Sanity check: DMA load of 4,096 words at one word/clock = 40.96 us theoretical;
42.43 us measured — within 3.6% of the ideal streaming time, consistent with
near-one-word-per-cycle delivery after fixed setup overhead. (Beat-level
confirmation would require an ILA or AXI performance monitor; not instrumented.)

## 2. SW optimization level (fairness control)

| SW build | SW compute | HW compute | Pure-compute speedup |
|---|---|---|---|
| -O0 | 1314.29 us | 41.6 us | ~31.6x |
| **-O2 (reported)** | **508-519 us** | 41.5-41.7 us | **~12.2-12.5x** |

HW compute is invariant to compiler flags (PS/PL separation); only the SW
reference and the CPU-driven loading loop change. All headline claims use -O2.

## 3. Where the time goes

| | PIO baseline | After DMA |
|---|---|---|
| Loading share of end-to-end | 97.5% (original baseline session) / 97.8% (same-bitstream A/B session, table above) | 67% |
| Bottleneck | 8,192 single-word AXI-Lite transactions (full address/data/response handshake + CPU loop per word) | burst DMA over HP0; remaining floor is 2 x 4,096-beat streams |

## 4. Measurement notes

- Timing via A9 global timer (`XTime_GetTime`); microsecond values reported.
- The timed region covers DMA transfer + PL compute + readback. Input
  generation and the one-time cache flush (`Xil_DCacheFlushRange`) execute
  before the timed region; the end-to-end figure is accelerator execution time
  excluding input generation and cache maintenance.
- Diagnostic printf inside the timed region inflates results by ~30-160x at
  115,200 baud; removed for all reported numbers (see
  `docs/debugging_story.md`).
- Two PIO datasets are included. The original baseline bitstream (pre-DMA)
  measured 832.98/834.29 us loading times at -O2
  (`serial_logs_baseline.txt`; raw captures `putty_baseline_O0.log`,
  `putty_baseline_O2.log`). The headline table uses the same-bitstream A/B
  session on the DMA-enabled build (923.22/922.83 us,
  `serial_logs_dma.txt`; raw capture `putty_dma_ab.log`), allowing the PIO and
  DMA paths to be compared under identical hardware. The difference between the
  two PIO measurements reflects different builds and does not affect the
  conclusions.
- The -O0 row in section 2 (1314.29 us) is from the baseline-bitstream
  session; HW compute is 41.5-41.7 us in every session, confirming PS/PL
  separation.
