// ============================================================================
//  space_behavior_engine.v  v1
//
//  [v9 space BEHAVIOR LAYER] 중간 계층
//
//  역할:
//    space_id + family_id + behavior_seed → time-varying behavior curves
//    ER feedback → modulation depth 조정
//    출력 → fdn_space_table scale 입력으로 연결
//
//  설계 원칙:
//    ✔ LUT/BRAM 접근 금지 (behavior 내부에서 table 직접 접근 금지)
//    ✔ shift/add only (DSP, LUT 최소화)
//    ✔ 3-stage pipeline (combinational 최소화, 타이밍 안전)
//    ✔ ER feedback clamp (무한 증폭 방지)
//    ✔ modulation vs diffusion priority arbitration
//    ✔ 출력 레지스터 단계 포함 (fanout 제어)
//
//  포트:
//    입력: space_id, family_id, behavior_seed, er_energy_feedback,
//          er_density_dynamic, line_idx, clk, rst_n
//    출력: behavior_gain_scale, behavior_lpf_scale, behavior_diff_scale
//
//  파이프라인:
//    STAGE 1: space_id → family decode + seed 적용 → base curve 선택
//    STAGE 2: family → modulation 계산 (shift/add LFO)
//    STAGE 3: ER feedback 블렌딩 + clamp + 출력 레지스터
//
//  합성 안전:
//    HDL 9-1206: named block 내 reg 상단 선언
//    synth 8-6858: 모든 FF 리셋값 명시
//    synth 8-6859: 단일 드라이버 (always 블록 분리)
//    타이밍: FSM critical path 외부 독립 동작
// ============================================================================

`timescale 1ns / 1ps

module space_behavior_engine (
    input  wire        clk,
    input  wire        rst_n,

    // ── 공간 정보 입력 ──────────────────────────────────────────────────────
    input  wire [4:0]  space_id,
    input  wire [7:0]  family_id,       // space_model_core 출력
    input  wire [7:0]  behavior_seed,   // space_model_core 출력

    // ── ER feedback 입력 (space_er_engine 출력) ───────────────────────────
    input  wire [7:0]  er_energy_feedback,
    input  wire [7:0]  er_density_dynamic,

    // ── line 인덱스 (per-line 미세 변조용) ──────────────────────────────
    input  wire [3:0]  line_idx,

    // ── behavior scale 출력 → fdn_space_table 입력 ───────────────────────
    // Q1.15 중립값: 0x8000 (= 1.0)
    // Q1.7  중립값: 0x80   (= 1.0)
    output reg  [15:0] behavior_gain_scale,
    output reg  [15:0] behavior_lpf_scale,
    output reg  [7:0]  behavior_diff_scale
);

// ============================================================================
//  로컬 파라미터
// ============================================================================
// Q1.15 상수
localparam [15:0] SCALE_NEUTRAL  = 16'h8000;  // 1.0
localparam [15:0] SCALE_MAX_GAIN = 16'h7F00;  // ~0.996 (gain 상한)
localparam [15:0] SCALE_MIN_GAIN = 16'h6000;  // ~0.75  (gain 하한)
localparam [15:0] SCALE_MAX_LPF  = 16'hA000;  // ~1.25  (더 어두움)
localparam [15:0] SCALE_MIN_LPF  = 16'h6000;  // ~0.75  (더 밝음)
localparam [7:0]  SCALE_MAX_DIFF = 8'hC0;     // 확산 상한
localparam [7:0]  SCALE_MIN_DIFF = 8'h60;     // 확산 하한
localparam [7:0]  SCALE_NEU_DIFF = 8'h80;     // 확산 중립

// ER feedback clamp 상한 (무한 증폭 방지)
localparam [7:0]  ER_ENERGY_CLAMP = 8'hC0;

// Family ID
localparam [7:0] FAM_ROOM    = 8'd0;
localparam [7:0] FAM_HALL    = 8'd1;
localparam [7:0] FAM_PLATE   = 8'd2;
localparam [7:0] FAM_TONAL   = 8'd3;
localparam [7:0] FAM_MOTION  = 8'd4;
localparam [7:0] FAM_SPECIAL = 8'd5;
localparam [7:0] FAM_GATE    = 8'd6;
localparam [7:0] FAM_CUSTOM  = 8'd7;

// ============================================================================
//  내부 free-running 위상 카운터 (LFO 대용, shift/add만 사용)
//  HDL 9-1206: reg 상단 선언
// ============================================================================
reg [23:0] phase_r;       // 공통 위상 (slow: ~3Hz @50MHz)
reg [19:0] phase_fast_r;  // 빠른 위상 (~30Hz @50MHz)

// ============================================================================
//  STAGE 1 레지스터: family decode + base curve
// ============================================================================
reg [15:0] s1_base_gain_scale;   // synth 8-6858: 리셋값 명시
reg [15:0] s1_base_lpf_scale;
reg [7:0]  s1_base_diff_scale;
reg [7:0]  s1_mod_depth;         // 변조 깊이 (family 기반)
reg [7:0]  s1_energy_clamped;    // ER feedback clamp
reg [7:0]  s1_family_r;

// ============================================================================
//  STAGE 2 레지스터: modulation 적용
// ============================================================================
reg [15:0] s2_gain_scale;
reg [15:0] s2_lpf_scale;
reg [7:0]  s2_diff_scale;

// ============================================================================
//  위상 카운터 (synth 8-6859: 전용 always)
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        phase_r      <= 24'h000000;  // synth 8-6858
        phase_fast_r <= 20'h00000;
    end else begin
        phase_r      <= phase_r      + 24'h000005;  // ~3Hz @50MHz
        phase_fast_r <= phase_fast_r + 20'h00032;   // ~30Hz @50MHz
    end
end

// ============================================================================
//  STAGE 1: family decode → base curve 선택
//  synth 8-6859: 전용 always 블록
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        s1_base_gain_scale <= SCALE_NEUTRAL;  // synth 8-6858
        s1_base_lpf_scale  <= SCALE_NEUTRAL;
        s1_base_diff_scale <= SCALE_NEU_DIFF;
        s1_mod_depth       <= 8'h10;
        s1_energy_clamped  <= 8'h00;
        s1_family_r        <= FAM_HALL;
    end else begin
        // ER energy clamp (무한 증폭 방지)
        s1_energy_clamped <= (er_energy_feedback > ER_ENERGY_CLAMP) ?
                              ER_ENERGY_CLAMP : er_energy_feedback;

        s1_family_r <= family_id;

        // family 기반 base curve 선택 (shift/add only, LUT 없음)
        case (family_id)
            FAM_ROOM: begin
                // 방: gain 약간 눌림(에너지 포화), lpf 밝음 유지, diff 중간
                s1_base_gain_scale <= SCALE_NEUTRAL - 16'h0800;
                s1_base_lpf_scale  <= SCALE_NEUTRAL - 16'h0400;
                s1_base_diff_scale <= SCALE_NEU_DIFF;
                s1_mod_depth       <= 8'h08;
            end
            FAM_HALL: begin
                // 홀: gain 중립, lpf 약간 어두움, diff 넓음
                s1_base_gain_scale <= SCALE_NEUTRAL;
                s1_base_lpf_scale  <= SCALE_NEUTRAL + 16'h0800;
                s1_base_diff_scale <= SCALE_NEU_DIFF + 8'h10;
                s1_mod_depth       <= 8'h10;
            end
            FAM_PLATE: begin
                // 플레이트: gain 중립, lpf 밝음, diff 강함
                s1_base_gain_scale <= SCALE_NEUTRAL;
                s1_base_lpf_scale  <= SCALE_NEUTRAL - 16'h1000;
                s1_base_diff_scale <= SCALE_NEU_DIFF + 8'h20;
                s1_mod_depth       <= 8'h18;
            end
            FAM_TONAL: begin
                // 토널: gain 약간 높음 (긴 울림), lpf 중간, diff 낮음
                s1_base_gain_scale <= SCALE_NEUTRAL + 16'h0400;
                s1_base_lpf_scale  <= SCALE_NEUTRAL;
                s1_base_diff_scale <= SCALE_NEU_DIFF - 8'h10;
                s1_mod_depth       <= 8'h20;
            end
            FAM_MOTION: begin
                // 모션: gain 중립, lpf 변조 강함, diff 강함
                s1_base_gain_scale <= SCALE_NEUTRAL;
                s1_base_lpf_scale  <= SCALE_NEUTRAL;
                s1_base_diff_scale <= SCALE_NEU_DIFF + 8'h18;
                s1_mod_depth       <= 8'h30;
            end
            FAM_SPECIAL: begin
                // 특수(shimmer/frozen): gain 높음, lpf 어두움, diff 최강
                s1_base_gain_scale <= SCALE_NEUTRAL + 16'h0800;
                s1_base_lpf_scale  <= SCALE_NEUTRAL + 16'h1000;
                s1_base_diff_scale <= SCALE_MAX_DIFF;
                s1_mod_depth       <= 8'h28;
            end
            FAM_GATE: begin
                // 게이트/앰비언스: gain 약함, lpf 밝음, diff 낮음
                s1_base_gain_scale <= SCALE_NEUTRAL - 16'h1000;
                s1_base_lpf_scale  <= SCALE_NEUTRAL - 16'h0800;
                s1_base_diff_scale <= SCALE_NEU_DIFF - 8'h20;
                s1_mod_depth       <= 8'h08;
            end
            default: begin  // FAM_CUSTOM 등
                s1_base_gain_scale <= SCALE_NEUTRAL;
                s1_base_lpf_scale  <= SCALE_NEUTRAL;
                s1_base_diff_scale <= SCALE_NEU_DIFF;
                s1_mod_depth       <= 8'h10;
            end
        endcase
    end
end

// ============================================================================
//  STAGE 2: modulation 계산 + priority arbitration
//  - 위상 카운터에서 삼각파 근사 (shift만 사용)
//  - mod_depth vs diffusion 충돌 방지: diff가 클수록 mod 억제
//  synth 8-6859: 전용 always 블록
//  HDL 9-1206: begin 블록 내 reg 상단 선언
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        s2_gain_scale <= SCALE_NEUTRAL;   // synth 8-6858
        s2_lpf_scale  <= SCALE_NEUTRAL;
        s2_diff_scale <= SCALE_NEU_DIFF;
    end else begin
        begin : stage2_mod_blk
            // --- 삼각파 LFO 근사 (shift/add only) ---
            // phase[23:16] = 0~255 주기 → 삼각파 [0..127..-127..0]
            reg [7:0]  tri_slow;   // slow LFO (gain/lpf 변조)
            reg [7:0]  tri_fast;   // fast LFO (diff 변조)
            reg [7:0]  mod_eff;    // 실효 mod depth (priority 조정 후)
            reg signed [15:0] gain_mod;
            reg signed [15:0] lpf_mod;
            reg [15:0] gain_result;
            reg [15:0] lpf_result;
            reg [7:0]  diff_result;

            // 삼각파: phase[22]가 0이면 상승, 1이면 하강
            tri_slow = phase_r[22] ? (~phase_r[21:14]) : phase_r[21:14];
            tri_fast = phase_fast_r[18] ? (~phase_fast_r[17:10]) :
                                           phase_fast_r[17:10];

            // per-line seed 미세 위상 오프셋 (line_idx × behavior_seed[3:0])
            // shift only, 오버플로우 무시 (wrapping 허용)
            tri_slow = tri_slow + {line_idx[3:0], behavior_seed[3:0]};

            // Priority arbitration:
            // diff가 크면 (s1_base_diff_scale > SCALE_NEU_DIFF+0x20) mod 억제
            if (s1_base_diff_scale > (SCALE_NEU_DIFF + 8'h20))
                mod_eff = s1_mod_depth >> 2;   // mod 1/4
            else if (s1_base_diff_scale > SCALE_NEU_DIFF)
                mod_eff = s1_mod_depth >> 1;   // mod 1/2
            else
                mod_eff = s1_mod_depth;

            // ER energy 기반 mod_eff 추가 감쇠
            // 에너지가 높으면 (공간 포화) → mod 더 줄임
            if (s1_energy_clamped > 8'h80)
                mod_eff = mod_eff >> 1;

            // gain 변조: base ± (tri_slow × mod_eff >> 8)
            gain_mod = $signed({1'b0, tri_slow}) *
                       $signed({1'b0, mod_eff}) >>> 8;
            gain_result = s1_base_gain_scale + gain_mod;

            // lpf 변조: base ± (tri_slow × mod_eff >> 9) [절반 깊이]
            lpf_mod  = $signed({1'b0, tri_slow}) *
                       $signed({1'b0, mod_eff}) >>> 9;
            lpf_result = s1_base_lpf_scale + lpf_mod;

            // diff 변조: tri_fast 기반 (독립 주기)
            diff_result = s1_base_diff_scale +
                          ((tri_fast * (s1_mod_depth >> 3)) >> 8);

            // clamp
            s2_gain_scale <= (gain_result > SCALE_MAX_GAIN) ? SCALE_MAX_GAIN :
                             (gain_result < SCALE_MIN_GAIN) ? SCALE_MIN_GAIN :
                              gain_result;

            s2_lpf_scale  <= (lpf_result > SCALE_MAX_LPF) ? SCALE_MAX_LPF :
                             (lpf_result < SCALE_MIN_LPF) ? SCALE_MIN_LPF :
                              lpf_result;

            s2_diff_scale <= (diff_result > SCALE_MAX_DIFF) ? SCALE_MAX_DIFF :
                             (diff_result < SCALE_MIN_DIFF) ? SCALE_MIN_DIFF :
                              diff_result;
        end
    end
end

// ============================================================================
//  STAGE 3: ER feedback 블렌딩 + 출력 레지스터
//  - ER density_dynamic이 높으면 → lpf 약간 어둡게 (공간 포화 모델)
//  - ER energy가 높으면 → gain scale 약간 억제 (발산 방지)
//  synth 8-6859: 전용 always 블록 (출력 FF 단일 드라이버)
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        behavior_gain_scale <= SCALE_NEUTRAL;   // synth 8-6858
        behavior_lpf_scale  <= SCALE_NEUTRAL;
        behavior_diff_scale <= SCALE_NEU_DIFF;
    end else begin
        begin : stage3_fb_blk
            reg [15:0] fb_gain;
            reg [15:0] fb_lpf;
            reg [7:0]  fb_diff;
            reg [7:0]  energy_adj;   // gain 억제량

            // ER energy → gain 억제 (발산 방지)
            // energy_clamped[7:4] 상위 4비트 → 억제량 (최대 0x0F00)
            energy_adj = s1_energy_clamped >> 4;
            fb_gain = s2_gain_scale - {8'h00, energy_adj};

            // ER density → lpf 약간 어둡게 (공간 포화)
            // density_dynamic[7:5] 상위 3비트 → lpf 오프셋
            fb_lpf = s2_lpf_scale + {10'h000, er_density_dynamic[7:5], 3'b000};

            fb_diff = s2_diff_scale;

            // 최종 clamp + 출력
            behavior_gain_scale <= (fb_gain > SCALE_MAX_GAIN) ? SCALE_MAX_GAIN :
                                   (fb_gain < SCALE_MIN_GAIN) ? SCALE_MIN_GAIN :
                                    fb_gain;

            behavior_lpf_scale  <= (fb_lpf > SCALE_MAX_LPF) ? SCALE_MAX_LPF :
                                   (fb_lpf < SCALE_MIN_LPF) ? SCALE_MIN_LPF :
                                    fb_lpf;

            behavior_diff_scale <= (fb_diff > SCALE_MAX_DIFF) ? SCALE_MAX_DIFF :
                                   (fb_diff < SCALE_MIN_DIFF) ? SCALE_MIN_DIFF :
                                    fb_diff;
        end
    end
end

endmodule