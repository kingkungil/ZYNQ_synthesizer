`timescale 1ns / 1ps

module i2c_iobuf (
    input  sda_o,  // 로직에서 나가는 데이터
    input  sda_t,  // Tri-state 제어 (0: Output, 1: Input/Hi-Z)
    output sda_i,  // 로직으로 들어오는 데이터
    inout  sda_pin, // 실제 물리 핀 (SDA)

    input  scl_o,  
    input  scl_t,  
    output scl_i,  
    inout  scl_pin  // 실제 물리 핀 (SCL)
);

    // Xilinx 전용 물리 버퍼 프리미티브 직접 사용
    IOBUF iobuf_sda (
        .O(sda_i),
        .IO(sda_pin),
        .I(sda_o),
        .T(sda_t)
    );

    IOBUF iobuf_scl (
        .O(scl_i),
        .IO(scl_pin),
        .I(scl_o),
        .T(scl_t)
    );

endmodule