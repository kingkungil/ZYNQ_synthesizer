`timescale 1ns / 1ps

module ym_sh_rst #(parameter width=5, stages=32, rstval=1'b0 )
(
	input					rst,	
	input 					clk,
	input					clk_en /* synthesis direct_enable */,
	input		[width-1:0]	din,
   	output		[width-1:0]	drop
);

reg [stages-1:0] bits[width-1:0];
wire [width-1:0] din_mx = rst ? {width{rstval[0]}} : din;

genvar i;
generate
	for (i=0; i < width; i=i+1) begin: bit_shifter
		always @(posedge clk) if(clk_en) begin
			bits[i] <= {bits[i][stages-2:0], din_mx[i]};
		end
		assign drop[i] = bits[i][stages-1];
	end
endgenerate

endmodule
