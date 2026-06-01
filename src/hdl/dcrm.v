`timescale 1ns / 1ps
// =============================================================================
//  dcrm1.v  -  DC Removal Filter  (rstn 버전)
//
//  ■ 원본 대비 변경 사항
//    - rst (active-high) → rstn (active-low) 으로 변경
//      axis 래퍼(dcrm1_axis)가 rstn→rst 변환을 수행하지 않아도 되도록
//      내부까지 rstn 통일 (dcrm1_axis에서 rst_int = ~rstn 불필요해짐)
//      단, dcrm1_axis는 호환성을 위해 rst_int 유지 선택도 가능
//
//  ■ 버그 수정 (원본 유지)
//    - $signed(integ) + $signed(...) : signed 덧셈 보장
//
//  ■ 파라미터
//    sw : 신호 비트폭
//    DW : 소수부 비트폭 (누산기 정밀도)
// =============================================================================
module dcrm1 #(
    parameter sw = 16,
    parameter DW = 16
)(
    input                    clk,
    input                    cen,
    input                    rstn,           // active-low (변경)
    input         [sw-1:0]   din,
    output signed [sw-1:0]   dout
);
    // 고정소수점 누산기: Q(sw).(DW), 부호비트 포함 총 sw+DW+1 비트
    reg signed [sw+DW:0] integ;

    // 정수부: 상위 sw+1 비트
    wire signed [sw:0] q = integ[sw+DW:DW];

    // 출력: din(unsigned) → 부호확장 후 q 감산
    wire signed [sw:0] pre_dout = $signed({1'b0, din}) - q;
    assign dout = pre_dout[sw-1:0];

    always @(posedge clk) begin
        if (!rstn) begin
            integ <= {sw+DW+1{1'b0}};
        end else if (cen) begin
            integ <= $signed(integ) + $signed({{DW{pre_dout[sw]}}, pre_dout});
        end
    end
endmodule