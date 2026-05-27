`timescale 1ns / 1ps

module audio_to_axis #(
    // 8MB 버퍼 기준, 32비트(4바이트) 단위 전송 횟수
    // 8,388,608 Bytes / 4 Bytes = 2,097,152
    parameter PACKET_SIZE = 2097152 
)(
    input wire aclk,
    input wire aresetn,

    // Slave Interface (From Reverb M_AXIS_DMA)
    input  wire [31:0] s_axis_tdata,
    input  wire        s_axis_tvalid,
    output wire        s_axis_tready,

    // Master Interface (To AXI DMA S_AXIS_S2MM)
    output wire [31:0] m_axis_tdata,
    output wire        m_axis_tvalid,
    input  wire        m_axis_tready,
    output wire        m_axis_tlast
);

    // 샘플 개수를 세기 위한 카운터 (2,097,152를 세려면 최소 21비트 이상 필요)
    reg [31:0] sample_count;

    // 데이터와 Valid, Ready 신호는 지연 없이 그대로 통과시킴 (Passthrough)
    assign m_axis_tdata  = s_axis_tdata;
    assign m_axis_tvalid = s_axis_tvalid;
    assign s_axis_tready = m_axis_tready;

    // 카운터가 PACKET_SIZE - 1에 도달했을 때 TLAST를 High로 출력
    assign m_axis_tlast  = (sample_count == (PACKET_SIZE - 1));

    // AXI-Stream Handshake 로직 (Valid와 Ready가 모두 High일 때 데이터가 넘어감)
    always @(posedge aclk) begin
        if (!aresetn) begin
            sample_count <= 32'd0;
        end else begin
            // 데이터가 한 번 전송될 때마다 카운트 증가
            if (s_axis_tvalid && m_axis_tready) begin
                if (sample_count == (PACKET_SIZE - 1)) begin
                    sample_count <= 32'd0; // 패킷의 끝, 카운터 초기화
                end else begin
                    sample_count <= sample_count + 1;
                end
            end
        end
    end

endmodule