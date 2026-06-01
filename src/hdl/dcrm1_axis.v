`timescale 1ns / 1ps
// =============================================================================
//  dcrm1_axis.v  -  dcrm1 AXI-Stream 래퍼
//
//  ■ 인터페이스
//    - AXI-Stream Slave : s_axis_tvalid / s_axis_tready / s_axis_tdata
//    - AXI-Stream Master: m_axis_tvalid / m_axis_tready / m_axis_tdata
//    - rstn : active-low (delay, axis_to_fifo와 통일)
//
//  ■ 핸드셰이크
//    out_stall = m_axis_tvalid & ~m_axis_tready
//    s_axis_tready = ~out_stall   → back-pressure 전파
//    fire = s_axis_tvalid & s_axis_tready → dcrm1 cen
//
//  ■ 레이턴시
//    dcrm1 dout는 조합 출력 (integ는 이전 샘플 값 유지)
//    fire 클럭에 dout 유효 → 같은 클럭 출력 레지스터 래치
//    → m_axis_tvalid는 fire 다음 클럭에 1
//
//  ■ 하위 모듈: dcrm1
// =============================================================================
module dcrm1_axis #(
    parameter sw = 16,
    parameter DW = 16
)(
    input  wire              clk,
    input  wire              rstn,

    // AXI-Stream Slave
    input  wire                  s_axis_tvalid,
    input  wire signed [sw-1:0]  s_axis_tdata,
    output wire                  s_axis_tready,

    // AXI-Stream Master
    output reg                   m_axis_tvalid,
    output reg  signed [sw-1:0]  m_axis_tdata,
    input  wire                  m_axis_tready
);

    // -------------------------------------------------------------------------
    //  handshake
    // -------------------------------------------------------------------------
    wire out_stall       = m_axis_tvalid & ~m_axis_tready;
    assign s_axis_tready = ~out_stall;
    wire fire            = s_axis_tvalid & s_axis_tready;

    // -------------------------------------------------------------------------
    //  dcrm1 인스턴스
    // -------------------------------------------------------------------------
    wire signed [sw-1:0] dc_dout;

    dcrm1 #(
        .sw (sw),
        .DW (DW)
    ) u_dcrm1 (
        .clk  (clk),
        .cen  (fire),
        .rstn (rstn),
        .din  (s_axis_tdata),   // 비트 패턴 그대로 전달
        .dout (dc_dout)
    );

    // -------------------------------------------------------------------------
    //  출력 레지스터
    //  fire 클럭: dcrm1 내부 상태(integ) 갱신 + dc_dout 유효
    //  → 같은 클럭에 래치, 다음 클럭 m_axis_tvalid=1
    // -------------------------------------------------------------------------
    always @(posedge clk) begin
        if (!rstn) begin
            m_axis_tvalid <= 1'b0;
            m_axis_tdata  <= {sw{1'b0}};
        end else begin
            if (fire) begin
                m_axis_tvalid <= 1'b1;
                m_axis_tdata  <= dc_dout;
            end else if (m_axis_tvalid & m_axis_tready) begin
                m_axis_tvalid <= 1'b0;
            end
        end
    end

endmodule