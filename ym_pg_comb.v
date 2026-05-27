`timescale 1ns / 1ps

module ym_pg_comb(
    input       [ 2:0]  block,
    input       [10:0]  fnum,
    // Phase Modulation
    input       [ 4:0]  lfo_mod,
    input       [ 2:0]  pms,
    // output       [ 7:0]  pm_out,

    // Detune
    input       [ 2:0]  detune,

    output  [ 4:0]  keycode,
    output  signed [5:0] detune_out,
    // Phase increment  
    output  [16:0]  phinc_out,
    // Phase add
    input       [ 3:0]  mul,
    input       [19:0]  phase_in,
    input               pg_rst,
    // input signed [7:0]   pm_in,
    input signed [5:0]  detune_in,
    input       [16:0]  phinc_in,

    output  [19:0]  phase_out,
    output  [ 9:0]  phase_op
);

wire signed [8:0] pm_offset;

/*  pm, pg_dt and pg_inc operate in parallel */ 
ym_pm u_pm(
    .lfo_mod    ( lfo_mod       ),
    .fnum       ( fnum          ),
    .pms        ( pms           ),
    .pm_offset  ( pm_offset     )
);

ym_pg_dt u_dt(
    .block      ( block     ),
    .fnum       ( fnum      ),
    .detune     ( detune    ),
    .keycode    ( keycode   ),
    .detune_signed( detune_out  )
);

ym_pg_inc u_inc(
    .block      ( block     ),
    .fnum       ( fnum      ),
    .pm_offset  ( pm_offset ),
    .phinc_pure ( phinc_out )
);

// pg_sum uses the output from the previous blocks

ym_pg_sum u_sum(
    .mul            ( mul           ),
    .phase_in       ( phase_in      ),
    .pg_rst         ( pg_rst        ),
    .detune_signed  ( detune_in     ),
    .phinc_pure     ( phinc_in      ),
    .phase_out      ( phase_out     ),
    .phase_op       ( phase_op      )
);

endmodule 