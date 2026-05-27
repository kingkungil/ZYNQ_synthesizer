`timescale 1ns / 1ps
// =============================================================================
//  ym49_mave_axis.v  -  ym49_mave AXI-Stream 래퍼
//
//  ■ 인터페이스
//    - AXI-Stream Slave : s_axis_tvalid / s_axis_tready / s_axis_tdata
//    - AXI-Stream Master: m_axis_tvalid / m_axis_tready / m_axis_tdata
//    - rstn : active-low
//
//  ■ 핸드셰이크 (dcrm1_axis와 동일)
//    out_stall = m_axis_tvalid & ~m_axis_tready
//    s_axis_tready = ~out_stall
//    fire = s_axis_tvalid & s_axis_tready → ym49_mave cen
//
//  ■ 레이턴시
//    ym49_mave 내부:
//      cen 클럭①: diff 갱신 (1FF)
//      cen 클럭②: sum  갱신 (1FF)
//      dout = sum[...] 조합 출력
//    → fire 클럭에 dout는 직전 샘플 기준 이동평균 (허용)
//    → 스트리밍 연속 동작에서 첫 2샘플 이후 정상 수렴
//
//  ■ 하위 모듈: ym49_mave → ym49_dly
// =============================================================================
module ym49_mave_axis #(
    parameter DW    = 16,
    parameter depth = 8
)(
    input  wire              clk,
    input  wire              rstn,

    // AXI-Stream Slave
    input  wire                  s_axis_tvalid,
    input  wire signed [DW-1:0]  s_axis_tdata,
    output wire                  s_axis_tready,

    // AXI-Stream Master
    output reg                   m_axis_tvalid,
    output reg  signed [DW-1:0]  m_axis_tdata,
    input  wire                  m_axis_tready
);

    // -------------------------------------------------------------------------
    //  handshake
    // -------------------------------------------------------------------------
    wire out_stall       = m_axis_tvalid & ~m_axis_tready;
    assign s_axis_tready = ~out_stall;
    wire fire            = s_axis_tvalid & s_axis_tready;

    // -------------------------------------------------------------------------
    //  ym49_mave 인스턴스
    // -------------------------------------------------------------------------
    wire signed [DW-1:0] mave_dout;

    ym49_mave #(
        .depth (depth),
        .DW    (DW)
    ) u_mave (
        .clk  (clk),
        .cen  (fire),
        .rstn (rstn),
        .din  (s_axis_tdata),
        .dout (mave_dout)
    );

    // -------------------------------------------------------------------------
    //  출력 레지스터
    // -------------------------------------------------------------------------
    always @(posedge clk) begin
        if (!rstn) begin
            m_axis_tvalid <= 1'b0;
            m_axis_tdata  <= {DW{1'b0}};
        end else begin
            if (fire) begin
                m_axis_tvalid <= 1'b1;
                m_axis_tdata  <= mave_dout;
            end else if (m_axis_tvalid & m_axis_tready) begin
                m_axis_tvalid <= 1'b0;
            end
        end
    end

endmodule