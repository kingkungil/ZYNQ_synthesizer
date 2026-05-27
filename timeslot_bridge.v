// ============================================================================
//  timeslot_bridge.v  - async FIFO → 슬롯 기반 이펙터 체인 브리지
//
//  배치: async_fifo.m_axis → [이 모듈] → 이펙터 체인 입구
//
//  동작:
//    - timeslot_master의 sample_req에 맞춰 FIFO에서 샘플 pop
//    - 팝된 샘플을 슬롯0 시작에 맞춰 체인에 주입
//    - 체인 총 레이턴시 = SLOT_TOTAL × N (N은 프레임 수, 고정)
//    - 레이턴시가 고정이므로 출력단에서 단순 카운터로 valid 생성 가능
//
//  중요: 이 모듈은 슬롯 마스터와 이펙터들이
//        동일한 SLOT_TOTAL로 설계되었을 때만 동작
// ============================================================================

module timeslot_bridge #(
    parameter integer DATA_W     = 16,
    parameter integer SLOT_TOTAL = 16,
    parameter integer SLOT_W     = 4,
    // 이펙터 체인이 샘플 1개를 처리하는 데 걸리는 프레임 수
    // (모든 이펙터가 슬롯 규칙을 지키면 항상 1프레임 = SLOT_TOTAL 클럭)
    parameter integer CHAIN_FRAMES = 1
)(
    input  wire              clk,
    input  wire              rstn,

    // timeslot_master 연결
    input  wire [SLOT_W-1:0] slot_id,
    input  wire              frame_start,
    input  wire              sample_req,

    // upstream: async FIFO (50MHz 출구)
    input  wire              fifo_valid,
    input  wire [DATA_W-1:0] fifo_data,
    output reg               fifo_pop,   // FIFO rd_en

    // downstream: 이펙터 체인 (AXI-Stream 호환)
    output reg               m_axis_tvalid,
    output reg  [DATA_W-1:0] m_axis_tdata,
    input  wire              m_axis_tready,

    // 디버그
    output wire              dbg_underrun,
    output wire              dbg_overrun
);

// ============================================================================
//  1단계: sample_req에 맞춰 FIFO pop
//  FIFO가 비어있으면 언더런 플래그 (이때 0 삽입 또는 홀드)
// ============================================================================
reg              pending_sample;
reg [DATA_W-1:0] pending_data;
reg              underrun_r;
reg              overrun_r;

always @(posedge clk) begin
    if (!rstn) begin
        fifo_pop      <= 1'b0;
        pending_sample <= 1'b0;
        pending_data  <= {DATA_W{1'b0}};
        underrun_r    <= 1'b0;
        overrun_r     <= 1'b0;
    end else begin
        fifo_pop    <= 1'b0;
        underrun_r  <= 1'b0;
        overrun_r   <= 1'b0;

        if (sample_req) begin
            if (fifo_valid) begin
                fifo_pop      <= 1'b1;
                pending_sample <= 1'b1;
                pending_data  <= fifo_data;
            end else begin
                // FIFO 언더런: 이전 샘플 홀드 또는 0 출력
                pending_sample <= 1'b1;
                pending_data  <= pending_data; // 홀드 (무음은 {DATA_W{1'b0}})
                underrun_r    <= 1'b1;
            end
        end

        // 이미 pending인데 또 sample_req → 오버런 (sample_req 빈도 > 4MHz)
        if (sample_req && pending_sample && !frame_start)
            overrun_r <= 1'b1;
    end
end

// ============================================================================
//  2단계: frame_start에 맞춰 이펙터 체인으로 주입
//  pending 샘플이 있고 frame_start인 클럭에 m_axis 출력
// ============================================================================
// 총 레이턴시 카운터: CHAIN_FRAMES × SLOT_TOTAL 클럭 후 출력 valid
localparam integer LAT_COUNT = CHAIN_FRAMES * SLOT_TOTAL;

reg [$clog2(LAT_COUNT+1)-1:0] lat_cnt;
reg                            lat_armed;

always @(posedge clk) begin
    if (!rstn) begin
        m_axis_tvalid <= 1'b0;
        m_axis_tdata  <= {DATA_W{1'b0}};
        lat_cnt       <= {$clog2(LAT_COUNT+1){1'b0}};
        lat_armed     <= 1'b0;
    end else begin
        // 주입: frame_start에 pending 샘플 전달
        if (frame_start && pending_sample) begin
            m_axis_tdata  <= pending_data;
            lat_armed     <= 1'b1;
            lat_cnt       <= LAT_COUNT[($clog2(LAT_COUNT+1)-1):0];
        end

        // 레이턴시 카운트다운 → 완료되면 valid 어서트
        if (lat_armed) begin
            if (lat_cnt == 0) begin
                m_axis_tvalid <= 1'b1;
                lat_armed     <= 1'b0;
            end else begin
                lat_cnt <= lat_cnt - 1'b1;
            end
        end

        if (m_axis_tvalid && m_axis_tready)
            m_axis_tvalid <= 1'b0;
    end
end

assign dbg_underrun = underrun_r;
assign dbg_overrun  = overrun_r;

endmodule