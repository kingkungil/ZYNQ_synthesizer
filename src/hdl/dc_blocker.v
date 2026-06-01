`timescale 1ns / 1ps
// ============================================================================
//  dc_blocker.v  v3  (1사이클 완전 동기 파이프라인)
//
//  v2→v3 수정 사항:
//    1. Stage 1이 en 보호 없이 매 클럭 실행되던 버그 수정
//       → 모든 연산을 단일 en 게이트 안으로 통합 (1사이클 레이턴시)
//    2. dead code 제거 (alpha_y 이중 할당)
//    3. fdn_reverb_top v29의 "Stage 9 발행 → Stage 10 결과" 1사이클
//       가정과 정확히 일치
//
//  구조:
//    y[n] = x[n] - x[n-1] + α * y[n-1]
//
//  레이턴시: 1클럭
//    clk ↑ (en=1): x_prev, y_prev 참조 → 계산 → dout, y_prev, x_prev 갱신
//    clk ↑ (en=0): 내부 상태 일절 변경 없음  ← v2 버그 수정 핵심
//
//  주의:
//    en은 매 샘플마다 1클럭만 high.
//    연속 en 허용 (각 사이클 독립 처리).
//
//  FDN 배치 권장:
//    입력 → DC blocker → FDN   또는
//    FDN  → 출력 → DC blocker
//    (feedback loop 내부 사용 시 위상 여유 감소에 유의)
// ============================================================================
module dc_blocker #(
    parameter integer DATA_W  = 16,
    parameter integer ALPHA_W = 15
)(
    input  wire                      clk,
    input  wire                      rst_n,
    input  wire                      en,
    input  wire [ALPHA_W-1:0]        alpha,      // Q0.15 (예: 0x7F60 ≈ 0.9980)
    input  wire signed [DATA_W-1:0]  din,
    output reg  signed [DATA_W-1:0]  dout
);

// 곱셈 결과 비트폭: s16 × u15 → s31, 부호 확장 포함 +1 = 32비트
localparam MUL_W = DATA_W + ALPHA_W + 1;  // 32

reg signed [DATA_W-1:0]  x_prev;
reg signed [DATA_W-1:0]  y_prev;

// 내부 계산용 (always 블록 내 automatic 변수 대신 모듈 레벨 선언)
reg signed [MUL_W-1:0]  alpha_y_full;
reg signed [DATA_W:0]    diff;
reg signed [MUL_W-1:0]  calc_sum;
reg signed [DATA_W-1:0] result;

// ─── 포화 함수 (MUL_W 비트 → DATA_W 비트) ───────────────────────────────────
function signed [DATA_W-1:0] sat;
    input signed [MUL_W-1:0] v;
    localparam signed [MUL_W-1:0] MAX_V =  (1 <<< (DATA_W-1)) - 1;  //  32767
    localparam signed [MUL_W-1:0] MIN_V = -(1 <<< (DATA_W-1));       // -32768
    begin
        if      (v >  MAX_V) sat = {1'b0, {(DATA_W-1){1'b1}}};
        else if (v <  MIN_V) sat = {1'b1, {(DATA_W-1){1'b0}}};
        else                 sat = v[DATA_W-1:0];
    end
endfunction

// ─── 단일 사이클 처리 ─────────────────────────────────────────────────────────
always @(posedge clk) begin
    if (!rst_n) begin
        x_prev <= {DATA_W{1'b0}};
        y_prev <= {DATA_W{1'b0}};
        dout   <= {DATA_W{1'b0}};
    end else if (en) begin
        // 모든 연산을 1사이클에 완료 - 순서 중요 (현재 y_prev, x_prev 참조 후 갱신)
        alpha_y_full = ($signed(y_prev) * $signed({1'b0, alpha})) >>> ALPHA_W;
        diff         = {din[DATA_W-1],   din}   -
                       {x_prev[DATA_W-1], x_prev};
        calc_sum     = {{(MUL_W - DATA_W - 1){diff[DATA_W]}}, diff} + alpha_y_full;
        result       = sat(calc_sum);

        dout   <= result;
        y_prev <= result;
        x_prev <= din;
    end
    // en=0: x_prev, y_prev, dout 변경 없음 (v2 버그 수정)
end

endmodule