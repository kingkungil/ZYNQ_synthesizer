/*  Based on JT49 - jt49_dcrm2.v
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

// DC removal filter
// input is unsigned
// output is signed

module dcrm2 #(parameter sw=10) (
    input                clk,
    input                cen,
    input                rst,
    input         [sw-1:0]  din,
    output signed [sw-1:0]  dout
);

localparam DW=10; // width of the decimal portion

reg  signed [sw+DW:0] integ, exact, error;
//reg  signed [2*(9+DW)-1:0] mult;
// wire signed [sw+DW:0] plus1 = { {sw+DW{1'b0}},1'b1};
reg  signed [sw:0] pre_dout;
// reg signed [sw+DW:0] dout_ext;
reg signed [sw:0] q;

always @(*) begin
    exact = integ+error;
    q = exact[sw+DW:DW];
    pre_dout  = { 1'b0, din } - q;
    //dout_ext = { pre_dout, {DW{1'b0}} };    
    //mult  = dout_ext;
end

assign dout = pre_dout[sw-1:0];

always @(posedge clk)
    if( rst ) begin
        integ <= {sw+DW+1{1'b0}};
        error <= {sw+DW+1{1'b0}};
    end else if( cen ) begin
        /* verilator lint_off WIDTH */
        integ <= integ + pre_dout; //mult[sw+DW*2:DW];
        /* verilator lint_on WIDTH */
        error <= exact-{q, {DW{1'b0}}};
    end

endmodule