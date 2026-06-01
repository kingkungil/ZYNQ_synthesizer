`timescale 1ns / 1ps

/* verilator coverage_off */
module ym49_div #(parameter W=12 )(   
    (* direct_enable *) input cen,
    input           clk, // this is the divided down clock from the core
    input           rst_n,
    input [W-1:0]  period,
    output reg      div
);

reg [W-1:0]count;

wire [W-1:0] one = { {W-1{1'b0}}, 1'b1};

always @(posedge clk, negedge rst_n ) begin
  if( !rst_n) begin
    count <= one;
    div   <= 1'b0;
  end else if(cen) begin
    if( count>=period ) begin
        count <= one;
        div   <= ~div;
    end else begin
        count <=  count + one ;
    end
    if(period==0) div<=0;
  end
end

endmodule
