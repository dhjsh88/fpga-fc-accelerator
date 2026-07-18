//------------------------------------------------------------------------------
// axis_to_bram.v
//
// AXI-Stream to BRAM write bridge.
//
// AXI-Stream carries data with valid/ready handshaking but no addressing.
// This module generates what the stream lacks: a write address. Each accepted
// beat (tvalid & tready) writes s_axis_tdata into the selected BRAM at an
// auto-incrementing address; TLAST resets the address counter for the next
// transfer.
//
// tready is tied high: the BRAM accepts one write per cycle, so the sink can
// always keep pace with the DMA stream (one word per clock, no backpressure).
//
// i_target selects the destination BRAM (0 = node/BRAM0, 1 = weight/BRAM1),
// set by the CPU through an AXI4-Lite control register before each transfer.
//
// Author: Minjun Jang
//------------------------------------------------------------------------------
module axis_to_bram #(
    parameter ADDR_WIDTH = 12,          // 4096-deep BRAM -> 12-bit address
    parameter DATA_WIDTH = 32
)(
    input                       clk,
    input                       rst_n,      // active-low reset

    // AXI-Stream slave (driven by AXI DMA MM2S)
    input  [DATA_WIDTH-1:0]     s_axis_tdata,
    input                       s_axis_tvalid,
    output                      s_axis_tready,
    input                       s_axis_tlast,

    // Destination select (from AXI4-Lite control register)
    input                       i_target,   // 0 = BRAM0 (node), 1 = BRAM1 (weight)

    // BRAM0 write port
    output                      o_bram0_we,
    output [ADDR_WIDTH-1:0]     o_bram0_addr,
    output [DATA_WIDTH-1:0]     o_bram0_din,

    // BRAM1 write port
    output                      o_bram1_we,
    output [ADDR_WIDTH-1:0]     o_bram1_addr,
    output [DATA_WIDTH-1:0]     o_bram1_din
);

    reg [ADDR_WIDTH-1:0] r_addr;

    // Always ready: BRAM write completes in one cycle.
    assign s_axis_tready = 1'b1;

    // Handshake: one beat accepted this cycle.
    wire w_hs = s_axis_tvalid & s_axis_tready;

    // Address counter: +1 per accepted beat; reset to 0 on TLAST
    // so the next transfer starts at address 0.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            r_addr <= {ADDR_WIDTH{1'b0}};
        else if (w_hs) begin
            if (s_axis_tlast)
                r_addr <= {ADDR_WIDTH{1'b0}};
            else
                r_addr <= r_addr + 1'b1;
        end
    end

    // Route the write to the selected BRAM only.
    assign o_bram0_we   = w_hs & (i_target == 1'b0);
    assign o_bram1_we   = w_hs & (i_target == 1'b1);
    assign o_bram0_addr = r_addr;
    assign o_bram1_addr = r_addr;
    assign o_bram0_din  = s_axis_tdata;
    assign o_bram1_din  = s_axis_tdata;

endmodule
