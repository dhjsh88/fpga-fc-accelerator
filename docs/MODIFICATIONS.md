# Modifications to Course-Provided Files

The course files themselves are not included in this repository. This document
records exactly what I changed in each, so the boundary between the course
baseline and my work is unambiguous. (The custom IP was repackaged as `myip`
for this project; file references below use the roles of the modules rather
than the course's original file names.)

## 1. Accelerator top module — stream port, receiver instance, path mux

**Added top-level ports** (AXI-Stream slave, connected to the AXI DMA's
`M_AXIS_MM2S` in the block design):

```verilog
input  wire [31:0]  s_axis_tdata,
input  wire         s_axis_tvalid,
output wire         s_axis_tready,
input  wire         s_axis_tlast,
```

**Instantiated `axis_to_bram`** (my module, `rtl/axis_to_bram.v`) and added
internal nets `w_dma_mode` / `w_dma_target` plus per-BRAM write buses
(`dma_bram0_*`, `dma_bram1_*`).

**Muxed BRAM port-1 write inputs** — the port previously driven only by the
AXI4-Lite register file is now selected at runtime. My mux expressions on each
BRAM instance's port-1 connections:

```verilog
.addr1 (w_dma_mode ? dma_bram0_addr : mem0_addr1),
.ce1   (w_dma_mode ? dma_bram0_we   : mem0_ce1  ),
.we1   (w_dma_mode ? dma_bram0_we   : mem0_we1  ),
.d1    (w_dma_mode ? dma_bram0_din  : mem0_d1   )
```

(same pattern on BRAM1 with `dma_bram1_*` / `mem1_*`). In DMA mode, chip-enable
and write-enable are driven together by the stream handshake.

Rationale for a mux instead of replacement: like-for-like A/B benchmarking on
one bitstream, and a known-good regression path during DMA bring-up.

## 2. AXI wrapper (`myip_v1_0.v`) — pass-through only

Added `o_dma_mode` / `o_dma_target` output ports and wired them through to the
register-file submodule. No logic.

## 3. AXI4-Lite register file (`myip_v1_0_S00_AXI.v`) — control register

Used a previously unused register slot (`slv_reg10`, byte offset 0x28; the
course design decodes 16 slots and used 10). Added:

```verilog
assign o_dma_mode   = slv_reg10[0];  // 0 = PIO (original path), 1 = DMA
assign o_dma_target = slv_reg10[1];  // 0 = BRAM0 (node), 1 = BRAM1 (weight)
```

Readback of the slot was already present in the course read mux, so the CPU
can verify the mode it set.

## 4. Benchmark application (course `main.c`) — additions

- Cache flush of both operand buffers after data generation
  (`Xil_DCacheFlushRange`), required for DMA correctness.
- New menu case `HW_RUN_DMA` mirroring the course's `HW_RUN` stage timing.
- `dma_mm2s_transfer()` with completion polling and timeout.

Full added code: `sw/dma_extension.c`.

## 5. Block design (Vivado, not a text file)

- Enabled Zynq PS `S_AXI_HP0` (high-performance slave port to DDR).
- Added AXI DMA IP: MM2S channel only, Scatter-Gather disabled, buffer length
  register width set to **23 bits** (see `docs/debugging_story.md` for why the
  14-bit default fails silently at exactly this transfer size).
- Connected `M_AXIS_MM2S` → the accelerator's new `s_axis` ports; DMA
  `M_AXI_MM2S` → `S_AXI_HP0`; DMA `S_AXI_LITE` on the GP0 interconnect
  (control via GP0, bulk data via HP0).
