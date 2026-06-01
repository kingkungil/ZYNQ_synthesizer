`timescale 1ns / 1ps

// Gain is assumed to be 0.75dB per bit.

module ym_adpcmb_gain(
    input                    rst_n,
    input                    clk,        // CPU clock
    input                    cen55,
    input             [ 7:0] tl,         // ADPCM Total Level
    input      signed [15:0] pcm_in,
    output reg signed [15:0] pcm_out
);

wire signed [15:0] factor = {8'd0, tl};
wire signed [31:0] pcm_mul = pcm_in * factor; // linear gain

always @(posedge clk) if(cen55)
    pcm_out <= pcm_mul[23:8];

endmodule
