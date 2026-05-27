`timescale 1ns / 1ps

/*
    dac2.v  [v10]

    2nd-order sigma-delta modulator (Schreier topology, signed).

    ─────────────────────────────────────────────────────────────────
    [v9 이하 근본 버그 2종]
    ─────────────────────────────────────────────────────────────────
    BUG-1 [치명] 스케일 불일치:
      undin 범위 [0, 65535], feedback HALF=2^(int_w-1)=2^20=1,048,576
      → HALF/undin_max ≈ 16×  → SDM이 신호를 무시하고 자체 오실레이션
      → 오실로스코프: 입력 무관 duty 0.47~0.51 고착 / VPP≈47mV 고정

    BUG-2 [치명] feedback 비대칭:
      dout=1 → error=y-HALF,  dout=0 → error=y-0
      → 평균 feedback이 signal과 균형을 못 맞춤 → SDM DC 편향

    BUG-3 [경미] y 비트폭 부족 (int_w=21):
      {error_1,1'b0} = 22bit RHS → y(21bit) 저장 시 MSB 손실
      → dout 판정 비트 오류

    ─────────────────────────────────────────────────────────────────
    [v10 수정] Signed SDM (올바른 토폴로지)
    ─────────────────────────────────────────────────────────────────
      din을 직접 signed 사용 (undin offset-binary 변환 제거)
      y     = din + 2*error_1 - error_2   (signed, int_w비트 확장)
      dout  = (y >= 0) ? 1 : 0
      feedback = dout ? +FULL_SCALE : -FULL_SCALE  ← 완전 대칭

      FULL_SCALE = 2^(width-1) = 32768  ← din 최대값과 동일 스케일
      duty = (din + FULL_SCALE) / (2 × FULL_SCALE)
        din=0      → 0.5000
        din=+16383 → 0.7500
        din=-16384 → 0.2500
        din=+32767 → 0.9999  ← dac_bridge GUARD_MAX=32767과 정확히 매핑

      int_w = width+5 = 21bit:
        2차 SDM에서 error ≈ ±FULL_SCALE → 2*e1 ≈ ±2^width
        guard 4비트 추가 → 발산 없음 (시뮬 검증)

      FULL_SE: int_w 비트 localparam으로 선언
        → FULL[width-1:0] part-select (Verilog-2001 비상수 경계 경고) 제거

    [v9 계승] dout FF 등록 (루프 피드백은 dout_comb 유지)
*/

module dac2 #(parameter width=16)
(
    input   clk,
    input   rst,
    input   signed [width-1:0] din,
    output  reg dout
);

localparam int_w = width + 5;   // 21bit: 신호 16bit + guard 5bit

// FULL_SCALE: int_w 비트로 직접 선언 (part-select 불필요)
// = 2^(width-1) = 32768, unsigned 21bit = 21'h08000
localparam [int_w-1:0] FULL_SE = (1 << (width-1));

reg signed [int_w-1:0] y, error, error_1, error_2;

// dout_comb: 루프 내부 조합 결정 (FF 등록 전, 피드백에 사용)
wire dout_comb;

// y = sign_ext(din) + 2*error_1 - error_2  (모두 signed int_w 비트)
always @(*) begin
    y = $signed({{(int_w-width){din[width-1]}}, din})
      + ($signed(error_1) <<< 1)
      - $signed(error_2);
end

// dout_comb: y[MSB]=0(양수) → 1, y[MSB]=1(음수) → 0
assign dout_comb = ~y[int_w-1];

// 대칭 feedback: ±FULL_SCALE
always @(*) begin
    if (dout_comb)
        error = $signed(y) - $signed(FULL_SE);   // y - 32768
    else
        error = $signed(y) + $signed(FULL_SE);   // y + 32768
end

always @(posedge clk)
    if (rst) begin
        error_1 <= {int_w{1'b0}};
        error_2 <= {int_w{1'b0}};
        dout    <= 1'b0;
    end else begin
        error_1 <= error;
        error_2 <= error_1;
        // [v9 계승] dout FF 등록: 출력 글리치/지터 차단
        dout    <= dout_comb;
    end

endmodule