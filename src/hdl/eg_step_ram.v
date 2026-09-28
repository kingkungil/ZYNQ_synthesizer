/*  Based on JT12 - eg_step_ram.v
    https://github.com/jotego/jt12
    Modified for ZYNQ_synthesizer (module renamed and adapted for Zynq-7020).

    JT12 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    JT12 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with JT12.  If not, see <http://www.gnu.org/licenses/>.

    Author: Jose Tejada Gomez. Twitter: @topapate
*/
/*
+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Module             | Partition | Slices*       | Slice Reg     | LUTs          | LUTRAM        | BRAM/FIFO | DSP48A1 | BUFG  | BUFIO | BUFR  | DCM   | PLL_ADV   | Full Hierarchical  |
+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| eg_step_ram/       |           | 3/3           | 0/0           | 7/7           | 0/0           | 0/0       | 0/0     | 0/0   | 0/0   | 0/0   | 0/0   | 0/0       | eg_step            |
+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

*/

module eg_step_ram(
	input [2:0] state_V,
	input [5:0] rate_V,
	input [2:0] cnt_V,
	output reg step_V
);

localparam ATTACK=3'd0, DECAY1=3'd1, DECAY2=3'd2, RELEASE=3'd7, HOLD=3'd3;
reg [7:0] step_idx;
reg [7:0] step_ram;

always @(*)
	case( { rate_V[5:4]==2'b11, rate_V[1:0]} )
		3'd0: step_ram = 8'b00000000;
		3'd1: step_ram = 8'b10001000; // 2
		3'd2: step_ram = 8'b10101010; // 4
		3'd3: step_ram = 8'b11101110; // 6
		3'd4: step_ram = 8'b10101010; // 4
		3'd5: step_ram = 8'b11101010; // 5
		3'd6: step_ram = 8'b11101110; // 6
		3'd7: step_ram = 8'b11111110; // 7
	endcase

always @(*) begin : rate_step
	if( rate_V[5:2]==4'hf && state_V == ATTACK)
		step_idx = 8'b11111111; // Maximum attack speed, rates 60&61
	else
	if( rate_V[5:2]==4'd0 && state_V != ATTACK)
		step_idx = 8'b11111110; // limit slowest decay rate_IV
	else
		step_idx = step_ram;
	// a rate_IV of zero keeps the level still
	step_V = rate_V[5:1]==5'd0 ? 1'b0 : step_idx[ cnt_V ];
end

endmodule // eg_step