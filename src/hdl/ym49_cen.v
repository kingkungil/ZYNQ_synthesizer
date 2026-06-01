`timescale 1ns / 1ps
/* verilator coverage_off */
module ym49_cen(
    input       clk,
    input       rst_n,
    input       cen,    // base clock enable signal
    input       sel,    // when low, divide by 2 once more
    output  reg cen16,
    output  reg cen256
);

reg [9:0] cencnt;
parameter CLKDIV = 3;
localparam eg = CLKDIV; //8;

wire toggle16 = sel ? ~|cencnt[CLKDIV-1:0] : ~|cencnt[CLKDIV:0];
wire toggle256= sel ? ~|cencnt[eg-2:0]     : ~|cencnt[eg-1:0];


always @(posedge clk, negedge rst_n) begin
    if(!rst_n)
        cencnt <= 10'd0;
    else begin 
        if(cen) cencnt <= cencnt+10'd1;
    end
end

always @(posedge clk) begin
    cen16  <= cen & toggle16;
    cen256 <= cen & toggle256;
end


endmodule
