`timescale 1ns / 1ps

module psg_fm_mix(
    input                rst,
    input                clk,
    input signed [15:0] fm_left,
    input signed [15:0] fm_right,
    input signed [10:0] psg_snd,
    input fm_en,  // enable FM
    input psg_en, // enable PSG
    // Mixed sound at clk sample rate
    output signed [15:0] snd_left,
    output signed [15:0] snd_right
);

/////////////////////////////////////////////////
// PSG
// x5 -> div 3 -> div 7
// 54MHz count up to:
// orig -> 16*15 = 240
// x5 -> 16*15/5 = 48
// div 3 -> 48*3 = 144
// div 7 -> 144*7 = 1008 <=> fm_sample
reg [5:0] psgcnt48;
reg [2:0] psgcnt240, psgcnt1008;
reg [1:0] psgcnt144;

always @(posedge clk)
    if( rst ) begin
        psgcnt48  <= 6'd0;
        psgcnt240 <= 3'd0;
        psgcnt144 <= 2'd0;
        psgcnt1008<= 3'd0;
    end else begin
        psgcnt48  <= psgcnt48 ==6'd47  ? 6'd0 : psgcnt48 +6'd1;
        if( psgcnt48 == 6'd47 ) begin
            psgcnt240 <= psgcnt240==3'd4 ? 3'd0 : psgcnt240+3'd1;
            psgcnt144  <= psgcnt144 ==2'd2 ? 2'd0 : psgcnt144+2'd1;
            if( psgcnt144==2'd0 )
                psgcnt1008 <= psgcnt1008==3'd6 ? 3'd0 : psgcnt1008+3'd1;
        end
    end

reg psg_cen_1008, psg_cen_240, psg_cen_48, psg_cen_144;
always @(posedge clk) begin
    psg_cen_240 <= psgcnt48 ==6'd47 && psgcnt240 == 3'd0;
    psg_cen_48  <= psgcnt48 ==6'd47;
    psg_cen_144 <= psgcnt48 ==6'd47 && psgcnt144==2'd0;
    psg_cen_1008<= psgcnt48 ==6'd47 && psgcnt144==2'd0 && psgcnt1008==3'd0;
end

wire signed [11:0] psg0, psg1, psg2, psg3; 
assign psg0 = psg_en ? { psg_snd[10], psg_snd } : 12'b0;

// 48
psg_fm_interpol #(.calcw(19),.inw(12),.rate(5),.m(4),.n(2)) 
u_psg1(
    .clk    ( clk      ),
    .rst    ( rst      ),        
    .cen_in ( psg_cen_240  ),
    .cen_out( psg_cen_48   ),
    .snd_in ( psg0     ),
    .snd_out( psg1     )
);

// 144 
psg_fm_decim #(.calcw(19),.inw(12),.rate(3),.m(2),.n(3) ) 
u_psg2(
    .clk    ( clk          ),
    .rst    ( rst          ),        
    .cen_in ( psg_cen_48  ),
    .cen_out( psg_cen_144 ),
    .snd_in ( psg1        ),
    .snd_out( psg2        )
);

// 1008
psg_fm_decim #(.calcw(15),.inw(12),.rate(7),.m(1),.n(1) ) 
u_psg3(
    .clk    ( clk          ),
    .rst    ( rst          ),        
    .cen_in ( psg_cen_144 ),
    .cen_out( psg_cen_1008),
    .snd_in ( psg2        ),
    .snd_out( psg3        )
);

/////////////////////////////////////////////////
// FM
// x4 -> x4 -> x7 -> x9
// 54MHz count up to:
// 252 -> 63 -> 9 -> 1
reg [1:0] clkcnt252, clkcnt1008;
reg [2:0] clkcnt63;
reg [3:0] clkcnt9;
always @(posedge clk)
    if( rst ) begin
        clkcnt1008<= 2'd0;
        clkcnt252 <= 2'd0;
        clkcnt63  <= 3'd0;
        clkcnt9   <= 4'd0;
    end else begin
        clkcnt9   <= clkcnt9  ==4'd8   ? 4'd0 : clkcnt9  +4'd1;
        if( clkcnt9== 4'd8 ) begin
            clkcnt63  <= clkcnt63 ==3'd6  ? 3'd0 : clkcnt63 +3'd1;
            if( clkcnt63==3'd6 ) begin
                clkcnt252 <= clkcnt252+2'd1;
                if(clkcnt252==2'd3) clkcnt1008<=clkcnt1008+2'd1;
            end
        end
    end 

reg cen_1008, cen_252, cen_63, cen_9;
always @(posedge clk) begin
    cen_9    <= clkcnt9  ==4'd8;
    cen_63   <= clkcnt9  ==4'd8 && clkcnt63  ==3'd0;
    cen_252  <= clkcnt9  ==4'd8 && clkcnt63  ==3'd0 && clkcnt252 ==2'd0;
    cen_1008 <= clkcnt9  ==4'd8 && clkcnt63  ==3'd0 && clkcnt252 ==2'd0 && clkcnt1008==2'd0;
end

psg_fm_uprate u_left(
    .rst        ( rst       ),
    .clk        ( clk       ),
    .fm_snd     ( fm_left   ),
    .psg_snd    ( psg3      ),
    .fm_en      ( fm_en     ),
    .cen_1008   ( cen_1008  ),
    .cen_252    ( cen_252   ),
    .cen_63     ( cen_63    ),
    .cen_9      ( cen_9     ),
    .snd        ( snd_left  )
);

psg_fm_uprate u_right(
    .rst        ( rst       ),
    .clk        ( clk       ),
    .fm_snd     ( fm_right  ),
    .psg_snd    ( psg3      ),
    .fm_en      ( fm_en     ),
    .cen_1008   ( cen_1008  ),
    .cen_252    ( cen_252   ),
    .cen_63     ( cen_63    ),
    .cen_9      ( cen_9     ),
    .snd        ( snd_right )
);

endmodule