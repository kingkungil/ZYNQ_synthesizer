`timescale 1ns / 1ps

module ym2203(
    input            rst,        // rst should be at least 6 clk&cen cycles long
    input            clk,        // CPU clock
    input            cen,        // optional clock enable, if not needed leave as 1'b1
    input   [7:0]    din,
    input            addr,
    input            cs_n,
    input            wr_n,

    output  [7:0]    dout,
    output           irq_n,
    // I/O pins used by YM2203 embedded YM2149 chip
    input   [7:0]    IOA_in,
    input   [7:0]    IOB_in,
    output  [7:0]    IOA_out,
    output  [7:0]    IOB_out,
    output           IOA_oe,
    output           IOB_oe,
    // Separated output
    output          [ 7:0] psg_A,
    output          [ 7:0] psg_B,
    output          [ 7:0] psg_C,
    output  signed  [15:0] fm_snd,
    // combined output
    output          [ 9:0] psg_snd,
    output  signed  [15:0] snd,
    output                 snd_sample,
    // Debug
    //input            [ 7:0] debug_bus,
    output          [ 7:0] debug_view
);

parameter YM2203_LUMPED=0; // set to 1 if all PSG outputs are shorted together without any resistor


ym_top #(
    .use_lfo(0),.use_ssg(1), .num_ch(3), .use_pcm(0), .use_adpcm(0), .mask_div(0),
    .YM2203_LUMPED(YM2203_LUMPED) )
u_core(
    .rst            ( rst           ),        // rst should be at least 6 clk&cen cycles long
    .clk            ( clk           ),        // CPU clock
    .cen            ( cen           ),        // optional clock enable, it not needed leave as 1'b1
    .din            ( din           ),
    .addr           ( {1'b0, addr}  ),
    .cs_n           ( cs_n          ),
    .wr_n           ( wr_n          ),
    .ch_enable      ( 6'd0          ),

    .dout           ( dout          ),
    .irq_n          ( irq_n         ),
    // YM2203 I/O pins
    .IOA_in         ( IOA_in        ),
    .IOB_in         ( IOB_in        ),
    .IOA_out        ( IOA_out       ),
    .IOB_out        ( IOB_out       ),
    .IOA_oe         ( IOA_oe        ),
    .IOB_oe         ( IOB_oe        ),
    // Unused ADPCM pins
    .en_hifi_pcm    ( 1'b0          ), // used only on YM2612 mode
    .adpcma_addr    (               ), // real hardware has 10 pins multiplexed through RMPX pin
    .adpcma_bank    (               ),
    .adpcma_roe_n   (               ), // ADPCM-A ROM output enable
    .adpcma_data    ( 8'd0          ), // Data from RAM
    .adpcmb_data    ( 8'd0          ),
    .adpcmb_addr    (               ), // real hardware has 12 pins multiplexed through PMPX pin
    .adpcmb_roe_n   (               ), // ADPCM-B ROM output enable
    // Separated output
    .psg_A          ( psg_A         ),
    .psg_B          ( psg_B         ),
    .psg_C          ( psg_C         ),
    .psg_snd        ( psg_snd       ),
    .fm_snd_left    ( fm_snd        ),
    .fm_snd_right   (               ),
    .adpcmA_l       (               ),
    .adpcmA_r       (               ),
    .adpcmB_l       (               ),
    .adpcmB_r       (               ),

    .snd_right      ( snd           ),
    .snd_left       (               ),
    .snd_sample     ( snd_sample    ),

    //.debug_bus      ( debug_bus    ),
    .debug_bus      ( 8'd0          ),
    .debug_view     ( debug_view    )
);

endmodule // ym2203