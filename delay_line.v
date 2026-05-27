// ============================================================================
//  delay_line.v  v2
//
//  목적: fdn_reverb_top의 딜레이 메모리 서브모듈
//
//  [Synth 8-4556 해결]
//    단일 모듈 변수 한계: 1,000,000 bits
//    본 모듈: DEPTH × DATA_W = 16384 × 16 = 262,144 bits < 한계 ✓
//    8인스턴스: 각각 독립 BRAM → cascade 없음 → DRC REQP-1962 없음 ✓
//
//  [BRAM 추론 검증]
//    BRAM36E1 물리: 36Kbit = 32Kbit data + 4Kbit parity
//    16bit 포트 모드: 2K depth per BRAM36
//    16384 depth / 2K = 8 BRAM36 per 인스턴스
//    8 인스턴스 × 8 BRAM36 = 64 BRAM36 (기본값 MAX_DELAY=16384)
//
//    ★ MAX_DELAY별 BRAM 사용량:
//      16384 (14bit): 8 BRAM36 × 8 = 64개  ← 기본값, CT프리셋 지원
//       8192 (13bit): 4 BRAM36 × 8 = 32개  ← 7-series 절약 모드
//       4096 (12bit): 2 BRAM36 × 8 = 16개  ← 소형 디바이스
//    ※ Zynq-7020 (XC7Z020): BRAM36 140개 → 16384모드 여유 있음
//    ※ Zynq-7010 (XC7Z010): BRAM36 60개  → 8192모드 권장
//
//  [타이밍]
//    읽기: 동기 (rd_data = next_clk after rd_addr 제시)
//    쓰기: 동기 (wr_en=1 사이클에 기록)
//    READ_FIRST: 동일 주소 동시 R/W → 이전 값 출력 (BRAM 기본 동작)
//
//  [파라미터]
//    DATA_W : 데이터 비트폭 (기본 16)
//    DEPTH  : 메모리 깊이 (기본 16384, 반드시 2의 거듭제곱)
//    ADDR_W : 주소 비트폭 = ceil(log2(DEPTH)) (기본 14)
// ============================================================================
`timescale 1ns / 1ps

module delay_line #(
    parameter integer DATA_W = 16,
    parameter integer DEPTH  = 16384,
    parameter integer ADDR_W = 14
)(
    input  wire                        clk,
    input  wire                        wr_en,
    input  wire  [ADDR_W-1:0]          wr_addr,
    input  wire  signed [DATA_W-1:0]   wr_data,
    input  wire  [ADDR_W-1:0]          rd_addr,
    output reg   signed [DATA_W-1:0]   rd_data
);

// ── BRAM 추론 어트리뷰트 ─────────────────────────────────────────────────
(* ram_style = "block" *)
reg signed [DATA_W-1:0] mem [0:DEPTH-1];

// ── 초기화 (시뮬레이션용, 합성 무시) ────────────────────────────────────
integer i;
initial begin
    for (i = 0; i < DEPTH; i = i + 1)
        mem[i] = {DATA_W{1'b0}};
end

// ── 쓰기 포트 (동기) ─────────────────────────────────────────────────────
always @(posedge clk) begin
    if (wr_en)
        mem[wr_addr] <= wr_data;
end

// ── 읽기 포트 (동기, READ_FIRST) ────────────────────────────────────────
// BRAM36 True Dual Port / Simple Dual Port 모드:
//   Vivado는 아래 패턴을 자동으로 BRAM으로 추론
//   비동기 읽기 원하면 (* ram_style = "distributed" *) 로 변경
always @(posedge clk) begin
    rd_data <= mem[rd_addr];
end

endmodule