`timescale 1ns / 1ps
// ============================================================================
//  unified_fx_chorus_top.v  Rev.5  (Analog-Warm Chorus)
//
//  ── Rev.5 변경 (Rev.4 대비) ──────────────────────────────────────────────
//
//  [WARM-1] Pre Harmonic Engine (LUT 0 추가 - DSP48 흡수)
//    - 입력 직후 soft saturation: y = x - x³·k_pre
//    - x²·k_even 으로 짝수 배음 (tube 느낌) 추가
//    - DSP48 2개로 x², x³ 계산 → LUT 전혀 안씀
//    - gain_pre = 0.75 (AXI reg으로 조정 가능)
//
//  [WARM-2] Pre LPF (LUT ~4 추가 - shift 기반)
//    - cutoff 조정 가능 (reg_ch_pre_lpf = α, shift 기반)
//    - α = reg >> 3 단계로 선택 → shift 연산만 사용
//    - BBD 느낌 고역 제한
//
//  [WARM-3] Modulation Nonlinearity (LUT ~8)
//    - 기존 삼각파 LFO에 cubic warp 적용
//    - lfo_nl = lfo - (lfo³ >>> 15)
//    - 중심부 부드럽게, 끝부분 강조
//    - DSP48 1개 (x³ 계산)
//
//  [WARM-4] Micro Feedback per Voice (LUT ~16)
//    - 각 voice에 약한 feedback 추가
//    - fb_state[8]: 시분할 공유 레지스터
//    - feedback = fb_gain * delay_out → delay_in 에 더하기
//    - fb_gain: 고정 0.125 (>>3) → LUT 최소
//
//  [WARM-5] Noise Injection (LUT 0 - LFSR 재활용)
//    - 기존 LFSR[7:0] 그대로 사용
//    - noise_level: AXI 레지스터로 조정
//    - 삽입 위치: pre-harmonic (입력에 살짝 섞기)
//
//  [WARM-6] Post Harmonic (약하게, DSP48 공유)
//    - mix 이후 soft clip
//    - Pre와 같은 DSP48 재활용 (시분할)
//
//  ── LUT 최적화 핵심 ─────────────────────────────────────────────────────
//  모든 비선형 연산을 DSP48에 흡수
//  LPF는 shift 연산만 (α = 1/8 ~ 1/64 선택)
//  Feedback은 시분할 레지스터 (BRAM 추가 없음)
//  Noise는 기존 LFSR 재활용 (LUT 추가 0)
//
//  ── 추가 AXI 레지스터 ───────────────────────────────────────────────────
//  0x4C  reg_ch_pre_lpf       pre LPF α (shift 기반: 0=1/8, 1=1/16, 2=1/32, 3=1/64)
//  0x50  reg_ch_harm_pre      pre harmonic gain (Q1.15)
//  0x54  reg_ch_harm_even     even harmonic amount (Q1.15)
//  0x58  reg_ch_harm_post     post harmonic gain (Q1.15)
//  0x5C  reg_ch_fb_gain       micro feedback gain (0=off, 1=1/8, 2=1/4)
//  0x60  reg_ch_noise         noise injection level (Q1.15, 0=off)
//  0x64  reg_ch_mod_nl        modulation nonlinearity (0=off, 1=on)
//
//  ── 타이밍 (Rev.4 유지) ─────────────────────────────────────────────────
//  50MHz / 48kHz = 1041 클럭/샘플
//  8 voices × 15 클럭/voice = 120 클럭 (+2 for feedback)
//  pre/post processing = 10 클럭
//  APF + mix + overhead = 40 클럭
//  총: ~170 클럭 < 1041 ✓
//
//  ── 리소스 증가 (Rev.4 대비) ─────────────────────────────────────────────
//  LUT: +~32 (shift, mux 최소화)
//  DSP48: +2~3 (harmonic x², x³)
//  BRAM: 0 (변화 없음)
//  FF: +~64 (feedback state 8개 + pre LPF state)
// ============================================================================

module unified_fx_chorus_top #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 8,
    parameter integer MAX_DELAY_BITS     = 14,
    parameter integer DATA_W             = 16,
    parameter integer NUM_VOICES         = 8
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI:s_axis:m_axis_l:m_axis_r, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
    input  wire                              S_AXI_ACLK,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 S_AXI_ARESETN RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                              S_AXI_ARESETN,

    // AXI-Lite
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

    // AXI-Stream 모노 입력
    input  wire                              s_axis_tvalid,
    input  wire signed [DATA_W-1:0]          s_axis_tdata,
    output wire                              s_axis_tready,

    // AXI-Stream 스테레오 출력
    output reg                               m_axis_l_tvalid,
    output reg  signed [DATA_W-1:0]          m_axis_l_tdata,
    input  wire                              m_axis_l_tready,
    output reg                               m_axis_r_tvalid,
    output reg  signed [DATA_W-1:0]          m_axis_r_tdata,
    input  wire                              m_axis_r_tready,

    // 외부 LFO 입력 (8포트)
    input  wire signed [15:0]                lfo_ext_0,
    input  wire signed [15:0]                lfo_ext_1,
    input  wire signed [15:0]                lfo_ext_2,
    input  wire signed [15:0]                lfo_ext_3,
    input  wire signed [15:0]                lfo_ext_4,
    input  wire signed [15:0]                lfo_ext_5,
    input  wire signed [15:0]                lfo_ext_6,
    input  wire signed [15:0]                lfo_ext_7
);

// ============================================================================
//  로컬파라미터
// ============================================================================
localparam DT_W        = MAX_DELAY_BITS + 8;
localparam BRAM_ADDR_W = MAX_DELAY_BITS;
localparam BRAM_DEPTH  = 1 << BRAM_ADDR_W;

localparam [DT_W-1:0] MIN_DELAY_Q = 22'h000400;

localparam signed [DATA_W-1:0] SAT16_MAX =  16'h7FFF;
localparam signed [DATA_W-1:0] SAT16_MIN = -16'h8000;

// Voice decorrelation prime offsets (Q14.8)
localparam [DT_W-1:0] PRIME_OFFSET_0 = 22'h000000;
localparam [DT_W-1:0] PRIME_OFFSET_1 = 22'h000B00;
localparam [DT_W-1:0] PRIME_OFFSET_2 = 22'h001300;
localparam [DT_W-1:0] PRIME_OFFSET_3 = 22'h001D00;
localparam [DT_W-1:0] PRIME_OFFSET_4 = 22'h002B00;
localparam [DT_W-1:0] PRIME_OFFSET_5 = 22'h003700;
localparam [DT_W-1:0] PRIME_OFFSET_6 = 22'h004300;
localparam [DT_W-1:0] PRIME_OFFSET_7 = 22'h005300;

// Pan positions Q1.15
localparam [14:0] PAN_0 = 15'h0000;
localparam [14:0] PAN_1 = 15'h1249;
localparam [14:0] PAN_2 = 15'h2492;
localparam [14:0] PAN_3 = 15'h36DB;
localparam [14:0] PAN_4 = 15'h4924;
localparam [14:0] PAN_5 = 15'h5B6D;
localparam [14:0] PAN_6 = 15'h6DB6;
localparam [14:0] PAN_7 = 15'h7FFF;

// LFO phase offsets Q0.16
localparam [15:0] PHASE_OFF_0 = 16'h0000;
localparam [15:0] PHASE_OFF_1 = 16'h2000;
localparam [15:0] PHASE_OFF_2 = 16'h4000;
localparam [15:0] PHASE_OFF_3 = 16'h6000;
localparam [15:0] PHASE_OFF_4 = 16'h8000;
localparam [15:0] PHASE_OFF_5 = 16'hA000;
localparam [15:0] PHASE_OFF_6 = 16'hC000;
localparam [15:0] PHASE_OFF_7 = 16'hE000;

// APF 파라미터
localparam AP_OUT0_D = 241; localparam AP_OUT0_AW = 8;
localparam AP_OUT1_D = 397; localparam AP_OUT1_AW = 9;
localparam AP_OUT2_D = 467; localparam AP_OUT2_AW = 9;
localparam AP_OUT3_D = 683; localparam AP_OUT3_AW = 10;
localparam AP_WAIT   = 4'd9;

// ============================================================================
//  AXI 레지스터 (Rev.4 유지 + Rev.5 추가)
// ============================================================================
reg        reg_bypass;
reg [15:0] reg_master_dry, reg_master_wet;
reg [DT_W-1:0] reg_ch_delay;
reg [15:0] reg_ch_depth;
reg [15:0] reg_ch_rate;
reg [2:0]  reg_ch_voices;
reg [15:0] reg_ch_drywet;
reg [15:0] reg_ch_diffusion;
reg [15:0] reg_ch_lpf;
reg [15:0] reg_ch_cross;
reg [15:0] reg_ch_width;
reg [15:0] reg_ch_drift_depth;
reg [15:0] reg_ch_jitter_depth;
reg [15:0] reg_ch_pitch_depth;
reg [15:0] reg_ch_saturation;
reg [15:0] reg_ch_tilt;
reg [15:0] reg_ch_hpf;
reg [15:0] reg_ch_depth_apout;

// [WARM-1~6] Rev.5 추가 레지스터
reg [1:0]  reg_ch_pre_lpf;     // pre LPF shift 선택: 0=>>3, 1=>>4, 2=>>5, 3=>>6
reg [15:0] reg_ch_harm_pre;    // pre harmonic x³ 계수 (Q1.15)
reg [15:0] reg_ch_harm_even;   // pre harmonic x² 계수 (Q1.15, tube 느낌)
reg [15:0] reg_ch_harm_post;   // post harmonic 계수 (Q1.15)
reg [1:0]  reg_ch_fb_gain;     // micro feedback: 0=off, 1=>>3(12.5%), 2=>>2(25%)
reg [15:0] reg_ch_noise;       // noise inject level (Q1.15)
reg        reg_ch_mod_nl;      // modulation nonlinearity on/off

// ============================================================================
//  AXI-Lite 쓰기
// ============================================================================
localparam [1:0] WR_IDLE=2'd0, WR_ACTIVE=2'd1, WR_RESP=2'd2;
reg [1:0] wr_state;
reg [C_S_AXI_ADDR_WIDTH-1:0] wr_addr;
reg [C_S_AXI_DATA_WIDTH-1:0] wdata_r;
reg wr_en;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        wr_state<=WR_IDLE; S_AXI_AWREADY<=0; S_AXI_WREADY<=0;
        S_AXI_BVALID<=0; S_AXI_BRESP<=0; wr_en<=0;
        reg_bypass<=0;
        reg_master_dry<=16'h7FFF; reg_master_wet<=16'h7FFF;
        reg_ch_delay<=22'h03C000;
        reg_ch_depth<=16'h1800;
        reg_ch_rate<=16'h0040;
        reg_ch_voices<=3'd7;
        reg_ch_drywet<=16'h5000;
        reg_ch_diffusion<=16'h4000;
        reg_ch_lpf<=16'h6000;
        reg_ch_cross<=16'h0C00;
        reg_ch_width<=16'h7FFF;
        reg_ch_drift_depth<=16'h0400;
        reg_ch_jitter_depth<=16'h0080;
        reg_ch_pitch_depth<=16'h0200;
        reg_ch_saturation<=16'h2000;
        reg_ch_tilt<=16'h0000;
        reg_ch_hpf<=16'h0100;
        reg_ch_depth_apout<=16'h0800;
        // Rev.5 기본값 (YM2203 최적)
        reg_ch_pre_lpf<=2'd1;       // α = 1/16 → cutoff ~3kHz
        reg_ch_harm_pre<=16'h1000;  // ~6% cubic (아주 약하게)
        reg_ch_harm_even<=16'h0800; // ~3% even (tube hint)
        reg_ch_harm_post<=16'h0400; // ~1.5% post (glue)
        reg_ch_fb_gain<=2'd1;       // 12.5% feedback
        reg_ch_noise<=16'h0040;     // -66dB noise
        reg_ch_mod_nl<=1'b1;        // modulation NL on
    end else begin
        S_AXI_AWREADY<=0; S_AXI_WREADY<=0; wr_en<=0;
        case(wr_state)
            WR_IDLE:   if(S_AXI_AWVALID) begin S_AXI_AWREADY<=1; wr_addr<=S_AXI_AWADDR; wr_state<=WR_ACTIVE; end
            WR_ACTIVE: if(S_AXI_WVALID)  begin wdata_r<=S_AXI_WDATA; S_AXI_WREADY<=1; wr_en<=1; wr_state<=WR_RESP; end
            WR_RESP: begin
                S_AXI_BVALID<=1; S_AXI_BRESP<=0;
                if(S_AXI_BVALID&&S_AXI_BREADY) begin S_AXI_BVALID<=0; wr_state<=WR_IDLE; end
            end
            default: wr_state<=WR_IDLE;
        endcase
        if(wr_en) begin
            case(wr_addr[7:2])
                6'h00: reg_bypass<=wdata_r[0];
                6'h01: reg_master_dry<=wdata_r[15:0];
                6'h02: reg_master_wet<=wdata_r[15:0];
                6'h03: reg_ch_delay<=wdata_r[DT_W-1:0];
                6'h04: reg_ch_depth<=wdata_r[15:0];
                6'h05: reg_ch_rate<=wdata_r[15:0];
                6'h06: reg_ch_voices<=wdata_r[2:0];
                6'h07: reg_ch_drywet<=wdata_r[15:0];
                6'h08: reg_ch_diffusion<=wdata_r[15:0];
                6'h09: reg_ch_lpf<=wdata_r[15:0];
                6'h0A: reg_ch_cross<=wdata_r[15:0];
                6'h0B: reg_ch_width<=wdata_r[15:0];
                6'h0C: reg_ch_drift_depth<=wdata_r[15:0];
                6'h0D: reg_ch_jitter_depth<=wdata_r[15:0];
                6'h0E: reg_ch_pitch_depth<=wdata_r[15:0];
                6'h0F: reg_ch_saturation<=wdata_r[15:0];
                6'h10: reg_ch_tilt<=wdata_r[15:0];
                6'h11: reg_ch_hpf<=wdata_r[15:0];
                6'h12: reg_ch_depth_apout<=wdata_r[15:0];
                // Rev.5 추가
                6'h13: reg_ch_pre_lpf<=wdata_r[1:0];
                6'h14: reg_ch_harm_pre<=wdata_r[15:0];
                6'h15: reg_ch_harm_even<=wdata_r[15:0];
                6'h16: reg_ch_harm_post<=wdata_r[15:0];
                6'h17: reg_ch_fb_gain<=wdata_r[1:0];
                6'h18: reg_ch_noise<=wdata_r[15:0];
                6'h19: reg_ch_mod_nl<=wdata_r[0];
                default: ;
            endcase
        end
    end
end

// AXI-Lite 읽기
localparam [1:0] RD_IDLE=2'd0, RD_SEND=2'd1;
reg [1:0] rd_state;
always @(posedge S_AXI_ACLK) begin
    if(!S_AXI_ARESETN) begin
        rd_state<=RD_IDLE; S_AXI_ARREADY<=0; S_AXI_RVALID<=0; S_AXI_RDATA<=0; S_AXI_RRESP<=0;
    end else begin
        S_AXI_ARREADY<=0;
        case(rd_state)
            RD_IDLE: if(S_AXI_ARVALID) begin
                S_AXI_ARREADY<=1;
                case(S_AXI_ARADDR[7:2])
                    6'h00: S_AXI_RDATA<={31'h0,reg_bypass};
                    6'h01: S_AXI_RDATA<={16'h0,reg_master_dry};
                    6'h02: S_AXI_RDATA<={16'h0,reg_master_wet};
                    6'h03: S_AXI_RDATA<={{(32-DT_W){1'b0}},reg_ch_delay};
                    6'h04: S_AXI_RDATA<={16'h0,reg_ch_depth};
                    6'h05: S_AXI_RDATA<={16'h0,reg_ch_rate};
                    6'h06: S_AXI_RDATA<={29'h0,reg_ch_voices};
                    6'h07: S_AXI_RDATA<={16'h0,reg_ch_drywet};
                    6'h08: S_AXI_RDATA<={16'h0,reg_ch_diffusion};
                    6'h09: S_AXI_RDATA<={16'h0,reg_ch_lpf};
                    6'h0A: S_AXI_RDATA<={16'h0,reg_ch_cross};
                    6'h0B: S_AXI_RDATA<={16'h0,reg_ch_width};
                    6'h0C: S_AXI_RDATA<={16'h0,reg_ch_drift_depth};
                    6'h0D: S_AXI_RDATA<={16'h0,reg_ch_jitter_depth};
                    6'h0E: S_AXI_RDATA<={16'h0,reg_ch_pitch_depth};
                    6'h0F: S_AXI_RDATA<={16'h0,reg_ch_saturation};
                    6'h10: S_AXI_RDATA<={16'h0,reg_ch_tilt};
                    6'h11: S_AXI_RDATA<={16'h0,reg_ch_hpf};
                    6'h12: S_AXI_RDATA<={16'h0,reg_ch_depth_apout};
                    6'h13: S_AXI_RDATA<={30'h0,reg_ch_pre_lpf};
                    6'h14: S_AXI_RDATA<={16'h0,reg_ch_harm_pre};
                    6'h15: S_AXI_RDATA<={16'h0,reg_ch_harm_even};
                    6'h16: S_AXI_RDATA<={16'h0,reg_ch_harm_post};
                    6'h17: S_AXI_RDATA<={30'h0,reg_ch_fb_gain};
                    6'h18: S_AXI_RDATA<={16'h0,reg_ch_noise};
                    6'h19: S_AXI_RDATA<={31'h0,reg_ch_mod_nl};
                    default: S_AXI_RDATA<=32'hDEAD_BEEF;
                endcase
                rd_state<=RD_SEND;
            end
            RD_SEND: begin
                S_AXI_RVALID<=1; S_AXI_RRESP<=0;
                if(S_AXI_RVALID&&S_AXI_RREADY) begin S_AXI_RVALID<=0; rd_state<=RD_IDLE; end
            end
            default: rd_state<=RD_IDLE;
        endcase
    end
end

// ============================================================================
//  BRAM (8 voice + 2 APF = 10 BRAM36E1, Rev.4 동일)
// ============================================================================
(* ram_style="block" *) reg signed [DATA_W-1:0] ram_v0 [0:BRAM_DEPTH-1];
(* ram_style="block" *) reg signed [DATA_W-1:0] ram_v1 [0:BRAM_DEPTH-1];
(* ram_style="block" *) reg signed [DATA_W-1:0] ram_v2 [0:BRAM_DEPTH-1];
(* ram_style="block" *) reg signed [DATA_W-1:0] ram_v3 [0:BRAM_DEPTH-1];
(* ram_style="block" *) reg signed [DATA_W-1:0] ram_v4 [0:BRAM_DEPTH-1];
(* ram_style="block" *) reg signed [DATA_W-1:0] ram_v5 [0:BRAM_DEPTH-1];
(* ram_style="block" *) reg signed [DATA_W-1:0] ram_v6 [0:BRAM_DEPTH-1];
(* ram_style="block" *) reg signed [DATA_W-1:0] ram_v7 [0:BRAM_DEPTH-1];
(* ram_style="block" *) reg signed [DATA_W-1:0] ram_apf_L [0:2047];
(* ram_style="block" *) reg signed [DATA_W-1:0] ram_apf_R [0:2047];

integer ii;
initial begin
    for(ii=0;ii<BRAM_DEPTH;ii=ii+1) begin
        ram_v0[ii]=0; ram_v1[ii]=0; ram_v2[ii]=0; ram_v3[ii]=0;
        ram_v4[ii]=0; ram_v5[ii]=0; ram_v6[ii]=0; ram_v7[ii]=0;
    end
    for(ii=0;ii<2048;ii=ii+1) begin ram_apf_L[ii]=0; ram_apf_R[ii]=0; end
end

reg [MAX_DELAY_BITS-1:0] wp [0:7];

reg [MAX_DELAY_BITS-1:0] bram_rd_addr;
reg [2:0]                bram_rd_vsel;
reg signed [DATA_W-1:0]  bram_rd_data;

always @(posedge S_AXI_ACLK) begin
    case(bram_rd_vsel)
        3'd0: bram_rd_data <= ram_v0[bram_rd_addr];
        3'd1: bram_rd_data <= ram_v1[bram_rd_addr];
        3'd2: bram_rd_data <= ram_v2[bram_rd_addr];
        3'd3: bram_rd_data <= ram_v3[bram_rd_addr];
        3'd4: bram_rd_data <= ram_v4[bram_rd_addr];
        3'd5: bram_rd_data <= ram_v5[bram_rd_addr];
        3'd6: bram_rd_data <= ram_v6[bram_rd_addr];
        3'd7: bram_rd_data <= ram_v7[bram_rd_addr];
    endcase
end

// ============================================================================
//  내부 LFO 엔진 (Rev.4 기반 + [WARM-3] Modulation Nonlinearity)
// ============================================================================
reg [15:0] lfo_phase [0:7];
reg signed [15:0] lfo_out [0:7];     // raw 삼각파
reg signed [15:0] lfo_nl_out [0:7];  // [WARM-3] nonlinear warped LFO

wire [15:0] lfo_rate_base = reg_ch_rate;
wire [15:0] lfo_rate_v [0:7];
assign lfo_rate_v[0] = lfo_rate_base;
assign lfo_rate_v[1] = lfo_rate_base + (lfo_rate_base>>5);
assign lfo_rate_v[2] = lfo_rate_base + (lfo_rate_base>>4);
assign lfo_rate_v[3] = lfo_rate_base + (lfo_rate_base>>3);
assign lfo_rate_v[4] = lfo_rate_base - (lfo_rate_base>>5);
assign lfo_rate_v[5] = lfo_rate_base - (lfo_rate_base>>4);
assign lfo_rate_v[6] = lfo_rate_base + (lfo_rate_base>>3)+(lfo_rate_base>>5);
assign lfo_rate_v[7] = lfo_rate_base - (lfo_rate_base>>3);

reg signed [15:0] lfo_ext_sm [0:7];

function signed [15:0] tri_wave;
    input [15:0] phase;
    begin
        if(!phase[15]) tri_wave = {1'b0, phase[14:0]};
        else           tri_wave = -{1'b0, ~phase[14:0]};
    end
endfunction

// [WARM-3] Modulation Nonlinearity: cubic warp (DSP48 1개)
// lfo_nl = lfo - (lfo³ >>> 15)
// 효과: 중심부 부드럽게, 끝부분 강조 → 아날로그 느낌
// DSP48 추론: signed multiply 34bit 이상이므로 자동 DSP48 흡수
function signed [15:0] nl_warp;
    input signed [15:0] x;
    reg signed [31:0] x2;
    reg signed [47:0] x3;
    reg signed [16:0] y;
    begin
        x2 = $signed(x) * $signed(x);      // Q2.30
        x3 = $signed(x2) * $signed(x);     // Q3.45 (47bit enough)
        // x³ >>> 30 → Q1.15 범위로 (입력 Q1.15, 출력 Q1.15)
        y = $signed(x) - $signed(x3[45:30]);
        if(y > 17'sh7FFF)       nl_warp = 16'h7FFF;
        else if(y < -17'sh8000) nl_warp = -16'h8000;
        else                    nl_warp = y[15:0];
    end
endfunction

reg lfo_update_en;

always @(posedge S_AXI_ACLK) begin
    if(!S_AXI_ARESETN) begin
        lfo_phase[0]<=PHASE_OFF_0; lfo_phase[1]<=PHASE_OFF_1;
        lfo_phase[2]<=PHASE_OFF_2; lfo_phase[3]<=PHASE_OFF_3;
        lfo_phase[4]<=PHASE_OFF_4; lfo_phase[5]<=PHASE_OFF_5;
        lfo_phase[6]<=PHASE_OFF_6; lfo_phase[7]<=PHASE_OFF_7;
        lfo_out[0]<=0; lfo_out[1]<=0; lfo_out[2]<=0; lfo_out[3]<=0;
        lfo_out[4]<=0; lfo_out[5]<=0; lfo_out[6]<=0; lfo_out[7]<=0;
        lfo_nl_out[0]<=0; lfo_nl_out[1]<=0; lfo_nl_out[2]<=0; lfo_nl_out[3]<=0;
        lfo_nl_out[4]<=0; lfo_nl_out[5]<=0; lfo_nl_out[6]<=0; lfo_nl_out[7]<=0;
        lfo_ext_sm[0]<=0; lfo_ext_sm[1]<=0; lfo_ext_sm[2]<=0; lfo_ext_sm[3]<=0;
        lfo_ext_sm[4]<=0; lfo_ext_sm[5]<=0; lfo_ext_sm[6]<=0; lfo_ext_sm[7]<=0;
    end else if(lfo_update_en) begin
        lfo_phase[0] <= lfo_phase[0] + lfo_rate_v[0];
        lfo_phase[1] <= lfo_phase[1] + lfo_rate_v[1];
        lfo_phase[2] <= lfo_phase[2] + lfo_rate_v[2];
        lfo_phase[3] <= lfo_phase[3] + lfo_rate_v[3];
        lfo_phase[4] <= lfo_phase[4] + lfo_rate_v[4];
        lfo_phase[5] <= lfo_phase[5] + lfo_rate_v[5];
        lfo_phase[6] <= lfo_phase[6] + lfo_rate_v[6];
        lfo_phase[7] <= lfo_phase[7] + lfo_rate_v[7];

        // 삼각파 raw
        lfo_out[0] <= tri_wave(lfo_phase[0]+PHASE_OFF_0);
        lfo_out[1] <= tri_wave(lfo_phase[1]+PHASE_OFF_1);
        lfo_out[2] <= tri_wave(lfo_phase[2]+PHASE_OFF_2);
        lfo_out[3] <= tri_wave(lfo_phase[3]+PHASE_OFF_3);
        lfo_out[4] <= tri_wave(lfo_phase[4]+PHASE_OFF_4);
        lfo_out[5] <= tri_wave(lfo_phase[5]+PHASE_OFF_5);
        lfo_out[6] <= tri_wave(lfo_phase[6]+PHASE_OFF_6);
        lfo_out[7] <= tri_wave(lfo_phase[7]+PHASE_OFF_7);

        // [WARM-3] Nonlinear warp (reg_ch_mod_nl로 on/off)
        if(reg_ch_mod_nl) begin
            lfo_nl_out[0] <= nl_warp(lfo_out[0]);
            lfo_nl_out[1] <= nl_warp(lfo_out[1]);
            lfo_nl_out[2] <= nl_warp(lfo_out[2]);
            lfo_nl_out[3] <= nl_warp(lfo_out[3]);
            lfo_nl_out[4] <= nl_warp(lfo_out[4]);
            lfo_nl_out[5] <= nl_warp(lfo_out[5]);
            lfo_nl_out[6] <= nl_warp(lfo_out[6]);
            lfo_nl_out[7] <= nl_warp(lfo_out[7]);
        end else begin
            lfo_nl_out[0] <= lfo_out[0]; lfo_nl_out[1] <= lfo_out[1];
            lfo_nl_out[2] <= lfo_out[2]; lfo_nl_out[3] <= lfo_out[3];
            lfo_nl_out[4] <= lfo_out[4]; lfo_nl_out[5] <= lfo_out[5];
            lfo_nl_out[6] <= lfo_out[6]; lfo_nl_out[7] <= lfo_out[7];
        end

        // 외부 LFO smoothing
        lfo_ext_sm[0] <= $signed(lfo_ext_sm[0]) + (($signed(lfo_ext_0)-$signed(lfo_ext_sm[0]))>>>3);
        lfo_ext_sm[1] <= $signed(lfo_ext_sm[1]) + (($signed(lfo_ext_1)-$signed(lfo_ext_sm[1]))>>>3);
        lfo_ext_sm[2] <= $signed(lfo_ext_sm[2]) + (($signed(lfo_ext_2)-$signed(lfo_ext_sm[2]))>>>3);
        lfo_ext_sm[3] <= $signed(lfo_ext_sm[3]) + (($signed(lfo_ext_3)-$signed(lfo_ext_sm[3]))>>>3);
        lfo_ext_sm[4] <= $signed(lfo_ext_sm[4]) + (($signed(lfo_ext_4)-$signed(lfo_ext_sm[4]))>>>3);
        lfo_ext_sm[5] <= $signed(lfo_ext_sm[5]) + (($signed(lfo_ext_5)-$signed(lfo_ext_sm[5]))>>>3);
        lfo_ext_sm[6] <= $signed(lfo_ext_sm[6]) + (($signed(lfo_ext_6)-$signed(lfo_ext_sm[6]))>>>3);
        lfo_ext_sm[7] <= $signed(lfo_ext_sm[7]) + (($signed(lfo_ext_7)-$signed(lfo_ext_sm[7]))>>>3);
    end
end

// ============================================================================
//  LFSR 기반 randomness (Rev.4 동일)
// ============================================================================
reg [31:0] lfsr [0:7];
initial begin
    lfsr[0]=32'hA5A5A5A5; lfsr[1]=32'h5A5A5A5A;
    lfsr[2]=32'hF0F0F0F0; lfsr[3]=32'h0F0F0F0F;
    lfsr[4]=32'hDEADBEEF; lfsr[5]=32'hCAFEBABE;
    lfsr[6]=32'h12345678; lfsr[7]=32'h87654321;
end

always @(posedge S_AXI_ACLK) begin
    if(!S_AXI_ARESETN) begin
        lfsr[0]<=32'hA5A5A5A5; lfsr[1]<=32'h5A5A5A5A;
        lfsr[2]<=32'hF0F0F0F0; lfsr[3]<=32'h0F0F0F0F;
        lfsr[4]<=32'hDEADBEEF; lfsr[5]<=32'hCAFEBABE;
        lfsr[6]<=32'h12345678; lfsr[7]<=32'h87654321;
    end else if(lfo_update_en) begin
        lfsr[0] <= {lfsr[0][0],lfsr[0][31:1]} ^ (lfsr[0][0] ? 32'h80200003 : 32'h0);
        lfsr[1] <= {lfsr[1][0],lfsr[1][31:1]} ^ (lfsr[1][0] ? 32'h80200003 : 32'h0);
        lfsr[2] <= {lfsr[2][0],lfsr[2][31:1]} ^ (lfsr[2][0] ? 32'h80200003 : 32'h0);
        lfsr[3] <= {lfsr[3][0],lfsr[3][31:1]} ^ (lfsr[3][0] ? 32'h80200003 : 32'h0);
        lfsr[4] <= {lfsr[4][0],lfsr[4][31:1]} ^ (lfsr[4][0] ? 32'h80200003 : 32'h0);
        lfsr[5] <= {lfsr[5][0],lfsr[5][31:1]} ^ (lfsr[5][0] ? 32'h80200003 : 32'h0);
        lfsr[6] <= {lfsr[6][0],lfsr[6][31:1]} ^ (lfsr[6][0] ? 32'h80200003 : 32'h0);
        lfsr[7] <= {lfsr[7][0],lfsr[7][31:1]} ^ (lfsr[7][0] ? 32'h80200003 : 32'h0);
    end
end

reg signed [31:0] drift_acc [0:7];
reg signed [15:0] drift_out [0:7];
always @(posedge S_AXI_ACLK) begin
    if(!S_AXI_ARESETN) begin
        drift_acc[0]<=0; drift_acc[1]<=0; drift_acc[2]<=0; drift_acc[3]<=0;
        drift_acc[4]<=0; drift_acc[5]<=0; drift_acc[6]<=0; drift_acc[7]<=0;
        drift_out[0]<=0; drift_out[1]<=0; drift_out[2]<=0; drift_out[3]<=0;
        drift_out[4]<=0; drift_out[5]<=0; drift_out[6]<=0; drift_out[7]<=0;
    end else if(lfo_update_en) begin
        drift_acc[0] <= drift_acc[0] + $signed({{16{lfsr[0][15]}},lfsr[0][15:0]}) - (drift_acc[0]>>>7);
        drift_acc[1] <= drift_acc[1] + $signed({{16{lfsr[1][15]}},lfsr[1][15:0]}) - (drift_acc[1]>>>7);
        drift_acc[2] <= drift_acc[2] + $signed({{16{lfsr[2][15]}},lfsr[2][15:0]}) - (drift_acc[2]>>>7);
        drift_acc[3] <= drift_acc[3] + $signed({{16{lfsr[3][15]}},lfsr[3][15:0]}) - (drift_acc[3]>>>7);
        drift_acc[4] <= drift_acc[4] + $signed({{16{lfsr[4][15]}},lfsr[4][15:0]}) - (drift_acc[4]>>>7);
        drift_acc[5] <= drift_acc[5] + $signed({{16{lfsr[5][15]}},lfsr[5][15:0]}) - (drift_acc[5]>>>7);
        drift_acc[6] <= drift_acc[6] + $signed({{16{lfsr[6][15]}},lfsr[6][15:0]}) - (drift_acc[6]>>>7);
        drift_acc[7] <= drift_acc[7] + $signed({{16{lfsr[7][15]}},lfsr[7][15:0]}) - (drift_acc[7]>>>7);
        drift_out[0] <= drift_acc[0][23:8]; drift_out[1] <= drift_acc[1][23:8];
        drift_out[2] <= drift_acc[2][23:8]; drift_out[3] <= drift_acc[3][23:8];
        drift_out[4] <= drift_acc[4][23:8]; drift_out[5] <= drift_acc[5][23:8];
        drift_out[6] <= drift_acc[6][23:8]; drift_out[7] <= drift_acc[7][23:8];
    end
end

reg signed [15:0] jitter_filt [0:7];
always @(posedge S_AXI_ACLK) begin
    if(!S_AXI_ARESETN) begin
        jitter_filt[0]<=0; jitter_filt[1]<=0; jitter_filt[2]<=0; jitter_filt[3]<=0;
        jitter_filt[4]<=0; jitter_filt[5]<=0; jitter_filt[6]<=0; jitter_filt[7]<=0;
    end else if(lfo_update_en) begin
        jitter_filt[0] <= $signed(jitter_filt[0]) - (jitter_filt[0]>>>2) + ($signed({lfsr[0][7:0],8'h0})>>>2);
        jitter_filt[1] <= $signed(jitter_filt[1]) - (jitter_filt[1]>>>2) + ($signed({lfsr[1][7:0],8'h0})>>>2);
        jitter_filt[2] <= $signed(jitter_filt[2]) - (jitter_filt[2]>>>2) + ($signed({lfsr[2][7:0],8'h0})>>>2);
        jitter_filt[3] <= $signed(jitter_filt[3]) - (jitter_filt[3]>>>2) + ($signed({lfsr[3][7:0],8'h0})>>>2);
        jitter_filt[4] <= $signed(jitter_filt[4]) - (jitter_filt[4]>>>2) + ($signed({lfsr[4][7:0],8'h0})>>>2);
        jitter_filt[5] <= $signed(jitter_filt[5]) - (jitter_filt[5]>>>2) + ($signed({lfsr[5][7:0],8'h0})>>>2);
        jitter_filt[6] <= $signed(jitter_filt[6]) - (jitter_filt[6]>>>2) + ($signed({lfsr[6][7:0],8'h0})>>>2);
        jitter_filt[7] <= $signed(jitter_filt[7]) - (jitter_filt[7]>>>2) + ($signed({lfsr[7][7:0],8'h0})>>>2);
    end
end

// ============================================================================
//  [WARM-1] Pre Harmonic Engine
//  y = x + x²·k_even - x³·k_pre
//  DSP48 자동 추론: 32bit/48bit 곱셈 → DSP48 흡수 (LUT 0 추가)
//  입력: Q1.15 (-32768~32767)
//  출력: Q1.15 (saturated)
// ============================================================================
function signed [15:0] harm_pre_fn;
    input signed [15:0] x;
    input [15:0] k_pre;    // x³ 계수 Q1.15
    input [15:0] k_even;   // x² 계수 Q1.15
    reg signed [31:0] x2;
    reg signed [47:0] x3;
    reg signed [31:0] cubic_term;
    reg signed [31:0] even_term;
    reg signed [18:0] y;
    begin
        x2 = $signed(x) * $signed(x);              // Q2.30
        x3 = $signed(x2[30:0]) * $signed(x);       // Q3.45

        // cubic_term = x³ * k_pre >> 30  (Q3.45 * Q1.15 >> 30 = Q1.15)
        // 단순화: x³>>>30 * k_pre >> 15
        cubic_term = $signed(x3[45:30]) * $signed({1'b0,k_pre});   // Q2.30
        // even_term = x² * k_even >> 30  (Q2.30 * Q1.15 >> 30 = Q1.15)
        even_term  = $signed(x2[30:15]) * $signed({1'b0,k_even});  // Q2.30

        // y = x - cubic + even (모두 Q1.15 기준으로 정렬)
        y = $signed({x[15],x[15],x,2'b00})         // x * 4 → Q3.13 기준
          - $signed(cubic_term[30:12])              // cubic/4
          + $signed(even_term[30:12]);              // even/4

        // saturate to Q1.15
        if(y > 19'sh07FFF)       harm_pre_fn = 16'h7FFF;
        else if(y < -19'sh08000) harm_pre_fn = -16'h8000;
        else                     harm_pre_fn = y[15:0];
    end
endfunction

// ============================================================================
//  [WARM-2] Pre LPF (shift 기반, DSP 미사용, LUT ~4)
//  α 선택: reg_ch_pre_lpf[1:0]
//    00 → shift 3 (α=1/8  → fc≈6kHz @ 48kHz)
//    01 → shift 4 (α=1/16 → fc≈3kHz @ 48kHz)  ← 기본
//    10 → shift 5 (α=1/32 → fc≈1.5kHz @ 48kHz)
//    11 → shift 6 (α=1/64 → fc≈750Hz  @ 48kHz)
// ============================================================================
reg signed [DATA_W-1:0] pre_lpf_state;

// pre_lpf_delta 계산 (shift 선택)
wire signed [DATA_W:0] pre_lpf_in_ext = $signed({harm_pre_out_w[15], harm_pre_out_w});
wire signed [DATA_W:0] pre_lpf_st_ext = $signed({pre_lpf_state[15], pre_lpf_state});
wire signed [DATA_W:0] pre_lpf_diff   = pre_lpf_in_ext - pre_lpf_st_ext;

// shift MUX (LUT ~4: 2bit selector)
wire signed [DATA_W:0] pre_lpf_delta =
    (reg_ch_pre_lpf==2'd0) ? $signed(pre_lpf_diff) >>> 3 :
    (reg_ch_pre_lpf==2'd1) ? $signed(pre_lpf_diff) >>> 4 :
    (reg_ch_pre_lpf==2'd2) ? $signed(pre_lpf_diff) >>> 5 :
                              $signed(pre_lpf_diff) >>> 6;

wire signed [DATA_W-1:0] pre_lpf_next = $signed(pre_lpf_state) + $signed(pre_lpf_delta[15:0]);

// harm_pre_out_w는 FSM에서 조합 (아래 harm_pre_reg 참조)
// 실제 연결: FSM ST_HPF에서 pre_lpf_state 업데이트
// harm_pre_out_w: FSM 레지스터
reg signed [DATA_W-1:0] harm_pre_reg; // pre harmonic 결과
wire signed [DATA_W-1:0] harm_pre_out_w = harm_pre_reg; // alias

// ============================================================================
//  [WARM-4] Micro Feedback per Voice (시분할 레지스터, BRAM 0 추가)
//  fb_state[v]: voice별 feedback 누산기 (FF 8×16 = 128 FF)
//  FSM에서 voice 처리 후 업데이트
//  feedback = delay_out * fb_gain → delay_input에 더하기
// ============================================================================
reg signed [DATA_W-1:0] fb_state [0:7]; // voice별 feedback 상태

// 현재 voice의 feedback 선택 (조합)
wire [2:0] cur_voice_fb_sel = cur_voice; // 아래 FSM에서 cur_voice 참조

wire signed [DATA_W-1:0] cur_fb =
    (cur_voice==3'd0) ? fb_state[0] :
    (cur_voice==3'd1) ? fb_state[1] :
    (cur_voice==3'd2) ? fb_state[2] :
    (cur_voice==3'd3) ? fb_state[3] :
    (cur_voice==3'd4) ? fb_state[4] :
    (cur_voice==3'd5) ? fb_state[5] :
    (cur_voice==3'd6) ? fb_state[6] : fb_state[7];

// feedback gain MUX (shift만 사용, LUT ~4)
wire signed [DATA_W-1:0] fb_contrib =
    (reg_ch_fb_gain==2'd0) ? 16'sd0 :
    (reg_ch_fb_gain==2'd1) ? $signed(cur_fb) >>> 3 :  // 12.5%
                              $signed(cur_fb) >>> 2;   // 25%

// BRAM write data: pre-processed input + feedback
// [WARM-5] Noise injection: LFSR[7:0] 직접 사용
// noise = lfsr[v][7:0] * reg_ch_noise >> 15 (아주 약함)
// 조합 계산: cur_voice 기준 lfsr 선택
wire [7:0] cur_lfsr_noise =
    (cur_voice==3'd0) ? lfsr[0][7:0] :
    (cur_voice==3'd1) ? lfsr[1][7:0] :
    (cur_voice==3'd2) ? lfsr[2][7:0] :
    (cur_voice==3'd3) ? lfsr[3][7:0] :
    (cur_voice==3'd4) ? lfsr[4][7:0] :
    (cur_voice==3'd5) ? lfsr[5][7:0] :
    (cur_voice==3'd6) ? lfsr[6][7:0] : lfsr[7][7:0];

// noise_contrib = noise_byte * reg_ch_noise >> 15
// noise_byte는 signed 8bit 확장
wire signed [23:0] noise_mul = $signed({{8{cur_lfsr_noise[7]}},cur_lfsr_noise}) * $signed({1'b0,reg_ch_noise});
wire signed [DATA_W-1:0] noise_contrib = $signed(noise_mul[23:8]);

// 최종 BRAM 입력 = pre_lpf_out + feedback + noise (포화 포함)
wire signed [DATA_W:0] mono_fb_raw =
    $signed({mono_in[15],mono_in}) + $signed({fb_contrib[15],fb_contrib}) + $signed({noise_contrib[15],noise_contrib});
wire signed [DATA_W-1:0] mono_fb_sat =
    mono_fb_raw[DATA_W] ? (mono_fb_raw[DATA_W-1] ? mono_fb_raw[DATA_W-1:0] : SAT16_MIN) :
                          (mono_fb_raw[DATA_W-1] ? SAT16_MAX : mono_fb_raw[DATA_W-1:0]);

// ============================================================================
//  Hermite 보간 파이프라인 (Rev.4 동일)
// ============================================================================
reg signed [DATA_W-1:0] h_ym1, h_y0, h_y1, h_y2;
reg [7:0] h_frac;
reg signed [28:0] h_p1;
reg signed [37:0] h_p2;
reg signed [34:0] h_p3;

wire signed [19:0] h_A3 =
    -$signed({{4{h_ym1[15]}},h_ym1})
    +($signed({{4{h_y0[15]}},h_y0})<<<1)+$signed({{4{h_y0[15]}},h_y0})
    -($signed({{4{h_y1[15]}},h_y1})<<<1)-$signed({{4{h_y1[15]}},h_y1})
    +$signed({{4{h_y2[15]}},h_y2});
wire signed [20:0] h_A2 =
    ($signed({{5{h_ym1[15]}},h_ym1})<<<1)
    -($signed({{5{h_y0[15]}},h_y0})<<<2)-$signed({{5{h_y0[15]}},h_y0})
    +($signed({{5{h_y1[15]}},h_y1})<<<2)-$signed({{5{h_y2[15]}},h_y2});
wire signed [17:0] h_A1 = -$signed({{2{h_ym1[15]}},h_ym1})+$signed({{2{h_y1[15]}},h_y1});
wire signed [17:0] h_A0 = {h_y0[15],h_y0,1'b0};

always @(posedge S_AXI_ACLK) begin
    if(!S_AXI_ARESETN) begin h_p1<=0; h_p2<=0; h_p3<=0; end
    else begin
        h_p1 <= $signed(h_A3)*$signed({1'b0,h_frac}) + ($signed(h_A2)<<<8);
        h_p2 <= $signed(h_p1)*$signed({1'b0,h_frac}) + ($signed(h_A1)<<<16);
        h_p3 <= $signed(h_p2[36:7])*$signed({1'b0,h_frac}) + ($signed(h_A0)<<<17);
    end
end
wire signed [16:0] h_raw = h_p3[34:18];

// ============================================================================
//  포화 함수
// ============================================================================
function signed [DATA_W-1:0] sat16;
    input signed [DATA_W:0] v;
    begin
        if(v>17'sd32767) sat16=16'h7FFF;
        else if(v<-17'sd32768) sat16=-16'h8000;
        else sat16=v[15:0];
    end
endfunction
function signed [DATA_W-1:0] sat16_17;
    input signed [16:0] v;
    begin
        if(v>17'sd32767) sat16_17=16'h7FFF;
        else if(v<-17'sd32768) sat16_17=-16'h8000;
        else sat16_17=v[15:0];
    end
endfunction
function signed [DATA_W-1:0] sat16_33;
    input signed [32:0] v;
    begin
        if(v>33'sh00007FFF) sat16_33=16'h7FFF;
        else if(v<-33'sh00008000) sat16_33=-16'h8000;
        else sat16_33=v[15:0];
    end
endfunction

// [WARM-6] Post Harmonic (out_L, out_R에 적용)
// Rev.4의 soft_sat 유지 + reg_ch_harm_post 사용
function signed [DATA_W-1:0] harm_post_fn;
    input signed [DATA_W-1:0] x;
    input [15:0] k_post;
    reg signed [31:0] x2;
    reg signed [47:0] x3;
    reg signed [31:0] cubic_term;
    reg signed [18:0] y;
    begin
        x2 = $signed(x) * $signed(x);
        x3 = $signed(x2[30:0]) * $signed(x);
        cubic_term = $signed(x3[45:30]) * $signed({1'b0,k_post});
        y = $signed({x[15],x[15],x,2'b00}) - $signed(cubic_term[30:12]);
        if(y > 19'sh07FFF)       harm_post_fn = 16'h7FFF;
        else if(y < -19'sh08000) harm_post_fn = -16'h8000;
        else                     harm_post_fn = y[15:0];
    end
endfunction

// Rev.4 soft_sat 유지 (기존 코드 호환)
function signed [DATA_W-1:0] soft_sat;
    input signed [DATA_W-1:0] x;
    input [15:0] sat_amt;
    reg signed [17:0] xe;
    reg signed [35:0] xcube;
    reg signed [17:0] y_lin, y_sat, y_mix;
    begin
        xe = {{2{x[15]}},x};
        xcube = $signed(xe)*$signed(xe)*$signed(xe);
        y_sat = xe - $signed(xcube[35:18])>>>2;
        y_mix = xe + $signed(($signed(y_sat-xe)*$signed({2'b0,sat_amt}))>>>16);
        if(y_mix>18'sh7FFF) soft_sat=16'h7FFF;
        else if(y_mix<-18'sh8000) soft_sat=-16'h8000;
        else soft_sat=y_mix[15:0];
    end
endfunction

// ============================================================================
//  Pre-processing: HPF (Rev.4 동일)
// ============================================================================
reg signed [DATA_W-1:0] hpf_state;
reg signed [DATA_W-1:0] mono_in;
reg signed [DATA_W-1:0] raw_in;

wire signed [DATA_W:0] hpf_delta = $signed({raw_in[15],raw_in}) - $signed({hpf_state[15],hpf_state});
wire signed [DATA_W-1:0] hpf_new_state = $signed(hpf_state) + $signed(($signed(hpf_delta)*$signed({1'b0,reg_ch_hpf}))>>>16);
wire signed [DATA_W-1:0] hpf_out = sat16(hpf_delta);

// ============================================================================
//  Voice 선택 조합 로직 (Rev.4 동일 + lfo_nl_out 사용)
// ============================================================================
reg [2:0] cur_voice;

wire [DT_W-1:0] prime_off =
    (cur_voice==3'd0) ? PRIME_OFFSET_0 : (cur_voice==3'd1) ? PRIME_OFFSET_1 :
    (cur_voice==3'd2) ? PRIME_OFFSET_2 : (cur_voice==3'd3) ? PRIME_OFFSET_3 :
    (cur_voice==3'd4) ? PRIME_OFFSET_4 : (cur_voice==3'd5) ? PRIME_OFFSET_5 :
    (cur_voice==3'd6) ? PRIME_OFFSET_6 : PRIME_OFFSET_7;

wire [14:0] cur_pan =
    (cur_voice==3'd0) ? PAN_0 : (cur_voice==3'd1) ? PAN_1 :
    (cur_voice==3'd2) ? PAN_2 : (cur_voice==3'd3) ? PAN_3 :
    (cur_voice==3'd4) ? PAN_4 : (cur_voice==3'd5) ? PAN_5 :
    (cur_voice==3'd6) ? PAN_6 : PAN_7;

wire [MAX_DELAY_BITS-1:0] cur_wp =
    (cur_voice==3'd0) ? wp[0] : (cur_voice==3'd1) ? wp[1] :
    (cur_voice==3'd2) ? wp[2] : (cur_voice==3'd3) ? wp[3] :
    (cur_voice==3'd4) ? wp[4] : (cur_voice==3'd5) ? wp[5] :
    (cur_voice==3'd6) ? wp[6] : wp[7];

// [WARM-3] lfo_nl_out 사용 (modulation NL 적용 LFO)
wire signed [15:0] cur_lfo =
    (cur_voice==3'd0) ? lfo_nl_out[0] : (cur_voice==3'd1) ? lfo_nl_out[1] :
    (cur_voice==3'd2) ? lfo_nl_out[2] : (cur_voice==3'd3) ? lfo_nl_out[3] :
    (cur_voice==3'd4) ? lfo_nl_out[4] : (cur_voice==3'd5) ? lfo_nl_out[5] :
    (cur_voice==3'd6) ? lfo_nl_out[6] : lfo_nl_out[7];

wire signed [15:0] cur_drift =
    (cur_voice==3'd0) ? drift_out[0] : (cur_voice==3'd1) ? drift_out[1] :
    (cur_voice==3'd2) ? drift_out[2] : (cur_voice==3'd3) ? drift_out[3] :
    (cur_voice==3'd4) ? drift_out[4] : (cur_voice==3'd5) ? drift_out[5] :
    (cur_voice==3'd6) ? drift_out[6] : drift_out[7];
wire signed [15:0] cur_jitter =
    (cur_voice==3'd0) ? jitter_filt[0] : (cur_voice==3'd1) ? jitter_filt[1] :
    (cur_voice==3'd2) ? jitter_filt[2] : (cur_voice==3'd3) ? jitter_filt[3] :
    (cur_voice==3'd4) ? jitter_filt[4] : (cur_voice==3'd5) ? jitter_filt[5] :
    (cur_voice==3'd6) ? jitter_filt[6] : jitter_filt[7];

wire signed [32:0] lfo_mod_s    = $signed(cur_lfo)*$signed({1'b0,reg_ch_depth});
wire signed [32:0] drift_mod_s  = $signed(cur_drift)*$signed({1'b0,reg_ch_drift_depth});
wire signed [32:0] jitter_mod_s = $signed(cur_jitter)*$signed({1'b0,reg_ch_jitter_depth});

wire signed [DT_W+2:0] dt_with_lfo =
    $signed({1'b0,reg_ch_delay}) + $signed({1'b0,prime_off})
    + $signed({{(DT_W+3-24){lfo_mod_s[32]}},lfo_mod_s[30:15],8'h00})
    + $signed({{(DT_W+3-24){drift_mod_s[32]}},drift_mod_s[30:15],8'h00})
    + $signed({{(DT_W+3-24){jitter_mod_s[32]}},jitter_mod_s[30:15],8'h00});

wire [DT_W-1:0] dt_clamped =
    dt_with_lfo[DT_W+2] ? MIN_DELAY_Q :
    (dt_with_lfo[DT_W+1:8] >= (1<<MAX_DELAY_BITS)) ? {{MAX_DELAY_BITS{1'b1}},8'hFF} :
    (dt_with_lfo[DT_W-1:0] < MIN_DELAY_Q) ? MIN_DELAY_Q :
    dt_with_lfo[DT_W-1:0];

wire signed [32:0] pitch_mod_s = $signed(cur_lfo)*$signed({1'b0,reg_ch_pitch_depth});
wire signed [8:0]  pitch_delta_q8 = $signed(pitch_mod_s[23:15]);

// ============================================================================
//  APF (Rev.4 동일)
// ============================================================================
reg ap_out0_en;
reg signed [DATA_W-1:0] ap_out_din_r, ap_g_out_r;
wire signed [DATA_W-1:0] ap_out0_out, ap_out1_out, ap_out2_out, ap_out3_out;
wire ap_out0_v, ap_out1_v, ap_out2_v, ap_out3_v;

schroeder_ap #(.DATA_W(DATA_W),.DELAY(AP_OUT0_D),.ADDR_W(AP_OUT0_AW)) u_ap_out0(
    .clk(S_AXI_ACLK),.rst_n(S_AXI_ARESETN),.en(ap_out0_en),.din(ap_out_din_r),.g(ap_g_out_r),.dout(ap_out0_out),.dout_valid(ap_out0_v));
schroeder_ap #(.DATA_W(DATA_W),.DELAY(AP_OUT1_D),.ADDR_W(AP_OUT1_AW)) u_ap_out1(
    .clk(S_AXI_ACLK),.rst_n(S_AXI_ARESETN),.en(ap_out0_v),.din(ap_out0_out),.g(ap_g_out_r),.dout(ap_out1_out),.dout_valid(ap_out1_v));
schroeder_ap #(.DATA_W(DATA_W),.DELAY(AP_OUT2_D),.ADDR_W(AP_OUT2_AW)) u_ap_out2(
    .clk(S_AXI_ACLK),.rst_n(S_AXI_ARESETN),.en(ap_out1_v),.din(ap_out1_out),.g(ap_g_out_r),.dout(ap_out2_out),.dout_valid(ap_out2_v));
schroeder_ap #(.DATA_W(DATA_W),.DELAY(AP_OUT3_D),.ADDR_W(AP_OUT3_AW)) u_ap_out3(
    .clk(S_AXI_ACLK),.rst_n(S_AXI_ARESETN),.en(ap_out2_v),.din(ap_out2_out),.g(ap_g_out_r),.dout(ap_out3_out),.dout_valid(ap_out3_v));

wire signed [DATA_W-1:0] apf_g_base = $signed({1'b0,reg_ch_diffusion[14:0]});
wire signed [32:0] apf_L_mod_s = $signed(lfo_ext_sm[0])*$signed({1'b0,reg_ch_depth_apout});
wire signed [32:0] apf_R_mod_s = $signed(lfo_ext_sm[1])*$signed({1'b0,reg_ch_depth_apout});
wire signed [17:0] apf_L_sum = $signed({2'b00,apf_g_base})+$signed({{2{apf_L_mod_s[30]}},apf_L_mod_s[30:15]});
wire signed [17:0] apf_R_sum = $signed({2'b00,apf_g_base})+$signed({{2{apf_R_mod_s[30]}},apf_R_mod_s[30:15]});
wire signed [DATA_W-1:0] apf_g_L = apf_L_sum[17] ? 16'sd0 : (apf_L_sum>18'sh7FFF ? 16'sh7FFF : apf_L_sum[15:0]);
wire signed [DATA_W-1:0] apf_g_R = apf_R_sum[17] ? 16'sd0 : (apf_R_sum>18'sh7FFF ? 16'sh7FFF : apf_R_sum[15:0]);

// ============================================================================
//  FSM 상태 정의 (Rev.5: ST_PRE_HARM, ST_PRE_LPF 추가)
// ============================================================================
localparam [7:0]
    ST_IDLE         = 8'd0,
    ST_LFO_UPDATE   = 8'd1,
    ST_HPF          = 8'd2,
    ST_PRE_HARM     = 8'd3,   // [WARM-1] Pre Harmonic (1clk)
    ST_PRE_LPF      = 8'd4,   // [WARM-2] Pre LPF (1clk)
    ST_WRITE_VOICE  = 8'd5,   // BRAM 쓰기 (feedback + noise 포함)
    ST_ADDR_CALC    = 8'd6,
    ST_RD_M1        = 8'd7,
    ST_RD_0         = 8'd8,
    ST_RD_1         = 8'd9,
    ST_RD_2         = 8'd10,
    ST_HERMI_1      = 8'd11,
    ST_HERMI_2      = 8'd12,
    ST_HERMI_3      = 8'd13,
    ST_FB_UPDATE    = 8'd14,  // [WARM-4] Feedback state 업데이트
    ST_VOICE_ACC    = 8'd15,
    ST_NEXT_VOICE   = 8'd16,
    ST_APF_L        = 8'd17,
    ST_APF_L_WAIT   = 8'd18,
    ST_APF_L_DONE   = 8'd19,
    ST_APF_R        = 8'd20,
    ST_APF_R_WAIT   = 8'd21,
    ST_APF_R_DONE   = 8'd22,
    ST_LPF_L        = 8'd23,
    ST_LPF_R        = 8'd24,
    ST_DRYWET       = 8'd25,
    ST_CROSS        = 8'd26,
    ST_POST_HARM    = 8'd27,  // [WARM-6] Post Harmonic
    ST_POST_SAT     = 8'd28,
    ST_OUTPUT       = 8'd29,
    ST_DRAIN        = 8'd30;

// ============================================================================
//  FSM 레지스터
// ============================================================================
reg [7:0] state;
reg [3:0] ap_wait_cnt;
reg tready_r;
assign s_axis_tready = tready_r;
wire fire_in = (s_axis_tvalid && tready_r);

reg signed [31:0] acc_L, acc_R;
reg signed [DATA_W-1:0] voice_out_r;

reg [MAX_DELAY_BITS-1:0] ha_m1, ha_0, ha_1, ha_2;
reg [7:0] ha_fr;

reg signed [DATA_W-1:0] lpf_st_L, lpf_st_R;
reg signed [DATA_W-1:0] chorus_L, chorus_R;
reg signed [DATA_W-1:0] out_L, out_R;

wire signed [32:0] lpf_L_delta = ($signed(chorus_L)-$signed(lpf_st_L))*$signed({1'b0,reg_ch_lpf});
wire signed [DATA_W-1:0] lpf_L_new = $signed(lpf_st_L)+$signed(lpf_L_delta[31:16]);
wire signed [32:0] lpf_R_delta = ($signed(chorus_R)-$signed(lpf_st_R))*$signed({1'b0,reg_ch_lpf});
wire signed [DATA_W-1:0] lpf_R_new = $signed(lpf_st_R)+$signed(lpf_R_delta[31:16]);

reg signed [DATA_W-1:0] mix_L, mix_R;
wire signed [32:0] dw_L_acc = $signed(mono_in)*$signed({1'b0,(16'h7FFF-reg_ch_drywet)})+$signed(chorus_L)*$signed({1'b0,reg_ch_drywet});
wire signed [DATA_W-1:0] dw_L_w = $signed(dw_L_acc[30:15]);
wire signed [32:0] dw_R_acc = $signed(mono_in)*$signed({1'b0,(16'h7FFF-reg_ch_drywet)})+$signed(chorus_R)*$signed({1'b0,reg_ch_drywet});
wire signed [DATA_W-1:0] dw_R_w = $signed(dw_R_acc[30:15]);

wire signed [31:0] cross_L_acc = $signed(mix_R)*$signed({1'b0,reg_ch_cross});
wire signed [31:0] cross_R_acc = $signed(mix_L)*$signed({1'b0,reg_ch_cross});
wire signed [DATA_W:0] cross_L_sum = $signed({mix_L[15],mix_L})+$signed({cross_L_acc[30],cross_L_acc[30:15]});
wire signed [DATA_W:0] cross_R_sum = $signed({mix_R[15],mix_R})+$signed({cross_R_acc[30],cross_R_acc[30:15]});

wire signed [32:0] final_L = $signed(raw_in)*$signed({1'b0,reg_master_dry})+$signed(out_L)*$signed({1'b0,reg_master_wet});
wire signed [32:0] final_R = $signed(raw_in)*$signed({1'b0,reg_master_dry})+$signed(out_R)*$signed({1'b0,reg_master_wet});
wire signed [DATA_W-1:0] final_L_w = $signed(final_L[30:15]);
wire signed [DATA_W-1:0] final_R_w = $signed(final_R[30:15]);

// ============================================================================
//  메인 FSM (Rev.5)
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if(!S_AXI_ARESETN) begin
        state<=ST_IDLE; tready_r<=0;
        cur_voice<=0; acc_L<=0; acc_R<=0;
        mono_in<=0; raw_in<=0;
        hpf_state<=0; pre_lpf_state<=0; harm_pre_reg<=0;
        lpf_st_L<=0; lpf_st_R<=0;
        chorus_L<=0; chorus_R<=0;
        mix_L<=0; mix_R<=0;
        out_L<=0; out_R<=0;
        voice_out_r<=0;
        lfo_update_en<=0;
        ap_out0_en<=0; ap_out_din_r<=0; ap_g_out_r<=0; ap_wait_cnt<=0;
        h_ym1<=0; h_y0<=0; h_y1<=0; h_y2<=0; h_frac<=0;
        ha_m1<=0; ha_0<=0; ha_1<=0; ha_2<=0; ha_fr<=0;
        bram_rd_addr<=0; bram_rd_vsel<=0;
        wp[0]<=0; wp[1]<=0; wp[2]<=0; wp[3]<=0;
        wp[4]<=0; wp[5]<=0; wp[6]<=0; wp[7]<=0;
        fb_state[0]<=0; fb_state[1]<=0; fb_state[2]<=0; fb_state[3]<=0;
        fb_state[4]<=0; fb_state[5]<=0; fb_state[6]<=0; fb_state[7]<=0;
    end else begin
        ap_out0_en<=0;
        lfo_update_en<=0;

        case(state)

        // ── IDLE ─────────────────────────────────────────────────────────
        ST_IDLE: begin
            tready_r<=1;
            if(fire_in) begin
                raw_in<=s_axis_tdata;
                tready_r<=0;
                state<=ST_LFO_UPDATE;
            end
        end

        // ── LFO / LFSR 업데이트 ──────────────────────────────────────────
        ST_LFO_UPDATE: begin
            lfo_update_en<=1;
            state<=ST_HPF;
        end

        // ── HPF ──────────────────────────────────────────────────────────
        ST_HPF: begin
            hpf_state <= hpf_new_state;
            // hpf_out을 pre harmonic으로 넘기기 위해 임시 저장
            mono_in <= hpf_out;
            state <= ST_PRE_HARM;
        end

        // ── [WARM-1] Pre Harmonic (1 clk) ────────────────────────────────
        // DSP48 추론: harm_pre_fn의 곱셈들 자동 흡수
        ST_PRE_HARM: begin
            harm_pre_reg <= harm_pre_fn(mono_in, reg_ch_harm_pre, reg_ch_harm_even);
            state <= ST_PRE_LPF;
        end

        // ── [WARM-2] Pre LPF (1 clk) ─────────────────────────────────────
        // shift 기반: DSP 0, LUT ~4
        ST_PRE_LPF: begin
            pre_lpf_state <= pre_lpf_next;
            mono_in <= pre_lpf_next;  // 이후 처리에 LPF 출력 사용
            cur_voice <= 0;
            acc_L <= 0; acc_R <= 0;
            state <= ST_WRITE_VOICE;
        end

        // ── VOICE 처리 루프 ───────────────────────────────────────────────
        // [WARM-4,5] feedback + noise가 mono_fb_sat에 포함됨
        ST_WRITE_VOICE: begin
            bram_rd_vsel <= cur_voice;
            case(cur_voice)
                3'd0: begin ram_v0[{1'b0,wp[0]}]<=mono_fb_sat; wp[0]<=wp[0]+1; end
                3'd1: begin ram_v1[{1'b0,wp[1]}]<=mono_fb_sat; wp[1]<=wp[1]+1; end
                3'd2: begin ram_v2[{1'b0,wp[2]}]<=mono_fb_sat; wp[2]<=wp[2]+1; end
                3'd3: begin ram_v3[{1'b0,wp[3]}]<=mono_fb_sat; wp[3]<=wp[3]+1; end
                3'd4: begin ram_v4[{1'b0,wp[4]}]<=mono_fb_sat; wp[4]<=wp[4]+1; end
                3'd5: begin ram_v5[{1'b0,wp[5]}]<=mono_fb_sat; wp[5]<=wp[5]+1; end
                3'd6: begin ram_v6[{1'b0,wp[6]}]<=mono_fb_sat; wp[6]<=wp[6]+1; end
                3'd7: begin ram_v7[{1'b0,wp[7]}]<=mono_fb_sat; wp[7]<=wp[7]+1; end
            endcase
            state<=ST_ADDR_CALC;
        end

        ST_ADDR_CALC: begin
            ha_m1 <= cur_wp - dt_clamped[DT_W-1:8] - 1;
            ha_0  <= cur_wp - dt_clamped[DT_W-1:8];
            ha_1  <= cur_wp - dt_clamped[DT_W-1:8] + 1;
            ha_2  <= cur_wp - dt_clamped[DT_W-1:8] + 2;
            ha_fr <= dt_clamped[7:0];
            bram_rd_addr <= cur_wp - dt_clamped[DT_W-1:8] - 1;
            state <= ST_RD_M1;
        end

        ST_RD_M1: begin h_ym1<=bram_rd_data; bram_rd_addr<=ha_0; state<=ST_RD_0; end
        ST_RD_0:  begin h_y0<=bram_rd_data;  bram_rd_addr<=ha_1; state<=ST_RD_1; end
        ST_RD_1:  begin h_y1<=bram_rd_data;  bram_rd_addr<=ha_2; state<=ST_RD_2; end
        ST_RD_2:  begin h_y2<=bram_rd_data;  h_frac<=ha_fr; state<=ST_HERMI_1; end
        ST_HERMI_1: state<=ST_HERMI_2;
        ST_HERMI_2: state<=ST_HERMI_3;
        ST_HERMI_3: begin
            voice_out_r <= sat16_17(h_raw);
            state <= ST_FB_UPDATE;
        end

        // ── [WARM-4] Feedback State 업데이트 ─────────────────────────────
        // voice_out_r → fb_state[cur_voice]
        // (delay_out이 다음 샘플 delay_input에 feedback됨)
        ST_FB_UPDATE: begin
            case(cur_voice)
                3'd0: fb_state[0] <= voice_out_r;
                3'd1: fb_state[1] <= voice_out_r;
                3'd2: fb_state[2] <= voice_out_r;
                3'd3: fb_state[3] <= voice_out_r;
                3'd4: fb_state[4] <= voice_out_r;
                3'd5: fb_state[5] <= voice_out_r;
                3'd6: fb_state[6] <= voice_out_r;
                3'd7: fb_state[7] <= voice_out_r;
            endcase
            state <= ST_VOICE_ACC;
        end

        // ── Voice → L/R 누산 ─────────────────────────────────────────────
        ST_VOICE_ACC: begin
            if(cur_voice <= {1'b0,reg_ch_voices}) begin
                acc_L <= acc_L + $signed($signed(voice_out_r)*$signed({2'b00,(15'h7FFF-cur_pan)}));
                acc_R <= acc_R + $signed($signed(voice_out_r)*$signed({2'b00,cur_pan}));
            end
            state <= ST_NEXT_VOICE;
        end

        ST_NEXT_VOICE: begin
            if(cur_voice >= {1'b0,reg_ch_voices}) begin
                state <= ST_APF_L;
            end else begin
                cur_voice <= cur_voice + 1;
                state <= ST_WRITE_VOICE;
            end
        end

        // ── APF L/R (Rev.4 동일) ─────────────────────────────────────────
        ST_APF_L: begin
            ap_out_din_r <= sat16_33($signed(acc_L[31:0])>>>18);
            ap_g_out_r <= apf_g_L;
            ap_out0_en <= 1;
            ap_wait_cnt <= AP_WAIT;
            state <= ST_APF_L_WAIT;
        end
        ST_APF_L_WAIT: begin
            if(ap_out3_v||(ap_wait_cnt==1)) state<=ST_APF_L_DONE;
            else ap_wait_cnt<=ap_wait_cnt-1;
        end
        ST_APF_L_DONE: begin chorus_L <= ap_out3_out; state <= ST_APF_R; end

        ST_APF_R: begin
            ap_out_din_r <= sat16_33($signed(acc_R[31:0])>>>18);
            ap_g_out_r <= apf_g_R;
            ap_out0_en <= 1;
            ap_wait_cnt <= AP_WAIT;
            state <= ST_APF_R_WAIT;
        end
        ST_APF_R_WAIT: begin
            if(ap_out3_v||(ap_wait_cnt==1)) state<=ST_APF_R_DONE;
            else ap_wait_cnt<=ap_wait_cnt-1;
        end
        ST_APF_R_DONE: begin chorus_R <= ap_out3_out; state <= ST_LPF_L; end

        // ── LPF ──────────────────────────────────────────────────────────
        ST_LPF_L: begin lpf_st_L <= lpf_L_new; chorus_L <= lpf_L_new; state <= ST_LPF_R; end
        ST_LPF_R: begin lpf_st_R <= lpf_R_new; chorus_R <= lpf_R_new; state <= ST_DRYWET; end

        // ── Dry/Wet ───────────────────────────────────────────────────────
        ST_DRYWET: begin
            mix_L <= sat16_17({dw_L_w[15],dw_L_w});
            mix_R <= sat16_17({dw_R_w[15],dw_R_w});
            state <= ST_CROSS;
        end

        // ── Crossfeed ─────────────────────────────────────────────────────
        ST_CROSS: begin
            out_L <= sat16(cross_L_sum);
            out_R <= sat16(cross_R_sum);
            state <= ST_POST_HARM;
        end

        // ── [WARM-6] Post Harmonic (1 clk) ───────────────────────────────
        ST_POST_HARM: begin
            out_L <= harm_post_fn(out_L, reg_ch_harm_post);
            out_R <= harm_post_fn(out_R, reg_ch_harm_post);
            state <= ST_POST_SAT;
        end

        // ── Post Saturation (Rev.4 유지) ─────────────────────────────────
        ST_POST_SAT: begin
            out_L <= soft_sat(out_L, reg_ch_saturation);
            out_R <= soft_sat(out_R, reg_ch_saturation);
            state <= ST_OUTPUT;
        end

        // ── OUTPUT ───────────────────────────────────────────────────────
        ST_OUTPUT: state <= ST_DRAIN;

        ST_DRAIN: begin
            if((!m_axis_l_tvalid||m_axis_l_tready)&&(!m_axis_r_tvalid||m_axis_r_tready)) begin
                state<=ST_IDLE; tready_r<=1;
            end
        end

        default: begin state<=ST_IDLE; tready_r<=0; end
        endcase
    end
end

// ============================================================================
//  출력 레지스터 (Rev.4 동일)
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if(!S_AXI_ARESETN) begin
        m_axis_l_tvalid<=0; m_axis_l_tdata<=0;
        m_axis_r_tvalid<=0; m_axis_r_tdata<=0;
    end else begin
        if(state==ST_OUTPUT) begin
            if(reg_bypass) begin
                m_axis_l_tdata<=raw_in; m_axis_r_tdata<=raw_in;
            end else begin
                if($signed(final_L_w)>$signed(SAT16_MAX)) m_axis_l_tdata<=SAT16_MAX;
                else if($signed(final_L_w)<$signed(SAT16_MIN)) m_axis_l_tdata<=SAT16_MIN;
                else m_axis_l_tdata<=final_L_w;
                if($signed(final_R_w)>$signed(SAT16_MAX)) m_axis_r_tdata<=SAT16_MAX;
                else if($signed(final_R_w)<$signed(SAT16_MIN)) m_axis_r_tdata<=SAT16_MIN;
                else m_axis_r_tdata<=final_R_w;
            end
            m_axis_l_tvalid<=1; m_axis_r_tvalid<=1;
        end else begin
            if(m_axis_l_tvalid&&m_axis_l_tready) m_axis_l_tvalid<=0;
            if(m_axis_r_tvalid&&m_axis_r_tready) m_axis_r_tvalid<=0;
        end
    end
end

endmodule