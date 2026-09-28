/*  Based on JT12 - jt12_rst.v
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
    Date: 21-03-2019
*/
`timescale 1ns / 1ps

module ym_rst(
    input   rst,
    input   clk,
    output  reg rst_n
);

reg r;

always @(negedge clk)
    if( rst ) begin
        r     <= 1'b0;
        rst_n <= 1'b0;
    end else begin
        { rst_n, r } <= { r, 1'b1 };
    end

endmodule