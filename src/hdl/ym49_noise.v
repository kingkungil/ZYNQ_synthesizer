`timescale 1ns / 1ps

/* verilator coverage_off */
module ym49_noise(
  (* direct_enable *) input cen,
    input       clk,
    input       rst_n,
    input [4:0] period,
    output reg  noise
);

reg [5:0]count;
reg [16:0]poly17;
wire poly17_zero = poly17==17'b0;
wire noise_en;
reg last_en;

wire noise_up = noise_en && !last_en;

always @(posedge clk ) if(cen) begin
    noise <= ~poly17[0];
end

always @( posedge clk, negedge rst_n )
  if( !rst_n ) 
    poly17 <= 17'd0;
  else if( cen ) begin
    last_en <= noise_en;
    if( noise_up )
        poly17 <= { poly17[0] ^ poly17[3] ^ poly17_zero, poly17[16:1] };
  end

ym49_div #(5) u_div( 
  .clk    ( clk       ), 
  .cen    ( cen       ),
  .rst_n  ( rst_n     ), 
  .period ( period    ), 
  .div    ( noise_en  ) 
);

endmodule