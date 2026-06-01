`timescale 1ns / 1ps

module ym12_acc(
    input               rst,
    input               clk,
    input               clk_en /* synthesis direct_enable */,
    input signed [8:0]  op_result,
    input        [ 1:0] rl,
    input               zero,
    input               s1_enters,
    input               s2_enters,
    input               s3_enters,
    input               s4_enters,
    input               ch6op,
    input   [2:0]       alg,
    input               pcm_en, // only enabled for channel 6
    input   signed [8:0] pcm,
    // combined output
    output reg signed   [11:0]  left,
    output reg signed   [11:0]  right
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

reg pcm_sum;

always @(posedge clk) if(clk_en)
    if( zero ) pcm_sum <= 1'b1;
    else if( ch6op ) pcm_sum <= 1'b0;

wire use_pcm = ch6op && pcm_en;
wire sum_or_pcm = sum_en | use_pcm;
wire left_en = rl[1];
wire right_en= rl[0];
wire signed [8:0] pcm_data = pcm_sum ? pcm : 9'd0;
wire [8:0] acc_input =  use_pcm ? pcm_data : op_result;

// Continuous output
wire signed   [11:0]  pre_left, pre_right;
ym_single_acc #(.win(9),.wout(12)) u_left(
    .clk        ( clk            ),
    .clk_en     ( clk_en         ),
    .op_result  ( acc_input      ),
    .sum_en     ( sum_or_pcm & left_en ),
    .zero       ( zero           ),
    .snd        ( pre_left       )
);

ym_single_acc #(.win(9),.wout(12)) u_right(
    .clk        ( clk            ),
    .clk_en     ( clk_en         ),
    .op_result  ( acc_input      ),
    .sum_en     ( sum_or_pcm & right_en ),
    .zero       ( zero           ),
    .snd        ( pre_right      )
);

// Output can be amplied by 8/6=1.33 to use full range
// an easy alternative is to add 1/4th and get 1.25 amplification
always @(posedge clk) if(clk_en) begin
    left  <= pre_left  + { {2{pre_left [11]}}, pre_left [11:2] };
    right <= pre_right + { {2{pre_right[11]}}, pre_right[11:2] };
end

endmodule
