`timescale 1ns / 1ps

module audio_engine_mux (
    input  wire [1:0]  addr,           // 제어 대상 선택 주소
    input  wire        fifo_full,      // FIFO 상태 신호
    input  wire        orig_cen,       // 원래 Clock Enable
    input  wire        orig_fm_en,     // 원래 FM Enable
    input  wire        orig_psg_en,    // 원래 PSG Enable
    
    output wire        gated_cen,
    output wire        gated_fm_en,
    output wire        gated_psg_en
);

    /* [주소 정의 예시]
       addr == 2'b00 : 전체 정지 (Full이 뜨면 모두 Stop)
       addr == 2'b01 : FM만 정지 (Full이 떠도 PSG는 동작)
       addr == 2'b10 : PSG만 정지 (Full이 떠도 FM은 동작)
       addr == 2'b11 : 통과 (Full 무시, 모두 동작 - 디버깅용)
    */

    // 1. FM 제어 로직
    // 주소가 00(전체)이거나 01(FM)일 때만 full 신호의 영향을 받음
    wire fm_stop_condition = (addr == 2'b00 || addr == 2'b01) && fifo_full;
    assign gated_fm_en = fm_stop_condition ? 1'b0 : orig_fm_en;

    // 2. PSG 제어 로직
    // 주소가 00(전체)이거나 10(PSG)일 때만 full 신호의 영향을 받음
    wire psg_stop_condition = (addr == 2'b00 || addr == 2'b10) && fifo_full;
    assign gated_psg_en = psg_stop_condition ? 1'b0 : orig_psg_en;

    // 3. CEN 제어 로직 (메인 엔진)
    // 보통 전체 정지 주소(00)일 때만 메인 클럭을 멈춤
    wire cen_stop_condition = (addr == 2'b00) && fifo_full;
    assign gated_cen = cen_stop_condition ? 1'b0 : orig_cen;

endmodule