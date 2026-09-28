/*  Based on JT49 - jt49_dly.v
    https://github.com/jotego/jt49
    Modified for ZYNQ_synthesizer (module renamed and adapted for Zynq-7020).

  This file is part of JT49.

    JT49 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    JT49 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with JT49.  If not, see <http://www.gnu.org/licenses/>.
    
    Author: Jose Tejada Gomez. Twitter: @topapate
    Version: 1.0
    Date: 15-Jan-2019
    
    
*/
`timescale 1ns / 1ps
// =============================================================================
//  ym49_dly.v  -  Delay Stage for Moving Averager  (rstn 버전)
//
//  ■ 원본 대비 변경 사항
//    - rst (active-high) → rstn (active-low)
// =============================================================================
module ym49_dly #(
    parameter DW    = 16,
    parameter depth = 8
)(
    input                clk,
    input                cen,
    input                rstn,          // active-low (변경)
    input      [DW-1:0]  din,
    output reg [DW-1:0]  dout,
    output reg [DW-1:0]  pre_dout
);
    reg [depth-1:0] rdpos, wrpos;
 
    (* ram_style = "distributed" *)
    reg [DW-1:0] ram [0:2**depth-1];
 
    always @(posedge clk) begin
        if (!rstn) begin
            pre_dout <= {DW{1'b0}};
        end else begin
            pre_dout <= ram[rdpos];
            if (cen) ram[wrpos] <= din;
        end
    end
 
    `ifdef SIMULATION
    integer k;
    initial begin
        for (k = 0; k < 2**depth; k = k+1)
            ram[k] = {DW{1'b0}};
    end
    `endif
 
    always @(posedge clk) begin
        if (!rstn) begin
            rdpos <= {{depth-1{1'b0}}, 1'b1};
            wrpos <= {depth{1'b1}};
            dout  <= {DW{1'b0}};
        end else if (cen) begin
            dout  <= pre_dout;
            rdpos <= rdpos + 1'b1;
            wrpos <= wrpos + 1'b1;
        end
    end
endmodule