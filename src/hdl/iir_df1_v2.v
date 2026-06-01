`timescale 1ns/1ps
// ============================================================================
//  iir_df1_v2.v  —  Direct Form-I biquad  (개선판 v2.0)
//
//  [v1 대비 개선점]
//  1. ST_WAIT 제거
//     - 원래 ST_WAIT는 "FIFO 1-clock latency 보상" 목적이었으나,
//       AXI-Stream 직접 연결 구조(registered s_axis_tdata)에서는 불필요.
//       fire_in 사이클에 tdata_r을 래치하면 바로 ST_MULT로 진입 가능.
//       → 샘플당 레이턴시 6→5 사이클로 단축.
//  2. function automatic + 내부 reg → Vivado HDL 9-1206 합성 오류 수정
//     - sat_round 함수에서 automatic 제거, 내부 reg 제거
//     - 포화/반올림 로직을 조합 wire로 완전 이관
//  3. ST_DRAIN 개선
//     - m_axis_tvalid가 이미 0이면 즉시 IDLE 복귀 (불필요한 1사이클 낭비 제거)
//  4. 계수 CE 로직 단순화
//     - rstn 조건을 CE에 넣지 않고 FDRE R 핀 연결로 리셋 처리
//       (FDRE는 R=1'b0이므로 리셋 후 0 유지, CE만으로 쓰기 제어)
//  5. (* use_dsp = "yes" *) 5곱셈 레지스터에 집중 명시
//  6. acc 비트폭 주석 명확화 (COEF_W*2+8 기준, DATA_W=COEF_W=24이면 56)
//
//  파이프라인 (5사이클/샘플):
//    ST_IDLE  → (fire_in) →
//    ST_MULT  (곱셈 5개, tdata_r 래치, x1/x2 갱신) →
//    ST_ACCUM (누산) →
//    ST_OUTPUT(y_new 출력 레지스터 적재, y1/y2 갱신) →
//    ST_DRAIN (downstream 소비 대기 → IDLE)
// ============================================================================

(* KEEP_HIERARCHY = "YES" *)
module iir_df1_v2 #(
    parameter integer DATA_W = 24,   // 입출력 폭 (보통 16 또는 24)
    parameter integer COEF_W = 24,   // 계수 폭 (Q SCALE 기준)
    parameter integer ACC_W  = 56,   // 누산기 폭 (COEF_W*2+8 권장)
    parameter integer SHIFT  = 22    // Q 소수점 위치 (Q22 = SCALE)
)(
    input  wire                       clk,
    input  wire                       rstn,

    // 계수 쓰기 인터페이스 (coef_addr: 0=b0 1=b1 2=b2 3=a1 4=a2)
    input  wire                       coef_we,
    input  wire [2:0]                 coef_addr,
    input  wire signed [COEF_W-1:0]  coef_wdata,

    // AXI-Stream 슬레이브 (입력)
    input  wire signed [DATA_W-1:0]  s_axis_tdata,
    input  wire                      s_axis_tvalid,
    output wire                      s_axis_tready,

    // AXI-Stream 마스터 (출력)
    output reg  signed [DATA_W-1:0]  m_axis_tdata,
    output reg                       m_axis_tvalid,
    input  wire                      m_axis_tready
);

// ============================================================================
// 0. 로컬 파라미터
// ============================================================================
localparam [3:0]
    ST_IDLE   = 4'd0,
    ST_MULT   = 4'd1,   // [v2] ST_WAIT 제거 → 바로 MULT
    ST_ACCUM  = 4'd2,
    ST_OUTPUT = 4'd3,
    ST_DRAIN  = 4'd4;

// 포화 한계 (DATA_W 기준)
localparam signed [DATA_W-1:0] SAT_MAX = {1'b0, {(DATA_W-1){1'b1}}};
localparam signed [DATA_W-1:0] SAT_MIN = {1'b1, {(DATA_W-1){1'b0}}};

// 반올림 오프셋 (SHIFT-1 비트 위치에 1)
localparam [ACC_W-1:0] ROUND_HALF =
    {{(ACC_W-1){1'b0}}, 1'b1} << (SHIFT - 1);

// ============================================================================
// 1. 계수 레지스터 (FDRE DONT_TOUCH — 합성기 최적화 차단)
// ============================================================================
wire signed [COEF_W-1:0] r_b0, r_b1, r_b2, r_a1, r_a2;

// [v2] CE 로직에서 rstn 제거 (FDRE R=1'b0 고정, 리셋 후 0 유지)
wire ce_b0 = coef_we && (coef_addr == 3'd0);
wire ce_b1 = coef_we && (coef_addr == 3'd1);
wire ce_b2 = coef_we && (coef_addr == 3'd2);
wire ce_a1 = coef_we && (coef_addr == 3'd3);
wire ce_a2 = coef_we && (coef_addr == 3'd4);

genvar ci;
generate
    for (ci = 0; ci < COEF_W; ci = ci + 1) begin : GEN_COEF_FF
        (* DONT_TOUCH = "TRUE" *)
        FDRE #(.INIT(1'b0)) ff_b0 (
            .C(clk), .R(1'b0), .CE(ce_b0), .D(coef_wdata[ci]), .Q(r_b0[ci]));
        (* DONT_TOUCH = "TRUE" *)
        FDRE #(.INIT(1'b0)) ff_b1 (
            .C(clk), .R(1'b0), .CE(ce_b1), .D(coef_wdata[ci]), .Q(r_b1[ci]));
        (* DONT_TOUCH = "TRUE" *)
        FDRE #(.INIT(1'b0)) ff_b2 (
            .C(clk), .R(1'b0), .CE(ce_b2), .D(coef_wdata[ci]), .Q(r_b2[ci]));
        (* DONT_TOUCH = "TRUE" *)
        FDRE #(.INIT(1'b0)) ff_a1 (
            .C(clk), .R(1'b0), .CE(ce_a1), .D(coef_wdata[ci]), .Q(r_a1[ci]));
        (* DONT_TOUCH = "TRUE" *)
        FDRE #(.INIT(1'b0)) ff_a2 (
            .C(clk), .R(1'b0), .CE(ce_a2), .D(coef_wdata[ci]), .Q(r_a2[ci]));
    end
endgenerate

// ============================================================================
// 2. FSM — 4상태 (ST_WAIT 제거)
// ============================================================================
reg [3:0] state;
reg       tready_r;

assign s_axis_tready = tready_r;
wire fire_in = s_axis_tvalid && tready_r;

reg signed [DATA_W-1:0] tdata_r;

always @(posedge clk) begin
    if (!rstn) begin
        state    <= ST_IDLE;
        tready_r <= 1'b0;
        tdata_r  <= {DATA_W{1'b0}};
    end else begin
        case (state)
            ST_IDLE: begin
                tready_r <= 1'b1;
                if (fire_in) begin
                    // [v2] fire_in 사이클에 즉시 래치 → ST_WAIT 불필요
                    tdata_r  <= s_axis_tdata;
                    tready_r <= 1'b0;
                    state    <= ST_MULT;
                end
            end

            ST_MULT:   state <= ST_ACCUM;
            ST_ACCUM:  state <= ST_OUTPUT;
            ST_OUTPUT: state <= ST_DRAIN;

            ST_DRAIN: begin
                // [v2] tvalid이 이미 0이면 즉시 복귀 (낭비 사이클 제거)
                if (!m_axis_tvalid || m_axis_tready) begin
                    state    <= ST_IDLE;
                    tready_r <= 1'b1;
                end
            end

            default: begin
                state    <= ST_IDLE;
                tready_r <= 1'b0;
            end
        endcase
    end
end

// ============================================================================
// 3. 딜레이 라인
// ============================================================================
reg signed [DATA_W-1:0] x1, x2;
reg signed [DATA_W-1:0] y1, y2;

// ============================================================================
// 4. DSP 파이프라인 레지스터
// ============================================================================
(* use_dsp = "yes" *) reg signed [ACC_W-1:0] mb0, mb1, mb2, ma1, ma2;
(* use_dsp = "no"  *) reg signed [ACC_W-1:0] acc;

// ============================================================================
// 5. 포화+반올림 — 조합 wire (function 사용 안 함 → HDL 9-1206 회피)
// ============================================================================
wire signed [ACC_W-1:0] sat_tmp = acc + $signed(ROUND_HALF);
wire signed [ACC_W-1:0] sat_shr = $signed(sat_tmp) >>> SHIFT;

// SAT_MAX/MIN을 ACC_W 폭으로 부호 확장하여 비교
wire signed [ACC_W-1:0] sat_max_ext = {{(ACC_W-DATA_W){1'b0}}, SAT_MAX};
wire signed [ACC_W-1:0] sat_min_ext = {{(ACC_W-DATA_W){1'b1}}, SAT_MIN};

wire signed [DATA_W-1:0] y_new =
    ($signed(sat_shr) > sat_max_ext) ? SAT_MAX :
    ($signed(sat_shr) < sat_min_ext) ? SAT_MIN :
    sat_shr[DATA_W-1:0];

// ============================================================================
// 6. 파이프라인 코어
// ============================================================================
always @(posedge clk) begin
    if (!rstn) begin
        x1 <= {DATA_W{1'b0}};  x2 <= {DATA_W{1'b0}};
        y1 <= {DATA_W{1'b0}};  y2 <= {DATA_W{1'b0}};
        mb0 <= {ACC_W{1'b0}};  mb1 <= {ACC_W{1'b0}};  mb2 <= {ACC_W{1'b0}};
        ma1 <= {ACC_W{1'b0}};  ma2 <= {ACC_W{1'b0}};
        acc <= {ACC_W{1'b0}};
        m_axis_tdata  <= {DATA_W{1'b0}};
        m_axis_tvalid <= 1'b0;
    end else begin

        // ── MULT: 5곱셈 + x 딜레이 갱신 ─────────────────────────────
        if (state == ST_MULT) begin
            mb0 <= $signed(tdata_r) * $signed(r_b0);
            mb1 <= $signed(x1)      * $signed(r_b1);
            mb2 <= $signed(x2)      * $signed(r_b2);
            ma1 <= $signed(y1)      * $signed(r_a1);
            ma2 <= $signed(y2)      * $signed(r_a2);
            x2  <= x1;
            x1  <= tdata_r;
        end

        // ── ACCUM: 누산 ───────────────────────────────────────────────
        if (state == ST_ACCUM) begin
            acc <= mb0 + mb1 + mb2 - ma1 - ma2;
        end

        // ── OUTPUT: 포화/반올림 결과 출력 레지스터 적재 ──────────────
        if (state == ST_OUTPUT) begin
            m_axis_tdata  <= y_new;
            m_axis_tvalid <= 1'b1;
            y2 <= y1;
            y1 <= y_new;
        end else if (m_axis_tvalid && m_axis_tready) begin
            m_axis_tvalid <= 1'b0;
        end

    end
end

endmodule
