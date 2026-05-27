`timescale 1ns / 1ps

module ym_adpcm_drvB(
    input           rst_n,
    input           clk,
    input           cen,      // 8MHz cen
    input           cen55,    // clk & cen55  =  55 kHz
    // Control
    input           acmd_on_b,  // Control - Process start, Key On
    input           acmd_rep_b, // Control - Repeat
    input           acmd_rst_b, // Control - Reset
    input           acmd_up_b, // Control - New command received
    input    [ 1:0] alr_b,      // Left / Right
    input    [15:0] astart_b,   // Start address
    input    [15:0] aend_b,     // End   address
    input    [15:0] adeltan_b,  // Delta-N
    input    [ 7:0] aeg_b,      // Envelope Generator Control
    output          flag,
    input           clr_flag,
    // memory
    output   [23:0] addr,
    input    [ 7:0] data,
    output reg roe_n,

    output reg signed [15:0]  pcm55_l,
    output reg signed [15:0]  pcm55_r
);

wire nibble_sel;
wire adv;           // advance to next reading
wire clr_dec;
wire chon;

// `ifdef SIMULATION
// real fsample;
// always @(posedge acmd_on_b) begin
//     fsample = adeltan_b;
//     fsample = fsample/65536;
//     fsample = fsample * 55.5;
//     $display("\nINFO: ADPCM-B ON: %X delta N = %6d (%2.1f kHz)", astart_b, adeltan_b, fsample );
// end
// `endif

always @(posedge clk) roe_n <= ~(adv & cen55);

ym_adpcmb_cnt u_cnt(
    .rst_n       ( rst_n           ),
    .clk         ( clk             ),
    .cen         ( cen55           ),
    .delta_n     ( adeltan_b       ),
	 .acmd_up_b   ( acmd_up_b       ),
    .clr         ( acmd_rst_b      ),
    .on          ( acmd_on_b       ),
    .astart      ( astart_b        ),
    .aend        ( aend_b          ),
    .arepeat     ( acmd_rep_b      ),
    .addr        ( addr            ),
    .nibble_sel  ( nibble_sel      ),
    // Flag control
    .chon        ( chon            ),
    .clr_flag    ( clr_flag        ),
    .flag        ( flag            ),
    .clr_dec     ( clr_dec         ),
    .adv         ( adv             )
);

reg [3:0] din;

always @(posedge clk) din <= !nibble_sel ? data[7:4] : data[3:0];

wire signed [15:0] pcmdec, pcminter, pcmgain;

ym_adpcmb u_decoder(
    .rst_n  ( rst_n          ),
    .clk    ( clk            ),
    .cen    ( cen            ),
    .adv    ( adv & cen55    ),
    .data   ( din            ),
    .chon   ( chon           ),
    .clr    ( clr_dec        ),
    .pcm    ( pcmdec         )
);

`ifndef NOBINTERPOL
ym_adpcmb_interpol u_interpol(
    .rst_n  ( rst_n          ),
    .clk    ( clk            ),
    .cen    ( cen            ),
    .cen55  ( cen55  && chon ),
    .adv    ( adv            ),
    .pcmdec ( pcmdec         ),
    .pcmout ( pcminter       )
);
`else 
assign pcminter = pcmdec;
`endif

ym_adpcmb_gain u_gain(
    .rst_n  ( rst_n          ),
    .clk    ( clk            ),
    .cen55  ( cen55          ),
    .tl     ( aeg_b          ),
    .pcm_in ( pcminter       ),
    .pcm_out( pcmgain        )
);

always @(posedge clk) if(cen55) begin
    pcm55_l <= alr_b[1] ? pcmgain : 16'd0;
    pcm55_r <= alr_b[0] ? pcmgain : 16'd0;
end

endmodule 
