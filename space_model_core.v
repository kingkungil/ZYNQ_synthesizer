// ============================================================================
//  space_model_core.v  v1
//
//  [P2] SPACE_MODEL_CORE - 24공간 단일 물리 파라미터 진입점
//
//  설계 원칙:
//    space_id → ONE physical parameter set ONLY
//    이 모듈이 space_id의 유일한 해석자
//    모든 하위 모듈은 이 모듈 출력만 참조해야 함
//
//  [SMC-1] 출력 파라미터 정의
//    rt60_low  Q24: 저역 RT60 감쇠 계수 (exp 근사)
//    rt60_mid  Q24: 중역 RT60 감쇠 계수
//    rt60_high Q24: 고역 RT60 감쇠 계수
//    er_density: ER 밀도 (cluster threshold 결정용)
//    er_spread : ER 확산 폭
//    lpf_alpha Q24: 스펙트럼 틸트 LPF
//    diff_pre  : pre-diffusion 계수
//    diff_post : post-diffusion 계수
//    cluster_thr: cluster 분류 임계값
//    inject_gain: ER→FDN injection gain
//
//  [SMC-2] custom mode 지원
//    SP_CUSTOM(5'd31) 또는 preset_en=0이면 AXI override 값 사용
//
//  [SMC-3] 합성 안전
//    모든 출력 레지스터 (combinational loop 방지)
//    모든 FF 리셋값 명시
//    function 내 non-blocking 금지
// ============================================================================

`timescale 1ns / 1ps

module space_model_core (
    input  wire        clk,
    input  wire        rst_n,

    // ── 공간 선택 ──────────────────────────────────────────────────────────
    input  wire [4:0]  space_id,
    input  wire        preset_en,       // 0=custom, 1=preset

    // ── AXI override (custom mode 시 사용) ──────────────────────────────
    input  wire [31:0] rt60_low_custom,
    input  wire [31:0] rt60_mid_custom,
    input  wire [31:0] rt60_high_custom,
    input  wire [31:0] lpf_alpha_custom,
    input  wire [15:0] pre_diff_custom,
    input  wire [15:0] post_diff_custom,

    // ── 물리 파라미터 출력 (등록된 값, 1-cycle 지연) ───────────────────
    output reg  [31:0] rt60_low_out,
    output reg  [31:0] rt60_mid_out,
    output reg  [31:0] rt60_high_out,
    output reg  [15:0] er_density_out,
    output reg  [15:0] er_spread_out,
    output reg  [31:0] lpf_alpha_out,
    output reg  [15:0] diff_pre_out,
    output reg  [15:0] diff_post_out,
    output reg  [15:0] cluster_thr_out,
    output reg  [31:0] inject_gain_out,

    // ── [v9] behavior layer seed 출력 ──────────────────────────────────────
    output reg  [7:0]  family_id_out,    // 공간 family 분류 (0~7)
    output reg  [7:0]  behavior_seed_out // deterministic hash seed
);

// ============================================================================
//  SP_CUSTOM 정의
// ============================================================================
localparam [4:0] SP_CUSTOM = 5'd31;

// ============================================================================
//  RT60 상수 (Q24, exp(-6.91 * d / (RT60_s * 48000)) 근사)
// ============================================================================
localparam [31:0] RT60_HALL_L = 32'h00F0CCCC; localparam [31:0] RT60_HALL_M = 32'h00E14000; localparam [31:0] RT60_HALL_H = 32'h00C7AE14;
localparam [31:0] RT60_CAT_L  = 32'h00FF8000; localparam [31:0] RT60_CAT_M  = 32'h00F5C28F; localparam [31:0] RT60_CAT_H  = 32'h00D47AE1;
localparam [31:0] RT60_CHR_L  = 32'h00F5C28F; localparam [31:0] RT60_CHR_M  = 32'h00EB851F; localparam [31:0] RT60_CHR_H  = 32'h00D99999;
localparam [31:0] RT60_PLC_L  = 32'h00E80000; localparam [31:0] RT60_PLC_M  = 32'h00E80000; localparam [31:0] RT60_PLC_H  = 32'h00E60000;
localparam [31:0] RT60_PLB_L  = 32'h00E00000; localparam [31:0] RT60_PLB_M  = 32'h00DF0000; localparam [31:0] RT60_PLB_H  = 32'h00DE0000;
localparam [31:0] RT60_PLD_L  = 32'h00F00000; localparam [31:0] RT60_PLD_M  = 32'h00E80000; localparam [31:0] RT60_PLD_H  = 32'h00CC0000;
localparam [31:0] RT60_SR_L   = 32'h00C73999; localparam [31:0] RT60_SR_M   = 32'h00C20000; localparam [31:0] RT60_SR_H   = 32'h00B40000;
localparam [31:0] RT60_MR_L   = 32'h00D80000; localparam [31:0] RT60_MR_M   = 32'h00D20000; localparam [31:0] RT60_MR_H   = 32'h00C60000;
localparam [31:0] RT60_LR_L   = 32'h00E60000; localparam [31:0] RT60_LR_M   = 32'h00E00000; localparam [31:0] RT60_LR_H   = 32'h00D60000;
localparam [31:0] RT60_AMB_L  = 32'h00CF0000; localparam [31:0] RT60_AMB_M  = 32'h00C80000; localparam [31:0] RT60_AMB_H  = 32'h00BE0000;
localparam [31:0] RT60_SHM_L  = 32'h00F60000; localparam [31:0] RT60_SHM_M  = 32'h00F10000; localparam [31:0] RT60_SHM_H  = 32'h00E40000;
localparam [31:0] RT60_GAT_L  = 32'h00DA0000; localparam [31:0] RT60_GAT_M  = 32'h00D60000; localparam [31:0] RT60_GAT_H  = 32'h00CF0000;
localparam [31:0] RT60_RNG_L  = 32'h00F30000; localparam [31:0] RT60_RNG_M  = 32'h00F28000; localparam [31:0] RT60_RNG_H  = 32'h00F00000;

// ============================================================================
//  ER 밀도/확산 상수
// ============================================================================
localparam [15:0] ER_DENSE  = 16'h00F0; localparam [15:0] ER_HIGH   = 16'h00C0;
localparam [15:0] ER_MED    = 16'h0080; localparam [15:0] ER_LOW    = 16'h0040;
localparam [15:0] ER_SPARSE = 16'h0020;
localparam [15:0] SP_NARROW = 16'h0030; localparam [15:0] SP_MED    = 16'h0070;
localparam [15:0] SP_WIDE   = 16'h00C0; localparam [15:0] SP_VWIDE  = 16'h0100;

// ============================================================================
//  Diffusion 상수
// ============================================================================
localparam [15:0] DIFF_NORMAL = 16'h0000; localparam [15:0] DIFF_DENSE  = 16'h4000;
localparam [15:0] DIFF_VDENSE = 16'h6000;

// ============================================================================
//  LPF alpha 상수
// ============================================================================
localparam [31:0] SPEC_VDARK  = 32'h00333333; localparam [31:0] SPEC_DARK   = 32'h00566666;
localparam [31:0] SPEC_MID    = 32'h006AAAAA; localparam [31:0] SPEC_LIGHT  = 32'h008B3333;
localparam [31:0] SPEC_BRIGHT = 32'h009CCCCC; localparam [31:0] SPEC_VBRIGHT= 32'h00AAE147;
localparam [31:0] SPEC_SPRING = 32'h00C00000;

// ============================================================================
//  Cluster threshold (density 기반)
// ============================================================================
function [15:0] density_to_thr;
    input [15:0] den;
    begin
        if      (den >= ER_DENSE) density_to_thr = 16'h0080;
        else if (den >= ER_HIGH)  density_to_thr = 16'h0100;
        else if (den >= ER_MED)   density_to_thr = 16'h0200;
        else if (den >= ER_LOW)   density_to_thr = 16'h0400;
        else                      density_to_thr = 16'h0800;
    end
endfunction

// ============================================================================
//  24공간 물리 파라미터 LUT 함수
// ============================================================================
function [31:0] smc_rt60_low;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  smc_rt60_low = RT60_AMB_L;  // AMBIENCE
        5'd1:  smc_rt60_low = RT60_HALL_L; // HALL
        5'd2:  smc_rt60_low = RT60_CAT_L;  // CATHEDRAL
        5'd3:  smc_rt60_low = RT60_CHR_L;  // CHURCH
        5'd4:  smc_rt60_low = RT60_SR_L;   // SMALL_ROOM
        5'd5:  smc_rt60_low = RT60_MR_L;   // MEDIUM_ROOM
        5'd6:  smc_rt60_low = RT60_LR_L;   // LARGE_ROOM
        5'd7:  smc_rt60_low = RT60_PLC_L;  // PLATE_CLASSIC
        5'd8:  smc_rt60_low = RT60_PLB_L;  // PLATE_BRIGHT
        5'd9:  smc_rt60_low = RT60_PLD_L;  // PLATE_DARK
        5'd10: smc_rt60_low = RT60_RNG_L;  // RESONANCE (long)
        5'd11: smc_rt60_low = RT60_CHR_L;  // RESONANT_SP
        5'd12: smc_rt60_low = RT60_LR_L;   // TONAL
        5'd13: smc_rt60_low = RT60_RNG_L;  // RINGING
        5'd14: smc_rt60_low = RT60_MR_L;   // ROTATING
        5'd15: smc_rt60_low = RT60_CHR_L;  // DRIFTING
        5'd16: smc_rt60_low = RT60_GAT_L;  // GATED
        5'd17: smc_rt60_low = RT60_CHR_L;  // REVERSE
        5'd18: smc_rt60_low = RT60_RNG_L;  // BROKEN
        5'd19: smc_rt60_low = RT60_SHM_L;  // SHIMMER
        5'd20: smc_rt60_low = RT60_CAT_L;  // FROZEN
        5'd21: smc_rt60_low = RT60_SHM_L;  // ASCENDING
        5'd22: smc_rt60_low = RT60_SHM_L;  // DESCENDING
        5'd23: smc_rt60_low = RT60_HALL_L; // SPECIAL
        default: smc_rt60_low = RT60_HALL_L;
        endcase
    end
endfunction

function [31:0] smc_rt60_mid;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  smc_rt60_mid = RT60_AMB_M;
        5'd1:  smc_rt60_mid = RT60_HALL_M;
        5'd2:  smc_rt60_mid = RT60_CAT_M;
        5'd3:  smc_rt60_mid = RT60_CHR_M;
        5'd4:  smc_rt60_mid = RT60_SR_M;
        5'd5:  smc_rt60_mid = RT60_MR_M;
        5'd6:  smc_rt60_mid = RT60_LR_M;
        5'd7:  smc_rt60_mid = RT60_PLC_M;
        5'd8:  smc_rt60_mid = RT60_PLB_M;
        5'd9:  smc_rt60_mid = RT60_PLD_M;
        5'd10: smc_rt60_mid = RT60_RNG_M;
        5'd11: smc_rt60_mid = RT60_CHR_M;
        5'd12: smc_rt60_mid = RT60_LR_M;
        5'd13: smc_rt60_mid = RT60_RNG_M;
        5'd14: smc_rt60_mid = RT60_MR_M;
        5'd15: smc_rt60_mid = RT60_CHR_M;
        5'd16: smc_rt60_mid = RT60_GAT_M;
        5'd17: smc_rt60_mid = RT60_CHR_M;
        5'd18: smc_rt60_mid = RT60_RNG_M;
        5'd19: smc_rt60_mid = RT60_SHM_M;
        5'd20: smc_rt60_mid = RT60_CAT_M;
        5'd21: smc_rt60_mid = RT60_SHM_M;
        5'd22: smc_rt60_mid = RT60_SHM_M;
        5'd23: smc_rt60_mid = RT60_HALL_M;
        default: smc_rt60_mid = RT60_HALL_M;
        endcase
    end
endfunction

function [31:0] smc_rt60_high;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  smc_rt60_high = RT60_AMB_H;
        5'd1:  smc_rt60_high = RT60_HALL_H;
        5'd2:  smc_rt60_high = RT60_CAT_H;
        5'd3:  smc_rt60_high = RT60_CHR_H;
        5'd4:  smc_rt60_high = RT60_SR_H;
        5'd5:  smc_rt60_high = RT60_MR_H;
        5'd6:  smc_rt60_high = RT60_LR_H;
        5'd7:  smc_rt60_high = RT60_PLC_H;
        5'd8:  smc_rt60_high = RT60_PLB_H;
        5'd9:  smc_rt60_high = RT60_PLD_H;
        5'd10: smc_rt60_high = RT60_RNG_H;
        5'd11: smc_rt60_high = RT60_CHR_H;
        5'd12: smc_rt60_high = RT60_LR_H;
        5'd13: smc_rt60_high = RT60_RNG_H;
        5'd14: smc_rt60_high = RT60_MR_H;
        5'd15: smc_rt60_high = RT60_CHR_H;
        5'd16: smc_rt60_high = RT60_GAT_H;
        5'd17: smc_rt60_high = RT60_CHR_H;
        5'd18: smc_rt60_high = RT60_RNG_H;
        5'd19: smc_rt60_high = RT60_SHM_H;
        5'd20: smc_rt60_high = RT60_CAT_H;
        5'd21: smc_rt60_high = RT60_SHM_H;
        5'd22: smc_rt60_high = RT60_SHM_H;
        5'd23: smc_rt60_high = RT60_HALL_H;
        default: smc_rt60_high = RT60_HALL_H;
        endcase
    end
endfunction

function [15:0] smc_er_density;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  smc_er_density = ER_MED;
        5'd1:  smc_er_density = ER_HIGH;
        5'd2:  smc_er_density = ER_LOW;
        5'd3:  smc_er_density = ER_MED;
        5'd4:  smc_er_density = ER_HIGH;
        5'd5:  smc_er_density = ER_HIGH;
        5'd6:  smc_er_density = ER_MED;
        5'd7:  smc_er_density = ER_DENSE;
        5'd8:  smc_er_density = ER_DENSE;
        5'd9:  smc_er_density = ER_DENSE;
        5'd10: smc_er_density = ER_MED;
        5'd11: smc_er_density = ER_MED;
        5'd12: smc_er_density = ER_MED;
        5'd13: smc_er_density = ER_LOW;
        5'd14: smc_er_density = ER_MED;
        5'd15: smc_er_density = ER_MED;
        5'd16: smc_er_density = ER_HIGH;
        5'd17: smc_er_density = ER_MED;
        5'd18: smc_er_density = ER_MED;
        5'd19: smc_er_density = ER_LOW;
        5'd20: smc_er_density = ER_SPARSE;
        5'd21: smc_er_density = ER_LOW;
        5'd22: smc_er_density = ER_LOW;
        5'd23: smc_er_density = ER_MED;
        default: smc_er_density = ER_MED;
        endcase
    end
endfunction

function [15:0] smc_er_spread;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  smc_er_spread = SP_MED;
        5'd1:  smc_er_spread = SP_WIDE;
        5'd2:  smc_er_spread = SP_VWIDE;
        5'd3:  smc_er_spread = SP_WIDE;
        5'd4:  smc_er_spread = SP_NARROW;
        5'd5:  smc_er_spread = SP_MED;
        5'd6:  smc_er_spread = SP_WIDE;
        5'd7:  smc_er_spread = SP_NARROW;
        5'd8:  smc_er_spread = SP_NARROW;
        5'd9:  smc_er_spread = SP_MED;
        5'd10: smc_er_spread = SP_MED;
        5'd11: smc_er_spread = SP_MED;
        5'd12: smc_er_spread = SP_MED;
        5'd13: smc_er_spread = SP_NARROW;
        5'd14: smc_er_spread = SP_WIDE;
        5'd15: smc_er_spread = SP_WIDE;
        5'd16: smc_er_spread = SP_MED;
        5'd17: smc_er_spread = SP_MED;
        5'd18: smc_er_spread = SP_MED;
        5'd19: smc_er_spread = SP_WIDE;
        5'd20: smc_er_spread = SP_VWIDE;
        5'd21: smc_er_spread = SP_WIDE;
        5'd22: smc_er_spread = SP_WIDE;
        5'd23: smc_er_spread = SP_WIDE;
        default: smc_er_spread = SP_MED;
        endcase
    end
endfunction

function [31:0] smc_lpf;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  smc_lpf = SPEC_LIGHT;
        5'd1:  smc_lpf = SPEC_MID;
        5'd2:  smc_lpf = SPEC_VDARK;
        5'd3:  smc_lpf = SPEC_DARK;
        5'd4:  smc_lpf = SPEC_LIGHT;
        5'd5:  smc_lpf = SPEC_LIGHT;
        5'd6:  smc_lpf = SPEC_MID;
        5'd7:  smc_lpf = SPEC_BRIGHT;
        5'd8:  smc_lpf = SPEC_VBRIGHT;
        5'd9:  smc_lpf = SPEC_DARK;
        5'd10: smc_lpf = SPEC_MID;
        5'd11: smc_lpf = SPEC_MID;
        5'd12: smc_lpf = SPEC_MID;
        5'd13: smc_lpf = SPEC_SPRING;
        5'd14: smc_lpf = SPEC_MID;
        5'd15: smc_lpf = SPEC_DARK;
        5'd16: smc_lpf = SPEC_LIGHT;
        5'd17: smc_lpf = SPEC_DARK;
        5'd18: smc_lpf = SPEC_BRIGHT;
        5'd19: smc_lpf = SPEC_LIGHT;
        5'd20: smc_lpf = SPEC_VDARK;
        5'd21: smc_lpf = SPEC_LIGHT;
        5'd22: smc_lpf = SPEC_DARK;
        5'd23: smc_lpf = SPEC_MID;
        default: smc_lpf = SPEC_MID;
        endcase
    end
endfunction

function [15:0] smc_diff_pre;
    input [4:0] sid;
    begin
        case (sid)
        5'd4:  smc_diff_pre = DIFF_NORMAL;  // SMALL_ROOM
        5'd5:  smc_diff_pre = DIFF_NORMAL;  // MEDIUM_ROOM
        5'd7:  smc_diff_pre = DIFF_VDENSE;  // PLATE_CLASSIC
        5'd8:  smc_diff_pre = DIFF_VDENSE;  // PLATE_BRIGHT
        5'd9:  smc_diff_pre = DIFF_VDENSE;  // PLATE_DARK
        5'd13: smc_diff_pre = DIFF_VDENSE;  // RINGING
        5'd16: smc_diff_pre = DIFF_NORMAL;  // GATED
        5'd18: smc_diff_pre = DIFF_VDENSE;  // BROKEN
        default: smc_diff_pre = DIFF_DENSE;
        endcase
    end
endfunction

function [15:0] smc_diff_post;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  smc_diff_post = DIFF_NORMAL;  // AMBIENCE
        5'd4:  smc_diff_post = DIFF_NORMAL;  // SMALL_ROOM
        5'd5:  smc_diff_post = DIFF_NORMAL;  // MEDIUM_ROOM
        5'd7:  smc_diff_post = DIFF_VDENSE;  // PLATE_CLASSIC
        5'd8:  smc_diff_post = DIFF_VDENSE;  // PLATE_BRIGHT
        5'd10: smc_diff_post = DIFF_NORMAL;  // RESONANCE
        5'd16: smc_diff_post = DIFF_NORMAL;  // GATED
        default: smc_diff_post = DIFF_DENSE;
        endcase
    end
endfunction

// ============================================================================
//  [v9] Family ID decode (space_id → acoustic family)
//  family 0: Room   (small/med/large room)
//  family 1: Hall   (hall/cathedral/church)
//  family 2: Plate  (classic/bright/dark plate)
//  family 3: Tonal  (resonance/ringing/tonal)
//  family 4: Motion (rotating/drifting/ascending/descending)
//  family 5: Special(shimmer/frozen/broken/reverse)
//  family 6: Gate   (gated/ambience)
//  family 7: Custom
// ============================================================================
function [7:0] smc_family_id;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  smc_family_id = 8'd6;  // AMBIENCE → gate family
        5'd1:  smc_family_id = 8'd1;  // HALL
        5'd2:  smc_family_id = 8'd1;  // CATHEDRAL
        5'd3:  smc_family_id = 8'd1;  // CHURCH
        5'd4:  smc_family_id = 8'd0;  // SMALL_ROOM
        5'd5:  smc_family_id = 8'd0;  // MEDIUM_ROOM
        5'd6:  smc_family_id = 8'd0;  // LARGE_ROOM
        5'd7:  smc_family_id = 8'd2;  // PLATE_CLASSIC
        5'd8:  smc_family_id = 8'd2;  // PLATE_BRIGHT
        5'd9:  smc_family_id = 8'd2;  // PLATE_DARK
        5'd10: smc_family_id = 8'd3;  // RESONANCE
        5'd11: smc_family_id = 8'd3;  // RESONANT_SP
        5'd12: smc_family_id = 8'd3;  // TONAL
        5'd13: smc_family_id = 8'd3;  // RINGING
        5'd14: smc_family_id = 8'd4;  // ROTATING
        5'd15: smc_family_id = 8'd4;  // DRIFTING
        5'd16: smc_family_id = 8'd6;  // GATED
        5'd17: smc_family_id = 8'd5;  // REVERSE
        5'd18: smc_family_id = 8'd5;  // BROKEN
        5'd19: smc_family_id = 8'd5;  // SHIMMER
        5'd20: smc_family_id = 8'd5;  // FROZEN
        5'd21: smc_family_id = 8'd4;  // ASCENDING
        5'd22: smc_family_id = 8'd4;  // DESCENDING
        5'd23: smc_family_id = 8'd7;  // SPECIAL
        default: smc_family_id = 8'd7;
        endcase
    end
endfunction

// [v9] behavior seed: deterministic hash(space_id, family_id)
// shift-add only, LUT 없음 (synth 8-6859 방지)
function [7:0] smc_behavior_seed;
    input [4:0] sid;
    input [7:0] fid;
    begin
        smc_behavior_seed = (sid[4:0] ^ fid[4:0] ^ (fid[7:3])) +
                            (sid << 2) + fid[2:0];
    end
endfunction

function [31:0] smc_inject_gain;
    input [4:0] sid;
    begin
        case (sid)
        5'd0:  smc_inject_gain = 32'h00280000;  // AMBIENCE
        5'd1:  smc_inject_gain = 32'h00300000;  // HALL
        5'd2:  smc_inject_gain = 32'h00200000;  // CATHEDRAL
        5'd3:  smc_inject_gain = 32'h00280000;  // CHURCH
        5'd4:  smc_inject_gain = 32'h00300000;  // SMALL_ROOM
        5'd5:  smc_inject_gain = 32'h00300000;  // MEDIUM_ROOM
        5'd6:  smc_inject_gain = 32'h00280000;  // LARGE_ROOM
        5'd7:  smc_inject_gain = 32'h00100000;  // PLATE_CLASSIC
        5'd8:  smc_inject_gain = 32'h00080000;  // PLATE_BRIGHT
        5'd9:  smc_inject_gain = 32'h00180000;  // PLATE_DARK
        5'd10: smc_inject_gain = 32'h00200000;  // RESONANCE
        5'd11: smc_inject_gain = 32'h00200000;  // RESONANT_SP
        5'd12: smc_inject_gain = 32'h00200000;  // TONAL
        5'd13: smc_inject_gain = 32'h00180000;  // RINGING
        5'd14: smc_inject_gain = 32'h00200000;  // ROTATING
        5'd15: smc_inject_gain = 32'h00200000;  // DRIFTING
        5'd16: smc_inject_gain = 32'h00280000;  // GATED
        5'd17: smc_inject_gain = 32'h00200000;  // REVERSE
        5'd18: smc_inject_gain = 32'h00200000;  // BROKEN
        5'd19: smc_inject_gain = 32'h00200000;  // SHIMMER
        5'd20: smc_inject_gain = 32'h00180000;  // FROZEN
        5'd21: smc_inject_gain = 32'h00200000;  // ASCENDING
        5'd22: smc_inject_gain = 32'h00200000;  // DESCENDING
        5'd23: smc_inject_gain = 32'h00200000;  // SPECIAL
        default: smc_inject_gain = 32'h00200000;
        endcase
    end
endfunction

// ============================================================================
//  조합 선택 (custom vs preset)
// ============================================================================
wire is_custom = (space_id == SP_CUSTOM) || !preset_en;

wire [31:0] w_rt60_low  = is_custom ? rt60_low_custom  : smc_rt60_low(space_id);
wire [31:0] w_rt60_mid  = is_custom ? rt60_mid_custom  : smc_rt60_mid(space_id);
wire [31:0] w_rt60_high = is_custom ? rt60_high_custom : smc_rt60_high(space_id);
wire [15:0] w_er_den    = is_custom ? 16'h0080         : smc_er_density(space_id);
wire [15:0] w_er_spr    = is_custom ? 16'h0070         : smc_er_spread(space_id);
wire [31:0] w_lpf       = is_custom ? lpf_alpha_custom : smc_lpf(space_id);
wire [15:0] w_dpre      = is_custom ? pre_diff_custom  : smc_diff_pre(space_id);
wire [15:0] w_dpost     = is_custom ? post_diff_custom : smc_diff_post(space_id);
wire [31:0] w_inj       = smc_inject_gain(space_id);   // custom도 preset 값 사용
wire [7:0]  w_family    = smc_family_id(space_id);
wire [7:0]  w_seed      = smc_behavior_seed(space_id, w_family);

// ============================================================================
//  출력 레지스터 (1-cycle 지연, latch 방지)
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        rt60_low_out    <= 32'h00E14000;  // HALL_M default
        rt60_mid_out    <= 32'h00E14000;
        rt60_high_out   <= 32'h00C7AE14;
        er_density_out  <= 16'h00C0;      // ER_HIGH
        er_spread_out   <= 16'h00C0;      // SP_WIDE
        lpf_alpha_out   <= 32'h006AAAAA;  // SPEC_MID
        diff_pre_out    <= 16'h4000;      // DIFF_DENSE
        diff_post_out   <= 16'h4000;
        cluster_thr_out <= 16'h0100;
        inject_gain_out <= 32'h00300000;
        family_id_out   <= 8'd1;   // HALL family default
        behavior_seed_out <= 8'h00;
    end else begin
        rt60_low_out    <= w_rt60_low;
        rt60_mid_out    <= w_rt60_mid;
        rt60_high_out   <= w_rt60_high;
        er_density_out  <= w_er_den;
        er_spread_out   <= w_er_spr;
        lpf_alpha_out   <= w_lpf;
        diff_pre_out    <= w_dpre;
        diff_post_out   <= w_dpost;
        cluster_thr_out <= density_to_thr(w_er_den);
        inject_gain_out <= w_inj;
        family_id_out   <= w_family;
        behavior_seed_out <= w_seed;
    end
end

endmodule