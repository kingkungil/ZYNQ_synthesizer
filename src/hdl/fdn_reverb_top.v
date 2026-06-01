// ============================================================================
//  fdn_reverb_top.v  v63s → v64 (Space Behavior Layer 통합)
//
//  v63s → v64 변경 목록 (v9 SPACE BEHAVIOR LAYER)
//  ─────────────────────────────────────────────────────────────────────────
//  [BHV-1]  space_behavior_engine.v 신규 인스턴스 (u_behavior)
//             space_id + family_id + behavior_seed + ER feedback
//             → behavior_gain_scale / behavior_lpf_scale / behavior_diff_scale
//             3-stage pipeline, shift/add only, BRAM 접근 금지
//
//  [BHV-2]  space_model_core 신규 출력 포트 연결
//             family_id_out, behavior_seed_out
//             → space_behavior_engine 입력으로 연결
//
//  [BHV-3]  space_er_engine 신규 출력 포트 연결
//             er_energy_feedback, er_density_dynamic
//             → space_behavior_engine feedback 입력으로 연결
//             → ER → behavior_engine → FDN 루프 (살아있는 공간)
//
//  [BHV-4]  fdn_space_table behavior scale 포트 연결
//             behavior_gain_scale / behavior_lpf_scale / behavior_diff_scale
//             → table base × scale = 실시간 색칠 (구조 변경 없음)
//
//  합성 안전:
//    HDL 9-1206: 모든 신규 인스턴스 포트 명시 연결
//    synth 8-6858: behavior_engine 내 모든 FF 리셋값 명시
//    synth 8-6859: behavior_engine 출력 전용 always 블록
//    타이밍: behavior output → register → fdn_space_table (fanout 제어)
//            behavior engine은 FSM critical path 외부 독립 동작
//
//  기존 구조 완전 보존:
//    FDN topology, fdn_space_table BRAM 구조, crossfade, space_er_engine
//    space_model_core LUT, AXI 레지스터 맵 변경 없음
//
//
//  v63 → v63s 변경 목록
//  ─────────────────────────────────────────────────────────────────────────
//  [STEREO-IN-1]  입력 포트 스테레오 분리
//                 s_axis_tdata (mono) → s_axis_l_tdata / s_axis_r_tdata
//                 s_axis_tvalid / s_axis_tready 는 그대로 유지 (동기화 공용)
//
//  [STEREO-IN-2]  Mid/Side 생성 (combinational wire)
//                 x_mid  = (L+R)/2 → ER, shimmer, gate, dry-mix (mono 경로 유지)
//                 x_side = (L-R)/2 → 출력 width 복원용
//
//  [STEREO-IN-3]  Cross Injection (FDN 입력 분리)
//                 si_fdn_in_l = L + cross*R  (DSP, 파이프 1단)
//                 si_fdn_in_r = R + cross*L
//                 → FS_PRE_DIFF sub9: x_prediff/r에 적용
//                 → ER(space_er_engine)은 x_pd(L-predelay) 기반 유지 (변경 없음)
//
//  [STEREO-IN-4]  Predelay 분리
//                 pd_l_mem ← x_hold_l (L)
//                 pd_r_mem ← x_hold_r (R)
//                 pd_b_mem ← x_hold_r (SPACE_DUAL B채널)
//                 er_mem   ← x_mid    (공간 물리 모델은 mono)
//
//  [STEREO-IN-5]  출력 Side 복원 (FS_WIDTH SPACE_SINGLE 케이스)
//                 sw_side_sc_total = fdn_side + (x_side * reg_in_width)
//                 앞단 스테레오 공간감 보존
//
//  [STEREO-IN-6]  AXI 레지스터 추가
//                 0x76 (word): reg_in_cross  Q1.15 cross injection 계수 (초기값 0x2000)
//                 0x77 (word): reg_in_width  Q1.15 side 복원 계수     (초기값 0x4000)
//
//  내부 모듈 변경 없음:
//    space_er_engine, geometry_engine, fdn_space_table,
//    inv_rt60_calc, shimmer_pitch, gate_env, Hadamard
//
//  v63 이전 변경 목록 유지
//  ─────────────────────────────────────────────────────────────────────────
//  [SYNC-1]  reg_src_z, reg_lst_z 레지스터 추가
//            AXI 0x68: {src_z[15:0], lst_z[15:0]}
//            → space_er_engine v3 src_z/lst_z 포트 연결
//            → geometry_engine v6 진정한 3D 거리 계산 활성화
//
//  [SYNC-2]  공간 전환 mute (space_start 시 er_out 즉시 클리어)
//            v60: er_out_clr_r 1-cycle pulse만 사용
//            v63: er_out_clr_r 유지 + space_er_engine 내부 mute
//                 → pop/click 방지
//
//  [SYNC-3]  공간별 src_z/lst_z 기본값 자동 설정
//            preset 로드 시 room_z / 2 로 src_z 자동 설정
//
//  v59 → v60 변경 목록
//  ─────────────────────────────────────────────────────────────────────────
//  [DYN-1]  FDN Energy Monitor 추가
//             fdn_out_l/r 8라인 절대값 합산 → IIR 1-pole smoothing
//             → fdn_energy_l/r [15:0] Q1.15 생성
//             → fdn_space_table v5 energy_l/r 포트에 연결
//             IIR: energy = energy - (energy>>7) + abs_sum
//             abs_sum: line 0~7(L), 8~15(R) 절대값 합산 >> 4 (정규화)
//
//  [DYN-2]  ER Density Counter 추가
//             ER 유효 출력(er_phys_valid) pulse 카운트
//             256-sample sliding window → 8bit density
//             er_density_r = hit_count, window = 256 samples
//             → fdn_space_table v5 er_density 포트에 연결
//
//  [DYN-3]  fdn_space_table 인스턴스 포트 확장
//             v59: energy_l/r, er_density 미연결 (포트 자체 없음)
//             v60: 3개 신호 모두 연결
//             → table의 dynamic overlay (delay/gain/lpf) 완전 활성화
//
//  [DYN-4]  spc_gain_out / spc_lpf_out 실제 활용
//             v59: 출력 wire만 선언, 실제 FDN 경로에 미적용
//             v60: FS_RTGAIN에서 rtg_g_t 산출 후
//                  spc_gain_out (dynamic gain clamp)를 상한으로 사용
//                  → FDN gain이 공간 에너지 상태에 반응
//             v60: cur_dyn_lpf_alpha 계산 시 spc_lpf_out 블렌딩
//                  → 공간 포화 시 LPF 자동 강화
//
//  [DYN-5]  AXI read 추가 (0x73~0x75) - 디버그용
//             0x73: fdn_energy_l [15:0]
//             0x74: fdn_energy_r [15:0]
//             0x75: er_density_r [7:0]
//
//  합성 안전성:
//    HDL 9-1206 방지: named block 내 reg 상단 선언 준수
//    synth 8-6858 방지: 모든 신규 FF 리셋값 명시
//    synth 8-6859 방지: 단일 드라이버 유지 (energy/density 전용 always)
//    타이밍: energy/density 계산은 독립 always 블록 (FSM critical path 외부)
//
//  v58 → v59 변경 목록 (유지)
//  ─────────────────────────────────────────────────────────────────────────
//  [FIX-6]  HDL 9-1206 완전 수정:
//              fdn_space_table 인스턴스에서 누락된 4개 출력 포트 연결
//              gain_out, lpf_alpha_out, diff_out, mod_depth_out
//              → spc_gain_out, spc_lpf_out, spc_diff_out, spc_mod_out wire 추가
//  [FIX-7]  space_id 포트 비트폭 불일치 수정:
//              reg_space_preset[3:0] (4비트) → reg_space_preset (5비트 전체)
//              fdn_space_table.space_id는 [4:0] 5비트 포트임
//  [FIX-8]  fdn_space_table 내부 addr_old forward-reference 수정:
//              old_space_r가 BRAM read always 블록보다 늦게 선언되어
//              Vivado가 wire로 처리 → reg 선언을 BRAM read 블록 앞으로 이동
//
//  v57 → v58 변경 목록 (유지)
//  ─────────────────────────────────────────────────────────────────────────
//  [FIX-3]  reg_geo_start 멀티드라이브 완전 제거:
//              "geo_start 1-cycle pulse 클리어" 전용 always 블록 삭제
//              AXI write always 블록의 else-begin 최상단에서
//              reg_geo_start <= 1'b0 매 사이클 클리어 (단일 드라이버)
//              space_change_pulse_r / er_out_clr_r 과 동일 패턴으로 통일
//  [FIX-4]  rtg_sub 비트폭 상수 통일:
//              rtg_sub를 reg [4:0]로 선언했으나 case 분기가
//              4'd0~4'd13(암묵적 4비트)과 5'd17(5비트)을 혼용
//              → 모든 case 레이블을 5'd0 ~ 5'd17로 명시적 5비트 통일
//              → FSM reset에서 rtg_sub <= 5'd0 명시
//  [FIX-5]  FS_RTGAIN 진입 시 rtg_sub <= 5'd0 명시
//              FS_ER_DIFF sub3에서 state<=FS_RTGAIN 전환 시 누락 방지
//  [CLN-1]  AXI reset 블록에 reg_geo_start <= 1'b0 명시 추가
//              (FIX-3과 함께 단일 드라이버 보장)
//
//  v56 → v57 변경 목록 (유지)
//  ─────────────────────────────────────────────────────────────────────────
//  [FIX-1]  Synth 8-6859 다중 드라이브 수정 (pl_wr_handler 통합)
//  [FIX-2]  Synth 8-6858 리셋 초기값 중복 수정
//  [STR-2]  fdn_space_table BRAM 기반 24공간 FDN 구조 통합
//  [BUG-6]  FS_RTGAIN sub0: cur_fdn_delay 사용
//  [TIM-8]  FS_RTGAIN sub11/sub17 분리
//  [BUG-4]  FS_PRE_DIFF sub9: x_prediff_r에 x_pd_r 직접 연결
//  [TIM-2]  FS_FDN_READ sub52: gain_wr 중복 write 제거
//  [TIM-1]  mod_depth_eff wire 즉시 계산
//  [BUG-5]  er_out_l/r 잔류값 클리어 플래그
//  [BUG-7]  rt60_dirty space/rtg 우선순위 처리
//  [BUG-9]  SPACE_DUAL FS_WIDTH stereo width 적용
//  [TIM-7]  LFO 부호 반전 -32768 오버플로우 방지
// ============================================================================

`timescale 1ns / 1ps

module fdn_reverb_top #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 10,  // [FIX-ADDR] 8->10: addr truncation fix
    parameter integer DL_SLOT_BITS       = 10,
    parameter integer MAX_PREDELAY       = 2400
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI:S_AXIS_L:S_AXIS_R:M_AXIS_L:M_AXIS_R:M_AXIS_DMA, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
    input  wire                              S_AXI_ACLK,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 S_AXI_ARESETN RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                              S_AXI_ARESETN,
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]    S_AXI_AWADDR,
    input  wire [2:0]                        S_AXI_AWPROT,
    input  wire                              S_AXI_AWVALID,
    output reg                               S_AXI_AWREADY,
    input  wire [C_S_AXI_DATA_WIDTH-1:0]    S_AXI_WDATA,
    input  wire [3:0]                        S_AXI_WSTRB,
    input  wire                              S_AXI_WVALID,
    output reg                               S_AXI_WREADY,
    output reg  [1:0]                        S_AXI_BRESP,
    output reg                               S_AXI_BVALID,
    input  wire                              S_AXI_BREADY,
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]    S_AXI_ARADDR,
    input  wire [2:0]                        S_AXI_ARPROT,
    input  wire                              S_AXI_ARVALID,
    output reg                               S_AXI_ARREADY,
    output reg  [C_S_AXI_DATA_WIDTH-1:0]    S_AXI_RDATA,
    output reg  [1:0]                        S_AXI_RRESP,
    output reg                               S_AXI_RVALID,
    input  wire                              S_AXI_RREADY,
    // LEFT AXI-Stream input
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_L TVALID" *)
    input  wire                              s_axis_l_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_L TDATA" *)
    input  wire signed [15:0]               s_axis_l_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_L TREADY" *)
    output wire                              s_axis_l_tready,
    // RIGHT AXI-Stream input
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_R TVALID" *)
    input  wire                              s_axis_r_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_R TDATA" *)
    input  wire signed [15:0]               s_axis_r_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_R TREADY" *)
    output wire                              s_axis_r_tready,
    // LEFT AXI-Stream output
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_L TVALID" *)
    output reg                               m_axis_l_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_L TDATA" *)
    output reg  signed [15:0]               m_axis_l_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_L TREADY" *)
    input  wire                              m_axis_l_tready,
    // RIGHT AXI-Stream output
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_R TVALID" *)
    output reg                               m_axis_r_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_R TDATA" *)
    output reg  signed [15:0]               m_axis_r_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_R TREADY" *)
    input  wire                              m_axis_r_tready,
    // ── DMA 32bit 스테레오 합산 출력 ────────────────────────────────────
    // TDATA[31:16] = R ch (signed Q1.15)
    // TDATA[15:0]  = L ch (signed Q1.15)
    // PS AXI DMA (S2MM) 직결용: 샘플당 1회 pulse, TLAST 없음(연속 스트림)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_DMA TVALID" *)
    output reg                               m_axis_dma_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_DMA TDATA" *)
    output reg  [31:0]                       m_axis_dma_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_DMA TREADY" *)
    input  wire                              m_axis_dma_tready,
    input  wire signed [15:0]               lfo_in_0,
    input  wire signed [15:0]               lfo_in_1,
    input  wire signed [15:0]               lfo_in_2,
    input  wire signed [15:0]               lfo_in_3,
    input  wire signed [15:0]               lfo_in_4,
    input  wire signed [15:0]               lfo_in_5,
    input  wire signed [15:0]               lfo_in_6,
    input  wire signed [15:0]               lfo_in_7
);

// ============================================================================
//  로컬 파라미터
// ============================================================================
localparam integer DATA_W     = 16;
localparam integer COEFF_W    = 32;
localparam integer COEFF_FRAC = 24;
localparam integer ACC_W      = 48;
localparam integer NUM_L      = 8;
localparam integer NUM_R      = 8;
localparam integer NUM_LINES  = 16;

localparam integer DL_SLOT    = (1 << DL_SLOT_BITS);
localparam integer DL_TOTAL   = NUM_LINES * DL_SLOT;
localparam integer DL_ADDR_W  = DL_SLOT_BITS + 4;
localparam integer PD_ADDR_W  = 12;

localparam [1:0] SPACE_SINGLE = 2'd0;
localparam [1:0] SPACE_DUAL   = 2'd1;
localparam [1:0] SPACE_UNISON = 2'd2;

localparam [31:0] MAX_SPEC_RADIUS = 32'h00F33333;
localparam [31:0] MAX_DECAY_GAIN  = 32'h00E147AE;
localparam [31:0] MAX_FB_GAIN     = 32'h00E147AE;
localparam [31:0] GAIN_FLOOR      = 32'h00000100;
localparam [31:0] CROSS_MAX_Q24   = 32'h00200000;
localparam [31:0] SHIMMER_MAX_Q24 = 32'h004CCCCC;
localparam [31:0] ONE_Q24         = 32'h01000000;

// [BUG-2] Dynamic LPF env 기여분 상한 (25%)
localparam [31:0] ENV_ALPHA_CAP   = 32'h00400000;

localparam [31:0] P97_Q24  = 32'h00F7AE14;
localparam [31:0] P95_Q24  = 32'h00F33333;
localparam [31:0] P92_Q24  = 32'h00EB851F;
localparam [31:0] P91_Q24  = 32'h00E8F5C3;
localparam [31:0] P90_Q24  = 32'h00E66666;
localparam [31:0] P88_Q24  = 32'h00E147AE;
localparam [31:0] P85_Q24  = 32'h00D99999;
localparam [31:0] P80_Q24  = 32'h00CCCCCC;
localparam [31:0] P78_Q24  = 32'h00C7AE14;
localparam [31:0] P75_Q24  = 32'h00C00000;
localparam [31:0] P73_Q24  = 32'h00BAE148;
localparam [31:0] P71_Q24  = 32'h00B5C28F;
localparam [31:0] P70_Q24  = 32'h00B33333;
localparam [31:0] P65_Q24  = 32'h00A66666;
localparam [31:0] P63_Q24  = 32'h00A147AE;
localparam [31:0] P61_Q24  = 32'h009C28F6;
localparam [31:0] P60_Q24  = 32'h00999999;
localparam [31:0] P55_Q24  = 32'h008CCCCC;
localparam [31:0] P52_Q24  = 32'h00851EB8;
localparam [31:0] P50_Q24  = 32'h00800000;
localparam [31:0] P45_Q24  = 32'h00733333;
localparam [31:0] P40_Q24  = 32'h00666666;
localparam [31:0] P35_Q24  = 32'h00599999;
localparam [31:0] P30_Q24  = 32'h004CCCCC;
localparam [31:0] P25_Q24  = 32'h00400000;
localparam [31:0] P20_Q24  = 32'h00333333;
localparam [31:0] P12_Q24  = 32'h00200000;
localparam [31:0] P10_Q24  = 32'h00199999;
localparam [31:0] P06_Q24  = 32'h000F5C29;
localparam [31:0] P03_Q24  = 32'h00080000;

// LPF alpha 상수
localparam [31:0] LPF_BRIGHT=32'h00400000; localparam [31:0] LPF_LIGHT =32'h00200000;
localparam [31:0] LPF_MID   =32'h006AAAAA; localparam [31:0] LPF_DARK  =32'h00955555;
localparam [31:0] LPF_VDARK =32'h00C00000; localparam [31:0] HPF_STD   =32'h00100000;
localparam [31:0] HPF_WEAK  =32'h00080000;

// Space Preset ID
localparam [4:0]
    SP_AMBIENCE      = 5'd0,
    SP_HALL          = 5'd1,
    SP_CATHEDRAL     = 5'd2,
    SP_CHURCH        = 5'd3,
    SP_SMALL_ROOM    = 5'd4,
    SP_MEDIUM_ROOM   = 5'd5,
    SP_LARGE_ROOM    = 5'd6,
    SP_PLATE_CLASSIC = 5'd7,
    SP_PLATE_BRIGHT  = 5'd8,
    SP_PLATE_DARK    = 5'd9,
    SP_RESONANCE     = 5'd10,
    SP_RESONANT_SP   = 5'd11,
    SP_TONAL         = 5'd12,
    SP_RINGING       = 5'd13,
    SP_ROTATING      = 5'd14,
    SP_DRIFTING      = 5'd15,
    SP_GATED         = 5'd16,
    SP_REVERSE       = 5'd17,
    SP_BROKEN        = 5'd18,
    SP_SHIMMER       = 5'd19,
    SP_FROZEN        = 5'd20,
    SP_ASCENDING     = 5'd21,
    SP_DESCENDING    = 5'd22,
    SP_SPECIAL       = 5'd23,
    SP_CUSTOM        = 5'd31;

// 3-band RT60 상수
localparam [31:0] RT60_HALL_L = 32'h00F0CCCC;
localparam [31:0] RT60_HALL_M = 32'h00E14000;
localparam [31:0] RT60_HALL_H = 32'h00C7AE14;
localparam [31:0] RT60_CAT_L  = 32'h00FF8000;
localparam [31:0] RT60_CAT_M  = 32'h00F5C28F;
localparam [31:0] RT60_CAT_H  = 32'h00D47AE1;
localparam [31:0] RT60_CHR_L  = 32'h00F5C28F;
localparam [31:0] RT60_CHR_M  = 32'h00EB851F;
localparam [31:0] RT60_CHR_H  = 32'h00D99999;
localparam [31:0] RT60_PLC_L  = 32'h00E80000;
localparam [31:0] RT60_PLC_M  = 32'h00E80000;
localparam [31:0] RT60_PLC_H  = 32'h00E60000;
localparam [31:0] RT60_PLB_L  = 32'h00E00000;
localparam [31:0] RT60_PLB_M  = 32'h00DF0000;
localparam [31:0] RT60_PLB_H  = 32'h00DE0000;
localparam [31:0] RT60_PLD_L  = 32'h00F00000;
localparam [31:0] RT60_PLD_M  = 32'h00E80000;
localparam [31:0] RT60_PLD_H  = 32'h00CC0000;
localparam [31:0] RT60_SR_L   = 32'h00C73999;
localparam [31:0] RT60_SR_M   = 32'h00C20000;
localparam [31:0] RT60_SR_H   = 32'h00B40000;
localparam [31:0] RT60_MR_L   = 32'h00D80000;
localparam [31:0] RT60_MR_M   = 32'h00D20000;
localparam [31:0] RT60_MR_H   = 32'h00C60000;
localparam [31:0] RT60_LR_L   = 32'h00E60000;
localparam [31:0] RT60_LR_M   = 32'h00E00000;
localparam [31:0] RT60_LR_H   = 32'h00D60000;
localparam [31:0] RT60_AMB_L  = 32'h00CF0000;
localparam [31:0] RT60_AMB_M  = 32'h00C80000;
localparam [31:0] RT60_AMB_H  = 32'h00BE0000;
localparam [31:0] RT60_SHM_L  = 32'h00F60000;
localparam [31:0] RT60_SHM_M  = 32'h00F10000;
localparam [31:0] RT60_SHM_H  = 32'h00E40000;
localparam [31:0] RT60_GAT_L  = 32'h00DA0000;
localparam [31:0] RT60_GAT_M  = 32'h00D60000;
localparam [31:0] RT60_GAT_H  = 32'h00CF0000;
localparam [31:0] RT60_RNG_L  = 32'h00F30000;
localparam [31:0] RT60_RNG_M  = 32'h00F28000;
localparam [31:0] RT60_RNG_H  = 32'h00F00000;

// ER density/spread 상수
localparam [15:0] ER_DENSE   = 18'h00F0;
localparam [15:0] ER_HIGH    = 18'h00C0;
localparam [15:0] ER_MED     = 18'h0080;
localparam [15:0] ER_LOW     = 18'h0040;
localparam [15:0] ER_SPARSE  = 18'h0020;
localparam [15:0] SPREAD_NARROW = 18'h0030;
localparam [15:0] SPREAD_MED    = 18'h0070;
localparam [15:0] SPREAD_WIDE   = 18'h00C0;
localparam [15:0] SPREAD_VWIDE  = 18'h0100;

// Diffusion profile 상수
localparam [15:0] DIFF_SPARSE_16 = 18'h2000;
localparam [15:0] DIFF_NORMAL_16 = 18'h0000;
localparam [15:0] DIFF_DENSE_16  = 18'h4000;
localparam [15:0] DIFF_VDENSE_16 = 18'h6000;

// Spectral tilt LPF 상수
localparam [31:0] SPEC_VDARK  = 32'h00333333;
localparam [31:0] SPEC_DARK   = 32'h00566666;
localparam [31:0] SPEC_MID    = 32'h006AAAAA;
localparam [31:0] SPEC_LIGHT  = 32'h008B3333;
localparam [31:0] SPEC_BRIGHT = 32'h009CCCCC;
localparam [31:0] SPEC_VBRIGHT= 32'h00AAE147;
localparam [31:0] SPEC_SPRING = 32'h00C00000;

localparam [31:0] TAP_L0 = 32'h00EB851F;
localparam [31:0] TAP_L3 = 32'h00BAE148;
localparam [31:0] TAP_L5 = 32'h009C28F6;
localparam [31:0] TAP_L7 = 32'h008CCCCC;
localparam [31:0] TAP_R1 = 32'h00E8F5C3;
localparam [31:0] TAP_R2 = 32'h00B5C28F;
localparam [31:0] TAP_R4 = 32'h00A147AE;
localparam [31:0] TAP_R6 = 32'h00851EB8;

localparam signed [15:0] INJ_G0 = 16'sh4CCC;
localparam signed [15:0] INJ_G1 = 16'shCCCD;
localparam signed [15:0] INJ_G2 = 16'sh5999;
localparam signed [15:0] INJ_G3 = 16'shD999;
localparam signed [15:0] INJ_G4 = 16'sh6666;
localparam signed [15:0] INJ_G5 = 16'shB334;
localparam signed [15:0] INJ_G6 = 16'sh3333;
localparam signed [15:0] INJ_G7 = 16'shE000;

localparam [8:0] MOD_SCL0 = 9'd200; localparam [8:0] MOD_SCL1 = 9'd300;
localparam [8:0] MOD_SCL2 = 9'd160; localparam [8:0] MOD_SCL3 = 9'd350;
localparam [8:0] MOD_SCL4 = 9'd220; localparam [8:0] MOD_SCL5 = 9'd280;
localparam [8:0] MOD_SCL6 = 9'd140; localparam [8:0] MOD_SCL7 = 9'd320;

localparam [3:0] MICRO_SCL_LO = 4'd2;
localparam [3:0] MICRO_SCL_HI = 4'd3;

localparam [DL_SLOT_BITS-1:0]
    FDN_LL0=10'd503, FDN_LL1=10'd599, FDN_LL2=10'd691, FDN_LL3=10'd769,
    FDN_LL4=10'd877, FDN_LL5=10'd929, FDN_LL6=10'd967, FDN_LL7=10'd1013;
localparam [DL_SLOT_BITS-1:0]
    FDN_RL0=10'd523, FDN_RL1=10'd607, FDN_RL2=10'd719, FDN_RL3=10'd809,
    FDN_RL4=10'd883, FDN_RL5=10'd947, FDN_RL6=10'd983, FDN_RL7=10'd1019;

localparam [31:0] EXP_INT_00=32'h01000000; localparam [31:0] EXP_INT_01=32'h005E2D58;
localparam [31:0] EXP_INT_02=32'h0022A555; localparam [31:0] EXP_INT_03=32'h000CBED8;
localparam [31:0] EXP_INT_04=32'h0004B055; localparam [31:0] EXP_INT_05=32'h0001B993;
localparam [31:0] EXP_INT_06=32'h0000A272; localparam [31:0] EXP_INT_07=32'h00003BC2;
localparam [31:0] EXP_INT_08=32'h000015FC; localparam [31:0] EXP_INT_09=32'h00000816;
localparam [31:0] EXP_INT_10=32'h000002F9; localparam [31:0] EXP_INT_11=32'h00000118;

localparam [31:0] EXP_8TH_0=32'h01000000; localparam [31:0] EXP_8TH_1=32'h00E1EB51;
localparam [31:0] EXP_8TH_2=32'h00C75F7C; localparam [31:0] EXP_8TH_3=32'h00AFF230;
localparam [31:0] EXP_8TH_4=32'h009B4597; localparam [31:0] EXP_8TH_5=32'h008906E4;
localparam [31:0] EXP_8TH_6=32'h0078ED03; localparam [31:0] EXP_8TH_7=32'h006AB778;

localparam [3:0] RT_RESONATOR=4'd0; localparam [3:0] RT_ROOM   =4'd1;
localparam [3:0] RT_HALL     =4'd2; localparam [3:0] RT_PLATE  =4'd3;
localparam [3:0] RT_SPRING   =4'd4; localparam [3:0] RT_CHAMBER=4'd5;
localparam [3:0] RT_CATHEDRAL=4'd6; localparam [3:0] RT_GATED  =4'd7;
localparam [3:0] RT_REVERSE  =4'd8; localparam [3:0] RT_CUSTOM =4'd9;

localparam [4:0]
    FS_IDLE=5'd0,  FS_PREDELAY=5'd1, FS_ER=5'd2,    FS_PRE_DIFF=5'd3,
    FS_ER_DIFF=5'd4, FS_RTGAIN=5'd5, FS_FDN_READ=5'd6, FS_HAD_WAIT=5'd7,
    FS_FDN_WRITE=5'd8, FS_TAP_SUM=5'd9, FS_POST_DIFF=5'd10,
    FS_GATE_PROC=5'd11, FS_MIX=5'd12, FS_WIDTH=5'd13,
    FS_OUTPUT=5'd14, FS_DRAIN=5'd15;

localparam [3:0] FSEL_LP=4'd0; localparam [3:0] FSEL_HP =4'd1;
localparam [3:0] FSEL_BX1=4'd2;localparam [3:0] FSEL_BX2=4'd3;
localparam [3:0] FSEL_BY1=4'd4;localparam [3:0] FSEL_BY2=4'd5;
localparam [3:0] FSEL_DC =4'd6;localparam [3:0] FSEL_LPH=4'd7;
localparam [3:0] FSEL_LPA=4'd8;

localparam [2:0] GTYPE_GAIN   =3'd0; localparam [2:0] GTYPE_GAIN_LF=3'd1;
localparam [2:0] GTYPE_GAIN_MF=3'd2; localparam [2:0] GTYPE_GAIN_HF=3'd3;
localparam [2:0] GTYPE_MOD_DLY=3'd4;

localparam integer DIFF_ADDR_W = 12;
localparam [DIFF_ADDR_W-1:0] PRE_BASE0=12'd0,    PRE_BASE1=12'd18,
    PRE_BASE2=12'd48,   PRE_BASE3=12'd92,   PRE_BASE4=12'd160,
    POSTL_BASE0=12'd258,POSTL_BASE1=12'd408,POSTL_BASE2=12'd620,
    POSTL_BASE3=12'd898,POSTL_BASE4=12'd1236,
    POSTR_BASE0=12'd1638,POSTR_BASE1=12'd1788,POSTR_BASE2=12'd2000,
    POSTR_BASE3=12'd2278,POSTR_BASE4=12'd2616,
    ERDIFF_BASE0=12'd3018, ERDIFF_BASE1=12'd3086;
localparam [DIFF_ADDR_W-1:0]
    PRE_MAX0=12'd17,  PRE_MAX1=12'd29,  PRE_MAX2=12'd43,
    PRE_MAX3=12'd67,  PRE_MAX4=12'd97,  POST_MAX0=12'd149,
    POST_MAX1=12'd211,POST_MAX2=12'd277,POST_MAX3=12'd337,
    POST_MAX4=12'd401,ERDIFF_MAX0=12'd67,ERDIFF_MAX1=12'd97;

// ============================================================================
//  유틸리티 함수
// ============================================================================
function signed [DATA_W-1:0] sat16;
    input signed [ACC_W-1:0] v;
    begin
        if      (v >  48'sh000000007FFF) sat16 =  16'h7FFF;
        else if (v < -48'sh000000008000) sat16 = -16'h8000;
        else                             sat16  = v[DATA_W-1:0];
    end
endfunction

function [DL_SLOT_BITS-1:0] dl_wrap_sub;
    input [DL_SLOT_BITS-1:0] ptr, sub;
    begin
        if (ptr >= sub) dl_wrap_sub = ptr - sub;
        else            dl_wrap_sub = ptr + DL_SLOT[DL_SLOT_BITS-1:0] - sub;
    end
endfunction

function [12:0] er_wrap_sub;
    input [12:0] ptr, sub;
    begin
        if (ptr >= sub) er_wrap_sub = ptr - sub;
        else            er_wrap_sub = ptr + 13'd4096 - sub;
    end
endfunction

function [31:0] exp_int_lut;
    input [3:0] k;
    begin
        case (k)
            4'd0:  exp_int_lut=EXP_INT_00; 4'd1:  exp_int_lut=EXP_INT_01;
            4'd2:  exp_int_lut=EXP_INT_02; 4'd3:  exp_int_lut=EXP_INT_03;
            4'd4:  exp_int_lut=EXP_INT_04; 4'd5:  exp_int_lut=EXP_INT_05;
            4'd6:  exp_int_lut=EXP_INT_06; 4'd7:  exp_int_lut=EXP_INT_07;
            4'd8:  exp_int_lut=EXP_INT_08; 4'd9:  exp_int_lut=EXP_INT_09;
            4'd10: exp_int_lut=EXP_INT_10;
            default: exp_int_lut=EXP_INT_11;
        endcase
    end
endfunction

function [31:0] exp_8th_lut;
    input [2:0] i;
    begin
        case (i)
            3'd0: exp_8th_lut=EXP_8TH_0; 3'd1: exp_8th_lut=EXP_8TH_1;
            3'd2: exp_8th_lut=EXP_8TH_2; 3'd3: exp_8th_lut=EXP_8TH_3;
            3'd4: exp_8th_lut=EXP_8TH_4; 3'd5: exp_8th_lut=EXP_8TH_5;
            3'd6: exp_8th_lut=EXP_8TH_6;
            default: exp_8th_lut=EXP_8TH_7;
        endcase
    end
endfunction

function [DL_SLOT_BITS-1:0] fdn_delay_sel;
    input [3:0] ln;
    begin
        case (ln)
            4'd0: fdn_delay_sel=FDN_LL0; 4'd1: fdn_delay_sel=FDN_LL1;
            4'd2: fdn_delay_sel=FDN_LL2; 4'd3: fdn_delay_sel=FDN_LL3;
            4'd4: fdn_delay_sel=FDN_LL4; 4'd5: fdn_delay_sel=FDN_LL5;
            4'd6: fdn_delay_sel=FDN_LL6; 4'd7: fdn_delay_sel=FDN_LL7;
            4'd8: fdn_delay_sel=FDN_RL0; 4'd9: fdn_delay_sel=FDN_RL1;
            4'd10:fdn_delay_sel=FDN_RL2; 4'd11:fdn_delay_sel=FDN_RL3;
            4'd12:fdn_delay_sel=FDN_RL4; 4'd13:fdn_delay_sel=FDN_RL5;
            4'd14:fdn_delay_sel=FDN_RL6; default:fdn_delay_sel=FDN_RL7;
        endcase
    end
endfunction

function signed [15:0] inj_gain_sel;
    input [2:0] i;
    begin
        case (i)
            3'd0:inj_gain_sel=INJ_G0; 3'd1:inj_gain_sel=INJ_G1;
            3'd2:inj_gain_sel=INJ_G2; 3'd3:inj_gain_sel=INJ_G3;
            3'd4:inj_gain_sel=INJ_G4; 3'd5:inj_gain_sel=INJ_G5;
            3'd6:inj_gain_sel=INJ_G6; default:inj_gain_sel=INJ_G7;
        endcase
    end
endfunction

function [8:0] mod_scale_sel;
    input [2:0] i;
    begin
        case (i)
            3'd0:mod_scale_sel=MOD_SCL0; 3'd1:mod_scale_sel=MOD_SCL1;
            3'd2:mod_scale_sel=MOD_SCL2; 3'd3:mod_scale_sel=MOD_SCL3;
            3'd4:mod_scale_sel=MOD_SCL4; 3'd5:mod_scale_sel=MOD_SCL5;
            3'd6:mod_scale_sel=MOD_SCL6; default:mod_scale_sel=MOD_SCL7;
        endcase
    end
endfunction

function [1:0] apf_shift_sel;
    input [3:0] ln;
    begin
        case (ln[3:1])
            3'd0,3'd1: apf_shift_sel=2'd1;
            3'd2,3'd3: apf_shift_sel=2'd2;
            3'd4,3'd5: apf_shift_sel=2'd2;
            default:   apf_shift_sel=2'd3;
        endcase
    end
endfunction

function apf_line_en;
    input [3:0] ln;
    begin apf_line_en = ~ln[0]; end
endfunction

function [2:0] apf_idx;
    input [3:0] ln;
    begin apf_idx = ln[3:1]; end
endfunction

function [DIFF_ADDR_W-1:0] diff_base;
    input [4:0] stage;
    begin
        case (stage)
            5'd0:  diff_base=PRE_BASE0;   5'd1:  diff_base=PRE_BASE1;
            5'd2:  diff_base=PRE_BASE2;   5'd3:  diff_base=PRE_BASE3;
            5'd4:  diff_base=PRE_BASE4;   5'd5:  diff_base=POSTL_BASE0;
            5'd6:  diff_base=POSTL_BASE1; 5'd7:  diff_base=POSTL_BASE2;
            5'd8:  diff_base=POSTL_BASE3; 5'd9:  diff_base=POSTL_BASE4;
            5'd10: diff_base=POSTR_BASE0; 5'd11: diff_base=POSTR_BASE1;
            5'd12: diff_base=POSTR_BASE2; 5'd13: diff_base=POSTR_BASE3;
            5'd14: diff_base=POSTR_BASE4;
            5'd15: diff_base=ERDIFF_BASE0;
            default: diff_base=ERDIFF_BASE1;
        endcase
    end
endfunction

function [DIFF_ADDR_W-1:0] diff_maxidx;
    input [4:0] stage;
    begin
        case (stage)
            5'd0:  diff_maxidx=PRE_MAX0;   5'd1:  diff_maxidx=PRE_MAX1;
            5'd2:  diff_maxidx=PRE_MAX2;   5'd3:  diff_maxidx=PRE_MAX3;
            5'd4:  diff_maxidx=PRE_MAX4;   5'd5:  diff_maxidx=POST_MAX0;
            5'd6:  diff_maxidx=POST_MAX1;  5'd7:  diff_maxidx=POST_MAX2;
            5'd8:  diff_maxidx=POST_MAX3;  5'd9:  diff_maxidx=POST_MAX4;
            5'd10: diff_maxidx=POST_MAX0;  5'd11: diff_maxidx=POST_MAX1;
            5'd12: diff_maxidx=POST_MAX2;  5'd13: diff_maxidx=POST_MAX3;
            5'd14: diff_maxidx=POST_MAX4;
            5'd15: diff_maxidx=ERDIFF_MAX0;
            default: diff_maxidx=ERDIFF_MAX1;
        endcase
    end
endfunction

// ============================================================================
//  [P2] Space Model Decoder 함수 제거
//  sm_rt60_low/mid/high, sm_er_density, sm_er_spread, sm_diff_pre/post, sm_lpf,
//  density_to_cluster_thr → space_model_core.v 로 이전됨
// ============================================================================


// ============================================================================
//  AXI 레지스터
// ============================================================================
reg [3:0]  reg_reverb_type;
reg        reg_enabled, reg_hpf_enable, reg_bq_enable, reg_cross_enable;
reg [31:0] reg_lpf_alpha, reg_hpf_alpha;
reg [15:0] reg_mod_depth;
reg [31:0] reg_wet_mix, reg_dry_mix, reg_er_level, reg_er_lpf;
reg [15:0] reg_pre_diff_g, reg_post_diff_g;
reg [31:0] reg_decay_gain, reg_stereo_width;
reg [15:0] reg_rt60_ms;
reg [31:0] reg_shimmer_mix;
reg [15:0] reg_shimmer_pitch;
reg [7:0]  reg_res_mask;
reg [31:0] reg_res_boost;
reg [15:0] reg_gate_thresh, reg_gate_attack, reg_gate_hold, reg_gate_release;
reg [31:0] reg_bq_b0, reg_bq_b1, reg_bq_b2, reg_bq_a1, reg_bq_a2, reg_bq_gain;
reg [31:0] reg_fb_gain [0:7];
reg [7:0]  reg_custom_size;
reg [15:0] reg_custom_decay;
reg [31:0] reg_custom_material, reg_custom_er_blend, reg_custom_width;
reg [15:0] reg_custom_diffuse;
reg [1:0]  reg_space_mode;
reg [3:0]  reg_space_a_type, reg_space_b_type;

reg [31:0] reg_a_lpf_alpha;
reg [15:0] reg_a_mod_depth, reg_a_pre_diff_g, reg_a_post_diff_g;
reg [31:0] reg_a_er_level, reg_a_decay_gain;
reg [15:0] reg_a_rt60_ms;
reg [31:0] reg_a_stereo_width;

// 스테레오 predelay - L/R 독립
reg [15:0] reg_predelay_l;
reg [15:0] reg_predelay_r;

reg [31:0] reg_b_lpf_alpha;
reg [15:0] reg_b_mod_depth, reg_b_pre_diff_g, reg_b_post_diff_g;
reg [31:0] reg_b_er_level, reg_b_decay_gain;
reg [15:0] reg_b_rt60_ms;
reg [31:0] reg_b_stereo_width;
reg [15:0] reg_b_predelay;

reg [31:0] reg_cross_blend, reg_shimmer_blend;
reg [15:0] reg_inj_blend;
reg [31:0] reg_space_a_mix, reg_space_b_mix;
reg [15:0] reg_inj_blend_b;
reg [3:0]  reg_er_pattern_override;
reg [15:0] reg_reverse_ramp_rate;
reg [3:0]  reg_env_shift;
reg [31:0] reg_lpf_hf_alpha, reg_lpf_air_alpha;
reg        reg_micro_mod_en;

// Space preset 레지스터
reg [4:0]  reg_space_preset;
reg        reg_space_preset_en;
reg [31:0] reg_rt60_low_custom;
reg [31:0] reg_rt60_mid_custom;
reg [31:0] reg_rt60_high_custom;

// ER Physics 레지스터 (AXI 0x60~0x68)
reg [15:0] reg_room_x;
reg [15:0] reg_room_y;
reg [15:0] reg_room_z;
reg [15:0] reg_src_x;
reg [15:0] reg_src_y;
reg [15:0] reg_src_z;          // [SYNC-1] v63 신규
reg [15:0] reg_lst_x;
reg [15:0] reg_lst_y;
reg [15:0] reg_lst_z;          // [SYNC-1] v63 신규
reg [15:0] reg_cluster_thr;
reg [31:0] reg_er_inject_gain;
reg        reg_geo_en;
// [FIX-3] reg_geo_start: AXI write always 블록에서만 구동 (별도 always 블록 완전 제거)
reg        reg_geo_start;

// [STEREO-IN] 스테레오 입력 파라미터 레지스터
// reg_in_cross: 입력 cross-injection 계수 Q1.15 (0x0000~0x4000 권장, 초기값 0x2000=0.25)
// reg_in_width: 출력 side 복원 계수 Q1.15 (초기값 0x4000=0.5)
reg [15:0] reg_in_cross;
reg [15:0] reg_in_width;

// ============================================================================
//  [P2] SPACE_MODEL_CORE 인스턴스
//  space_id의 유일한 물리 파라미터 해석자
//  기존 sm_*_w wire들을 이 모듈 출력으로 대체
// ============================================================================
wire [31:0] smc_rt60_low_out;
wire [31:0] smc_rt60_mid_out;
wire [31:0] smc_rt60_high_out;
wire [15:0] smc_er_density_out;
wire [15:0] smc_er_spread_out;
wire [31:0] smc_lpf_out;
wire [15:0] smc_diff_pre_out;
wire [15:0] smc_diff_post_out;
wire [15:0] smc_cluster_thr_out;
wire [31:0] smc_inject_gain_out;
// [v9] behavior layer seed wires
wire [7:0]  smc_family_id_out;
wire [7:0]  smc_behavior_seed_out;

space_model_core u_space_model (
    .clk             (S_AXI_ACLK),
    .rst_n           (S_AXI_ARESETN),
    .space_id        (reg_space_preset),
    .preset_en       (reg_space_preset_en),
    .rt60_low_custom (reg_rt60_low_custom),
    .rt60_mid_custom (reg_rt60_mid_custom),
    .rt60_high_custom(reg_rt60_high_custom),
    .lpf_alpha_custom(reg_a_lpf_alpha),
    .pre_diff_custom (reg_a_pre_diff_g),
    .post_diff_custom(reg_a_post_diff_g),
    .rt60_low_out    (smc_rt60_low_out),
    .rt60_mid_out    (smc_rt60_mid_out),
    .rt60_high_out   (smc_rt60_high_out),
    .er_density_out  (smc_er_density_out),
    .er_spread_out   (smc_er_spread_out),
    .lpf_alpha_out   (smc_lpf_out),
    .diff_pre_out    (smc_diff_pre_out),
    .diff_post_out   (smc_diff_post_out),
    .cluster_thr_out (smc_cluster_thr_out),
    .inject_gain_out (smc_inject_gain_out),
    // [v9] behavior layer seed 출력
    .family_id_out      (smc_family_id_out),
    .behavior_seed_out  (smc_behavior_seed_out)
);

// [P2] space model 실효값 - SPACE_MODEL_CORE 출력으로 통일
// 기존 inline 함수 호출(sm_rt60_low() 등)을 smc_* 출력으로 대체
// 1-cycle 지연이 있으나 space_start 시 AXI 핸들러도 동일 사이클에 동작하므로 문제없음
// [P2] is_custom_mode → space_model_core 내부에서 처리됨, 제거

wire [31:0] sm_rt60_low_w  = smc_rt60_low_out;
wire [31:0] sm_rt60_mid_w  = smc_rt60_mid_out;
wire [31:0] sm_rt60_high_w = smc_rt60_high_out;
wire [15:0] sm_er_den_w    = smc_er_density_out;
wire [15:0] sm_er_spr_w    = smc_er_spread_out;
wire [15:0] sm_dpre_w      = smc_diff_pre_out;
wire [15:0] sm_dpost_w     = smc_diff_post_out;
wire [31:0] sm_lpf_w       = smc_lpf_out;

wire [15:0] eff_pre_diff_g  = reg_space_preset_en ? sm_dpre_w  : reg_a_pre_diff_g;
wire [15:0] eff_post_diff_g = reg_space_preset_en ? sm_dpost_w : reg_a_post_diff_g;
wire [31:0] eff_lpf_alpha   = reg_space_preset_en ? sm_lpf_w   : reg_a_lpf_alpha;

reg [15:0] er_density_r;
reg [15:0] er_spread_r;

// [FIX-DECL] AXI 읽기에서 참조되는 신호 - 선언 순서 수정
reg [15:0] fdn_energy_l;   // Q1.15 FDN 에너지 L (AXI 0x73)
reg [15:0] fdn_energy_r;   // Q1.15 FDN 에너지 R (AXI 0x74)
reg [7:0]  fdn_er_density_r;  // ER hit density [8bit] (AXI 0x75)
wire [9:0]  spc_delay_out;     // space table delay output (AXI 0x71)
wire        spc_xfade_done;    // space crossfade done (AXI 0x72)
reg signed [DATA_W-1:0] tap_l_sat, tap_r_sat;  // [FIX-DECL] LFO/spc_lpf 사용 전 선언

// ER gain scale (density → scale)
wire [8:0] er_gain_scale =
    (er_density_r >= ER_DENSE) ? 9'd160 :
    (er_density_r >= ER_HIGH)  ? 9'd200 :
    (er_density_r >= ER_MED)   ? 9'd228 :
    (er_density_r >= ER_LOW)   ? 9'd248 : 9'd255;

// ER Physics 엔진 출력 wire
wire signed [15:0] er_l_phys;
wire signed [15:0] er_r_phys;
wire               er_phys_valid;
wire signed [15:0] x_prediff_injected;
wire signed [15:0] x_prediff_b_injected;

// ============================================================================
//  inv_rt60 관련
// ============================================================================
reg [31:0] reg_inv_rt60_a;
reg [31:0] reg_inv_rt60_b;
reg        inv_calc_start_r;
reg [15:0] inv_calc_rt60_r;
reg        inv_calc_sel_b_r;
wire       inv_calc_done_w;
wire [31:0] inv_calc_out_w;
wire       inv_calc_busy_w;
reg        inv_ready_a_r;
reg        inv_ready_b_r;
reg        inv_sel_b_latch_r;

wire [31:0] cross_blend_eff =
    (reg_space_mode == SPACE_SINGLE) ? 32'd0 : reg_cross_blend;
wire [31:0] cross_blend_clamped =
    (cross_blend_eff > CROSS_MAX_Q24) ? CROSS_MAX_Q24 : cross_blend_eff;
wire [31:0] shimmer_blend_safe =
    (reg_shimmer_blend > SHIMMER_MAX_Q24) ? SHIMMER_MAX_Q24 : reg_shimmer_blend;
wire [3:0] er_pattern =
    (reg_er_pattern_override <= 4'd9) ? reg_er_pattern_override : reg_space_a_type;

// ============================================================================
//  BRAM 선언 (스테레오 predelay 분리)
// ============================================================================
(* ram_style = "block" *) reg signed [DATA_W-1:0] pd_l_mem [0:MAX_PREDELAY-1];
(* ram_style = "block" *) reg signed [DATA_W-1:0] pd_r_mem [0:MAX_PREDELAY-1];
(* ram_style = "block" *) reg signed [DATA_W-1:0] pd_b_mem [0:MAX_PREDELAY-1];

reg [PD_ADDR_W-1:0] pd_l_wptr, pd_r_wptr, pd_b_wptr;

(* ram_style = "block" *) reg signed [DATA_W-1:0] er_mem [0:4095];
reg [12:0] er_wptr;

integer mi_i;
initial begin
    for (mi_i=0; mi_i<MAX_PREDELAY; mi_i=mi_i+1) begin
        pd_l_mem[mi_i]  = {DATA_W{1'b0}};
        pd_r_mem[mi_i]  = {DATA_W{1'b0}};
        pd_b_mem[mi_i]  = {DATA_W{1'b0}};
    end
    for (mi_i=0; mi_i<4096; mi_i=mi_i+1) er_mem[mi_i] = {DATA_W{1'b0}};
end

(* ram_style = "block" *) reg [63:0] er_pattern_rom [0:127];
integer er_rom_i;
initial begin
    for (er_rom_i=0; er_rom_i<128; er_rom_i=er_rom_i+1)
        er_pattern_rom[er_rom_i] = 64'h0;
    er_pattern_rom[ 0]=64'h00D000E000032001; er_pattern_rom[ 1]=64'h03280338000A0032;
    er_pattern_rom[ 2]=64'h00A000B00012206E; er_pattern_rom[ 3]=64'h03500360001B80B4;
    er_pattern_rom[ 4]=64'h0078008800262104; er_pattern_rom[ 5]=64'h037803880032015E;
    er_pattern_rom[ 6]=64'h00500060003E81C2; er_pattern_rom[ 7]=64'h03A003AC004C4230;
    er_pattern_rom[ 8]=64'h00C800D80012C001; er_pattern_rom[ 9]=64'h00A400B40044C15E;
    er_pattern_rom[10]=64'h00880094006A428A; er_pattern_rom[11]=64'h00680074009C441A;
    er_pattern_rom[12]=64'h004C005800E7460E; er_pattern_rom[13]=64'h0038004001324866;
    er_pattern_rom[14]=64'h0024002C0189CB22; er_pattern_rom[15]=64'h0018002001EDCE42;
    er_pattern_rom[16]=64'h00B000C000320001; er_pattern_rom[17]=64'h009000A0008982EE;
    er_pattern_rom[18]=64'h0070008000E105AA; er_pattern_rom[19]=64'h0050006001450898;
    er_pattern_rom[20]=64'h0038004801A90C1C; er_pattern_rom[21]=64'h0028003801FFEF3C;
    er_pattern_rom[22]=64'h0018002001FFEFFF; er_pattern_rom[23]=64'h000C001001FFEFFF;
    er_pattern_rom[24]=64'h00F0010000078001; er_pattern_rom[25]=64'h00D000E000172078;
    er_pattern_rom[26]=64'h00B000C0002800FA; er_pattern_rom[27]=64'h009000A0003A2186;
    er_pattern_rom[28]=64'h00700080004D821C; er_pattern_rom[29]=64'h00500060006222BC;
    er_pattern_rom[30]=64'h0038004800780366; er_pattern_rom[31]=64'h00280038008F241A;
    er_pattern_rom[56]=64'h00F00100000C8001; er_pattern_rom[57]=64'h00B000C00026C0C8;
    er_pattern_rom[58]=64'h00700080004381A4; er_pattern_rom[59]=64'h003800480062C294;
    er_pattern_rom[60]=64'h0018002000848398; er_pattern_rom[61]=64'h000C001000A8C4B0;
    er_pattern_rom[62]=64'h0006000800CF85DC; er_pattern_rom[63]=64'h0002000400F8C71C;
    er_pattern_rom[64]=64'h0010001801F40ED8; er_pattern_rom[65]=64'h0028003001A90C80;
    er_pattern_rom[66]=64'h00480050016A8A8C; er_pattern_rom[67]=64'h00680070012C0898;
    er_pattern_rom[68]=64'h0088009000ED86A4; er_pattern_rom[69]=64'h00A800B000AF04B0;
    er_pattern_rom[70]=64'h00C000D0007082BC; er_pattern_rom[71]=64'h00E000F0003E812C;
end

(* ram_style = "block" *) reg [31:0] preset_rom [0:255];
initial begin : preset_rom_init
    integer t;
    for (t=0; t<256; t=t+1) preset_rom[t] = 32'd0;
    preset_rom[ 0]=32'd240;    preset_rom[ 1]=LPF_MID;
    preset_rom[ 2]={16'd0,18'h0040}; preset_rom[ 3]={16'sh4800,16'sh4000};
    preset_rom[ 4]=P45_Q24;    preset_rom[ 5]=32'h00D99999;
    preset_rom[ 6]=32'd2000;   preset_rom[ 7]=P80_Q24;
    preset_rom[16]=32'd120;    preset_rom[17]=LPF_MID;
    preset_rom[18]={16'd0,18'h0040}; preset_rom[19]={16'sh4000,16'sh3800};
    preset_rom[20]=P65_Q24;    preset_rom[21]=32'h00A66666;
    preset_rom[22]=32'd600;    preset_rom[23]=P70_Q24;
    preset_rom[32]=32'd480;    preset_rom[33]=LPF_LIGHT;
    preset_rom[34]={16'd0,18'h00C0}; preset_rom[35]={16'sh5800,16'sh5000};
    preset_rom[36]=P50_Q24;    preset_rom[37]=P80_Q24;
    preset_rom[38]=32'd1800;   preset_rom[39]=P85_Q24;
    preset_rom[48]=32'd48;     preset_rom[49]=LPF_BRIGHT;
    preset_rom[50]={16'd0,18'h0040}; preset_rom[51]={16'sh6000,16'sh6000};
    preset_rom[52]=P25_Q24;    preset_rom[53]=P78_Q24;
    preset_rom[54]=32'd1200;   preset_rom[55]=P70_Q24;
    preset_rom[96] =32'd960;   preset_rom[97] =LPF_DARK;
    preset_rom[98] ={16'd0,18'h00C0}; preset_rom[99] ={16'sh5800,16'sh6000};
    preset_rom[100]=P40_Q24;   preset_rom[101]=P88_Q24;
    preset_rom[102]=32'd5000;  preset_rom[103]=P90_Q24;
end

(* ram_style = "block" *) reg signed [DATA_W-1:0] diff_ram [0:4095];
integer diff_init_i;
initial begin
    for (diff_init_i=0; diff_init_i<4096; diff_init_i=diff_init_i+1)
        diff_ram[diff_init_i] = {DATA_W{1'b0}};
end
reg [DIFF_ADDR_W-1:0] diff_wp [0:16];

(* ram_style = "distributed" *) reg signed [DATA_W-1:0] apf_ram [0:7];
integer apf_init_i;
initial begin
    for (apf_init_i=0; apf_init_i<8; apf_init_i=apf_init_i+1)
        apf_ram[apf_init_i] = {DATA_W{1'b0}};
end

(* ram_style = "block" *) reg signed [DATA_W-1:0] dl_unified [0:DL_TOTAL-1];
integer dl_init_i;
initial begin
    for (dl_init_i=0; dl_init_i<DL_TOTAL; dl_init_i=dl_init_i+1)
        dl_unified[dl_init_i] = {DATA_W{1'b0}};
end

reg [DL_ADDR_W-1:0]     dl_rd_addr;
reg signed [DATA_W-1:0] dl_rd_data;
reg [DL_ADDR_W-1:0]     dl_wr_addr;
reg signed [DATA_W-1:0] dl_wr_data;
reg                      dl_wr_en;

always @(posedge S_AXI_ACLK) begin
    dl_rd_data <= dl_unified[dl_rd_addr];
    if (dl_wr_en) dl_unified[dl_wr_addr] <= dl_wr_data;
end

reg [DL_SLOT_BITS-1:0] dl_wptr [0:NUM_LINES-1];

(* ram_style = "block" *) reg signed [DATA_W-1:0] filt_ram [0:255];
integer filt_init_i;
initial begin
    for (filt_init_i=0; filt_init_i<256; filt_init_i=filt_init_i+1)
        filt_ram[filt_init_i] = {DATA_W{1'b0}};
end

reg [7:0]                filt_rd_addr;
reg signed [DATA_W-1:0] filt_rd_data;
reg [7:0]                filt_wr_addr;
reg signed [DATA_W-1:0] filt_wr_data;
reg                      filt_wr_en;

always @(posedge S_AXI_ACLK) begin
    filt_rd_data <= filt_ram[filt_rd_addr];
    if (filt_wr_en) filt_ram[filt_wr_addr] <= filt_wr_data;
end

(* ram_style = "block" *) reg [31:0] gain_ram [0:127];
integer gain_init_i;
initial begin
    for (gain_init_i=0; gain_init_i<128; gain_init_i=gain_init_i+1)
        gain_ram[gain_init_i] = P85_Q24;
end

reg [6:0]  gain_rd_addr;
reg [31:0] gain_rd_data;
reg [6:0]  gain_wr_addr;
reg [31:0] gain_wr_data;
reg        gain_wr_en;

always @(posedge S_AXI_ACLK) begin
    gain_rd_data <= gain_ram[gain_rd_addr];
    if (gain_wr_en) gain_ram[gain_wr_addr] <= gain_wr_data;
end

// ============================================================================
//  rt60_dirty / preset_loading
// ============================================================================
reg rt60_dirty, rt60b_dirty;
reg preset_load_req_a, preset_load_req_b;
reg [3:0] preset_load_type_a, preset_load_type_b;
reg       preset_loading;
reg space_preset_dirty;

// ============================================================================
//  프리셋 로더 FSM
// ============================================================================
reg [7:0]  prom_addr;
reg [31:0] prom_dout;
always @(posedge S_AXI_ACLK) begin
    prom_dout <= preset_rom[prom_addr];
end

localparam [1:0] PL_IDLE=2'd0, PL_LOAD_A=2'd1, PL_LOAD_B=2'd2;
reg [1:0]  pl_state;
reg [3:0]  pl_param;
reg [3:0]  pl_type;
reg        pl_wr_en;
reg [3:0]  pl_wr_addr;
reg [31:0] pl_wr_data;
reg        pl_wr_sel_b;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        pl_state<=PL_IDLE; pl_param<=4'd0; preset_loading<=1'b0;
        pl_wr_en<=1'b0; pl_wr_addr<=4'd0; pl_wr_data<=32'd0; pl_wr_sel_b<=1'b0;
    end else begin
        pl_wr_en <= 1'b0;
        case (pl_state)
        PL_IDLE: begin
            if (preset_load_req_a) begin
                pl_type<=preset_load_type_a; pl_param<=4'd0;
                prom_addr<={preset_load_type_a,4'd0};
                preset_loading<=1'b1; pl_state<=PL_LOAD_A;
            end else if (preset_load_req_b) begin
                pl_type<=preset_load_type_b; pl_param<=4'd0;
                prom_addr<={preset_load_type_b,4'd0};
                preset_loading<=1'b1; pl_state<=PL_LOAD_B;
            end else preset_loading<=1'b0;
        end
        PL_LOAD_A: begin
            if (pl_param > 4'd0) begin
                pl_wr_en<=1'b1; pl_wr_addr<=pl_param-4'd1;
                pl_wr_data<=prom_dout; pl_wr_sel_b<=1'b0;
            end
            prom_addr<={pl_type,pl_param}; pl_param<=pl_param+4'd1;
            if (pl_param==4'd15) begin pl_state<=PL_IDLE; preset_loading<=1'b0; end
        end
        PL_LOAD_B: begin
            if (pl_param > 4'd0) begin
                pl_wr_en<=1'b1; pl_wr_addr<=pl_param-4'd1;
                pl_wr_data<=prom_dout; pl_wr_sel_b<=1'b1;
            end
            prom_addr<={pl_type,pl_param}; pl_param<=pl_param+4'd1;
            if (pl_param==4'd15) begin pl_state<=PL_IDLE; preset_loading<=1'b0; end
        end
        default: pl_state<=PL_IDLE;
        endcase
    end
end

// ============================================================================
//  inv_rt60_calc 인스턴스
// ============================================================================
inv_rt60_calc u_inv_rt60_calc (
    .clk     (S_AXI_ACLK),
    .rst_n   (S_AXI_ARESETN),
    .start   (inv_calc_start_r),
    .rt60_ms (inv_calc_rt60_r),
    .done    (inv_calc_done_w),
    .inv_out (inv_calc_out_w),
    .busy    (inv_calc_busy_w)
);

// ============================================================================
//  AXI 쓰기 FSM
//  [FIX-3] reg_geo_start는 이 블록에서만 구동 (단일 드라이버)
//          else begin 최상단에서 매 사이클 0 클리어
//          space_preset_dirty / AXI 0x67 처리에서 1로 세트 → 1-cycle pulse
// ============================================================================
localparam [1:0] WR_IDLE=2'd0, WR_ACTIVE=2'd1, WR_RESP=2'd2;
reg [1:0]                        wr_state;
// [FIX-DECL] 펄스 신호 선언 - 사용 전 위치로 이동
reg space_change_pulse_r;
reg er_out_clr_r;

reg [C_S_AXI_ADDR_WIDTH-1:0]    wr_addr_r;
reg [C_S_AXI_DATA_WIDTH-1:0]    wdata_r;
reg                              wr_en;
integer                          wi;

reg rtg_dirty_clear_a, rtg_dirty_clear_b;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        wr_state<=WR_IDLE; S_AXI_AWREADY<=1'b0; S_AXI_WREADY<=1'b0;
        S_AXI_BVALID<=1'b0; S_AXI_BRESP<=2'b00;
        wr_addr_r<={C_S_AXI_ADDR_WIDTH{1'b0}}; wdata_r<=32'd0; wr_en<=1'b0;

        inv_calc_start_r  <= 1'b0;
        inv_calc_rt60_r   <= 16'd1800;
        inv_calc_sel_b_r  <= 1'b0;
        inv_sel_b_latch_r <= 1'b0;
        inv_ready_a_r     <= 1'b1;
        inv_ready_b_r     <= 1'b1;
        reg_inv_rt60_a    <= 32'd9320;
        reg_inv_rt60_b    <= 32'd13981;

        reg_reverb_type<=RT_HALL; reg_enabled<=1'b0; reg_hpf_enable<=1'b0;
        reg_bq_enable<=1'b0; reg_cross_enable<=1'b1;
        reg_res_mask<=8'h00; reg_res_boost<=P80_Q24;
        reg_lpf_alpha<=LPF_LIGHT; reg_hpf_alpha<=HPF_STD;
        reg_mod_depth<=18'h00C0; reg_wet_mix<=P60_Q24; reg_dry_mix<=P80_Q24;
        reg_er_level<=P50_Q24; reg_er_lpf<=LPF_LIGHT;
        reg_pre_diff_g<=16'sh5000; reg_post_diff_g<=16'sh5000;
        reg_decay_gain<=P80_Q24; reg_stereo_width<=P85_Q24;
        reg_predelay_l<=16'd480; reg_predelay_r<=16'd504;
        reg_a_lpf_alpha<=LPF_LIGHT;
        reg_a_mod_depth<=18'h00C0; reg_a_pre_diff_g<=16'sh5000;
        reg_a_post_diff_g<=16'sh5800; reg_a_er_level<=P50_Q24;
        reg_a_decay_gain<=P80_Q24; reg_a_rt60_ms<=16'd1800;
        reg_a_stereo_width<=P85_Q24;
        reg_b_predelay<=16'd48;  reg_b_lpf_alpha<=LPF_BRIGHT;
        reg_b_mod_depth<=18'h0040; reg_b_pre_diff_g<=16'sh6000;
        reg_b_post_diff_g<=16'sh6000; reg_b_er_level<=P25_Q24;
        reg_b_decay_gain<=P78_Q24; reg_b_rt60_ms<=16'd1200;
        reg_b_stereo_width<=P70_Q24;
        reg_rt60_ms<=16'd1800;
        reg_shimmer_mix<=32'd0; reg_shimmer_pitch<=18'hFFFF;
        reg_gate_thresh<=18'h0800; reg_gate_attack<=16'd48;
        reg_gate_hold<=16'd4800; reg_gate_release<=16'd2400;
        reg_bq_b0<=ONE_Q24; reg_bq_b1<=32'd0; reg_bq_b2<=32'd0;
        reg_bq_a1<=32'd0; reg_bq_a2<=32'd0; reg_bq_gain<=ONE_Q24;
        rt60_dirty<=1'b1; rt60b_dirty<=1'b1;
        for (wi=0; wi<8; wi=wi+1) reg_fb_gain[wi]<=P85_Q24;
        reg_custom_size<=8'h80; reg_custom_decay<=16'd1200;
        reg_custom_material<=LPF_MID; reg_custom_er_blend<=P50_Q24;
        reg_custom_width<=P85_Q24; reg_custom_diffuse<=16'sh5000;
        reg_space_mode<=SPACE_SINGLE;
        reg_space_a_type<=RT_HALL; reg_space_b_type<=RT_PLATE;
        reg_cross_blend<=P25_Q24; reg_shimmer_blend<=32'd0;
        reg_inj_blend<=16'sh5000; reg_space_a_mix<=P80_Q24; reg_space_b_mix<=P80_Q24;
        reg_inj_blend_b<=16'sh5000;
        reg_er_pattern_override<=4'hF; reg_reverse_ramp_rate<=16'd1;
        preset_load_req_a<=1'b0; preset_load_req_b<=1'b0;
        preset_load_type_a<=RT_HALL; preset_load_type_b<=RT_PLATE;
        reg_env_shift<=4'd4;
        reg_lpf_hf_alpha<=LPF_BRIGHT; reg_lpf_air_alpha<=LPF_MID;
        reg_micro_mod_en<=1'b1;
        reg_space_preset    <= SP_HALL;
        reg_space_preset_en <= 1'b1;
        reg_rt60_low_custom  <= P88_Q24;
        reg_rt60_mid_custom  <= P85_Q24;
        reg_rt60_high_custom <= P75_Q24;
        space_preset_dirty  <= 1'b1;
        er_density_r        <= ER_HIGH;
        er_spread_r         <= SPREAD_WIDE;
        // ER Physics 초기화
        reg_room_x         <= 18'h1E00;
        reg_room_y         <= 18'h1000;
        reg_room_z         <= 18'h0800;
        reg_src_x          <= 18'h0F00;
        reg_src_y          <= 18'h0A00;
        reg_src_z          <= 18'h0400;    // [SYNC-1] room_z/2 기본값
        reg_lst_x          <= 18'h0F00;
        reg_lst_y          <= 18'h0500;
        reg_lst_z          <= 18'h0400;    // [SYNC-1] room_z/2 기본값
        reg_cluster_thr    <= 18'h0100;
        reg_er_inject_gain <= 32'h00300000;
        reg_geo_en         <= 1'b1;
        // [FIX-3][CLN-1] reg_geo_start 리셋 초기화 - 이 블록에서만 초기화
        reg_geo_start      <= 1'b0;
        // 펄스 신호 초기화
        space_change_pulse_r <= 1'b0;
        er_out_clr_r         <= 1'b0;
        // [STEREO-IN] 스테레오 입력 파라미터 초기화
        reg_in_cross       <= 18'h2000;   // 0.25 Q1.15
        reg_in_width       <= 18'h4000;   // 0.50 Q1.15
    end else begin
        S_AXI_AWREADY<=1'b0; S_AXI_WREADY<=1'b0; wr_en<=1'b0;
        inv_calc_start_r <= 1'b0;
        // ─────────────────────────────────────────────────────────────────
        // [FIX-3] 1-cycle 펄스 신호 매 사이클 자동 클리어 (단일 드라이버)
        // reg_geo_start, space_change_pulse_r, er_out_clr_r 모두 이 블록에서만 구동
        // ─────────────────────────────────────────────────────────────────
        reg_geo_start        <= 1'b0;
        space_change_pulse_r <= 1'b0;
        er_out_clr_r         <= 1'b0;

        if (inv_calc_done_w) begin
            if (!inv_sel_b_latch_r) begin
                reg_inv_rt60_a <= inv_calc_out_w;
                inv_ready_a_r  <= 1'b1;
            end else begin
                reg_inv_rt60_b <= inv_calc_out_w;
                inv_ready_b_r  <= 1'b1;
            end
        end

        if (rtg_dirty_clear_a) rt60_dirty  <= 1'b0;
        if (rtg_dirty_clear_b) rt60b_dirty <= 1'b0;

        // [BUG-7] space_preset_dirty 처리: rt60_dirty는 항상 set (clear보다 우선)
        if (space_preset_dirty && reg_space_preset_en && !preset_loading) begin
            space_preset_dirty   <= 1'b0;
            rt60_dirty           <= 1'b1;
            space_change_pulse_r <= 1'b1;
            er_out_clr_r         <= 1'b1;
            er_density_r <= sm_er_den_w;
            er_spread_r  <= sm_er_spr_w;
            reg_lpf_alpha    <= sm_lpf_w;
            reg_a_lpf_alpha  <= sm_lpf_w;
            reg_pre_diff_g   <= sm_dpre_w;
            reg_post_diff_g  <= sm_dpost_w;
            reg_geo_start    <= 1'b1;
            // [P2] SPACE_MODEL_CORE 출력 사용 (inline case 대체)
            reg_cluster_thr  <= smc_cluster_thr_out;
            reg_er_inject_gain <= smc_inject_gain_out;
            case (reg_space_preset)
                SP_AMBIENCE:      begin reg_room_x<=18'h0400; reg_room_y<=18'h0300; reg_room_z<=18'h0280; end
                SP_HALL:          begin reg_room_x<=18'h1E00; reg_room_y<=18'h1000; reg_room_z<=18'h0800; end
                SP_CATHEDRAL:     begin reg_room_x<=18'h3C00; reg_room_y<=18'h1E00; reg_room_z<=18'h1800; end
                SP_CHURCH:        begin reg_room_x<=18'h1900; reg_room_y<=18'h0F00; reg_room_z<=18'h0A00; end
                SP_SMALL_ROOM:    begin reg_room_x<=18'h0400; reg_room_y<=18'h0400; reg_room_z<=18'h0280; end
                SP_MEDIUM_ROOM:   begin reg_room_x<=18'h0800; reg_room_y<=18'h0600; reg_room_z<=18'h0300; end
                SP_LARGE_ROOM:    begin reg_room_x<=18'h0F00; reg_room_y<=18'h0A00; reg_room_z<=18'h0400; end
                SP_PLATE_CLASSIC,
                SP_PLATE_BRIGHT,
                SP_PLATE_DARK:    begin reg_geo_en<=1'b0; end
                SP_SHIMMER,
                SP_FROZEN:        begin reg_room_x<=18'h1400; reg_room_y<=18'h0F00; reg_room_z<=18'h0A00; end
                default:          begin reg_room_x<=18'h0C00; reg_room_y<=18'h0800; reg_room_z<=18'h0500; end
            endcase
            // [SYNC-3] src_z/lst_z 자동 설정: room_z / 2 (방 중앙)
            case (reg_space_preset)
                SP_AMBIENCE:      begin reg_src_z<=18'h0140; reg_lst_z<=18'h0140; end
                SP_HALL:          begin reg_src_z<=18'h0400; reg_lst_z<=18'h0200; end
                SP_CATHEDRAL:     begin reg_src_z<=18'h0C00; reg_lst_z<=18'h0600; end
                SP_CHURCH:        begin reg_src_z<=18'h0500; reg_lst_z<=18'h0300; end
                SP_SMALL_ROOM:    begin reg_src_z<=18'h0140; reg_lst_z<=18'h0140; end
                SP_MEDIUM_ROOM:   begin reg_src_z<=18'h0180; reg_lst_z<=18'h0180; end
                SP_LARGE_ROOM:    begin reg_src_z<=18'h0200; reg_lst_z<=18'h0200; end
                SP_PLATE_CLASSIC,
                SP_PLATE_BRIGHT,
                SP_PLATE_DARK:    begin reg_src_z<=18'h0100; reg_lst_z<=18'h0100; end
                SP_SHIMMER,
                SP_FROZEN:        begin reg_src_z<=18'h0500; reg_lst_z<=18'h0300; end
                default:          begin reg_src_z<=18'h0280; reg_lst_z<=18'h0280; end
            endcase
            if (reg_space_preset != SP_PLATE_CLASSIC &&
                reg_space_preset != SP_PLATE_BRIGHT  &&
                reg_space_preset != SP_PLATE_DARK)
                reg_geo_en <= 1'b1;
        end

        if (pl_state==PL_LOAD_A && preset_load_req_a) preset_load_req_a<=1'b0;
        if (pl_state==PL_LOAD_B && preset_load_req_b) preset_load_req_b<=1'b0;

        case (wr_state)
        WR_IDLE:   if (S_AXI_AWVALID) begin S_AXI_AWREADY<=1'b1; wr_addr_r<=S_AXI_AWADDR; wr_state<=WR_ACTIVE; end
        WR_ACTIVE: if (S_AXI_WVALID)  begin wdata_r<=S_AXI_WDATA; S_AXI_WREADY<=1'b1; wr_en<=1'b1; wr_state<=WR_RESP; end
        WR_RESP: begin
            S_AXI_BVALID<=1'b1; S_AXI_BRESP<=2'b00;
            if (S_AXI_BVALID && S_AXI_BREADY) begin S_AXI_BVALID<=1'b0; wr_state<=WR_IDLE; end
        end
        default: wr_state<=WR_IDLE;
        endcase

        if (wr_en) begin
            case (wr_addr_r[9:2])  // [FIX-ADDR] 6-bit→8-bit index
            8'h00: begin
                reg_enabled<=wdata_r[4]; reg_hpf_enable<=wdata_r[5];
                if (wdata_r[3:0]!=reg_reverb_type) begin
                    reg_reverb_type<=wdata_r[3:0];
                    if (wdata_r[3:0]!=RT_CUSTOM) begin
                        preset_load_type_a<=wdata_r[3:0]; preset_load_req_a<=1'b1;
                    end
                    rt60_dirty<=1'b1;
                end
            end
            8'h01: reg_predelay_l<=wdata_r[15:0];
            8'h02: begin reg_lpf_alpha<=wdata_r; if (!reg_space_preset_en) reg_a_lpf_alpha<=wdata_r; end
            8'h03: reg_hpf_alpha<=wdata_r;
            8'h04: reg_mod_depth<=wdata_r[15:0];
            8'h05: reg_wet_mix<=wdata_r;
            8'h06: reg_dry_mix<=wdata_r;
            8'h07: reg_er_level<=wdata_r;
            8'h08: reg_er_lpf<=wdata_r;
            8'h09: begin reg_pre_diff_g<=wdata_r[15:0]; if (!reg_space_preset_en) reg_a_pre_diff_g<=wdata_r[15:0]; end
            8'h0A: begin reg_post_diff_g<=wdata_r[15:0]; if (!reg_space_preset_en) reg_a_post_diff_g<=wdata_r[15:0]; end
            8'h0B: begin reg_decay_gain<=(wdata_r>MAX_DECAY_GAIN)?MAX_DECAY_GAIN:wdata_r; rt60_dirty<=1'b1; end
            8'h0C: reg_stereo_width<=wdata_r;
            8'h0D: begin
                reg_rt60_ms<=wdata_r[15:0]; rt60_dirty<=1'b1;
                if (wdata_r[15:0]!=16'd0 && !inv_calc_busy_w) begin
                    inv_calc_rt60_r <= wdata_r[15:0];
                    inv_calc_sel_b_r <= 1'b0;
                    inv_sel_b_latch_r <= 1'b0;
                    inv_calc_start_r <= 1'b1;
                    inv_ready_a_r <= 1'b0;
                end
            end
            8'h0E: reg_shimmer_mix<=(wdata_r>SHIMMER_MAX_Q24)?SHIMMER_MAX_Q24:wdata_r;
            8'h0F: reg_shimmer_pitch<=wdata_r[15:0];
            8'h10: reg_gate_thresh<=wdata_r[15:0];
            8'h11: reg_gate_attack<=wdata_r[15:0];
            8'h12: reg_gate_hold<=wdata_r[15:0];
            8'h13: reg_gate_release<=wdata_r[15:0];
            8'h14: begin reg_bq_enable<=wdata_r[31]; reg_bq_b0<={1'b0,wdata_r[30:0]}; end
            8'h15: reg_bq_b1<=wdata_r;
            8'h16: reg_bq_b2<=wdata_r;
            8'h17: reg_bq_a1<=wdata_r;
            8'h18: reg_bq_a2<=wdata_r;
            8'h19: reg_bq_gain<=wdata_r;
            8'h1A: begin reg_cross_enable<=wdata_r[0]; end
            8'h1B: reg_res_mask<=wdata_r[7:0];
            8'h1C: reg_res_boost<=wdata_r;
            8'h20: begin reg_fb_gain[0]<=(wdata_r>MAX_FB_GAIN)?MAX_FB_GAIN:wdata_r; rt60_dirty<=1'b1; end
            8'h21: begin reg_fb_gain[1]<=(wdata_r>MAX_FB_GAIN)?MAX_FB_GAIN:wdata_r; rt60_dirty<=1'b1; end
            8'h22: begin reg_fb_gain[2]<=(wdata_r>MAX_FB_GAIN)?MAX_FB_GAIN:wdata_r; rt60_dirty<=1'b1; end
            8'h23: begin reg_fb_gain[3]<=(wdata_r>MAX_FB_GAIN)?MAX_FB_GAIN:wdata_r; rt60_dirty<=1'b1; end
            8'h24: begin reg_fb_gain[4]<=(wdata_r>MAX_FB_GAIN)?MAX_FB_GAIN:wdata_r; rt60_dirty<=1'b1; end
            8'h25: begin reg_fb_gain[5]<=(wdata_r>MAX_FB_GAIN)?MAX_FB_GAIN:wdata_r; rt60_dirty<=1'b1; end
            8'h26: begin reg_fb_gain[6]<=(wdata_r>MAX_FB_GAIN)?MAX_FB_GAIN:wdata_r; rt60_dirty<=1'b1; end
            8'h27: begin reg_fb_gain[7]<=(wdata_r>MAX_FB_GAIN)?MAX_FB_GAIN:wdata_r; rt60_dirty<=1'b1; end
            8'h28: begin
                reg_custom_size<=wdata_r[7:0];
                if (reg_reverb_type==RT_CUSTOM) begin
                    reg_predelay_l<={wdata_r[7:0],3'd0};
                    reg_predelay_r<={wdata_r[7:0],3'd0}+16'd24;
                end
            end
            8'h29: begin
                reg_custom_decay<=wdata_r[15:0];
                if (reg_reverb_type==RT_CUSTOM) begin
                    reg_a_rt60_ms<=wdata_r[15:0]; reg_rt60_ms<=wdata_r[15:0]; rt60_dirty<=1'b1;
                end
            end
            8'h2A: begin reg_custom_material<=wdata_r; if (reg_reverb_type==RT_CUSTOM) begin reg_a_lpf_alpha<=wdata_r; reg_lpf_alpha<=wdata_r; end end
            8'h2B: begin reg_custom_er_blend<=wdata_r; if (reg_reverb_type==RT_CUSTOM) begin reg_a_er_level<=wdata_r; reg_er_level<=wdata_r; end end
            8'h2C: begin reg_custom_width<=wdata_r; if (reg_reverb_type==RT_CUSTOM) begin reg_a_stereo_width<=wdata_r; reg_stereo_width<=wdata_r; end end
            8'h2D: begin
                reg_custom_diffuse<=wdata_r[15:0];
                if (reg_reverb_type==RT_CUSTOM) begin
                    reg_a_pre_diff_g<=wdata_r[15:0]; reg_a_post_diff_g<=wdata_r[15:0];
                    reg_pre_diff_g<=wdata_r[15:0]; reg_post_diff_g<=wdata_r[15:0];
                end
            end
            8'h30: begin
                reg_space_mode<=wdata_r[1:0];
                if (wdata_r[7:4]!=reg_space_a_type) begin
                    reg_space_a_type<=wdata_r[7:4]; preset_load_type_a<=wdata_r[7:4];
                    preset_load_req_a<=1'b1; rt60_dirty<=1'b1;
                end
                if (wdata_r[11:8]!=reg_space_b_type) begin
                    reg_space_b_type<=wdata_r[11:8]; preset_load_type_b<=wdata_r[11:8];
                    preset_load_req_b<=1'b1; rt60b_dirty<=1'b1;
                end
            end
            8'h31: reg_a_lpf_alpha<=wdata_r;
            8'h32: reg_a_mod_depth<=wdata_r[15:0];
            8'h33: begin reg_a_pre_diff_g<=wdata_r[15:0]; reg_a_post_diff_g<=wdata_r[31:16]; end
            8'h34: reg_a_er_level<=wdata_r;
            8'h35: begin reg_a_decay_gain<=(wdata_r>MAX_DECAY_GAIN)?MAX_DECAY_GAIN:wdata_r; rt60_dirty<=1'b1; end
            8'h36: begin
                reg_a_rt60_ms<=wdata_r[15:0]; rt60_dirty<=1'b1;
                if (wdata_r[15:0]!=16'd0 && !inv_calc_busy_w) begin
                    inv_calc_rt60_r<=wdata_r[15:0]; inv_calc_sel_b_r<=1'b0;
                    inv_sel_b_latch_r<=1'b0; inv_calc_start_r<=1'b1; inv_ready_a_r<=1'b0;
                end
            end
            8'h37: reg_a_stereo_width<=wdata_r;
            8'h38: reg_b_lpf_alpha<=wdata_r;
            8'h39: reg_b_mod_depth<=wdata_r[15:0];
            8'h3A: begin reg_b_pre_diff_g<=wdata_r[15:0]; reg_b_post_diff_g<=wdata_r[31:16]; end
            8'h3B: reg_b_er_level<=wdata_r;
            8'h3C: begin reg_b_decay_gain<=(wdata_r>MAX_DECAY_GAIN)?MAX_DECAY_GAIN:wdata_r; rt60b_dirty<=1'b1; end
            8'h3D: begin
                reg_b_rt60_ms<=wdata_r[15:0]; rt60b_dirty<=1'b1;
                if (wdata_r[15:0]!=16'd0 && !inv_calc_busy_w) begin
                    inv_calc_rt60_r<=wdata_r[15:0]; inv_calc_sel_b_r<=1'b1;
                    inv_sel_b_latch_r<=1'b1; inv_calc_start_r<=1'b1; inv_ready_b_r<=1'b0;
                end
            end
            8'h3E: reg_b_stereo_width<=wdata_r;
            8'h40: reg_cross_blend<=wdata_r;
            8'h41: reg_shimmer_blend<=wdata_r;
            8'h42: reg_inj_blend<=wdata_r[15:0];
            8'h43: reg_space_a_mix<=wdata_r;
            8'h44: reg_space_b_mix<=wdata_r;
            8'h45: reg_inj_blend_b<=wdata_r[15:0];
            8'h46: reg_er_pattern_override<=wdata_r[3:0];
            8'h47: reg_reverse_ramp_rate<=wdata_r[15:0];
            8'h48: reg_env_shift<=wdata_r[3:0];
            8'h49: reg_lpf_hf_alpha<=wdata_r;
            8'h4A: reg_lpf_air_alpha<=wdata_r;
            8'h4B: reg_micro_mod_en<=wdata_r[0];
            8'h50: begin
                reg_space_preset_en <= wdata_r[31];
                if (wdata_r[4:0] != reg_space_preset) begin
                    reg_space_preset    <= wdata_r[4:0];
                    space_preset_dirty  <= 1'b1;
                    rt60_dirty          <= 1'b1;
                end
            end
            8'h51: begin reg_rt60_low_custom <= wdata_r; if (reg_space_preset==SP_CUSTOM) rt60_dirty<=1'b1; end
            8'h52: begin reg_rt60_mid_custom <= wdata_r; if (reg_space_preset==SP_CUSTOM) rt60_dirty<=1'b1; end
            8'h53: begin reg_rt60_high_custom <= wdata_r; if (reg_space_preset==SP_CUSTOM) rt60_dirty<=1'b1; end
            8'h60: reg_room_x <= wdata_r[15:0];
            8'h61: reg_room_y <= wdata_r[15:0];
            8'h62: reg_room_z <= wdata_r[15:0];
            8'h63: begin reg_src_x <= wdata_r[31:16]; reg_src_y <= wdata_r[15:0]; end
            8'h64: begin reg_lst_x <= wdata_r[31:16]; reg_lst_y <= wdata_r[15:0]; end
            8'h65: reg_cluster_thr <= wdata_r[15:0];
            8'h66: reg_er_inject_gain <= (wdata_r>32'h00400000)?32'h00400000:wdata_r;
            8'h67: begin
                reg_geo_en <= wdata_r[1];
                // [FIX-3] geo_start 세트: 이 블록에서만 구동
                if (wdata_r[0]) begin reg_geo_start<=1'b1; space_preset_dirty<=1'b1; end
            end
            8'h68: begin reg_src_z <= wdata_r[31:16]; reg_lst_z <= wdata_r[15:0]; end  // [SYNC-1]
            8'h70: reg_predelay_r <= wdata_r[15:0];
            // [STEREO-IN] 스테레오 입력 파라미터 (0x76/0x77: 0x71~0x75는 기존 사용)
            8'h76: reg_in_cross <= wdata_r[15:0];   // cross injection 계수 Q1.15
            8'h77: reg_in_width <= wdata_r[15:0];   // side 복원 계수 Q1.15
            default: ;
            endcase
        end

        // [FIX-1] pl_wr_handler 통합 (단일 always 블록 내에서만 처리)
        if (pl_wr_en) begin
            case (pl_wr_addr)
            4'd0: begin
                if (!pl_wr_sel_b) begin
                    reg_predelay_l <= pl_wr_data[15:0];
                    if (reg_predelay_r == reg_predelay_l + 16'd24 ||
                        reg_predelay_r == 16'd0)
                        reg_predelay_r <= pl_wr_data[15:0] + 16'd24;
                end else reg_b_predelay <= pl_wr_data[15:0];
            end
            4'd1: begin
                if (!pl_wr_sel_b) reg_a_lpf_alpha <= pl_wr_data;
                else              reg_b_lpf_alpha <= pl_wr_data;
            end
            4'd2: begin
                if (!pl_wr_sel_b) reg_a_mod_depth <= pl_wr_data[15:0];
                else              reg_b_mod_depth <= pl_wr_data[15:0];
            end
            4'd3: begin
                if (!pl_wr_sel_b) begin
                    if (!reg_space_preset_en) begin
                        reg_a_pre_diff_g  <= pl_wr_data[15:0];
                        reg_a_post_diff_g <= pl_wr_data[31:16];
                        reg_pre_diff_g    <= pl_wr_data[15:0];
                        reg_post_diff_g   <= pl_wr_data[31:16];
                    end else begin
                        reg_a_pre_diff_g  <= pl_wr_data[15:0];
                        reg_a_post_diff_g <= pl_wr_data[31:16];
                    end
                end else begin
                    reg_b_pre_diff_g  <= pl_wr_data[15:0];
                    reg_b_post_diff_g <= pl_wr_data[31:16];
                end
            end
            4'd4: begin
                if (!pl_wr_sel_b) begin reg_a_er_level<=pl_wr_data; reg_er_level<=pl_wr_data; end
                else reg_b_er_level <= pl_wr_data;
            end
            4'd5: begin
                if (!pl_wr_sel_b) begin
                    reg_a_decay_gain <= (pl_wr_data>MAX_DECAY_GAIN)?MAX_DECAY_GAIN:pl_wr_data;
                    reg_decay_gain   <= (pl_wr_data>MAX_DECAY_GAIN)?MAX_DECAY_GAIN:pl_wr_data;
                end else reg_b_decay_gain <= (pl_wr_data>MAX_DECAY_GAIN)?MAX_DECAY_GAIN:pl_wr_data;
            end
            4'd6: begin
                if (!pl_wr_sel_b) begin
                    reg_a_rt60_ms <= pl_wr_data[15:0]; reg_rt60_ms <= pl_wr_data[15:0];
                    if (pl_wr_data[15:0]!=16'd0 && !inv_calc_busy_w) begin
                        inv_calc_rt60_r<=pl_wr_data[15:0]; inv_calc_sel_b_r<=1'b0;
                        inv_sel_b_latch_r<=1'b0; inv_calc_start_r<=1'b1; inv_ready_a_r<=1'b0;
                    end
                end else begin
                    reg_b_rt60_ms <= pl_wr_data[15:0];
                    if (pl_wr_data[15:0]!=16'd0 && !inv_calc_busy_w) begin
                        inv_calc_rt60_r<=pl_wr_data[15:0]; inv_calc_sel_b_r<=1'b1;
                        inv_sel_b_latch_r<=1'b1; inv_calc_start_r<=1'b1; inv_ready_b_r<=1'b0;
                    end
                end
            end
            4'd7: begin
                if (!pl_wr_sel_b) begin reg_a_stereo_width<=pl_wr_data; reg_stereo_width<=pl_wr_data; end
                else reg_b_stereo_width <= pl_wr_data;
            end
            default: ;
            endcase
        end
    end
end

// ============================================================================
//  AXI-Lite 읽기
// ============================================================================
localparam [0:0] RD_IDLE=1'b0, RD_SEND=1'b1;
reg rd_state;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rd_state<=RD_IDLE; S_AXI_ARREADY<=1'b0;
        S_AXI_RVALID<=1'b0; S_AXI_RDATA<=32'd0; S_AXI_RRESP<=2'b00;
    end else begin
        S_AXI_ARREADY<=1'b0;
        case (rd_state)
        RD_IDLE: if (S_AXI_ARVALID) begin
            S_AXI_ARREADY<=1'b1;
            case (S_AXI_ARADDR[9:2])  // [FIX-ADDR] 8-bit index
            8'h00: S_AXI_RDATA<={26'd0,reg_hpf_enable,reg_enabled,reg_reverb_type};
            8'h01: S_AXI_RDATA<={16'd0,reg_predelay_l};
            8'h0D: S_AXI_RDATA<={16'd0,reg_rt60_ms};
            8'h28: S_AXI_RDATA<={24'd0,reg_custom_size};
            8'h36: S_AXI_RDATA<={16'd0,reg_a_rt60_ms};
            8'h3D: S_AXI_RDATA<={16'd0,reg_b_rt60_ms};
            // version v59 = 0x3B
            8'h3F: S_AXI_RDATA<=32'h0000003B;
            8'h40: S_AXI_RDATA<=reg_cross_blend;
            8'h41: S_AXI_RDATA<=shimmer_blend_safe;
            8'h46: S_AXI_RDATA<={28'd0,reg_er_pattern_override};
            8'h4B: S_AXI_RDATA<={31'd0,reg_micro_mod_en};
            8'h50: S_AXI_RDATA<={reg_space_preset_en,26'd0,reg_space_preset};
            8'h51: S_AXI_RDATA<=reg_rt60_low_custom;
            8'h52: S_AXI_RDATA<=reg_rt60_mid_custom;
            8'h53: S_AXI_RDATA<=reg_rt60_high_custom;
            8'h54: S_AXI_RDATA<={16'd0,er_density_r};
            8'h55: S_AXI_RDATA<={16'd0,er_spread_r};
            8'h60: S_AXI_RDATA<={16'd0,reg_room_x};
            8'h61: S_AXI_RDATA<={16'd0,reg_room_y};
            8'h62: S_AXI_RDATA<={16'd0,reg_room_z};
            8'h63: S_AXI_RDATA<={reg_src_x,reg_src_y};
            8'h64: S_AXI_RDATA<={reg_lst_x,reg_lst_y};
            8'h65: S_AXI_RDATA<={16'd0,reg_cluster_thr};
            8'h66: S_AXI_RDATA<=reg_er_inject_gain;
            8'h67: S_AXI_RDATA<={30'd0,reg_geo_en,1'b0};
            8'h68: S_AXI_RDATA<={reg_src_z,reg_lst_z};  // [SYNC-1]
            8'h70: S_AXI_RDATA<={16'd0,reg_predelay_r};
            8'h71: S_AXI_RDATA<={22'd0, spc_delay_out};
            8'h72: S_AXI_RDATA<={31'd0, spc_xfade_done};
            // [DYN-5] v60 신규: FDN energy / ER density 디버그 읽기
            8'h73: S_AXI_RDATA<={16'd0, fdn_energy_l};    // FDN 에너지 L Q1.15
            8'h74: S_AXI_RDATA<={16'd0, fdn_energy_r};    // FDN 에너지 R Q1.15
            8'h75: S_AXI_RDATA<={24'd0, fdn_er_density_r}; // ER hit density (8bit counter)
            // [STEREO-IN] 스테레오 입력 파라미터 읽기
            8'h76: S_AXI_RDATA<={16'd0, reg_in_cross};
            8'h77: S_AXI_RDATA<={16'd0, reg_in_width};
            default: S_AXI_RDATA<=32'hDEADBEEF;
            endcase
            rd_state<=RD_SEND;
        end
        RD_SEND: begin
            S_AXI_RVALID<=1'b1; S_AXI_RRESP<=2'b00;
            if (S_AXI_RVALID && S_AXI_RREADY) begin S_AXI_RVALID<=1'b0; rd_state<=RD_IDLE; end
        end
        default: rd_state<=RD_IDLE;
        endcase
    end
end

// ============================================================================
//  LFO 동기화
// ============================================================================
reg signed [DATA_W-1:0] lfo_val [0:NUM_LINES-1];
integer lfo_i;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        for (lfo_i=0; lfo_i<NUM_LINES; lfo_i=lfo_i+1) lfo_val[lfo_i]<=16'sd0;
    end else begin
        lfo_val[0]<=lfo_in_0;  lfo_val[1]<=lfo_in_1;
        lfo_val[2]<=lfo_in_2;  lfo_val[3]<=lfo_in_3;
        lfo_val[4]<=lfo_in_4;  lfo_val[5]<=lfo_in_5;
        lfo_val[6]<=lfo_in_6;  lfo_val[7]<=lfo_in_7;
        // [TIM-7] -32768 오버플로우 방지
        lfo_val[8] <=($signed(lfo_in_0)==16'sh8000)?16'sh7FFF:-$signed(lfo_in_0);
        lfo_val[9] <=($signed(lfo_in_1)==16'sh8000)?16'sh7FFF:-$signed(lfo_in_1);
        lfo_val[10]<=($signed(lfo_in_2)==16'sh8000)?16'sh7FFF:-$signed(lfo_in_2);
        lfo_val[11]<=($signed(lfo_in_3)==16'sh8000)?16'sh7FFF:-$signed(lfo_in_3);
        lfo_val[12]<=($signed(lfo_in_4)==16'sh8000)?16'sh7FFF:-$signed(lfo_in_4);
        lfo_val[13]<=($signed(lfo_in_5)==16'sh8000)?16'sh7FFF:-$signed(lfo_in_5);
        lfo_val[14]<=($signed(lfo_in_6)==16'sh8000)?16'sh7FFF:-$signed(lfo_in_6);
        lfo_val[15]<=($signed(lfo_in_7)==16'sh8000)?16'sh7FFF:-$signed(lfo_in_7);
    end
end

// ============================================================================
//  [LFO-MOD] LFO 안전 계층 (Scale → Clamp → Q-normalize)
//
//  LFO2 → FDN tap gain modulation (slow 0.5Hz, depth ≤ 1%)
//    lfo2_tap: Q1.15 → ±327 수준 (1% of 32767)
//    적용: TAP_L/R 상수에 ±1% 흔들림 부가
//    안전 보장: |lfo2_tap_mod| ≤ 655 (2%) clamp
//
//  LFO1 → ER stereo width modulation (space_er_engine으로 전달)
//    lfo1_er_width: Q1.15 → ±1638 수준 (5% of 32767)
//    space_er_engine에서 L/R ER gain에 anti-phase 적용
//    안전 보장: |lfo1_er_width| ≤ 3276 (10%) clamp
// ============================================================================

// LFO2: FDN tap gain mod - scale × (1/128) → ±256 이하
wire signed [15:0] lfo2_raw    = lfo_in_2;
wire signed [15:0] lfo2_scaled = $signed(lfo2_raw) >>> 7;  // ÷128 → ±256
// clamp ±512 (1.56%)
wire signed [15:0] lfo2_tap_mod =
    ($signed(lfo2_scaled) >  16'sd512) ?  16'sd512 :
    ($signed(lfo2_scaled) < -16'sd512) ? -16'sd512 :
    lfo2_scaled;

// LFO1: ER stereo width mod - scale × (1/16) → ±2048
wire signed [15:0] lfo1_raw       = lfo_in_1;
wire signed [15:0] lfo1_scaled    = $signed(lfo1_raw) >>> 4;  // ÷16 → ±2048
// clamp ±3276 (10%)
wire signed [15:0] lfo1_er_width =
    ($signed(lfo1_scaled) >  16'sd3276) ?  16'sd3276 :
    ($signed(lfo1_scaled) < -16'sd3276) ? -16'sd3276 :
    lfo1_scaled;

// ============================================================================
//  공유 DSP48 multiplier
// ============================================================================
reg  signed [DATA_W-1:0] mul_a;
reg  [COEFF_W-1:0]       mul_b;
(* use_dsp = "yes" *) reg signed [ACC_W-1:0] mul_res_r;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) mul_res_r<={ACC_W{1'b0}};
    else mul_res_r<=($signed(mul_a)*$signed({1'b0,mul_b}))>>>COEFF_FRAC;
end
wire signed [DATA_W-1:0] mul_out = sat16(mul_res_r);

// ============================================================================
//  Dynamic Dampening Envelope (env cap 추가)
// ============================================================================
reg [31:0] env_lp, dyn_alpha;
reg signed [DATA_W-1:0] tap_l_sat_r2;
reg [31:0] abs_in_r, env_new_r, alpha_t_r;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        env_lp<=32'd0; dyn_alpha<=LPF_LIGHT;
        tap_l_sat_r2<={DATA_W{1'b0}};
        abs_in_r<=32'd0; env_new_r<=32'd0; alpha_t_r<=32'd0;
    end else begin
        tap_l_sat_r2<=tap_l_sat;
        abs_in_r<=tap_l_sat_r2[DATA_W-1]
                  ?{24'd0,~tap_l_sat_r2[14:7]+8'd1}
                  :{24'd0, tap_l_sat_r2[14:7]};
        env_new_r<=env_lp-(env_lp>>13)+abs_in_r;
        env_lp<=(env_new_r>ONE_Q24)?ONE_Q24:env_new_r;
        begin : dyn_alpha_blk
            reg [31:0] env_contrib;
            env_contrib = env_lp >> reg_env_shift;
            if (env_contrib > ENV_ALPHA_CAP) env_contrib = ENV_ALPHA_CAP;
            // [v7] spc_lpf_out dynamic blending 제거 (static table 전환)
            alpha_t_r <= (reg_space_preset_en?eff_lpf_alpha:reg_lpf_alpha)
                         + env_contrib;
        end
        dyn_alpha<=(alpha_t_r>ONE_Q24)?ONE_Q24:alpha_t_r;
    end
end

wire [31:0] b_alpha_base = reg_space_preset_en ? sm_lpf_w : reg_b_lpf_alpha;
wire [31:0] b_env_contrib_raw = env_lp >> reg_env_shift;
wire [31:0] b_env_contrib = (b_env_contrib_raw > ENV_ALPHA_CAP) ? ENV_ALPHA_CAP : b_env_contrib_raw;
wire [31:0] b_alpha_t = b_alpha_base + b_env_contrib;
wire [31:0] cur_dyn_lpf_alpha_b = (b_alpha_t>ONE_Q24)?ONE_Q24:b_alpha_t;

// ============================================================================
//  Shimmer / Gate
// ============================================================================
reg  signed [DATA_W-1:0] shimmer_in_sig;
wire signed [DATA_W-1:0] shimmer_out_sig;
wire                     shimmer_dout_valid;
reg                      shimmer_tick;
reg  signed [DATA_W-1:0] shimmer_out_r;

shimmer_pitch #(.DATA_W(DATA_W),.BUF_BITS(12),.GRAIN_BITS(11)) u_shimmer (
    .clk(S_AXI_ACLK),.rst_n(S_AXI_ARESETN),
    .tick(shimmer_tick),.din(shimmer_in_sig),
    .pitch_ratio(reg_shimmer_pitch),
    .dout(shimmer_out_sig),.dout_valid(shimmer_dout_valid)
);
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) shimmer_out_r<={DATA_W{1'b0}};
    else if (shimmer_dout_valid) shimmer_out_r<=shimmer_out_sig;
end

reg  signed [DATA_W-1:0] gate_din_r;
wire [DATA_W-1:0]        gate_gain_w;
reg  [DATA_W-1:0]        gate_gain;
reg                      gate_tick;

gate_env #(.DATA_W(DATA_W)) u_gate (
    .clk(S_AXI_ACLK),.rst_n(S_AXI_ARESETN),
    .tick(gate_tick),.din(gate_din_r),
    .thresh(reg_gate_thresh),.attack_samp(reg_gate_attack),
    .hold_samp(reg_gate_hold),.release_samp(reg_gate_release),
    .gain(gate_gain_w)
);
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) gate_gain<={DATA_W{1'b0}};
    else gate_gain<=gate_gain_w;
end

// ============================================================================
//  Watchdog
// ============================================================================
localparam signed [DATA_W-1:0] BLOW_THRESH=16'sh7C00;
reg [2:0]  blow_cnt;
reg [31:0] damp_gain_r;
reg signed [DATA_W-1:0] fdn_out_l [0:NUM_L-1];
reg signed [DATA_W-1:0] fdn_out_r [0:NUM_R-1];

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin blow_cnt<=3'd0; damp_gain_r<=ONE_Q24; end
    else begin
        if (($signed(fdn_out_l[0])>$signed(BLOW_THRESH))||
            ($signed(fdn_out_l[0])<-$signed(BLOW_THRESH))||
            ($signed(fdn_out_r[0])>$signed(BLOW_THRESH))||
            ($signed(fdn_out_r[0])<-$signed(BLOW_THRESH)))
            blow_cnt<=(blow_cnt<3'd7)?blow_cnt+3'd1:3'd7;
        else
            blow_cnt<=(blow_cnt>3'd0)?blow_cnt-3'd1:3'd0;
        case (blow_cnt)
            3'd0,3'd1,3'd2,3'd3: damp_gain_r<=ONE_Q24;
            3'd4: damp_gain_r<=P50_Q24;
            3'd5: damp_gain_r<=P25_Q24;
            default: damp_gain_r<=P12_Q24;
        endcase
    end
end

// ============================================================================
//  8×8 Hadamard
// ============================================================================
reg signed [16:0] hs1_l[0:7],hs2_l[0:7];
reg signed [17:0] hs3_l[0:7];
reg signed [16:0] hs1_r[0:7],hs2_r[0:7];
reg signed [17:0] hs3_r[0:7];
reg signed [DATA_W-1:0] had_l[0:NUM_L-1];
reg signed [DATA_W-1:0] had_r[0:NUM_R-1];
integer hi;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        for (hi=0;hi<8;hi=hi+1) begin
            hs1_l[hi]<=17'sd0;hs2_l[hi]<=17'sd0;hs3_l[hi]<=18'sd0;
            hs1_r[hi]<=17'sd0;hs2_r[hi]<=17'sd0;hs3_r[hi]<=18'sd0;
            had_l[hi]<={DATA_W{1'b0}};had_r[hi]<={DATA_W{1'b0}};
        end
    end else begin
        hs1_l[0]<=$signed(fdn_out_l[0])+$signed(fdn_out_l[1]);
        hs1_l[1]<=$signed(fdn_out_l[0])-$signed(fdn_out_l[1]);
        hs1_l[2]<=$signed(fdn_out_l[2])+$signed(fdn_out_l[3]);
        hs1_l[3]<=$signed(fdn_out_l[2])-$signed(fdn_out_l[3]);
        hs1_l[4]<=$signed(fdn_out_l[4])+$signed(fdn_out_l[5]);
        hs1_l[5]<=$signed(fdn_out_l[4])-$signed(fdn_out_l[5]);
        hs1_l[6]<=$signed(fdn_out_l[6])+$signed(fdn_out_l[7]);
        hs1_l[7]<=$signed(fdn_out_l[6])-$signed(fdn_out_l[7]);
        hs1_r[0]<=$signed(fdn_out_r[0])+$signed(fdn_out_r[1]);
        hs1_r[1]<=$signed(fdn_out_r[0])-$signed(fdn_out_r[1]);
        hs1_r[2]<=$signed(fdn_out_r[2])+$signed(fdn_out_r[3]);
        hs1_r[3]<=$signed(fdn_out_r[2])-$signed(fdn_out_r[3]);
        hs1_r[4]<=$signed(fdn_out_r[4])+$signed(fdn_out_r[5]);
        hs1_r[5]<=$signed(fdn_out_r[4])-$signed(fdn_out_r[5]);
        hs1_r[6]<=$signed(fdn_out_r[6])+$signed(fdn_out_r[7]);
        hs1_r[7]<=$signed(fdn_out_r[6])-$signed(fdn_out_r[7]);
        hs2_l[0]<=hs1_l[0]+hs1_l[2];hs2_l[1]<=hs1_l[1]+hs1_l[3];
        hs2_l[2]<=hs1_l[0]-hs1_l[2];hs2_l[3]<=hs1_l[1]-hs1_l[3];
        hs2_l[4]<=hs1_l[4]+hs1_l[6];hs2_l[5]<=hs1_l[5]+hs1_l[7];
        hs2_l[6]<=hs1_l[4]-hs1_l[6];hs2_l[7]<=hs1_l[5]-hs1_l[7];
        hs2_r[0]<=hs1_r[0]+hs1_r[2];hs2_r[1]<=hs1_r[1]+hs1_r[3];
        hs2_r[2]<=hs1_r[0]-hs1_r[2];hs2_r[3]<=hs1_r[1]-hs1_r[3];
        hs2_r[4]<=hs1_r[4]+hs1_r[6];hs2_r[5]<=hs1_r[5]+hs1_r[7];
        hs2_r[6]<=hs1_r[4]-hs1_r[6];hs2_r[7]<=hs1_r[5]-hs1_r[7];
        hs3_l[0]<=hs2_l[0]+hs2_l[4];hs3_l[1]<=hs2_l[1]+hs2_l[5];
        hs3_l[2]<=hs2_l[2]+hs2_l[6];hs3_l[3]<=hs2_l[3]+hs2_l[7];
        hs3_l[4]<=hs2_l[0]-hs2_l[4];hs3_l[5]<=hs2_l[1]-hs2_l[5];
        hs3_l[6]<=hs2_l[2]-hs2_l[6];hs3_l[7]<=hs2_l[3]-hs2_l[7];
        hs3_r[0]<=hs2_r[0]+hs2_r[4];hs3_r[1]<=hs2_r[1]+hs2_r[5];
        hs3_r[2]<=hs2_r[2]+hs2_r[6];hs3_r[3]<=hs2_r[3]+hs2_r[7];
        hs3_r[4]<=hs2_r[0]-hs2_r[4];hs3_r[5]<=hs2_r[1]-hs2_r[5];
        hs3_r[6]<=hs2_r[2]-hs2_r[6];hs3_r[7]<=hs2_r[3]-hs2_r[7];
        for (hi=0;hi<8;hi=hi+1) begin
            had_l[hi]<=hs3_l[hi][17:3];
            had_r[hi]<=hs3_r[hi][17:3];
        end
    end
end

// ============================================================================
//  메인 FSM 신호 선언
// ============================================================================
(* max_fanout = 16 *) reg [4:0] state;
(* max_fanout = 16 *) reg [5:0] sub;
(* max_fanout = 16 *) reg [3:0] line_i;
reg [2:0]  er_tap_i;

reg        use_b_params_r;
wire       is_space_b    = line_i[3];

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) use_b_params_r <= 1'b0;
    else use_b_params_r <= line_i[3] && (reg_space_mode == SPACE_DUAL);
end

wire [15:0] cur_pre_diff_g_sm  = reg_space_preset_en ? sm_dpre_w  : reg_a_pre_diff_g;
wire [15:0] cur_post_diff_g_sm = reg_space_preset_en ? sm_dpost_w : reg_a_post_diff_g;

wire [31:0] cur_lpf_alpha   = use_b_params_r ?
    (reg_space_preset_en ? sm_lpf_w : reg_b_lpf_alpha) :
    (reg_space_preset_en ? sm_lpf_w : reg_a_lpf_alpha);
wire [15:0] cur_mod_depth   = use_b_params_r?reg_b_mod_depth   :reg_a_mod_depth;
wire [15:0] cur_pre_diff_g  = use_b_params_r?reg_b_pre_diff_g  :cur_pre_diff_g_sm;
wire [15:0] cur_post_diff_g = use_b_params_r?reg_b_post_diff_g :cur_post_diff_g_sm;
wire [31:0] cur_er_level_w  = use_b_params_r?reg_b_er_level    :reg_a_er_level;
wire [31:0] cur_stereo_w    = use_b_params_r?reg_b_stereo_width:reg_a_stereo_width;
wire [15:0] cur_inj_blend   = use_b_params_r?reg_inj_blend_b   :reg_inj_blend;

// [FIX-DECL] x_prediff 선언 - wire 참조 전 위치로 이동
reg signed [DATA_W-1:0] x_prediff, x_prediff_r, x_prediff_b;

wire signed [DATA_W-1:0] cur_prediff_l_base =
    (is_space_b && (reg_space_mode==SPACE_DUAL)) ? x_prediff_b : x_prediff;
wire signed [DATA_W-1:0] cur_prediff_r_base =
    (is_space_b && (reg_space_mode==SPACE_DUAL)) ? x_prediff_b : x_prediff_r;

wire signed [DATA_W-1:0] cur_prediff =
    (reg_geo_en && (reg_er_inject_gain > 32'd0)) ?
    ((is_space_b && (reg_space_mode==SPACE_DUAL)) ?
     x_prediff_b_injected : x_prediff_injected) :
    cur_prediff_l_base;

wire [31:0] cur_dyn_lpf_alpha = use_b_params_r?cur_dyn_lpf_alpha_b:dyn_alpha;

reg signed [DATA_W-1:0] x_hold, x_pd, x_pd_r, x_pd_b;
// [moved up - see FIX-DECL]

// [STEREO-IN] 스테레오 입력 홀드 레지스터 및 Mid/Side
reg signed [DATA_W-1:0] x_hold_l;   // L 채널 홀드
reg signed [DATA_W-1:0] x_hold_r;   // R 채널 홀드

// Mid/Side: combinational, 17→16bit 산술 시프트 (포화 없음)
wire signed [16:0] si_mid_17  = $signed(x_hold_l) + $signed(x_hold_r);
wire signed [16:0] si_side_17 = $signed(x_hold_l) - $signed(x_hold_r);
wire signed [DATA_W-1:0] x_mid  = si_mid_17[16:1];   // (L+R)/2 → ER/shimmer/gate
wire signed [DATA_W-1:0] x_side = si_side_17[16:1];  // (L-R)/2 → 출력 width 복원

// Cross Injection: fdn_in_l = L + cross*R,  fdn_in_r = R + cross*L
// DSP 사용으로 타이밍 안전; 결과는 파이프 레지스터 1단
(* use_dsp = "yes" *) wire signed [31:0] si_mul_lr = $signed(x_hold_r) * $signed({1'b0, reg_in_cross});
(* use_dsp = "yes" *) wire signed [31:0] si_mul_rl = $signed(x_hold_l) * $signed({1'b0, reg_in_cross});
wire signed [16:0] si_fdn_l_17 = $signed(x_hold_l) + $signed(si_mul_lr[30:15]);
wire signed [16:0] si_fdn_r_17 = $signed(x_hold_r) + $signed(si_mul_rl[30:15]);
// 포화
wire signed [DATA_W-1:0] si_fdn_in_l =
    (si_fdn_l_17 > 17'sh07FFF) ? 16'h7FFF :
    (si_fdn_l_17 < -17'sh08000) ? -16'h8000 :
    si_fdn_l_17[DATA_W-1:0];
wire signed [DATA_W-1:0] si_fdn_in_r =
    (si_fdn_r_17 > 17'sh07FFF) ? 16'h7FFF :
    (si_fdn_r_17 < -17'sh08000) ? -16'h8000 :
    si_fdn_r_17[DATA_W-1:0];
// 파이프 레지스터 (타이밍 여유 확보)
reg signed [DATA_W-1:0] si_fdn_in_l_r;
reg signed [DATA_W-1:0] si_fdn_in_r_r;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        si_fdn_in_l_r <= {DATA_W{1'b0}};
        si_fdn_in_r_r <= {DATA_W{1'b0}};
    end else begin
        si_fdn_in_l_r <= si_fdn_in_l;
        si_fdn_in_r_r <= si_fdn_in_r;
    end
end

reg [63:0]  er_rom_data;
reg [6:0]   er_rom_addr;
reg signed [DATA_W-1:0] er_rd_l,er_rd_r,er_gl_r,er_gr_r;

always @(posedge S_AXI_ACLK) begin
    er_rom_data <= er_pattern_rom[er_rom_addr];
end

wire [12:0]        er_rom_tl = er_rom_data[12:0];
wire [12:0]        er_rom_tr = er_rom_data[25:13];
wire signed [15:0] er_rom_gl = $signed(er_rom_data[41:26]);
wire signed [15:0] er_rom_gr = $signed(er_rom_data[57:42]);

reg signed [ACC_W-1:0]  er_acc_l, er_acc_r;
reg signed [DATA_W-1:0] er_lp_l, er_lp_r, er_out_l, er_out_r;
reg [12:0]               er_rd_addr;

reg signed [DATA_W-1:0] cur_lp,cur_hp,cur_lph,cur_lpa;
reg signed [DATA_W-1:0] cur_bx1,cur_bx2,cur_by1,cur_by2,cur_dc;
reg [31:0] cur_gain_lf, cur_gain_mf, cur_gain_hf;

reg signed [DATA_W-1:0] damp_hf_sig, damp_mf_sig, damp_lf_sig;
reg signed [ACC_W-1:0]  damp_fb_acc;
reg signed [ACC_W-1:0]  lph_delta_r;

reg signed [ACC_W-1:0]  tap_l_acc, tap_r_acc;
reg signed [DATA_W-1:0] wet_l, wet_r;
reg signed [DATA_W-1:0] mix_dry_r, mix_wet_l_r, mix_wet_r_r, mix_er_l_r, mix_er_r_r;
reg signed [ACC_W-1:0]  final_l, final_r;
reg signed [DATA_W-1:0] out_l_r, out_r_r;
reg drain_d1;

reg tready_r;
assign s_axis_l_tready = tready_r;
assign s_axis_r_tready = tready_r;
wire fire_in = (s_axis_l_tvalid & s_axis_r_tvalid) && tready_r;

reg signed [ACC_W-1:0]  tmp_acc;
reg signed [DATA_W-1:0] tmp_sig;
reg signed [ACC_W-1:0]  tmp_dc_diff, tmp_lp_delta, tmp_bq_pos, tmp_bq_neg, tmp_bq_acc;
reg signed [ACC_W-1:0]  bq_sum_b0_r, bq_sum_b1_r, bq_sum_b2_r, bq_sum_a1_r, bq_sum_a2_r;
reg signed [DATA_W-1:0] bq_in_r;
reg signed [DATA_W-1:0] ap_rd, ap_tmp, ap_out_l, ap_out_r;
reg signed [ACC_W-1:0]  cross_l_acc, cross_r_acc, inj_contrib, shim_acc;

reg signed [31:0] mod_offset_raw_r;
reg signed [31:0] mod_offset_t_r;
reg [20:0]        mod_target_t_r;
reg signed [21:0] mod_delta_t_r;
reg [20:0]        mod_delay_upd_r;
reg [DL_SLOT_BITS-1:0] mod_ptr_slot_r;

reg [24:0] mod_depth_eff_r;
reg signed [15:0] inj_scaled_g;
reg [20:0] mod_delay_cur_r;
reg signed [ACC_W-1:0] sw_mid, sw_side, sw_side_sc;

reg signed [DATA_W-1:0] apf_d_reg;
reg signed [DATA_W+0:0] apf_diff_s;
reg signed [DATA_W-1:0] apf_out_reg;
reg [1:0]  apf_sh;
reg signed [DATA_W-1:0] dl_rd_raw;

reg [7:0]                frac_r;
reg signed [DATA_W-1:0] dl_rd_interp;

reg [DIFF_ADDR_W-1:0] diff_rd_addr, diff_wr_addr;
reg signed [DATA_W-1:0] diff_wr_data, diff_rd_data;
reg                     diff_wr_en;
always @(posedge S_AXI_ACLK) begin
    diff_rd_data<=diff_ram[diff_rd_addr];
    if (diff_wr_en) diff_ram[diff_wr_addr]<=diff_wr_data;
end

// ============================================================================
//  [FIX-4] 3-band RTGAIN 변수 - rtg_sub 5비트 전용
// ============================================================================
reg [3:0]  rtg_line;
reg [4:0]  rtg_sub;   // 5비트 - 모든 case 레이블 5'd로 통일
reg [31:0] rtg_k_Q11;
reg [3:0]  rtg_k_int;
reg [2:0]  rtg_frac_8th;
reg [31:0] rtg_exp_int_val, rtg_exp_8th_val;
(* use_dsp = "yes" *) reg [63:0] rtg_prod_t;
reg [31:0] rtg_g_t, rtg_damp_t;
reg [31:0] rtg_g_raw_r;
reg [31:0] rtg_damp_raw_r;
reg [31:0] rtg_inv_latch_r;
reg [31:0] rtg_numer;
(* use_dsp = "yes" *) reg [63:0] rtg_mf_prod;
reg [31:0] rtg_lf_boost_r;
reg [31:0] rtg_g_mf, rtg_g_hf;

wire [31:0] rtg_lf_ratio = (sm_rt60_low_w > sm_rt60_mid_w) ?
                            (sm_rt60_low_w - sm_rt60_mid_w) : 32'd0;
wire [31:0] rtg_hf_ratio = (sm_rt60_mid_w > sm_rt60_high_w) ?
                            (sm_rt60_mid_w - sm_rt60_high_w) : 32'd0;

wire [3:0] rtg_line_gain_addr = rtg_line;
integer fdn_i;

// ============================================================================
//  FDN Energy Monitor (AXI DEBUG ONLY - v7)
//  [P1] fdn_space_table dynamic overlay 연결 제거됨
//  현재 역할: AXI 0x73/0x74 read-only 디버그 레지스터 구동만
//  fdn_out_l[0..7], fdn_out_r[0..7] 절대값 합산 → IIR smoothing → Q1.15
//  독립 always 블록 (FSM critical path 분리, synth 8-6859 방지)
// ============================================================================
reg [19:0] fdn_elp_l;      // IIR 누산기 L (20bit 내부 정밀도)
reg [19:0] fdn_elp_r;      // IIR 누산기 R

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        fdn_energy_l <= 16'd0;
        fdn_energy_r <= 16'd0;
        fdn_elp_l    <= 20'd0;
        fdn_elp_r    <= 20'd0;
    end else begin
        // abs 합산: line 0~7 (L), line 0~7 R-side (fdn_out_r[0..7])
        // 합산 후 >> 4 정규화 (8라인 × max 0x7FFF → 최대 20bit)
        begin : energy_blk
            reg [19:0] abs_sum_l, abs_sum_r;
            // L: fdn_out_l[0..7] 절대값 합산
            abs_sum_l =
                ({4'd0, fdn_out_l[0][DATA_W-1] ? (~fdn_out_l[0][14:0]+15'd1) : fdn_out_l[0][14:0]}) +
                ({4'd0, fdn_out_l[1][DATA_W-1] ? (~fdn_out_l[1][14:0]+15'd1) : fdn_out_l[1][14:0]}) +
                ({4'd0, fdn_out_l[2][DATA_W-1] ? (~fdn_out_l[2][14:0]+15'd1) : fdn_out_l[2][14:0]}) +
                ({4'd0, fdn_out_l[3][DATA_W-1] ? (~fdn_out_l[3][14:0]+15'd1) : fdn_out_l[3][14:0]}) +
                ({4'd0, fdn_out_l[4][DATA_W-1] ? (~fdn_out_l[4][14:0]+15'd1) : fdn_out_l[4][14:0]}) +
                ({4'd0, fdn_out_l[5][DATA_W-1] ? (~fdn_out_l[5][14:0]+15'd1) : fdn_out_l[5][14:0]}) +
                ({4'd0, fdn_out_l[6][DATA_W-1] ? (~fdn_out_l[6][14:0]+15'd1) : fdn_out_l[6][14:0]}) +
                ({4'd0, fdn_out_l[7][DATA_W-1] ? (~fdn_out_l[7][14:0]+15'd1) : fdn_out_l[7][14:0]});
            // R: fdn_out_r[0..7]
            abs_sum_r =
                ({4'd0, fdn_out_r[0][DATA_W-1] ? (~fdn_out_r[0][14:0]+15'd1) : fdn_out_r[0][14:0]}) +
                ({4'd0, fdn_out_r[1][DATA_W-1] ? (~fdn_out_r[1][14:0]+15'd1) : fdn_out_r[1][14:0]}) +
                ({4'd0, fdn_out_r[2][DATA_W-1] ? (~fdn_out_r[2][14:0]+15'd1) : fdn_out_r[2][14:0]}) +
                ({4'd0, fdn_out_r[3][DATA_W-1] ? (~fdn_out_r[3][14:0]+15'd1) : fdn_out_r[3][14:0]}) +
                ({4'd0, fdn_out_r[4][DATA_W-1] ? (~fdn_out_r[4][14:0]+15'd1) : fdn_out_r[4][14:0]}) +
                ({4'd0, fdn_out_r[5][DATA_W-1] ? (~fdn_out_r[5][14:0]+15'd1) : fdn_out_r[5][14:0]}) +
                ({4'd0, fdn_out_r[6][DATA_W-1] ? (~fdn_out_r[6][14:0]+15'd1) : fdn_out_r[6][14:0]}) +
                ({4'd0, fdn_out_r[7][DATA_W-1] ? (~fdn_out_r[7][14:0]+15'd1) : fdn_out_r[7][14:0]});
            // IIR 1-pole: alpha ≈ 1 - 1/128 (τ ≈ 128 samples @ 48kHz ≈ 2.67ms)
            // abs_sum 최대: 8 × 0x7FFF = 0x3FFF8 → 18bit
            // >> 3 후: 0x7FFF = 15bit → 누산기 20bit 충분
            fdn_elp_l <= fdn_elp_l - (fdn_elp_l >> 7) + (abs_sum_l >> 3);
            fdn_elp_r <= fdn_elp_r - (fdn_elp_r >> 7) + (abs_sum_r >> 3);
            // Q1.15 출력: [19:4] = 16bit 슬라이스, 상위 4bit(19:16) != 0이면 포화
            fdn_energy_l <= (|fdn_elp_l[19:16]) ? 18'hFFFF : fdn_elp_l[15:0];
            fdn_energy_r <= (|fdn_elp_r[19:16]) ? 18'hFFFF : fdn_elp_r[15:0];
        end
    end
end

// ============================================================================
//  ER Density Counter (AXI DEBUG ONLY - v7)
//  [P1] fdn_space_table dynamic overlay 연결 제거됨
//  현재 역할: AXI 0x75 read-only 디버그 레지스터 구동만
//  er_phys_valid pulse를 256-sample 윈도우로 카운트 → 8bit density
//  윈도우: phys_audio_valid (FS_PREDELAY) 기준 256 sample마다 초기화
// ============================================================================
// [FIX-DECL] phys_audio_valid - always 블록 사용 전 module-level 선언
wire phys_audio_valid = (state == FS_PREDELAY);

reg [7:0]  er_hit_cnt_r;      // 현재 윈도우 내 hit 횟수
reg [7:0]  er_win_cnt_r;      // 윈도우 sample 카운터

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        fdn_er_density_r <= 8'd0;
        er_hit_cnt_r     <= 8'd0;
        er_win_cnt_r     <= 8'd0;
    end else begin
        // ER 유효 출력 감지 → hit 카운트 (포화 포함)
        if (er_phys_valid)
            er_hit_cnt_r <= (er_hit_cnt_r == 8'hFF) ? 8'hFF :
                             er_hit_cnt_r + 8'd1;
        // 256-sample 윈도우 (phys_audio_valid = FS_PREDELAY 1-cycle pulse)
        if (phys_audio_valid) begin
            er_win_cnt_r <= er_win_cnt_r + 8'd1;
            if (er_win_cnt_r == 8'd255) begin
                // 윈도우 종료: density 래치 후 카운터 초기화
                fdn_er_density_r <= er_hit_cnt_r;
                er_hit_cnt_r     <= 8'd0;
            end
        end
    end
end

// ============================================================================
//  [STR-2] fdn_space_table 인터페이스
// ============================================================================
wire [15:0] spc_gain_out;      // [FIX-6] 누락 포트 추가
wire [15:0] spc_lpf_out;       // [FIX-6] 누락 포트 추가
wire [7:0]  spc_diff_out;      // [FIX-6] 누락 포트 추가
wire [7:0]  spc_mod_out;       // [FIX-6] 누락 포트 추가

wire [3:0] spc_line_query = (state == FS_RTGAIN) ? rtg_line : line_i;
wire [9:0] cur_fdn_delay  = spc_delay_out;

// [FIX-3] space_change_pulse_r, er_out_clr_r: AXI 블록에서만 구동
// [moved up - see FIX-DECL below]

// ============================================================================
//  [v9] behavior engine 연결 wire
// ============================================================================
wire [7:0]  er_energy_fb_w;      // space_er_engine → behavior_engine
wire [7:0]  er_density_dyn_w;    // space_er_engine → behavior_engine
wire [15:0] bhv_gain_scale_w;    // behavior_engine → fdn_space_table
wire [15:0] bhv_lpf_scale_w;     // behavior_engine → fdn_space_table
wire [7:0]  bhv_diff_scale_w;    // behavior_engine → fdn_space_table

// ============================================================================
//  [v9] space_behavior_engine 인스턴스
//  중간 계층: space_id + ER feedback → behavior scale 곡선 생성
//  RULE: BRAM/LUT 접근 없음, table은 scale 받기만
// ============================================================================
space_behavior_engine u_behavior (
    .clk                 (S_AXI_ACLK),
    .rst_n               (S_AXI_ARESETN),
    .space_id            (reg_space_preset),
    .family_id           (smc_family_id_out),
    .behavior_seed       (smc_behavior_seed_out),
    .er_energy_feedback  (er_energy_fb_w),
    .er_density_dynamic  (er_density_dyn_w),
    .line_idx            (spc_line_query),
    .behavior_gain_scale (bhv_gain_scale_w),
    .behavior_lpf_scale  (bhv_lpf_scale_w),
    .behavior_diff_scale (bhv_diff_scale_w)
);

fdn_space_table #(
    .DL_SLOT_BITS (DL_SLOT_BITS),
    .NUM_LINES    (16),
    .NUM_SPACES   (24),
    .FADE_STEPS   (512)
) u_fdn_space_table (
    .clk                 (S_AXI_ACLK),
    .rst_n               (S_AXI_ARESETN),
    .space_id            (reg_space_preset),      // [FIX-7] 5비트 전체 연결
    .space_change        (space_change_pulse_r),
    .line_idx            (spc_line_query),
    // [v9] behavior scale 입력 연결
    .behavior_gain_scale (bhv_gain_scale_w),
    .behavior_lpf_scale  (bhv_lpf_scale_w),
    .behavior_diff_scale (bhv_diff_scale_w),
    .delay_out           (spc_delay_out),
    .gain_out            (spc_gain_out),
    .lpf_alpha_out       (spc_lpf_out),
    .diff_out            (spc_diff_out),
    .mod_depth_out       (spc_mod_out),
    .crossfade_done      (spc_xfade_done)
);

// [TIM-1] mod_depth_eff wire - line_i 기준 즉시 계산
wire [24:0] mod_depth_eff_w =
    ({9'd0, cur_mod_depth} * {16'd0, mod_scale_sel(line_i[2:0])}) >> 8;

// [BUG-3] audio_valid wire (FS_PREDELAY에서 pulse)
// [moved up - see FIX-DECL]

// ============================================================================
//  [PHYS-1] space_er_engine 인스턴스
// ============================================================================
space_er_engine u_space_er (
    .clk             (S_AXI_ACLK),
    .rst_n           (S_AXI_ARESETN),
    .space_id        (reg_space_preset),
    .space_start     (reg_geo_start),
    .geo_en          (reg_geo_en),
    .room_x          (reg_room_x),
    .room_y          (reg_room_y),
    .room_z          (reg_room_z),
    .src_x           (reg_src_x),
    .src_y           (reg_src_y),
    .src_z           (reg_src_z),         // [SYNC-1] v63 신규
    .lst_x           (reg_lst_x),
    .lst_y           (reg_lst_y),
    .lst_z           (reg_lst_z),         // [SYNC-1] v63 신규
    .cluster_thr     (reg_cluster_thr),
    .injection_gain  (reg_er_inject_gain),
    .audio_in        (x_pd),
    .audio_valid     (phys_audio_valid),
    .er_gain_scale   (er_gain_scale),
    // [LFO-MOD] LFO 안전 계층 통과 값 연결
    .lfo0_jitter     (lfo_in_0),         // LFO0: ER delay jitter (reflection_engine jitter_mask)
    .lfo1_width      (lfo1_er_width),    // LFO1: ER stereo width (clamp 완료)
    .lpf_bright      (LPF_BRIGHT),
    .lpf_mid         (LPF_MID),
    .lpf_dark        (LPF_DARK),
    .fdn_l_in        (x_prediff),
    .fdn_r_in        (x_prediff_r),
    .er_l_out        (er_l_phys),
    .er_r_out        (er_r_phys),
    .er_valid        (er_phys_valid),
    .fdn_l_out       (x_prediff_injected),
    .fdn_r_out       (x_prediff_b_injected),
    // [v9] behavior feedback 출력
    .er_energy_feedback  (er_energy_fb_w),
    .er_density_dynamic  (er_density_dyn_w),
    .engine_state    (),
    .cluster_dbg     ()
);

// ============================================================================
//  메인 FSM
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        state<=FS_IDLE; tready_r<=1'b0; sub<=6'd0; line_i<=4'd0; er_tap_i<=3'd0;
        x_hold<={DATA_W{1'b0}};
        x_hold_l<={DATA_W{1'b0}}; x_hold_r<={DATA_W{1'b0}};   // [STEREO-IN]
        x_pd<={DATA_W{1'b0}}; x_pd_r<={DATA_W{1'b0}}; x_pd_b<={DATA_W{1'b0}};
        x_prediff<={DATA_W{1'b0}}; x_prediff_r<={DATA_W{1'b0}}; x_prediff_b<={DATA_W{1'b0}};
        er_acc_l<={ACC_W{1'b0}}; er_acc_r<={ACC_W{1'b0}};
        er_lp_l<={DATA_W{1'b0}}; er_lp_r<={DATA_W{1'b0}};
        er_out_l<={DATA_W{1'b0}}; er_out_r<={DATA_W{1'b0}};
        er_rd_l<={DATA_W{1'b0}}; er_rd_r<={DATA_W{1'b0}};
        er_rd_addr<=13'd0; er_rom_addr<=7'd0;
        tap_l_acc<={ACC_W{1'b0}}; tap_r_acc<={ACC_W{1'b0}};
        tap_l_sat<={DATA_W{1'b0}}; tap_r_sat<={DATA_W{1'b0}};
        wet_l<={DATA_W{1'b0}}; wet_r<={DATA_W{1'b0}};
        mix_dry_r<={DATA_W{1'b0}};
        mix_wet_l_r<={DATA_W{1'b0}}; mix_wet_r_r<={DATA_W{1'b0}};
        mix_er_l_r<={DATA_W{1'b0}}; mix_er_r_r<={DATA_W{1'b0}};
        final_l<={ACC_W{1'b0}}; final_r<={ACC_W{1'b0}};
        out_l_r<={DATA_W{1'b0}}; out_r_r<={DATA_W{1'b0}};
        drain_d1<=1'b0;
        pd_l_wptr<={PD_ADDR_W{1'b0}}; pd_r_wptr<={PD_ADDR_W{1'b0}}; pd_b_wptr<={PD_ADDR_W{1'b0}};
        er_wptr<=13'd0;
        cur_lp<={DATA_W{1'b0}}; cur_hp<={DATA_W{1'b0}};
        cur_lph<={DATA_W{1'b0}}; cur_lpa<={DATA_W{1'b0}};
        cur_bx1<={DATA_W{1'b0}}; cur_bx2<={DATA_W{1'b0}};
        cur_by1<={DATA_W{1'b0}}; cur_by2<={DATA_W{1'b0}};
        cur_dc<={DATA_W{1'b0}};
        cur_gain_lf<=P85_Q24; cur_gain_mf<=P85_Q24; cur_gain_hf<=P85_Q24;
        damp_hf_sig<={DATA_W{1'b0}}; damp_mf_sig<={DATA_W{1'b0}};
        damp_lf_sig<={DATA_W{1'b0}}; damp_fb_acc<={ACC_W{1'b0}};
        lph_delta_r<={ACC_W{1'b0}};
        mul_a<={DATA_W{1'b0}}; mul_b<={COEFF_W{1'b0}};
        shimmer_tick<=1'b0; gate_tick<=1'b0;
        gate_din_r<={DATA_W{1'b0}}; shimmer_in_sig<={DATA_W{1'b0}};
        cross_l_acc<={ACC_W{1'b0}}; cross_r_acc<={ACC_W{1'b0}};
        inj_contrib<={ACC_W{1'b0}}; shim_acc<={ACC_W{1'b0}};
        bq_in_r<={DATA_W{1'b0}};
        bq_sum_b0_r<={ACC_W{1'b0}}; bq_sum_b1_r<={ACC_W{1'b0}}; bq_sum_b2_r<={ACC_W{1'b0}};
        bq_sum_a1_r<={ACC_W{1'b0}}; bq_sum_a2_r<={ACC_W{1'b0}};
        tmp_acc<={ACC_W{1'b0}}; tmp_sig<={DATA_W{1'b0}};
        // [FIX-4] rtg_sub 5비트 초기화 명시
        rtg_line<=4'd0; rtg_sub<=5'd0; rtg_k_Q11<=32'd0;
        rtg_k_int<=4'd0; rtg_frac_8th<=3'd0;
        rtg_numer<=32'd0; rtg_inv_latch_r<=32'd1;
        rtg_g_raw_r<=32'd0; rtg_damp_raw_r<=32'd0;
        rtg_exp_int_val<=32'd0; rtg_exp_8th_val<=32'd0;
        rtg_prod_t<=64'd0; rtg_g_t<=32'd0; rtg_damp_t<=32'd0;
        rtg_mf_prod<=64'd0;
        rtg_lf_boost_r<=32'd0;
        rtg_g_mf<=P92_Q24; rtg_g_hf<=P75_Q24;
        apf_d_reg<={DATA_W{1'b0}}; apf_diff_s<={DATA_W+1{1'b0}};
        apf_out_reg<={DATA_W{1'b0}}; apf_sh<=2'd1;
        ap_out_l<={DATA_W{1'b0}}; ap_out_r<={DATA_W{1'b0}};
        inj_scaled_g<=16'sh0; dl_rd_raw<={DATA_W{1'b0}};
        frac_r<=8'd0; dl_rd_interp<={DATA_W{1'b0}};
        diff_rd_addr<={DIFF_ADDR_W{1'b0}}; diff_wr_en<=1'b0;
        diff_wr_addr<={DIFF_ADDR_W{1'b0}}; diff_wr_data<={DATA_W{1'b0}};
        begin : diff_wp_rst integer dwi;
            for (dwi=0;dwi<17;dwi=dwi+1) diff_wp[dwi]<=12'd0;
        end
        dl_rd_addr<={DL_ADDR_W{1'b0}};
        dl_wr_en<=1'b0; dl_wr_addr<={DL_ADDR_W{1'b0}}; dl_wr_data<={DATA_W{1'b0}};
        filt_rd_addr<=8'd0; filt_wr_en<=1'b0; filt_wr_addr<=8'd0; filt_wr_data<={DATA_W{1'b0}};
        gain_rd_addr<=7'd0; gain_wr_en<=1'b0; gain_wr_addr<=7'd0; gain_wr_data<=32'd0;
        mod_delay_cur_r<=21'd0;
        mod_offset_raw_r<=32'sd0; mod_offset_t_r<=32'sd0;
        mod_target_t_r<=21'd0; mod_delta_t_r<=22'sd0;
        mod_delay_upd_r<=21'd0; mod_ptr_slot_r<={DL_SLOT_BITS{1'b0}};
        mod_depth_eff_r<=25'd0;
        for (fdn_i=0;fdn_i<NUM_L;fdn_i=fdn_i+1) begin
            fdn_out_l[fdn_i]<={DATA_W{1'b0}}; dl_wptr[fdn_i]<={DL_SLOT_BITS{1'b0}};
        end
        for (fdn_i=0;fdn_i<NUM_R;fdn_i=fdn_i+1) begin
            fdn_out_r[fdn_i]<={DATA_W{1'b0}}; dl_wptr[fdn_i+8]<={DL_SLOT_BITS{1'b0}};
        end
        rtg_dirty_clear_a<=1'b0; rtg_dirty_clear_b<=1'b0;
    end else begin
        shimmer_tick<=1'b0; gate_tick<=1'b0;
        diff_wr_en<=1'b0; dl_wr_en<=1'b0;
        filt_wr_en<=1'b0; gain_wr_en<=1'b0;
        rtg_dirty_clear_a<=1'b0; rtg_dirty_clear_b<=1'b0;

        case (state)

        // ====================================================================
        FS_IDLE: begin
            tready_r<=reg_enabled && !preset_loading;
            drain_d1<=1'b0;
            if (fire_in) begin
                // [STEREO-IN] L/R 각각 홀드
                x_hold_l <= s_axis_l_tdata;
                x_hold_r <= s_axis_r_tdata;
                // x_hold = Mid: 포트에서 직접 계산 (x_mid wire는 x_hold_l/r 기반이므로
                // 동일 사이클 래치 전 값을 참조함 → 포트값으로 직접 산출)
                // [FIX-OVERFLOW] L+R 합산 시 16-bit 오버플로우 방지
                // L=R=0x4000(+16384) 시 16-bit sum = 0x8000(-32768) 으로 wrap
                // → 17-bit 부호확장 후 합산, 그 다음 >>> 1
                x_hold <= ({{s_axis_l_tdata[15]},s_axis_l_tdata} +
                           {{s_axis_r_tdata[15]},s_axis_r_tdata}) >>> 1;
                tready_r<=1'b0;
                state<=FS_PREDELAY; sub<=6'd0;
            end
        end

        // ====================================================================
        // FS_PREDELAY: 스테레오 predelay
        // ====================================================================
        FS_PREDELAY: begin
            // [STEREO-IN] ER 메모리는 Mid 기반 (공간은 물리적으로 1개)
            er_mem[er_wptr]<=x_mid;
            er_wptr<=(er_wptr==13'd4095)?13'd0:er_wptr+13'd1;

            // [STEREO-IN] L predelay: x_hold_l 사용
            pd_l_mem[pd_l_wptr]<=x_hold_l;
            if (reg_predelay_l>16'd0)
                x_pd<=pd_l_mem[(pd_l_wptr>=reg_predelay_l[PD_ADDR_W-1:0])
                    ?(pd_l_wptr-reg_predelay_l[PD_ADDR_W-1:0])
                    :(pd_l_wptr-reg_predelay_l[PD_ADDR_W-1:0]+MAX_PREDELAY[PD_ADDR_W-1:0])];
            else x_pd<=x_hold_l;
            pd_l_wptr<=(pd_l_wptr==MAX_PREDELAY-1)?{PD_ADDR_W{1'b0}}:pd_l_wptr+1'b1;

            // [STEREO-IN] R predelay: x_hold_r 사용
            pd_r_mem[pd_r_wptr]<=x_hold_r;
            if (reg_predelay_r>16'd0)
                x_pd_r<=pd_r_mem[(pd_r_wptr>=reg_predelay_r[PD_ADDR_W-1:0])
                    ?(pd_r_wptr-reg_predelay_r[PD_ADDR_W-1:0])
                    :(pd_r_wptr-reg_predelay_r[PD_ADDR_W-1:0]+MAX_PREDELAY[PD_ADDR_W-1:0])];
            else x_pd_r<=x_hold_r;
            pd_r_wptr<=(pd_r_wptr==MAX_PREDELAY-1)?{PD_ADDR_W{1'b0}}:pd_r_wptr+1'b1;

            // [STEREO-IN] B predelay (SPACE_DUAL용): x_hold_r 사용
            pd_b_mem[pd_b_wptr]<=x_hold_r;
            if (reg_b_predelay>16'd0)
                x_pd_b<=pd_b_mem[(pd_b_wptr>=reg_b_predelay[PD_ADDR_W-1:0])
                    ?(pd_b_wptr-reg_b_predelay[PD_ADDR_W-1:0])
                    :(pd_b_wptr-reg_b_predelay[PD_ADDR_W-1:0]+MAX_PREDELAY[PD_ADDR_W-1:0])];
            else x_pd_b<=x_hold_r;
            pd_b_wptr<=(pd_b_wptr==MAX_PREDELAY-1)?{PD_ADDR_W{1'b0}}:pd_b_wptr+1'b1;

            er_acc_l<={ACC_W{1'b0}}; er_acc_r<={ACC_W{1'b0}};
            er_tap_i<=3'd0; er_rom_addr<={er_pattern,3'd0};

            state<=FS_ER; sub<=6'd0;
        end

        // ====================================================================
        // FS_ER: 물리 ER 엔진 sample-and-hold
        // ====================================================================
        FS_ER: begin
            if (er_out_clr_r) begin
                er_out_l <= {DATA_W{1'b0}};
                er_out_r <= {DATA_W{1'b0}};
            end else if (er_phys_valid) begin
                er_out_l <= er_l_phys;
                er_out_r <= er_r_phys;
            end
            diff_rd_addr<=diff_base(5'd0)+diff_wp[0];
            state<=FS_PRE_DIFF; sub<=6'd0;
        end

        // ====================================================================
        // FS_PRE_DIFF
        // ====================================================================
        FS_PRE_DIFF: begin
            case (sub)
            6'd0: sub<=6'd1;
            6'd1: begin
                ap_rd=diff_rd_data; ap_tmp=$signed(x_pd)-ap_rd;
                case (cur_pre_diff_g[14:13])
                    2'b10: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_l=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd0)+diff_wp[0]; diff_wr_data<=x_pd;
                diff_wp[0]<=(diff_wp[0]>=diff_maxidx(5'd0))?{DIFF_ADDR_W{1'b0}}:diff_wp[0]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_l; diff_rd_addr<=diff_base(5'd1)+diff_wp[1]; sub<=6'd2;
            end
            6'd2: sub<=6'd3;
            6'd3: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_pre_diff_g[14:13])
                    2'b10: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_l=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd1)+diff_wp[1]; diff_wr_data<=tmp_sig;
                diff_wp[1]<=(diff_wp[1]>=diff_maxidx(5'd1))?{DIFF_ADDR_W{1'b0}}:diff_wp[1]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_l; diff_rd_addr<=diff_base(5'd2)+diff_wp[2]; sub<=6'd4;
            end
            6'd4: sub<=6'd5;
            6'd5: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_pre_diff_g[14:13])
                    2'b10: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_l=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd2)+diff_wp[2]; diff_wr_data<=tmp_sig;
                diff_wp[2]<=(diff_wp[2]>=diff_maxidx(5'd2))?{DIFF_ADDR_W{1'b0}}:diff_wp[2]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_l; diff_rd_addr<=diff_base(5'd3)+diff_wp[3]; sub<=6'd6;
            end
            6'd6: sub<=6'd7;
            6'd7: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_pre_diff_g[14:13])
                    2'b10: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_l=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd3)+diff_wp[3]; diff_wr_data<=tmp_sig;
                diff_wp[3]<=(diff_wp[3]>=diff_maxidx(5'd3))?{DIFF_ADDR_W{1'b0}}:diff_wp[3]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_l; diff_rd_addr<=diff_base(5'd4)+diff_wp[4]; sub<=6'd8;
            end
            6'd8: sub<=6'd9;
            6'd9: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_pre_diff_g[14:13])
                    2'b10: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_l=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd4)+diff_wp[4]; diff_wr_data<=tmp_sig;
                diff_wp[4]<=(diff_wp[4]>=diff_maxidx(5'd4))?{DIFF_ADDR_W{1'b0}}:diff_wp[4]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                // [STEREO-IN] cross injection 적용: L/R 분리 FDN 입력
                // si_fdn_in_l_r = L + cross*R (파이프 레지스터 1단)
                // si_fdn_in_r_r = R + cross*L
                x_prediff  <= si_fdn_in_l_r;
                x_prediff_r<= (reg_space_mode==SPACE_DUAL)?x_pd_b:si_fdn_in_r_r;
                x_prediff_b<= (reg_space_mode==SPACE_DUAL)?x_pd_b:si_fdn_in_l_r;
                diff_rd_addr<=diff_base(5'd15)+diff_wp[15];
                state<=FS_ER_DIFF; sub<=6'd0;
            end
            default: sub<=6'd0;
            endcase
        end

        // ====================================================================
        FS_ER_DIFF: begin
            case (sub)
            6'd0: sub<=6'd1;
            6'd1: begin
                ap_rd=diff_rd_data;
                ap_tmp=$signed(x_prediff)-ap_rd;
                ap_out_l=ap_rd+$signed(ap_tmp>>>1)-$signed(ap_tmp>>>3);
                diff_wr_en<=1'b1;
                diff_wr_addr<=diff_base(5'd15)+diff_wp[15];
                diff_wr_data<=x_prediff;
                diff_wp[15]<=(diff_wp[15]>=diff_maxidx(5'd15))?
                    {DIFF_ADDR_W{1'b0}}:diff_wp[15]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_l;
                diff_rd_addr<=diff_base(5'd16)+diff_wp[16];
                sub<=6'd2;
            end
            6'd2: sub<=6'd3;
            6'd3: begin
                ap_rd=diff_rd_data;
                ap_tmp=tmp_sig-ap_rd;
                ap_out_l=ap_rd+$signed(ap_tmp>>>1)-$signed(ap_tmp>>>3);
                diff_wr_en<=1'b1;
                diff_wr_addr<=diff_base(5'd16)+diff_wp[16];
                diff_wr_data<=tmp_sig;
                diff_wp[16]<=(diff_wp[16]>=diff_maxidx(5'd16))?
                    {DIFF_ADDR_W{1'b0}}:diff_wp[16]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                x_prediff<=ap_out_l;
                if (reg_space_mode!=SPACE_DUAL) x_prediff_b<=ap_out_l;
                // [P1] inv_ready 조건 제거: rt60_dirty 시 즉시 rtgain 실행
                // inv_rt60_calc 완료를 기다리지 않음 → gain 불일치 방지
                if ((rt60_dirty||rt60b_dirty) &&
                    (reg_a_rt60_ms>16'd0||reg_b_rt60_ms>16'd0)) begin
                    state<=FS_RTGAIN; rtg_line<=4'd0; rtg_sub<=5'd0; sub<=6'd0;
                end else begin
                    state<=FS_FDN_READ; line_i<=4'd0; sub<=6'd0;
                    gain_rd_addr<={GTYPE_MOD_DLY,4'd0};
                end
            end
            default: sub<=6'd0;
            endcase
        end

        // ====================================================================
        // [FIX-4] FS_RTGAIN: 모든 rtg_sub case 레이블 5비트로 통일
        // ====================================================================
        FS_RTGAIN: begin
            case (rtg_sub)
            5'd0: begin
                rtg_numer       <= {22'd0, cur_fdn_delay};
                rtg_inv_latch_r <= (rtg_line>=4'd8) ? reg_inv_rt60_b : reg_inv_rt60_a;
                rtg_sub <= 5'd1;
            end
            5'd1: begin
                rtg_prod_t <= {54'd0, rtg_numer[9:0]} * 64'd40800;
                rtg_sub <= 5'd2;
            end
            5'd2: begin
                rtg_numer <= rtg_prod_t[31:0];
                rtg_sub <= 5'd3;
            end
            5'd3: begin
                rtg_prod_t <= {32'd0, rtg_numer} * {32'd0, rtg_inv_latch_r};
                rtg_sub <= 5'd4;
            end
            5'd4: begin
                rtg_k_Q11 <= rtg_prod_t[55:24];
                rtg_sub <= 5'd5;
            end
            5'd5: begin
                rtg_k_int    <= (rtg_k_Q11[31:11]!=21'd0) ? 4'd11 : rtg_k_Q11[14:11];
                rtg_frac_8th <= rtg_k_Q11[6:4];
                rtg_sub <= 5'd6;
            end
            5'd6: begin
                rtg_exp_int_val <= exp_int_lut(rtg_k_int);
                rtg_exp_8th_val <= exp_8th_lut(rtg_frac_8th);
                rtg_sub <= 5'd7;
            end
            5'd7: begin
                rtg_prod_t <= {32'd0, rtg_exp_int_val} * {32'd0, rtg_exp_8th_val};
                rtg_sub <= 5'd8;
            end
            5'd8: begin
                rtg_g_raw_r <= rtg_prod_t[47:24];
                rtg_sub <= 5'd9;
            end
            5'd9: begin
                // [v7] static clamp only (dynamic spc_gain_out ceiling 제거)
                // spc_gain_out은 이제 static table 값 → MAX_SPEC_RADIUS 기준 고정 사용
                if      (rtg_g_raw_r > MAX_SPEC_RADIUS) rtg_g_t <= MAX_SPEC_RADIUS;
                else if (rtg_g_raw_r < GAIN_FLOOR)      rtg_g_t <= GAIN_FLOOR;
                else                                     rtg_g_t <= rtg_g_raw_r;
                rtg_sub <= 5'd10;
            end
            5'd10: begin
                rtg_prod_t     <= {32'd0, rtg_g_t} * {32'd0, damp_gain_r};
                rtg_lf_boost_r <= ({32'd0,rtg_g_t} * {32'd0,rtg_lf_ratio[23:0]}) >> 25;
                rtg_mf_prod    <= {32'd0, rtg_g_t} * {32'd0, P92_Q24};
                rtg_sub <= 5'd11;
            end
            5'd11: begin
                rtg_damp_raw_r <= rtg_prod_t[55:24];
                // [TIM-8] GTYPE_GAIN 기록 (LF는 5'd17에서 분리)
                gain_wr_en   <= 1'b1;
                gain_wr_addr <= {GTYPE_GAIN, rtg_line_gain_addr};
                gain_wr_data <= (rtg_line<4'd8 && reg_res_mask[rtg_line[2:0]]) ?
                                 reg_res_boost : rtg_g_t;
                // LF/MF 사전 계산 래치
                rtg_lf_boost_r <= ({32'd0,rtg_g_t} * {32'd0,rtg_lf_ratio[23:0]}) >> 25;
                rtg_mf_prod    <= {32'd0, rtg_g_t} * {32'd0, P92_Q24};
                rtg_sub <= 5'd17;
            end
            5'd17: begin
                // [TIM-8] GTYPE_GAIN_LF 기록 (sub11 분리)
                begin : rtg_lf_wr_blk
                    reg [31:0] lf_g;
                    lf_g = rtg_g_t + rtg_lf_boost_r;
                    if (lf_g > MAX_SPEC_RADIUS) lf_g = MAX_SPEC_RADIUS;
                    gain_wr_en   <= 1'b1;
                    gain_wr_addr <= {GTYPE_GAIN_LF, rtg_line_gain_addr};
                    gain_wr_data <= (lf_g < GAIN_FLOOR) ? GAIN_FLOOR : lf_g;
                end
                rtg_sub <= 5'd12;
            end
            5'd12: begin
                rtg_damp_t <= (rtg_damp_raw_r < GAIN_FLOOR) ? GAIN_FLOOR : rtg_damp_raw_r;
                rtg_g_mf   <= rtg_mf_prod[47:24];
                gain_wr_en   <= 1'b1;
                gain_wr_addr <= {GTYPE_GAIN_MF, rtg_line_gain_addr};
                gain_wr_data <= (rtg_mf_prod[47:24] < GAIN_FLOOR) ?
                                 GAIN_FLOOR : rtg_mf_prod[47:24];
                begin : rtg_hf_calc_blk
                    reg [31:0] hf_cut;
                    hf_cut = ({32'd0,rtg_g_t} * {32'd0,rtg_hf_ratio[23:0]}) >> 25;
                    rtg_g_hf <= (rtg_g_t > hf_cut) ? rtg_g_t - hf_cut : GAIN_FLOOR;
                end
                rtg_sub <= 5'd13;
            end
            5'd13: begin
                gain_wr_en   <= 1'b1;
                gain_wr_addr <= {GTYPE_GAIN_HF, rtg_line_gain_addr};
                gain_wr_data <= (rtg_g_hf < GAIN_FLOOR) ? GAIN_FLOOR : rtg_g_hf;
                // [FIX-4] rtg_sub 5비트 0으로 명시 복귀
                rtg_sub <= 5'd0;
                if (rtg_line == 4'd15) begin
                    rtg_dirty_clear_a <= 1'b1;
                    rtg_dirty_clear_b <= 1'b1;
                    state        <= FS_FDN_READ;
                    line_i       <= 4'd0;
                    sub          <= 6'd0;
                    gain_rd_addr <= {GTYPE_MOD_DLY, 4'd0};
                end else begin
                    rtg_line <= rtg_line + 4'd1;
                end
            end
            default: rtg_sub <= 5'd0;
            endcase
        end

        // ====================================================================
        // FS_FDN_READ
        // ====================================================================
        FS_FDN_READ: begin
            case (sub)
            6'd0: sub<=6'd1;
            6'd1: begin
                mod_delay_cur_r <= gain_rd_data[20:0];
                mod_depth_eff_r <=
                    ({9'd0,cur_mod_depth} *
                     {16'd0,mod_scale_sel(line_i[2:0])}) >> 8;
                // [TIM-1] wire(즉시값) 사용
                mod_offset_raw_r <=
                    ($signed({lfo_val[line_i][DATA_W-1], lfo_val[line_i]}) *
                     $signed({1'b0, mod_depth_eff_w})) >>> 7;
                sub <= 6'd50;
            end
            6'd50: begin
                mod_offset_t_r  <= {{8{mod_offset_raw_r[31]}}, mod_offset_raw_r[31:8]};
                begin : sub1b_blk
                    reg [20:0] tgt_raw;
                    tgt_raw = {cur_fdn_delay, 8'd0} +
                              mod_offset_raw_r[20:0];
                    mod_target_t_r <= (tgt_raw[20:8] < (DL_SLOT>>3)) ?
                                       21'h000800 : tgt_raw;
                end
                sub <= 6'd51;
            end
            6'd51: begin
                begin : sub1c_blk
                    reg signed [21:0] delta;
                    reg [20:0] delay_new;
                    delta    = $signed({1'b0, mod_target_t_r}) -
                               $signed({1'b0, mod_delay_cur_r});
                    delay_new = mod_delay_cur_r +
                                {{11{delta[21]}}, delta[21:11]};
                    mod_delay_upd_r <= delay_new;
                    mod_ptr_slot_r <= dl_wrap_sub(dl_wptr[line_i],
                                                  delay_new[10+DL_SLOT_BITS-10:8]);
                end
                gain_wr_en   <= 1'b1;
                gain_wr_addr <= {GTYPE_MOD_DLY, line_i};
                gain_wr_data <= {11'd0, mod_delay_upd_r};
                sub <= 6'd52;
            end
            6'd52: begin
                // [TIM-2] gain_wr 중복 제거 (sub51에서 기록 완료)
                dl_rd_addr <= {line_i, mod_ptr_slot_r};
                apf_sh     <= apf_shift_sel(line_i);
                frac_r     <= mod_delay_upd_r[7:0];
                sub <= 6'd2;
            end
            6'd2: begin
                dl_rd_raw  <= dl_rd_data;
                dl_rd_addr <= {line_i,
                               (mod_ptr_slot_r == {DL_SLOT_BITS{1'b0}}) ?
                               (DL_SLOT[DL_SLOT_BITS-1:0] - {{(DL_SLOT_BITS-1){1'b0}},1'b1}) :
                               (mod_ptr_slot_r - {{(DL_SLOT_BITS-1){1'b0}},1'b1})};
                sub <= 6'd55;
            end
            6'd55: begin
                begin : interp_blk
                    reg signed [16:0] diff17;
                    reg signed [25:0] prod26;
                    diff17 = $signed({dl_rd_data[DATA_W-1], dl_rd_data}) -
                             $signed({dl_rd_raw[DATA_W-1],  dl_rd_raw});
                    prod26 = $signed(diff17) * $signed({1'b0, frac_r});
                    dl_rd_interp <= sat16(
                        $signed({{(ACC_W-DATA_W){dl_rd_raw[DATA_W-1]}}, dl_rd_raw}) +
                        ($signed({{(ACC_W-26){prod26[25]}}, prod26}) >>> 8)
                    );
                end
                sub <= 6'd3;
            end
            6'd3: begin
                if (apf_line_en(line_i)) begin
                    apf_d_reg  <= apf_ram[apf_idx(line_i)];
                    apf_diff_s <= $signed({apf_ram[apf_idx(line_i)][DATA_W-1],
                                           apf_ram[apf_idx(line_i)]}) -
                                  $signed({dl_rd_interp[DATA_W-1], dl_rd_interp});
                    sub <= 6'd4;
                end else begin
                    if (line_i<4'd8) fdn_out_l[line_i]<=dl_rd_interp;
                    else             fdn_out_r[line_i[2:0]]<=dl_rd_interp;
                    if (line_i==4'd15) begin
                        state<=FS_HAD_WAIT; sub<=6'd0;
                    end else begin
                        gain_rd_addr<={GTYPE_MOD_DLY,line_i+4'd1};
                        line_i<=line_i+4'd1; sub<=6'd0;
                    end
                end
            end
            6'd4: begin
                case (apf_sh)
                    2'd1: apf_out_reg=sat16({{(ACC_W-DATA_W){apf_d_reg[DATA_W-1]}},apf_d_reg}+
                                           ({{(ACC_W-DATA_W-1){apf_diff_s[DATA_W]}},apf_diff_s}>>>1));
                    2'd2: apf_out_reg=sat16({{(ACC_W-DATA_W){apf_d_reg[DATA_W-1]}},apf_d_reg}+
                                           ({{(ACC_W-DATA_W-1){apf_diff_s[DATA_W]}},apf_diff_s}>>>2));
                    2'd3: apf_out_reg=sat16({{(ACC_W-DATA_W){apf_d_reg[DATA_W-1]}},apf_d_reg}+
                                           ({{(ACC_W-DATA_W-1){apf_diff_s[DATA_W]}},apf_diff_s}>>>3));
                    default: apf_out_reg=dl_rd_interp;
                endcase
                apf_ram[apf_idx(line_i)]<=dl_rd_interp;
                if (line_i<4'd8) fdn_out_l[line_i]<=apf_out_reg;
                else             fdn_out_r[line_i[2:0]]<=apf_out_reg;
                if (line_i==4'd15) begin
                    state<=FS_HAD_WAIT; sub<=6'd0;
                end else begin
                    gain_rd_addr<={GTYPE_MOD_DLY,line_i+4'd1};
                    line_i<=line_i+4'd1; sub<=6'd0;
                end
            end
            default: sub<=6'd0;
            endcase
        end

        // ====================================================================
        FS_HAD_WAIT: begin
            if (sub<6'd3) begin sub<=sub+6'd1; end
            else begin
                if (reg_cross_enable && (cross_blend_clamped>32'd0)) begin
                    tmp_acc=$signed({{(ACC_W-DATA_W){had_r[1][DATA_W-1]}},had_r[1]})+
                            $signed({{(ACC_W-DATA_W){had_r[3][DATA_W-1]}},had_r[3]})-
                            $signed({{(ACC_W-DATA_W){had_r[5][DATA_W-1]}},had_r[5]});
                    cross_l_acc<=($signed(tmp_acc)*$signed({1'b0,cross_blend_clamped}))>>>COEFF_FRAC;
                    tmp_acc=$signed({{(ACC_W-DATA_W){had_l[0][DATA_W-1]}},had_l[0]})-
                            $signed({{(ACC_W-DATA_W){had_l[2][DATA_W-1]}},had_l[2]})+
                            $signed({{(ACC_W-DATA_W){had_l[6][DATA_W-1]}},had_l[6]});
                    if (reg_space_mode==SPACE_UNISON)
                        cross_r_acc<=-($signed(tmp_acc)*$signed({1'b0,cross_blend_clamped}))>>>COEFF_FRAC;
                    else
                        cross_r_acc<=($signed(tmp_acc)*$signed({1'b0,cross_blend_clamped}))>>>COEFF_FRAC;
                end else begin cross_l_acc<={ACC_W{1'b0}}; cross_r_acc<={ACC_W{1'b0}}; end
                state<=FS_FDN_WRITE; line_i<=4'd0; sub<=6'd0;
                filt_rd_addr<={line_i[3:0],FSEL_LP};
                gain_rd_addr<={GTYPE_GAIN_LF,4'd0};
            end
        end

        // ====================================================================
        // FS_FDN_WRITE
        // ====================================================================
        FS_FDN_WRITE: begin
            case (sub)
            6'd0: sub<=6'd1;
            6'd1: begin cur_lp<=filt_rd_data; filt_rd_addr<={line_i[3:0],FSEL_HP}; sub<=6'd2; end
            6'd2: begin
                cur_hp<=filt_rd_data; filt_rd_addr<={line_i[3:0],FSEL_BX1};
                cur_gain_lf<=gain_rd_data; gain_rd_addr<={GTYPE_GAIN_MF,line_i};
                sub<=6'd3;
            end
            6'd3: begin cur_bx1<=filt_rd_data; filt_rd_addr<={line_i[3:0],FSEL_BX2}; sub<=6'd4; end
            6'd4: begin
                cur_bx2<=filt_rd_data; filt_rd_addr<={line_i[3:0],FSEL_BY1};
                cur_gain_mf<=gain_rd_data; gain_rd_addr<={GTYPE_GAIN_HF,line_i};
                sub<=6'd5;
            end
            6'd5: begin cur_by1<=filt_rd_data; filt_rd_addr<={line_i[3:0],FSEL_BY2}; cur_gain_hf<=gain_rd_data; sub<=6'd6; end
            6'd6: begin cur_by2<=filt_rd_data; filt_rd_addr<={line_i[3:0],FSEL_DC};  sub<=6'd7; end
            6'd7: begin cur_dc<=filt_rd_data; filt_rd_addr<={line_i[3:0],FSEL_LPH}; sub<=6'd8; end
            6'd8: begin cur_lph<=filt_rd_data; filt_rd_addr<={line_i[3:0],FSEL_LPA}; sub<=6'd40; end
            6'd40: begin cur_lpa<=filt_rd_data; sub<=6'd41; end
            6'd41: begin
                if (line_i<4'd8)
                    tmp_acc=$signed({{(ACC_W-DATA_W){had_l[line_i][DATA_W-1]}},had_l[line_i]})+cross_l_acc;
                else
                    tmp_acc=$signed({{(ACC_W-DATA_W){had_r[line_i[2:0]][DATA_W-1]}},had_r[line_i[2:0]]})+cross_r_acc;
                inj_scaled_g<=$signed(($signed(inj_gain_sel(line_i[2:0]))*
                               $signed({1'b0,cur_inj_blend}))>>>15);
                tmp_acc<=tmp_acc; sub<=6'd42;
            end
            // [FIX-MUL-PIPE] FS_FDN_WRITE 승산기 파이프라인 정렬
            6'd42: begin
                inj_contrib=($signed({{(ACC_W-DATA_W){cur_prediff[DATA_W-1]}},cur_prediff})*
                             $signed(inj_scaled_g))>>>15;
                mul_a<=$signed(sat16(tmp_acc+inj_contrib)); mul_b<=cur_gain_lf;
                sub<=6'd9;
            end
            6'd9:  sub<=6'd10; // WAIT gain_lf
            6'd10: begin
                tmp_sig=mul_out;
                if (reg_bq_enable) begin
                    bq_in_r<=tmp_sig; mul_a<=$signed(tmp_sig); mul_b<=reg_bq_b0; sub<=6'd46;
                end else begin
                    mul_a<=$signed(tmp_sig-cur_lph); mul_b<=reg_lpf_hf_alpha; sub<=6'd53;
                end
            end
            6'd46: sub<=6'd12; // WAIT bq_b0
            6'd12: begin bq_sum_b0_r<=mul_res_r; mul_a<=$signed(cur_bx1); mul_b<=reg_bq_b1; sub<=6'd47; end
            6'd47: sub<=6'd13; // WAIT bq_b1
            6'd13: begin bq_sum_b1_r<=mul_res_r; mul_a<=$signed(cur_bx2); mul_b<=reg_bq_b2; sub<=6'd48; end
            6'd48: sub<=6'd14; // WAIT bq_b2
            6'd14: begin bq_sum_b2_r<=mul_res_r; mul_a<=$signed(cur_by1); mul_b<=reg_bq_a1; sub<=6'd49; end
            6'd49: sub<=6'd15; // WAIT bq_a1
            6'd15: begin bq_sum_a1_r<=mul_res_r; mul_a<=$signed(cur_by2); mul_b<=reg_bq_a2; sub<=6'd50; end
            6'd50: sub<=6'd16; // WAIT bq_a2
            6'd16: begin bq_sum_a2_r<=mul_res_r; sub<=6'd17; end
            6'd17: begin
                tmp_bq_pos=bq_sum_b0_r+bq_sum_b1_r+bq_sum_b2_r;
                tmp_bq_neg=bq_sum_a1_r+bq_sum_a2_r;
                tmp_bq_acc=tmp_bq_pos-tmp_bq_neg;
                cur_bx2<=cur_bx1; cur_bx1<=bq_in_r;
                cur_by2<=cur_by1; cur_by1<=sat16(tmp_bq_acc);
                mul_a<=$signed(sat16(tmp_bq_acc)); mul_b<=reg_bq_gain; sub<=6'd51;
            end
            6'd51: sub<=6'd18; // WAIT bq_gain
            6'd18: begin
                tmp_sig=mul_out;
                mul_a<=$signed(tmp_sig-cur_lph); mul_b<=reg_lpf_hf_alpha; sub<=6'd52;
            end
            6'd52: sub<=6'd32; // WAIT hf_alpha (BQ path)
            6'd53: sub<=6'd32; // WAIT hf_alpha (no-BQ path)
            6'd32: begin lph_delta_r<=mul_res_r; sub<=6'd45; end
            6'd45: begin
                cur_lph<=sat16({{(ACC_W-DATA_W){cur_lph[DATA_W-1]}},cur_lph}+lph_delta_r);
                damp_hf_sig<=sat16($signed({{(ACC_W-DATA_W){tmp_sig[DATA_W-1]}},tmp_sig})-
                                   $signed({{(ACC_W-DATA_W){cur_lph[DATA_W-1]}},cur_lph})-
                                   lph_delta_r);
                mul_a<=sat16({{(ACC_W-DATA_W){cur_lph[DATA_W-1]}},cur_lph}+lph_delta_r-
                             {{(ACC_W-DATA_W){cur_lpa[DATA_W-1]}},cur_lpa});
                mul_b<=reg_lpf_air_alpha; sub<=6'd54;
            end
            6'd54: sub<=6'd33; // WAIT air_alpha
            6'd33: begin
                cur_lpa<=sat16({{(ACC_W-DATA_W){cur_lpa[DATA_W-1]}},cur_lpa}+mul_res_r);
                damp_mf_sig<=sat16($signed({{(ACC_W-DATA_W){cur_lph[DATA_W-1]}},cur_lph})-
                                   $signed({{(ACC_W-DATA_W){cur_lpa[DATA_W-1]}},cur_lpa})-mul_res_r);
                damp_lf_sig<=sat16({{(ACC_W-DATA_W){cur_lpa[DATA_W-1]}},cur_lpa}+mul_res_r);
                mul_a<=damp_hf_sig; mul_b<=cur_gain_hf; sub<=6'd55;
            end
            6'd55: sub<=6'd34; // WAIT gain_hf
            6'd34: begin damp_fb_acc<=mul_res_r; mul_a<=damp_mf_sig; mul_b<=cur_gain_mf; sub<=6'd56; end
            6'd56: sub<=6'd35; // WAIT gain_mf
            6'd35: begin damp_fb_acc<=damp_fb_acc+mul_res_r; mul_a<=damp_lf_sig; mul_b<=cur_gain_lf; sub<=6'd57; end
            6'd57: sub<=6'd36; // WAIT gain_lf
            6'd36: begin damp_fb_acc<=damp_fb_acc+mul_res_r; sub<=6'd37; end
            6'd37: begin
                tmp_sig<=sat16(damp_fb_acc);
                mul_a<=sat16(damp_fb_acc-{{(ACC_W-DATA_W){cur_lp[DATA_W-1]}},cur_lp});
                mul_b<=cur_dyn_lpf_alpha; sub<=6'd58;
            end
            6'd58: sub<=6'd11; // WAIT dyn_lpf
            6'd11: begin
                cur_lp<=sat16({{(ACC_W-DATA_W){cur_lp[DATA_W-1]}},cur_lp}+mul_res_r);
                tmp_sig<=sat16({{(ACC_W-DATA_W){cur_lp[DATA_W-1]}},cur_lp}+mul_res_r);
                sub<=6'd19;
            end
            6'd19: begin
                if (reg_hpf_enable) begin
                    mul_a<=$signed(tmp_sig-cur_hp); mul_b<=reg_hpf_alpha; sub<=6'd59;
                end else sub<=6'd21;
            end
            6'd59: sub<=6'd20; // WAIT hpf_alpha
            6'd20: begin
                tmp_acc=$signed({{(ACC_W-DATA_W){cur_hp[DATA_W-1]}},cur_hp})+mul_res_r;
                cur_hp<=sat16(tmp_acc);
                tmp_sig<=sat16($signed({{(ACC_W-DATA_W){tmp_sig[DATA_W-1]}},tmp_sig})-
                               $signed({{(ACC_W-DATA_W){cur_hp[DATA_W-1]}},cur_hp})-mul_res_r);
                sub<=6'd21;
            end
            6'd21: begin
                tmp_dc_diff=$signed({{(ACC_W-DATA_W){tmp_sig[DATA_W-1]}},tmp_sig})-
                            $signed({{(ACC_W-DATA_W){cur_dc[DATA_W-1]}},cur_dc});
                cur_dc<=sat16($signed({{(ACC_W-DATA_W){cur_dc[DATA_W-1]}},cur_dc})+(tmp_dc_diff>>>10));
                tmp_sig<=sat16(tmp_dc_diff); sub<=6'd22;
            end
            6'd22: begin
                dl_wr_en<=1'b1; dl_wr_addr<={line_i,dl_wptr[line_i]}; dl_wr_data<=tmp_sig;
                dl_wptr[line_i]<=(dl_wptr[line_i]==DL_SLOT-1)?{DL_SLOT_BITS{1'b0}}:dl_wptr[line_i]+1'b1;
                filt_wr_en<=1'b1; filt_wr_addr<={line_i[3:0],FSEL_LP}; filt_wr_data<=cur_lp;
                sub<=6'd23;
            end
            6'd23: begin filt_wr_en<=1'b1; filt_wr_addr<={line_i[3:0],FSEL_HP};  filt_wr_data<=cur_hp; sub<=6'd24; end
            6'd24: begin filt_wr_en<=1'b1; filt_wr_addr<={line_i[3:0],FSEL_BX1}; filt_wr_data<=cur_bx1; sub<=6'd26; end
            6'd26: begin filt_wr_en<=1'b1; filt_wr_addr<={line_i[3:0],FSEL_BX2}; filt_wr_data<=cur_bx2; sub<=6'd27; end
            6'd27: begin filt_wr_en<=1'b1; filt_wr_addr<={line_i[3:0],FSEL_BY1}; filt_wr_data<=cur_by1; sub<=6'd28; end
            6'd28: begin filt_wr_en<=1'b1; filt_wr_addr<={line_i[3:0],FSEL_BY2}; filt_wr_data<=cur_by2; sub<=6'd29; end
            6'd29: begin filt_wr_en<=1'b1; filt_wr_addr<={line_i[3:0],FSEL_DC};  filt_wr_data<=cur_dc;  sub<=6'd43; end
            6'd43: begin filt_wr_en<=1'b1; filt_wr_addr<={line_i[3:0],FSEL_LPH}; filt_wr_data<=cur_lph; sub<=6'd44; end
            6'd44: begin
                filt_wr_en<=1'b1; filt_wr_addr<={line_i[3:0],FSEL_LPA}; filt_wr_data<=cur_lpa;
                if (line_i==4'd15) begin state<=FS_TAP_SUM; sub<=6'd0; end
                else begin
                    line_i<=line_i+4'd1;
                    filt_rd_addr<={(line_i+4'd1),FSEL_LP};
                    gain_rd_addr<={GTYPE_GAIN_LF,(line_i+4'd1)};
                    sub<=6'd0;
                end
            end
            default: sub<=6'd0;
            endcase
        end

        // ====================================================================
        // [FIX-MUL-PIPE] FS_TAP_SUM 승산기 파이프라인 정렬
        FS_TAP_SUM: begin
            begin : tap_lfo_blk
                reg signed [31:0] tap_lfo_adj;
                tap_lfo_adj = $signed({{16{lfo2_tap_mod[15]}}, lfo2_tap_mod}) <<< 9;
                case (sub)
                6'd0: begin tap_l_acc<={ACC_W{1'b0}};
                            mul_a<=$signed(had_l[0]);
                            mul_b<=(TAP_L0 + tap_lfo_adj > MAX_SPEC_RADIUS) ? MAX_SPEC_RADIUS :
                                   (TAP_L0 + tap_lfo_adj < GAIN_FLOOR)      ? GAIN_FLOOR      :
                                   $unsigned(TAP_L0 + tap_lfo_adj);
                            sub<=6'd9; end
                6'd9:  sub<=6'd1; // WAIT L0
                6'd1: begin tap_l_acc<=mul_res_r;
                            mul_a<=$signed(had_l[3]);
                            mul_b<=(TAP_L3 + tap_lfo_adj > MAX_SPEC_RADIUS) ? MAX_SPEC_RADIUS :
                                   (TAP_L3 + tap_lfo_adj < GAIN_FLOOR)      ? GAIN_FLOOR      :
                                   $unsigned(TAP_L3 + tap_lfo_adj);
                            sub<=6'd10; end
                6'd10: sub<=6'd2; // WAIT L3
                6'd2: begin tap_l_acc<=tap_l_acc+mul_res_r;
                            mul_a<=$signed(had_l[5]);
                            mul_b<=(TAP_L5 + tap_lfo_adj > MAX_SPEC_RADIUS) ? MAX_SPEC_RADIUS :
                                   (TAP_L5 + tap_lfo_adj < GAIN_FLOOR)      ? GAIN_FLOOR      :
                                   $unsigned(TAP_L5 + tap_lfo_adj);
                            sub<=6'd11; end
                6'd11: sub<=6'd3; // WAIT L5
                6'd3: begin tap_l_acc<=tap_l_acc+mul_res_r;
                            mul_a<=$signed(had_l[7]);
                            mul_b<=(TAP_L7 + tap_lfo_adj > MAX_SPEC_RADIUS) ? MAX_SPEC_RADIUS :
                                   (TAP_L7 + tap_lfo_adj < GAIN_FLOOR)      ? GAIN_FLOOR      :
                                   $unsigned(TAP_L7 + tap_lfo_adj);
                            sub<=6'd12; end
                6'd12: sub<=6'd4; // WAIT L7
                6'd4: begin
                    tap_l_sat<=sat16(tap_l_acc+mul_res_r);
                    tap_r_acc<={ACC_W{1'b0}};
                    mul_a<=$signed(had_r[1]);
                    mul_b<=(TAP_R1 + tap_lfo_adj > MAX_SPEC_RADIUS) ? MAX_SPEC_RADIUS :
                           (TAP_R1 + tap_lfo_adj < GAIN_FLOOR)      ? GAIN_FLOOR      :
                           $unsigned(TAP_R1 + tap_lfo_adj);
                    if (shimmer_blend_safe>32'd0) begin
                        shimmer_in_sig<=sat16(tap_l_acc+mul_res_r); shimmer_tick<=1'b1;
                    end
                    sub<=6'd13; end
                6'd13: sub<=6'd5; // WAIT R1
                6'd5: begin tap_r_acc<=mul_res_r;
                            mul_a<=$signed(had_r[2]);
                            mul_b<=(TAP_R2 + tap_lfo_adj > MAX_SPEC_RADIUS) ? MAX_SPEC_RADIUS :
                                   (TAP_R2 + tap_lfo_adj < GAIN_FLOOR)      ? GAIN_FLOOR      :
                                   $unsigned(TAP_R2 + tap_lfo_adj);
                            sub<=6'd14; end
                6'd14: sub<=6'd6; // WAIT R2
                6'd6: begin tap_r_acc<=tap_r_acc+mul_res_r;
                            mul_a<=$signed(had_r[4]);
                            mul_b<=(TAP_R4 + tap_lfo_adj > MAX_SPEC_RADIUS) ? MAX_SPEC_RADIUS :
                                   (TAP_R4 + tap_lfo_adj < GAIN_FLOOR)      ? GAIN_FLOOR      :
                                   $unsigned(TAP_R4 + tap_lfo_adj);
                            sub<=6'd15; end
                6'd15: sub<=6'd7; // WAIT R4
                6'd7: begin tap_r_acc<=tap_r_acc+mul_res_r;
                            mul_a<=$signed(had_r[6]);
                            mul_b<=(TAP_R6 + tap_lfo_adj > MAX_SPEC_RADIUS) ? MAX_SPEC_RADIUS :
                                   (TAP_R6 + tap_lfo_adj < GAIN_FLOOR)      ? GAIN_FLOOR      :
                                   $unsigned(TAP_R6 + tap_lfo_adj);
                            sub<=6'd16; end
                6'd16: sub<=6'd8; // WAIT R6
                6'd8: begin
                    tap_r_sat<=sat16(tap_r_acc+mul_res_r);
                    diff_rd_addr<=diff_base(5'd5)+diff_wp[5];
                    state<=FS_POST_DIFF; sub<=6'd0;
                end
                default: sub<=6'd0;
                endcase
            end
        end

        // ====================================================================
        FS_POST_DIFF: begin
            case (sub)
            6'd0: sub<=6'd1;
            6'd1: begin
                ap_rd=diff_rd_data; ap_tmp=$signed(tap_l_sat)-ap_rd;
                case (cur_post_diff_g[14:13])
                    2'b10: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_l=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd5)+diff_wp[5]; diff_wr_data<=tap_l_sat;
                diff_wp[5]<=(diff_wp[5]>=diff_maxidx(5'd5))?{DIFF_ADDR_W{1'b0}}:diff_wp[5]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_l; diff_rd_addr<=diff_base(5'd6)+diff_wp[6]; sub<=6'd2;
            end
            6'd2: sub<=6'd3;
            6'd3: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_post_diff_g[14:13])
                    2'b10: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_l=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd6)+diff_wp[6]; diff_wr_data<=tmp_sig;
                diff_wp[6]<=(diff_wp[6]>=diff_maxidx(5'd6))?{DIFF_ADDR_W{1'b0}}:diff_wp[6]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_l; diff_rd_addr<=diff_base(5'd7)+diff_wp[7]; sub<=6'd4;
            end
            6'd4: sub<=6'd5;
            6'd5: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_post_diff_g[14:13])
                    2'b10: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_l=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd7)+diff_wp[7]; diff_wr_data<=tmp_sig;
                diff_wp[7]<=(diff_wp[7]>=diff_maxidx(5'd7))?{DIFF_ADDR_W{1'b0}}:diff_wp[7]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_l; diff_rd_addr<=diff_base(5'd8)+diff_wp[8]; sub<=6'd6;
            end
            6'd6: sub<=6'd7;
            6'd7: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_post_diff_g[14:13])
                    2'b10: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_l=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd8)+diff_wp[8]; diff_wr_data<=tmp_sig;
                diff_wp[8]<=(diff_wp[8]>=diff_maxidx(5'd8))?{DIFF_ADDR_W{1'b0}}:diff_wp[8]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_l; diff_rd_addr<=diff_base(5'd9)+diff_wp[9]; sub<=6'd8;
            end
            6'd8: sub<=6'd9;
            6'd9: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_post_diff_g[14:13])
                    2'b10: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_l=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_l=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd9)+diff_wp[9]; diff_wr_data<=tmp_sig;
                diff_wp[9]<=(diff_wp[9]>=diff_maxidx(5'd9))?{DIFF_ADDR_W{1'b0}}:diff_wp[9]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                if (shimmer_blend_safe>32'd0) begin
                    shim_acc=($signed({{(ACC_W-DATA_W){shimmer_out_r[DATA_W-1]}},shimmer_out_r})*
                              $signed({1'b0,shimmer_blend_safe[23:0]}))>>>COEFF_FRAC;
                    wet_l<=sat16($signed({{(ACC_W-DATA_W){ap_out_l[DATA_W-1]}},ap_out_l})+shim_acc);
                end else wet_l<=ap_out_l;
                diff_rd_addr<=diff_base(5'd10)+diff_wp[10]; tmp_sig<=tap_r_sat; sub<=6'd10;
            end
            6'd10: sub<=6'd11;
            6'd11: begin
                ap_rd=diff_rd_data; ap_tmp=$signed(tap_r_sat)-ap_rd;
                case (cur_post_diff_g[14:13])
                    2'b10: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_r=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd10)+diff_wp[10]; diff_wr_data<=tap_r_sat;
                diff_wp[10]<=(diff_wp[10]>=diff_maxidx(5'd10))?{DIFF_ADDR_W{1'b0}}:diff_wp[10]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_r; diff_rd_addr<=diff_base(5'd11)+diff_wp[11]; sub<=6'd12;
            end
            6'd12: sub<=6'd13;
            6'd13: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_post_diff_g[14:13])
                    2'b10: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_r=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd11)+diff_wp[11]; diff_wr_data<=tmp_sig;
                diff_wp[11]<=(diff_wp[11]>=diff_maxidx(5'd11))?{DIFF_ADDR_W{1'b0}}:diff_wp[11]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_r; diff_rd_addr<=diff_base(5'd12)+diff_wp[12]; sub<=6'd14;
            end
            6'd14: sub<=6'd15;
            6'd15: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_post_diff_g[14:13])
                    2'b10: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_r=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd12)+diff_wp[12]; diff_wr_data<=tmp_sig;
                diff_wp[12]<=(diff_wp[12]>=diff_maxidx(5'd12))?{DIFF_ADDR_W{1'b0}}:diff_wp[12]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_r; diff_rd_addr<=diff_base(5'd13)+diff_wp[13]; sub<=6'd16;
            end
            6'd16: sub<=6'd17;
            6'd17: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_post_diff_g[14:13])
                    2'b10: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_r=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd13)+diff_wp[13]; diff_wr_data<=tmp_sig;
                diff_wp[13]<=(diff_wp[13]>=diff_maxidx(5'd13))?{DIFF_ADDR_W{1'b0}}:diff_wp[13]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                tmp_sig<=ap_out_r; diff_rd_addr<=diff_base(5'd14)+diff_wp[14]; sub<=6'd18;
            end
            6'd18: sub<=6'd19;
            6'd19: begin
                ap_rd=diff_rd_data; ap_tmp=tmp_sig-ap_rd;
                case (cur_post_diff_g[14:13])
                    2'b10: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>3));
                    2'b11: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>4));
                    2'b01: ap_out_r=ap_rd+$signed(ap_tmp>>>1);
                    default: ap_out_r=ap_rd+(ap_tmp-$signed(ap_tmp>>>2));
                endcase
                diff_wr_en<=1'b1; diff_wr_addr<=diff_base(5'd14)+diff_wp[14]; diff_wr_data<=tmp_sig;
                diff_wp[14]<=(diff_wp[14]>=diff_maxidx(5'd14))?{DIFF_ADDR_W{1'b0}}:diff_wp[14]+{{(DIFF_ADDR_W-1){1'b0}},1'b1};
                wet_r<=ap_out_r; state<=FS_GATE_PROC; sub<=6'd0;
            end
            default: sub<=6'd0;
            endcase
        end

        // ====================================================================
        FS_GATE_PROC: begin
            if (reg_reverb_type==RT_GATED || reg_space_a_type==RT_GATED) begin
                gate_din_r<=x_hold; gate_tick<=1'b1;
                wet_l<=sat16(($signed({{(ACC_W-DATA_W){wet_l[DATA_W-1]}},wet_l})*$signed({1'b0,gate_gain}))>>>DATA_W);
                wet_r<=sat16(($signed({{(ACC_W-DATA_W){wet_r[DATA_W-1]}},wet_r})*$signed({1'b0,gate_gain}))>>>DATA_W);
            end
            state<=FS_MIX; sub<=6'd0;
        end

        // ====================================================================
        // FS_MIX  [FIX-MUL-PIPE] 승산기 파이프라인 정렬
        // ====================================================================
        FS_MIX: begin
            case (sub)
            6'd0: begin mul_a<=$signed(x_hold); mul_b<=reg_dry_mix; sub<=6'd1; end
            6'd1: sub<=6'd2; // WAIT
            6'd2: begin
                mix_dry_r<=mul_out;
                if (reg_space_mode==SPACE_DUAL) begin mul_a<=$signed(wet_l); mul_b<=reg_space_a_mix; end
                else begin mul_a<=$signed(wet_l); mul_b<=reg_wet_mix; end
                sub<=6'd3;
            end
            6'd3: sub<=6'd4; // WAIT
            6'd4: begin
                mix_wet_l_r<=mul_out;
                if (reg_space_mode==SPACE_DUAL) begin mul_a<=$signed(wet_r); mul_b<=reg_space_b_mix; end
                else begin mul_a<=$signed(wet_r); mul_b<=reg_wet_mix; end
                sub<=6'd5;
            end
            6'd5: sub<=6'd6; // WAIT
            6'd6: begin
                mix_wet_r_r<=mul_out;
                mul_a<=$signed(er_out_l);
                mul_b<=(reg_space_mode==SPACE_DUAL)?reg_a_er_level:reg_er_level;
                sub<=6'd7;
            end
            6'd7: sub<=6'd8; // WAIT
            6'd8: begin
                mix_er_l_r<=mul_out;
                mul_a<=$signed(er_out_r);
                mul_b<=(reg_space_mode==SPACE_DUAL)?reg_b_er_level:reg_er_level;
                sub<=6'd9;
            end
            6'd9: sub<=6'd10; // WAIT
            6'd10: begin mix_er_r_r<=mul_out; state<=FS_WIDTH; sub<=6'd0; end
            default: sub<=6'd0;
            endcase
        end

        // ====================================================================
        FS_WIDTH: begin
            if (reg_space_mode==SPACE_DUAL) begin
                begin : dual_width_blk
                    reg signed [ACC_W-1:0] dw_mid, dw_side, dw_side_sc;
                    dw_mid   = $signed({{(ACC_W-DATA_W){mix_wet_l_r[DATA_W-1]}},mix_wet_l_r})+
                               $signed({{(ACC_W-DATA_W){mix_wet_r_r[DATA_W-1]}},mix_wet_r_r});
                    dw_side  = $signed({{(ACC_W-DATA_W){mix_wet_l_r[DATA_W-1]}},mix_wet_l_r})-
                               $signed({{(ACC_W-DATA_W){mix_wet_r_r[DATA_W-1]}},mix_wet_r_r});
                    dw_side_sc = ($signed(dw_side)*$signed({1'b0,reg_a_stereo_width}))>>>COEFF_FRAC;
                    final_l <= ((dw_mid+dw_side_sc)>>>1)+
                               $signed({{(ACC_W-DATA_W){mix_er_l_r[DATA_W-1]}},mix_er_l_r})+
                               $signed({{(ACC_W-DATA_W){mix_dry_r[DATA_W-1]}},mix_dry_r});
                    final_r <= ((dw_mid-dw_side_sc)>>>1)+
                               $signed({{(ACC_W-DATA_W){mix_er_r_r[DATA_W-1]}},mix_er_r_r})+
                               $signed({{(ACC_W-DATA_W){mix_dry_r[DATA_W-1]}},mix_dry_r});
                end
            end else begin
                begin : single_width_blk
                    reg signed [ACC_W-1:0] sw_side_sc_total;
                    // [STEREO-IN] side 복원: 앞단 stereo 공간감 보존
                    // x_side = (L-R)/2, reg_in_width Q1.15
                    reg signed [ACC_W-1:0] si_side_restore;
                    si_side_restore = ($signed({{(ACC_W-DATA_W){x_side[DATA_W-1]}},x_side}) *
                                       $signed({1'b0,reg_in_width})) >>> 15;
                    sw_mid=$signed({{(ACC_W-DATA_W){mix_wet_l_r[DATA_W-1]}},mix_wet_l_r})+
                           $signed({{(ACC_W-DATA_W){mix_wet_r_r[DATA_W-1]}},mix_wet_r_r});
                    sw_side=$signed({{(ACC_W-DATA_W){mix_wet_l_r[DATA_W-1]}},mix_wet_l_r})-
                            $signed({{(ACC_W-DATA_W){mix_wet_r_r[DATA_W-1]}},mix_wet_r_r});
                    sw_side_sc=($signed(sw_side)*$signed({1'b0,reg_a_stereo_width}))>>>COEFF_FRAC;
                    sw_side_sc_total = sw_side_sc + si_side_restore;
                    final_l<=((sw_mid+sw_side_sc_total)>>>1)+
                             $signed({{(ACC_W-DATA_W){mix_er_l_r[DATA_W-1]}},mix_er_l_r})+
                             $signed({{(ACC_W-DATA_W){mix_dry_r[DATA_W-1]}},mix_dry_r});
                    final_r<=((sw_mid-sw_side_sc_total)>>>1)+
                             $signed({{(ACC_W-DATA_W){mix_er_r_r[DATA_W-1]}},mix_er_r_r})+
                             $signed({{(ACC_W-DATA_W){mix_dry_r[DATA_W-1]}},mix_dry_r});
                end
            end
            state<=FS_OUTPUT; sub<=6'd0;
        end

        // ====================================================================
        FS_OUTPUT: begin
            out_l_r<=sat16(final_l); out_r_r<=sat16(final_r);
            state<=FS_DRAIN; drain_d1<=1'b0; sub<=6'd0;
        end

        FS_DRAIN: begin
            drain_d1<=1'b1;
            if (drain_d1 &&
                (!m_axis_l_tvalid || m_axis_l_tready) &&
                (!m_axis_r_tvalid || m_axis_r_tready)) begin
                state<=FS_IDLE;
                tready_r<=reg_enabled && !preset_loading;
            end
        end

        default: begin state<=FS_IDLE; tready_r<=1'b0; sub<=6'd0; end
        endcase
    end
end

// ============================================================================
//  출력 레지스터
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        m_axis_l_tvalid   <= 1'b0; m_axis_l_tdata <= 18'h0000;
        m_axis_r_tvalid   <= 1'b0; m_axis_r_tdata <= 18'h0000;
        m_axis_dma_tvalid <= 1'b0; m_axis_dma_tdata <= 32'h0000_0000;
    end else begin
        if (drain_d1 && !m_axis_l_tvalid) begin
            // 개별 L/R 채널 출력 (기존 유지)
            m_axis_l_tdata  <= out_l_r; m_axis_l_tvalid <= 1'b1;
            m_axis_r_tdata  <= out_r_r; m_axis_r_tvalid <= 1'b1;
            // DMA 32bit 스테레오 합산: {R[15:0], L[15:0]}
            // PS AXI DMA S2MM 포맷: 낮은 주소 = L, 높은 주소 = R
            m_axis_dma_tdata  <= {out_r_r[15:0], out_l_r[15:0]};
            m_axis_dma_tvalid <= 1'b1;
        end else begin
            if (m_axis_l_tvalid   && m_axis_l_tready)   m_axis_l_tvalid   <= 1'b0;
            if (m_axis_r_tvalid   && m_axis_r_tready)   m_axis_r_tvalid   <= 1'b0;
            if (m_axis_dma_tvalid && m_axis_dma_tready) m_axis_dma_tvalid <= 1'b0;
        end
    end
end

endmodule