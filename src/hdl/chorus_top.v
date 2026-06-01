`timescale 1ns / 1ps
// ============================================================================
//  chorus_top.v  Rev.9  (Full TDM / BRAM-Centered / Minimum Resource)
//
//  ── Rev.9 완전 재설계 (OPT v1.0 준수) ──────────────────────────────────
//
//  [R1] VOICE 4개 (8→4): BRAM depth 절반
//  [R2] delay_mem 단일 배열 - 완전 TDM 1read+1write per voice
//  [R3] per-voice 배열 완전 제거: st_lfo_phase[8], st_fb[8], st_wp[8] 등
//       → wp[4], fb[4] 최소 배열만 유지 (4×14bit + 4×16bit = 120 FF)
//  [R4] APF 4개 완전 제거 (LUT/FF/BRAM 절감)
//  [R5] LFO 단일화: 단일 phase_acc + LFSR + drift (per-voice → case offset)
//  [R6] post-harm / soft-sat DSP 체인 완전 제거 (출력 sat만 유지)
//  [R7] PRIME_OFF / PAN_TBL → case 함수 (wire array / LUTRAM 방지)
//  [R8] wire 배열 완전 제거 (LFO_COEFF_SH, PHASE_OFF 등)
//
//  ── 리소스 목표 ─────────────────────────────────────────────────────────
//  BRAM  : 1~2   (4 voice × 16384 × 16bit = 4MB → MAX_DELAY_BITS=11이면 1 BRAM)
//  DSP   : ≤ 4   (interp×1, lpf×1, drywet×1, final×1)
//  LUT   : -60%+ (FSM + case ROM만)
//  FF    : -60%+ (per-voice 배열 제거, APF 제거)
//  Fmax  : 50MHz 안정
//
//  ─ 참고: BRAM 목표 1~2개를 위해 MAX_DELAY_BITS=11 권장 (2048 samples/voice)
//    현재 기본값 14 (16384 samples/voice) → BRAM ~2개 (BRAM36 사용시 1개)
// ============================================================================

module chorus_top #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 8,
    parameter integer MAX_DELAY_BITS     = 14,
    parameter integer DATA_W             = 16,
    parameter integer NUM_VOICES         = 4   // [R1] 8→4
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI:s_axis:m_axis_l:m_axis_r, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
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
    input  wire                              s_axis_tvalid,
    input  wire signed [DATA_W-1:0]          s_axis_tdata,
    output wire                              s_axis_tready,
    output reg                               m_axis_l_tvalid,
    output reg  signed [DATA_W-1:0]          m_axis_l_tdata,
    input  wire                              m_axis_l_tready,
    output reg                               m_axis_r_tvalid,
    output reg  signed [DATA_W-1:0]          m_axis_r_tdata,
    input  wire                              m_axis_r_tready,
    // 포트 호환성 유지 (내부 미사용)
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
//  파라미터
// ============================================================================
localparam DT_W       = MAX_DELAY_BITS + 8;   // 22bit Q14.8
localparam BRAM_DEPTH = 1 << MAX_DELAY_BITS;  // 16384 per voice
localparam VBRAM_AW   = MAX_DELAY_BITS + 2;   // [R1] 4 voice: +2bit
localparam [DT_W-1:0]  MIN_DELAY_Q  = 22'h000400;
localparam signed [DATA_W-1:0] SAT16_MAX =  16'h7FFF;
localparam signed [DATA_W-1:0] SAT16_MIN = -16'h8000;

// ============================================================================
//  AXI 레지스터
// ============================================================================
reg        reg_bypass;
reg [15:0] reg_master_dry, reg_master_wet;
reg [DT_W-1:0] reg_ch_delay;
reg [15:0] reg_ch_depth, reg_ch_rate;
reg [1:0]  reg_ch_voices;     // 0~3 (4voice 기준)
reg [15:0] reg_ch_drywet;
reg [15:0] reg_ch_lpf;
reg [15:0] reg_ch_cross;
reg [15:0] reg_ch_drift_depth;
reg [1:0]  reg_ch_fb_gain;
reg [15:0] reg_ch_noise;

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
        reg_bypass<=0; reg_master_dry<=16'h7FFF; reg_master_wet<=16'h7FFF;
        reg_ch_delay<=22'h03C000; reg_ch_depth<=16'h1800;
        reg_ch_rate<=16'h0040; reg_ch_voices<=2'd3;
        reg_ch_drywet<=16'h5000; reg_ch_lpf<=16'h6000;
        reg_ch_cross<=16'h0C00; reg_ch_drift_depth<=16'h0400;
        reg_ch_fb_gain<=2'd1; reg_ch_noise<=16'h0040;
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
        if(wr_en) case(wr_addr[7:2])
            6'h00: reg_bypass        <= wdata_r[0];
            6'h01: reg_master_dry    <= wdata_r[15:0];
            6'h02: reg_master_wet    <= wdata_r[15:0];
            6'h03: reg_ch_delay      <= wdata_r[DT_W-1:0];
            6'h04: reg_ch_depth      <= wdata_r[15:0];
            6'h05: reg_ch_rate       <= wdata_r[15:0];
            6'h06: reg_ch_voices     <= wdata_r[1:0];
            6'h07: reg_ch_drywet     <= wdata_r[15:0];
            6'h09: reg_ch_lpf        <= wdata_r[15:0];
            6'h0A: reg_ch_cross      <= wdata_r[15:0];
            6'h0C: reg_ch_drift_depth<= wdata_r[15:0];
            6'h17: reg_ch_fb_gain    <= wdata_r[1:0];
            6'h18: reg_ch_noise      <= wdata_r[15:0];
            default: ;
        endcase
    end
end

// AXI-Lite 읽기
localparam [1:0] RD_IDLE=2'd0, RD_SEND=2'd1;
reg [1:0] rd_state;
always @(posedge S_AXI_ACLK) begin
    if(!S_AXI_ARESETN) begin
        rd_state<=RD_IDLE; S_AXI_ARREADY<=0; S_AXI_RVALID<=0;
        S_AXI_RDATA<=0; S_AXI_RRESP<=0;
    end else begin
        S_AXI_ARREADY<=0;
        case(rd_state)
            RD_IDLE: if(S_AXI_ARVALID) begin
                S_AXI_ARREADY<=1;
                case(S_AXI_ARADDR[7:2])
                    6'h00: S_AXI_RDATA <= {31'h0, reg_bypass};
                    6'h01: S_AXI_RDATA <= {16'h0, reg_master_dry};
                    6'h02: S_AXI_RDATA <= {16'h0, reg_master_wet};
                    6'h03: S_AXI_RDATA <= {{(32-DT_W){1'b0}}, reg_ch_delay};
                    6'h04: S_AXI_RDATA <= {16'h0, reg_ch_depth};
                    6'h05: S_AXI_RDATA <= {16'h0, reg_ch_rate};
                    6'h06: S_AXI_RDATA <= {30'h0, reg_ch_voices};
                    6'h07: S_AXI_RDATA <= {16'h0, reg_ch_drywet};
                    6'h09: S_AXI_RDATA <= {16'h0, reg_ch_lpf};
                    6'h0A: S_AXI_RDATA <= {16'h0, reg_ch_cross};
                    6'h0C: S_AXI_RDATA <= {16'h0, reg_ch_drift_depth};
                    6'h17: S_AXI_RDATA <= {30'h0, reg_ch_fb_gain};
                    6'h18: S_AXI_RDATA <= {16'h0, reg_ch_noise};
                    default: S_AXI_RDATA <= 32'hDEAD_BEEF;
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
//  [R2] BRAM - 단일 배열, 완전 TDM (1read/cycle, 1write/cycle)
//  4 voice × BRAM_DEPTH × 16bit
//  BRAM inference 필수 패턴:
//    always @(posedge clk) begin
//      if (we) mem[waddr] <= din;
//      dout <= mem[raddr];
//    end
//  주의: waddr/raddr은 BRAM 출력에 의존하지 않는 등록 신호
// ============================================================================
(* ram_style = "block" *)
reg signed [DATA_W-1:0] delay_mem [0:(4*BRAM_DEPTH)-1];

reg                      bram_we;
reg [VBRAM_AW-1:0]      bram_waddr;
reg signed [DATA_W-1:0] bram_din;
reg [VBRAM_AW-1:0]      bram_raddr;
reg signed [DATA_W-1:0] bram_dout;

// BRAM 전용 always 블록 (단일)
always @(posedge S_AXI_ACLK) begin
    if (bram_we)
        delay_mem[bram_waddr] <= bram_din;
    bram_dout <= delay_mem[bram_raddr];
end

// ============================================================================
//  [R3] TDM 상태: per-voice 배열 최소화
//  wp[4]: write pointer (4×14bit = 56 FF)
//  fb[4]: feedback     (4×16bit = 64 FF)
//  총 120 FF (기존 8×(14+16+16+32+16+16+32) = 8×142 = 1136 FF 대비 -89%)
// ============================================================================
reg [MAX_DELAY_BITS-1:0] wp [0:3];
reg signed [DATA_W-1:0]  fb [0:3];

// ============================================================================
//  [R5] LFO 단일화 (per-voice FF 대폭 절감)
//  단일: lfo_phase, lfo_lfsr, lfo_drift_acc, lfo_drift_out
// ============================================================================
reg [15:0]        lfo_phase;
reg [31:0]        lfo_lfsr;
reg signed [31:0] lfo_drift_acc;
reg signed [15:0] lfo_drift_out;

// ============================================================================
//  [R7] case 함수 ROM (wire array 제거 → LUTRAM 방지)
// ============================================================================
// 4-voice phase offset (90° 간격)
function [15:0] phase_off_f;
    input [1:0] idx;
    begin
        case(idx)
            2'd0: phase_off_f = 16'h0000;
            2'd1: phase_off_f = 16'h4000;
            2'd2: phase_off_f = 16'h8000;
            2'd3: phase_off_f = 16'hC000;
            default: phase_off_f = 16'h0000;
        endcase
    end
endfunction

// 4-voice prime delay offset (Q14.8)
function [DT_W-1:0] prime_off_f;
    input [1:0] idx;
    begin
        case(idx)
            2'd0: prime_off_f = 22'h000000;
            2'd1: prime_off_f = 22'h001300;
            2'd2: prime_off_f = 22'h002B00;
            2'd3: prime_off_f = 22'h004300;
            default: prime_off_f = 22'h000000;
        endcase
    end
endfunction

// 4-voice pan (L=0, R=full)
function [14:0] pan_val_f;
    input [1:0] idx;
    begin
        case(idx)
            2'd0: pan_val_f = 15'h0000;
            2'd1: pan_val_f = 15'h2AAA;
            2'd2: pan_val_f = 15'h5555;
            2'd3: pan_val_f = 15'h7FFF;
            default: pan_val_f = 15'h4000;
        endcase
    end
endfunction

// tri_wave (MSB 1-level LUT)
function signed [15:0] tri_wave;
    input [15:0] ph;
    begin
        tri_wave = ph[15] ? -$signed({1'b0, ~ph[14:0]})
                          :  $signed({1'b0,  ph[14:0]});
    end
endfunction

// ============================================================================
//  포화 함수
// ============================================================================
function signed [DATA_W-1:0] sat16;
    input signed [DATA_W:0] v;
    begin
        sat16 = v[DATA_W] ? (v[DATA_W-1] ? v[DATA_W-1:0] : SAT16_MIN)
                          : (v[DATA_W-1] ? SAT16_MAX      : v[DATA_W-1:0]);
    end
endfunction

function signed [DATA_W-1:0] sat16_17;
    input signed [16:0] v;
    begin
        sat16_17 = v[16] ? (v[15] ? v[15:0] : SAT16_MIN)
                         : (v[15] ? SAT16_MAX : v[15:0]);
    end
endfunction

function signed [DATA_W-1:0] sat16_33;
    input signed [32:0] v;
    begin
        sat16_33 = v[32] ? (v[15] ? v[15:0] : SAT16_MIN)
                         : (v[15] ? SAT16_MAX : v[15:0]);
    end
endfunction

// ============================================================================
//  Pre-processing (HPF 고정 계수, pre-LPF)
// ============================================================================
reg signed [DATA_W-1:0] raw_in, mono_in;
reg signed [DATA_W-1:0] hpf_state, pre_lpf_state;

wire signed [DATA_W:0]   hpf_delta_w = $signed({raw_in[15], raw_in})
                                     - $signed({hpf_state[15], hpf_state});
wire signed [DATA_W-1:0] hpf_out_w   = sat16(hpf_delta_w);
// HPF: 고정 alpha = 0x0100 (soft HPF)
wire signed [DATA_W-1:0] hpf_new_w   = $signed(hpf_state)
    + $signed(($signed(hpf_delta_w) * $signed(16'sh0100)) >>> 16);

wire signed [DATA_W:0]   pre_lpf_diff_w = $signed({hpf_out_w[15], hpf_out_w})
                                        - $signed({pre_lpf_state[15], pre_lpf_state});
// pre-LPF: 고정 shift >>4
wire signed [DATA_W-1:0] pre_lpf_next_w = $signed(pre_lpf_state)
                                        + $signed($signed(pre_lpf_diff_w) >>> 4);

// ============================================================================
//  출력 믹스 wire
// ============================================================================
reg signed [DATA_W-1:0] chorus_L, chorus_R, out_L, out_R;
reg signed [31:0]        acc_L, acc_R;
reg signed [DATA_W-1:0] lpf_st_L, lpf_st_R;

// LPF (DSP 공유)
wire signed [32:0] lpf_L_delta = ($signed(chorus_L) - $signed(lpf_st_L))
                                * $signed({1'b0, reg_ch_lpf});
wire signed [32:0] lpf_R_delta = ($signed(chorus_R) - $signed(lpf_st_R))
                                * $signed({1'b0, reg_ch_lpf});
wire signed [DATA_W-1:0] lpf_L_new = $signed(lpf_st_L) + $signed(lpf_L_delta[31:16]);
wire signed [DATA_W-1:0] lpf_R_new = $signed(lpf_st_R) + $signed(lpf_R_delta[31:16]);

// DryWet (DSP 공유)
reg signed [DATA_W-1:0] dw_mono_r, dw_chL_r, dw_chR_r;
wire signed [32:0] dw_L_acc = $signed(dw_mono_r) * $signed({1'b0, (16'h7FFF - reg_ch_drywet)})
                             + $signed(dw_chL_r)  * $signed({1'b0, reg_ch_drywet});
wire signed [32:0] dw_R_acc = $signed(dw_mono_r) * $signed({1'b0, (16'h7FFF - reg_ch_drywet)})
                             + $signed(dw_chR_r)  * $signed({1'b0, reg_ch_drywet});
wire signed [DATA_W-1:0] dw_L_w = $signed(dw_L_acc[30:15]);
wire signed [DATA_W-1:0] dw_R_w = $signed(dw_R_acc[30:15]);

// Cross
reg signed [DATA_W-1:0] cross_mixL_r, cross_mixR_r, mix_L, mix_R;
wire signed [31:0] cross_L_acc = $signed(cross_mixR_r) * $signed({1'b0, reg_ch_cross});
wire signed [31:0] cross_R_acc = $signed(cross_mixL_r) * $signed({1'b0, reg_ch_cross});
wire signed [DATA_W:0] cross_L_sum = $signed({cross_mixL_r[15], cross_mixL_r})
                                   + $signed({cross_L_acc[30], cross_L_acc[30:15]});
wire signed [DATA_W:0] cross_R_sum = $signed({cross_mixR_r[15], cross_mixR_r})
                                   + $signed({cross_R_acc[30], cross_R_acc[30:15]});

// Final output
wire signed [32:0] final_L = $signed(raw_in) * $signed({1'b0, reg_master_dry})
                            + $signed(out_L)  * $signed({1'b0, reg_master_wet});
wire signed [32:0] final_R = $signed(raw_in) * $signed({1'b0, reg_master_dry})
                            + $signed(out_R)  * $signed({1'b0, reg_master_wet});
wire signed [DATA_W-1:0] final_L_w = $signed(final_L[30:15]);
wire signed [DATA_W-1:0] final_R_w = $signed(final_R[30:15]);

// ============================================================================
//  FSM 상태 (최소화, 5bit)
// ============================================================================
localparam [4:0]
    ST_IDLE         = 5'd0,
    ST_LFO_TICK     = 5'd1,   // [R5] LFO 단일 tick
    ST_HPF          = 5'd2,
    ST_PRE_LPF      = 5'd3,
    ST_VOICE_START  = 5'd4,   // voice 상태 래치
    ST_MOD_CALC     = 5'd5,   // LFO mod + delay 계산 + din 계산
    ST_ADDR         = 5'd6,   // ST0: raddr 등록 (BRAM 의존성 없음)
    ST_READ_A       = 5'd7,   // ST1: tap-a BRAM read
    ST_READ_B       = 5'd8,   // ST1: tap-b BRAM read
    ST_CALC         = 5'd9,   // ST2: linear interp DSP
    ST_WRITE        = 5'd10,  // BRAM 쓰기 + wp 갱신
    ST_ACC          = 5'd11,  // L/R 누적 + fb 갱신
    ST_NEXT_VOICE   = 5'd12,
    ST_LPF_INIT     = 5'd13,  // acc → chorus 등록
    ST_LPF_L        = 5'd14,
    ST_LPF_R        = 5'd15,
    ST_DRYWET_LATCH = 5'd16,
    ST_DRYWET_CALC  = 5'd17,
    ST_CROSS_LATCH  = 5'd18,
    ST_CROSS_CALC   = 5'd19,
    ST_OUTPUT       = 5'd20,
    ST_DRAIN        = 5'd21;

// ============================================================================
//  FSM 레지스터
// ============================================================================
reg [4:0]  state;
reg        tready_r;
assign s_axis_tready = tready_r;
wire fire_in = s_axis_tvalid && tready_r;

reg [1:0]  voice_idx;

// TDM 단일 레지스터 (voice별 상태 래치)
reg [MAX_DELAY_BITS-1:0] vl_wp;
reg signed [DATA_W-1:0]  vl_fb;
reg [14:0]               vl_pan;
reg [DT_W-1:0]           vl_dt;       // Q14.8 delay (클램핑 후)
reg [MAX_DELAY_BITS-1:0] vl_rbase;    // BRAM read base
reg [7:0]                vl_frac;
reg signed [DATA_W-1:0]  tap_a;       // BRAM tap a

// DSP 레지스터
reg signed [24:0]        lin_mul;     // (b-a)*frac [17bit×8bit=25bit]
reg signed [DATA_W-1:0]  interp_out;
reg signed [DATA_W-1:0]  voice_out;

// LFSR noise
reg [7:0]  lfsr_noise_r;

// ============================================================================
//  메인 FSM
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        state      <= ST_IDLE;
        tready_r   <= 0;
        voice_idx  <= 0;
        bram_we    <= 0;
        bram_waddr <= 0;
        bram_din   <= 0;
        bram_raddr <= 0;
        acc_L <= 0; acc_R <= 0;
        chorus_L <= 0; chorus_R <= 0;
        out_L    <= 0; out_R    <= 0;
        mix_L    <= 0; mix_R    <= 0;
        lpf_st_L <= 0; lpf_st_R <= 0;
        raw_in   <= 0; mono_in  <= 0;
        hpf_state <= 0; pre_lpf_state <= 0;
        lfo_phase <= 0;
        lfo_lfsr  <= 32'hA5A5A5A5;
        lfo_drift_acc <= 0; lfo_drift_out <= 0;
        vl_wp <= 0; vl_fb <= 0; vl_pan <= 0;
        vl_dt <= 0; vl_rbase <= 0; vl_frac <= 0;
        tap_a <= 0;
        lin_mul <= 0; interp_out <= 0; voice_out <= 0;
        dw_mono_r <= 0; dw_chL_r <= 0; dw_chR_r <= 0;
        cross_mixL_r <= 0; cross_mixR_r <= 0;
        lfsr_noise_r <= 0;
        begin : rst_v
            integer j;
            for (j = 0; j < 4; j = j+1) begin
                wp[j] <= 0;
                fb[j] <= 0;
            end
        end
    end else begin
        bram_we <= 0;  // 기본값: write disable

        case (state)

        // ── IDLE ─────────────────────────────────────────────────────────
        ST_IDLE: begin
            tready_r <= 1;
            if (fire_in) begin
                raw_in   <= s_axis_tdata;
                tready_r <= 0;
                state    <= ST_LFO_TICK;
            end
        end

        // ── [R5] LFO 단일 tick ─────────────────────────────────────────
        ST_LFO_TICK: begin
            lfo_lfsr <= {lfo_lfsr[0], lfo_lfsr[31:1]}
                      ^ (lfo_lfsr[0] ? 32'h80200003 : 32'h0);
            lfsr_noise_r  <= lfo_lfsr[7:0];
            lfo_drift_acc <= lfo_drift_acc
                           + $signed({{16{lfo_lfsr[15]}}, lfo_lfsr[15:0]})
                           - (lfo_drift_acc >>> 7);
            lfo_drift_out <= lfo_drift_acc[23:8];
            lfo_phase     <= lfo_phase + reg_ch_rate;
            state <= ST_HPF;
        end

        // ── Pre-processing ───────────────────────────────────────────────
        ST_HPF: begin
            hpf_state <= hpf_new_w;
            mono_in   <= hpf_out_w;
            state     <= ST_PRE_LPF;
        end

        ST_PRE_LPF: begin
            pre_lpf_state <= pre_lpf_next_w;
            mono_in       <= pre_lpf_next_w;
            acc_L         <= 0;
            acc_R         <= 0;
            voice_idx     <= 0;
            state         <= ST_VOICE_START;
        end

        // ── Voice TDM loop ───────────────────────────────────────────────
        ST_VOICE_START: begin
            vl_wp  <= wp[voice_idx];
            vl_fb  <= fb[voice_idx];
            vl_pan <= pan_val_f(voice_idx);
            state  <= ST_MOD_CALC;
        end

        // MOD_CALC: LFO → delay time + bram_din 준비
        // 이 사이클에서 bram_din을 계산해두고 ST_WRITE에서 사용
       ST_MOD_CALC: begin : mod_blk
            // 1. 변수 선언부 (블록 최상단)
            reg signed [32:0]       lfo_mod;
            reg signed [DT_W+2:0]   dt_raw;
            reg signed [DATA_W-1:0] mc_fb_c, mc_noise_c;
            reg signed [23:0]       mc_n_mul;
            reg signed [DATA_W:0]   mc_fb_sum;
            reg [15:0]              v_phase;
            
            reg [15:0]              tri_val;       // 함수 반환값 임시 저장용
            reg [15:0]              phase_off_val; // 배열(PHASE_OFF) 대체용

            // 2. PHASE_OFF 배열 접근을 case문으로 대체 (문법 에러 원천 차단)
            case (voice_idx)
                3'd0: phase_off_val = 16'h0000;
                3'd1: phase_off_val = 16'h2000;
                3'd2: phase_off_val = 16'h4000;
                3'd3: phase_off_val = 16'h6000;
                3'd4: phase_off_val = 16'h8000;
                3'd5: phase_off_val = 16'hA000;
                3'd6: phase_off_val = 16'hC000;
                3'd7: phase_off_val = 16'hE000;
                default: phase_off_val = 16'h0000;
            endcase

            // 3. 계산 수행
            v_phase = lfo_phase + phase_off_val; 

            // 함수 반환값을 직접 자르지 않고 임시 변수를 거침
            tri_val = tri_wave(v_phase);
            lfo_mod = $signed({tri_val[15], tri_val}) 
                    * $signed({1'b0, reg_ch_depth});

            // delay time Q14.8
            dt_raw = $signed({1'b0, reg_ch_delay})
                   + $signed({1'b0, prime_off_f(voice_idx)})
                   + $signed({{(DT_W+3-24){lfo_mod[32]}},
                              lfo_mod[30:15], 8'h00});

            vl_dt <= dt_raw[DT_W+2] ? MIN_DELAY_Q :
                     (dt_raw[DT_W+1:8] >= (1 << MAX_DELAY_BITS)) ?
                         {{MAX_DELAY_BITS{1'b1}}, 8'hFF} :
                     (dt_raw[DT_W-1:0] < MIN_DELAY_Q) ? MIN_DELAY_Q :
                                                        dt_raw[DT_W-1:0];

            // BRAM 쓰기 데이터 = mono_in + fb + noise
            mc_fb_c = (reg_ch_fb_gain == 2'd0) ? 16'sd0 :
                      (reg_ch_fb_gain == 2'd1) ? ($signed(vl_fb) >>> 3) :
                                                 ($signed(vl_fb) >>> 2);
            mc_n_mul   = $signed({{8{lfsr_noise_r[7]}}, lfsr_noise_r})
                       * $signed({1'b0, reg_ch_noise});
            mc_noise_c = $signed(mc_n_mul[23:8]);
            mc_fb_sum  = $signed({mono_in[15], mono_in})
                       + $signed({mc_fb_c[15],    mc_fb_c})
                       + $signed({mc_noise_c[15],  mc_noise_c});
            bram_din  <= mc_fb_sum[DATA_W] ?
                (mc_fb_sum[DATA_W-1] ? mc_fb_sum[DATA_W-1:0] : SAT16_MIN) :
                (mc_fb_sum[DATA_W-1] ? SAT16_MAX              : mc_fb_sum[DATA_W-1:0]);

            state <= ST_ADDR;
        end

        // ── [ST0] ADDR: raddr 등록 (BRAM 출력 의존 없음) ────────────────
        ST_ADDR: begin
            vl_rbase   <= vl_wp - vl_dt[DT_W-1:8];
            vl_frac    <= vl_dt[7:0];
            // tap-a raddr 등록
            bram_raddr <= {voice_idx, (vl_wp - vl_dt[DT_W-1:8])};
            state      <= ST_READ_A;
        end

        // ── [ST1-a] READ_A: tap-a read (bram_dout = tap-a 다음 사이클) ──
        ST_READ_A: begin
            tap_a <= bram_dout;                     // tap-a 래치
            bram_raddr <= {voice_idx, (vl_rbase + 1'b1)};  // tap-b raddr 등록
            state <= ST_READ_B;
        end

        // ── [ST1-b] READ_B: tap-b read ───────────────────────────────────
        ST_READ_B: begin
            // bram_dout = tap-b
            // [DSP 1] (b-a)*frac
            lin_mul <= ($signed({bram_dout[15], bram_dout}) - $signed({tap_a[15], tap_a}))
                     * $signed({1'b0, vl_frac});
            state <= ST_CALC;
        end

        // ── [ST2] CALC: interp 완료 ───────────────────────────────────────
        ST_CALC: begin
            // lin_mul settled → interp_out
            interp_out <= sat16_17($signed({tap_a[15], tap_a})
                        + $signed(lin_mul[23:8]));
            state <= ST_WRITE;
        end

        // ── WRITE: BRAM 쓰기 + wp 갱신 ───────────────────────────────────
        ST_WRITE: begin
            bram_we    <= 1;
            bram_waddr <= {voice_idx, vl_wp};
            // bram_din은 ST_MOD_CALC에서 계산됨
            wp[voice_idx] <= vl_wp + 1;
            voice_out  <= interp_out;
            state <= ST_ACC;
        end

        // ── ACC: L/R 누적 + fb 갱신 ──────────────────────────────────────
        ST_ACC: begin : acc_blk
            reg [14:0] pan_l;
            fb[voice_idx] <= voice_out;
            pan_l = 15'h7FFF - vl_pan;
            // [DSP 2] TDM pan 누적
            if (voice_idx <= reg_ch_voices) begin
                acc_L <= acc_L + ($signed(voice_out) * $signed({2'b00, pan_l}));
                acc_R <= acc_R + ($signed(voice_out) * $signed({2'b00, vl_pan}));
            end
            state <= ST_NEXT_VOICE;
        end

        ST_NEXT_VOICE: begin
            if (voice_idx >= reg_ch_voices) begin
                state <= ST_LPF_INIT;
            end else begin
                voice_idx <= voice_idx + 1;
                state     <= ST_VOICE_START;
            end
        end

        // ── Post: acc → chorus 변환 (APF 제거됨) ─────────────────────────
        ST_LPF_INIT: begin
            chorus_L <= sat16_33($signed(acc_L[31:0]) >>> 18);
            chorus_R <= sat16_33($signed(acc_R[31:0]) >>> 18);
            state    <= ST_LPF_L;
        end

        ST_LPF_L: begin
            lpf_st_L <= lpf_L_new;
            chorus_L <= lpf_L_new;
            state    <= ST_LPF_R;
        end
        ST_LPF_R: begin
            lpf_st_R <= lpf_R_new;
            chorus_R <= lpf_R_new;
            state    <= ST_DRYWET_LATCH;
        end

        // ── DryWet ────────────────────────────────────────────────────────
        ST_DRYWET_LATCH: begin
            dw_mono_r <= mono_in;
            dw_chL_r  <= chorus_L;
            dw_chR_r  <= chorus_R;
            state     <= ST_DRYWET_CALC;
        end
        ST_DRYWET_CALC: begin
            mix_L <= sat16_17({dw_L_w[15], dw_L_w});
            mix_R <= sat16_17({dw_R_w[15], dw_R_w});
            state <= ST_CROSS_LATCH;
        end

        // ── Cross ──────────────────────────────────────────────────────────
        ST_CROSS_LATCH: begin
            cross_mixL_r <= mix_L;
            cross_mixR_r <= mix_R;
            state <= ST_CROSS_CALC;
        end
        ST_CROSS_CALC: begin
            out_L <= sat16(cross_L_sum);
            out_R <= sat16(cross_R_sum);
            state <= ST_OUTPUT;
        end

        // ── 출력 ───────────────────────────────────────────────────────────
        ST_OUTPUT: state <= ST_DRAIN;

        ST_DRAIN: begin
            if ((!m_axis_l_tvalid || m_axis_l_tready) &&
                (!m_axis_r_tvalid || m_axis_r_tready)) begin
                state    <= ST_IDLE;
                tready_r <= 1;
            end
        end

        default: begin state <= ST_IDLE; tready_r <= 0; end
        endcase
    end
end

// ============================================================================
//  출력 레지스터
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        m_axis_l_tvalid <= 0; m_axis_l_tdata <= 0;
        m_axis_r_tvalid <= 0; m_axis_r_tdata <= 0;
    end else begin
        if (state == ST_OUTPUT) begin
            if (reg_bypass) begin
                m_axis_l_tdata <= raw_in;
                m_axis_r_tdata <= raw_in;
            end else begin
                m_axis_l_tdata <= ($signed(final_L_w) > $signed(SAT16_MAX)) ? SAT16_MAX :
                                  ($signed(final_L_w) < $signed(SAT16_MIN)) ? SAT16_MIN : final_L_w;
                m_axis_r_tdata <= ($signed(final_R_w) > $signed(SAT16_MAX)) ? SAT16_MAX :
                                  ($signed(final_R_w) < $signed(SAT16_MIN)) ? SAT16_MIN : final_R_w;
            end
            m_axis_l_tvalid <= 1;
            m_axis_r_tvalid <= 1;
        end else begin
            if (m_axis_l_tvalid && m_axis_l_tready) m_axis_l_tvalid <= 0;
            if (m_axis_r_tvalid && m_axis_r_tready) m_axis_r_tvalid <= 0;
        end
    end
end

endmodule