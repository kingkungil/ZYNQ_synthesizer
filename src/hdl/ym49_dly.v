`timescale 1ns / 1ps
// =============================================================================
//  ym49_dly.v  -  Delay Stage for Moving Averager  (rstn 버전)
//
//  ■ 원본 대비 변경 사항
//    - rst (active-high) → rstn (active-low)
// =============================================================================
module ym49_dly #(
    parameter DW    = 16,
    parameter depth = 8
)(
    input                clk,
    input                cen,
    input                rstn,          // active-low (변경)
    input      [DW-1:0]  din,
    output reg [DW-1:0]  dout,
    output reg [DW-1:0]  pre_dout
);
    reg [depth-1:0] rdpos, wrpos;
 
    (* ram_style = "distributed" *)
    reg [DW-1:0] ram [0:2**depth-1];
 
    always @(posedge clk) begin
        if (!rstn) begin
            pre_dout <= {DW{1'b0}};
        end else begin
            pre_dout <= ram[rdpos];
            if (cen) ram[wrpos] <= din;
        end
    end
 
    `ifdef SIMULATION
    integer k;
    initial begin
        for (k = 0; k < 2**depth; k = k+1)
            ram[k] = {DW{1'b0}};
    end
    `endif
 
    always @(posedge clk) begin
        if (!rstn) begin
            rdpos <= {{depth-1{1'b0}}, 1'b1};
            wrpos <= {depth{1'b1}};
            dout  <= {DW{1'b0}};
        end else if (cen) begin
            dout  <= pre_dout;
            rdpos <= rdpos + 1'b1;
            wrpos <= wrpos + 1'b1;
        end
    end
endmodule