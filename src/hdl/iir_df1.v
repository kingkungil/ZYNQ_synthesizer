`timescale 1ns/1ps
// ============================================================================
//  iir_df1.v  (v_fsm_updated + backpressure_fix)
//
//  ── 수정 내역 ────────────────────────────────────────────────────────────
//  [기존 수정 유지]
//  ST_WAIT 상태: FIFO 1-clock latency 보상 (upstream이 registered FF output이면
//  불필요하지만 안전 마진으로 유지. 6-stage 체인에서 추가 6사이클 레이턴시 발생)
//
//  [FIX: 백프레셔 상황 output overwrite 방지]
//    기존: ST_OUTPUT → 즉시 ST_IDLE + tready_r=1
//          → downstream back-pressure 상황에서 이전 출력 미소비 상태에도
//             upstream 샘플을 새로 받아 5사이클 뒤 덮어쓰기 → 샘플 소실
//    수정: ST_OUTPUT 후 ST_DRAIN 상태 추가
//          ST_DRAIN: m_axis_tvalid=0 또는 m_axis_tready=1 확인 후 ST_IDLE 복귀
//          → downstream이 샘플을 소비할 때까지 upstream 차단 보장
//
//  수정 후 FSM:
//    ST_IDLE → (fire_in) → ST_WAIT → ST_MULT → ST_ACCUM → ST_OUTPUT
//    ST_OUTPUT → ST_DRAIN
//    ST_DRAIN → (출력 소비 완료) → ST_IDLE
// ============================================================================

(* KEEP_HIERARCHY = "YES" *)
module iir_df1 #(
    parameter DATA_W = 24,
    parameter COEF_W = 24,
    parameter ACC_W  = 56,
    parameter SHIFT  = 22
)(
    input  wire                      clk,
    input  wire                      rstn,

    // 계수 쓰기 (coef_addr: 0=b0 1=b1 2=b2 3=a1 4=a2)
    input  wire                      coef_we,
    input  wire [2:0]                coef_addr,
    input  wire signed [COEF_W-1:0]  coef_wdata,

    // AXI-Stream
    input  wire signed [DATA_W-1:0]  s_axis_tdata,
    input  wire                      s_axis_tvalid,
    output wire                      s_axis_tready,   // registered FF

    output reg  signed [DATA_W-1:0]  m_axis_tdata,
    output reg                       m_axis_tvalid,
    input  wire                      m_axis_tready
);

// ============================================================================
// 1. 계수 레지스터 - FDRE primitive (R=1'b0 고정, DONT_TOUCH 유지)
// ============================================================================
wire wr_b0 = rstn && coef_we && (coef_addr == 3'd0);
wire wr_b1 = rstn && coef_we && (coef_addr == 3'd1);
wire wr_b2 = rstn && coef_we && (coef_addr == 3'd2);
wire wr_a1 = rstn && coef_we && (coef_addr == 3'd3);
wire wr_a2 = rstn && coef_we && (coef_addr == 3'd4);

wire signed [COEF_W-1:0] r_b0, r_b1, r_b2, r_a1, r_a2;

genvar ci;
generate
    for (ci = 0; ci < COEF_W; ci = ci + 1) begin : GEN_COEF_FF
        (* DONT_TOUCH = "TRUE" *)
        FDRE #(.INIT(1'b0)) ff_b0 (.C(clk),.R(1'b0),.CE(wr_b0),.D(coef_wdata[ci]),.Q(r_b0[ci]));
        (* DONT_TOUCH = "TRUE" *)
        FDRE #(.INIT(1'b0)) ff_b1 (.C(clk),.R(1'b0),.CE(wr_b1),.D(coef_wdata[ci]),.Q(r_b1[ci]));
        (* DONT_TOUCH = "TRUE" *)
        FDRE #(.INIT(1'b0)) ff_b2 (.C(clk),.R(1'b0),.CE(wr_b2),.D(coef_wdata[ci]),.Q(r_b2[ci]));
        (* DONT_TOUCH = "TRUE" *)
        FDRE #(.INIT(1'b0)) ff_a1 (.C(clk),.R(1'b0),.CE(wr_a1),.D(coef_wdata[ci]),.Q(r_a1[ci]));
        (* DONT_TOUCH = "TRUE" *)
        FDRE #(.INIT(1'b0)) ff_a2 (.C(clk),.R(1'b0),.CE(wr_a2),.D(coef_wdata[ci]),.Q(r_a2[ci]));
    end
endgenerate

// ============================================================================
// 2. 6상태 FSM (FIFO latency 대응 + 백프레셔 보호)
// ============================================================================
localparam [2:0] ST_IDLE   = 3'd0;
localparam [2:0] ST_WAIT   = 3'd1;  // FIFO 1-clock latency 보상
localparam [2:0] ST_MULT   = 3'd2;
localparam [2:0] ST_ACCUM  = 3'd3;
localparam [2:0] ST_OUTPUT = 3'd4;
localparam [2:0] ST_DRAIN  = 3'd5;  // [FIX] downstream 소비 완료 대기

reg [2:0] state;
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
                    state    <= ST_WAIT;
                    tready_r <= 1'b0;
                end
            end

            ST_WAIT: begin
                // pop 1사이클 뒤 FIFO dout 안정 → 래치
                tdata_r <= s_axis_tdata;
                state   <= ST_MULT;
            end

            ST_MULT:  state <= ST_ACCUM;
            ST_ACCUM: state <= ST_OUTPUT;

            ST_OUTPUT: begin
                // 출력은 파이프라인 블록에서 처리
                // [FIX] tready 즉시 복구 금지 → ST_DRAIN에서 소비 확인 후 복구
                state <= ST_DRAIN;
            end

            ST_DRAIN: begin
                // [FIX] downstream이 샘플을 소비했거나 이미 비어있으면 IDLE 복귀
                if (!m_axis_tvalid || m_axis_tready) begin
                    state    <= ST_IDLE;
                    tready_r <= 1'b1;
                end
                // 소비 전이면 tready_r=0 유지 → upstream 차단
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
// 4. 파이프라인 레지스터 (DSP)
// ============================================================================
(* use_dsp = "yes" *) reg signed [ACC_W-1:0] mb0, mb1, mb2, ma1, ma2;
(* use_dsp = "no"  *) reg signed [ACC_W-1:0] acc;

// ============================================================================
// 5. 포화 + 반올림
// ============================================================================
localparam signed [DATA_W-1:0] SAT_MAX = {1'b0, {(DATA_W-1){1'b1}}};
localparam signed [DATA_W-1:0] SAT_MIN = {1'b1, {(DATA_W-1){1'b0}}};
localparam [ACC_W-1:0] ROUND_OFFSET =
    ({{(ACC_W-1){1'b0}}, 1'b1}) << (SHIFT - 1);

function signed [DATA_W-1:0] sat_round;
    input signed [ACC_W-1:0] in;
    reg   signed [ACC_W-1:0] tmp;
begin
    tmp = in + $signed(ROUND_OFFSET);
    tmp = tmp >>> SHIFT;
    if      (tmp > $signed({{(ACC_W-DATA_W){1'b0}}, SAT_MAX})) sat_round = SAT_MAX;
    else if (tmp < $signed({{(ACC_W-DATA_W){1'b1}}, SAT_MIN})) sat_round = SAT_MIN;
    else                                                        sat_round = tmp[DATA_W-1:0];
end
endfunction

wire signed [DATA_W-1:0] y_new = sat_round(acc);

// ============================================================================
// 6. 파이프라인 코어
// ============================================================================
always @(posedge clk) begin
    if (!rstn) begin
        x1 <= {DATA_W{1'b0}};  x2 <= {DATA_W{1'b0}};
        y1 <= {DATA_W{1'b0}};  y2 <= {DATA_W{1'b0}};
        mb0 <= {ACC_W{1'b0}};  mb1 <= {ACC_W{1'b0}};
        mb2 <= {ACC_W{1'b0}};  ma1 <= {ACC_W{1'b0}};  ma2 <= {ACC_W{1'b0}};
        acc <= {ACC_W{1'b0}};
        m_axis_tvalid <= 1'b0;
        m_axis_tdata  <= {DATA_W{1'b0}};
    end else begin

        // ── Stage0: 곱셈, 딜레이 갱신 ────────────────────────────────
        if (state == ST_MULT) begin
            mb0 <= $signed(tdata_r) * $signed(r_b0);
            mb1 <= $signed(x1)      * $signed(r_b1);
            mb2 <= $signed(x2)      * $signed(r_b2);
            ma1 <= $signed(y1)      * $signed(r_a1);
            ma2 <= $signed(y2)      * $signed(r_a2);
            x2  <= x1;
            x1  <= tdata_r;
        end

        // ── Stage1: 누산 ──────────────────────────────────────────────
        if (state == ST_ACCUM) begin
            acc <= mb0 + mb1 + mb2 - ma1 - ma2;
        end

        // ── Stage2: 출력 ──────────────────────────────────────────────
        if (state == ST_OUTPUT) begin
            // [FIX] ST_OUTPUT 진입은 한 번만 → overwrite 없음
            // (ST_DRAIN에서 차단하므로 이 블록은 매 샘플당 정확히 1회 실행)
            m_axis_tdata  <= y_new;
            m_axis_tvalid <= 1'b1;
            y2 <= y1;
            y1 <= y_new;
        end else if (m_axis_tvalid && m_axis_tready) begin
            // downstream이 소비하면 tvalid 클리어
            m_axis_tvalid <= 1'b0;
        end

    end
end

endmodule