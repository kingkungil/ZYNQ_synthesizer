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