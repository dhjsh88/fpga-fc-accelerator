/*------------------------------------------------------------------------------
 * dma_extension.c
 *
 * DMA driver code added to the course's bare-metal benchmark application.
 * Only my additions are published here; the course application itself is not
 * redistributed (see docs/MODIFICATIONS.md for exactly what was changed).
 *
 * Author: Minjun Jang
 *----------------------------------------------------------------------------*/

#include "xparameters.h"
#include "xil_io.h"
#include "xil_cache.h"

/* ---- AXI DMA (direct register mode, MM2S only) ---------------------------- */
#define DMA_BASEADDR      XPAR_AXI_DMA_0_BASEADDR   /* 0x40400000 */
#define MM2S_DMACR        0x00   /* control                                    */
#define MM2S_DMASR        0x04   /* status                                     */
#define MM2S_SA           0x18   /* source address (DDR)                       */
#define MM2S_LENGTH       0x28   /* transfer length in bytes; write starts it  */

/* NOTE on MM2S_LENGTH width — the bug that cost the most time here:
 * The default 14 bits caps at 16,383 bytes; our transfers are
 * 16,384 bytes (4096 x 4B) — one byte over — which silently truncates the
 * programmed length to zero (no error flags, never completes). Set the width
 * to 23 bits in the IP configuration. See docs/debugging_story.md.           */

/* ---- Accelerator control (added register in the course IP) ---------------- */
#define DMA_CTRL_REG      10     /* slv_reg10 @ offset 0x28 on the accelerator */
/* values written: 0x1 = DMA mode -> BRAM0, 0x3 = DMA mode -> BRAM1, 0x0 = PIO */

/*------------------------------------------------------------------------------
 * Issue one MM2S burst transfer and poll for completion.
 * Returns silently on success; prints SR on timeout for diagnosis.
 *----------------------------------------------------------------------------*/
void dma_mm2s_transfer(unsigned int buf_addr, unsigned int len_bytes)
{
    unsigned int sr;
    Xil_Out32(DMA_BASEADDR + MM2S_DMACR, 1);          /* RS = 1: run          */
    Xil_Out32(DMA_BASEADDR + MM2S_SA, buf_addr);      /* source in DDR        */
    Xil_Out32(DMA_BASEADDR + MM2S_LENGTH, len_bytes); /* write starts transfer*/

    int timeout = 20000000;
    while (((sr = Xil_In32(DMA_BASEADDR + MM2S_DMASR)) & 0x2) == 0) {
        if (--timeout == 0) {
            printf("DMA TIMEOUT!! SR=0x%08x\n", sr);  /* keep: silent-failure guard */
            return;
        }
    }
}

/*------------------------------------------------------------------------------
 * Cache coherency: called after generating operand data in DDR buffers.
 * The CPU writes land in the D-cache; the DMA reads DDR directly. Without the
 * flush the DMA reads stale memory — results are wrong with no error anywhere.
 *----------------------------------------------------------------------------*/
void flush_operands(unsigned int *node_buf, unsigned int *wegt_buf, int depth)
{
    Xil_DCacheFlushRange((INTPTR)node_buf, depth * sizeof(unsigned int));
    Xil_DCacheFlushRange((INTPTR)wegt_buf, depth * sizeof(unsigned int));
}

/*------------------------------------------------------------------------------
 * HW_RUN_DMA benchmark case (added alongside the course's HW_RUN).
 * Pseudocode of the flow — stage timing identical to the PIO case so the
 * two paths compare directly:
 *
 *   flush_operands(node, wegt, MEM_DEPTH);
 *
 *   write accel[DMA_CTRL_REG] = 0x1;                       // DMA -> BRAM0
 *   dma_mm2s_transfer((u32)node_buf, MEM_DEPTH * 4);       // timed: 42.4 us 
 *
 *   write accel[DMA_CTRL_REG] = 0x3;                       // DMA -> BRAM1
 *   dma_mm2s_transfer((u32)wegt_buf, MEM_DEPTH * 4);       // timed: 42.4 us 
 *
 *   write accel[DMA_CTRL_REG] = 0x0;                       // back to PIO mode
 *   (compute + result readback: unchanged course flow)     // 41.7 + 0.83 us 
 *
 * Measured on board: end-to-end 127.37 us vs. 1888.54 us PIO baseline.
 *----------------------------------------------------------------------------*/
