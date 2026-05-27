`timescale 1ns/1ps
// ============================================================================
//  post_fb_diffuser_2.v  - Z-7020 최적화: 2단 후단 allpass (ALL BRAM)
//
//  v29 대비: 4단→2단, LUTRAM→BRAM 통일
//  BRAM: 2 × BRAM18 = 1 BRAM36
//  DSP: 2 × 2 = 4 DSP48
//  레이턴시: 2단 × 3사이클 = 6사이클
// ============================================================================
module post_fb_diffuser_2 #(
    parameter DATA_W = 16,
    parameter D0 = 67,
    parameter D1 = 89
)(
    input  wire                      clk,
    input  wire                      rst_n,
    input  wire                      en,
    input  wire signed [DATA_W-1:0]  din,
    input  wire signed [DATA_W-1:0]  g,
    output wire signed [DATA_W-1:0]  dout,
    output wire                      dout_valid
);

wire signed [DATA_W-1:0] s0;
wire v0;

schroeder_ap #(.DATA_W(DATA_W),.DELAY(D0),.ADDR_W(7)) u0(
    .clk(clk),.rst_n(rst_n),.en(en), .din(din),.g(g),.dout(s0),.dout_valid(v0));
schroeder_ap #(.DATA_W(DATA_W),.DELAY(D1),.ADDR_W(7)) u1(
    .clk(clk),.rst_n(rst_n),.en(v0), .din(s0), .g(g),.dout(dout),.dout_valid(dout_valid));

endmodule
