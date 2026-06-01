// ============================================================================
//  er_fdn_bridge.v  v4
//
//  v3r2 → v4: SIGNAL ROUTING ONLY (P0 아키텍처 정정)
//
//  [BRG-v4-1]  cross injection / state bias / space LUT 완전 제거
//    - space_cross_shift / space_cross_invert / space_state_shift LUT ❌
//    - cross mixing (L/R 교차 혼합) ❌
//    - state bias (FDN 상태 편향) ❌
//    → 이유: 물리적 근거 없는 space-dependent gain shaping
//
//  [BRG-v4-2]  동작 정의
//    inject_en=1: fdn_out = fdn_in + (er_in * injection_gain)
//    inject_en=0: fdn_out = fdn_in (pass-through)
//    → ER은 초기 반사 신호, FDN에 더해지는 입력일 뿐
//
//  [BRG-v4-3]  파이프라인 유지 (2-stage)
//    Stage 1: er_in * injection_gain (DSP)
//    Stage 2: fdn_in + prod (합산 + 포화)
//
//  [BRG-v4-4]  포트 변경
//    space_id 포트 제거 (더 이상 사용 안 함)
//    나머지 포트 v3r2 완전 호환
//
//  합성 안전: DSP 1개, FF ~100개, LUT 최소화
// ============================================================================

`timescale 1ns / 1ps

module er_fdn_bridge (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        inject_en,
    input  wire signed [15:0] er_l_in,
    input  wire signed [15:0] er_r_in,
    input  wire signed [15:0] fdn_l_in,
    input  wire signed [15:0] fdn_r_in,
    input  wire [31:0] injection_gain,
    // [v4] space_id 제거 - top 연결 시 open으로 남길 것
    output reg  signed [15:0] fdn_l_out,
    output reg  signed [15:0] fdn_r_out
);

localparam integer DATA_W     = 16;
localparam integer ACC_W      = 48;
localparam integer COEFF_FRAC = 24;

// sat16
function signed [DATA_W-1:0] sat16;
    input signed [ACC_W-1:0] v;
    begin
        if      (v >  48'sh000000007FFF) sat16 =  16'h7FFF;
        else if (v < -48'sh000000008000) sat16 = -16'h8000;
        else                             sat16  = v[DATA_W-1:0];
    end
endfunction

// ============================================================================
//  Stage 1 레지스터: ER * injection_gain (DSP)
// ============================================================================
(* use_dsp = "yes" *) reg signed [ACC_W-1:0] prod_l_r;
(* use_dsp = "yes" *) reg signed [ACC_W-1:0] prod_r_r;

reg signed [DATA_W-1:0] fdn_l_d1, fdn_r_d1;
reg                     inject_d1;

// ============================================================================
//  순차 블록
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        prod_l_r  <= {ACC_W{1'b0}};
        prod_r_r  <= {ACC_W{1'b0}};
        fdn_l_d1  <= 16'sd0;
        fdn_r_d1  <= 16'sd0;
        inject_d1 <= 1'b0;
        fdn_l_out <= 16'sd0;
        fdn_r_out <= 16'sd0;
    end else begin

        // ── Stage 1: ER * injection_gain ──────────────────────────────────
        prod_l_r <= ($signed({{(ACC_W-DATA_W){er_l_in[DATA_W-1]}}, er_l_in}) *
                     $signed({1'b0, injection_gain})) >>> COEFF_FRAC;
        prod_r_r <= ($signed({{(ACC_W-DATA_W){er_r_in[DATA_W-1]}}, er_r_in}) *
                     $signed({1'b0, injection_gain})) >>> COEFF_FRAC;

        fdn_l_d1  <= fdn_l_in;
        fdn_r_d1  <= fdn_r_in;
        inject_d1 <= inject_en;

        // ── Stage 2: fdn + er_scaled (signal routing only) ────────────────
        if (inject_d1) begin
            fdn_l_out <= sat16(
                $signed({{(ACC_W-DATA_W){fdn_l_d1[DATA_W-1]}}, fdn_l_d1}) +
                prod_l_r
            );
            fdn_r_out <= sat16(
                $signed({{(ACC_W-DATA_W){fdn_r_d1[DATA_W-1]}}, fdn_r_d1}) +
                prod_r_r
            );
        end else begin
            fdn_l_out <= fdn_l_d1;
            fdn_r_out <= fdn_r_d1;
        end
    end
end

endmodule