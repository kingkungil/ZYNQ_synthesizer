`timescale 1ns / 1ps

// calculates d=a/b
// a = b*d + r

module ym_adpcm_div #(parameter DW=16)(
    input               rst_n,
    input               clk,    // CPU clock
    input               cen,
    input               start,  // strobe
    input      [DW-1:0] a,
    input      [DW-1:0] b,
    output reg [DW-1:0] d,
    output reg [DW-1:0] r,
    output              working
);

reg  [DW-1:0] cycle;
assign working = cycle[0];

wire [DW:0] sub = { r[DW-2:0], d[DW-1] } - b;  

always @(posedge clk or negedge rst_n)
    if( !rst_n ) begin
        cycle <= 'd0;
    end else if(cen) begin
        if( start ) begin
            cycle <= {DW{1'b1}};
            r     <= 0;
            d     <= a;
        end else if(cycle[0]) begin
            cycle <= { 1'b0, cycle[DW-1:1] };
            if( sub[DW] == 0 ) begin
                r <= sub[DW-1:0];
                d <= { d[DW-2:0], 1'b1};
            end else begin
                r <= { r[DW-2:0], d[DW-1] };
                d <= { d[DW-2:0], 1'b0 };
            end
        end
    end

endmodule // jt10_adpcm_div
