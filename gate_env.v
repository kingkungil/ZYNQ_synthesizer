// ============================================================================
//  gate_env.v  v3
//  게이트 엔벨로프 생성기 - 4단 파이프라인 (타이밍 완전 해결)
//
//  v2 잔존 타이밍 문제 (Vivado WNS -16.66ns, 90 Logic Levels):
//    ST2 조합 always(*) 내부:
//      if (timer_r >= (attack_samp - 1'b1))
//      → attack_samp 입력 → 16bit 감산 → 16bit 비교 → FSM MUX → timer/CE
//      이 경로가 carry chain + LUT cascade = 90 LL의 직접 원인
//
//  v3 핵심 변경:
//  ┌─────────────────────────────────────────────────────────────────────────┐
//  │  ST0 (매 clk)  │  ST1 (T+1)       │  ST2 (T+2)      │  ST3 (T+3/T+4)  │
//  │  threshold     │  |din|, thresh   │  FSM 전이,       │  gain 증감,     │
//  │  사전 등록     │  비교, above_r   │  timer 갱신      │  출력 래치      │
//  │  attack_th_r   │  lat 래치        │  state_r,timer_r │  gain (T+4)     │
//  │  = samp - 1    │  (threshold FF)  │  step_r 동시계산 │                 │
//  └─────────────────────────────────────────────────────────────────────────┘
//
//  [FIX-1] ST0: threshold 사전 등록
//    attack_th_r  <= attack_samp  - 1
//    hold_th_r    <= hold_samp    - 1
//    release_th_r <= release_samp - 1
//    → ST2 조합 블록에서 감산 완전 제거 → 단순 16bit 비교만 남음
//    → 목표 Logic Levels ≤ 12 달성
//
//  [FIX-2] ST1: threshold를 ST1에서 추가 래치 (above_r과 동기)
//    attack_th_lat_r  / hold_th_lat_r / release_th_lat_r
//    → ST2 조합 블록에서 참조하는 threshold가 above_r과 같은 클럭 기준
//    → FSM 상태와 타이머 비교값이 항상 동일 샘플에 대응
//
//  [FIX-3] ST2: step 계산을 상태 전이와 동일 클럭에서 수행
//    v2: step을 ST2b(별도 always)에서 계산했으나, state_r과 clock 동기가
//        불명확해 Vivado가 step → gain 경로를 최적화 못하는 경우 발생
//    v3: step_r을 ST2 always 내에서 state 전이와 동시에 등록
//        → ST3 진입 시 state_st3와 step_r이 항상 쌍으로 유효
//
//  [FIX-4] 엣지 케이스 처리
//    attack_samp == 0: clamp → 1 (0-1 언더플로우 방지)
//    hold/release 동일 처리
//
//  [FIX-5] ST3 gain 경로 단순화
//    v2: gain_int_r → gain (출력 1FF) 구조 유지
//    v3: 동일 구조, 단 step_r이 이미 ST2에서 등록되었으므로
//        ST3 조합 경로 = 덧셈/뺄셈 + 포화 클리핑만 → ≤8 LL
//
//  인터페이스: v1/v2 완전 호환 (포트/파라미터 동일)
//  총 레이턴시: tick 후 +4 클럭 (48kHz 기준 83µs, 청감 무관)
//  Logic Level 목표:
//    ST0 조합: ≤ 2  (감산)
//    ST1 조합: ≤ 4  (비교)
//    ST2 조합: ≤ 12 (비교 + FSM MUX, 감산 없음)
//    ST3 조합: ≤ 8  (덧셈/뺄셈 + 포화)
// ============================================================================
`timescale 1ns / 1ps

module gate_env #(
    parameter integer DATA_W = 16
)(
    input  wire                       clk,
    input  wire                       rst_n,
    input  wire                       tick,
    input  wire signed [DATA_W-1:0]   din,
    input  wire        [DATA_W-1:0]   thresh,
    input  wire        [DATA_W-1:0]   attack_samp,
    input  wire        [DATA_W-1:0]   hold_samp,
    input  wire        [DATA_W-1:0]   release_samp,
    output reg         [DATA_W-1:0]   gain
);

// ============================================================================
//  파라미터
// ============================================================================
localparam [DATA_W-1:0] GAIN_MAX  = {1'b0, {(DATA_W-1){1'b1}}};  // 0x7FFF
localparam [DATA_W-1:0] GAIN_ZERO = {DATA_W{1'b0}};

localparam [1:0]
    ST_CLOSED  = 2'd0,
    ST_ATTACK  = 2'd1,
    ST_HOLD    = 2'd2,
    ST_RELEASE = 2'd3;

// ============================================================================
//  ST0: threshold 사전 등록 (매 클럭, tick 무관)
//  [FIX-1] 조합 경로에서 감산 제거의 핵심
//
//  입력 파라미터 변경이 드물고 항상 AXI 레지스터에서 오므로
//  매 클럭 갱신해도 글리치 위험 없음.
//  (변경 시 1클럭 후 반영 → 48kHz 샘플링에서 62.5µs, 무시 가능)
//
//  samp == 0 엣지 케이스: 0 - 1 = 0xFFFF (언더플로우) 방지를 위해
//  samp가 0이면 threshold를 0으로 클램프 → timer >= 0 은 항상 참
//  → 즉시 다음 상태로 전이 (실질적 disable)
// ============================================================================
reg [DATA_W-1:0] attack_th_r;
reg [DATA_W-1:0] hold_th_r;
reg [DATA_W-1:0] release_th_r;

always @(posedge clk) begin
    if (!rst_n) begin
        attack_th_r  <= {DATA_W{1'b0}};
        hold_th_r    <= {DATA_W{1'b0}};
        release_th_r <= {DATA_W{1'b0}};
    end else begin
        // [FIX-4] 0-clamp: samp==0이면 th=0, 아니면 samp-1
        attack_th_r  <= (|attack_samp)  ? attack_samp  - 1'b1 : {DATA_W{1'b0}};
        hold_th_r    <= (|hold_samp)    ? hold_samp    - 1'b1 : {DATA_W{1'b0}};
        release_th_r <= (|release_samp) ? release_samp - 1'b1 : {DATA_W{1'b0}};
    end
end

// ============================================================================
//  ST1: |din| 계산 + thresh 비교 + threshold 래치 (tick 기준 T+1)
//  [FIX-2] threshold를 above_r과 동일 클럭에서 래치
//
//  above_r: |din| >= thresh (이 사이클의 판정)
//  *_th_lat_r: ST0 출력을 추가 FF로 보유 → ST2 조합 경로에서 참조
//              above_r과 동일 샘플 기준으로 정렬됨
// ============================================================================
reg tick_st1;
reg above_r;
reg [DATA_W-1:0] attack_th_lat_r;
reg [DATA_W-1:0] hold_th_lat_r;
reg [DATA_W-1:0] release_th_lat_r;

wire [DATA_W-1:0] abs_din =
    din[DATA_W-1] ? ($unsigned(-din)) : $unsigned(din);

always @(posedge clk) begin
    if (!rst_n) begin
        tick_st1         <= 1'b0;
        above_r          <= 1'b0;
        attack_th_lat_r  <= {DATA_W{1'b0}};
        hold_th_lat_r    <= {DATA_W{1'b0}};
        release_th_lat_r <= {DATA_W{1'b0}};
    end else begin
        tick_st1         <= tick;
        above_r          <= (abs_din >= thresh);
        // [FIX-2] ST0 출력을 ST1과 동일 클럭에서 래치
        // → ST2에서 above_r과 *_th_lat_r이 항상 같은 샘플에 대응
        attack_th_lat_r  <= attack_th_r;
        hold_th_lat_r    <= hold_th_r;
        release_th_lat_r <= release_th_r;
    end
end

// ============================================================================
//  ST2: FSM 전이 + 타이머 갱신 (tick 기준 T+2)
//  [FIX-1 효과] 조합 블록에서 단순 비교만 수행:
//    timer_r >= attack_th_lat_r   ← FF 출력끼리 비교
//    (v2에서는 timer_r >= (attack_samp - 1) → 조합 감산 포함이었음)
//
//  조합 경로: above_r(FF) → case MUX → ns/nt
//             timer_r(FF) + *_th_lat_r(FF) → 16bit 비교 → case MUX
//  Logic Levels 목표: ≤ 12 (16bit 비교 ~5LL + FSM MUX ~5LL = ~10LL)
//
//  [FIX-3] step_r을 state 전이와 동일 always 블록에서 등록
//    → ST3에서 state_st3와 step_r이 항상 1:1로 유효
//    step 계산: GAIN_MAX >> enc_msb(samp) | 1 (power-of-2 근사)
//    enc_msb LUT: casez 기반 우선순위 인코더 (~2 LL)
//    barrel shift: Vivado LUT cascade (~2 LL)
//    total step path: ~4 LL → 충분히 마진 내
// ============================================================================

// MSB 우선순위 인코더 (16→4bit)
function [3:0] enc_msb;
    input [DATA_W-1:0] v;
    begin
        casez (v)
            16'b1???????????????: enc_msb = 4'd15;
            16'b01??????????????: enc_msb = 4'd14;
            16'b001?????????????: enc_msb = 4'd13;
            16'b0001????????????: enc_msb = 4'd12;
            16'b00001???????????: enc_msb = 4'd11;
            16'b000001??????????: enc_msb = 4'd10;
            16'b0000001?????????: enc_msb = 4'd9;
            16'b00000001????????: enc_msb = 4'd8;
            16'b000000001???????: enc_msb = 4'd7;
            16'b0000000001??????: enc_msb = 4'd6;
            16'b00000000001?????: enc_msb = 4'd5;
            16'b000000000001????: enc_msb = 4'd4;
            16'b0000000000001???: enc_msb = 4'd3;
            16'b00000000000001??: enc_msb = 4'd2;
            16'b000000000000001?: enc_msb = 4'd1;
            default:              enc_msb = 4'd0;
        endcase
    end
endfunction

reg tick_st2;
reg [1:0]        state_r;
reg [DATA_W-1:0] timer_r;
reg [DATA_W-1:0] attack_step_r;
reg [DATA_W-1:0] release_step_r;

// 조합 next-state / next-timer
// [FIX-1] timer_r >= *_th_lat_r: 모두 FF 출력끼리 비교 → 조합 감산 없음
reg [1:0]        ns;
reg [DATA_W-1:0] nt;

always @(*) begin
    ns = state_r;
    nt = timer_r;
    case (state_r)
        ST_CLOSED: begin
            if (above_r) begin
                ns = ST_ATTACK;
                nt = {DATA_W{1'b0}};
            end
        end
        ST_ATTACK: begin
            if (timer_r >= attack_th_lat_r) begin
                // attack 완료 → HOLD
                ns = ST_HOLD;
                nt = {DATA_W{1'b0}};
            end else if (!above_r && (timer_r == {DATA_W{1'b0}})) begin
                // 즉시 소멸
                ns = ST_CLOSED;
                nt = {DATA_W{1'b0}};
            end else begin
                nt = timer_r + 1'b1;
            end
        end
        ST_HOLD: begin
            if (above_r) begin
                // 재트리거: 타이머만 리셋
                nt = {DATA_W{1'b0}};
            end else if (timer_r >= hold_th_lat_r) begin
                ns = ST_RELEASE;
                nt = {DATA_W{1'b0}};
            end else begin
                nt = timer_r + 1'b1;
            end
        end
        ST_RELEASE: begin
            if (above_r) begin
                // 재트리거 → ATTACK
                ns = ST_ATTACK;
                nt = {DATA_W{1'b0}};
            end else if (timer_r >= release_th_lat_r) begin
                ns = ST_CLOSED;
                nt = {DATA_W{1'b0}};
            end else begin
                nt = timer_r + 1'b1;
            end
        end
        default: begin ns = ST_CLOSED; nt = {DATA_W{1'b0}}; end
    endcase
end

always @(posedge clk) begin
    if (!rst_n) begin
        tick_st2       <= 1'b0;
        state_r        <= ST_CLOSED;
        timer_r        <= {DATA_W{1'b0}};
        attack_step_r  <= 16'd1;
        release_step_r <= 16'd1;
    end else begin
        tick_st2 <= tick_st1;
        if (tick_st1) begin
            state_r <= ns;
            timer_r <= nt;
            // [FIX-3] step 계산을 state 전이와 동일 클럭에서 등록
            // → ST3에서 state_st3와 step_r이 항상 쌍으로 유효
            // attack_samp/release_samp는 AXI 레지스터에서 오므로 안정
            // enc_msb + barrel shift: ~4 LL, 타이밍 마진 충분
            attack_step_r  <= (GAIN_MAX >> enc_msb(attack_samp))  | 16'd1;
            release_step_r <= (GAIN_MAX >> enc_msb(release_samp)) | 16'd1;
        end
    end
end

// ============================================================================
//  ST3: gain 증감 (tick 기준 T+3, gain 출력 T+4)
//  [FIX-3 효과] state_st3 = state_r (1FF 지연), step_r도 동일 클럭 기준
//               → 조합 경로: state_st3(FF) → case MUX → gain_int_r
//                            step_r(FF) → 덧셈/뺄셈 → gain_int_r
//
//  조합 경로: ~8 LL
//    - case MUX: ~4 LL
//    - 16bit 덧셈 + 포화 clamp: ~4 LL
//
//  출력 FF 구조 (v2와 동일):
//    gain_int_r: 내부 누산 레지스터 (ST3 registered)
//    gain:       출력 레지스터 (gain_int_r → 1FF)
//    총 2FF → gain 출력 경로에서 타이밍 여유 추가 확보
// ============================================================================
reg [1:0]        state_st3;
reg              tick_st3;
reg [DATA_W-1:0] gain_int_r;

always @(posedge clk) begin
    if (!rst_n) begin
        tick_st3   <= 1'b0;
        state_st3  <= ST_CLOSED;
    end else begin
        tick_st3  <= tick_st2;
        state_st3 <= state_r;
    end
end

always @(posedge clk) begin
    if (!rst_n) begin
        gain_int_r <= GAIN_ZERO;
        gain       <= GAIN_ZERO;
    end else if (tick_st3) begin
        case (state_st3)
            ST_CLOSED: begin
                gain_int_r <= GAIN_ZERO;
            end
            ST_ATTACK: begin
                // 포화 덧셈: gain_int_r + step ≥ MAX → 클램프
                if (gain_int_r >= (GAIN_MAX - attack_step_r))
                    gain_int_r <= GAIN_MAX;
                else
                    gain_int_r <= gain_int_r + attack_step_r;
            end
            ST_HOLD: begin
                gain_int_r <= GAIN_MAX;
            end
            ST_RELEASE: begin
                // 포화 뺄셈: gain_int_r - step ≤ 0 → 클램프
                if (gain_int_r <= release_step_r)
                    gain_int_r <= GAIN_ZERO;
                else
                    gain_int_r <= gain_int_r - release_step_r;
            end
            default: gain_int_r <= GAIN_ZERO;
        endcase
        // 출력 1FF: gain_int_r → gain
        // ST3의 gain_int_r 계산 결과를 추가로 래치
        // → fdn_reverb_top FS_MIX의 곱셈 경로와 완전 분리
        gain <= gain_int_r;
    end
end

endmodule