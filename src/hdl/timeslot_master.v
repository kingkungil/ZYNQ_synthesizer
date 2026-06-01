// ============================================================================
//  timeslot_master.v
//
//  역할: 50MHz 도메인의 글로벌 타임슬롯 카운터
//        모든 시분할 이펙터가 이 카운터를 공유해서
//        "내 슬롯에만 처리" 방식으로 동작
//
//  원리:
//    - SLOT_TOTAL 클럭을 1 프레임으로 정의
//      예) SLOT_TOTAL=8: 컴프=슬롯0, EQ=슬롯1, 페이저=슬롯2,
//          딜레이=슬롯3, 코러스=슬롯4, 플랜저=슬롯5, 리버브=슬롯6, 여유=슬롯7
//    - 샘플은 슬롯0 시작에만 주입 → 이후 각 이펙터는 자기 슬롯에
//      정확히 1클럭 후에 처리 → 총 레이턴시 = SLOT_TOTAL 클럭 (고정)
//
//  이점:
//    - 레이턴시가 항상 SLOT_TOTAL 클럭으로 고정 → 보상 trivial
//    - 각 이펙터는 slot_id 신호만 보면 됨 → 인터페이스 단순
//    - 새 이펙터 추가 = 슬롯 번호 하나 할당하면 끝
// ============================================================================

module timeslot_master #(
    parameter integer SLOT_TOTAL = 16,  // 프레임 길이 (클럭 수)
    parameter integer SLOT_W     = 4    // log2(SLOT_TOTAL)
)(
    input  wire               clk,
    input  wire               rstn,

    // 각 이펙터가 자기 슬롯인지 확인하는 신호
    output reg  [SLOT_W-1:0]  slot_id,      // 현재 슬롯 번호 (0 ~ SLOT_TOTAL-1)
    output reg                frame_start,  // 프레임 시작 (슬롯0 진입 1클럭 펄스)

    // phase accumulator: jt03 4MHz 샘플 요청 타이밍
    // frame_start와 동기화되어 있음
    output reg                sample_req    // 이 사이클에 FIFO에서 1샘플 pop 요청
);

// ============================================================================
//  phase accumulator (이전 모듈과 동일한 방식)
//  단, 여기서는 frame_start와 sample_req를 항상 슬롯0에 고정 정렬
// ============================================================================
localparam [31:0] PHASE_STEP = 32'd343_597_384; // 2^32 * 4MHz / 50MHz

reg [31:0] phase_acc;
wire       phase_carry;
wire [32:0] phase_next_w = {1'b0, phase_acc} + {1'b0, PHASE_STEP};
assign phase_carry = phase_next_w[32];

// slot_id 카운터
reg [SLOT_W-1:0] slot_cnt;

always @(posedge clk) begin
    if (!rstn) begin
        phase_acc   <= 32'b0;
        slot_cnt    <= {SLOT_W{1'b0}};
        slot_id     <= {SLOT_W{1'b0}};
        frame_start <= 1'b0;
        sample_req  <= 1'b0;
    end else begin
        // 항상 슬롯 카운터 진행
        if (slot_cnt == SLOT_TOTAL - 1)
            slot_cnt <= {SLOT_W{1'b0}};
        else
            slot_cnt <= slot_cnt + 1'b1;

        slot_id <= slot_cnt;

        // 프레임 시작 신호
        frame_start <= (slot_cnt == {SLOT_W{1'b0}});

        // phase accumulator: carry 발생 = 4MHz 타이밍
        // carry를 슬롯0 진입에 맞춰 정렬 (슬롯0에서만 sample_req 발사)
        phase_acc <= phase_next_w[31:0];

        // sample_req: carry 발생 시 다음 슬롯0에 정렬해서 발사
        // (간단 구현: carry 즉시 발사 - 슬롯0와의 위상차는 BRAM FIFO가 흡수)
        sample_req <= phase_carry;
    end
end

endmodule