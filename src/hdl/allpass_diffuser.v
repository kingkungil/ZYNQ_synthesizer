`timescale 1ns/1ps
// ============================================================================
//  allpass_diffuser_6.v  - Z-7020 최적화: 6단 직렬 allpass (ALL BRAM)
//
//  v29 대비 변경:
//    8단 → 6단: DSP 4개 + BRAM 2개 절약 (충분한 확산 유지)
//    LUTRAM 전부 제거 → schroeder_ap (BRAM) 통일
//    → LUT 소모 0 (LUTRAM 2800+ LUT 절감)
//
//  BRAM 사용: 6 × BRAM18 = 3 BRAM36
//  DSP 사용: 6 × 2 = 12 DSP48
//  레이턴시: 6단 × 3사이클 = 18사이클
// ============================================================================
module allpass_diffuser_6 #(
    parameter DATA_W = 16,
    parameter D0 =  83,
    parameter D1 = 113,
    parameter D2 = 151,
    parameter D3 = 197,
    parameter D4 = 307,
    parameter D5 = 397
)(
    input  wire                      clk,
    input  wire                      rst_n,
    input  wire                      en,
    input  wire signed [DATA_W-1:0]  din,
    input  wire signed [DATA_W-1:0]  g,
    output wire signed [DATA_W-1:0]  dout,
    output wire                      dout_valid
);

wire signed [DATA_W-1:0] s0,s1,s2,s3,s4;
wire v0,v1,v2,v3,v4;

schroeder_ap #(.DATA_W(DATA_W),.DELAY(D0),.ADDR_W(7))  u0(
    .clk(clk),.rst_n(rst_n),.en(en),  .din(din),.g(g),.dout(s0),.dout_valid(v0));
schroeder_ap #(.DATA_W(DATA_W),.DELAY(D1),.ADDR_W(7))  u1(
    .clk(clk),.rst_n(rst_n),.en(v0),  .din(s0), .g(g),.dout(s1),.dout_valid(v1));
schroeder_ap #(.DATA_W(DATA_W),.DELAY(D2),.ADDR_W(8))  u2(
    .clk(clk),.rst_n(rst_n),.en(v1),  .din(s1), .g(g),.dout(s2),.dout_valid(v2));
schroeder_ap #(.DATA_W(DATA_W),.DELAY(D3),.ADDR_W(8))  u3(
    .clk(clk),.rst_n(rst_n),.en(v2),  .din(s2), .g(g),.dout(s3),.dout_valid(v3));
schroeder_ap #(.DATA_W(DATA_W),.DELAY(D4),.ADDR_W(9))  u4(
    .clk(clk),.rst_n(rst_n),.en(v3),  .din(s3), .g(g),.dout(s4),.dout_valid(v4));
schroeder_ap #(.DATA_W(DATA_W),.DELAY(D5),.ADDR_W(9))  u5(
    .clk(clk),.rst_n(rst_n),.en(v4),  .din(s4), .g(g),.dout(dout),.dout_valid(dout_valid));

endmodule
