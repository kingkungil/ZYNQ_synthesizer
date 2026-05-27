`timescale 1ns / 1ps
// =============================================================================
//  ym49_mave.v  -  Moving Averager  (rstn 버전)
//
//  ■ 원본 대비 변경 사항
//    - rst (active-high) → rstn (active-low)
//
//  ■ 버그 수정 (원본 유지)
//    - diff <= $signed(din) - $signed(dly0)  (0패딩 오류 수정)
//    - sum 누산 $signed 명시
// =============================================================================
module ym49_mave #(
    parameter depth = 8,
    parameter DW    = 16
)(
    input                   clk,
    input                   cen,
    input                   rstn,           // active-low (변경)
    input  signed [DW-1:0]  din,
    output signed [DW-1:0]  dout
);
    wire signed [DW-1:0] dly0;
    wire signed [DW-1:0] pre_dly0;
 
    ym49_dly #(
        .DW    (DW),
        .depth (depth)
    ) u_dly0 (
        .clk      (clk),
        .cen      (cen),
        .rstn     (rstn),           // rstn 전달
        .din      (din),
        .dout     (dly0),
        .pre_dout (pre_dly0)
    );
 
    reg signed [DW:0]       diff;
    reg signed [DW+depth:0] sum;
 
    always @(posedge clk) begin
        if (!rstn) begin
            diff <= {DW+1{1'b0}};
            sum  <= {DW+depth+1{1'b0}};
        end else if (cen) begin
            diff <= $signed(din) - $signed(dly0);
            sum  <= $signed({{depth{diff[DW]}}, diff}) + $signed(sum);
        end
    end
 
    assign dout = sum[DW+depth-1:depth];
endmodule