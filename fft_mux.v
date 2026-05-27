`timescale 1ns / 1ps

module fft_mux (
    input  wire [1:0]  sel,    // 선택 신호 (00, 01, 10)
    input  wire [15:0] in0,    // 입력 0
    input  wire [15:0] in1,    // 입력 1
    input  wire [15:0] in2,    // 입력 2
    output reg  [15:0] sig     // 선택된 출력
);

    always @(*) begin
        case (sel)
            2'b00:   sig = in0;
            2'b01:   sig = in1;
            2'b10:   sig = in2;
            default: sig = 16'h0000; // 정의되지 않은 값일 때 처리
        endcase
    end

endmodule