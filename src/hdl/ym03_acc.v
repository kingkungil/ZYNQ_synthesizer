`timescale 1ns / 1ps

module ym03_acc
(
    input               rst,
    input               clk,
    input               clk_en /* synthesis direct_enable */,
    input signed [13:0] op_result,
    input               s1_enters,
    input               s2_enters,
    input               s3_enters,
    input               s4_enters,
    input               zero,
    input   [2:0]       alg,
    // combined output
    output signed [15:0] snd
);

reg sum_en;

always @(*) begin
    case ( alg )
        default: sum_en = s4_enters;
        3'd4: sum_en = s2_enters | s4_enters;
        3'd5,3'd6: sum_en = ~s1_enters;        
        3'd7: sum_en = 1'b1;
    endcase
end

// real YM2608 drops the op_result LSB, resulting in a 13-bit accumulator
// but in YM2203, a 13-bit acc for 3 channels only requires 15 bits
// and YM3014 has a 16-bit dynamic range.
// I am leaving the LSB and scaling the output voltage accordingly. This
// should result in less quantification noise.
ym_single_acc #(.win(14),.wout(16)) u_mono(
    .clk        ( clk            ),
    .clk_en     ( clk_en         ),
    .op_result  ( op_result      ),
    .sum_en     ( sum_en         ),
    .zero       ( zero           ),
    .snd        ( snd            )
);

endmodule
