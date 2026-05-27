`timescale 1ns / 1ps
// ============================================================================
//  analog_delay_stereo_top.v  Rev.9  "Mono-In / Stereo+DMA Out"
//
//  Rev.9 = Rev.8 + 입력 모노화 + DMA 출력 포트 추가
//  -------------------------------------------------------------------------
//  [CHG-Rev.9] 입력 포트 변경:
//               s_axis_l / s_axis_r  →  s_axis_mono (단일 모노 포트)
//               내부에서 in_hold_L = in_hold_R = s_axis_mono_tdata
//
//  [ADD-Rev.9] DMA 출력 포트 추가 (audio_to_axis 연결용):
//               m_axis_dma_tdata  [31:0] = {ms_out_R[15:0], ms_out_L[15:0]}
//               m_axis_dma_tvalid
//               m_axis_dma_tready
//               ST_OUTPUT 에서 m_axis_l/r 과 동시에 래치
//               ST_DRAIN 에서 l/r handshake 완료 시 IDLE 복귀 (dma는 fire-and-forget)
//
//  [KEEP-Rev.8] Rev.2 architecture + Rev.7 additional registers
//  -------------------------------------------------------------------------
//  [KEEP-Rev.2] Direct feedback: echo_r -> fb_acc_w -> bram_wr_w
//               L/R independent APF ×4: u_ap0_L/R, u_ap1_L/R
//               echo_idx-based apply_diff conditional APF
//
//  [ADD-Rev.7]  reg_bypass       (0x04) : HW bypass
//               reg_damp_alpha   (0x54) : FB loop IIR damping (tape/BBD)
//               reg_damp_hf      (0x58) : output mix HF damping IIR
//               reg_ms_width     (0x4C) : M/S widening
//               reg_outhpf_bypass(0x50) : output HPF bypass
//
//  [AXI-MAP]
//    0x00  reg_delay_time       Q14.8 22bit
//    0x04  reg_bypass           [0]      (NEW)
//    0x08  reg_feedback         [15:0]
//    0x14  reg_dry_wet          [15:0]
//    0x30  reg_diffusion_start  [7:0]
//    0x34  reg_diffusion_mix    [15:0]
//    0x38  reg_diffusion_g      [15:0]
//    0x3C  reg_lfo_depth_apf    [15:0]
//    0x40  reg_slew_rate        [15:0]
//    0x44  reg_sat_thresh       [15:0]
//    0x4C  reg_ms_width         [15:0]   (NEW)
//    0x50  reg_outhpf_bypass    [0]      (NEW)
//    0x54  reg_damp_alpha       [15:0]   (NEW)
//    0x58  reg_damp_hf          [15:0]   (NEW)
// ============================================================================

module analog_delay_top #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 8,
    parameter integer MAX_DELAY_BITS     = 16,
    parameter integer DATA_W             = 16
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI:s_axis_mono:m_axis_l:m_axis_r:m_axis_dma, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
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

    // AXI-Stream 입력 (모노) - VCA 출력에 직결
    input  wire                              s_axis_mono_tvalid,
    input  wire signed [DATA_W-1:0]          s_axis_mono_tdata,
    output wire                              s_axis_mono_tready,

    // AXI-Stream 출력 - 스테레오 L/R (16비트 × 2)
    output reg                               m_axis_l_tvalid,
    output reg  signed [DATA_W-1:0]          m_axis_l_tdata,
    input  wire                              m_axis_l_tready,
    output reg                               m_axis_r_tvalid,
    output reg  signed [DATA_W-1:0]          m_axis_r_tdata,
    input  wire                              m_axis_r_tready,

    // AXI-Stream 출력 - DMA용 32비트 패킹 {R[15:0], L[15:0]} (audio_to_axis 연결)
    output reg                               m_axis_dma_tvalid,
    output reg  [31:0]                       m_axis_dma_tdata,
    input  wire                              m_axis_dma_tready,

    // [ARCH-4] LFO 8채널 입력 (L/R 분리 없음, MUX+offset 방식)
    // lfo[0..3] : APF 딜레이 변조용 (diff 조건부)
    // lfo[4..7] : 스테레오 오프셋 혼합용
    input  wire signed [15:0]                lfo_0,
    input  wire signed [15:0]                lfo_1,
    input  wire signed [15:0]                lfo_2,
    input  wire signed [15:0]                lfo_3,
    input  wire signed [15:0]                lfo_4,
    input  wire signed [15:0]                lfo_5,
    input  wire signed [15:0]                lfo_6,
    input  wire signed [15:0]                lfo_7
);

// ============================================================================
//  로컬파라미터
// ============================================================================
localparam DT_W        = MAX_DELAY_BITS + 8;   // 22bit Q14.8 (MAX_DELAY_BITS=14)
localparam BRAM_ADDR_W = MAX_DELAY_BITS + 1;
localparam BRAM_DEPTH  = 1 << BRAM_ADDR_W;    // 32768

localparam [DT_W-1:0] MIN_DELAY_Q = 24'h000400;  // 4샘플 하한

localparam signed [DATA_W-1:0] SAT16_MAX =  16'h7FFF;
localparam signed [DATA_W-1:0] SAT16_MIN = -16'h8000;

// [ARCH-3] 4 APF (L×2 + R×2)
// apf0 : 단순 확산 (짧은 딜레이)
// apf1 : 공간감 확산 (긴 딜레이)
localparam AP0_D  = 241;  localparam AP0_AW = 8;
localparam AP1_D  = 397;  localparam AP1_AW = 9;
localparam AP_WAIT = 4'd6;

localparam [15:0] FB_MAX = 16'h6000;   // 피드백 상한 0.75

// ============================================================================
//  AXI 레지스터
// ============================================================================
reg [DT_W-1:0] reg_delay_time;      // 0x00  딜레이 시간 Q14.8
reg [15:0]     reg_feedback;        // 0x08  피드백 0~0x6000
reg [15:0]     reg_dry_wet;         // 0x14  드라이/웨트 믹스
reg [7:0]      reg_diffusion_start; // 0x30  확산 시작 echo 인덱스 (0~15)
reg [15:0]     reg_diffusion_mix;   // 0x34  확산 강도 0~0x7FFF
reg [15:0]     reg_diffusion_g;     // 0x38  APF 계수 (Schroeder g)
reg [15:0]     reg_lfo_depth_apf;   // 0x3C  APF LFO 깊이
reg [15:0]     reg_slew_rate;       // 0x40  딜레이 슬루 속도
reg [15:0]     reg_sat_thresh;      // 0x44  소프트 클립 임계값
reg [15:0]     reg_ms_width;        // 0x4C  M/S 와이드닝
reg            reg_outhpf_bypass;   // 0x50  출력 HPF 바이패스
reg [15:0]     reg_damp_alpha;      // 0x54  피드백 루프 IIR 댐핑 α
reg [15:0]     reg_damp_hf;         // 0x58  출력 믹스 HF 댐핑 α
reg            reg_bypass;          // 0x04  HW 바이패스

// ============================================================================
//  AXI-Lite 쓰기 FSM
// ============================================================================
localparam [1:0] WR_IDLE = 2'd0, WR_ACTIVE = 2'd1, WR_RESP = 2'd2;
reg [1:0]                     wr_state;
reg [C_S_AXI_ADDR_WIDTH-1:0] wr_addr;
reg [C_S_AXI_DATA_WIDTH-1:0] wdata_r;
reg                           wr_en;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        wr_state          <= WR_IDLE;
        S_AXI_AWREADY     <= 1'b0; S_AXI_WREADY  <= 1'b0;
        S_AXI_BVALID      <= 1'b0; S_AXI_BRESP   <= 2'b00;
        wr_addr           <= {C_S_AXI_ADDR_WIDTH{1'b0}};
        wdata_r           <= 32'd0; wr_en <= 1'b0;
        // 기본값
        reg_delay_time      <= 22'h300000;  // ~256샘플 @ 48kHz
        reg_feedback        <= 16'h5000;    // ~0.63
        reg_dry_wet         <= 16'h4000;    // 50%
        reg_diffusion_start <= 8'd3;        // 3번째 echo부터 확산
        reg_diffusion_mix   <= 16'h3000;    // 확산 강도 ~37%
        reg_diffusion_g     <= 16'h5000;    // APF 계수 ~0.63
        reg_lfo_depth_apf   <= 16'h0200;    // APF LFO 깊이
        reg_slew_rate       <= 16'h0010;
        reg_sat_thresh      <= 16'd22000;
        reg_ms_width        <= 16'h4000;    // M/S 와이드닝 50%
        reg_outhpf_bypass   <= 1'b0;        // HPF 활성
        reg_damp_alpha      <= 16'h5800;    // FB 루프 댐핑 기본 (fc≈2.5kHz)
        reg_damp_hf         <= 16'h0000;    // 출력 믹스 댐핑 기본 OFF
        reg_bypass          <= 1'b0;
    end else begin
        S_AXI_AWREADY <= 1'b0; S_AXI_WREADY <= 1'b0;
        wr_en <= 1'b0;
        case (wr_state)
            WR_IDLE:   if (S_AXI_AWVALID) begin S_AXI_AWREADY<=1'b1; wr_addr<=S_AXI_AWADDR; wr_state<=WR_ACTIVE; end
            WR_ACTIVE: if (S_AXI_WVALID)  begin wdata_r<=S_AXI_WDATA; S_AXI_WREADY<=1'b1; wr_en<=1'b1; wr_state<=WR_RESP; end
            WR_RESP: begin
                S_AXI_BVALID <= 1'b1; S_AXI_BRESP <= 2'b00;
                if (S_AXI_BVALID && S_AXI_BREADY) begin S_AXI_BVALID<=1'b0; wr_state<=WR_IDLE; end
            end
            default: wr_state <= WR_IDLE;
        endcase
        if (wr_en) begin
            case (wr_addr[7:2])
                6'h00: reg_delay_time      <= wdata_r[DT_W-1:0];
                6'h01: reg_bypass          <= wdata_r[0];          // 0x04
                6'h02: reg_feedback        <= wdata_r[15:0];
                6'h05: reg_dry_wet         <= wdata_r[15:0];
                6'h0C: reg_diffusion_start <= wdata_r[7:0];   // 0x30
                6'h0D: reg_diffusion_mix   <= wdata_r[15:0];  // 0x34
                6'h0E: reg_diffusion_g     <= wdata_r[15:0];  // 0x38
                6'h0F: reg_lfo_depth_apf  <= wdata_r[15:0];  // 0x3C
                6'h10: reg_slew_rate       <= wdata_r[15:0];  // 0x40
                6'h11: reg_sat_thresh      <= wdata_r[15:0];  // 0x44
                6'h12: reg_bypass          <= wdata_r[0];     // 0x48 alias
                6'h13: reg_ms_width        <= wdata_r[15:0];  // 0x4C
                6'h14: reg_outhpf_bypass   <= wdata_r[0];     // 0x50
                6'h15: reg_damp_alpha      <= wdata_r[15:0];  // 0x54
                6'h16: reg_damp_hf         <= wdata_r[15:0];  // 0x58
                default: ;
            endcase
        end
    end
end

// AXI-Lite 읽기
localparam [1:0] RD_IDLE = 2'd0, RD_SEND = 2'd1;
reg [1:0] rd_state;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rd_state<=RD_IDLE; S_AXI_ARREADY<=1'b0;
        S_AXI_RVALID<=1'b0; S_AXI_RDATA<=32'd0; S_AXI_RRESP<=2'b00;
    end else begin
        S_AXI_ARREADY<=1'b0;
        case (rd_state)
            RD_IDLE: if (S_AXI_ARVALID) begin
                S_AXI_ARREADY<=1'b1;
                case (S_AXI_ARADDR[7:2])
                    6'h00: S_AXI_RDATA<={{(32-DT_W){1'b0}}, reg_delay_time};
                    6'h01: S_AXI_RDATA<={31'h0, reg_bypass};        // 0x04
                    6'h02: S_AXI_RDATA<={16'h0, reg_feedback};
                    6'h05: S_AXI_RDATA<={16'h0, reg_dry_wet};
                    6'h0C: S_AXI_RDATA<={24'h0, reg_diffusion_start};
                    6'h0D: S_AXI_RDATA<={16'h0, reg_diffusion_mix};
                    6'h0E: S_AXI_RDATA<={16'h0, reg_diffusion_g};
                    6'h0F: S_AXI_RDATA<={16'h0, reg_lfo_depth_apf};
                    6'h10: S_AXI_RDATA<={16'h0, reg_slew_rate};
                    6'h11: S_AXI_RDATA<={16'h0, reg_sat_thresh};
                    6'h12: S_AXI_RDATA<={31'h0, reg_bypass};        // 0x48 alias
                    6'h13: S_AXI_RDATA<={16'h0, reg_ms_width};
                    6'h14: S_AXI_RDATA<={31'h0, reg_outhpf_bypass};
                    6'h15: S_AXI_RDATA<={16'h0, reg_damp_alpha};
                    6'h16: S_AXI_RDATA<={16'h0, reg_damp_hf};
                    default: S_AXI_RDATA<=32'hDEAD_BEEF;
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
//  BRAM - 15bit 주소, L/R 파티션 공유
//    bit[14]=ch_sel: 0→L(0x0000~0x3FFF), 1→R(0x4000~0x7FFF)
// ============================================================================
(* ram_style = "block" *)
reg signed [DATA_W-1:0] delay_ram [0:BRAM_DEPTH-1];

integer init_i;
initial begin
    for (init_i = 0; init_i < BRAM_DEPTH; init_i = init_i + 1)
        delay_ram[init_i] = {DATA_W{1'b0}};
end

// 채널별 쓰기 포인터
reg [MAX_DELAY_BITS-1:0] wr_ptr_L;
reg [MAX_DELAY_BITS-1:0] wr_ptr_R;

// TDM 채널 선택
reg                       ch_sel;   // 0=L, 1=R
wire [MAX_DELAY_BITS-1:0] wr_ptr_cur = ch_sel ? wr_ptr_R : wr_ptr_L;

// BRAM 읽기
reg [BRAM_ADDR_W-1:0]    rd_addr_r;
reg signed [DATA_W-1:0]  bram_rd;

always @(posedge S_AXI_ACLK)
    bram_rd <= delay_ram[rd_addr_r];

// ============================================================================
//  채널별 슬루 레지스터
// ============================================================================
reg [DT_W-1:0] dt_slewed_L;
reg [DT_W-1:0] dt_slewed_R;

wire [DT_W-1:0] dt_slewed_cur = ch_sel ? dt_slewed_R : dt_slewed_L;
wire [DT_W-1:0] dt_target     = reg_delay_time;
wire [DT_W-1:0] dt_diff_w     = (dt_target > dt_slewed_cur) ?
                                  (dt_target - dt_slewed_cur) :
                                  (dt_slewed_cur - dt_target);
wire [DT_W-1:0] slew_step     = {{(DT_W-16){1'b0}}, reg_slew_rate};

// ============================================================================
//  채널별 입출력 홀드
// ============================================================================
reg signed [DATA_W-1:0] in_hold_L, in_hold_R;
reg signed [DATA_W-1:0] out_L_hold;

wire signed [DATA_W-1:0] in_hold_cur = ch_sel ? in_hold_R : in_hold_L;

// ============================================================================
//  [ARCH-4] LFO 8채널 분배
//
//  diff_stage(0~3) → lfo_mux = lfo[diff_stage]
//  lfo_mix = lfo_mux + (lfo_4 >>> 3)          (shift만, DSP 없음)
//
//  스테레오 오프셋: L=lfo_mix, R=lfo_mix + (lfo_5 >>> 2)
//  apf_delay = apply_diff ? (base + (lfo_ch >>> depth)) : base
// ============================================================================
// diff_stage: APF 내 파이프라인 스테이지 선택 (0~3 → 4개 APF 중 현재 활성)
// (Rev.2에서는 APF가 2개 cascade이므로 0~1로 사용)
wire signed [15:0] lfo_mux =
    (1'b0) ? lfo_1 :   // 단순화: 출력 APF 2개는 lfo_0, lfo_1 담당
              lfo_0;    // 향후 확장 가능

// 공통 LFO mix (shift만 사용, DSP 불필요)
wire signed [15:0] lfo_mix_base = lfo_mux + (lfo_4 >>> 3);

// 채널별 LFO (L: lfo_mix_base, R: lfo_mix_base + offset)
wire signed [15:0] lfo_apf_L = lfo_mix_base;
wire signed [15:0] lfo_apf_R = lfo_mix_base + (lfo_5 >>> 2);

wire signed [15:0] lfo_apf_cur = ch_sel ? lfo_apf_R : lfo_apf_L;

// ============================================================================
//  [ARCH-5] Echo Index
//  echo_idx: 피드백 반복 카운터 - 새 샘플마다 0으로 초기화,
//            BRAM 쓰기(ST_FBMIX) 때마다 증가
// ============================================================================
reg [3:0] echo_idx_L, echo_idx_R;

wire [3:0] echo_idx_cur  = ch_sel ? echo_idx_R : echo_idx_L;

// apply_diff: echo_idx >= reg_diffusion_start 이면 APF 활성화
wire apply_diff = (echo_idx_cur >= reg_diffusion_start[3:0]);

// ============================================================================
//  FSM 상태 코드
// ============================================================================
localparam [5:0]
    ST_IDLE        = 6'd0,
    ST_MOD_LATCH   = 6'd1,   // 슬루 / LFO 래치
    ST_ADDR        = 6'd2,   // BRAM 주소 계산
    ST_RD0         = 6'd3,   // BRAM 읽기 x0
    ST_RD1         = 6'd4,   // BRAM 읽기 x1
    ST_INTERP      = 6'd5,   // Linear 보간 계산
    ST_INTERP2     = 6'd6,   // 보간 파이프라인 2
    ST_APF_TRIG    = 6'd7,   // APF 트리거 (apply_diff 조건부)
    ST_APF_WAIT    = 6'd8,   // APF 완료 대기
    ST_DIFF_MIX    = 6'd9,   // Diffusion 믹스
    ST_SAT         = 6'd10,  // 소프트 클립
    ST_FBMIX       = 6'd11,  // BRAM 쓰기 + echo_idx 증가 + TDM 전환
    ST_OUTPUT      = 6'd12,  // 출력 래치
    ST_DRAIN       = 6'd13;  // AXI handshake

// ============================================================================
//  FSM 레지스터
// ============================================================================
reg [5:0]                state;
reg [3:0]                ap_wait_cnt;
reg                      tready_r;

assign s_axis_mono_tready = tready_r;
wire fire_in = s_axis_mono_tvalid && tready_r;

// 파이프라인 내부 레지스터
reg signed [DATA_W-1:0]  echo_r;      // BRAM에서 읽은 현재 echo
reg signed [DATA_W-1:0]  sat_r;       // 소프트 클립 출력
reg signed [DATA_W-1:0]  diff_out_r;  // [TIM-FIX] diff_out_w 파이프라인 레지스터
reg                       ap0_en;      // APF 트리거 펄스

// [Rev.7] 댐핑 상태 레지스터 (채널별 IIR 상태)
reg signed [DATA_W-1:0]  fb_damp_r;   // 피드백 루프 IIR LPF 상태 (L/R TDM)
reg signed [DATA_W-1:0]  damp_out_r;  // 출력 믹스 HF 댐핑 IIR 상태 (L/R TDM)

// [Rev.7] M/S 와이드닝용 R채널 출력 홀드
reg signed [DATA_W-1:0]  out_R_hold;  // R채널 mix_w 래치 (ST_FBMIX ch_sel=1)

// ============================================================================
//  [ARCH-2] Linear 보간 레지스터
// ============================================================================
reg signed [DATA_W-1:0] li_x0, li_x1;  // 인접 두 샘플
reg [7:0]               li_frac;        // 소수부 Q0.8

// 보간 결과 파이프라인 (1 DSP48E1 공유)
// stage1: diff = x1 - x0
// stage2: interp = x0 + (diff * frac) >> 8
reg signed [DATA_W:0]    li_diff_r;     // x1 - x0 (17bit)
reg signed [DATA_W-1:0]  li_out_r;      // 보간 결과

wire signed [DATA_W:0]   li_diff_w = $signed({li_x1[DATA_W-1], li_x1}) -
                                      $signed({li_x0[DATA_W-1], li_x0});
wire signed [24:0]       li_prod_w = $signed(li_diff_r) * $signed({1'b0, li_frac});
wire signed [DATA_W-1:0] li_result = $signed({li_x0[DATA_W-1], li_x0[DATA_W-1:0]}) +
                                      $signed(li_prod_w[23:8]);

// BRAM 주소 레지스터
reg [MAX_DELAY_BITS-1:0] addr0_r, addr1_r;
reg [7:0]                frac_r;

// MOD_LATCH에서 래치된 딜레이 정수부/소수부
reg [DT_W-1:0]           dt_slewed_lat;  // 래치된 슬루값

// ============================================================================
//  [ARCH-3] APF 인스턴스: 4개 (L×2 + R×2)
//    apf0 → apf1 cascade (출력단 확산만)
//    apf_delay = apply_diff ? (base + lfo) : base  (APF 내부 변조)
//
//  schroeder_ap 모듈은 DELAY 파라미터가 정수 고정이므로,
//  LFO 변조를 위해 apf_delay_mod wire로 주소 오프셋 전달이 필요한 경우
//  schroeder_ap_lfo 서브모듈로 교체하거나, 고정 딜레이 + g 변조로 구현.
//  Rev.2에서는 g 계수를 LFO로 변조하는 방식을 사용 (DSP 최소화).
// ============================================================================

// APF g계수 LFO 변조 (apply_diff 조건 적용)
wire signed [32:0] lfo_apf_scaled =
    $signed(lfo_apf_cur) * $signed({1'b0, reg_lfo_depth_apf});

wire signed [17:0] apf_g_sum =
    $signed({2'b00, reg_diffusion_g[14:0]}) +
    $signed({{2{lfo_apf_scaled[30]}}, lfo_apf_scaled[30:15]});

wire signed [DATA_W-1:0] apf_g_mod =
    apply_diff ?
        (apf_g_sum[17]           ? 16'sd0     :
         (apf_g_sum > 18'sh7FFF) ? 16'sh7FFF  :
                                    apf_g_sum[15:0]) :
        $signed({1'b0, reg_diffusion_g[14:0]});

// L채널 APF
wire ap0_L_en  = (!ch_sel) && ap0_en && apply_diff;

wire signed [DATA_W-1:0] ap0_L_out; wire ap0_L_valid;
wire signed [DATA_W-1:0] ap1_L_out; wire ap1_L_valid;

schroeder_ap #(.DATA_W(DATA_W),.DELAY(AP0_D),.ADDR_W(AP0_AW)) u_ap0_L (
    .clk(S_AXI_ACLK), .rst_n(S_AXI_ARESETN),
    .en(ap0_L_en), .din(echo_r), .g(apf_g_mod),
    .dout(ap0_L_out), .dout_valid(ap0_L_valid)
);
schroeder_ap #(.DATA_W(DATA_W),.DELAY(AP1_D),.ADDR_W(AP1_AW)) u_ap1_L (
    .clk(S_AXI_ACLK), .rst_n(S_AXI_ARESETN),
    .en(ap0_L_valid), .din(ap0_L_out), .g(apf_g_mod),
    .dout(ap1_L_out), .dout_valid(ap1_L_valid)
);

// R채널 APF
wire ap0_R_en  = ch_sel && ap0_en && apply_diff;

wire signed [DATA_W-1:0] ap0_R_out; wire ap0_R_valid;
wire signed [DATA_W-1:0] ap1_R_out; wire ap1_R_valid;

schroeder_ap #(.DATA_W(DATA_W),.DELAY(AP0_D),.ADDR_W(AP0_AW)) u_ap0_R (
    .clk(S_AXI_ACLK), .rst_n(S_AXI_ARESETN),
    .en(ap0_R_en), .din(echo_r), .g(apf_g_mod),
    .dout(ap0_R_out), .dout_valid(ap0_R_valid)
);
schroeder_ap #(.DATA_W(DATA_W),.DELAY(AP1_D),.ADDR_W(AP1_AW)) u_ap1_R (
    .clk(S_AXI_ACLK), .rst_n(S_AXI_ARESETN),
    .en(ap0_R_valid), .din(ap0_R_out), .g(apf_g_mod),
    .dout(ap1_R_out), .dout_valid(ap1_R_valid)
);

// 현재 채널 APF 출력 MUX
wire signed [DATA_W-1:0] apf_out_cur   = ch_sel ? ap1_R_out   : ap1_L_out;
wire                     apf_valid_cur = ch_sel ? ap1_R_valid  : ap1_L_valid;

// ============================================================================
//  유틸리티 함수
// ============================================================================
function signed [DATA_W-1:0] soft_clip;
    input signed [DATA_W-1:0] x;
    input        [15:0]       thresh;
    reg signed [17:0] t_pos, t_neg, x_e, excess, result;
    begin
        t_pos = {2'b00, thresh[DATA_W-2:0]};
        t_neg = -{2'b00, thresh[DATA_W-2:0]};
        x_e   = {{2{x[DATA_W-1]}}, x};
        if (x_e > t_pos) begin
            excess = x_e - t_pos; result = t_pos + (excess >>> 2);
        end else if (x_e < t_neg) begin
            excess = x_e - t_neg; result = t_neg + (excess >>> 2);
        end else begin
            result = x_e;
        end
        if      (result >  18'sh00007FFF) soft_clip =  16'h7FFF;
        else if (result < -18'sh00008000) soft_clip = -16'h8000;
        else                              soft_clip  = result[DATA_W-1:0];
    end
endfunction

function signed [DATA_W-1:0] sat16;
    input signed [DATA_W:0] v;
    begin
        if      (v >  17'sd32767) sat16 =  16'h7FFF;
        else if (v < -17'sd32768) sat16 = -16'h8000;
        else                      sat16 =  v[DATA_W-1:0];
    end
endfunction

function signed [DATA_W-1:0] sat16_25;
    input signed [24:0] v;
    begin
        if      (v >  25'sh0000007FFF) sat16_25 =  16'h7FFF;
        else if (v < -25'sh0000008000) sat16_25 = -16'h8000;
        else                           sat16_25  = v[DATA_W-1:0];
    end
endfunction

// ============================================================================
//  조합 로직
// ============================================================================

// ── [ARCH-5] Diffusion 믹스 출력 ─────────────────────────────────────────
// out = echo + ((apf_out - echo) * diffusion_mix) >> 15
// apply_diff=0 이면 diff_out = echo (바이패스)
wire signed [DATA_W-1:0] diff_src =
    apply_diff ? apf_out_cur : echo_r;

wire signed [DATA_W:0]   diff_delta_w =
    $signed({diff_src[DATA_W-1], diff_src}) -
    $signed({echo_r[DATA_W-1],   echo_r});

wire signed [32:0] diff_mix_acc =
    $signed({{16{echo_r[DATA_W-1]}}, echo_r}) +
    ($signed({{16{diff_delta_w[DATA_W]}}, diff_delta_w}) *
     $signed({1'b0, reg_diffusion_mix}) >>> 15);

wire signed [DATA_W-1:0] diff_out_w;
assign diff_out_w =
    (diff_mix_acc > 33'sh0000007FFF) ?  16'h7FFF :
    (diff_mix_acc < -33'sh000000_8000) ? -16'h8000 :
                                          diff_mix_acc[DATA_W-1:0];

// ── 피드백 계산 (damp_alpha IIR 적용, fb_damp_r은 FSM 레지스터) ──────────
wire [15:0] fb_clamped =
    ($signed({2'b00, reg_feedback}) > $signed({2'b00, FB_MAX})) ? FB_MAX :
                                                                    reg_feedback;

// damp_alpha=0 이면 echo_r 직접 사용 (Rev.2 원래 동작)
// damp_alpha>0 이면 fb_damp_r (IIR 필터된 echo) 사용
wire signed [DATA_W-1:0] fb_src = (reg_damp_alpha == 16'h0) ? echo_r : fb_damp_r;

wire signed [31:0] fb_acc_w = $signed(fb_src) * $signed({1'b0, fb_clamped});
wire signed [DATA_W-1:0] fb_w = $signed(fb_acc_w[30:15]);

// ── BRAM 쓰기 데이터: input + feedback ────────────────────────────────────
wire signed [DATA_W:0] bram_sum_w =
    $signed({in_hold_cur[DATA_W-1], in_hold_cur}) +
    $signed({fb_w[DATA_W-1],        fb_w});
wire signed [DATA_W-1:0] bram_wr_w =
    reg_bypass ? in_hold_cur : sat16(bram_sum_w);

// ── 출력 믹스 (damp_hf=0 이면 sat_r 직접, >0 이면 damp_out_r 사용) ──────
// damp_out_r, fb_damp_r : FSM에서 관리
wire signed [DATA_W-1:0] mix_src = (reg_damp_hf == 16'h0) ? sat_r : damp_out_r;
wire signed [32:0] mix_acc_w =
    $signed(in_hold_cur) * $signed({1'b0, (16'h7FFF - reg_dry_wet)}) +
    $signed(mix_src)     * $signed({1'b0, reg_dry_wet});
wire signed [DATA_W-1:0] mix_wet_w = $signed(mix_acc_w[30:15]);
wire signed [DATA_W-1:0] mix_w = reg_bypass ? in_hold_cur : mix_wet_w;

// (M/S 와이드닝은 출력 레지스터 블록에서 처리)

// ── [Rev.7] IIR 댐핑 조합 연산 ───────────────────────────────────────────
// fb_damp IIR Q15: (α*prev + (1-α)*echo) >> 15
wire signed [31:0] iir_fb_acc =
    $signed({{16{fb_damp_r[DATA_W-1]}}, fb_damp_r}) * $signed({1'b0, reg_damp_alpha}) +
    $signed({{16{echo_r[DATA_W-1]}},    echo_r})    * $signed({1'b0, (16'h7FFF - reg_damp_alpha[14:0])});
wire signed [DATA_W-1:0] iir_fb_w =
    (iir_fb_acc[31:15] >  17'sd32767) ?  16'h7FFF :
    (iir_fb_acc[31:15] < -17'sd32768) ? -16'h8000 :
                                          iir_fb_acc[30:15];

// damp_out IIR Q15: 출력 HF LPF
wire signed [31:0] iir_out_acc =
    $signed({{16{damp_out_r[DATA_W-1]}}, damp_out_r}) * $signed({1'b0, reg_damp_hf}) +
    $signed({{16{sat_r[DATA_W-1]}},      sat_r})       * $signed({1'b0, (16'h7FFF - reg_damp_hf[14:0])});
wire signed [DATA_W-1:0] iir_out_w =
    (iir_out_acc[31:15] >  17'sd32767) ?  16'h7FFF :
    (iir_out_acc[31:15] < -17'sd32768) ? -16'h8000 :
                                           iir_out_acc[30:15];

// ============================================================================
//  메인 FSM  - TDM L→R 구조
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        state          <= ST_IDLE;
        ch_sel         <= 1'b0;
        tready_r       <= 1'b0;
        rd_addr_r      <= {BRAM_ADDR_W{1'b0}};
        wr_ptr_L       <= {MAX_DELAY_BITS{1'b0}};
        wr_ptr_R       <= {MAX_DELAY_BITS{1'b0}};
        in_hold_L      <= {DATA_W{1'b0}};
        in_hold_R      <= {DATA_W{1'b0}};
        out_L_hold     <= {DATA_W{1'b0}};
        out_R_hold     <= {DATA_W{1'b0}};
        echo_r         <= {DATA_W{1'b0}};
        sat_r          <= {DATA_W{1'b0}};
        diff_out_r     <= {DATA_W{1'b0}};  // [TIM-FIX]
        fb_damp_r      <= {DATA_W{1'b0}};
        damp_out_r     <= {DATA_W{1'b0}};
        ap0_en         <= 1'b0;
        ap_wait_cnt    <= 4'd0;
        dt_slewed_L    <= 22'h200000;
        dt_slewed_R    <= 22'h200000;
        dt_slewed_lat  <= {DT_W{1'b0}};
        li_x0          <= {DATA_W{1'b0}};
        li_x1          <= {DATA_W{1'b0}};
        li_frac        <= 8'h00;
        li_diff_r      <= {(DATA_W+1){1'b0}};
        li_out_r       <= {DATA_W{1'b0}};
        addr0_r        <= {MAX_DELAY_BITS{1'b0}};
        addr1_r        <= {MAX_DELAY_BITS{1'b0}};
        frac_r         <= 8'h00;
        echo_idx_L     <= 4'd0;
        echo_idx_R     <= 4'd0;
    end else begin
        ap0_en <= 1'b0;

        case (state)

        // ── IDLE: L/R 동시 수신 ──────────────────────────────────────────────
        ST_IDLE: begin
            tready_r <= 1'b1;
            if (fire_in) begin
                // 모노 입력을 L/R 양쪽에 동일하게 래치
                in_hold_L <= s_axis_mono_tdata;
                in_hold_R <= s_axis_mono_tdata;
                tready_r  <= 1'b0;
                ch_sel    <= 1'b0;
                // 새 샘플 → echo_idx 리셋
                echo_idx_L <= 4'd0;
                echo_idx_R <= 4'd0;
                state     <= ST_MOD_LATCH;
            end
        end

        // ── MOD_LATCH: 슬루 딜레이 래치 ─────────────────────────────────────
        ST_MOD_LATCH: begin
            dt_slewed_lat <= dt_slewed_cur;
            state         <= ST_ADDR;
        end

        // ── ADDR: BRAM 주소 계산 (Linear 보간용 x0, x1) ──────────────────────
        ST_ADDR: begin
            // 정수부 (딜레이 샘플 수), 소수부 (보간 frac Q0.8)
            addr0_r <= wr_ptr_cur - dt_slewed_lat[DT_W-1:8];
            addr1_r <= wr_ptr_cur - dt_slewed_lat[DT_W-1:8] + 1'b1;
            frac_r  <= dt_slewed_lat[7:0];
            // BRAM 읽기 1: x0
            rd_addr_r <= {ch_sel, wr_ptr_cur - dt_slewed_lat[DT_W-1:8]};
            state <= ST_RD0;
        end

        // ── RD0: x0 래치, x1 읽기 요청 ──────────────────────────────────────
        ST_RD0: begin
            li_x0     <= bram_rd;
            rd_addr_r <= {ch_sel, addr1_r};
            state     <= ST_RD1;
        end

        // ── RD1: x1 래치 ─────────────────────────────────────────────────────
        ST_RD1: begin
            li_x1  <= bram_rd;
            li_frac <= frac_r;
            state  <= ST_INTERP;
        end

        // ── INTERP: Linear 보간 stage1 (diff 계산) ───────────────────────────
        ST_INTERP: begin
            li_diff_r <= li_diff_w;  // x1 - x0
            state     <= ST_INTERP2;
        end

        // ── INTERP2: Linear 보간 stage2 (x0 + diff*frac>>8) ─────────────────
        ST_INTERP2: begin
            // li_result 는 combinational (li_diff_r, li_frac, li_x0 기반)
            echo_r <= li_result;
            // APF 트리거 (apply_diff 조건은 combinational apply_diff wire 사용)
            ap0_en <= apply_diff;
            ap_wait_cnt <= AP_WAIT;
            state  <= apply_diff ? ST_APF_TRIG : ST_DIFF_MIX;
        end

        // ── APF_TRIG: APF 활성화 펄스 (이미 ap0_en=1 설정됨) ────────────────
        ST_APF_TRIG: begin
            // ap0_en은 이미 INTERP2에서 설정. 여기서는 대기 카운터 감소.
            state <= ST_APF_WAIT;
        end

        // ── APF_WAIT: APF 완료 대기 ──────────────────────────────────────────
        ST_APF_WAIT: begin
            if (apf_valid_cur || (ap_wait_cnt == 4'd1))
                state <= ST_DIFF_MIX;
            else
                ap_wait_cnt <= ap_wait_cnt - 4'd1;
        end

        // ── DIFF_MIX: Diffusion 믹스 (echo + (apf-echo)*mix) ─────────────────
        // [TIM-FIX] diff_out_w 를 레지스터에 래치하여 조합 경로 분리
        //   이전: echo_idx→apply_diff→DSP(diff_mix_acc)→sat(diff_out_w)→soft_clip = 23 LV
        //   수정: echo_idx→apply_diff→DSP→sat→diff_out_r (Path A, ~14ns)
        //         diff_out_r→soft_clip→sat_r           (Path B, ~6ns)
        ST_DIFF_MIX: begin
            diff_out_r <= diff_out_w;   // 파이프라인 컷: DSP+포화 결과 래치
            state <= ST_SAT;
        end

        // ── SAT: 소프트 클립 ─────────────────────────────────────────────────
        ST_SAT: begin
            sat_r      <= soft_clip(diff_out_r, reg_sat_thresh);
            fb_damp_r  <= (reg_damp_alpha != 16'h0) ? iir_fb_w  : echo_r;
            damp_out_r <= (reg_damp_hf    != 16'h0) ? iir_out_w : sat_r;
            state      <= ST_FBMIX;
        end

        // ── FBMIX: BRAM 쓰기, echo_idx 증가, TDM 전환 ───────────────────────
        ST_FBMIX: begin
            // BRAM 쓰기: input + feedback
            delay_ram[{ch_sel, wr_ptr_cur}] <= bram_wr_w;

            // 쓰기 포인터 증가
            if (ch_sel) wr_ptr_R <= wr_ptr_R + 1'b1;
            else        wr_ptr_L <= wr_ptr_L + 1'b1;

            // echo_idx 증가 (피드백 반복 카운터)
            if (ch_sel) echo_idx_R <= echo_idx_R + 4'd1;
            else        echo_idx_L <= echo_idx_L + 4'd1;

            // 딜레이 슬루 갱신
            if (ch_sel) begin
                if      (dt_slewed_R < dt_target)
                    dt_slewed_R <= (dt_diff_w <= slew_step) ? dt_target : dt_slewed_R + slew_step;
                else if (dt_slewed_R > dt_target)
                    dt_slewed_R <= (dt_diff_w <= slew_step) ? dt_target : dt_slewed_R - slew_step;
            end else begin
                if      (dt_slewed_L < dt_target)
                    dt_slewed_L <= (dt_diff_w <= slew_step) ? dt_target : dt_slewed_L + slew_step;
                else if (dt_slewed_L > dt_target)
                    dt_slewed_L <= (dt_diff_w <= slew_step) ? dt_target : dt_slewed_L - slew_step;
            end

            // TDM 채널 전환
            if (!ch_sel) begin
                out_L_hold <= mix_w;  // L 출력 보존
                ch_sel     <= 1'b1;
                state      <= ST_MOD_LATCH;
            end else begin
                out_R_hold <= mix_w;  // R 출력 보존 (M/S 와이드닝용)
                state <= ST_OUTPUT;
            end
        end

        // ── OUTPUT: 출력 전환 ────────────────────────────────────────────────
        ST_OUTPUT: state <= ST_DRAIN;

        ST_DRAIN: begin
            // m_axis_dma는 WAV 녹음 경로 - tready 대기 없이 fire-and-forget
            // L/R handshake만 완료되면 즉시 IDLE 복귀 → 스피커 출력 차단 없음
            if ((!m_axis_l_tvalid || m_axis_l_tready) &&
                (!m_axis_r_tvalid || m_axis_r_tready)) begin
                state    <= ST_IDLE;
                tready_r <= 1'b1;
            end
        end

        default: begin state <= ST_IDLE; tready_r <= 1'b0; end
        endcase
    end
end

// ============================================================================
//  출력 레지스터 (M/S 와이드닝 포함)
// ============================================================================
// ms_2m = L+R (Mid), ms_2s = L-R (Side)
// ms_out_L = (M + S*width) / 2 = L, ms_out_R = (M - S*width) / 2 = R
// 간소화: width=0 → 스테레오 그대로, width=0x7FFF → 최대 확산
wire signed [DATA_W:0] ms_mid_w =
    $signed({out_L_hold[DATA_W-1], out_L_hold}) +
    $signed({out_R_hold[DATA_W-1], out_R_hold});  // 2M
wire signed [DATA_W:0] ms_sid_w =
    $signed({out_L_hold[DATA_W-1], out_L_hold}) -
    $signed({out_R_hold[DATA_W-1], out_R_hold});  // 2S

// side_amp = side * width (Q1.15 곱)
wire signed [31:0] ms_side_amp =
    $signed(ms_sid_w) * $signed({1'b0, reg_ms_width});

// ms_out_L = (2M + side_amp>>15) >> 1 = M + S*w/2
// ms_out_R = (2M - side_amp>>15) >> 1 = M - S*w/2
wire signed [DATA_W-1:0] ms_out_L =
    sat16($signed(ms_mid_w) + $signed(ms_side_amp[30:15]));  // ÷2 생략 (0dB 유지)
wire signed [DATA_W-1:0] ms_out_R =
    sat16($signed(ms_mid_w) - $signed(ms_side_amp[30:15]));

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        m_axis_l_tvalid   <= 1'b0; m_axis_l_tdata   <= {DATA_W{1'b0}};
        m_axis_r_tvalid   <= 1'b0; m_axis_r_tdata   <= {DATA_W{1'b0}};
        m_axis_dma_tvalid <= 1'b0; m_axis_dma_tdata <= 32'd0;
    end else begin
        if (state == ST_OUTPUT) begin
            m_axis_l_tdata    <= ms_out_L;
            m_axis_r_tdata    <= ms_out_R;
            m_axis_l_tvalid   <= 1'b1;
            m_axis_r_tvalid   <= 1'b1;
            // DMA 포트: {R[15:0], L[15:0]} 패킹
            m_axis_dma_tdata  <= {ms_out_R, ms_out_L};
            m_axis_dma_tvalid <= 1'b1;
        end else begin
            if (m_axis_l_tvalid   && m_axis_l_tready)   m_axis_l_tvalid   <= 1'b0;
            if (m_axis_r_tvalid   && m_axis_r_tready)   m_axis_r_tvalid   <= 1'b0;
            if (m_axis_dma_tvalid && m_axis_dma_tready) m_axis_dma_tvalid <= 1'b0;
        end
    end
end

endmodule