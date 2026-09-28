/*  Based on JT12 - jt12_fm_uprate.v
    https://github.com/jotego/jt12
    Modified for ZYNQ_synthesizer (module renamed and adapted for Zynq-7020).

 This file is part of JT12.

 
    JT12 program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    JT12 program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with JT12.  If not, see <http://www.gnu.org/licenses/>.

    Author: Jose Tejada Gomez. Twitter: @topapate
    Version: 1.0
    Date: 11-12-2018
    
    Each channel can use the full range of the DAC as they do not
    get summed in the real chip.

    Operator data is summed up without adding extra bits. This is
    the case of real YM3438, which was used on Megadrive 2 models.
*/
`timescale 1ns / 1ps

/* Part of the sound mixing system.
    - All 'jt' prefixes replaced with 'psg_fm'.
    - Logic: Mixes FM & PSG, then upscales the sample rate using multi-stage CIC filters.
*/

module psg_fm_uprate(
    input                rst,
    input                clk,
    input signed [15:0] fm_snd,
    input signed [11:0] psg_snd,
    input fm_en,  // enable FM
    input cen_1008,
    input cen_252,
    input cen_63,
    input cen_9,
    output signed [15:0] snd      // Mixed sound at clk sample rate
);

wire signed [15:0] fm2, fm3, fm4;
reg  signed [15:0] mix_sum, mixed, fmin, psgin;
reg                ov;

// 1. Mixing Section (FM + PSG)
always @* begin
    fmin    = fm_en ? fm_snd : 16'd0;
    // PSG 12bit를 FM 16bit와 맞추기 위해 부호 확장 및 쉬프트
    psgin   = {{1{psg_snd[11]}}, psg_snd, 3'b0}; 
    mix_sum = fmin + psgin;
    // Overflow check logic
    ov      = &{fmin[15], psgin[15], ~mix_sum[15]} | &{~fmin[15], ~psgin[15], mix_sum[15]};
end

// Clipping (Saturation)
always @(posedge clk) begin
    mixed <= ov ? {fmin[15], {15{~fmin[15]}}} : mix_sum;
end

// 2. Multi-stage Interpolation (Upscaling) Section
// 1008 --> 252 x4
psg_fm_interpol #(.calcw(17), .inw(16), .rate(4), .m(1), .n(1)) 
u_fm2(
    .clk    ( clk      ),
    .rst    ( rst      ),
    .cen_in ( cen_1008 ),
    .cen_out( cen_252  ),
    .snd_in ( mixed    ),
    .snd_out( fm2      )
);

// 252 --> 63 x4
psg_fm_interpol #(.calcw(19), .inw(16), .rate(4), .m(1), .n(3)) 
u_fm3(
    .clk    ( clk      ),
    .rst    ( rst      ),    
    .cen_in ( cen_252  ),
    .cen_out( cen_63   ),
    .snd_in ( fm2      ),
    .snd_out( fm3      )
);

// 63 --> 9 x7
psg_fm_interpol #(.calcw(21), .inw(16), .rate(7), .m(2), .n(2)) 
u_fm4(
    .clk    ( clk      ),
    .rst    ( rst      ),        
    .cen_in ( cen_63   ),
    .cen_out( cen_9    ),
    .snd_in ( fm3      ),
    .snd_out( fm4      )
);

// 9 --> 1 x9 (Final Output at clk rate)
psg_fm_interpol #(.calcw(21), .inw(16), .rate(9), .m(2), .n(2)) 
u_fm5(
    .clk    ( clk      ),
    .rst    ( rst      ),        
    .cen_in ( cen_9    ),
    .cen_out( 1'b1     ),
    .snd_in ( fm4      ),
    .snd_out( snd      )
);

endmodule