`timescale 1ns / 1ps
// ============================================================================
//  flanger_top.v  Rev.1  (Integrated Mono Flanger)
//
//  ── 설계 방침 ────────────────────────────────────────────────────────────────
//  · 기존 analog_delay_top × 2 래퍼 구조를 폐기
//  · BBD(Bucket Brigade Device) 플랜저 전용 단일 모듈로 재설계
//  · 출력: 모노 1채널 (뒷단 리버브/이펙터 체인 연결 호환)
//  · LFO: 외부 8채널 입력 (내부 NCO 미포함)
//  · AXI-Lite 레지스터 맵은 analog_delay_top 방식 계승
//
//  ── 플랜저 동작 원리 ────────────────────────────────────────────────────────
//  · 가변 딜레이(1~10ms) + 높은 피드백 → 빗살 필터(Comb Filter)
//  · LFO로 딜레이 변조 → 빗살 노치 주파수 스윕 → 제트기/워시 효과
//  · Dry + Wet 혼합 필수: Wet 단독으로는 Comb 효과 미발생
//
//  ── 불필요 기능 제거 (딜레이 모듈 대비) ─────────────────────────────────────
//  · 멀티탭(Tap1/2/3) 제거: 플랜저는 단일 Comb Filter로 충분
//    → 탭 추가 시 노치 패턴 붕괴, 음색 혼탁
//  · 스테레오 이중화 제거: 모노 단일 경로
//  · reg_slew_rate: 유지 (딜레이 갑변 시 클릭 노이즈 방지)
//  · reg_mode: 제거 (플랜저는 항상 피드백 모드)
//  · reg_color: 유지 (BBD 음색 에뮬레이션)
//
//  ── 플랜저 전용 기능 강화 ───────────────────────────────────────────────────
//  · FEAT-F1: 피드백 극성 선택 (reg_fb_polarity)
//    · 양(+) 피드백: 강한 공명, 공격적 플랜저
//    · 음(-) 피드백: 부드러운 노치, BBD 클래식 느낌
//  · FEAT-F2: Comb Filter Depth 레지스터 (reg_comb_depth)
//    · Dry/Wet 비율과 독립적으로 Comb 깊이 조절
//    · 1.0 = 완전 Comb, 0.5 = 하프 노치
//  · FEAT-F3: 피드백 APF diffusion 유지 (2단)
//    · 피드백 루프 내 APF 2단 → BBD 위상 확산 에뮬레이션
//  · FEAT-F4: 출력단 APF diffusion 유지 (2단)
//    · 출력 전 APF 2단 → 플랜저 스윕에 부드러운 위상 확산
//  · FEAT-F5: BBD 비선형성 (포화) 피드백 루프 내 유지
//  · FEAT-F6: LFO 8채널 모두 활용
//    · ch0: 메인 딜레이 변조 (플랜저 스윕 핵심)
//    · ch1: 피드백량 변조 (공명 breathing)
//    · ch2: LPF alpha 변조 (BBD 음색 시변)
//    · ch3: 피드백 APF g 변조 (위상 확산 변화)
//    · ch4: 출력 APF g 변조
//    · ch5~ch7: 예비 (미사용, 포트만 존재)
//
//  ── AXI-Lite 레지스터 맵 ────────────────────────────────────────────────────
//  오프셋  워드주소  이름                  기본값    설명
//  0x00    6'h00    reg_delay_time        0x00F000  Q14.8, 5ms@48kHz
//  0x04    6'h01    reg_lfo_depth         0x3000    37.5% 딜레이 스윕 폭
//  0x08    6'h02    reg_feedback          0x5800    68.75% (FB_MAX=0x6000)
//  0x0C    6'h03    reg_lpf_alpha         0x6000    BBD 고역 롤오프
//  0x10    6'h04    reg_hpf_alpha         0x7C00    DC 커팅
//  0x14    6'h05    reg_dry_wet           0x4000    50% (Comb Filter 최적)
//  0x18    6'h06    reg_sat_thresh        0x7000    BBD 비선형성 힌트
//  0x1C    6'h07    reg_diffusion         0x6800    APF g=0.8125
//  0x20    6'h08    reg_fb_sat_thresh     0x5800    피드백 포화
//  0x24    6'h09    reg_slew_rate         0x0200    딜레이 슬루율
//  0x28    6'h0A    reg_color             [1:0]     BBD 음색 모드
//  0x2C    6'h0B    reg_fb_polarity       [0:0]     0=양성, 1=음성 피드백
//  0x30    6'h0C    reg_comb_depth        0x7FFF    Comb 깊이 (독립)
//  0x34    6'h0D    reg_lfo_depth_fb      0x1000    피드백 breathing depth
//  0x38    6'h0E    reg_lfo_depth_lpf     0x0800    LPF tone sweep depth
//  0x3C    6'h0F    reg_lfo_depth_apfb    0x0A00    APF fb 변조 depth
//  0x40    6'h10    reg_lfo_depth_apout   0x0A00    APF out 변조 depth
//
//  ── 리소스 추정 (XC7Z020) ───────────────────────────────────────────────────
//  DSP48E1: ~13개 (Hermite 3단×3 + APF 4개 + 믹스 곱셈 2개 + LFO 변조 4개)
//  LUT:     ~900
//  FF:      ~500
//  BRAM36E1: 1개 (32768 × 16bit, 딜레이 라인)
//  APF BRAM: 4개 소형 (distrib. RAM 권장)
//
//  ── 처리 레이턴시 (48kHz 기준) ──────────────────────────────────────────────
//  샘플당 클럭: ~55클럭 @50MHz (20.8µs < 1/48kHz = 20.83µs 경계)
//    ST_IDLE(1) + ST_MOD_LATCH(1) + ST_ADDR(1)
//    + ST_M_RDm1~PL3(7) + ST_SAT(1) + ST_HPF_PRE(1) + ST_HPF(1)
//    + ST_APFB_TRIG(1) + ST_APFB_WAIT(AP_WAIT) + ST_FBMIX(1)
//    + ST_OUTPUT(1) + ST_DRAIN(1) ≈ 50클럭 이하 → 여유 있음
// ============================================================================

module flanger_top #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 8,
    parameter integer MAX_DELAY_BITS     = 14,   // 최대 딜레이: 2^14 = 16384샘플
    parameter integer DATA_W             = 16
)(
    // ── 클럭/리셋 ─────────────────────────────────────────────────────────────
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI:s_axis:m_axis, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
    input  wire                              S_AXI_ACLK,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 S_AXI_ARESETN RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                              S_AXI_ARESETN,

    // ── AXI-Lite 슬레이브 ─────────────────────────────────────────────────────
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

    // ── AXI-Stream 입력 (모노) ────────────────────────────────────────────────
    input  wire                              s_axis_tvalid,
    input  wire signed [DATA_W-1:0]          s_axis_tdata,
    output wire                              s_axis_tready,

    // ── AXI-Stream 출력 (모노, 뒷단 이펙터 체인 연결) ─────────────────────────
    output reg                               m_axis_tvalid,
    output reg  signed [DATA_W-1:0]          m_axis_tdata,
    input  wire                              m_axis_tready,

    // ── 8채널 LFO 입력 (외부 NCO, 미사용 포트는 0 연결) ──────────────────────
    // ch0: 메인 딜레이 변조 (플랜저 스윕 핵심, 0.5~1.0Hz 권장)
    // ch1: 피드백량 변조 (breathing, 0.2~0.5Hz 권장)
    // ch2: LPF alpha 변조 (BBD 음색, 3~5Hz 권장)
    // ch3: 피드백 APF g 변조 (위상 확산, 0.3Hz 권장)
    // ch4: 출력 APF g 변조 (위상 확산, 0.3Hz +45° 권장)
    // ch5~ch7: 예비 (현재 미사용, 0 연결)
    input  wire signed [15:0]                lfo_ch0,
    input  wire signed [15:0]                lfo_ch1,
    input  wire signed [15:0]                lfo_ch2,
    input  wire signed [15:0]                lfo_ch3,
    input  wire signed [15:0]                lfo_ch4,
    input  wire signed [15:0]                lfo_ch5,   // reserved
    input  wire signed [15:0]                lfo_ch6,   // reserved
    input  wire signed [15:0]                lfo_ch7    // reserved
);

// ============================================================================
//  로컬파라미터
// ============================================================================
localparam DT_W         = MAX_DELAY_BITS + 8;   // 22bit Q14.8
localparam BRAM_ADDR_W  = 15;
localparam BRAM_DEPTH   = 1 << BRAM_ADDR_W;     // 32768

// 최소 딜레이: Hermite 4점 읽기 안전 보장 (addr-1 ~ addr+2)
localparam [DT_W-1:0] MIN_DELAY_Q = 22'h000400;  // 4샘플 Q14.8

// [FEAT-F5] 피드백 최대값: 0x6000 = 0.75 (발진 방지 하드 클램프)
// 실제 BBD 플랜저 최대 feedback ≈ 0.7 → 0.75가 실용적 상한
localparam [15:0] FB_MAX = 16'h6000;

localparam signed [DATA_W-1:0] SAT16_MAX =  16'h7FFF;
localparam signed [DATA_W-1:0] SAT16_MIN = -16'h8000;

// APF 딜레이 설정 (BBD 플랜저 위상 확산 최적화)
// 피드백 APF: 짧은 딜레이 → 고역 위상 확산
localparam AP_FB0_D   = 113;
localparam AP_FB0_AW  = 7;
localparam AP_FB1_D   = 162;
localparam AP_FB1_AW  = 8;
// 출력 APF: 긴 딜레이 → 전역 위상 확산
localparam AP_OUT0_D  = 241;
localparam AP_OUT0_AW = 8;
localparam AP_OUT1_D  = 397;
localparam AP_OUT1_AW = 9;
localparam AP_WAIT    = 4'd4;   // schroeder_ap v2 레이턴시 여유

// ============================================================================
//  AXI-Lite 레지스터
// ============================================================================
reg [DT_W-1:0] reg_delay_time;      // Q14.8 딜레이
reg [15:0]     reg_lfo_depth;        // 메인 딜레이 LFO 깊이
reg [15:0]     reg_feedback;         // 피드백량
reg [15:0]     reg_lpf_alpha;        // BBD LPF 컷오프
reg [15:0]     reg_hpf_alpha;        // DC 커팅 HPF
reg [15:0]     reg_dry_wet;          // Dry/Wet (Comb 효과를 위해 50% 권장)
reg [15:0]     reg_sat_thresh;       // BBD 비선형성 포화 임계
reg [15:0]     reg_diffusion;        // APF diffusion 계수 기준
reg [15:0]     reg_fb_sat_thresh;    // 피드백 루프 포화 임계
reg [15:0]     reg_slew_rate;        // 딜레이 슬루율 (클릭 방지)
reg [1:0]      reg_color;            // BBD 음색 모드 (0=flat,1=bright,2=dark)
// [FEAT-F1] 피드백 극성
reg            reg_fb_polarity;      // 0=양성(공격적), 1=음성(클래식)
// [FEAT-F2] Comb 깊이
reg [15:0]     reg_comb_depth;       // Comb Filter 노치 깊이 독립 제어
// [FEAT-F6] LFO 채널별 변조 깊이
reg [15:0]     reg_lfo_depth_fb;     // ch1: 피드백 breathing
reg [15:0]     reg_lfo_depth_lpf;    // ch2: LPF 음색 스윕
reg [15:0]     reg_lfo_depth_apfb;   // ch3: 피드백 APF g 변조
reg [15:0]     reg_lfo_depth_apout;  // ch4: 출력 APF g 변조

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
        S_AXI_AWREADY     <= 1'b0;
        S_AXI_WREADY      <= 1'b0;
        S_AXI_BVALID      <= 1'b0;
        S_AXI_BRESP       <= 2'b00;
        wr_addr           <= {C_S_AXI_ADDR_WIDTH{1'b0}};
        wdata_r           <= 32'd0;
        wr_en             <= 1'b0;
        // ── 상용 플랜저 기본값 ──────────────────────────────────────────
        reg_delay_time    <= 22'h00F000;  // 5ms @48kHz
        reg_lfo_depth     <= 16'h3000;    // 37.5% 스윕 (±1.875ms)
        reg_feedback      <= 16'h5800;    // 68.75% 강한 공명
        reg_lpf_alpha     <= 16'h6000;    // 어두운 BBD 톤
        reg_hpf_alpha     <= 16'h7C00;    // DC 커팅
        reg_dry_wet       <= 16'h4000;    // 50% → Comb 최적
        reg_sat_thresh    <= 16'h7000;    // 약한 BBD 비선형성
        reg_diffusion     <= 16'h6800;    // APF g=0.8125
        reg_fb_sat_thresh <= 16'h5800;    // 피드백 포화
        reg_slew_rate     <= 16'h0200;    // 빠른 슬루 허용
        reg_color         <= 2'd0;
        reg_fb_polarity   <= 1'b0;        // 양성 피드백 (기본)
        reg_comb_depth    <= 16'h7FFF;    // 최대 Comb 깊이
        reg_lfo_depth_fb  <= 16'h1000;    // 피드백 12.5% breathing
        reg_lfo_depth_lpf <= 16'h0800;    // LPF 6.25% 음색 스윕
        reg_lfo_depth_apfb  <= 16'h0A00; // APF fb 7.8%
        reg_lfo_depth_apout <= 16'h0A00; // APF out 7.8%
    end else begin
        S_AXI_AWREADY <= 1'b0;
        S_AXI_WREADY  <= 1'b0;
        wr_en <= 1'b0;
        case (wr_state)
            WR_IDLE:
                if (S_AXI_AWVALID) begin
                    S_AXI_AWREADY <= 1'b1;
                    wr_addr       <= S_AXI_AWADDR;
                    wr_state      <= WR_ACTIVE;
                end
            WR_ACTIVE:
                if (S_AXI_WVALID) begin
                    wdata_r      <= S_AXI_WDATA;
                    S_AXI_WREADY <= 1'b1;
                    wr_en        <= 1'b1;
                    wr_state     <= WR_RESP;
                end
            WR_RESP: begin
                S_AXI_BVALID <= 1'b1;
                S_AXI_BRESP  <= 2'b00;
                if (S_AXI_BVALID && S_AXI_BREADY) begin
                    S_AXI_BVALID <= 1'b0;
                    wr_state     <= WR_IDLE;
                end
            end
            default: wr_state <= WR_IDLE;
        endcase

        if (wr_en) begin
            case (wr_addr[7:2])
                6'h00: reg_delay_time    <= wdata_r[DT_W-1:0];
                6'h01: reg_lfo_depth     <= wdata_r[15:0];
                6'h02: reg_feedback      <= wdata_r[15:0];
                6'h03: reg_lpf_alpha     <= wdata_r[15:0];
                6'h04: reg_hpf_alpha     <= wdata_r[15:0];
                6'h05: reg_dry_wet       <= wdata_r[15:0];
                6'h06: reg_sat_thresh    <= wdata_r[15:0];
                6'h07: reg_diffusion     <= wdata_r[15:0];
                6'h08: reg_fb_sat_thresh <= wdata_r[15:0];
                6'h09: reg_slew_rate     <= wdata_r[15:0];
                6'h0A: reg_color         <= wdata_r[1:0];
                6'h0B: reg_fb_polarity   <= wdata_r[0];
                6'h0C: reg_comb_depth    <= wdata_r[15:0];
                6'h0D: reg_lfo_depth_fb   <= wdata_r[15:0];
                6'h0E: reg_lfo_depth_lpf  <= wdata_r[15:0];
                6'h0F: reg_lfo_depth_apfb  <= wdata_r[15:0];
                6'h10: reg_lfo_depth_apout <= wdata_r[15:0];
                default: ;
            endcase
        end
    end
end

// ============================================================================
//  AXI-Lite 읽기 FSM
// ============================================================================
localparam [1:0] RD_IDLE = 2'd0, RD_SEND = 2'd1;
reg [1:0] rd_state;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rd_state     <= RD_IDLE;
        S_AXI_ARREADY <= 1'b0;
        S_AXI_RVALID  <= 1'b0;
        S_AXI_RDATA   <= 32'd0;
        S_AXI_RRESP   <= 2'b00;
    end else begin
        S_AXI_ARREADY <= 1'b0;
        case (rd_state)
            RD_IDLE:
                if (S_AXI_ARVALID) begin
                    S_AXI_ARREADY <= 1'b1;
                    case (S_AXI_ARADDR[7:2])
                        6'h00: S_AXI_RDATA <= {{(32-DT_W){1'b0}}, reg_delay_time};
                        6'h01: S_AXI_RDATA <= {16'h0, reg_lfo_depth};
                        6'h02: S_AXI_RDATA <= {16'h0, reg_feedback};
                        6'h03: S_AXI_RDATA <= {16'h0, reg_lpf_alpha};
                        6'h04: S_AXI_RDATA <= {16'h0, reg_hpf_alpha};
                        6'h05: S_AXI_RDATA <= {16'h0, reg_dry_wet};
                        6'h06: S_AXI_RDATA <= {16'h0, reg_sat_thresh};
                        6'h07: S_AXI_RDATA <= {16'h0, reg_diffusion};
                        6'h08: S_AXI_RDATA <= {16'h0, reg_fb_sat_thresh};
                        6'h09: S_AXI_RDATA <= {16'h0, reg_slew_rate};
                        6'h0A: S_AXI_RDATA <= {30'h0, reg_color};
                        6'h0B: S_AXI_RDATA <= {31'h0, reg_fb_polarity};
                        6'h0C: S_AXI_RDATA <= {16'h0, reg_comb_depth};
                        6'h0D: S_AXI_RDATA <= {16'h0, reg_lfo_depth_fb};
                        6'h0E: S_AXI_RDATA <= {16'h0, reg_lfo_depth_lpf};
                        6'h0F: S_AXI_RDATA <= {16'h0, reg_lfo_depth_apfb};
                        6'h10: S_AXI_RDATA <= {16'h0, reg_lfo_depth_apout};
                        default: S_AXI_RDATA <= 32'hDEAD_BEEF;
                    endcase
                    rd_state <= RD_SEND;
                end
            RD_SEND: begin
                S_AXI_RVALID <= 1'b1;
                S_AXI_RRESP  <= 2'b00;
                if (S_AXI_RVALID && S_AXI_RREADY) begin
                    S_AXI_RVALID <= 1'b0;
                    rd_state     <= RD_IDLE;
                end
            end
            default: rd_state <= RD_IDLE;
        endcase
    end
end

// ============================================================================
//  BRAM 딜레이 라인 (15bit 주소, BRAM36E1 단일 합성)
//  MAX_DELAY_BITS=14 → 최대 딜레이 16383샘플 @48kHz ≈ 341ms
//  플랜저 목표: 1~10ms → 48~480샘플 → 충분한 여유
// ============================================================================
(* ram_style = "block" *)
reg signed [DATA_W-1:0] delay_ram [0:BRAM_DEPTH-1];

integer init_i;
initial begin
    for (init_i = 0; init_i < BRAM_DEPTH; init_i = init_i + 1)
        delay_ram[init_i] = {DATA_W{1'b0}};
end

reg [MAX_DELAY_BITS-1:0] wr_ptr;
reg [BRAM_ADDR_W-1:0]    rd_addr_r;
reg signed [DATA_W-1:0]  bram_rd;

always @(posedge S_AXI_ACLK)
    bram_rd <= delay_ram[rd_addr_r];

// ============================================================================
//  슬루 레지스터 (딜레이 급변 시 클릭 노이즈 방지)
// ============================================================================
reg [DT_W-1:0] dt_slewed;
wire [DT_W-1:0] dt_target  = reg_delay_time;
wire [DT_W-1:0] dt_diff_w  = (dt_target > dt_slewed) ?
                               (dt_target - dt_slewed) :
                               (dt_slewed - dt_target);
wire [DT_W-1:0] slew_step  = {{(DT_W-16){1'b0}}, reg_slew_rate};

// ============================================================================
//  LFO 변조 딜레이 계산 (ch0 → 메인 딜레이 스윕)
//
//  수식:
//    lfo_scaled  = lfo_ch0 * reg_lfo_depth   [Q1.15 × Q1.15 = Q2.30]
//    dt_mod_raw  = dt_slewed + lfo_scaled[30:15] << 8  [Q14.8]
//    dt_mod_sat  = clamp(dt_mod_raw, MIN_DELAY_Q, MAX)
//
//  예: dt_slewed=0x00F000(5ms), depth=0x3000(37.5%), lfo=+1.0
//    offset = 0x3000 × 32768 / 32768 × 256 = 0x030000 → +3ms
//    → 실효 딜레이 8ms (최대 노치 이동)
// ============================================================================
wire signed [32:0] lfo_main_scaled =
    $signed(lfo_ch0) * $signed({1'b0, reg_lfo_depth});

wire signed [DT_W+1:0] dt_mod_raw_w =
    $signed({1'b0, dt_slewed}) +
    $signed({{(DT_W+2-24){lfo_main_scaled[32]}},
              lfo_main_scaled[30:15], 8'h00});

wire [DT_W-1:0] dt_mod_sat_w =
    dt_mod_raw_w[DT_W+1]                                ? MIN_DELAY_Q :
    (dt_mod_raw_w[DT_W:8] >= (1 << MAX_DELAY_BITS))     ? {{MAX_DELAY_BITS{1'b1}}, 8'hFF} :
    (dt_mod_raw_w[DT_W-1:0] < MIN_DELAY_Q)              ? MIN_DELAY_Q :
                                                           dt_mod_raw_w[DT_W-1:0];

// ============================================================================
//  [FEAT-F6] LFO 변조: 피드백, LPF, APF
// ============================================================================

// ch1 → 피드백량 변조 (breathing echo)
wire signed [32:0] lfo_fb_scaled =
    $signed(lfo_ch1) * $signed({1'b0, reg_lfo_depth_fb});
wire signed [17:0] fb_sum =
    $signed({2'b00, reg_feedback}) +
    $signed({{2{lfo_fb_scaled[30]}}, lfo_fb_scaled[30:15]});
wire [15:0] fb_clamped =
    fb_sum[17]                                       ? 16'h0000 :
    ($signed(fb_sum) > $signed({2'b00, FB_MAX}))     ? FB_MAX   :
                                                       fb_sum[15:0];

// ch2 → LPF alpha 변조 (BBD 음색 시변)
wire [15:0] base_lpf_alpha =
    (reg_color == 2'd1) ?
        (reg_lpf_alpha[15] ? 16'h7FFF : {reg_lpf_alpha[14:0], 1'b0}) :
    (reg_color == 2'd2) ? {2'b00, reg_lpf_alpha[15:2]} :
                           reg_lpf_alpha;

wire signed [32:0] lfo_lpf_scaled =
    $signed(lfo_ch2) * $signed({1'b0, reg_lfo_depth_lpf});
wire signed [17:0] lpf_alpha_sum =
    $signed({2'b00, base_lpf_alpha}) +
    $signed({{2{lfo_lpf_scaled[30]}}, lfo_lpf_scaled[30:15]});
wire [15:0] eff_lpf_alpha =
    lpf_alpha_sum[17]                     ? 16'h0000 :
    (lpf_alpha_sum > 18'sh0_7FFF)         ? 16'h7FFF :
                                            lpf_alpha_sum[15:0];

// ch3 → 피드백 APF g 변조
wire signed [DATA_W-1:0] ap_g_base = $signed({1'b0, reg_diffusion[14:0]});

wire signed [32:0] lfo_apfb_scaled =
    $signed(lfo_ch3) * $signed({1'b0, reg_lfo_depth_apfb});
wire signed [17:0] apfb_sum =
    $signed({2'b00, ap_g_base}) +
    $signed({{2{lfo_apfb_scaled[30]}}, lfo_apfb_scaled[30:15]});
wire signed [DATA_W-1:0] ap_g_fb_w =
    apfb_sum[17]                   ? 16'sd0    :
    (apfb_sum > 18'sh0_7FFF)       ? 16'sh7FFF :
                                     apfb_sum[15:0];

// ch4 → 출력 APF g 변조
wire signed [32:0] lfo_apout_scaled =
    $signed(lfo_ch4) * $signed({1'b0, reg_lfo_depth_apout});
wire signed [17:0] apout_sum =
    $signed({2'b00, ap_g_base}) +
    $signed({{2{lfo_apout_scaled[30]}}, lfo_apout_scaled[30:15]});
wire signed [DATA_W-1:0] ap_g_out_w =
    apout_sum[17]                  ? 16'sd0    :
    (apout_sum > 18'sh0_7FFF)      ? 16'sh7FFF :
                                     apout_sum[15:0];

// ============================================================================
//  FSM 상태 정의
// ============================================================================
localparam [5:0]
    ST_IDLE       = 6'd0,
    ST_MOD_LATCH  = 6'd1,   // LFO 변조 결과 파이프라인 래치
    ST_ADDR       = 6'd2,
    ST_RDm1       = 6'd3,
    ST_RD0        = 6'd4,
    ST_RD1        = 6'd5,
    ST_RD2        = 6'd6,
    ST_PL1        = 6'd7,
    ST_PL2        = 6'd8,
    ST_PL3        = 6'd9,
    ST_SAT        = 6'd10,
    ST_HPF_PRE    = 6'd11,
    ST_HPF        = 6'd12,
    ST_APFB_TRIG  = 6'd13,
    ST_APFB_WAIT  = 6'd14,
    ST_FBMIX      = 6'd15,
    ST_APOUT_TRIG = 6'd16,
    ST_APOUT_WAIT = 6'd17,
    ST_MIX        = 6'd18,
    ST_OUTPUT     = 6'd19,
    ST_DRAIN      = 6'd20;

// ============================================================================
//  FSM 내부 레지스터
// ============================================================================
reg [5:0]                state;
reg [3:0]                ap_wait_cnt;
reg                      tready_r;

assign s_axis_tready = tready_r;
wire fire_in = s_axis_tvalid && tready_r;

// 파이프라인 래치 (ST_MOD_LATCH)
reg [DT_W-1:0]           dt_mod_sat_r;
reg [15:0]               fb_clamped_r;
reg [15:0]               eff_lpf_alpha_r;
reg signed [DATA_W-1:0]  ap_g_fb_r;
reg signed [DATA_W-1:0]  ap_g_out_r;

// 처리 레지스터
reg signed [DATA_W-1:0]  in_hold;       // 입력 샘플 홀드
reg signed [DATA_W-1:0]  dly_out;       // Hermite 보간 완료 딜레이 출력
reg signed [DATA_W-1:0]  sat_r;         // 포화 처리 후
reg signed [DATA_W-1:0]  lpf_state;     // LPF 상태 (IIR)
reg signed [DATA_W-1:0]  lpf_r;         // LPF 출력
reg signed [DATA_W-1:0]  hpf_state;     // HPF 상태
reg signed [DATA_W-1:0]  hpf_prev_in;   // HPF 이전 입력
reg signed [DATA_W-1:0]  hpf_r;         // HPF 출력
reg signed [DATA_W-1:0]  lpf_fb_r;      // 피드백 루프 LPF 출력
reg signed [DATA_W-1:0]  wet_r;         // APF 통과 후 Wet 신호
reg                      ap_fb0_en;
reg                      ap_out0_en;

// Hermite 보간 레지스터
reg signed [DATA_W-1:0]  h_ym1, h_y0, h_y1, h_y2;
reg [7:0]                h_frac;

// Hermite 파이프라인
reg signed [28:0]        h_p1;
reg signed [37:0]        h_p2;
reg signed [34:0]        h_p3;

// BRAM 읽기 주소
reg [MAX_DELAY_BITS-1:0] main_addrm1, main_addr0, main_addr1, main_addr2;
reg [7:0]                main_frac;

// ============================================================================
//  Hermite 계수 (조합 로직)
//  4점 3차 보간: ym1, y0, y1, y2 → frac ∈ [0,1) 위치의 추정값
// ============================================================================
wire signed [19:0] h_A3 =
    -$signed({{4{h_ym1[DATA_W-1]}}, h_ym1}) +
    ($signed({{4{h_y0[DATA_W-1]}}, h_y0})  <<< 1) +
    $signed({{4{h_y0[DATA_W-1]}},  h_y0})  +
    -($signed({{4{h_y1[DATA_W-1]}}, h_y1}) <<< 1) +
    -$signed({{4{h_y1[DATA_W-1]}}, h_y1})  +
    $signed({{4{h_y2[DATA_W-1]}},  h_y2});

wire signed [20:0] h_A2 =
    ($signed({{5{h_ym1[DATA_W-1]}}, h_ym1}) <<< 1) +
    -($signed({{5{h_y0[DATA_W-1]}}, h_y0})  <<< 2) +
    -$signed({{5{h_y0[DATA_W-1]}}, h_y0})   +
    ($signed({{5{h_y1[DATA_W-1]}}, h_y1})  <<< 2) +
    -$signed({{5{h_y2[DATA_W-1]}}, h_y2});

wire signed [17:0] h_A1 =
    -$signed({{2{h_ym1[DATA_W-1]}}, h_ym1}) +
     $signed({{2{h_y1[DATA_W-1]}},  h_y1});

wire signed [17:0] h_A0 = {h_y0[DATA_W-1], h_y0, 1'b0};

// ============================================================================
//  Hermite DSP 파이프라인
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        h_p1 <= 29'sd0;
        h_p2 <= 38'sd0;
        h_p3 <= 35'sd0;
    end else begin
        h_p1 <= $signed(h_A3) * $signed({1'b0, h_frac})
               + ($signed(h_A2) <<< 8);
        h_p2 <= $signed(h_p1) * $signed({1'b0, h_frac})
               + ($signed(h_A1) <<< 16);
        h_p3 <= $signed(h_p2[36:7]) * $signed({1'b0, h_frac})
               + ($signed(h_A0) <<< 17);
    end
end

// h_raw: 17bit signed (>>18 보정 → gain=1.0 보장)
wire signed [16:0] h_raw = h_p3[34:18];

// ============================================================================
//  포화/포화16 함수
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
            excess = x_e - t_pos;
            result = t_pos + (excess >>> 2);
        end else if (x_e < t_neg) begin
            excess = x_e - t_neg;
            result = t_neg + (excess >>> 2);
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

function signed [DATA_W-1:0] sat16_17;
    input signed [16:0] v;
    begin
        if      (v >  17'sd32767) sat16_17 =  16'h7FFF;
        else if (v < -17'sd32768) sat16_17 = -16'h8000;
        else                      sat16_17  = v[DATA_W-1:0];
    end
endfunction

function signed [DATA_W-1:0] sat16_33;
    input signed [32:0] v;
    begin
        if      (v >  33'sh0000007FFF)  sat16_33 =  16'h7FFF;
        else if (v < -33'sh000000_8000) sat16_33 = -16'h8000;
        else                            sat16_33  = v[DATA_W-1:0];
    end
endfunction

// ============================================================================
//  APF 인스턴스 (schroeder_ap v2 기준, 레이턴시 3클럭)
// ============================================================================
// 피드백 루프 APF 2단
wire ap_fb0_en_w   = ap_fb0_en;
wire signed [DATA_W-1:0] ap_fb0_out;
wire                     ap_fb0_valid;
wire                     ap_fb1_en   = ap_fb0_valid;
wire signed [DATA_W-1:0] ap_fb1_out;
wire                     ap_fb1_valid;

schroeder_ap #(.DATA_W(DATA_W), .DELAY(AP_FB0_D), .ADDR_W(AP_FB0_AW)) u_ap_fb0 (
    .clk(S_AXI_ACLK), .rst_n(S_AXI_ARESETN),
    .en(ap_fb0_en), .din(lpf_fb_r), .g(ap_g_fb_r),
    .dout(ap_fb0_out), .dout_valid(ap_fb0_valid)
);
schroeder_ap #(.DATA_W(DATA_W), .DELAY(AP_FB1_D), .ADDR_W(AP_FB1_AW)) u_ap_fb1 (
    .clk(S_AXI_ACLK), .rst_n(S_AXI_ARESETN),
    .en(ap_fb1_en), .din(ap_fb0_out), .g(ap_g_fb_r),
    .dout(ap_fb1_out), .dout_valid(ap_fb1_valid)
);

// 출력단 APF 2단 (플랜저 스윕에 부드러운 위상 확산)
wire signed [DATA_W-1:0] ap_out0_out;
wire                     ap_out0_valid;
wire                     ap_out1_en   = ap_out0_valid;
wire signed [DATA_W-1:0] ap_out1_out;
wire                     ap_out1_valid;

schroeder_ap #(.DATA_W(DATA_W), .DELAY(AP_OUT0_D), .ADDR_W(AP_OUT0_AW)) u_ap_out0 (
    .clk(S_AXI_ACLK), .rst_n(S_AXI_ARESETN),
    .en(ap_out0_en), .din(dly_out), .g(ap_g_out_r),
    .dout(ap_out0_out), .dout_valid(ap_out0_valid)
);
schroeder_ap #(.DATA_W(DATA_W), .DELAY(AP_OUT1_D), .ADDR_W(AP_OUT1_AW)) u_ap_out1 (
    .clk(S_AXI_ACLK), .rst_n(S_AXI_ARESETN),
    .en(ap_out1_en), .din(ap_out0_out), .g(ap_g_out_r),
    .dout(ap_out1_out), .dout_valid(ap_out1_valid)
);

// ============================================================================
//  조합 로직: LPF, HPF, 피드백, 믹스
// ============================================================================

// LPF (BBD 고역 롤오프 에뮬레이션)
wire signed [32:0] lpf_delta_w =
    ($signed(sat_r) - $signed(lpf_state)) * $signed({1'b0, eff_lpf_alpha_r});
wire signed [DATA_W-1:0] lpf_new_w =
    $signed(lpf_state) + $signed(lpf_delta_w[31:16]);

// HPF (DC 커팅)
wire signed [33:0] hpf_sum_w =
    ($signed({{2{hpf_state[DATA_W-1]}}, hpf_state}) +
     $signed({{2{lpf_r[DATA_W-1]}},    lpf_r})     -
     $signed({{2{hpf_prev_in[DATA_W-1]}}, hpf_prev_in})) *
    $signed({1'b0, reg_hpf_alpha});
wire signed [DATA_W-1:0] hpf_new_w = $signed(hpf_sum_w[32:17]);

// 피드백 적용 (극성 선택 포함)
// fb_clamped_r: 이미 FB_MAX 클램프 완료 → 안전
wire signed [31:0] fb_acc_w =
    $signed(lpf_r) * $signed({1'b0, fb_clamped_r});
wire signed [DATA_W-1:0] fb_raw_w = $signed(fb_acc_w[30:15]);
// [FEAT-F1] 극성: 양성(+fb) vs 음성(-fb)
wire signed [DATA_W-1:0] fb_applied =
    reg_fb_polarity ? -fb_raw_w : fb_raw_w;
wire signed [DATA_W-1:0] lpf_fb_new = soft_clip(fb_applied, reg_fb_sat_thresh);

// BRAM 쓰기 데이터 (in_hold + 피드백 합산)
wire signed [DATA_W:0]   bram_sum_w  =
    $signed({in_hold[DATA_W-1], in_hold}) +
    $signed({ap_fb1_out[DATA_W-1], ap_fb1_out});
wire signed [DATA_W-1:0] bram_wr_w   = sat16(bram_sum_w);

// [FEAT-F2] Comb Filter 믹스
// wet_r: 출력 APF 통과한 딜레이 신호
// mix = dry*comb_depth_inv + wet*comb_depth (reg_dry_wet 비율 적용)
// 최종: out = in*(1-dry_wet) + (in*(1-comb_depth) + wet*comb_depth)*dry_wet
// 단순화: out = in*(1-dry_wet) + wet_comb*dry_wet
// wet_comb = in*(1-comb_depth) + wet*comb_depth → Comb Filter 동작
wire signed [32:0] wet_comb_acc =
    $signed(in_hold) * $signed({1'b0, (16'h7FFF - reg_comb_depth)}) +
    $signed(wet_r)   * $signed({1'b0, reg_comb_depth});
wire signed [DATA_W-1:0] wet_comb_w = $signed(wet_comb_acc[30:15]);

wire signed [32:0] mix_acc_w =
    $signed(in_hold)    * $signed({1'b0, (16'h7FFF - reg_dry_wet)}) +
    $signed(wet_comb_w) * $signed({1'b0, reg_dry_wet});
wire signed [DATA_W-1:0] mix_w = $signed(mix_acc_w[30:15]);

// ============================================================================
//  메인 FSM
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        state           <= ST_IDLE;
        tready_r        <= 1'b0;
        rd_addr_r       <= {BRAM_ADDR_W{1'b0}};
        wr_ptr          <= {MAX_DELAY_BITS{1'b0}};
        in_hold         <= {DATA_W{1'b0}};
        dly_out         <= {DATA_W{1'b0}};
        sat_r           <= {DATA_W{1'b0}};
        lpf_state       <= {DATA_W{1'b0}};
        lpf_r           <= {DATA_W{1'b0}};
        hpf_state       <= {DATA_W{1'b0}};
        hpf_prev_in     <= {DATA_W{1'b0}};
        hpf_r           <= {DATA_W{1'b0}};
        lpf_fb_r        <= {DATA_W{1'b0}};
        wet_r           <= {DATA_W{1'b0}};
        ap_fb0_en       <= 1'b0;
        ap_out0_en      <= 1'b0;
        ap_wait_cnt     <= 4'd0;
        dt_slewed       <= 22'h00F000;
        // 파이프라인 래치 리셋
        dt_mod_sat_r    <= {DT_W{1'b0}};
        fb_clamped_r    <= 16'h0000;
        eff_lpf_alpha_r <= 16'h0000;
        ap_g_fb_r       <= {DATA_W{1'b0}};
        ap_g_out_r      <= {DATA_W{1'b0}};
        // Hermite 리셋
        h_ym1 <= {DATA_W{1'b0}};
        h_y0  <= {DATA_W{1'b0}};
        h_y1  <= {DATA_W{1'b0}};
        h_y2  <= {DATA_W{1'b0}};
        h_frac <= 8'h00;
        main_addrm1 <= {MAX_DELAY_BITS{1'b0}};
        main_addr0  <= {MAX_DELAY_BITS{1'b0}};
        main_addr1  <= {MAX_DELAY_BITS{1'b0}};
        main_addr2  <= {MAX_DELAY_BITS{1'b0}};
        main_frac   <= 8'h00;
    end else begin
        ap_fb0_en  <= 1'b0;
        ap_out0_en <= 1'b0;

        case (state)

        // ── 입력 대기 ────────────────────────────────────────────────────────
        ST_IDLE: begin
            tready_r <= 1'b1;
            if (fire_in) begin
                in_hold  <= s_axis_tdata;
                tready_r <= 1'b0;
                state    <= ST_MOD_LATCH;
            end
        end

        // ── LFO 변조 결과 래치 (조합→조합 체인 타이밍 절단) ─────────────────
        ST_MOD_LATCH: begin
            dt_mod_sat_r    <= dt_mod_sat_w;
            fb_clamped_r    <= fb_clamped;
            eff_lpf_alpha_r <= eff_lpf_alpha;
            ap_g_fb_r       <= ap_g_fb_w;
            ap_g_out_r      <= ap_g_out_w;
            state           <= ST_ADDR;
        end

        // ── Hermite 읽기 주소 계산 ──────────────────────────────────────────
        ST_ADDR: begin
            main_addrm1 <= wr_ptr - dt_mod_sat_r[DT_W-1:8] - 1'b1;
            main_addr0  <= wr_ptr - dt_mod_sat_r[DT_W-1:8];
            main_addr1  <= wr_ptr - dt_mod_sat_r[DT_W-1:8] + 1'b1;
            main_addr2  <= wr_ptr - dt_mod_sat_r[DT_W-1:8] + 2'd2;
            main_frac   <= dt_mod_sat_r[7:0];
            rd_addr_r   <= {1'b0, (wr_ptr - dt_mod_sat_r[DT_W-1:8] - 1'b1)};
            state       <= ST_RDm1;
        end

        // ── Hermite 4점 BRAM 읽기 ────────────────────────────────────────────
        ST_RDm1: begin
            h_ym1     <= bram_rd;
            rd_addr_r <= {1'b0, main_addr0};
            state     <= ST_RD0;
        end
        ST_RD0: begin
            h_y0      <= bram_rd;
            rd_addr_r <= {1'b0, main_addr1};
            state     <= ST_RD1;
        end
        ST_RD1: begin
            h_y1      <= bram_rd;
            rd_addr_r <= {1'b0, main_addr2};
            state     <= ST_RD2;
        end
        ST_RD2: begin
            h_y2   <= bram_rd;
            h_frac <= main_frac;
            state  <= ST_PL1;
        end

        // ── Hermite DSP 파이프라인 대기 (3클럭) ──────────────────────────────
        ST_PL1: state <= ST_PL2;
        ST_PL2: state <= ST_PL3;
        ST_PL3: begin
            dly_out <= sat16_17(h_raw);  // 보간 완료 딜레이 출력
            state   <= ST_SAT;
        end

        // ── BBD 비선형성 포화 ────────────────────────────────────────────────
        ST_SAT: begin
            sat_r <= soft_clip(dly_out, reg_sat_thresh);
            state <= ST_HPF_PRE;
        end

        // ── LPF (BBD 고역 롤오프) ────────────────────────────────────────────
        ST_HPF_PRE: begin
            lpf_r     <= lpf_new_w;
            lpf_state <= lpf_new_w;
            state     <= ST_HPF;
        end

        // ── HPF (DC 커팅) ────────────────────────────────────────────────────
        ST_HPF: begin
            hpf_r       <= hpf_new_w;
            hpf_state   <= hpf_new_w;
            hpf_prev_in <= lpf_r;
            state       <= ST_APFB_TRIG;
        end

        // ── 피드백 APF diffusion 트리거 ──────────────────────────────────────
        ST_APFB_TRIG: begin
            lpf_fb_r    <= lpf_fb_new;
            ap_fb0_en   <= 1'b1;
            ap_wait_cnt <= AP_WAIT;
            state       <= ST_APFB_WAIT;
        end

        ST_APFB_WAIT: begin
            if (ap_fb1_valid || (ap_wait_cnt == 4'd1))
                state <= ST_FBMIX;
            else
                ap_wait_cnt <= ap_wait_cnt - 4'd1;
        end

        // ── 피드백 루프 완성 + BRAM 쓰기 + 슬루 ─────────────────────────────
        ST_FBMIX: begin
            delay_ram[{1'b0, wr_ptr}] <= bram_wr_w;
            wr_ptr <= wr_ptr + 1'b1;

            // 딜레이 슬루 (클릭 방지)
            if (dt_slewed < dt_target)
                dt_slewed <= (dt_diff_w <= slew_step) ?
                              dt_target : dt_slewed + slew_step;
            else if (dt_slewed > dt_target)
                dt_slewed <= (dt_diff_w <= slew_step) ?
                              dt_target : dt_slewed - slew_step;

            state <= ST_APOUT_TRIG;
        end

        // ── 출력 APF diffusion (딜레이 출력에 위상 확산 적용) ────────────────
        ST_APOUT_TRIG: begin
            ap_out0_en  <= 1'b1;
            ap_wait_cnt <= AP_WAIT;
            state       <= ST_APOUT_WAIT;
        end

        ST_APOUT_WAIT: begin
            if (ap_out1_valid || (ap_wait_cnt == 4'd1)) begin
                wet_r <= ap_out1_out;
                state <= ST_MIX;
            end else begin
                ap_wait_cnt <= ap_wait_cnt - 4'd1;
            end
        end

        // ── Dry/Wet 믹스 + Comb Filter 합성 ─────────────────────────────────
        ST_MIX: begin
            state <= ST_OUTPUT;
        end

        // ── 출력 ─────────────────────────────────────────────────────────────
        ST_OUTPUT: begin
            state <= ST_DRAIN;
        end

        // ── 출력 핸드셰이크 대기 ─────────────────────────────────────────────
        ST_DRAIN: begin
            if (!m_axis_tvalid || m_axis_tready) begin
                state    <= ST_IDLE;
                tready_r <= 1'b1;
            end
        end

        default: begin
            state    <= ST_IDLE;
            tready_r <= 1'b0;
        end
        endcase
    end
end

// ============================================================================
//  출력 레지스터 (AXI-Stream Master)
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        m_axis_tvalid <= 1'b0;
        m_axis_tdata  <= {DATA_W{1'b0}};
    end else begin
        if (state == ST_OUTPUT) begin
            if      ($signed(mix_w) > $signed(SAT16_MAX)) m_axis_tdata <= SAT16_MAX;
            else if ($signed(mix_w) < $signed(SAT16_MIN)) m_axis_tdata <= SAT16_MIN;
            else                                           m_axis_tdata <= mix_w;
            m_axis_tvalid <= 1'b1;
        end else if (m_axis_tvalid && m_axis_tready) begin
            m_axis_tvalid <= 1'b0;
        end
    end
end

// lfo_ch5~7: 포트 선언만 (미사용, 향후 확장용)
wire _unused_lfo = |{lfo_ch5, lfo_ch6, lfo_ch7};

endmodule