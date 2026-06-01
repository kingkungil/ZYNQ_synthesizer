`timescale 1ns / 1ps

module ym_pg(
    input               clk,
    input               clk_en /* synthesis direct_enable */,
    input               rst,
    // Channel frequency
    input       [10:0]  fnum_I,
    input       [ 2:0]  block_I,
    // Operator multiplying
    input       [ 3:0]  mul_II,
    // Operator detuning
    input       [ 2:0]  dt1_I,
    // phase modulation from LFO
    input       [ 6:0]  lfo_mod,
    input       [ 2:0]  pms_I,
    // phase operation
    input               pg_rst_II,
    input               pg_stop,    // not implemented
    
    output  reg [ 4:0]  keycode_II,
    output      [ 9:0]  phase_VIII
);

parameter num_ch=6;

wire [4:0] keycode_I;
wire signed [5:0] detune_mod_I;
reg signed [5:0] detune_mod_II;
wire [16:0] phinc_I;
reg  [16:0] phinc_II;
wire [19:0] phase_drop, phase_in;
wire [ 9:0] phase_II;

always @(posedge clk) if(clk_en) begin
    keycode_II      <= keycode_I;
    detune_mod_II   <= detune_mod_I;
    phinc_II        <= phinc_I;
end

ym_pg_comb u_comb(
    .block      ( block_I       ),
    .fnum       ( fnum_I        ),
    // Phase Modulation
    .lfo_mod    ( lfo_mod[6:2]  ),
    .pms        ( pms_I         ),

    // Detune
    .detune     ( dt1_I         ),
    .keycode    ( keycode_I     ),
    .detune_out ( detune_mod_I  ),
    // Phase increment  
    .phinc_out  ( phinc_I       ),
    // Phase add
    .mul        ( mul_II        ),
    .phase_in   ( phase_drop    ),
    .pg_rst     ( pg_rst_II     ),
    .detune_in  ( detune_mod_II ),
    .phinc_in   ( phinc_II      ),

    .phase_out  ( phase_in      ),
    .phase_op   ( phase_II      )
);

ym_sh_rst #( .width(20), .stages(4*num_ch) ) u_phsh(
    .clk    ( clk       ),
    .clk_en ( clk_en    ),
    .rst    ( rst       ),
    .din    ( phase_in  ),
    .drop   ( phase_drop)
);

ym_sh_rst #( .width(10), .stages(6) ) u_pad(
    .clk    ( clk       ),
    .clk_en ( clk_en    ),
    .rst    ( rst       ),  
    .din    ( phase_II  ),
    .drop   ( phase_VIII)
);

endmodule

