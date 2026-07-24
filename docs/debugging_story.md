# Debugging a Silent DMA Failure

The most instructive failure in this project: the first DMA bring-up hung with
**no error indication anywhere**. This document records the actual diagnostic
path, with the real status-register values from the board.

## Symptom

Selecting the new DMA benchmark case froze the application before any output.
A subsequent CHECK was also unresponsive — consistent with the CPU being stuck
inside the DMA case, not with two independent failures. The original PIO path
on the same bitstream still worked, showing that the baseline accelerator and
original control path were intact and narrowing the investigation to the
newly added DMA path — while DMA-specific reset, addressing, and interface
connectivity still remained to be checked.

## Instrumentation

The transfer routine was rebuilt as a diagnostic version: read and print the
MM2S status register (SR) on entry, after kicking the transfer, and inside a
polling loop with a timeout (so a hang becomes a printable state instead of a
freeze). Observed on the board:

```
DMA: enter,  SR=0x00000001    <- Halted (engine not yet started: expected)
DMA: kicked, SR=0x00000000    <- running... and it stays here forever
```

(The status chronology here was transcribed from the live bring-up terminal;
the raw capture from this diagnostic session was not retained.)

## Reading the state

`SR=0x00000000` is the interesting value — it rules things *out*:

No internal DMA, slave, or decode error bits were set, so the status register
did not indicate those fault classes. A completed transfer would eventually
assert **Idle** (and **IOC**), but neither appeared. This suggested the
transfer might not have been launched with a nonzero effective length —
leading to inspection of the programmed length against the configured
register width.

## Root cause

We program the length as `MEM_DEPTH * 4 = 4096 * 4 = 16,384` bytes.

The AXI DMA's *Width of Buffer Length Register* defaults to **14 bits**,
capping a single transfer at **16,383 bytes**. Our size exceeds the cap by
**exactly one byte**; the write truncates to 0. Confirmed in the generated
hardware header:

```
#define XPAR_AXI_DMA_0_SG_LENGTH_WIDTH 14
```

A zero-length transfer is not an error condition — the engine simply has no
work — which is why every error flag stayed low.

## Fix and verification

Set the length-register width to 23 bits in the DMA IP configuration
(max 8 MB), regenerated the bitstream. No software change needed.

![AXI DMA configuration — length register widened to 23 bits](images/dma_ip_length_23bit.png)

The same dialog shows the rest of the DMA configuration described in
`MODIFICATIONS.md`: Scatter-Gather disabled, read (MM2S) channel only.
Next run:

```
DMA: done, SR=0x00001002      <- Idle + IOC: transfer complete
```

followed by a bit-exact match against the software golden reference, and load
time of 42.4 µs — within about 3.6% of the 40.96 µs ideal transfer time,
consistent with near-ideal aggregate throughput including fixed setup
overhead (beat-level stalls were not instrumented).

## Secondary lesson: instrumentation cost

The diagnostic printf calls inside the timed region inflated the measured load
time to ~1,400–7,000 µs (UART at 115,200 baud dominates). Removing them
revealed the true ~42.4 µs. Measurement code can perturb the measurement.
