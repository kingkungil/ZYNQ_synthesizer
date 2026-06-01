`timescale 1ns / 1ps
// =============================================================================
//  dc_mave_top.v  -  DC제거 + 이동평균 AXI-Stream Top 모듈
//
//  ■ 체인 위치
//    analog_delay_top (m_axis) ──► [dc_mave_top] ──► axis_to_fifo (s_axis)
//
//  ■ 내부 구조 (하위 모듈 인스턴스)
//
//    s_axis ──► dcrm1_axis ──► ym49_mave_axis ──► m_axis
//                  │                  │
//               u_dcrm            u_mave
//             (dcrm1 내부)    (ym49_mave → ym49_dly 내부)
//
//  ■ back-pressure 전파 흐름
//    axis_to_fifo fifo_full → m_axis_tready=0
//      → ym49_mave_axis out_stall=1 → s_axis_tready=0
//      → dcrm1_axis out_stall=1    → s_axis_tready=0
//      → delay m_axis_tready=0     → delay 출력 정지
//    → 샘플 손실/중복 없음
//
//  ■ rstn: active-low (delay, axis_to_fifo와 통일)
//
//  ■ 파라미터
//    DW    : 데이터 비트폭 (delay DATA_W와 동일 = 16)
//    DC_DW : dcrm1 소수부 비트폭 (기본 16)
//    DEPTH : 이동평균 윈도우 log2 (기본 8 → 256샘플)
//
//  ■ 포함 파일 목록 (Vivado 프로젝트에 모두 추가)
//    dc_mave_top.v  ← 이 파일
//    dcrm1_axis.v
//    dcrm1.v
//    ym49_mave_axis.v
//    ym49_mave.v
//    ym49_dly.v
// =============================================================================
module dc_mave_top #(
    parameter DW    = 16,
    parameter DC_DW = 16,
    parameter DEPTH = 8
)(
    input  wire              clk,
    input  wire              rstn,           // active-low

    // ── AXI-Stream Slave (analog_delay_top m_axis 연결) ──────────────────
    input  wire                  s_axis_tvalid,
    input  wire signed [DW-1:0]  s_axis_tdata,
    output wire                  s_axis_tready,

    // ── AXI-Stream Master (axis_to_fifo s_axis 연결) ─────────────────────
    output wire                  m_axis_tvalid,
    output wire signed [DW-1:0]  m_axis_tdata,
    input  wire                  m_axis_tready
);

    // -------------------------------------------------------------------------
    //  내부 연결선: dcrm1_axis → ym49_mave_axis
    // -------------------------------------------------------------------------
    wire                  dc_m_valid;
    wire signed [DW-1:0]  dc_m_data;
    wire                  dc_m_ready;

    // -------------------------------------------------------------------------
    //  u_dcrm : dcrm1_axis 인스턴스
    //   - s_axis: delay m_axis 연결
    //   - m_axis: ym49_mave_axis s_axis 연결
    // -------------------------------------------------------------------------
    dcrm1_axis #(
        .sw (DW),
        .DW (DC_DW)
    ) u_dcrm (
        .clk            (clk),
        .rstn           (rstn),
        // slave
        .s_axis_tvalid  (s_axis_tvalid),
        .s_axis_tdata   (s_axis_tdata),
        .s_axis_tready  (s_axis_tready),
        // master
        .m_axis_tvalid  (dc_m_valid),
        .m_axis_tdata   (dc_m_data),
        .m_axis_tready  (dc_m_ready)
    );

    // -------------------------------------------------------------------------
    //  u_mave : ym49_mave_axis 인스턴스
    //   - s_axis: dcrm1_axis m_axis 연결
    //   - m_axis: axis_to_fifo s_axis 연결 (top m_axis로 노출)
    // -------------------------------------------------------------------------
    ym49_mave_axis #(
        .DW    (DW),
        .depth (DEPTH)
    ) u_mave (
        .clk            (clk),
        .rstn           (rstn),
        // slave
        .s_axis_tvalid  (dc_m_valid),
        .s_axis_tdata   (dc_m_data),
        .s_axis_tready  (dc_m_ready),
        // master (top m_axis 직결)
        .m_axis_tvalid  (m_axis_tvalid),
        .m_axis_tdata   (m_axis_tdata),
        .m_axis_tready  (m_axis_tready)
    );

endmodule