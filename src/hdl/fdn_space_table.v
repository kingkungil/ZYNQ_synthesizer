// ============================================================================
//  fdn_space_table.v  v9
//
//  v7 → v8: STATIC ONLY (P0 아키텍처 정정)
//
//  [TBL-v7-1]  Dynamic overlay 완전 제거
//    energy_l / energy_r → delay/gain/lpf modulation ❌ 제거
//    er_density → parameter modulation ❌ 제거
//    → 테이블 = ROM (읽기 전용 물리 상수 DB)
//
//  [TBL-v7-2]  출력 = BRAM 직접 읽기 (또는 crossfade 블렌딩)
//    delay_out / gain_out / lpf_alpha_out / diff_out / mod_depth_out
//    → 모두 테이블 값 그대로 (runtime modulation 없음)
//
//  [TBL-v7-3]  인터페이스 변경
//    energy_l / energy_r / er_density 포트 제거
//    dynamic overlay 관련 파라미터 제거
//    나머지 포트 v6 완전 호환
//
//  [TBL-v7-4]  24개 공간 물리 RT60 (ms) - v6 테이블 값 유지:
//    AMBIENCE=800, HALL=2200, CATHEDRAL=5000, CHURCH=3000
//    SMALL_ROOM=400, MEDIUM_ROOM=800, LARGE_ROOM=1400
//    PLATE_CLASSIC=1800, PLATE_BRIGHT=1400, PLATE_DARK=2200
//    RESONANCE=3500, RESONANT_SP=3000, TONAL=2500
//    RINGING=4500, ROTATING=2000, DRIFTING=3500
//    GATED=600, REVERSE=3000, BROKEN=2000
//    SHIMMER=4000, FROZEN=8000, ASCENDING=3800
//    DESCENDING=3200, SPECIAL=2500
//
//  합성 안전: dynamic clamp 함수 제거로 LUT 절감
//  인터페이스: energy_l/r/er_density 포트 제거 (top 연결 수정 필요)
// ============================================================================

`timescale 1ns / 1ps

module fdn_space_table #(
    parameter integer DL_SLOT_BITS = 10,
    parameter integer NUM_LINES    = 16,
    parameter integer NUM_SPACES   = 24,
    parameter integer FADE_STEPS   = 512
)(
    input  wire                        clk,
    input  wire                        rst_n,

    input  wire [4:0]                  space_id,
    input  wire                        space_change,
    input  wire [3:0]                  line_idx,

    // [v9] behavior engine scale 입력 (behavior_engine → table "색칠")
    // table 구조 변경 없음; base × scale 오버레이
    // synth 8-6858: 리셋값은 중립(gain=1x, lpf=1x, diff=1x)
    input  wire [15:0]                 behavior_gain_scale,  // Q1.15 (0x8000=1.0)
    input  wire [15:0]                 behavior_lpf_scale,   // Q1.15
    input  wire [7:0]                  behavior_diff_scale,  // Q1.7  (0x80=1.0)

    output reg  [DL_SLOT_BITS-1:0]    delay_out,
    output reg  [15:0]                 gain_out,
    output reg  [15:0]                 lpf_alpha_out,
    output reg  [7:0]                  diff_out,
    output reg  [7:0]                  mod_depth_out,
    output wire                        crossfade_done
);

// ============================================================================
//  로컬 파라미터
// ============================================================================
localparam FADE_BITS  = 10;
localparam FADE_SHIFT = 9;
localparam ENTRIES    = NUM_SPACES * NUM_LINES;

// ============================================================================
//  BRAM
// ============================================================================
(* ram_style = "block" *) reg [DL_SLOT_BITS-1:0] tbl_delay [0:ENTRIES-1];
(* ram_style = "block" *) reg [15:0]              tbl_gain  [0:ENTRIES-1];
(* ram_style = "block" *) reg [15:0]              tbl_lpf   [0:ENTRIES-1];
(* ram_style = "block" *) reg [7:0]               tbl_diff  [0:ENTRIES-1];
(* ram_style = "block" *) reg [7:0]               tbl_mod   [0:ENTRIES-1];

// ============================================================================
//  Crossfade FSM 상태 레지스터
//  [v8-FIX1] cur_space_r 추가: old_space_r에 "이전 공간"을 정확히 저장
//  space_change 시점의 space_id는 이미 새 공간 → cur_space_r을 경유해야 함
// ============================================================================
reg [4:0]         old_space_r;
reg [4:0]         cur_space_r;   // [v8-FIX1] 현재 재생 중인 공간 ID
reg [FADE_BITS:0] fade_cnt;
reg               fading_r;

// ============================================================================
//  주소
// ============================================================================
wire [8:0] addr_cur = {cur_space_r,   line_idx[3:0]};
wire [8:0] addr_old = {old_space_r,   line_idx[3:0]};

// ============================================================================
//  BRAM 동기 읽기 - Stage 1 (clk+1)
// ============================================================================
reg [DL_SLOT_BITS-1:0] rd_cur_dly,  rd_old_dly;
reg [15:0]              rd_cur_gain, rd_old_gain;
reg [15:0]              rd_cur_lpf,  rd_old_lpf;
reg [7:0]               rd_cur_diff, rd_old_diff;
reg [7:0]               rd_cur_mod,  rd_old_mod;

always @(posedge clk) begin
    rd_cur_dly  <= tbl_delay[addr_cur];
    rd_old_dly  <= tbl_delay[addr_old];
    rd_cur_gain <= tbl_gain [addr_cur];
    rd_old_gain <= tbl_gain [addr_old];
    rd_cur_lpf  <= tbl_lpf  [addr_cur];
    rd_old_lpf  <= tbl_lpf  [addr_old];
    rd_cur_diff <= tbl_diff [addr_cur];
    rd_old_diff <= tbl_diff [addr_old];
    rd_cur_mod  <= tbl_mod  [addr_cur];
    rd_old_mod  <= tbl_mod  [addr_old];
end

// ============================================================================
//  Crossfade FSM
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        old_space_r <= 5'd0;           // [v8-FIX1] reset to space 0
        cur_space_r <= 5'd0;           // [v8-FIX1]
        fade_cnt    <= {(FADE_BITS+1){1'b0}};
        fading_r    <= 1'b0;
    end else begin
        if (space_change) begin
            old_space_r <= cur_space_r;        // [v8-FIX1] 이전 공간 = 현재 재생 중이던 공간
            cur_space_r <= space_id[4:0];      // [v8-FIX1] 현재 공간 = 새 space_id
            fade_cnt    <= {(FADE_BITS+1){1'b0}};
            fading_r    <= 1'b1;
        end else if (fading_r) begin
            if (fade_cnt < FADE_STEPS)
                fade_cnt <= fade_cnt + 1'b1;
            else
                fading_r <= 1'b0;
        end
    end
end

assign crossfade_done = !fading_r;

// ============================================================================
//  Crossfade 블렌딩 - combinational
//  [FIX-9] bsum_dly: DL_SLOT_BITS+FADE_BITS+1 = 21비트
// ============================================================================
wire [FADE_BITS:0] alpha     = (fade_cnt > FADE_STEPS) ?
                               FADE_STEPS[FADE_BITS:0] : fade_cnt;
wire [FADE_BITS:0] inv_alpha = FADE_STEPS[FADE_BITS:0] - alpha;

wire [DL_SLOT_BITS+FADE_BITS:0] bsum_dly =           // [FIX-9] 21비트
    rd_old_dly * inv_alpha + rd_cur_dly * alpha;

wire [15+FADE_BITS+1:0] bsum_gain =
    rd_old_gain * inv_alpha + rd_cur_gain * alpha;

wire [15+FADE_BITS+1:0] bsum_lpf =
    rd_old_lpf * inv_alpha + rd_cur_lpf * alpha;

wire [7+FADE_BITS+1:0] bsum_diff =
    rd_old_diff * inv_alpha + rd_cur_diff * alpha;

wire [7+FADE_BITS+1:0] bsum_mod =
    rd_old_mod * inv_alpha + rd_cur_mod * alpha;

// crossfade 결과 (blended base)
wire [DL_SLOT_BITS-1:0] base_dly  = fading_r ?
    bsum_dly [DL_SLOT_BITS+FADE_SHIFT-1:FADE_SHIFT] : rd_cur_dly;
wire [15:0] base_gain = fading_r ?
    bsum_gain[15+FADE_SHIFT:FADE_SHIFT]              : rd_cur_gain;
wire [15:0] base_lpf  = fading_r ?
    bsum_lpf [15+FADE_SHIFT:FADE_SHIFT]              : rd_cur_lpf;
wire [7:0]  base_diff = fading_r ?
    bsum_diff[7+FADE_SHIFT:FADE_SHIFT]               : rd_cur_diff;
wire [7:0]  base_mod  = fading_r ?
    bsum_mod [7+FADE_SHIFT:FADE_SHIFT]               : rd_cur_mod;

// ============================================================================
//  [v9] Stage 2 출력 레지스터 - behavior scale 오버레이 적용
//  base × behavior_scale → final output
//  delay는 behavior 영향 없음 (timing explosion 방지)
//  HDL 9-1206: 출력 포트 전부 driven
//  synth 8-6859: 전용 always 블록 단일 드라이버
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        delay_out     <= 10'd503;
        gain_out      <= 16'h7800;
        lpf_alpha_out <= 16'h2800;
        diff_out      <= 8'd160;
        mod_depth_out <= 8'd18;
    end else begin
        // delay: 고정 (behavior 변조 금지 - timing critical path)
        delay_out     <= base_dly;

        // gain: base_gain × behavior_gain_scale / 0x8000
        // 상한 clamp: 0x7F00 (발산 방지)
        begin : gain_scale_blk
            reg [31:0] gain_scaled;
            gain_scaled = (base_gain * behavior_gain_scale) >> 15;
            gain_out <= (gain_scaled > 16'h7F00) ? 16'h7F00 :
                        (gain_scaled < 16'h1000) ? 16'h1000 :
                        gain_scaled[15:0];
        end

        // lpf: base_lpf × behavior_lpf_scale / 0x8000
        // clamp: [0x0400, 0x7F00]
        begin : lpf_scale_blk
            reg [31:0] lpf_scaled;
            lpf_scaled = (base_lpf * behavior_lpf_scale) >> 15;
            lpf_alpha_out <= (lpf_scaled > 16'h7F00) ? 16'h7F00 :
                             (lpf_scaled < 16'h0400) ? 16'h0400 :
                             lpf_scaled[15:0];
        end

        // diff: base_diff × behavior_diff_scale / 0x80
        // clamp: [0x10, 0xFF]
        begin : diff_scale_blk
            reg [15:0] diff_scaled;
            diff_scaled = (base_diff * behavior_diff_scale) >> 7;
            diff_out <= (diff_scaled > 16'h00FF) ? 8'hFF :
                        (diff_scaled < 16'h0010) ? 8'h10 :
                        diff_scaled[7:0];
        end

        // mod: base_mod + behavior_diff_scale 일부 (>> 4) → motion 계열 생동감
        // [v8-FIX2] behavior_diff_scale Q1.7 (0x80=1.0) → >>4 = 0~15 기여
        // clamp: [0, 255]
        begin : mod_scale_blk
            reg [8:0] mod_scaled;
            mod_scaled = {1'b0, base_mod} + {5'd0, behavior_diff_scale[7:4]};
            mod_depth_out <= (mod_scaled > 9'd255) ? 8'd255 : mod_scaled[7:0];
        end
    end
end
// ============================================================================
//  BRAM 초기값 - v8 Acoustic Identity Redesign
//
//  delay topology family 완전 분리:
//    HALL      (space 0,1,3)    : 503~1571, 균일 분산, smooth tail
//    CATHEDRAL (space 2,20)     : 911~4273, 넓은 modal spacing, huge bloom
//    ROOM      (space 4,5,6)    : 211~659,  단밀 short, bright
//    PLATE     (space 7,8,9,16) : 149~491,  금속 밀집, high diff
//    RESONANT  (space 10-13)    : near-harmonic 배수, tonal identity
//    MOTION    (space 14,15,21,22): 431~1601, hall+strong mod
//    SPECIAL   (space 17-19,23) : 비대칭, chaos, effect-driven
// ============================================================================
integer ii;

initial begin
    for (ii = 0; ii < ENTRIES; ii = ii + 1) begin
        tbl_delay[ii] = 10'd503;
        tbl_gain [ii] = 16'h7800;
        tbl_lpf  [ii] = 16'h2800;
        tbl_diff [ii] = 8'd160;
        tbl_mod  [ii] = 8'd18;
    end

    // =====================================================================
    //  SP_AMBIENCE (0) - RT60=800ms
    // =====================================================================
    tbl_delay[0*16+ 0]= 503; tbl_gain[0*16+ 0]=16'h73FF; tbl_lpf[0*16+ 0]=16'h2800; tbl_diff[0*16+ 0]=158; tbl_mod[0*16+ 0]= 14;
    tbl_delay[0*16+ 1]= 597; tbl_gain[0*16+ 1]=16'h687B; tbl_lpf[0*16+ 1]=16'h2900; tbl_diff[0*16+ 1]=156; tbl_mod[0*16+ 1]= 16;
    tbl_delay[0*16+ 2]= 701; tbl_gain[0*16+ 2]=16'h5F85; tbl_lpf[0*16+ 2]=16'h2A00; tbl_diff[0*16+ 2]=154; tbl_mod[0*16+ 2]= 18;
    tbl_delay[0*16+ 3]= 817; tbl_gain[0*16+ 3]=16'h58E2; tbl_lpf[0*16+ 3]=16'h2C00; tbl_diff[0*16+ 3]=152; tbl_mod[0*16+ 3]= 20;
    tbl_delay[0*16+ 4]= 947; tbl_gain[0*16+ 4]=16'h5454; tbl_lpf[0*16+ 4]=16'h2E00; tbl_diff[0*16+ 4]=150; tbl_mod[0*16+ 4]= 14;
    tbl_delay[0*16+ 5]=1091; tbl_gain[0*16+ 5]=16'h5192; tbl_lpf[0*16+ 5]=16'h3000; tbl_diff[0*16+ 5]=148; tbl_mod[0*16+ 5]= 16;
    tbl_delay[0*16+ 6]=1249; tbl_gain[0*16+ 6]=16'h5047; tbl_lpf[0*16+ 6]=16'h3200; tbl_diff[0*16+ 6]=146; tbl_mod[0*16+ 6]= 18;
    tbl_delay[0*16+ 7]=1423; tbl_gain[0*16+ 7]=16'h5000; tbl_lpf[0*16+ 7]=16'h3400; tbl_diff[0*16+ 7]=144; tbl_mod[0*16+ 7]= 20;
    tbl_delay[0*16+ 8]= 547; tbl_gain[0*16+ 8]=16'h71D9; tbl_lpf[0*16+ 8]=16'h3600; tbl_diff[0*16+ 8]=142; tbl_mod[0*16+ 8]= 14;
    tbl_delay[0*16+ 9]= 641; tbl_gain[0*16+ 9]=16'h65BE; tbl_lpf[0*16+ 9]=16'h3800; tbl_diff[0*16+ 9]=140; tbl_mod[0*16+ 9]= 16;
    tbl_delay[0*16+10]= 751; tbl_gain[0*16+10]=16'h5C51; tbl_lpf[0*16+10]=16'h3A00; tbl_diff[0*16+10]=138; tbl_mod[0*16+10]= 18;
    tbl_delay[0*16+11]= 871; tbl_gain[0*16+11]=16'h5557; tbl_lpf[0*16+11]=16'h3C00; tbl_diff[0*16+11]=136; tbl_mod[0*16+11]= 20;
    tbl_delay[0*16+12]=1007; tbl_gain[0*16+12]=16'h508D; tbl_lpf[0*16+12]=16'h3E00; tbl_diff[0*16+12]=134; tbl_mod[0*16+12]= 14;
    tbl_delay[0*16+13]=1153; tbl_gain[0*16+13]=16'h4DA6; tbl_lpf[0*16+13]=16'h4000; tbl_diff[0*16+13]=132; tbl_mod[0*16+13]= 16;
    tbl_delay[0*16+14]=1321; tbl_gain[0*16+14]=16'h4C4A; tbl_lpf[0*16+14]=16'h4200; tbl_diff[0*16+14]=130; tbl_mod[0*16+14]= 18;
    tbl_delay[0*16+15]=1499; tbl_gain[0*16+15]=16'h4C00; tbl_lpf[0*16+15]=16'h4400; tbl_diff[0*16+15]=128; tbl_mod[0*16+15]= 20;

    // =====================================================================
    //  SP_HALL (1) - RT60=2200ms
    // =====================================================================
    tbl_delay[1*16+ 0]= 503; tbl_gain[1*16+ 0]=16'h7B7F; tbl_lpf[1*16+ 0]=16'h2400; tbl_diff[1*16+ 0]=172; tbl_mod[1*16+ 0]= 10;
    tbl_delay[1*16+ 1]= 653; tbl_gain[1*16+ 1]=16'h7B58; tbl_lpf[1*16+ 1]=16'h2500; tbl_diff[1*16+ 1]=170; tbl_mod[1*16+ 1]= 12;
    tbl_delay[1*16+ 2]= 839; tbl_gain[1*16+ 2]=16'h7AA5; tbl_lpf[1*16+ 2]=16'h2600; tbl_diff[1*16+ 2]=168; tbl_mod[1*16+ 2]= 14;
    tbl_delay[1*16+ 3]=1049; tbl_gain[1*16+ 3]=16'h7926; tbl_lpf[1*16+ 3]=16'h2700; tbl_diff[1*16+ 3]=166; tbl_mod[1*16+ 3]= 16;
    tbl_delay[1*16+ 4]=1289; tbl_gain[1*16+ 4]=16'h76AF; tbl_lpf[1*16+ 4]=16'h2800; tbl_diff[1*16+ 4]=164; tbl_mod[1*16+ 4]= 10;
    tbl_delay[1*16+ 5]=1561; tbl_gain[1*16+ 5]=16'h7316; tbl_lpf[1*16+ 5]=16'h2900; tbl_diff[1*16+ 5]=162; tbl_mod[1*16+ 5]= 12;
    tbl_delay[1*16+ 6]=1867; tbl_gain[1*16+ 6]=16'h6E3C; tbl_lpf[1*16+ 6]=16'h2A00; tbl_diff[1*16+ 6]=160; tbl_mod[1*16+ 6]= 14;
    tbl_delay[1*16+ 7]=2203; tbl_gain[1*16+ 7]=16'h6800; tbl_lpf[1*16+ 7]=16'h2B00; tbl_diff[1*16+ 7]=158; tbl_mod[1*16+ 7]= 16;
    tbl_delay[1*16+ 8]= 547; tbl_gain[1*16+ 8]=16'h79E1; tbl_lpf[1*16+ 8]=16'h2C00; tbl_diff[1*16+ 8]=156; tbl_mod[1*16+ 8]= 10;
    tbl_delay[1*16+ 9]= 709; tbl_gain[1*16+ 9]=16'h79B9; tbl_lpf[1*16+ 9]=16'h2D00; tbl_diff[1*16+ 9]=154; tbl_mod[1*16+ 9]= 12;
    tbl_delay[1*16+10]= 907; tbl_gain[1*16+10]=16'h7902; tbl_lpf[1*16+10]=16'h2E00; tbl_diff[1*16+10]=152; tbl_mod[1*16+10]= 14;
    tbl_delay[1*16+11]=1129; tbl_gain[1*16+11]=16'h777D; tbl_lpf[1*16+11]=16'h2F00; tbl_diff[1*16+11]=150; tbl_mod[1*16+11]= 16;
    tbl_delay[1*16+12]=1381; tbl_gain[1*16+12]=16'h74F8; tbl_lpf[1*16+12]=16'h3000; tbl_diff[1*16+12]=148; tbl_mod[1*16+12]= 10;
    tbl_delay[1*16+13]=1667; tbl_gain[1*16+13]=16'h714E; tbl_lpf[1*16+13]=16'h3100; tbl_diff[1*16+13]=146; tbl_mod[1*16+13]= 12;
    tbl_delay[1*16+14]=1993; tbl_gain[1*16+14]=16'h6C5B; tbl_lpf[1*16+14]=16'h3200; tbl_diff[1*16+14]=144; tbl_mod[1*16+14]= 14;
    tbl_delay[1*16+15]=2351; tbl_gain[1*16+15]=16'h6600; tbl_lpf[1*16+15]=16'h3400; tbl_diff[1*16+15]=142; tbl_mod[1*16+15]= 16;

    // =====================================================================
    //  SP_CATHEDRAL (2) - RT60=5000ms
    // =====================================================================
    tbl_delay[2*16+ 0]= 911; tbl_gain[2*16+ 0]=16'h7C66; tbl_lpf[2*16+ 0]=16'h3800; tbl_diff[2*16+ 0]=200; tbl_mod[2*16+ 0]=  8;
    tbl_delay[2*16+ 1]=1187; tbl_gain[2*16+ 1]=16'h7B54; tbl_lpf[2*16+ 1]=16'h3900; tbl_diff[2*16+ 1]=199; tbl_mod[2*16+ 1]= 10;
    tbl_delay[2*16+ 2]=1499; tbl_gain[2*16+ 2]=16'h7A21; tbl_lpf[2*16+ 2]=16'h3A00; tbl_diff[2*16+ 2]=198; tbl_mod[2*16+ 2]= 10;
    tbl_delay[2*16+ 3]=1861; tbl_gain[2*16+ 3]=16'h78C0; tbl_lpf[2*16+ 3]=16'h3B00; tbl_diff[2*16+ 3]=197; tbl_mod[2*16+ 3]= 12;
    tbl_delay[2*16+ 4]=2293; tbl_gain[2*16+ 4]=16'h7721; tbl_lpf[2*16+ 4]=16'h3C00; tbl_diff[2*16+ 4]=196; tbl_mod[2*16+ 4]=  8;
    tbl_delay[2*16+ 5]=2789; tbl_gain[2*16+ 5]=16'h754A; tbl_lpf[2*16+ 5]=16'h3D00; tbl_diff[2*16+ 5]=195; tbl_mod[2*16+ 5]= 10;
    tbl_delay[2*16+ 6]=3371; tbl_gain[2*16+ 6]=16'h732C; tbl_lpf[2*16+ 6]=16'h3E00; tbl_diff[2*16+ 6]=194; tbl_mod[2*16+ 6]= 10;
    tbl_delay[2*16+ 7]=4027; tbl_gain[2*16+ 7]=16'h70D4; tbl_lpf[2*16+ 7]=16'h3F00; tbl_diff[2*16+ 7]=193; tbl_mod[2*16+ 7]= 12;
    tbl_delay[2*16+ 8]= 983; tbl_gain[2*16+ 8]=16'h7C1E; tbl_lpf[2*16+ 8]=16'h4200; tbl_diff[2*16+ 8]=192; tbl_mod[2*16+ 8]=  8;
    tbl_delay[2*16+ 9]=1277; tbl_gain[2*16+ 9]=16'h7AFB; tbl_lpf[2*16+ 9]=16'h4300; tbl_diff[2*16+ 9]=191; tbl_mod[2*16+ 9]= 10;
    tbl_delay[2*16+10]=1601; tbl_gain[2*16+10]=16'h79BD; tbl_lpf[2*16+10]=16'h4400; tbl_diff[2*16+10]=190; tbl_mod[2*16+10]= 10;
    tbl_delay[2*16+11]=1999; tbl_gain[2*16+11]=16'h783B; tbl_lpf[2*16+11]=16'h4500; tbl_diff[2*16+11]=189; tbl_mod[2*16+11]= 12;
    tbl_delay[2*16+12]=2441; tbl_gain[2*16+12]=16'h7694; tbl_lpf[2*16+12]=16'h4600; tbl_diff[2*16+12]=188; tbl_mod[2*16+12]=  8;
    tbl_delay[2*16+13]=2971; tbl_gain[2*16+13]=16'h74A0; tbl_lpf[2*16+13]=16'h4600; tbl_diff[2*16+13]=187; tbl_mod[2*16+13]= 10;
    tbl_delay[2*16+14]=3581; tbl_gain[2*16+14]=16'h726B; tbl_lpf[2*16+14]=16'h4700; tbl_diff[2*16+14]=186; tbl_mod[2*16+14]= 10;
    tbl_delay[2*16+15]=4273; tbl_gain[2*16+15]=16'h6FF6; tbl_lpf[2*16+15]=16'h4800; tbl_diff[2*16+15]=185; tbl_mod[2*16+15]= 12;

    // =====================================================================
    //  SP_CHURCH (3) - RT60=3000ms
    // =====================================================================
    tbl_delay[3*16+ 0]= 503; tbl_gain[3*16+ 0]=16'h7CAF; tbl_lpf[3*16+ 0]=16'h2400; tbl_diff[3*16+ 0]=188; tbl_mod[3*16+ 0]= 14;
    tbl_delay[3*16+ 1]= 887; tbl_gain[3*16+ 1]=16'h7C8E; tbl_lpf[3*16+ 1]=16'h2500; tbl_diff[3*16+ 1]=187; tbl_mod[3*16+ 1]= 16;
    tbl_delay[3*16+ 2]=1327; tbl_gain[3*16+ 2]=16'h7BF4; tbl_lpf[3*16+ 2]=16'h2600; tbl_diff[3*16+ 2]=186; tbl_mod[3*16+ 2]= 18;
    tbl_delay[3*16+ 3]=1871; tbl_gain[3*16+ 3]=16'h7AAD; tbl_lpf[3*16+ 3]=16'h2700; tbl_diff[3*16+ 3]=185; tbl_mod[3*16+ 3]= 20;
    tbl_delay[3*16+ 4]=2557; tbl_gain[3*16+ 4]=16'h7890; tbl_lpf[3*16+ 4]=16'h2800; tbl_diff[3*16+ 4]=184; tbl_mod[3*16+ 4]= 14;
    tbl_delay[3*16+ 5]=3191; tbl_gain[3*16+ 5]=16'h757D; tbl_lpf[3*16+ 5]=16'h2900; tbl_diff[3*16+ 5]=183; tbl_mod[3*16+ 5]= 16;
    tbl_delay[3*16+ 6]=3881; tbl_gain[3*16+ 6]=16'h7155; tbl_lpf[3*16+ 6]=16'h2A00; tbl_diff[3*16+ 6]=182; tbl_mod[3*16+ 6]= 18;
    tbl_delay[3*16+ 7]=4679; tbl_gain[3*16+ 7]=16'h6C00; tbl_lpf[3*16+ 7]=16'h2B00; tbl_diff[3*16+ 7]=181; tbl_mod[3*16+ 7]= 20;
    tbl_delay[3*16+ 8]= 547; tbl_gain[3*16+ 8]=16'h7B27; tbl_lpf[3*16+ 8]=16'h2C00; tbl_diff[3*16+ 8]=180; tbl_mod[3*16+ 8]= 14;
    tbl_delay[3*16+ 9]= 947; tbl_gain[3*16+ 9]=16'h7B05; tbl_lpf[3*16+ 9]=16'h2D00; tbl_diff[3*16+ 9]=179; tbl_mod[3*16+ 9]= 16;
    tbl_delay[3*16+10]=1409; tbl_gain[3*16+10]=16'h7A67; tbl_lpf[3*16+10]=16'h2E00; tbl_diff[3*16+10]=178; tbl_mod[3*16+10]= 18;
    tbl_delay[3*16+11]=1997; tbl_gain[3*16+11]=16'h7917; tbl_lpf[3*16+11]=16'h2F00; tbl_diff[3*16+11]=177; tbl_mod[3*16+11]= 20;
    tbl_delay[3*16+12]=2713; tbl_gain[3*16+12]=16'h76EB; tbl_lpf[3*16+12]=16'h3000; tbl_diff[3*16+12]=176; tbl_mod[3*16+12]= 14;
    tbl_delay[3*16+13]=3389; tbl_gain[3*16+13]=16'h73C1; tbl_lpf[3*16+13]=16'h3100; tbl_diff[3*16+13]=175; tbl_mod[3*16+13]= 16;
    tbl_delay[3*16+14]=4127; tbl_gain[3*16+14]=16'h6F7C; tbl_lpf[3*16+14]=16'h3200; tbl_diff[3*16+14]=174; tbl_mod[3*16+14]= 18;
    tbl_delay[3*16+15]=4951; tbl_gain[3*16+15]=16'h6A00; tbl_lpf[3*16+15]=16'h3400; tbl_diff[3*16+15]=172; tbl_mod[3*16+15]= 20;

    // =====================================================================
    //  SP_SMALL_ROOM (4) - RT60=400ms
    // =====================================================================
    tbl_delay[4*16+ 0]= 157; tbl_gain[4*16+ 0]=16'h7200; tbl_lpf[4*16+ 0]=16'h2200; tbl_diff[4*16+ 0]= 82; tbl_mod[4*16+ 0]=  0;
    tbl_delay[4*16+ 1]= 211; tbl_gain[4*16+ 1]=16'h60BB; tbl_lpf[4*16+ 1]=16'h2200; tbl_diff[4*16+ 1]= 80; tbl_mod[4*16+ 1]=  2;
    tbl_delay[4*16+ 2]= 263; tbl_gain[4*16+ 2]=16'h5348; tbl_lpf[4*16+ 2]=16'h2300; tbl_diff[4*16+ 2]= 78; tbl_mod[4*16+ 2]=  3;
    tbl_delay[4*16+ 3]= 331; tbl_gain[4*16+ 3]=16'h4954; tbl_lpf[4*16+ 3]=16'h2300; tbl_diff[4*16+ 3]= 76; tbl_mod[4*16+ 3]=  4;
    tbl_delay[4*16+ 4]= 389; tbl_gain[4*16+ 4]=16'h427E; tbl_lpf[4*16+ 4]=16'h2400; tbl_diff[4*16+ 4]= 74; tbl_mod[4*16+ 4]=  0;
    tbl_delay[4*16+ 5]= 431; tbl_gain[4*16+ 5]=16'h3E5B; tbl_lpf[4*16+ 5]=16'h2400; tbl_diff[4*16+ 5]= 72; tbl_mod[4*16+ 5]=  2;
    tbl_delay[4*16+ 6]= 503; tbl_gain[4*16+ 6]=16'h3C6A; tbl_lpf[4*16+ 6]=16'h2500; tbl_diff[4*16+ 6]= 70; tbl_mod[4*16+ 6]=  3;
    tbl_delay[4*16+ 7]= 557; tbl_gain[4*16+ 7]=16'h3C00; tbl_lpf[4*16+ 7]=16'h2500; tbl_diff[4*16+ 7]= 68; tbl_mod[4*16+ 7]=  4;
    tbl_delay[4*16+ 8]= 179; tbl_gain[4*16+ 8]=16'h6F00; tbl_lpf[4*16+ 8]=16'h2600; tbl_diff[4*16+ 8]= 66; tbl_mod[4*16+ 8]=  0;
    tbl_delay[4*16+ 9]= 229; tbl_gain[4*16+ 9]=16'h5D69; tbl_lpf[4*16+ 9]=16'h2600; tbl_diff[4*16+ 9]= 64; tbl_mod[4*16+ 9]=  2;
    tbl_delay[4*16+10]= 283; tbl_gain[4*16+10]=16'h4FB7; tbl_lpf[4*16+10]=16'h2700; tbl_diff[4*16+10]= 62; tbl_mod[4*16+10]=  3;
    tbl_delay[4*16+11]= 353; tbl_gain[4*16+11]=16'h4593; tbl_lpf[4*16+11]=16'h2700; tbl_diff[4*16+11]= 60; tbl_mod[4*16+11]=  4;
    tbl_delay[4*16+12]= 409; tbl_gain[4*16+12]=16'h3E9D; tbl_lpf[4*16+12]=16'h2800; tbl_diff[4*16+12]= 58; tbl_mod[4*16+12]=  0;
    tbl_delay[4*16+13]= 449; tbl_gain[4*16+13]=16'h3A66; tbl_lpf[4*16+13]=16'h2800; tbl_diff[4*16+13]= 56; tbl_mod[4*16+13]=  2;
    tbl_delay[4*16+14]= 521; tbl_gain[4*16+14]=16'h386C; tbl_lpf[4*16+14]=16'h2900; tbl_diff[4*16+14]= 54; tbl_mod[4*16+14]=  3;
    tbl_delay[4*16+15]= 577; tbl_gain[4*16+15]=16'h3800; tbl_lpf[4*16+15]=16'h2A00; tbl_diff[4*16+15]= 52; tbl_mod[4*16+15]=  4;

    // =====================================================================
    //  SP_MEDIUM_ROOM (5) - RT60=800ms
    // =====================================================================
    tbl_delay[5*16+ 0]= 211; tbl_gain[5*16+ 0]=16'h7723; tbl_lpf[5*16+ 0]=16'h1E00; tbl_diff[5*16+ 0]=108; tbl_mod[5*16+ 0]=  4;
    tbl_delay[5*16+ 1]= 271; tbl_gain[5*16+ 1]=16'h71D4; tbl_lpf[5*16+ 1]=16'h1E00; tbl_diff[5*16+ 1]=106; tbl_mod[5*16+ 1]=  6;
    tbl_delay[5*16+ 2]= 347; tbl_gain[5*16+ 2]=16'h6C86; tbl_lpf[5*16+ 2]=16'h1F00; tbl_diff[5*16+ 2]=104; tbl_mod[5*16+ 2]=  7;
    tbl_delay[5*16+ 3]= 433; tbl_gain[5*16+ 3]=16'h6738; tbl_lpf[5*16+ 3]=16'h2000; tbl_diff[5*16+ 3]=102; tbl_mod[5*16+ 3]=  8;
    tbl_delay[5*16+ 4]= 523; tbl_gain[5*16+ 4]=16'h61EA; tbl_lpf[5*16+ 4]=16'h2100; tbl_diff[5*16+ 4]=100; tbl_mod[5*16+ 4]=  4;
    tbl_delay[5*16+ 5]= 619; tbl_gain[5*16+ 5]=16'h5C9C; tbl_lpf[5*16+ 5]=16'h2200; tbl_diff[5*16+ 5]= 98; tbl_mod[5*16+ 5]=  6;
    tbl_delay[5*16+ 6]= 727; tbl_gain[5*16+ 6]=16'h574E; tbl_lpf[5*16+ 6]=16'h2300; tbl_diff[5*16+ 6]= 96; tbl_mod[5*16+ 6]=  7;
    tbl_delay[5*16+ 7]= 851; tbl_gain[5*16+ 7]=16'h5200; tbl_lpf[5*16+ 7]=16'h2400; tbl_diff[5*16+ 7]= 94; tbl_mod[5*16+ 7]=  8;
    tbl_delay[5*16+ 8]= 229; tbl_gain[5*16+ 8]=16'h7445; tbl_lpf[5*16+ 8]=16'h2500; tbl_diff[5*16+ 8]= 92; tbl_mod[5*16+ 8]=  4;
    tbl_delay[5*16+ 9]= 293; tbl_gain[5*16+ 9]=16'h6EF2; tbl_lpf[5*16+ 9]=16'h2600; tbl_diff[5*16+ 9]= 90; tbl_mod[5*16+ 9]=  6;
    tbl_delay[5*16+10]= 373; tbl_gain[5*16+10]=16'h699F; tbl_lpf[5*16+10]=16'h2700; tbl_diff[5*16+10]= 88; tbl_mod[5*16+10]=  7;
    tbl_delay[5*16+11]= 463; tbl_gain[5*16+11]=16'h644C; tbl_lpf[5*16+11]=16'h2800; tbl_diff[5*16+11]= 86; tbl_mod[5*16+11]=  8;
    tbl_delay[5*16+12]= 557; tbl_gain[5*16+12]=16'h5EF9; tbl_lpf[5*16+12]=16'h2900; tbl_diff[5*16+12]= 84; tbl_mod[5*16+12]=  4;
    tbl_delay[5*16+13]= 659; tbl_gain[5*16+13]=16'h59A6; tbl_lpf[5*16+13]=16'h2A00; tbl_diff[5*16+13]= 82; tbl_mod[5*16+13]=  6;
    tbl_delay[5*16+14]= 773; tbl_gain[5*16+14]=16'h5453; tbl_lpf[5*16+14]=16'h2B00; tbl_diff[5*16+14]= 80; tbl_mod[5*16+14]=  7;
    tbl_delay[5*16+15]= 907; tbl_gain[5*16+15]=16'h4F00; tbl_lpf[5*16+15]=16'h2C00; tbl_diff[5*16+15]= 78; tbl_mod[5*16+15]=  8;

    // =====================================================================
    //  SP_LARGE_ROOM (6) - RT60=1400ms
    // =====================================================================
    tbl_delay[6*16+ 0]= 211; tbl_gain[6*16+ 0]=16'h7943; tbl_lpf[6*16+ 0]=16'h2200; tbl_diff[6*16+ 0]=125; tbl_mod[6*16+ 0]=  8;
    tbl_delay[6*16+ 1]= 283; tbl_gain[6*16+ 1]=16'h7909; tbl_lpf[6*16+ 1]=16'h2300; tbl_diff[6*16+ 1]=123; tbl_mod[6*16+ 1]= 10;
    tbl_delay[6*16+ 2]= 379; tbl_gain[6*16+ 2]=16'h77FC; tbl_lpf[6*16+ 2]=16'h2400; tbl_diff[6*16+ 2]=121; tbl_mod[6*16+ 2]= 11;
    tbl_delay[6*16+ 3]= 499; tbl_gain[6*16+ 3]=16'h75BE; tbl_lpf[6*16+ 3]=16'h2500; tbl_diff[6*16+ 3]=119; tbl_mod[6*16+ 3]= 12;
    tbl_delay[6*16+ 4]= 641; tbl_gain[6*16+ 4]=16'h7209; tbl_lpf[6*16+ 4]=16'h2600; tbl_diff[6*16+ 4]=117; tbl_mod[6*16+ 4]=  8;
    tbl_delay[6*16+ 5]= 809; tbl_gain[6*16+ 5]=16'h6CA4; tbl_lpf[6*16+ 5]=16'h2700; tbl_diff[6*16+ 5]=115; tbl_mod[6*16+ 5]= 10;
    tbl_delay[6*16+ 6]=1009; tbl_gain[6*16+ 6]=16'h655B; tbl_lpf[6*16+ 6]=16'h2800; tbl_diff[6*16+ 6]=113; tbl_mod[6*16+ 6]= 11;
    tbl_delay[6*16+ 7]=1249; tbl_gain[6*16+ 7]=16'h5C00; tbl_lpf[6*16+ 7]=16'h2900; tbl_diff[6*16+ 7]=111; tbl_mod[6*16+ 7]= 12;
    tbl_delay[6*16+ 8]= 229; tbl_gain[6*16+ 8]=16'h7686; tbl_lpf[6*16+ 8]=16'h2A00; tbl_diff[6*16+ 8]=109; tbl_mod[6*16+ 8]=  8;
    tbl_delay[6*16+ 9]= 307; tbl_gain[6*16+ 9]=16'h764B; tbl_lpf[6*16+ 9]=16'h2B00; tbl_diff[6*16+ 9]=107; tbl_mod[6*16+ 9]= 10;
    tbl_delay[6*16+10]= 409; tbl_gain[6*16+10]=16'h753C; tbl_lpf[6*16+10]=16'h2C00; tbl_diff[6*16+10]=105; tbl_mod[6*16+10]= 11;
    tbl_delay[6*16+11]= 539; tbl_gain[6*16+11]=16'h72F9; tbl_lpf[6*16+11]=16'h2D00; tbl_diff[6*16+11]=103; tbl_mod[6*16+11]= 12;
    tbl_delay[6*16+12]= 691; tbl_gain[6*16+12]=16'h6F3C; tbl_lpf[6*16+12]=16'h2E00; tbl_diff[6*16+12]=101; tbl_mod[6*16+12]=  8;
    tbl_delay[6*16+13]= 877; tbl_gain[6*16+13]=16'h69CA; tbl_lpf[6*16+13]=16'h2F00; tbl_diff[6*16+13]= 99; tbl_mod[6*16+13]= 10;
    tbl_delay[6*16+14]=1093; tbl_gain[6*16+14]=16'h6271; tbl_lpf[6*16+14]=16'h3000; tbl_diff[6*16+14]= 97; tbl_mod[6*16+14]= 11;
    tbl_delay[6*16+15]=1361; tbl_gain[6*16+15]=16'h5900; tbl_lpf[6*16+15]=16'h3200; tbl_diff[6*16+15]= 95; tbl_mod[6*16+15]= 12;

    // =====================================================================
    //  SP_PLATE_CLASSIC (7) - RT60=1800ms
    // =====================================================================
    tbl_delay[7*16+ 0]= 149; tbl_gain[7*16+ 0]=16'h7E5A; tbl_lpf[7*16+ 0]=16'h3000; tbl_diff[7*16+ 0]=245; tbl_mod[7*16+ 0]=  3;
    tbl_delay[7*16+ 1]= 179; tbl_gain[7*16+ 1]=16'h7E25; tbl_lpf[7*16+ 1]=16'h3200; tbl_diff[7*16+ 1]=244; tbl_mod[7*16+ 1]=  4;
    tbl_delay[7*16+ 2]= 223; tbl_gain[7*16+ 2]=16'h7D33; tbl_lpf[7*16+ 2]=16'h3400; tbl_diff[7*16+ 2]=243; tbl_mod[7*16+ 2]=  5;
    tbl_delay[7*16+ 3]= 269; tbl_gain[7*16+ 3]=16'h7B2E; tbl_lpf[7*16+ 3]=16'h3600; tbl_diff[7*16+ 3]=242; tbl_mod[7*16+ 3]=  6;
    tbl_delay[7*16+ 4]= 313; tbl_gain[7*16+ 4]=16'h77D8; tbl_lpf[7*16+ 4]=16'h3800; tbl_diff[7*16+ 4]=241; tbl_mod[7*16+ 4]=  3;
    tbl_delay[7*16+ 5]= 367; tbl_gain[7*16+ 5]=16'h72FD; tbl_lpf[7*16+ 5]=16'h3A00; tbl_diff[7*16+ 5]=240; tbl_mod[7*16+ 5]=  4;
    tbl_delay[7*16+ 6]= 419; tbl_gain[7*16+ 6]=16'h6C6D; tbl_lpf[7*16+ 6]=16'h3C00; tbl_diff[7*16+ 6]=240; tbl_mod[7*16+ 6]=  5;
    tbl_delay[7*16+ 7]= 463; tbl_gain[7*16+ 7]=16'h6400; tbl_lpf[7*16+ 7]=16'h3E00; tbl_diff[7*16+ 7]=239; tbl_mod[7*16+ 7]=  6;
    tbl_delay[7*16+ 8]= 163; tbl_gain[7*16+ 8]=16'h7E32; tbl_lpf[7*16+ 8]=16'h4000; tbl_diff[7*16+ 8]=238; tbl_mod[7*16+ 8]=  3;
    tbl_delay[7*16+ 9]= 197; tbl_gain[7*16+ 9]=16'h7DFA; tbl_lpf[7*16+ 9]=16'h4200; tbl_diff[7*16+ 9]=237; tbl_mod[7*16+ 9]=  4;
    tbl_delay[7*16+10]= 239; tbl_gain[7*16+10]=16'h7CF7; tbl_lpf[7*16+10]=16'h4400; tbl_diff[7*16+10]=236; tbl_mod[7*16+10]=  5;
    tbl_delay[7*16+11]= 283; tbl_gain[7*16+11]=16'h7ACE; tbl_lpf[7*16+11]=16'h4600; tbl_diff[7*16+11]=235; tbl_mod[7*16+11]=  6;
    tbl_delay[7*16+12]= 331; tbl_gain[7*16+12]=16'h773C; tbl_lpf[7*16+12]=16'h4800; tbl_diff[7*16+12]=234; tbl_mod[7*16+12]=  3;
    tbl_delay[7*16+13]= 389; tbl_gain[7*16+13]=16'h7209; tbl_lpf[7*16+13]=16'h4A00; tbl_diff[7*16+13]=233; tbl_mod[7*16+13]=  4;
    tbl_delay[7*16+14]= 443; tbl_gain[7*16+14]=16'h6B04; tbl_lpf[7*16+14]=16'h4C00; tbl_diff[7*16+14]=232; tbl_mod[7*16+14]=  5;
    tbl_delay[7*16+15]= 491; tbl_gain[7*16+15]=16'h6200; tbl_lpf[7*16+15]=16'h4E00; tbl_diff[7*16+15]=231; tbl_mod[7*16+15]=  6;

    // =====================================================================
    //  SP_PLATE_BRIGHT (8) - RT60=1400ms
    // =====================================================================
    tbl_delay[8*16+ 0]= 149; tbl_gain[8*16+ 0]=16'h7DE2; tbl_lpf[8*16+ 0]=16'h4800; tbl_diff[8*16+ 0]=252; tbl_mod[8*16+ 0]=  0;
    tbl_delay[8*16+ 1]= 179; tbl_gain[8*16+ 1]=16'h7DAA; tbl_lpf[8*16+ 1]=16'h4A00; tbl_diff[8*16+ 1]=251; tbl_mod[8*16+ 1]=  1;
    tbl_delay[8*16+ 2]= 223; tbl_gain[8*16+ 2]=16'h7CAA; tbl_lpf[8*16+ 2]=16'h4C00; tbl_diff[8*16+ 2]=250; tbl_mod[8*16+ 2]=  2;
    tbl_delay[8*16+ 3]= 269; tbl_gain[8*16+ 3]=16'h7A87; tbl_lpf[8*16+ 3]=16'h4E00; tbl_diff[8*16+ 3]=249; tbl_mod[8*16+ 3]=  3;
    tbl_delay[8*16+ 4]= 313; tbl_gain[8*16+ 4]=16'h7700; tbl_lpf[8*16+ 4]=16'h5000; tbl_diff[8*16+ 4]=248; tbl_mod[8*16+ 4]=  0;
    tbl_delay[8*16+ 5]= 367; tbl_gain[8*16+ 5]=16'h71DC; tbl_lpf[8*16+ 5]=16'h5200; tbl_diff[8*16+ 5]=247; tbl_mod[8*16+ 5]=  1;
    tbl_delay[8*16+ 6]= 419; tbl_gain[8*16+ 6]=16'h6AEA; tbl_lpf[8*16+ 6]=16'h5400; tbl_diff[8*16+ 6]=247; tbl_mod[8*16+ 6]=  2;
    tbl_delay[8*16+ 7]= 463; tbl_gain[8*16+ 7]=16'h6200; tbl_lpf[8*16+ 7]=16'h5600; tbl_diff[8*16+ 7]=246; tbl_mod[8*16+ 7]=  3;
    tbl_delay[8*16+ 8]= 163; tbl_gain[8*16+ 8]=16'h7DB0; tbl_lpf[8*16+ 8]=16'h5800; tbl_diff[8*16+ 8]=245; tbl_mod[8*16+ 8]=  0;
    tbl_delay[8*16+ 9]= 197; tbl_gain[8*16+ 9]=16'h7D75; tbl_lpf[8*16+ 9]=16'h5A00; tbl_diff[8*16+ 9]=244; tbl_mod[8*16+ 9]=  1;
    tbl_delay[8*16+10]= 239; tbl_gain[8*16+10]=16'h7C64; tbl_lpf[8*16+10]=16'h5C00; tbl_diff[8*16+10]=243; tbl_mod[8*16+10]=  2;
    tbl_delay[8*16+11]= 283; tbl_gain[8*16+11]=16'h7A1E; tbl_lpf[8*16+11]=16'h5E00; tbl_diff[8*16+11]=242; tbl_mod[8*16+11]=  3;
    tbl_delay[8*16+12]= 331; tbl_gain[8*16+12]=16'h765C; tbl_lpf[8*16+12]=16'h6000; tbl_diff[8*16+12]=241; tbl_mod[8*16+12]=  0;
    tbl_delay[8*16+13]= 389; tbl_gain[8*16+13]=16'h70E2; tbl_lpf[8*16+13]=16'h6200; tbl_diff[8*16+13]=240; tbl_mod[8*16+13]=  1;
    tbl_delay[8*16+14]= 443; tbl_gain[8*16+14]=16'h697E; tbl_lpf[8*16+14]=16'h6400; tbl_diff[8*16+14]=240; tbl_mod[8*16+14]=  2;
    tbl_delay[8*16+15]= 491; tbl_gain[8*16+15]=16'h6000; tbl_lpf[8*16+15]=16'h6600; tbl_diff[8*16+15]=239; tbl_mod[8*16+15]=  3;

    // =====================================================================
    //  SP_PLATE_DARK (9) - RT60=2200ms
    // =====================================================================
    tbl_delay[9*16+ 0]= 149; tbl_gain[9*16+ 0]=16'h7EA6; tbl_lpf[9*16+ 0]=16'h0800; tbl_diff[9*16+ 0]=218; tbl_mod[9*16+ 0]=  6;
    tbl_delay[9*16+ 1]= 179; tbl_gain[9*16+ 1]=16'h7E79; tbl_lpf[9*16+ 1]=16'h0900; tbl_diff[9*16+ 1]=217; tbl_mod[9*16+ 1]=  8;
    tbl_delay[9*16+ 2]= 223; tbl_gain[9*16+ 2]=16'h7DA9; tbl_lpf[9*16+ 2]=16'h0A00; tbl_diff[9*16+ 2]=216; tbl_mod[9*16+ 2]=  9;
    tbl_delay[9*16+ 3]= 269; tbl_gain[9*16+ 3]=16'h7BEC; tbl_lpf[9*16+ 3]=16'h0B00; tbl_diff[9*16+ 3]=215; tbl_mod[9*16+ 3]= 10;
    tbl_delay[9*16+ 4]= 313; tbl_gain[9*16+ 4]=16'h790E; tbl_lpf[9*16+ 4]=16'h0C00; tbl_diff[9*16+ 4]=214; tbl_mod[9*16+ 4]=  6;
    tbl_delay[9*16+ 5]= 367; tbl_gain[9*16+ 5]=16'h74E1; tbl_lpf[9*16+ 5]=16'h0D00; tbl_diff[9*16+ 5]=213; tbl_mod[9*16+ 5]=  8;
    tbl_delay[9*16+ 6]= 419; tbl_gain[9*16+ 6]=16'h6F3E; tbl_lpf[9*16+ 6]=16'h0E00; tbl_diff[9*16+ 6]=212; tbl_mod[9*16+ 6]=  9;
    tbl_delay[9*16+ 7]= 463; tbl_gain[9*16+ 7]=16'h6800; tbl_lpf[9*16+ 7]=16'h0F00; tbl_diff[9*16+ 7]=211; tbl_mod[9*16+ 7]= 10;
    tbl_delay[9*16+ 8]= 163; tbl_gain[9*16+ 8]=16'h7E86; tbl_lpf[9*16+ 8]=16'h1000; tbl_diff[9*16+ 8]=210; tbl_mod[9*16+ 8]=  6;
    tbl_delay[9*16+ 9]= 197; tbl_gain[9*16+ 9]=16'h7E55; tbl_lpf[9*16+ 9]=16'h1100; tbl_diff[9*16+ 9]=209; tbl_mod[9*16+ 9]=  8;
    tbl_delay[9*16+10]= 239; tbl_gain[9*16+10]=16'h7D74; tbl_lpf[9*16+10]=16'h1200; tbl_diff[9*16+10]=208; tbl_mod[9*16+10]=  9;
    tbl_delay[9*16+11]= 283; tbl_gain[9*16+11]=16'h7B93; tbl_lpf[9*16+11]=16'h1300; tbl_diff[9*16+11]=207; tbl_mod[9*16+11]= 10;
    tbl_delay[9*16+12]= 331; tbl_gain[9*16+12]=16'h7878; tbl_lpf[9*16+12]=16'h1400; tbl_diff[9*16+12]=206; tbl_mod[9*16+12]=  6;
    tbl_delay[9*16+13]= 389; tbl_gain[9*16+13]=16'h73F2; tbl_lpf[9*16+13]=16'h1500; tbl_diff[9*16+13]=205; tbl_mod[9*16+13]=  8;
    tbl_delay[9*16+14]= 443; tbl_gain[9*16+14]=16'h6DD7; tbl_lpf[9*16+14]=16'h1600; tbl_diff[9*16+14]=204; tbl_mod[9*16+14]=  9;
    tbl_delay[9*16+15]= 491; tbl_gain[9*16+15]=16'h6600; tbl_lpf[9*16+15]=16'h1800; tbl_diff[9*16+15]=203; tbl_mod[9*16+15]= 10;

    // =====================================================================
    //  SP_RESONANCE (10) - RT60=3500ms
    // =====================================================================
    tbl_delay[10*16+ 0]= 257; tbl_gain[10*16+ 0]=16'h7E89; tbl_lpf[10*16+ 0]=16'h2800; tbl_diff[10*16+ 0]=100; tbl_mod[10*16+ 0]= 24;
    tbl_delay[10*16+ 1]= 385; tbl_gain[10*16+ 1]=16'h7DD0; tbl_lpf[10*16+ 1]=16'h2900; tbl_diff[10*16+ 1]= 98; tbl_mod[10*16+ 1]= 28;
    tbl_delay[10*16+ 2]= 577; tbl_gain[10*16+ 2]=16'h7CBD; tbl_lpf[10*16+ 2]=16'h2A00; tbl_diff[10*16+ 2]= 96; tbl_mod[10*16+ 2]= 32;
    tbl_delay[10*16+ 3]= 769; tbl_gain[10*16+ 3]=16'h7BAB; tbl_lpf[10*16+ 3]=16'h2B00; tbl_diff[10*16+ 3]= 94; tbl_mod[10*16+ 3]= 36;
    tbl_delay[10*16+ 4]=1153; tbl_gain[10*16+ 4]=16'h7990; tbl_lpf[10*16+ 4]=16'h2C00; tbl_diff[10*16+ 4]= 92; tbl_mod[10*16+ 4]= 24;
    tbl_delay[10*16+ 5]=1537; tbl_gain[10*16+ 5]=16'h777E; tbl_lpf[10*16+ 5]=16'h2D00; tbl_diff[10*16+ 5]= 90; tbl_mod[10*16+ 5]= 28;
    tbl_delay[10*16+ 6]=2305; tbl_gain[10*16+ 6]=16'h7374; tbl_lpf[10*16+ 6]=16'h2E00; tbl_diff[10*16+ 6]= 88; tbl_mod[10*16+ 6]= 32;
    tbl_delay[10*16+ 7]=3073; tbl_gain[10*16+ 7]=16'h6F8D; tbl_lpf[10*16+ 7]=16'h2F00; tbl_diff[10*16+ 7]= 86; tbl_mod[10*16+ 7]= 36;
    tbl_delay[10*16+ 8]= 289; tbl_gain[10*16+ 8]=16'h7E5B; tbl_lpf[10*16+ 8]=16'h3000; tbl_diff[10*16+ 8]= 84; tbl_mod[10*16+ 8]= 24;
    tbl_delay[10*16+ 9]= 433; tbl_gain[10*16+ 9]=16'h7D8B; tbl_lpf[10*16+ 9]=16'h3100; tbl_diff[10*16+ 9]= 82; tbl_mod[10*16+ 9]= 28;
    tbl_delay[10*16+10]= 647; tbl_gain[10*16+10]=16'h7C59; tbl_lpf[10*16+10]=16'h3200; tbl_diff[10*16+10]= 80; tbl_mod[10*16+10]= 32;
    tbl_delay[10*16+11]= 863; tbl_gain[10*16+11]=16'h7B27; tbl_lpf[10*16+11]=16'h3300; tbl_diff[10*16+11]= 78; tbl_mod[10*16+11]= 36;
    tbl_delay[10*16+12]=1297; tbl_gain[10*16+12]=16'h78C8; tbl_lpf[10*16+12]=16'h3400; tbl_diff[10*16+12]= 76; tbl_mod[10*16+12]= 24;
    tbl_delay[10*16+13]=1729; tbl_gain[10*16+13]=16'h7678; tbl_lpf[10*16+13]=16'h3500; tbl_diff[10*16+13]= 74; tbl_mod[10*16+13]= 28;
    tbl_delay[10*16+14]=2593; tbl_gain[10*16+14]=16'h71FA; tbl_lpf[10*16+14]=16'h3600; tbl_diff[10*16+14]= 72; tbl_mod[10*16+14]= 32;
    tbl_delay[10*16+15]=3457; tbl_gain[10*16+15]=16'h6DA7; tbl_lpf[10*16+15]=16'h3800; tbl_diff[10*16+15]= 70; tbl_mod[10*16+15]= 36;

    // =====================================================================
    //  SP_RESONANT_SP (11) - RT60=3000ms
    // =====================================================================
    tbl_delay[11*16+ 0]= 257; tbl_gain[11*16+ 0]=16'h7E4B; tbl_lpf[11*16+ 0]=16'h2600; tbl_diff[11*16+ 0]= 90; tbl_mod[11*16+ 0]= 20;
    tbl_delay[11*16+ 1]= 385; tbl_gain[11*16+ 1]=16'h7D74; tbl_lpf[11*16+ 1]=16'h2700; tbl_diff[11*16+ 1]= 88; tbl_mod[11*16+ 1]= 24;
    tbl_delay[11*16+ 2]= 577; tbl_gain[11*16+ 2]=16'h7C33; tbl_lpf[11*16+ 2]=16'h2800; tbl_diff[11*16+ 2]= 86; tbl_mod[11*16+ 2]= 28;
    tbl_delay[11*16+ 3]= 769; tbl_gain[11*16+ 3]=16'h7AF6; tbl_lpf[11*16+ 3]=16'h2900; tbl_diff[11*16+ 3]= 84; tbl_mod[11*16+ 3]= 32;
    tbl_delay[11*16+ 4]=1153; tbl_gain[11*16+ 4]=16'h7885; tbl_lpf[11*16+ 4]=16'h2A00; tbl_diff[11*16+ 4]= 83; tbl_mod[11*16+ 4]= 20;
    tbl_delay[11*16+ 5]=1537; tbl_gain[11*16+ 5]=16'h7621; tbl_lpf[11*16+ 5]=16'h2B00; tbl_diff[11*16+ 5]= 81; tbl_mod[11*16+ 5]= 24;
    tbl_delay[11*16+ 6]=2305; tbl_gain[11*16+ 6]=16'h717C; tbl_lpf[11*16+ 6]=16'h2C00; tbl_diff[11*16+ 6]= 79; tbl_mod[11*16+ 6]= 28;
    tbl_delay[11*16+ 7]=3073; tbl_gain[11*16+ 7]=16'h6D06; tbl_lpf[11*16+ 7]=16'h2D00; tbl_diff[11*16+ 7]= 77; tbl_mod[11*16+ 7]= 32;
    tbl_delay[11*16+ 8]= 289; tbl_gain[11*16+ 8]=16'h7E15; tbl_lpf[11*16+ 8]=16'h2E00; tbl_diff[11*16+ 8]= 75; tbl_mod[11*16+ 8]= 20;
    tbl_delay[11*16+ 9]= 433; tbl_gain[11*16+ 9]=16'h7D23; tbl_lpf[11*16+ 9]=16'h2F00; tbl_diff[11*16+ 9]= 73; tbl_mod[11*16+ 9]= 24;
    tbl_delay[11*16+10]= 647; tbl_gain[11*16+10]=16'h7BBF; tbl_lpf[11*16+10]=16'h3000; tbl_diff[11*16+10]= 71; tbl_mod[11*16+10]= 28;
    tbl_delay[11*16+11]= 863; tbl_gain[11*16+11]=16'h7A5C; tbl_lpf[11*16+11]=16'h3100; tbl_diff[11*16+11]= 69; tbl_mod[11*16+11]= 32;
    tbl_delay[11*16+12]=1297; tbl_gain[11*16+12]=16'h779E; tbl_lpf[11*16+12]=16'h3200; tbl_diff[11*16+12]= 68; tbl_mod[11*16+12]= 20;
    tbl_delay[11*16+13]=1729; tbl_gain[11*16+13]=16'h74F3; tbl_lpf[11*16+13]=16'h3300; tbl_diff[11*16+13]= 66; tbl_mod[11*16+13]= 24;
    tbl_delay[11*16+14]=2593; tbl_gain[11*16+14]=16'h6FCB; tbl_lpf[11*16+14]=16'h3400; tbl_diff[11*16+14]= 64; tbl_mod[11*16+14]= 28;
    tbl_delay[11*16+15]=3457; tbl_gain[11*16+15]=16'h6ADC; tbl_lpf[11*16+15]=16'h3600; tbl_diff[11*16+15]= 62; tbl_mod[11*16+15]= 32;

    // =====================================================================
    //  SP_TONAL (12) - RT60=2500ms
    // =====================================================================
    tbl_delay[12*16+ 0]= 257; tbl_gain[12*16+ 0]=16'h7DF5; tbl_lpf[12*16+ 0]=16'h2A00; tbl_diff[12*16+ 0]= 80; tbl_mod[12*16+ 0]= 16;
    tbl_delay[12*16+ 1]= 385; tbl_gain[12*16+ 1]=16'h7CF3; tbl_lpf[12*16+ 1]=16'h2B00; tbl_diff[12*16+ 1]= 78; tbl_mod[12*16+ 1]= 20;
    tbl_delay[12*16+ 2]= 577; tbl_gain[12*16+ 2]=16'h7B74; tbl_lpf[12*16+ 2]=16'h2C00; tbl_diff[12*16+ 2]= 77; tbl_mod[12*16+ 2]= 22;
    tbl_delay[12*16+ 3]= 769; tbl_gain[12*16+ 3]=16'h79FB; tbl_lpf[12*16+ 3]=16'h2D00; tbl_diff[12*16+ 3]= 75; tbl_mod[12*16+ 3]= 26;
    tbl_delay[12*16+ 4]=1153; tbl_gain[12*16+ 4]=16'h7714; tbl_lpf[12*16+ 4]=16'h2E00; tbl_diff[12*16+ 4]= 73; tbl_mod[12*16+ 4]= 16;
    tbl_delay[12*16+ 5]=1537; tbl_gain[12*16+ 5]=16'h743F; tbl_lpf[12*16+ 5]=16'h2F00; tbl_diff[12*16+ 5]= 72; tbl_mod[12*16+ 5]= 20;
    tbl_delay[12*16+ 6]=2305; tbl_gain[12*16+ 6]=16'h6EC9; tbl_lpf[12*16+ 6]=16'h3000; tbl_diff[12*16+ 6]= 70; tbl_mod[12*16+ 6]= 22;
    tbl_delay[12*16+ 7]=3073; tbl_gain[12*16+ 7]=16'h6995; tbl_lpf[12*16+ 7]=16'h3100; tbl_diff[12*16+ 7]= 68; tbl_mod[12*16+ 7]= 26;
    tbl_delay[12*16+ 8]= 289; tbl_gain[12*16+ 8]=16'h7DB4; tbl_lpf[12*16+ 8]=16'h3200; tbl_diff[12*16+ 8]= 67; tbl_mod[12*16+ 8]= 16;
    tbl_delay[12*16+ 9]= 433; tbl_gain[12*16+ 9]=16'h7C93; tbl_lpf[12*16+ 9]=16'h3300; tbl_diff[12*16+ 9]= 65; tbl_mod[12*16+ 9]= 20;
    tbl_delay[12*16+10]= 647; tbl_gain[12*16+10]=16'h7AEA; tbl_lpf[12*16+10]=16'h3400; tbl_diff[12*16+10]= 63; tbl_mod[12*16+10]= 22;
    tbl_delay[12*16+11]= 863; tbl_gain[12*16+11]=16'h7943; tbl_lpf[12*16+11]=16'h3500; tbl_diff[12*16+11]= 62; tbl_mod[12*16+11]= 26;
    tbl_delay[12*16+12]=1297; tbl_gain[12*16+12]=16'h7602; tbl_lpf[12*16+12]=16'h3600; tbl_diff[12*16+12]= 60; tbl_mod[12*16+12]= 16;
    tbl_delay[12*16+13]=1729; tbl_gain[12*16+13]=16'h72DC; tbl_lpf[12*16+13]=16'h3700; tbl_diff[12*16+13]= 58; tbl_mod[12*16+13]= 20;
    tbl_delay[12*16+14]=2593; tbl_gain[12*16+14]=16'h6CCE; tbl_lpf[12*16+14]=16'h3800; tbl_diff[12*16+14]= 57; tbl_mod[12*16+14]= 22;
    tbl_delay[12*16+15]=3457; tbl_gain[12*16+15]=16'h6712; tbl_lpf[12*16+15]=16'h3A00; tbl_diff[12*16+15]= 55; tbl_mod[12*16+15]= 26;

    // =====================================================================
    //  SP_RINGING (13) - RT60=4500ms
    // =====================================================================
    tbl_delay[13*16+ 0]= 257; tbl_gain[13*16+ 0]=16'h7EDC; tbl_lpf[13*16+ 0]=16'h2C00; tbl_diff[13*16+ 0]= 70; tbl_mod[13*16+ 0]= 28;
    tbl_delay[13*16+ 1]= 385; tbl_gain[13*16+ 1]=16'h7E4C; tbl_lpf[13*16+ 1]=16'h2D00; tbl_diff[13*16+ 1]= 69; tbl_mod[13*16+ 1]= 32;
    tbl_delay[13*16+ 2]= 577; tbl_gain[13*16+ 2]=16'h7D74; tbl_lpf[13*16+ 2]=16'h2E00; tbl_diff[13*16+ 2]= 67; tbl_mod[13*16+ 2]= 36;
    tbl_delay[13*16+ 3]= 769; tbl_gain[13*16+ 3]=16'h7C9E; tbl_lpf[13*16+ 3]=16'h2F00; tbl_diff[13*16+ 3]= 66; tbl_mod[13*16+ 3]= 40;
    tbl_delay[13*16+ 4]=1153; tbl_gain[13*16+ 4]=16'h7AF7; tbl_lpf[13*16+ 4]=16'h3000; tbl_diff[13*16+ 4]= 64; tbl_mod[13*16+ 4]= 28;
    tbl_delay[13*16+ 5]=1537; tbl_gain[13*16+ 5]=16'h7955; tbl_lpf[13*16+ 5]=16'h3100; tbl_diff[13*16+ 5]= 63; tbl_mod[13*16+ 5]= 32;
    tbl_delay[13*16+ 6]=2305; tbl_gain[13*16+ 6]=16'h7622; tbl_lpf[13*16+ 6]=16'h3200; tbl_diff[13*16+ 6]= 61; tbl_mod[13*16+ 6]= 36;
    tbl_delay[13*16+ 7]=3073; tbl_gain[13*16+ 7]=16'h7304; tbl_lpf[13*16+ 7]=16'h3300; tbl_diff[13*16+ 7]= 60; tbl_mod[13*16+ 7]= 40;
    tbl_delay[13*16+ 8]= 289; tbl_gain[13*16+ 8]=16'h7EB8; tbl_lpf[13*16+ 8]=16'h3400; tbl_diff[13*16+ 8]= 58; tbl_mod[13*16+ 8]= 28;
    tbl_delay[13*16+ 9]= 433; tbl_gain[13*16+ 9]=16'h7E16; tbl_lpf[13*16+ 9]=16'h3500; tbl_diff[13*16+ 9]= 57; tbl_mod[13*16+ 9]= 32;
    tbl_delay[13*16+10]= 647; tbl_gain[13*16+10]=16'h7D26; tbl_lpf[13*16+10]=16'h3600; tbl_diff[13*16+10]= 55; tbl_mod[13*16+10]= 36;
    tbl_delay[13*16+11]= 863; tbl_gain[13*16+11]=16'h7C36; tbl_lpf[13*16+11]=16'h3700; tbl_diff[13*16+11]= 54; tbl_mod[13*16+11]= 40;
    tbl_delay[13*16+12]=1297; tbl_gain[13*16+12]=16'h7A59; tbl_lpf[13*16+12]=16'h3800; tbl_diff[13*16+12]= 52; tbl_mod[13*16+12]= 28;
    tbl_delay[13*16+13]=1729; tbl_gain[13*16+13]=16'h7886; tbl_lpf[13*16+13]=16'h3900; tbl_diff[13*16+13]= 51; tbl_mod[13*16+13]= 32;
    tbl_delay[13*16+14]=2593; tbl_gain[13*16+14]=16'h74F4; tbl_lpf[13*16+14]=16'h3A00; tbl_diff[13*16+14]= 49; tbl_mod[13*16+14]= 36;
    tbl_delay[13*16+15]=3457; tbl_gain[13*16+15]=16'h717D; tbl_lpf[13*16+15]=16'h3C00; tbl_diff[13*16+15]= 48; tbl_mod[13*16+15]= 40;

    // =====================================================================
    //  SP_ROTATING (14) - RT60=2000ms
    // =====================================================================
    tbl_delay[14*16+ 0]= 431; tbl_gain[14*16+ 0]=16'h7BC0; tbl_lpf[14*16+ 0]=16'h2400; tbl_diff[14*16+ 0]=155; tbl_mod[14*16+ 0]= 52;
    tbl_delay[14*16+ 1]= 563; tbl_gain[14*16+ 1]=16'h78ED; tbl_lpf[14*16+ 1]=16'h2500; tbl_diff[14*16+ 1]=153; tbl_mod[14*16+ 1]= 58;
    tbl_delay[14*16+ 2]= 691; tbl_gain[14*16+ 2]=16'h761B; tbl_lpf[14*16+ 2]=16'h2600; tbl_diff[14*16+ 2]=151; tbl_mod[14*16+ 2]= 64;
    tbl_delay[14*16+ 3]= 821; tbl_gain[14*16+ 3]=16'h7349; tbl_lpf[14*16+ 3]=16'h2700; tbl_diff[14*16+ 3]=149; tbl_mod[14*16+ 3]= 70;
    tbl_delay[14*16+ 4]= 977; tbl_gain[14*16+ 4]=16'h7076; tbl_lpf[14*16+ 4]=16'h2800; tbl_diff[14*16+ 4]=147; tbl_mod[14*16+ 4]= 52;
    tbl_delay[14*16+ 5]=1153; tbl_gain[14*16+ 5]=16'h6DA4; tbl_lpf[14*16+ 5]=16'h2900; tbl_diff[14*16+ 5]=145; tbl_mod[14*16+ 5]= 58;
    tbl_delay[14*16+ 6]=1321; tbl_gain[14*16+ 6]=16'h6AD2; tbl_lpf[14*16+ 6]=16'h2A00; tbl_diff[14*16+ 6]=143; tbl_mod[14*16+ 6]= 64;
    tbl_delay[14*16+ 7]=1511; tbl_gain[14*16+ 7]=16'h6800; tbl_lpf[14*16+ 7]=16'h2B00; tbl_diff[14*16+ 7]=141; tbl_mod[14*16+ 7]= 70;
    tbl_delay[14*16+ 8]= 463; tbl_gain[14*16+ 8]=16'h7A35; tbl_lpf[14*16+ 8]=16'h2C00; tbl_diff[14*16+ 8]=139; tbl_mod[14*16+ 8]= 52;
    tbl_delay[14*16+ 9]= 601; tbl_gain[14*16+ 9]=16'h7752; tbl_lpf[14*16+ 9]=16'h2D00; tbl_diff[14*16+ 9]=137; tbl_mod[14*16+ 9]= 58;
    tbl_delay[14*16+10]= 739; tbl_gain[14*16+10]=16'h746F; tbl_lpf[14*16+10]=16'h2E00; tbl_diff[14*16+10]=135; tbl_mod[14*16+10]= 64;
    tbl_delay[14*16+11]= 883; tbl_gain[14*16+11]=16'h718C; tbl_lpf[14*16+11]=16'h2F00; tbl_diff[14*16+11]=133; tbl_mod[14*16+11]= 70;
    tbl_delay[14*16+12]=1039; tbl_gain[14*16+12]=16'h6EA9; tbl_lpf[14*16+12]=16'h3000; tbl_diff[14*16+12]=131; tbl_mod[14*16+12]= 52;
    tbl_delay[14*16+13]=1223; tbl_gain[14*16+13]=16'h6BC6; tbl_lpf[14*16+13]=16'h3100; tbl_diff[14*16+13]=129; tbl_mod[14*16+13]= 58;
    tbl_delay[14*16+14]=1409; tbl_gain[14*16+14]=16'h68E3; tbl_lpf[14*16+14]=16'h3200; tbl_diff[14*16+14]=127; tbl_mod[14*16+14]= 64;
    tbl_delay[14*16+15]=1601; tbl_gain[14*16+15]=16'h6600; tbl_lpf[14*16+15]=16'h3400; tbl_diff[14*16+15]=125; tbl_mod[14*16+15]= 70;

    // =====================================================================
    //  SP_DRIFTING (15) - RT60=3500ms
    // =====================================================================
    tbl_delay[15*16+ 0]= 431; tbl_gain[15*16+ 0]=16'h7D8E; tbl_lpf[15*16+ 0]=16'h2800; tbl_diff[15*16+ 0]=165; tbl_mod[15*16+ 0]= 42;
    tbl_delay[15*16+ 1]= 563; tbl_gain[15*16+ 1]=16'h7D77; tbl_lpf[15*16+ 1]=16'h2900; tbl_diff[15*16+ 1]=163; tbl_mod[15*16+ 1]= 48;
    tbl_delay[15*16+ 2]= 691; tbl_gain[15*16+ 2]=16'h7D0C; tbl_lpf[15*16+ 2]=16'h2A00; tbl_diff[15*16+ 2]=161; tbl_mod[15*16+ 2]= 54;
    tbl_delay[15*16+ 3]= 821; tbl_gain[15*16+ 3]=16'h7C2A; tbl_lpf[15*16+ 3]=16'h2B00; tbl_diff[15*16+ 3]=159; tbl_mod[15*16+ 3]= 60;
    tbl_delay[15*16+ 4]= 977; tbl_gain[15*16+ 4]=16'h7AB3; tbl_lpf[15*16+ 4]=16'h2C00; tbl_diff[15*16+ 4]=157; tbl_mod[15*16+ 4]= 42;
    tbl_delay[15*16+ 5]=1153; tbl_gain[15*16+ 5]=16'h7892; tbl_lpf[15*16+ 5]=16'h2D00; tbl_diff[15*16+ 5]=155; tbl_mod[15*16+ 5]= 48;
    tbl_delay[15*16+ 6]=1321; tbl_gain[15*16+ 6]=16'h75B1; tbl_lpf[15*16+ 6]=16'h2E00; tbl_diff[15*16+ 6]=153; tbl_mod[15*16+ 6]= 54;
    tbl_delay[15*16+ 7]=1511; tbl_gain[15*16+ 7]=16'h7200; tbl_lpf[15*16+ 7]=16'h2F00; tbl_diff[15*16+ 7]=151; tbl_mod[15*16+ 7]= 60;
    tbl_delay[15*16+ 8]= 463; tbl_gain[15*16+ 8]=16'h7C1F; tbl_lpf[15*16+ 8]=16'h3000; tbl_diff[15*16+ 8]=149; tbl_mod[15*16+ 8]= 42;
    tbl_delay[15*16+ 9]= 601; tbl_gain[15*16+ 9]=16'h7C07; tbl_lpf[15*16+ 9]=16'h3100; tbl_diff[15*16+ 9]=147; tbl_mod[15*16+ 9]= 48;
    tbl_delay[15*16+10]= 739; tbl_gain[15*16+10]=16'h7B97; tbl_lpf[15*16+10]=16'h3200; tbl_diff[15*16+10]=145; tbl_mod[15*16+10]= 54;
    tbl_delay[15*16+11]= 883; tbl_gain[15*16+11]=16'h7AA9; tbl_lpf[15*16+11]=16'h3300; tbl_diff[15*16+11]=143; tbl_mod[15*16+11]= 60;
    tbl_delay[15*16+12]=1039; tbl_gain[15*16+12]=16'h7921; tbl_lpf[15*16+12]=16'h3400; tbl_diff[15*16+12]=141; tbl_mod[15*16+12]= 42;
    tbl_delay[15*16+13]=1223; tbl_gain[15*16+13]=16'h76E4; tbl_lpf[15*16+13]=16'h3500; tbl_diff[15*16+13]=139; tbl_mod[15*16+13]= 48;
    tbl_delay[15*16+14]=1409; tbl_gain[15*16+14]=16'h73E0; tbl_lpf[15*16+14]=16'h3600; tbl_diff[15*16+14]=137; tbl_mod[15*16+14]= 54;
    tbl_delay[15*16+15]=1601; tbl_gain[15*16+15]=16'h7000; tbl_lpf[15*16+15]=16'h3800; tbl_diff[15*16+15]=135; tbl_mod[15*16+15]= 60;

    // =====================================================================
    //  SP_GATED (16) - RT60=600ms
    // =====================================================================
    tbl_delay[16*16+ 0]= 149; tbl_gain[16*16+ 0]=16'h7B1E; tbl_lpf[16*16+ 0]=16'h1400; tbl_diff[16*16+ 0]=220; tbl_mod[16*16+ 0]=  0;
    tbl_delay[16*16+ 1]= 199; tbl_gain[16*16+ 1]=16'h78E0; tbl_lpf[16*16+ 1]=16'h1400; tbl_diff[16*16+ 1]=218; tbl_mod[16*16+ 1]=  1;
    tbl_delay[16*16+ 2]= 251; tbl_gain[16*16+ 2]=16'h76A3; tbl_lpf[16*16+ 2]=16'h1500; tbl_diff[16*16+ 2]=217; tbl_mod[16*16+ 2]=  2;
    tbl_delay[16*16+ 3]= 307; tbl_gain[16*16+ 3]=16'h7466; tbl_lpf[16*16+ 3]=16'h1600; tbl_diff[16*16+ 3]=215; tbl_mod[16*16+ 3]=  3;
    tbl_delay[16*16+ 4]= 367; tbl_gain[16*16+ 4]=16'h62E8; tbl_lpf[16*16+ 4]=16'h1700; tbl_diff[16*16+ 4]=213; tbl_mod[16*16+ 4]=  0;
    tbl_delay[16*16+ 5]= 431; tbl_gain[16*16+ 5]=16'h1D4E; tbl_lpf[16*16+ 5]=16'h1800; tbl_diff[16*16+ 5]=212; tbl_mod[16*16+ 5]=  1;
    tbl_delay[16*16+ 6]= 499; tbl_gain[16*16+ 6]=16'h03A9; tbl_lpf[16*16+ 6]=16'h1900; tbl_diff[16*16+ 6]=210; tbl_mod[16*16+ 6]=  2;
    tbl_delay[16*16+ 7]= 563; tbl_gain[16*16+ 7]=16'h0000; tbl_lpf[16*16+ 7]=16'h1A00; tbl_diff[16*16+ 7]=208; tbl_mod[16*16+ 7]=  3;
    tbl_delay[16*16+ 8]= 163; tbl_gain[16*16+ 8]=16'h7AAB; tbl_lpf[16*16+ 8]=16'h1B00; tbl_diff[16*16+ 8]=207; tbl_mod[16*16+ 8]=  0;
    tbl_delay[16*16+ 9]= 211; tbl_gain[16*16+ 9]=16'h7870; tbl_lpf[16*16+ 9]=16'h1C00; tbl_diff[16*16+ 9]=205; tbl_mod[16*16+ 9]=  1;
    tbl_delay[16*16+10]= 271; tbl_gain[16*16+10]=16'h7635; tbl_lpf[16*16+10]=16'h1D00; tbl_diff[16*16+10]=203; tbl_mod[16*16+10]=  2;
    tbl_delay[16*16+11]= 331; tbl_gain[16*16+11]=16'h73FA; tbl_lpf[16*16+11]=16'h1E00; tbl_diff[16*16+11]=202; tbl_mod[16*16+11]=  3;
    tbl_delay[16*16+12]= 397; tbl_gain[16*16+12]=16'h628C; tbl_lpf[16*16+12]=16'h1F00; tbl_diff[16*16+12]=200; tbl_mod[16*16+12]=  0;
    tbl_delay[16*16+13]= 463; tbl_gain[16*16+13]=16'h1D33; tbl_lpf[16*16+13]=16'h2000; tbl_diff[16*16+13]=198; tbl_mod[16*16+13]=  1;
    tbl_delay[16*16+14]= 541; tbl_gain[16*16+14]=16'h03A6; tbl_lpf[16*16+14]=16'h2100; tbl_diff[16*16+14]=197; tbl_mod[16*16+14]=  2;
    tbl_delay[16*16+15]= 613; tbl_gain[16*16+15]=16'h0000; tbl_lpf[16*16+15]=16'h2200; tbl_diff[16*16+15]=195; tbl_mod[16*16+15]=  3;

    // =====================================================================
    //  SP_REVERSE (17) - RT60=3000ms
    // =====================================================================
    tbl_delay[17*16+ 0]= 503; tbl_gain[17*16+ 0]=16'h69FB; tbl_lpf[17*16+ 0]=16'h2A00; tbl_diff[17*16+ 0]=180; tbl_mod[17*16+ 0]= 10;
    tbl_delay[17*16+ 1]= 617; tbl_gain[17*16+ 1]=16'h6DB3; tbl_lpf[17*16+ 1]=16'h2B00; tbl_diff[17*16+ 1]=178; tbl_mod[17*16+ 1]= 12;
    tbl_delay[17*16+ 2]= 743; tbl_gain[17*16+ 2]=16'h70D8; tbl_lpf[17*16+ 2]=16'h2C00; tbl_diff[17*16+ 2]=176; tbl_mod[17*16+ 2]= 14;
    tbl_delay[17*16+ 3]= 877; tbl_gain[17*16+ 3]=16'h736C; tbl_lpf[17*16+ 3]=16'h2D00; tbl_diff[17*16+ 3]=175; tbl_mod[17*16+ 3]= 16;
    tbl_delay[17*16+ 4]=1019; tbl_gain[17*16+ 4]=16'h756C; tbl_lpf[17*16+ 4]=16'h2E00; tbl_diff[17*16+ 4]=173; tbl_mod[17*16+ 4]= 10;
    tbl_delay[17*16+ 5]=1171; tbl_gain[17*16+ 5]=16'h76DB; tbl_lpf[17*16+ 5]=16'h2F00; tbl_diff[17*16+ 5]=171; tbl_mod[17*16+ 5]= 12;
    tbl_delay[17*16+ 6]=1321; tbl_gain[17*16+ 6]=16'h77B6; tbl_lpf[17*16+ 6]=16'h3000; tbl_diff[17*16+ 6]=169; tbl_mod[17*16+ 6]= 14;
    tbl_delay[17*16+ 7]=1493; tbl_gain[17*16+ 7]=16'h7800; tbl_lpf[17*16+ 7]=16'h3100; tbl_diff[17*16+ 7]=167; tbl_mod[17*16+ 7]= 16;
    tbl_delay[17*16+ 8]= 547; tbl_gain[17*16+ 8]=16'h6C39; tbl_lpf[17*16+ 8]=16'h3200; tbl_diff[17*16+ 8]=165; tbl_mod[17*16+ 8]= 10;
    tbl_delay[17*16+ 9]= 659; tbl_gain[17*16+ 9]=16'h6F14; tbl_lpf[17*16+ 9]=16'h3300; tbl_diff[17*16+ 9]=163; tbl_mod[17*16+ 9]= 12;
    tbl_delay[17*16+10]= 787; tbl_gain[17*16+10]=16'h7180; tbl_lpf[17*16+10]=16'h3400; tbl_diff[17*16+10]=161; tbl_mod[17*16+10]= 14;
    tbl_delay[17*16+11]= 929; tbl_gain[17*16+11]=16'h737B; tbl_lpf[17*16+11]=16'h3500; tbl_diff[17*16+11]=160; tbl_mod[17*16+11]= 16;
    tbl_delay[17*16+12]=1069; tbl_gain[17*16+12]=16'h7505; tbl_lpf[17*16+12]=16'h3600; tbl_diff[17*16+12]=158; tbl_mod[17*16+12]= 10;
    tbl_delay[17*16+13]=1223; tbl_gain[17*16+13]=16'h761E; tbl_lpf[17*16+13]=16'h3700; tbl_diff[17*16+13]=156; tbl_mod[17*16+13]= 12;
    tbl_delay[17*16+14]=1399; tbl_gain[17*16+14]=16'h76C7; tbl_lpf[17*16+14]=16'h3800; tbl_diff[17*16+14]=154; tbl_mod[17*16+14]= 14;
    tbl_delay[17*16+15]=1571; tbl_gain[17*16+15]=16'h7700; tbl_lpf[17*16+15]=16'h3A00; tbl_diff[17*16+15]=152; tbl_mod[17*16+15]= 16;

    // =====================================================================
    //  SP_BROKEN (18) - RT60=2000ms
    // =====================================================================
    tbl_delay[18*16+ 0]= 311; tbl_gain[18*16+ 0]=16'h7CEB; tbl_lpf[18*16+ 0]=16'h1E00; tbl_diff[18*16+ 0]= 60; tbl_mod[18*16+ 0]=180;
    tbl_delay[18*16+ 1]= 733; tbl_gain[18*16+ 1]=16'h78DC; tbl_lpf[18*16+ 1]=16'h3200; tbl_diff[18*16+ 1]=180; tbl_mod[18*16+ 1]= 40;
    tbl_delay[18*16+ 2]= 157; tbl_gain[18*16+ 2]=16'h7E70; tbl_lpf[18*16+ 2]=16'h1200; tbl_diff[18*16+ 2]= 35; tbl_mod[18*16+ 2]=210;
    tbl_delay[18*16+ 3]= 911; tbl_gain[18*16+ 3]=16'h772F; tbl_lpf[18*16+ 3]=16'h3C00; tbl_diff[18*16+ 3]=200; tbl_mod[18*16+ 3]= 20;
    tbl_delay[18*16+ 4]= 463; tbl_gain[18*16+ 4]=16'h7B71; tbl_lpf[18*16+ 4]=16'h2200; tbl_diff[18*16+ 4]= 90; tbl_mod[18*16+ 4]=170;
    tbl_delay[18*16+ 5]=1201; tbl_gain[18*16+ 5]=16'h7482; tbl_lpf[18*16+ 5]=16'h4000; tbl_diff[18*16+ 5]=160; tbl_mod[18*16+ 5]= 60;
    tbl_delay[18*16+ 6]= 271; tbl_gain[18*16+ 6]=16'h7D50; tbl_lpf[18*16+ 6]=16'h1600; tbl_diff[18*16+ 6]= 45; tbl_mod[18*16+ 6]=220;
    tbl_delay[18*16+ 7]=1433; tbl_gain[18*16+ 7]=16'h7269; tbl_lpf[18*16+ 7]=16'h4400; tbl_diff[18*16+ 7]=220; tbl_mod[18*16+ 7]= 10;
    tbl_delay[18*16+ 8]= 389; tbl_gain[18*16+ 8]=16'h7C29; tbl_lpf[18*16+ 8]=16'h2000; tbl_diff[18*16+ 8]= 70; tbl_mod[18*16+ 8]=190;
    tbl_delay[18*16+ 9]= 821; tbl_gain[18*16+ 9]=16'h7807; tbl_lpf[18*16+ 9]=16'h3800; tbl_diff[18*16+ 9]=190; tbl_mod[18*16+ 9]= 50;
    tbl_delay[18*16+10]= 197; tbl_gain[18*16+10]=16'h7E0A; tbl_lpf[18*16+10]=16'h1400; tbl_diff[18*16+10]= 40; tbl_mod[18*16+10]=200;
    tbl_delay[18*16+11]=1039; tbl_gain[18*16+11]=16'h75FF; tbl_lpf[18*16+11]=16'h4200; tbl_diff[18*16+11]=210; tbl_mod[18*16+11]= 30;
    tbl_delay[18*16+12]= 557; tbl_gain[18*16+12]=16'h7A89; tbl_lpf[18*16+12]=16'h2400; tbl_diff[18*16+12]=100; tbl_mod[18*16+12]=160;
    tbl_delay[18*16+13]=1327; tbl_gain[18*16+13]=16'h735D; tbl_lpf[18*16+13]=16'h3E00; tbl_diff[18*16+13]=170; tbl_mod[18*16+13]= 70;
    tbl_delay[18*16+14]= 293; tbl_gain[18*16+14]=16'h7D19; tbl_lpf[18*16+14]=16'h1800; tbl_diff[18*16+14]= 50; tbl_mod[18*16+14]=215;
    tbl_delay[18*16+15]=1601; tbl_gain[18*16+15]=16'h70EA; tbl_lpf[18*16+15]=16'h4600; tbl_diff[18*16+15]=230; tbl_mod[18*16+15]= 15;

    // =====================================================================
    //  SP_SHIMMER (19) - RT60=4000ms
    // =====================================================================
    tbl_delay[19*16+ 0]= 503; tbl_gain[19*16+ 0]=16'h7D81; tbl_lpf[19*16+ 0]=16'h5000; tbl_diff[19*16+ 0]=210; tbl_mod[19*16+ 0]=200;
    tbl_delay[19*16+ 1]= 617; tbl_gain[19*16+ 1]=16'h7D6A; tbl_lpf[19*16+ 1]=16'h5200; tbl_diff[19*16+ 1]=209; tbl_mod[19*16+ 1]=205;
    tbl_delay[19*16+ 2]= 743; tbl_gain[19*16+ 2]=16'h7D00; tbl_lpf[19*16+ 2]=16'h5400; tbl_diff[19*16+ 2]=208; tbl_mod[19*16+ 2]=210;
    tbl_delay[19*16+ 3]= 877; tbl_gain[19*16+ 3]=16'h7C1E; tbl_lpf[19*16+ 3]=16'h5600; tbl_diff[19*16+ 3]=207; tbl_mod[19*16+ 3]=215;
    tbl_delay[19*16+ 4]=1019; tbl_gain[19*16+ 4]=16'h7AAA; tbl_lpf[19*16+ 4]=16'h5800; tbl_diff[19*16+ 4]=206; tbl_mod[19*16+ 4]=200;
    tbl_delay[19*16+ 5]=1171; tbl_gain[19*16+ 5]=16'h788B; tbl_lpf[19*16+ 5]=16'h5A00; tbl_diff[19*16+ 5]=205; tbl_mod[19*16+ 5]=205;
    tbl_delay[19*16+ 6]=1321; tbl_gain[19*16+ 6]=16'h75AD; tbl_lpf[19*16+ 6]=16'h5C00; tbl_diff[19*16+ 6]=204; tbl_mod[19*16+ 6]=210;
    tbl_delay[19*16+ 7]=1493; tbl_gain[19*16+ 7]=16'h7200; tbl_lpf[19*16+ 7]=16'h5E00; tbl_diff[19*16+ 7]=203; tbl_mod[19*16+ 7]=215;
    tbl_delay[19*16+ 8]= 547; tbl_gain[19*16+ 8]=16'h7C09; tbl_lpf[19*16+ 8]=16'h6000; tbl_diff[19*16+ 8]=202; tbl_mod[19*16+ 8]=200;
    tbl_delay[19*16+ 9]= 659; tbl_gain[19*16+ 9]=16'h7BF1; tbl_lpf[19*16+ 9]=16'h6200; tbl_diff[19*16+ 9]=201; tbl_mod[19*16+ 9]=205;
    tbl_delay[19*16+10]= 787; tbl_gain[19*16+10]=16'h7B82; tbl_lpf[19*16+10]=16'h6400; tbl_diff[19*16+10]=200; tbl_mod[19*16+10]=210;
    tbl_delay[19*16+11]= 929; tbl_gain[19*16+11]=16'h7A96; tbl_lpf[19*16+11]=16'h6600; tbl_diff[19*16+11]=199; tbl_mod[19*16+11]=215;
    tbl_delay[19*16+12]=1069; tbl_gain[19*16+12]=16'h7910; tbl_lpf[19*16+12]=16'h6800; tbl_diff[19*16+12]=198; tbl_mod[19*16+12]=200;
    tbl_delay[19*16+13]=1223; tbl_gain[19*16+13]=16'h76D8; tbl_lpf[19*16+13]=16'h6A00; tbl_diff[19*16+13]=197; tbl_mod[19*16+13]=205;
    tbl_delay[19*16+14]=1399; tbl_gain[19*16+14]=16'h73D9; tbl_lpf[19*16+14]=16'h6C00; tbl_diff[19*16+14]=196; tbl_mod[19*16+14]=210;
    tbl_delay[19*16+15]=1571; tbl_gain[19*16+15]=16'h7000; tbl_lpf[19*16+15]=16'h6E00; tbl_diff[19*16+15]=195; tbl_mod[19*16+15]=215;

    // =====================================================================
    //  SP_FROZEN (20) - RT60=8000ms
    // =====================================================================
    tbl_delay[20*16+ 0]= 911; tbl_gain[20*16+ 0]=16'h7DBD; tbl_lpf[20*16+ 0]=16'h3C00; tbl_diff[20*16+ 0]=222; tbl_mod[20*16+ 0]=  0;
    tbl_delay[20*16+ 1]=1187; tbl_gain[20*16+ 1]=16'h7D0F; tbl_lpf[20*16+ 1]=16'h3D00; tbl_diff[20*16+ 1]=220; tbl_mod[20*16+ 1]=  1;
    tbl_delay[20*16+ 2]=1499; tbl_gain[20*16+ 2]=16'h7C4C; tbl_lpf[20*16+ 2]=16'h3E00; tbl_diff[20*16+ 2]=218; tbl_mod[20*16+ 2]=  1;
    tbl_delay[20*16+ 3]=1861; tbl_gain[20*16+ 3]=16'h7B6B; tbl_lpf[20*16+ 3]=16'h3F00; tbl_diff[20*16+ 3]=217; tbl_mod[20*16+ 3]=  2;
    tbl_delay[20*16+ 4]=2293; tbl_gain[20*16+ 4]=16'h7A61; tbl_lpf[20*16+ 4]=16'h4000; tbl_diff[20*16+ 4]=215; tbl_mod[20*16+ 4]=  0;
    tbl_delay[20*16+ 5]=2789; tbl_gain[20*16+ 5]=16'h799A; tbl_lpf[20*16+ 5]=16'h4100; tbl_diff[20*16+ 5]=213; tbl_mod[20*16+ 5]=  1;
    tbl_delay[20*16+ 6]=3371; tbl_gain[20*16+ 6]=16'h799A; tbl_lpf[20*16+ 6]=16'h4200; tbl_diff[20*16+ 6]=211; tbl_mod[20*16+ 6]=  1;
    tbl_delay[20*16+ 7]=4027; tbl_gain[20*16+ 7]=16'h799A; tbl_lpf[20*16+ 7]=16'h4300; tbl_diff[20*16+ 7]=209; tbl_mod[20*16+ 7]=  2;
    tbl_delay[20*16+ 8]= 983; tbl_gain[20*16+ 8]=16'h7D8F; tbl_lpf[20*16+ 8]=16'h4400; tbl_diff[20*16+ 8]=208; tbl_mod[20*16+ 8]=  0;
    tbl_delay[20*16+ 9]=1277; tbl_gain[20*16+ 9]=16'h7CD7; tbl_lpf[20*16+ 9]=16'h4500; tbl_diff[20*16+ 9]=206; tbl_mod[20*16+ 9]=  1;
    tbl_delay[20*16+10]=1601; tbl_gain[20*16+10]=16'h7C0D; tbl_lpf[20*16+10]=16'h4600; tbl_diff[20*16+10]=204; tbl_mod[20*16+10]=  1;
    tbl_delay[20*16+11]=1999; tbl_gain[20*16+11]=16'h7B16; tbl_lpf[20*16+11]=16'h4700; tbl_diff[20*16+11]=202; tbl_mod[20*16+11]=  2;
    tbl_delay[20*16+12]=2441; tbl_gain[20*16+12]=16'h7A07; tbl_lpf[20*16+12]=16'h4800; tbl_diff[20*16+12]=200; tbl_mod[20*16+12]=  0;
    tbl_delay[20*16+13]=2971; tbl_gain[20*16+13]=16'h799A; tbl_lpf[20*16+13]=16'h4900; tbl_diff[20*16+13]=199; tbl_mod[20*16+13]=  1;
    tbl_delay[20*16+14]=3581; tbl_gain[20*16+14]=16'h799A; tbl_lpf[20*16+14]=16'h4A00; tbl_diff[20*16+14]=197; tbl_mod[20*16+14]=  1;
    tbl_delay[20*16+15]=4273; tbl_gain[20*16+15]=16'h799A; tbl_lpf[20*16+15]=16'h4C00; tbl_diff[20*16+15]=195; tbl_mod[20*16+15]=  2;

    // =====================================================================
    //  SP_ASCENDING (21) - RT60=3800ms
    // =====================================================================
    tbl_delay[21*16+ 0]= 431; tbl_gain[21*16+ 0]=16'h7DBF; tbl_lpf[21*16+ 0]=16'h1E00; tbl_diff[21*16+ 0]=200; tbl_mod[21*16+ 0]=220;
    tbl_delay[21*16+ 1]= 563; tbl_gain[21*16+ 1]=16'h7DAB; tbl_lpf[21*16+ 1]=16'h1F00; tbl_diff[21*16+ 1]=198; tbl_mod[21*16+ 1]=226;
    tbl_delay[21*16+ 2]= 691; tbl_gain[21*16+ 2]=16'h7D52; tbl_lpf[21*16+ 2]=16'h2000; tbl_diff[21*16+ 2]=196; tbl_mod[21*16+ 2]=232;
    tbl_delay[21*16+ 3]= 821; tbl_gain[21*16+ 3]=16'h7C92; tbl_lpf[21*16+ 3]=16'h2100; tbl_diff[21*16+ 3]=194; tbl_mod[21*16+ 3]=238;
    tbl_delay[21*16+ 4]= 977; tbl_gain[21*16+ 4]=16'h7B57; tbl_lpf[21*16+ 4]=16'h2200; tbl_diff[21*16+ 4]=193; tbl_mod[21*16+ 4]=220;
    tbl_delay[21*16+ 5]=1153; tbl_gain[21*16+ 5]=16'h798B; tbl_lpf[21*16+ 5]=16'h2300; tbl_diff[21*16+ 5]=191; tbl_mod[21*16+ 5]=226;
    tbl_delay[21*16+ 6]=1321; tbl_gain[21*16+ 6]=16'h771D; tbl_lpf[21*16+ 6]=16'h2400; tbl_diff[21*16+ 6]=189; tbl_mod[21*16+ 6]=232;
    tbl_delay[21*16+ 7]=1511; tbl_gain[21*16+ 7]=16'h7400; tbl_lpf[21*16+ 7]=16'h2500; tbl_diff[21*16+ 7]=187; tbl_mod[21*16+ 7]=238;
    tbl_delay[21*16+ 8]= 463; tbl_gain[21*16+ 8]=16'h7C53; tbl_lpf[21*16+ 8]=16'h2600; tbl_diff[21*16+ 8]=185; tbl_mod[21*16+ 8]=220;
    tbl_delay[21*16+ 9]= 601; tbl_gain[21*16+ 9]=16'h7C3E; tbl_lpf[21*16+ 9]=16'h2700; tbl_diff[21*16+ 9]=183; tbl_mod[21*16+ 9]=226;
    tbl_delay[21*16+10]= 739; tbl_gain[21*16+10]=16'h7BDF; tbl_lpf[21*16+10]=16'h2800; tbl_diff[21*16+10]=181; tbl_mod[21*16+10]=232;
    tbl_delay[21*16+11]= 883; tbl_gain[21*16+11]=16'h7B15; tbl_lpf[21*16+11]=16'h2900; tbl_diff[21*16+11]=179; tbl_mod[21*16+11]=238;
    tbl_delay[21*16+12]=1039; tbl_gain[21*16+12]=16'h79C6; tbl_lpf[21*16+12]=16'h2A00; tbl_diff[21*16+12]=178; tbl_mod[21*16+12]=220;
    tbl_delay[21*16+13]=1223; tbl_gain[21*16+13]=16'h77DF; tbl_lpf[21*16+13]=16'h2B00; tbl_diff[21*16+13]=176; tbl_mod[21*16+13]=226;
    tbl_delay[21*16+14]=1409; tbl_gain[21*16+14]=16'h754D; tbl_lpf[21*16+14]=16'h2C00; tbl_diff[21*16+14]=174; tbl_mod[21*16+14]=232;
    tbl_delay[21*16+15]=1601; tbl_gain[21*16+15]=16'h7200; tbl_lpf[21*16+15]=16'h2E00; tbl_diff[21*16+15]=172; tbl_mod[21*16+15]=238;

    // =====================================================================
    //  SP_DESCENDING (22) - RT60=3200ms
    // =====================================================================
    tbl_delay[22*16+ 0]= 431; tbl_gain[22*16+ 0]=16'h7D54; tbl_lpf[22*16+ 0]=16'h2800; tbl_diff[22*16+ 0]=148; tbl_mod[22*16+ 0]=180;
    tbl_delay[22*16+ 1]= 563; tbl_gain[22*16+ 1]=16'h7ADA; tbl_lpf[22*16+ 1]=16'h2900; tbl_diff[22*16+ 1]=146; tbl_mod[22*16+ 1]=188;
    tbl_delay[22*16+ 2]= 691; tbl_gain[22*16+ 2]=16'h7860; tbl_lpf[22*16+ 2]=16'h2A00; tbl_diff[22*16+ 2]=144; tbl_mod[22*16+ 2]=196;
    tbl_delay[22*16+ 3]= 821; tbl_gain[22*16+ 3]=16'h75E6; tbl_lpf[22*16+ 3]=16'h2B00; tbl_diff[22*16+ 3]=142; tbl_mod[22*16+ 3]=204;
    tbl_delay[22*16+ 4]= 977; tbl_gain[22*16+ 4]=16'h736D; tbl_lpf[22*16+ 4]=16'h2C00; tbl_diff[22*16+ 4]=140; tbl_mod[22*16+ 4]=180;
    tbl_delay[22*16+ 5]=1153; tbl_gain[22*16+ 5]=16'h70F3; tbl_lpf[22*16+ 5]=16'h2D00; tbl_diff[22*16+ 5]=138; tbl_mod[22*16+ 5]=188;
    tbl_delay[22*16+ 6]=1321; tbl_gain[22*16+ 6]=16'h6E79; tbl_lpf[22*16+ 6]=16'h2E00; tbl_diff[22*16+ 6]=136; tbl_mod[22*16+ 6]=196;
    tbl_delay[22*16+ 7]=1511; tbl_gain[22*16+ 7]=16'h6C00; tbl_lpf[22*16+ 7]=16'h2F00; tbl_diff[22*16+ 7]=134; tbl_mod[22*16+ 7]=204;
    tbl_delay[22*16+ 8]= 463; tbl_gain[22*16+ 8]=16'h7BE1; tbl_lpf[22*16+ 8]=16'h3000; tbl_diff[22*16+ 8]=132; tbl_mod[22*16+ 8]=180;
    tbl_delay[22*16+ 9]= 601; tbl_gain[22*16+ 9]=16'h7953; tbl_lpf[22*16+ 9]=16'h3100; tbl_diff[22*16+ 9]=130; tbl_mod[22*16+ 9]=188;
    tbl_delay[22*16+10]= 739; tbl_gain[22*16+10]=16'h76C5; tbl_lpf[22*16+10]=16'h3200; tbl_diff[22*16+10]=128; tbl_mod[22*16+10]=196;
    tbl_delay[22*16+11]= 883; tbl_gain[22*16+11]=16'h7437; tbl_lpf[22*16+11]=16'h3300; tbl_diff[22*16+11]=126; tbl_mod[22*16+11]=204;
    tbl_delay[22*16+12]=1039; tbl_gain[22*16+12]=16'h71A9; tbl_lpf[22*16+12]=16'h3400; tbl_diff[22*16+12]=124; tbl_mod[22*16+12]=180;
    tbl_delay[22*16+13]=1223; tbl_gain[22*16+13]=16'h6F1B; tbl_lpf[22*16+13]=16'h3500; tbl_diff[22*16+13]=122; tbl_mod[22*16+13]=188;
    tbl_delay[22*16+14]=1409; tbl_gain[22*16+14]=16'h6C8D; tbl_lpf[22*16+14]=16'h3600; tbl_diff[22*16+14]=120; tbl_mod[22*16+14]=196;
    tbl_delay[22*16+15]=1601; tbl_gain[22*16+15]=16'h6A00; tbl_lpf[22*16+15]=16'h3800; tbl_diff[22*16+15]=118; tbl_mod[22*16+15]=204;

    // =====================================================================
    //  SP_SPECIAL (23) - RT60=2500ms
    // =====================================================================
    tbl_delay[23*16+ 0]= 311; tbl_gain[23*16+ 0]=16'h7D88; tbl_lpf[23*16+ 0]=16'h2200; tbl_diff[23*16+ 0]=185; tbl_mod[23*16+ 0]=120;
    tbl_delay[23*16+ 1]= 733; tbl_gain[23*16+ 1]=16'h7A41; tbl_lpf[23*16+ 1]=16'h3800; tbl_diff[23*16+ 1]= 90; tbl_mod[23*16+ 1]=200;
    tbl_delay[23*16+ 2]= 157; tbl_gain[23*16+ 2]=16'h7EBF; tbl_lpf[23*16+ 2]=16'h1800; tbl_diff[23*16+ 2]=210; tbl_mod[23*16+ 2]= 60;
    tbl_delay[23*16+ 3]= 911; tbl_gain[23*16+ 3]=16'h78E6; tbl_lpf[23*16+ 3]=16'h4000; tbl_diff[23*16+ 3]= 70; tbl_mod[23*16+ 3]=240;
    tbl_delay[23*16+ 4]= 463; tbl_gain[23*16+ 4]=16'h7C57; tbl_lpf[23*16+ 4]=16'h2800; tbl_diff[23*16+ 4]=160; tbl_mod[23*16+ 4]=140;
    tbl_delay[23*16+ 5]=1201; tbl_gain[23*16+ 5]=16'h76B9; tbl_lpf[23*16+ 5]=16'h3C00; tbl_diff[23*16+ 5]=110; tbl_mod[23*16+ 5]=180;
    tbl_delay[23*16+ 6]= 271; tbl_gain[23*16+ 6]=16'h7DD8; tbl_lpf[23*16+ 6]=16'h1C00; tbl_diff[23*16+ 6]=230; tbl_mod[23*16+ 6]= 80;
    tbl_delay[23*16+ 7]=1433; tbl_gain[23*16+ 7]=16'h7502; tbl_lpf[23*16+ 7]=16'h4400; tbl_diff[23*16+ 7]= 55; tbl_mod[23*16+ 7]=255;
    tbl_delay[23*16+ 8]= 389; tbl_gain[23*16+ 8]=16'h7CEB; tbl_lpf[23*16+ 8]=16'h2400; tbl_diff[23*16+ 8]=175; tbl_mod[23*16+ 8]=110;
    tbl_delay[23*16+ 9]= 821; tbl_gain[23*16+ 9]=16'h7995; tbl_lpf[23*16+ 9]=16'h3A00; tbl_diff[23*16+ 9]=100; tbl_mod[23*16+ 9]=220;
    tbl_delay[23*16+10]= 197; tbl_gain[23*16+10]=16'h7E6E; tbl_lpf[23*16+10]=16'h1A00; tbl_diff[23*16+10]=220; tbl_mod[23*16+10]= 70;
    tbl_delay[23*16+11]=1039; tbl_gain[23*16+11]=16'h77EF; tbl_lpf[23*16+11]=16'h4200; tbl_diff[23*16+11]= 60; tbl_mod[23*16+11]=250;
    tbl_delay[23*16+12]= 557; tbl_gain[23*16+12]=16'h7B9C; tbl_lpf[23*16+12]=16'h2C00; tbl_diff[23*16+12]=170; tbl_mod[23*16+12]=130;
    tbl_delay[23*16+13]=1327; tbl_gain[23*16+13]=16'h75CA; tbl_lpf[23*16+13]=16'h3E00; tbl_diff[23*16+13]=120; tbl_mod[23*16+13]=190;
    tbl_delay[23*16+14]= 293; tbl_gain[23*16+14]=16'h7DAC; tbl_lpf[23*16+14]=16'h2000; tbl_diff[23*16+14]=200; tbl_mod[23*16+14]= 90;
    tbl_delay[23*16+15]=1601; tbl_gain[23*16+15]=16'h73C8; tbl_lpf[23*16+15]=16'h4800; tbl_diff[23*16+15]= 65; tbl_mod[23*16+15]=255;


end

endmodule