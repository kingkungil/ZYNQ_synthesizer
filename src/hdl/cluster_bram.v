// ============================================================================
//  cluster_bram.v  v2
//
//  cluster_engine 전용 2개 BRAM wrapper
//
//  BRAM A: event_fifo_bram  (64-bit × 2048)
//    - reflection event FIFO 버퍼
//    - single-port 순차 read/write
//
//  BRAM B: cluster_acc_bram (64-bit × 256)
//    - cluster accumulator (cluster_id별 누적 상태)
//    - dual-port: FSM이 동시에 read/write 가능
//
//  cluster_acc entry 형식 (64-bit) v5 기준:
//    [63:48] peak_gain   Q15  (cluster 내 최대 gain)
//    [47:32] rep_time    Q8   (첫 이벤트 시간, 불변)
//    [31:24] count       8-bit
//    [23:16] direction   8-bit (energy-weighted)
//    [15:8]  spread      8-bit (angle_max - angle_min)
//    [7:0]   wall_type   8-bit (지배적 wall_id)
//
//  합성 안전 규칙:
//    [SYN-1] (* ram_style = "block" *) → BRAM36/18 자동 추론
//    [SYN-2] 단일 always 블록에서 read/write 분리
//    [SYN-3] initial begin 완전 명시 (Vivado 8-6858 방지)
// ============================================================================

`timescale 1ns / 1ps

module cluster_bram (
    input  wire        clk,

    // ── BRAM A: event FIFO ────────────────────────────────────────────────
    input  wire [10:0] ev_wr_addr,
    input  wire [63:0] ev_wr_data,
    input  wire        ev_wr_en,
    input  wire [10:0] ev_rd_addr,
    output reg  [63:0] ev_rd_data,

    // ── BRAM B: cluster accumulator (dual-port) ──────────────────────────
    // Port A: read
    input  wire [7:0]  cl_ra_addr,
    output reg  [63:0] cl_ra_data,
    // Port B: write
    input  wire [7:0]  cl_wb_addr,
    input  wire [63:0] cl_wb_data,
    input  wire        cl_wb_en
);

// ============================================================================
//  [SYN-1] BRAM A: event FIFO (64-bit × 2048)
// ============================================================================
(* ram_style = "block" *) reg [63:0] event_fifo [0:2047];

integer ef_i;
initial begin
    for (ef_i = 0; ef_i < 2048; ef_i = ef_i + 1)
        event_fifo[ef_i] = 64'd0;
end

always @(posedge clk) begin
    // [SYN-2] read-first 모드 (write와 동일 클럭에서 read 허용)
    ev_rd_data <= event_fifo[ev_rd_addr];
    if (ev_wr_en)
        event_fifo[ev_wr_addr] <= ev_wr_data;
end

// ============================================================================
//  [SYN-1] BRAM B: cluster accumulator (64-bit × 256, true dual-port)
// ============================================================================
(* ram_style = "block" *) reg [63:0] cluster_acc [0:255];

integer ca_i;
initial begin
    for (ca_i = 0; ca_i < 256; ca_i = ca_i + 1)
        cluster_acc[ca_i] = 64'd0;
end

// Port A: read (1-cycle latency)
always @(posedge clk) begin
    cl_ra_data <= cluster_acc[cl_ra_addr];
end

// Port B: write
always @(posedge clk) begin
    if (cl_wb_en)
        cluster_acc[cl_wb_addr] <= cl_wb_data;
end

endmodule