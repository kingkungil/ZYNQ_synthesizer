`timescale 1ns / 1ps

/* Part of the audio system.
    - All 'jt12' prefixes replaced with 'sound'.
    - Logic: Volume amplification and Stereo mixing (FM + PSG).
*/

module sound_amp(
    input            clk,
    input            rst,
    input            sample,
    input    [2:0]   volume,

    input        signed    [13:0]    pre,    
    output    reg signed    [15:0]    post
);

wire signed [14:0] x2 = pre<<<1;
wire signed [15:0] x3 = x2+pre;
wire signed [15:0] x4 = pre<<<2;
wire signed [16:0] x6 = x4+x2;
wire signed [16:0] x8 = pre<<<3;
wire signed [17:0] x12 = x8+x4;
wire signed [17:0] x16 = pre<<<4;

always @(posedge clk)
if( rst )
    post <= 16'd0;
else
if( sample )
    case( volume ) 
        3'd0: // /2
            post <= { {2{pre[13]}}, pre };
        3'd1: // x1
            post <= { x2[14], x2    };
        3'd2: // x2
            post <= { x2, 1'd0       };
        3'd3: // x4
            post <= x4;
        3'd4: // x6
            casex( x6[16:15] )
                2'b00, 2'b11: post <= x6[15:0];
                2'b0x: post <= 16'h7FFF;
                2'b1x: post <= 16'h8000;
            endcase             
        3'd5: // x8
            casex( x8[16:15] )
                2'b00, 2'b11: post <= x8[15:0];
                2'b0x: post <= 16'h7FFF;
                2'b1x: post <= 16'h8000;
            endcase
        3'd6: // x12
            casex( x12[17:15] )
                3'b000, 3'b111: post <= x12[15:0];
                3'b0xx: post <= 16'h7FFF;
                3'b1xx: post <= 16'h8000;                
            endcase 
        3'd7: // x16
            casex( x16[17:15] )
                3'b000, 3'b111: post <= x16[15:0];
                3'b0xx: post <= 16'h7FFF;
                3'b1xx: post <= 16'h8000;                
            endcase                     
    endcase

endmodule

module sound_amp_stereo(
    input            clk,
    input            rst,
    input            sample,

    input            [ 5:0]    psg,
    input            enable_psg,

    input    signed    [11:0]    fmleft,
    input    signed    [11:0]    fmright,
    input    [2:0]    volume,
    
    output    signed    [15:0]    postleft,
    output    signed    [15:0]    postright
);

wire signed    [13:0]    preleft;
wire signed    [13:0]    preright;

wire signed [8:0] psg_dac = psg<<<3;
wire signed [12:0] psg_sum = {13{enable_psg}} & { 2'b0, psg_dac, 1'b0 };

assign preleft = {  fmleft [11], fmleft, 1'd0 } + psg_sum;
assign preright= {  fmright[11], fmright, 1'd0 } + psg_sum;

// jt12_amp -> sound_amp 로 변경
sound_amp amp_left(
    .clk    ( clk        ),
    .rst    ( rst        ),
    .sample    ( sample    ),
    .pre    ( preleft    ),
    .post    ( postleft    ),
    .volume    ( volume    )
);

sound_amp amp_right(
    .clk    ( clk        ),
    .rst    ( rst        ),
    .sample    ( sample    ),
    .pre    ( preright    ),
    .post    ( postright    ),
    .volume    ( volume    )
);

endmodule