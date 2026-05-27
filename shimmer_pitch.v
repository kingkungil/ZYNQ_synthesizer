`timescale 1ns / 1ps
// ============================================================================
//  shimmer_pitch.v  v4  (syntax fix)
//
//  수정 이력 (v4 syntax fix):
//
//  [SYNTAX-1] expression part-select 제거
//
//  Vivado HDL 9-1206 에러 원인:
//    rdA_int_r <= (rdphaseA + phase_inc)[PTR_W-1:PHASE_FRAC];
//    → Verilog-2001에서 "expression[range]" 형태 part-select 불허
//    → SystemVerilog에서도 non-blocking 우변 직접 슬라이스 불허
//
//  수정:
//    wire [PTR_W-1:0] rdphaseA_next = rdphaseA + phase_inc;
//    wire [PTR_W-1:0] rdphaseB_next = rdphaseB + phase_inc;
//    → 중간 wire로 덧셈 결과를 받은 후 슬라이스
//
//  [유지]
//    - v4의 3단계 파이프라인 (TIMING-1) 모두 유지
//    - v3의 FIX-1/2/3 모두 유지
//    - 2그레인 Hann OLA, BRAM 버퍼, 순환 포인터
//    - pitch_ratio Q1.15, phase_inc = pitch_ratio << 1
//
//  파라미터:
//    DATA_W    : 데이터 비트폭 (16)
//    BUF_BITS  : 버퍼 깊이 log2 (12)
//    GRAIN_BITS: 그레인 길이 log2 (11)
// ============================================================================
module shimmer_pitch #(
    parameter DATA_W    = 16,
    parameter BUF_BITS  = 12,
    parameter GRAIN_BITS= 11
)(
    input  wire                      clk,
    input  wire                      rst_n,
    input  wire                      tick,
    input  wire signed [DATA_W-1:0]  din,
    input  wire [15:0]               pitch_ratio,
    output reg  signed [DATA_W-1:0]  dout,
    output reg                       dout_valid   // tick 기준 3사이클 후 유효
);

// ============================================================================
//  상수
// ============================================================================
localparam BUF_DEPTH  = 1 << BUF_BITS;
localparam GRAIN_LEN  = 1 << GRAIN_BITS;
localparam PHASE_FRAC = 16;
localparam PTR_W      = BUF_BITS + PHASE_FRAC;  // 28

localparam [PTR_W-1:0] RD_INIT_A =
    ((BUF_DEPTH - GRAIN_LEN)    ) << PHASE_FRAC;
localparam [PTR_W-1:0] RD_INIT_B =
    ((BUF_DEPTH - GRAIN_LEN / 2)) << PHASE_FRAC;

// ============================================================================
//  입력 버퍼 (BRAM)
// ============================================================================
(* ram_style = "block" *)
reg signed [DATA_W-1:0] buf_mem [0:BUF_DEPTH-1];
reg [BUF_BITS-1:0]      wptr;

// ============================================================================
//  Hann 윈도우 LUT (LUTRAM, 조합 읽기)
// ============================================================================
(* ram_style = "distributed" *)
reg [14:0] hann_lut [0:GRAIN_LEN-1];
integer hi;
initial begin
    for (hi = 0; hi < GRAIN_LEN; hi = hi + 1)
        hann_lut[hi] = $rtoi(
            (1.0 - $cos(2.0 * 3.14159265358979 * hi / GRAIN_LEN))
            * 0.5 * 32767.0 + 0.5
        );
end

// ============================================================================
//  읽기 포인터
// ============================================================================
reg [PTR_W-1:0] rdphaseA;
reg [PTR_W-1:0] rdphaseB;

wire [PTR_W-1:0] phase_inc = {{(PTR_W-17){1'b0}}, pitch_ratio[15:0], 1'b0};

// ============================================================================
//  [SYNTAX-1] 덧셈 결과 중간 wire
//  Verilog에서 "expression[range]" part-select 불허
//  → wire로 먼저 받은 뒤 슬라이스
//
//  조합 경로: rdphaseA(FF) → 28bit 가산 → rdphaseA_next(wire)  ~4LL
//             → 슬라이스 → rdA_int_r(FF)        ~2LL  합계 ~6LL ✓
// ============================================================================
wire [PTR_W-1:0] rdphaseA_next = rdphaseA + phase_inc;
wire [PTR_W-1:0] rdphaseB_next = rdphaseB + phase_inc;

// ============================================================================
//  Stage 0 레지스터: 읽기 주소 / grainPos 캡처
// ============================================================================
reg [BUF_BITS-1:0]   rdA_int_r,    rdB_int_r;
reg [GRAIN_BITS-1:0] grainA_pos_r, grainB_pos_r;
reg                  pipe_v0;   // Stage0 valid

// ============================================================================
//  Stage 1 레지스터: BRAM 읽기 결과 + LUTRAM 읽기 결과 캡처
//  BRAM: 동기 읽기 → rdA_int_r(FF) → (BRAM 1사이클) → sampA_r(FF)
//  LUTRAM: 조합 읽기 → grainA_pos_r(FF) → hann_lut(조합) → winA_r(FF)  ~4LL
// ============================================================================
reg signed [DATA_W-1:0] sampA_r, sampB_r;
reg [14:0]              winA_r,  winB_r;
reg                     pipe_v1;

// ============================================================================
//  Stage 2 레지스터: 곱셈 결과 캡처 (DSP 추론)
//  조합 경로: 16×15 곱셈  ~12LL (DSP 사용 시 ~6LL)
// ============================================================================
(* use_dsp = "yes" *)
reg signed [DATA_W+DATA_W-1:0] wA_full_r, wB_full_r;
reg                             pipe_v2;

// ============================================================================
//  Stage 3: 합산 + 포화 → dout
//  조합 경로: 17bit 가산 + 비교/MUX  ~5LL
// ============================================================================
wire signed [DATA_W-1:0] wA_s16 = $signed(wA_full_r[DATA_W+14:15]);
wire signed [DATA_W-1:0] wB_s16 = $signed(wB_full_r[DATA_W+14:15]);

wire signed [DATA_W:0] sum_s17 =
    $signed({wA_s16[DATA_W-1], wA_s16}) +
    $signed({wB_s16[DATA_W-1], wB_s16});

wire signed [DATA_W-1:0] sum_sat =
    (sum_s17 >  17'sd32767) ?  16'sd32767 :
    (sum_s17 < -17'sd32768) ? -16'sd32768 :
     sum_s17[DATA_W-1:0];

// ============================================================================
//  buf_mem 초기화
// ============================================================================
integer mi;
initial begin
    for (mi = 0; mi < BUF_DEPTH; mi = mi + 1)
        buf_mem[mi] = {DATA_W{1'b0}};
end

// ============================================================================
//  순차 로직 (4단계 파이프)
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        wptr         <= {BUF_BITS{1'b0}};
        rdphaseA     <= RD_INIT_A;
        rdphaseB     <= RD_INIT_B;
        // Stage 0
        rdA_int_r    <= {BUF_BITS{1'b0}};
        rdB_int_r    <= {BUF_BITS{1'b0}};
        grainA_pos_r <= {GRAIN_BITS{1'b0}};
        grainB_pos_r <= {GRAIN_BITS{1'b0}};
        pipe_v0      <= 1'b0;
        // Stage 1
        sampA_r      <= {DATA_W{1'b0}};
        sampB_r      <= {DATA_W{1'b0}};
        winA_r       <= 15'd0;
        winB_r       <= 15'd0;
        pipe_v1      <= 1'b0;
        // Stage 2
        wA_full_r    <= {(DATA_W*2){1'b0}};
        wB_full_r    <= {(DATA_W*2){1'b0}};
        pipe_v2      <= 1'b0;
        // Output
        dout         <= {DATA_W{1'b0}};
        dout_valid   <= 1'b0;
    end else begin

        // ── Stage 0: tick에서 포인터 전진 + 주소 캡처 ──────────────────────
        //
        //  [SYNTAX-1 수정]
        //  Before: rdA_int_r <= (rdphaseA + phase_inc)[PTR_W-1:PHASE_FRAC];
        //  After:  rdA_int_r <= rdphaseA_next[PTR_W-1:PHASE_FRAC];
        //          (rdphaseA_next = rdphaseA + phase_inc 는 상단 wire)
        //
        pipe_v0 <= 1'b0;
        if (tick) begin
            // 입력 쓰기
            buf_mem[wptr] <= din;
            wptr          <= wptr + 1'b1;

            // 읽기 포인터 전진
            rdphaseA <= rdphaseA_next;
            rdphaseB <= rdphaseB_next;

            // 다음 사이클 BRAM/LUTRAM 주소를 레지스터에 캡처
            // (wire에서 슬라이스 → 조합 경로 ~6LL)
            rdA_int_r    <= rdphaseA_next[PTR_W-1 : PHASE_FRAC];
            rdB_int_r    <= rdphaseB_next[PTR_W-1 : PHASE_FRAC];
            grainA_pos_r <= rdphaseA_next[PHASE_FRAC + GRAIN_BITS - 1 : PHASE_FRAC];
            grainB_pos_r <= rdphaseB_next[PHASE_FRAC + GRAIN_BITS - 1 : PHASE_FRAC];
            pipe_v0      <= 1'b1;
        end

        // ── Stage 1: BRAM 읽기 결과 + LUTRAM 읽기 결과 캡처 ────────────────
        // BRAM: rdA_int_r(FF) → buf_mem(BRAM, 1사이클 동기) → sampA_r(FF)
        // LUTRAM: grainA_pos_r(FF) → hann_lut(조합) → winA_r(FF)  ~4LL
        sampA_r  <= buf_mem[rdA_int_r];
        sampB_r  <= buf_mem[rdB_int_r];
        winA_r   <= hann_lut[grainA_pos_r];
        winB_r   <= hann_lut[grainB_pos_r];
        pipe_v1  <= pipe_v0;

        // ── Stage 2: 곱셈 (DSP, ~6LL) ───────────────────────────────────────
        wA_full_r <= $signed(sampA_r) * $signed({1'b0, winA_r});
        wB_full_r <= $signed(sampB_r) * $signed({1'b0, winB_r});
        pipe_v2   <= pipe_v1;

        // ── Stage 3: 합산 + 포화 + 출력 (~5LL) ──────────────────────────────
        dout       <= sum_sat;
        dout_valid <= pipe_v2;

    end
end

endmodule