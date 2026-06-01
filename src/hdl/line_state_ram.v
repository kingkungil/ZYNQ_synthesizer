`timescale 1ns/1ps
// ============================================================================
//  line_state_ram.v  — per-line 필터 상태를 BRAM에 저장 (FF 절약)
//
//  ★ Z-7020 최적화 핵심: 레지스터 배열 → BRAM
//
//  저장 항목 (per line, 6 words × 16bit):
//    addr offset 0: lpf_state
//    addr offset 1: hpf_state
//    addr offset 2: bq_x1
//    addr offset 3: bq_x2
//    addr offset 4: bq_y1
//    addr offset 5: bq_y2
//
//  총 용량: 16 lines × 6 words = 96 words × 16bit = 1536bit
//  BRAM: 1 × BRAM18 (0.5 BRAM36)
//
//  FF 절약: 96 × 16 = 1536 FF → 0 FF
//
//  인터페이스:
//    FSM이 FS_WRITE 진입 시 현재 라인의 6개 상태를 순차 읽기
//    FS_WRITE 완료 시 갱신된 상태를 순차 쓰기
//    → FSM sub_state에 read/write 단계 추가
//
//  주소 계산: addr = line_idx * 6 + field_offset
// ============================================================================
module line_state_ram #(
    parameter DATA_W    = 16,
    parameter NUM_LINES = 16,
    parameter FIELDS    = 6,    // lpf,hpf,bq_x1,x2,y1,y2
    parameter DEPTH     = 96,   // NUM_LINES * FIELDS
    parameter ADDR_W    = 7     // ceil(log2(96)) = 7
)(
    input  wire                   clk,
    // Port A: 읽기/쓰기 (FSM 제어)
    input  wire                   we,
    input  wire [ADDR_W-1:0]      addr,
    input  wire signed [DATA_W-1:0] wdata,
    output reg  signed [DATA_W-1:0] rdata
);

(* ram_style = "block" *)
reg signed [DATA_W-1:0] mem [0:DEPTH-1];

integer i;
initial begin
    for (i = 0; i < DEPTH; i = i + 1)
        mem[i] = {DATA_W{1'b0}};
end

// Simple dual-port: 동기 읽기 + 동기 쓰기
always @(posedge clk) begin
    if (we)
        mem[addr] <= wdata;
    rdata <= mem[addr];
end

endmodule
