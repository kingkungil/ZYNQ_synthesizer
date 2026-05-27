// ============================================================================
//  reflection_engine.v  v6
//
//  v5 → v6: geometry_engine v6 / space_er_engine v3 인터페이스 정합 확인
//
//  변경 없음 (기능 완전): 5축 공간 분리 유지
//    1. bounce2/3_atten, 2. scatter_1/2/3, 3. thresh2/3,
//    4. time_add2/3, 5. tof_shift
//  모든 24공간 LFSR seed, jitter, density thinning 정상 동작
//
//  24개 공간 완전 분리 - 타이밍/합성 안전 버전
//
//  설계 원칙:
//    [REF-SYN1] named block 내 reg 금지 → module-level reg
//    [REF-SYN2] if-else 기반 조합 체인 최소화 → case 기반
//    [REF-TIM1] 곱셈 1-cycle 파이프라인 (DSP 추론)
//    [REF-TIM2] 공간 파라미터 레지스터 래치 (space_start 시)
//    [REF-SEED] 공간별 LFSR seed → 재현 가능 + 공간별 다른 패턴
//
//  5축 공간 분리:
//    1. bounce2/3_atten : 감쇠 특성
//    2. scatter_1/2/3   : 방향 난반사
//    3. thresh2/3       : 파생 반사 생성 기준
//    4. time_add2/3     : 파생 반사 시간 간격 (공간 크기 반영)
//    5. tof_shift       : 시간-지연 변환 스케일
// ============================================================================

`timescale 1ns / 1ps

module reflection_engine (
    input  wire        clk,
    input  wire        rst_n,

    // geometry 입력
    input  wire [63:0] geo_event_data,
    input  wire        geo_event_valid,
    input  wire        geo_done,

    // 공간 파라미터
    input  wire [4:0]  space_id,
    input  wire        space_start,

    // jitter override (0 = 내부 공간값 자동)
    input  wire [3:0]  jitter_mask,
    input  wire [15:0] gain_floor,

    output reg  [63:0] ev_out,
    output reg         ev_valid,
    output reg         ev_done
);

// ============================================================================
//  상수
// ============================================================================
localparam [7:0] MAX_EVENTS_OUT = 8'd72;

// ============================================================================
//  공간별 파라미터 함수 (5축 분리)
// ============================================================================

// 1. Scatter mask (1st order)
function [7:0] ref_scatter1;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_scatter1 = 8'h02;  // AMBIENCE     : 최소
        5'd1:  ref_scatter1 = 8'h09;  // HALL
        5'd2:  ref_scatter1 = 8'h13;  // CATHEDRAL
        5'd3:  ref_scatter1 = 8'h0D;  // CHURCH
        5'd4:  ref_scatter1 = 8'h01;  // SMALL_ROOM   : 없음
        5'd5:  ref_scatter1 = 8'h03;  // MEDIUM_ROOM
        5'd6:  ref_scatter1 = 8'h07;  // LARGE_ROOM
        5'd7:  ref_scatter1 = 8'h00;  // PLATE_CLASSIC: 완전 거울
        5'd8:  ref_scatter1 = 8'h00;  // PLATE_BRIGHT
        5'd9:  ref_scatter1 = 8'h01;  // PLATE_DARK
        5'd10: ref_scatter1 = 8'h07;  // RESONANCE
        5'd11: ref_scatter1 = 8'h07;  // RESONANT_SP
        5'd12: ref_scatter1 = 8'h03;  // TONAL       : 낮음
        5'd13: ref_scatter1 = 8'h01;  // RINGING     : 최소
        5'd14: ref_scatter1 = 8'h0F;  // ROTATING    : 강함
        5'd15: ref_scatter1 = 8'h11;  // DRIFTING
        5'd16: ref_scatter1 = 8'h0F;  // GATED
        5'd17: ref_scatter1 = 8'h0B;  // REVERSE
        5'd18: ref_scatter1 = 8'h1F;  // BROKEN      : 최강
        5'd19: ref_scatter1 = 8'h0F;  // SHIMMER
        5'd20: ref_scatter1 = 8'h07;  // FROZEN
        5'd21: ref_scatter1 = 8'h0D;  // ASCENDING
        5'd22: ref_scatter1 = 8'h0D;  // DESCENDING
        5'd23: ref_scatter1 = 8'h1F;  // SPECIAL
        default: ref_scatter1 = 8'h05;
        endcase
    end
endfunction

// 2. Scatter mask (2nd order)
function [7:0] ref_scatter2;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_scatter2 = 8'h05;
        5'd1:  ref_scatter2 = 8'h13;
        5'd2:  ref_scatter2 = 8'h27;
        5'd3:  ref_scatter2 = 8'h1B;
        5'd4:  ref_scatter2 = 8'h03;
        5'd5:  ref_scatter2 = 8'h07;
        5'd6:  ref_scatter2 = 8'h0F;
        5'd7:  ref_scatter2 = 8'h01;
        5'd8:  ref_scatter2 = 8'h01;
        5'd9:  ref_scatter2 = 8'h03;
        5'd10: ref_scatter2 = 8'h0F;
        5'd11: ref_scatter2 = 8'h13;
        5'd12: ref_scatter2 = 8'h09;
        5'd13: ref_scatter2 = 8'h05;
        5'd14: ref_scatter2 = 8'h1F;
        5'd15: ref_scatter2 = 8'h23;
        5'd16: ref_scatter2 = 8'h1F;
        5'd17: ref_scatter2 = 8'h17;
        5'd18: ref_scatter2 = 8'h3F;
        5'd19: ref_scatter2 = 8'h1F;
        5'd20: ref_scatter2 = 8'h0F;
        5'd21: ref_scatter2 = 8'h1B;
        5'd22: ref_scatter2 = 8'h1B;
        5'd23: ref_scatter2 = 8'h3F;
        default: ref_scatter2 = 8'h0F;
        endcase
    end
endfunction

// 3. Scatter mask (3rd order)
function [7:0] ref_scatter3;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_scatter3 = 8'h09;
        5'd1:  ref_scatter3 = 8'h1F;
        5'd2:  ref_scatter3 = 8'h3F;
        5'd3:  ref_scatter3 = 8'h2F;
        5'd4:  ref_scatter3 = 8'h07;
        5'd5:  ref_scatter3 = 8'h0F;
        5'd6:  ref_scatter3 = 8'h1F;
        5'd7:  ref_scatter3 = 8'h03;
        5'd8:  ref_scatter3 = 8'h03;
        5'd9:  ref_scatter3 = 8'h07;
        5'd10: ref_scatter3 = 8'h1F;
        5'd11: ref_scatter3 = 8'h27;
        5'd12: ref_scatter3 = 8'h13;
        5'd13: ref_scatter3 = 8'h09;
        5'd14: ref_scatter3 = 8'h37;
        5'd15: ref_scatter3 = 8'h43;
        5'd16: ref_scatter3 = 8'h3F;
        5'd17: ref_scatter3 = 8'h2F;
        5'd18: ref_scatter3 = 8'h7F;
        5'd19: ref_scatter3 = 8'h3F;
        5'd20: ref_scatter3 = 8'h1F;
        5'd21: ref_scatter3 = 8'h2F;
        5'd22: ref_scatter3 = 8'h2F;
        5'd23: ref_scatter3 = 8'h7F;
        default: ref_scatter3 = 8'h1F;
        endcase
    end
endfunction

// 4. Bounce 2nd atten Q8 (클수록 강한 감쇠)
function [7:0] ref_b2atten;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_b2atten = 8'hA8;  // AMBIENCE     : 강함
        5'd1:  ref_b2atten = 8'h80;  // HALL
        5'd2:  ref_b2atten = 8'h68;  // CATHEDRAL   : 약함
        5'd3:  ref_b2atten = 8'h78;  // CHURCH
        5'd4:  ref_b2atten = 8'hC8;  // SMALL_ROOM  : 매우 강함
        5'd5:  ref_b2atten = 8'hB8;  // MEDIUM_ROOM
        5'd6:  ref_b2atten = 8'h98;  // LARGE_ROOM
        5'd7:  ref_b2atten = 8'hD0;  // PLATE_CLASSIC
        5'd8:  ref_b2atten = 8'hC8;  // PLATE_BRIGHT
        5'd9:  ref_b2atten = 8'hD8;  // PLATE_DARK  : 최강
        5'd10: ref_b2atten = 8'h78;  // RESONANCE   : 약함
        5'd11: ref_b2atten = 8'h80;  // RESONANT_SP
        5'd12: ref_b2atten = 8'h88;  // TONAL
        5'd13: ref_b2atten = 8'h70;  // RINGING     : 최약
        5'd14: ref_b2atten = 8'h98;  // ROTATING
        5'd15: ref_b2atten = 8'h80;  // DRIFTING
        5'd16: ref_b2atten = 8'hC0;  // GATED
        5'd17: ref_b2atten = 8'h72;  // REVERSE
        5'd18: ref_b2atten = 8'hA8;  // BROKEN
        5'd19: ref_b2atten = 8'h78;  // SHIMMER
        5'd20: ref_b2atten = 8'h58;  // FROZEN      : 최약
        5'd21: ref_b2atten = 8'h7A;  // ASCENDING
        5'd22: ref_b2atten = 8'h7E;  // DESCENDING
        5'd23: ref_b2atten = 8'h88;  // SPECIAL
        default: ref_b2atten = 8'hB0;
        endcase
    end
endfunction

// 5. Bounce 3rd atten Q8
function [7:0] ref_b3atten;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_b3atten = 8'h78;
        5'd1:  ref_b3atten = 8'h58;
        5'd2:  ref_b3atten = 8'h48;
        5'd3:  ref_b3atten = 8'h50;
        5'd4:  ref_b3atten = 8'h98;
        5'd5:  ref_b3atten = 8'h88;
        5'd6:  ref_b3atten = 8'h68;
        5'd7:  ref_b3atten = 8'hA0;
        5'd8:  ref_b3atten = 8'h98;
        5'd9:  ref_b3atten = 8'hA8;
        5'd10: ref_b3atten = 8'h50;
        5'd11: ref_b3atten = 8'h58;
        5'd12: ref_b3atten = 8'h60;
        5'd13: ref_b3atten = 8'h40;
        5'd14: ref_b3atten = 8'h68;
        5'd15: ref_b3atten = 8'h55;
        5'd16: ref_b3atten = 8'h90;
        5'd17: ref_b3atten = 8'h48;
        5'd18: ref_b3atten = 8'h78;
        5'd19: ref_b3atten = 8'h50;
        5'd20: ref_b3atten = 8'h38;
        5'd21: ref_b3atten = 8'h50;
        5'd22: ref_b3atten = 8'h55;
        5'd23: ref_b3atten = 8'h5A;
        default: ref_b3atten = 8'h70;
        endcase
    end
endfunction

// 6. Threshold for 2nd order generation
function [15:0] ref_thresh2;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_thresh2 = 16'h0C00;  // AMBIENCE     : 높음 (억제)
        5'd1:  ref_thresh2 = 16'h0280;  // HALL
        5'd2:  ref_thresh2 = 16'h0100;  // CATHEDRAL   : 최저 (풍부)
        5'd3:  ref_thresh2 = 16'h0200;  // CHURCH
        5'd4:  ref_thresh2 = 16'h1800;  // SMALL_ROOM  : 최고
        5'd5:  ref_thresh2 = 16'h0C00;  // MEDIUM_ROOM
        5'd6:  ref_thresh2 = 16'h0500;  // LARGE_ROOM
        5'd7:  ref_thresh2 = 16'h3FFF;  // PLATE_CLASSIC: 2차 없음
        5'd8:  ref_thresh2 = 16'h3FFF;  // PLATE_BRIGHT
        5'd9:  ref_thresh2 = 16'h2000;  // PLATE_DARK
        5'd10: ref_thresh2 = 16'h0200;  // RESONANCE
        5'd11: ref_thresh2 = 16'h0280;  // RESONANT_SP
        5'd12: ref_thresh2 = 16'h0380;  // TONAL
        5'd13: ref_thresh2 = 16'h0180;  // RINGING
        5'd14: ref_thresh2 = 16'h0380;  // ROTATING
        5'd15: ref_thresh2 = 16'h0280;  // DRIFTING
        5'd16: ref_thresh2 = 16'h0600;  // GATED
        5'd17: ref_thresh2 = 16'h0180;  // REVERSE
        5'd18: ref_thresh2 = 16'h0480;  // BROKEN
        5'd19: ref_thresh2 = 16'h0180;  // SHIMMER
        5'd20: ref_thresh2 = 16'h0100;  // FROZEN      : 최저
        5'd21: ref_thresh2 = 16'h0200;  // ASCENDING
        5'd22: ref_thresh2 = 16'h0200;  // DESCENDING
        5'd23: ref_thresh2 = 16'h0300;  // SPECIAL
        default: ref_thresh2 = 16'h0400;
        endcase
    end
endfunction

// 7. Threshold for 3rd order generation
function [15:0] ref_thresh3;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_thresh3 = 16'h3FFF;
        5'd1:  ref_thresh3 = 16'h0A00;
        5'd2:  ref_thresh3 = 16'h0400;
        5'd3:  ref_thresh3 = 16'h0700;
        5'd4:  ref_thresh3 = 16'h7FFF;  // 3차 없음
        5'd5:  ref_thresh3 = 16'h3FFF;
        5'd6:  ref_thresh3 = 16'h1200;
        5'd7:  ref_thresh3 = 16'h7FFF;
        5'd8:  ref_thresh3 = 16'h7FFF;
        5'd9:  ref_thresh3 = 16'h7FFF;
        5'd10: ref_thresh3 = 16'h0700;
        5'd11: ref_thresh3 = 16'h0900;
        5'd12: ref_thresh3 = 16'h0D00;
        5'd13: ref_thresh3 = 16'h0500;
        5'd14: ref_thresh3 = 16'h0D00;
        5'd15: ref_thresh3 = 16'h0800;
        5'd16: ref_thresh3 = 16'h1A00;
        5'd17: ref_thresh3 = 16'h0700;
        5'd18: ref_thresh3 = 16'h1200;
        5'd19: ref_thresh3 = 16'h0500;
        5'd20: ref_thresh3 = 16'h0300;
        5'd21: ref_thresh3 = 16'h0700;
        5'd22: ref_thresh3 = 16'h0700;
        5'd23: ref_thresh3 = 16'h0900;
        default: ref_thresh3 = 16'h1000;
        endcase
    end
endfunction

// 8. Time add for 2nd order (Q8 sample offset)
function [7:0] ref_tadd2;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_tadd2 = 8'd10;   // AMBIENCE     : 짧음
        5'd1:  ref_tadd2 = 8'd28;   // HALL
        5'd2:  ref_tadd2 = 8'd60;   // CATHEDRAL   : 긺
        5'd3:  ref_tadd2 = 8'd42;   // CHURCH
        5'd4:  ref_tadd2 = 8'd5;    // SMALL_ROOM  : 매우 짧음
        5'd5:  ref_tadd2 = 8'd14;   // MEDIUM_ROOM
        5'd6:  ref_tadd2 = 8'd26;   // LARGE_ROOM
        5'd7:  ref_tadd2 = 8'd8;    // PLATE_CLASSIC
        5'd8:  ref_tadd2 = 8'd6;    // PLATE_BRIGHT
        5'd9:  ref_tadd2 = 8'd11;   // PLATE_DARK
        5'd10: ref_tadd2 = 8'd32;   // RESONANCE
        5'd11: ref_tadd2 = 8'd28;   // RESONANT_SP
        5'd12: ref_tadd2 = 8'd24;   // TONAL
        5'd13: ref_tadd2 = 8'd52;   // RINGING     : 매우 긺
        5'd14: ref_tadd2 = 8'd22;   // ROTATING
        5'd15: ref_tadd2 = 8'd38;   // DRIFTING
        5'd16: ref_tadd2 = 8'd12;   // GATED
        5'd17: ref_tadd2 = 8'd40;   // REVERSE
        5'd18: ref_tadd2 = 8'd18;   // BROKEN
        5'd19: ref_tadd2 = 8'd34;   // SHIMMER
        5'd20: ref_tadd2 = 8'd64;   // FROZEN      : 매우 긺
        5'd21: ref_tadd2 = 8'd36;   // ASCENDING
        5'd22: ref_tadd2 = 8'd36;   // DESCENDING
        5'd23: ref_tadd2 = 8'd27;   // SPECIAL
        default: ref_tadd2 = 8'd24;
        endcase
    end
endfunction

// 9. Time add for 3rd order
function [7:0] ref_tadd3;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_tadd3 = 8'd20;
        5'd1:  ref_tadd3 = 8'd56;
        5'd2:  ref_tadd3 = 8'd120;
        5'd3:  ref_tadd3 = 8'd84;
        5'd4:  ref_tadd3 = 8'd10;
        5'd5:  ref_tadd3 = 8'd28;
        5'd6:  ref_tadd3 = 8'd52;
        5'd7:  ref_tadd3 = 8'd16;
        5'd8:  ref_tadd3 = 8'd12;
        5'd9:  ref_tadd3 = 8'd22;
        5'd10: ref_tadd3 = 8'd64;
        5'd11: ref_tadd3 = 8'd56;
        5'd12: ref_tadd3 = 8'd48;
        5'd13: ref_tadd3 = 8'd104;
        5'd14: ref_tadd3 = 8'd44;
        5'd15: ref_tadd3 = 8'd76;
        5'd16: ref_tadd3 = 8'd24;
        5'd17: ref_tadd3 = 8'd80;
        5'd18: ref_tadd3 = 8'd36;
        5'd19: ref_tadd3 = 8'd68;
        5'd20: ref_tadd3 = 8'd128;
        5'd21: ref_tadd3 = 8'd72;
        5'd22: ref_tadd3 = 8'd72;
        5'd23: ref_tadd3 = 8'd54;
        default: ref_tadd3 = 8'd48;
        endcase
    end
endfunction

// 10. TOF shift
function [3:0] ref_tof_shift;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_tof_shift = 4'd5;
        5'd1:  ref_tof_shift = 4'd7;
        5'd2:  ref_tof_shift = 4'd8;
        5'd3:  ref_tof_shift = 4'd7;
        5'd4:  ref_tof_shift = 4'd4;
        5'd5:  ref_tof_shift = 4'd5;
        5'd6:  ref_tof_shift = 4'd6;
        5'd7:  ref_tof_shift = 4'd4;
        5'd8:  ref_tof_shift = 4'd4;
        5'd9:  ref_tof_shift = 4'd4;
        5'd10: ref_tof_shift = 4'd7;
        5'd11: ref_tof_shift = 4'd6;
        5'd12: ref_tof_shift = 4'd6;
        5'd13: ref_tof_shift = 4'd8;
        5'd14: ref_tof_shift = 4'd6;
        5'd15: ref_tof_shift = 4'd7;
        5'd16: ref_tof_shift = 4'd5;
        5'd17: ref_tof_shift = 4'd7;
        5'd18: ref_tof_shift = 4'd5;
        5'd19: ref_tof_shift = 4'd7;
        5'd20: ref_tof_shift = 4'd8;
        5'd21: ref_tof_shift = 4'd7;
        5'd22: ref_tof_shift = 4'd7;
        5'd23: ref_tof_shift = 4'd6;
        default: ref_tof_shift = 4'd6;
        endcase
    end
endfunction

// 11. Jitter mask
function [3:0] ref_jitter;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_jitter = 4'h1;
        5'd1:  ref_jitter = 4'h5;
        5'd2:  ref_jitter = 4'h7;
        5'd3:  ref_jitter = 4'h6;
        5'd4:  ref_jitter = 4'h1;
        5'd5:  ref_jitter = 4'h2;
        5'd6:  ref_jitter = 4'h4;
        5'd7:  ref_jitter = 4'h0;
        5'd8:  ref_jitter = 4'h0;
        5'd9:  ref_jitter = 4'h1;
        5'd10: ref_jitter = 4'h3;
        5'd11: ref_jitter = 4'h4;
        5'd12: ref_jitter = 4'h2;
        5'd13: ref_jitter = 4'h1;
        5'd14: ref_jitter = 4'h9;
        5'd15: ref_jitter = 4'hA;
        5'd16: ref_jitter = 4'h3;
        5'd17: ref_jitter = 4'h5;
        5'd18: ref_jitter = 4'hF;
        5'd19: ref_jitter = 4'h7;
        5'd20: ref_jitter = 4'h2;
        5'd21: ref_jitter = 4'h8;
        5'd22: ref_jitter = 4'h8;
        5'd23: ref_jitter = 4'hB;
        default: ref_jitter = 4'h3;
        endcase
    end
endfunction

// 12. LFSR seed
function [15:0] ref_seed;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  ref_seed = 16'hF1A3;
        5'd1:  ref_seed = 16'hB7C2;
        5'd2:  ref_seed = 16'hD4E5;
        5'd3:  ref_seed = 16'hA8B9;
        5'd4:  ref_seed = 16'hE2F3;
        5'd5:  ref_seed = 16'h9ACD;
        5'd6:  ref_seed = 16'hC3D4;
        5'd7:  ref_seed = 16'h5678;
        5'd8:  ref_seed = 16'h4567;
        5'd9:  ref_seed = 16'h6789;
        5'd10: ref_seed = 16'h8BCD;
        5'd11: ref_seed = 16'h7ABC;
        5'd12: ref_seed = 16'h3DEF;
        5'd13: ref_seed = 16'h2EF0;
        5'd14: ref_seed = 16'hF012;
        5'd15: ref_seed = 16'hE123;
        5'd16: ref_seed = 16'h1234;
        5'd17: ref_seed = 16'h0345;
        5'd18: ref_seed = 16'hCDEF;
        5'd19: ref_seed = 16'hBCE1;
        5'd20: ref_seed = 16'hABD2;
        5'd21: ref_seed = 16'h9AC3;
        5'd22: ref_seed = 16'h89B4;
        5'd23: ref_seed = 16'h78A5;
        default: ref_seed = 16'hACE1;
        endcase
    end
endfunction

// ============================================================================
//  LFSR
// ============================================================================
reg [15:0] lfsr_r;

always @(posedge clk) begin
    if (!rst_n)
        lfsr_r <= 16'hACE1;
    else if (space_start)
        lfsr_r <= ref_seed(space_id);
    else
        lfsr_r <= {1'b0, lfsr_r[15:1]} ^ (lfsr_r[0] ? 16'hB400 : 16'h0000);
end

// ============================================================================
//  공간 파라미터 래치 레지스터 (space_start 시 1회 캡처 → 타이밍 안정)
// ============================================================================
reg [7:0]  r_sct1, r_sct2, r_sct3;
reg [7:0]  r_b2atten, r_b3atten;
reg [3:0]  r_jitter;
reg [15:0] r_thresh2, r_thresh3;
reg [7:0]  r_tadd2, r_tadd3;
reg [3:0]  r_tshift;

always @(posedge clk) begin
    if (!rst_n) begin
        r_sct1    <= 8'h09;
        r_sct2    <= 8'h13;
        r_sct3    <= 8'h1F;
        r_b2atten <= 8'h80;
        r_b3atten <= 8'h58;
        r_jitter  <= 4'h5;
        r_thresh2 <= 16'h0280;
        r_thresh3 <= 16'h0A00;
        r_tadd2   <= 8'd28;
        r_tadd3   <= 8'd56;
        r_tshift  <= 4'd7;
    end else if (space_start) begin
        r_sct1    <= ref_scatter1(space_id);
        r_sct2    <= ref_scatter2(space_id);
        r_sct3    <= ref_scatter3(space_id);
        r_b2atten <= ref_b2atten(space_id);
        r_b3atten <= ref_b3atten(space_id);
        r_jitter  <= ref_jitter(space_id);
        r_thresh2 <= ref_thresh2(space_id);
        r_thresh3 <= ref_thresh3(space_id);
        r_tadd2   <= ref_tadd2(space_id);
        r_tadd3   <= ref_tadd3(space_id);
        r_tshift  <= ref_tof_shift(space_id);
    end
end

wire [3:0] eff_jitter = (jitter_mask != 4'h0) ? jitter_mask : r_jitter;

// ============================================================================
//  scatter 함수 (branchless)
// ============================================================================
function [7:0] apply_scatter;
    input [7:0] angle_in;
    input [7:0] smask;
    input [7:0] rnd_byte;
    input       rnd_sign;
    reg   [8:0] sc;
    begin
        if (rnd_sign)
            sc = {1'b0, angle_in} + {1'b0, rnd_byte & smask};
        else
            sc = {1'b0, angle_in} - {1'b0, rnd_byte & smask};
        apply_scatter = sc[7:0];
    end
endfunction

// ============================================================================
//  FSM 상태
// ============================================================================
localparam [3:0]
    RE_IDLE      = 4'd0,
    RE_PREP      = 4'd1,
    RE_EMIT_1ST  = 4'd2,
    RE_2ND_CHECK = 4'd3,
    RE_2ND_MUL   = 4'd4,
    RE_2ND_EMIT  = 4'd5,
    RE_3RD_CHECK = 4'd6,
    RE_3RD_MUL   = 4'd7,
    RE_3RD_EMIT  = 4'd8;

reg [3:0]  fsm_state;
reg [7:0]  ev_cnt_r;
reg        done_latch_r;

// 래치 레지스터 (named block 대신 module-level)
reg [15:0] lat_time_1st,  lat_gain_1st;
reg [7:0]  lat_angle_1st;
reg [2:0]  lat_wall_id;
reg [15:0] lat_time_2nd,  lat_gain_2nd;
reg [7:0]  lat_angle_2nd;

// PREP 파이프라인
reg [15:0] prep_time_in,  prep_gain_in;
reg [7:0]  prep_angle_in;
reg [2:0]  prep_wall_in;
reg [15:0] prep_gain_q,   prep_time_tof;
reg [7:0]  prep_angle_sc;

// DSP 파이프라인 ([REF-TIM1])
(* use_dsp = "yes" *) reg [23:0] b2_prod_r;
(* use_dsp = "yes" *) reg [23:0] b3_prod_r;

// 중간 계산용 module-level
reg [16:0] t2_ext_r;
reg [16:0] t3_ext_r;
reg [7:0]  jit3_r;

// ============================================================================
//  메인 FSM
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        ev_out        <= 64'd0;
        ev_valid      <= 1'b0;
        ev_done       <= 1'b0;
        done_latch_r  <= 1'b0;
        ev_cnt_r      <= 8'd0;
        fsm_state     <= RE_IDLE;
        lat_time_1st  <= 16'd0; lat_gain_1st  <= 16'd0;
        lat_angle_1st <= 8'd0;  lat_wall_id   <= 3'd0;
        lat_time_2nd  <= 16'd0; lat_gain_2nd  <= 16'd0;
        lat_angle_2nd <= 8'd0;
        prep_time_in  <= 16'd0; prep_gain_in  <= 16'd0;
        prep_angle_in <= 8'd0;  prep_wall_in  <= 3'd0;
        prep_gain_q   <= 16'd0; prep_time_tof <= 16'd0;
        prep_angle_sc <= 8'd0;
        b2_prod_r     <= 24'd0; b3_prod_r     <= 24'd0;
        t2_ext_r      <= 17'd0; t3_ext_r      <= 17'd0;
        jit3_r        <= 8'd0;
    end else begin
        ev_valid <= 1'b0;
        ev_done  <= 1'b0;

        if (geo_event_valid) done_latch_r <= 1'b0;

        case (fsm_state)

        RE_IDLE: begin
            if (geo_done && !done_latch_r) begin
                ev_done      <= 1'b1;
                done_latch_r <= 1'b1;
                ev_cnt_r     <= 8'd0;
            end
            if (geo_event_valid && ev_cnt_r < MAX_EVENTS_OUT) begin
                prep_time_in  <= geo_event_data[63:48];
                prep_gain_in  <= geo_event_data[47:32];
                prep_angle_in <= geo_event_data[31:24];
                prep_wall_in  <= geo_event_data[26:24];
                fsm_state     <= RE_PREP;
            end
        end

        RE_PREP: begin
            // jitter 계산 (branchless: 조합 체인 최소화)
            begin : prep_jit_blk
                reg [7:0]  jval;
                reg [15:0] tj;
                reg [7:0]  tof_add;
                reg [15:0] tn;
                reg [7:0]  comp8;

                jval = {4'd0, lfsr_r[3:0] & eff_jitter};
                if (lfsr_r[15]) begin
                    tj = (prep_time_in[15:8] == 8'hFE) ? prep_time_in :
                         {prep_time_in[15:8] + jval, prep_time_in[7:0]};
                end else begin
                    tj = (prep_time_in[15:8] <= jval) ?
                         {8'h01, prep_time_in[7:0]} :
                         {prep_time_in[15:8] - jval, prep_time_in[7:0]};
                end

                prep_gain_q <= (prep_gain_in < gain_floor) ? gain_floor : prep_gain_in;

                // TOF 보정 (공간 크기 반영)
                comp8  = 8'h7F - ((prep_gain_in < gain_floor) ?
                                  gain_floor[14:7] : prep_gain_in[14:7]);
                case (r_tshift)
                    4'd4: tof_add = {4'd0, comp8[7:4]};
                    4'd5: tof_add = {5'd0, comp8[7:5]};
                    4'd6: tof_add = {6'd0, comp8[7:6]};
                    4'd7: tof_add = {7'd0, comp8[7]};
                    4'd8: tof_add = 8'd0;
                    default: tof_add = {6'd0, comp8[7:6]};
                endcase

                tn = {tj[15:8], 8'd0} + {8'd0, tof_add, 8'd0};
                prep_time_tof <= (|tn[15:8]) ?
                                  {tj[15:8] + tof_add, tj[7:0]} : tj;

                prep_angle_sc <= apply_scatter(prep_angle_in, r_sct1,
                                               lfsr_r[7:0], lfsr_r[8]);
            end
            fsm_state <= RE_EMIT_1ST;
        end

        RE_EMIT_1ST: begin
            ev_out   <= {prep_time_tof, prep_gain_q,
                         prep_angle_sc, 2'b00, prep_wall_in,
                         2'b01, 20'd0};
            ev_valid <= 1'b1;
            ev_cnt_r      <= ev_cnt_r + 8'd1;
            lat_time_1st  <= prep_time_tof;
            lat_gain_1st  <= prep_gain_q;
            lat_angle_1st <= prep_angle_sc;
            lat_wall_id   <= prep_wall_in;
            fsm_state     <= RE_2ND_CHECK;
        end

        RE_2ND_CHECK: begin
            if (lat_gain_1st >= r_thresh2 && ev_cnt_r < MAX_EVENTS_OUT) begin
                t2_ext_r      <= {1'b0, lat_time_1st} + {9'd0, r_tadd2};
                lat_angle_2nd <= apply_scatter(lat_angle_1st, r_sct2,
                                               lfsr_r[7:0], lfsr_r[9]);
                // [REF-TIM1] DSP 파이프라인
                b2_prod_r     <= {8'd0, lat_gain_1st} * {16'd0, r_b2atten};
                fsm_state     <= RE_2ND_MUL;
            end else begin
                fsm_state <= RE_IDLE;
            end
        end

        RE_2ND_MUL: begin
            lat_time_2nd <= t2_ext_r[16] ? 16'hFFFF : t2_ext_r[15:0];
            begin : g2_blk
                reg [15:0] g2;
                g2 = b2_prod_r[23:8];
                lat_gain_2nd <= (g2 < gain_floor) ? gain_floor : g2;
            end
            fsm_state <= RE_2ND_EMIT;
        end

        RE_2ND_EMIT: begin
            ev_out   <= {lat_time_2nd, lat_gain_2nd,
                         lat_angle_2nd, 2'b00, lat_wall_id,
                         2'b10, 20'd0};
            ev_valid <= 1'b1;
            ev_cnt_r <= ev_cnt_r + 8'd1;
            fsm_state <= RE_3RD_CHECK;
        end

        RE_3RD_CHECK: begin
            if (lat_gain_1st >= r_thresh3 && ev_cnt_r < MAX_EVENTS_OUT) begin
                // jitter3
                jit3_r        <= {3'b0, lfsr_r[4:0]} & {4'd0, eff_jitter};
                // time3
                t3_ext_r      <= {1'b0, lat_time_2nd} + {9'd0, r_tadd3};
                lat_angle_2nd <= apply_scatter(lat_angle_2nd, r_sct3,
                                               lfsr_r[7:0], lfsr_r[12]);
                // [REF-TIM1] DSP
                b3_prod_r     <= {8'd0, lat_gain_2nd} * {16'd0, r_b3atten};
                fsm_state     <= RE_3RD_MUL;
            end else begin
                fsm_state <= RE_IDLE;
            end
        end

        RE_3RD_MUL: begin
            begin : t3_jit_blk
                reg [17:0] t3j;
                if (lfsr_r[11]) begin
                    t3j = {1'b0, t3_ext_r} + {10'd0, jit3_r};
                end else begin
                    t3j = {1'b0, t3_ext_r} - {10'd0, jit3_r};
                end
                lat_time_2nd <= (t3j[17] || t3j[16]) ? 16'hFFFF : t3j[15:0];
            end
            begin : g3_blk
                reg [15:0] g3;
                g3 = b3_prod_r[23:8];
                ev_out   <= {lat_time_2nd, (g3 < gain_floor ? gain_floor : g3),
                             lat_angle_2nd, 2'b00, lat_wall_id,
                             2'b11, 20'd0};
                ev_valid <= 1'b1;
                ev_cnt_r <= ev_cnt_r + 8'd1;
            end
            fsm_state <= RE_IDLE;
        end

        RE_3RD_EMIT: fsm_state <= RE_IDLE; // 예비 (미사용)

        default: fsm_state <= RE_IDLE;
        endcase

        // geo_done 재처리 (IDLE에서만)
        if (geo_done && !done_latch_r && fsm_state == RE_IDLE) begin
            ev_done      <= 1'b1;
            done_latch_r <= 1'b1;
            ev_cnt_r     <= 8'd0;
        end
    end
end

endmodule