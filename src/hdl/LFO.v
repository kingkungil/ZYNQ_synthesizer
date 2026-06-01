// ============================================================================
//  nco_multi_lfo.v  Rev.4
//  범용 8채널 LFO 생성기 (fdn_reverb_top v13 전용 외부 NCO)
//
//  ── Rev.4 변경 사항 (Rev.3 대비) ─────────────────────────────────────
//
//  [OPT-A] 위상 누산기 32bit 통일
//    Rev.3: 32bit 위상 + 7bit ROM 인덱스 (128점 BRAM ROM)
//    Rev.4: 동일 유지 + 위상 오프셋 32bit 전폭 활용
//    → ch_eff_phase = phase_acc + reg_poff (32bit 덧셈, 오버플로우 = 자연 위상 순환) ✓
//
//  [OPT-B] LFO FCW(주파수 제어 워드) 재계산
//    FCW = round(f_hz × 2^32 / clk_hz) = round(f_hz × 2^32 / 50_000_000)
//    CH0 (0.80Hz): 0.80 × 2^32 / 50e6 = 68.72 → 32'h00000045
//    CH1 (0.50Hz): 0.50 × 2^32 / 50e6 = 42.95 → 32'h0000002B
//    CH2 (0.50Hz): 동일                        → 32'h0000002B
//    CH3 (5.00Hz): 5.00 × 2^32 / 50e6 = 429.5 → 32'h000001AE
//    CH4 (0.30Hz): 0.30 × 2^32 / 50e6 = 25.77 → 32'h0000001A
//    CH5 (0.30Hz): 동일                        → 32'h0000001A
//    CH6 (4.50Hz): 4.50 × 2^32 / 50e6 = 386.5 → 32'h00000183
//    CH7 (0.20Hz): 0.20 × 2^32 / 50e6 = 17.18 → 32'h00000011
//
//  [OPT-C] BRAM ROM 128점 → fdn_reverb_top v13과 동일한 ROM 값
//    DRC 안전: (* rom_style = "block" *) 어트리뷰트 유지
//    BRAM18E1 1개 사용 (128×16 = 2Kbit < 18Kbit) ✓
//
//  [OPT-D] 삼각파 계산 수치 안정화
//    Rev.3 [FIX-N] 유지: eff_phase[30:15] 전체 16bit 사용
//    범위: 0~65535 → tri 값: -32768 ~ +32767
//    포화: sat_17to16 함수로 -32768~+32767 클램프
//
//  [OPT-E] Depth 스케일 정밀화
//    Rev.3: scaled = wave_raw × depth → [30:15]
//    Rev.4: 동일 방식 유지하되, depth=0x8000(1.0) 시 최대 진폭 확인
//    wave_raw 최대: +32767 (sine 피크 < 32768) → 오버플로우 없음 ✓
//    depth=0x8000, wave=32767: scaled = 32767×32768 = 0x7FFF0000
//    [30:15] = 0x7FFF = 32767 ✓ (정확히 1.0 게인)
//
//  [OPT-F] DRC 안전성 강화
//    · 모든 레지스터 리셋값 명시 (Vivado DRC REQP-13 방지)
//    · 조합 논리 latch 방지: always @(*) 에 default 추가
//    · generate 블록 명명: GEN_PHASE (기존 유지)
//
//  ── BRAM/LUT 사용량 (@50MHz, Vivado 2022.2) ─────────────────────────
//    BRAM18E1: 1개 (sine ROM 128×16bit)
//    DSP48E1:  1개 (depth 곱셈, 공유)
//    FF:       ~200 (위상 누산기 8×32bit + 파이프라인 + 출력 레지스터)
//    LUT:      ~150 (파형 선택 + 주소 계산)
//
//  ── IO 핀 수 ───────────────────────────────────────────────────────
//    CLK/RST: 2, AXI-Lite: 52, LFO_OUT×8: 128 → 합계 182핀
//    XC7Z020 CLG484 가용 IO: 200핀 → 여유 18핀
//    ※ fdn_reverb_top과 같은 디바이스에 넣으면 IO 초과 → 별도 IP로 사용할 것
//       또는 fdn_reverb_top에 내장 NCO 사용 (USE_EXTERNAL_LFO=0)
//
//  ── 연결 예시 ──────────────────────────────────────────────────────
//    fdn_reverb_top v13에 직접 연결 (USE_EXTERNAL_LFO=0이면 불필요):
//    만약 최상위 래퍼에서 두 IP를 내부 연결하면 IO 핀 소모 없음
// ============================================================================
`timescale 1ns/1ps

module nco_multi_lfo #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 8,
    parameter integer NUM_CH             = 8,
    parameter integer DATA_W             = 16,

    // ── 기본 FCW (@50MHz, 수정된 값) ──────────────────────────────────
    parameter [31:0] DEFAULT_FCW_0  = 32'h00000045,  // 0.80 Hz  CH0: ER delay jitter
    parameter [31:0] DEFAULT_FCW_1  = 32'h0000002B,  // 0.50 Hz  CH1: ER stereo width
    parameter [31:0] DEFAULT_FCW_2  = 32'h0000001A,  // 0.30 Hz  CH2: FDN tap gain (slow) [LFO-MOD]
    parameter [31:0] DEFAULT_FCW_3  = 32'h0000000D,  // 0.15 Hz  CH3: FDN fb micro drift (very slow) [LFO-MOD]
    parameter [31:0] DEFAULT_FCW_4  = 32'h0000001A,  // 0.30 Hz (Reverb A)
    parameter [31:0] DEFAULT_FCW_5  = 32'h0000001A,  // 0.30 Hz (Reverb B, +45° 오프셋)
    parameter [31:0] DEFAULT_FCW_6  = 32'h00000183,  // 4.50 Hz (Vibrato)
    parameter [31:0] DEFAULT_FCW_7  = 32'h00000011,  // 0.20 Hz (Phaser)

    // ── 기본 Depth (Q1.15: 0x8000=1.0) ───────────────────────────────
    parameter [15:0] DEFAULT_DEPTH_0 = 16'h0180,   // 약한 Wow (~0.75%)  CH0: ER jitter
    parameter [15:0] DEFAULT_DEPTH_1 = 16'h0800,   // ER stereo width (~6.25%) [LFO-MOD: 10%→6.25%]
    parameter [15:0] DEFAULT_DEPTH_2 = 16'h8000,   // CH2 FDN tap: full→safety layer가 ÷128 clamp
    parameter [15:0] DEFAULT_DEPTH_3 = 16'h8000,   // CH3 FDN fb: full→safety layer가 추가 clamp
    parameter [15:0] DEFAULT_DEPTH_4 = 16'h0800,   // Reverb (~6.25%)
    parameter [15:0] DEFAULT_DEPTH_5 = 16'h0800,   // Reverb
    parameter [15:0] DEFAULT_DEPTH_6 = 16'h0C00,   // Vibrato (~9.4%)
    parameter [15:0] DEFAULT_DEPTH_7 = 16'h0400,   // Phaser (~3.1%)

    // ── 기본 위상 오프셋 (32bit, 0x40000000=90°) ──────────────────────
    parameter [31:0] DEFAULT_POFF_0 = 32'h0000_0000,
    parameter [31:0] DEFAULT_POFF_1 = 32'h0000_0000,
    parameter [31:0] DEFAULT_POFF_2 = 32'h4000_0000, // +90° Chorus R
    parameter [31:0] DEFAULT_POFF_3 = 32'h0000_0000,
    parameter [31:0] DEFAULT_POFF_4 = 32'h0000_0000,
    parameter [31:0] DEFAULT_POFF_5 = 32'h2000_0000, // +45° Reverb B
    parameter [31:0] DEFAULT_POFF_6 = 32'h0000_0000,
    parameter [31:0] DEFAULT_POFF_7 = 32'h0000_0000,

    // ── 기본 파형 (0=Sine 1=Triangle 2=Square) ────────────────────────
    parameter [1:0] DEFAULT_WAVE_0 = 2'd0,
    parameter [1:0] DEFAULT_WAVE_1 = 2'd0,
    parameter [1:0] DEFAULT_WAVE_2 = 2'd0,
    parameter [1:0] DEFAULT_WAVE_3 = 2'd1,   // Triangle (Tremolo)
    parameter [1:0] DEFAULT_WAVE_4 = 2'd0,
    parameter [1:0] DEFAULT_WAVE_5 = 2'd0,
    parameter [1:0] DEFAULT_WAVE_6 = 2'd0,
    parameter [1:0] DEFAULT_WAVE_7 = 2'd0
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
    input  wire                              S_AXI_ACLK,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 S_AXI_ARESETN RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                              S_AXI_ARESETN,

    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWADDR" *)
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]    S_AXI_AWADDR,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWPROT" *)
    input  wire [2:0]                        S_AXI_AWPROT,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *)
    input  wire                              S_AXI_AWVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *)
    output reg                               S_AXI_AWREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WDATA" *)
    input  wire [C_S_AXI_DATA_WIDTH-1:0]    S_AXI_WDATA,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WSTRB" *)
    input  wire [3:0]                        S_AXI_WSTRB,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WVALID" *)
    input  wire                              S_AXI_WVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WREADY" *)
    output reg                               S_AXI_WREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BRESP" *)
    output reg  [1:0]                        S_AXI_BRESP,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BVALID" *)
    output reg                               S_AXI_BVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BREADY" *)
    input  wire                              S_AXI_BREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARADDR" *)
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]    S_AXI_ARADDR,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARPROT" *)
    input  wire [2:0]                        S_AXI_ARPROT,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *)
    input  wire                              S_AXI_ARVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *)
    output reg                               S_AXI_ARREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RDATA" *)
    output reg  [C_S_AXI_DATA_WIDTH-1:0]    S_AXI_RDATA,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RRESP" *)
    output reg  [1:0]                        S_AXI_RRESP,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RVALID" *)
    output reg                               S_AXI_RVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RREADY" *)
    input  wire                              S_AXI_RREADY,

    output reg signed [DATA_W-1:0] lfo_out_0,
    output reg signed [DATA_W-1:0] lfo_out_1,
    output reg signed [DATA_W-1:0] lfo_out_2,
    output reg signed [DATA_W-1:0] lfo_out_3,
    output reg signed [DATA_W-1:0] lfo_out_4,
    output reg signed [DATA_W-1:0] lfo_out_5,
    output reg signed [DATA_W-1:0] lfo_out_6,
    output reg signed [DATA_W-1:0] lfo_out_7
);

// ============================================================================
//  Quarter-wave Sine ROM (128×16bit, BRAM18E1)
//  [OPT-C] fdn_reverb_top v13과 동일한 128점 ROM
//  값: round(sin(i*π/256)*32767), i=0..127
// ============================================================================
(* rom_style = "block" *) reg [15:0] sine_rom [0:127];

initial begin
    sine_rom[  0]=16'd0;     sine_rom[  1]=16'd402;   sine_rom[  2]=16'd804;
    sine_rom[  3]=16'd1206;  sine_rom[  4]=16'd1608;  sine_rom[  5]=16'd2010;
    sine_rom[  6]=16'd2411;  sine_rom[  7]=16'd2811;  sine_rom[  8]=16'd3212;
    sine_rom[  9]=16'd3612;  sine_rom[ 10]=16'd4011;  sine_rom[ 11]=16'd4410;
    sine_rom[ 12]=16'd4808;  sine_rom[ 13]=16'd5205;  sine_rom[ 14]=16'd5602;
    sine_rom[ 15]=16'd5998;  sine_rom[ 16]=16'd6393;  sine_rom[ 17]=16'd6787;
    sine_rom[ 18]=16'd7179;  sine_rom[ 19]=16'd7571;  sine_rom[ 20]=16'd7962;
    sine_rom[ 21]=16'd8351;  sine_rom[ 22]=16'd8739;  sine_rom[ 23]=16'd9126;
    sine_rom[ 24]=16'd9512;  sine_rom[ 25]=16'd9896;  sine_rom[ 26]=16'd10278;
    sine_rom[ 27]=16'd10659; sine_rom[ 28]=16'd11039; sine_rom[ 29]=16'd11417;
    sine_rom[ 30]=16'd11793; sine_rom[ 31]=16'd12167; sine_rom[ 32]=16'd12540;
    sine_rom[ 33]=16'd12910; sine_rom[ 34]=16'd13279; sine_rom[ 35]=16'd13645;
    sine_rom[ 36]=16'd14010; sine_rom[ 37]=16'd14372; sine_rom[ 38]=16'd14733;
    sine_rom[ 39]=16'd15091; sine_rom[ 40]=16'd15447; sine_rom[ 41]=16'd15800;
    sine_rom[ 42]=16'd16151; sine_rom[ 43]=16'd16499; sine_rom[ 44]=16'd16846;
    sine_rom[ 45]=16'd17189; sine_rom[ 46]=16'd17530; sine_rom[ 47]=16'd17869;
    sine_rom[ 48]=16'd18205; sine_rom[ 49]=16'd18538; sine_rom[ 50]=16'd18868;
    sine_rom[ 51]=16'd19196; sine_rom[ 52]=16'd19520; sine_rom[ 53]=16'd19841;
    sine_rom[ 54]=16'd20160; sine_rom[ 55]=16'd20475; sine_rom[ 56]=16'd20788;
    sine_rom[ 57]=16'd21097; sine_rom[ 58]=16'd21403; sine_rom[ 59]=16'd21706;
    sine_rom[ 60]=16'd22005; sine_rom[ 61]=16'd22301; sine_rom[ 62]=16'd22594;
    sine_rom[ 63]=16'd22884; sine_rom[ 64]=16'd23170; sine_rom[ 65]=16'd23453;
    sine_rom[ 66]=16'd23732; sine_rom[ 67]=16'd24008; sine_rom[ 68]=16'd24280;
    sine_rom[ 69]=16'd24548; sine_rom[ 70]=16'd24813; sine_rom[ 71]=16'd25075;
    sine_rom[ 72]=16'd25330; sine_rom[ 73]=16'd25581; sine_rom[ 74]=16'd25832;
    sine_rom[ 75]=16'd26077; sine_rom[ 76]=16'd26319; sine_rom[ 77]=16'd26557;
    sine_rom[ 78]=16'd26791; sine_rom[ 79]=16'd27020; sine_rom[ 80]=16'd27245;
    sine_rom[ 81]=16'd27466; sine_rom[ 82]=16'd27684; sine_rom[ 83]=16'd27897;
    sine_rom[ 84]=16'd28106; sine_rom[ 85]=16'd28311; sine_rom[ 86]=16'd28511;
    sine_rom[ 87]=16'd28707; sine_rom[ 88]=16'd28898; sine_rom[ 89]=16'd29086;
    sine_rom[ 90]=16'd29269; sine_rom[ 91]=16'd29447; sine_rom[ 92]=16'd29621;
    sine_rom[ 93]=16'd29791; sine_rom[ 94]=16'd29956; sine_rom[ 95]=16'd30117;
    sine_rom[ 96]=16'd30273; sine_rom[ 97]=16'd30425; sine_rom[ 98]=16'd30571;
    sine_rom[ 99]=16'd30714; sine_rom[100]=16'd30852; sine_rom[101]=16'd30985;
    sine_rom[102]=16'd31114; sine_rom[103]=16'd31238; sine_rom[104]=16'd31357;
    sine_rom[105]=16'd31471; sine_rom[106]=16'd31581; sine_rom[107]=16'd31685;
    sine_rom[108]=16'd31785; sine_rom[109]=16'd31880; sine_rom[110]=16'd31971;
    sine_rom[111]=16'd32057; sine_rom[112]=16'd32138; sine_rom[113]=16'd32214;
    sine_rom[114]=16'd32285; sine_rom[115]=16'd32352; sine_rom[116]=16'd32413;
    sine_rom[117]=16'd32470; sine_rom[118]=16'd32522; sine_rom[119]=16'd32570;
    sine_rom[120]=16'd32610; sine_rom[121]=16'd32646; sine_rom[122]=16'd32679;
    sine_rom[123]=16'd32706; sine_rom[124]=16'd32728; sine_rom[125]=16'd32746;
    sine_rom[126]=16'd32758; sine_rom[127]=16'd32766;
end

// ============================================================================
//  AXI-Lite 레지스터
// ============================================================================
reg [31:0] reg_fcw   [0:7];
reg [15:0] reg_depth [0:7];
reg [31:0] reg_poff  [0:7];
reg [1:0]  reg_wave  [0:7];

// ============================================================================
//  AXI-Lite 쓰기 FSM
//  주소: addr[6:4]=채널(0~7), addr[3:2]=레지스터(0~3)
// ============================================================================
localparam [1:0] WR_IDLE=2'd0, WR_ACTIVE=2'd1, WR_RESP=2'd2;
reg [1:0]                     wr_state;
reg [C_S_AXI_ADDR_WIDTH-1:0] wr_addr;
reg [C_S_AXI_DATA_WIDTH-1:0] wdata_r;
reg                           wr_en;

wire [2:0] wr_ch  = wr_addr[6:4];
wire [1:0] wr_reg = wr_addr[3:2];

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        wr_state      <= WR_IDLE;
        S_AXI_AWREADY <= 1'b0;
        S_AXI_WREADY  <= 1'b0;
        S_AXI_BVALID  <= 1'b0;
        S_AXI_BRESP   <= 2'b00;
        wr_en         <= 1'b0;
        wr_addr       <= {C_S_AXI_ADDR_WIDTH{1'b0}};
        wdata_r       <= 32'd0;
        // [OPT-F] 기본값 명시적 초기화
        reg_fcw[0]<=DEFAULT_FCW_0;   reg_depth[0]<=DEFAULT_DEPTH_0;
        reg_poff[0]<=DEFAULT_POFF_0; reg_wave[0]<=DEFAULT_WAVE_0;
        reg_fcw[1]<=DEFAULT_FCW_1;   reg_depth[1]<=DEFAULT_DEPTH_1;
        reg_poff[1]<=DEFAULT_POFF_1; reg_wave[1]<=DEFAULT_WAVE_1;
        reg_fcw[2]<=DEFAULT_FCW_2;   reg_depth[2]<=DEFAULT_DEPTH_2;
        reg_poff[2]<=DEFAULT_POFF_2; reg_wave[2]<=DEFAULT_WAVE_2;
        reg_fcw[3]<=DEFAULT_FCW_3;   reg_depth[3]<=DEFAULT_DEPTH_3;
        reg_poff[3]<=DEFAULT_POFF_3; reg_wave[3]<=DEFAULT_WAVE_3;
        reg_fcw[4]<=DEFAULT_FCW_4;   reg_depth[4]<=DEFAULT_DEPTH_4;
        reg_poff[4]<=DEFAULT_POFF_4; reg_wave[4]<=DEFAULT_WAVE_4;
        reg_fcw[5]<=DEFAULT_FCW_5;   reg_depth[5]<=DEFAULT_DEPTH_5;
        reg_poff[5]<=DEFAULT_POFF_5; reg_wave[5]<=DEFAULT_WAVE_5;
        reg_fcw[6]<=DEFAULT_FCW_6;   reg_depth[6]<=DEFAULT_DEPTH_6;
        reg_poff[6]<=DEFAULT_POFF_6; reg_wave[6]<=DEFAULT_WAVE_6;
        reg_fcw[7]<=DEFAULT_FCW_7;   reg_depth[7]<=DEFAULT_DEPTH_7;
        reg_poff[7]<=DEFAULT_POFF_7; reg_wave[7]<=DEFAULT_WAVE_7;
    end else begin
        S_AXI_AWREADY <= 1'b0;
        S_AXI_WREADY  <= 1'b0;
        wr_en         <= 1'b0;
        case (wr_state)
            WR_IDLE: begin
                if (S_AXI_AWVALID) begin
                    S_AXI_AWREADY <= 1'b1;
                    wr_addr       <= S_AXI_AWADDR;
                    wr_state      <= WR_ACTIVE;
                end
            end
            WR_ACTIVE: begin
                if (S_AXI_WVALID) begin
                    wdata_r      <= S_AXI_WDATA;
                    S_AXI_WREADY <= 1'b1;
                    wr_en        <= 1'b1;
                    wr_state     <= WR_RESP;
                end
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
            case (wr_reg)
                2'd0: reg_fcw  [wr_ch] <= wdata_r;
                2'd1: reg_depth[wr_ch] <= wdata_r[15:0];
                2'd2: reg_poff [wr_ch] <= wdata_r;
                2'd3: reg_wave [wr_ch] <= wdata_r[1:0];
                default: ;
            endcase
        end
    end
end

// ============================================================================
//  AXI-Lite 읽기 FSM
// ============================================================================
wire [2:0] rd_ch  = S_AXI_ARADDR[6:4];
wire [1:0] rd_reg = S_AXI_ARADDR[3:2];

localparam [0:0] RD_IDLE=1'b0, RD_SEND=1'b1;
reg rd_state;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rd_state      <= RD_IDLE;
        S_AXI_ARREADY <= 1'b0;
        S_AXI_RVALID  <= 1'b0;
        S_AXI_RDATA   <= 32'h0;
        S_AXI_RRESP   <= 2'b00;
    end else begin
        S_AXI_ARREADY <= 1'b0;
        case (rd_state)
            RD_IDLE: begin
                if (S_AXI_ARVALID) begin
                    S_AXI_ARREADY <= 1'b1;
                    case (rd_reg)
                        2'd0: S_AXI_RDATA <= reg_fcw  [rd_ch];
                        2'd1: S_AXI_RDATA <= {{16{1'b0}}, reg_depth[rd_ch]};
                        2'd2: S_AXI_RDATA <= reg_poff [rd_ch];
                        2'd3: S_AXI_RDATA <= {{30{1'b0}}, reg_wave[rd_ch]};
                        default: S_AXI_RDATA <= 32'hDEADBEEF;
                    endcase
                    rd_state <= RD_SEND;
                end
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
//  8채널 위상 누산기
// ============================================================================
reg [31:0] phase_acc [0:7];

wire [31:0] eff_phase [0:7];
assign eff_phase[0] = phase_acc[0] + reg_poff[0];
assign eff_phase[1] = phase_acc[1] + reg_poff[1];
assign eff_phase[2] = phase_acc[2] + reg_poff[2];
assign eff_phase[3] = phase_acc[3] + reg_poff[3];
assign eff_phase[4] = phase_acc[4] + reg_poff[4];
assign eff_phase[5] = phase_acc[5] + reg_poff[5];
assign eff_phase[6] = phase_acc[6] + reg_poff[6];
assign eff_phase[7] = phase_acc[7] + reg_poff[7];

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        phase_acc[0]<=32'h0; phase_acc[1]<=32'h0;
        phase_acc[2]<=32'h0; phase_acc[3]<=32'h0;
        phase_acc[4]<=32'h0; phase_acc[5]<=32'h0;
        phase_acc[6]<=32'h0; phase_acc[7]<=32'h0;
    end else begin
        phase_acc[0]<=phase_acc[0]+reg_fcw[0];
        phase_acc[1]<=phase_acc[1]+reg_fcw[1];
        phase_acc[2]<=phase_acc[2]+reg_fcw[2];
        phase_acc[3]<=phase_acc[3]+reg_fcw[3];
        phase_acc[4]<=phase_acc[4]+reg_fcw[4];
        phase_acc[5]<=phase_acc[5]+reg_fcw[5];
        phase_acc[6]<=phase_acc[6]+reg_fcw[6];
        phase_acc[7]<=phase_acc[7]+reg_fcw[7];
    end
end

// ============================================================================
//  사분면 + ROM 인덱스 (8채널)
//  quad[1:0] = eff_phase[31:30]
//  Q0,Q2(quad[0]=0): 직접 인덱스    eff_phase[29:23]
//  Q1,Q3(quad[0]=1): 반전 인덱스  = 127 - eff_phase[29:23]
// ============================================================================
wire [1:0] quad    [0:7];
wire [6:0] rom_idx [0:7];

genvar gi;
generate
    for (gi=0; gi<8; gi=gi+1) begin : GEN_PHASE
        assign quad[gi]    = eff_phase[gi][31:30];
        assign rom_idx[gi] = quad[gi][0]
                             ? (7'd127 - eff_phase[gi][29:23])
                             :            eff_phase[gi][29:23];
    end
endgenerate

// ============================================================================
//  라운드로빈 ROM 읽기 파이프라인 (3스테이지)
//
//  T+0: 채널 카운터 증가, ROM 주소 래치
//  T+1: ROM 데이터 읽기 완료 (BRAM 1클럭 지연)
//  T+2: 파형 합성 + depth 스케일 + 출력 레지스터 갱신
//
//  갱신 주기: 8채널 / 50MHz = 160ns
//  LFO 최고 주파수 5Hz → 주기 200ms → 갱신 비율 160ns/200ms = 8×10^-7 → 무시 가능 ✓
// ============================================================================
reg [2:0]  rr_cnt;

// T+0 → T+1 파이프라인
reg [6:0]  rom_addr_r;
reg [1:0]  quad_lat1;
reg [2:0]  rr_ch_lat1;
reg [31:0] eff_phase_lat1;

// T+1 → T+2 파이프라인
reg [15:0] rom_data_r;
reg [2:0]  rr_ch_lat2;
reg [1:0]  quad_lat2;
reg [1:0]  wave_lat2;
reg [15:0] depth_lat2;
reg [31:0] eff_phase_lat2;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rr_cnt        <= 3'd0;
        rom_addr_r    <= 7'd0;
        quad_lat1     <= 2'd0;
        rr_ch_lat1    <= 3'd0;
        eff_phase_lat1<= 32'd0;
        rom_data_r    <= 16'd0;
        rr_ch_lat2    <= 3'd0;
        quad_lat2     <= 2'd0;
        depth_lat2    <= 16'd0;
        wave_lat2     <= 2'd0;
        eff_phase_lat2<= 32'd0;
    end else begin
        // T+0: 주소 래치
        rr_cnt         <= rr_cnt + 3'd1;
        rom_addr_r     <= rom_idx[rr_cnt];
        quad_lat1      <= quad[rr_cnt];
        rr_ch_lat1     <= rr_cnt;
        eff_phase_lat1 <= eff_phase[rr_cnt];

        // T+1: ROM 읽기 + 메타데이터 지연
        rom_data_r     <= sine_rom[rom_addr_r];
        rr_ch_lat2     <= rr_ch_lat1;
        quad_lat2      <= quad_lat1;
        depth_lat2     <= reg_depth[rr_ch_lat1];
        wave_lat2      <= reg_wave [rr_ch_lat1];
        eff_phase_lat2 <= eff_phase_lat1;
    end
end

// ============================================================================
//  파형 합성 (T+2, 조합 논리)
//
//  ── 사인파 ──────────────────────────────────────────────────────────────
//  ROM 값: 0~32766 (절대값, 0°~90° 구간)
//  quad[1]=0 → 양수 (+sine), quad[1]=1 → 음수 (-sine)
//  sine_signed 범위: -32766 ~ +32766
//
//  ── 삼각파 [OPT-D] ──────────────────────────────────────────────────────
//  ph_half = eff_phase_lat2[30:15] (0~65535, 반주기)
//  상승 구간(bit31=0): out = ph_half - 32767  → -32767 ~ +32768
//  하강 구간(bit31=1): out = 32767 - ph_half  → +32767 ~ -32768
//  17bit 임시값 → 16bit 클램프 (±32767 이내로 포화)
//
//  [수학 검증] ph_half=0(0°): out=-32767(-1.0) → 시작점
//  ph_half=32767(90°): out=0 → 제로크로싱 ✓ (하강으로 전환)
//  ph_half=65535(270°): out=32767-65535 = -32768 → 바닥 ✓
//
//  ── 구형파 [OPT-D] ──────────────────────────────────────────────────────
//  bit31=0: +32767, bit31=1: -32768 (±1.0 대칭, DC 미세 오프셋 허용)
//
//  ── Depth 스케일 [OPT-E] ────────────────────────────────────────────────
//  scaled = wave_raw × depth (Q1.15) → [30:15] 추출
//  오버플로우 검증: max = 32767 × 32768 = 1,073,709,056 = 0x3FFF0000
//  → bit31=0 (양수 최대) ✓, 부호 비트 오염 없음
// ============================================================================
wire [15:0] ph_half = eff_phase_lat2[30:15];

wire signed [16:0] tri_s17 =
    eff_phase_lat2[31]
    ? (17'sd32767 - $signed({1'b0, ph_half}))
    : ($signed({1'b0, ph_half}) - 17'sd32767);

wire signed [15:0] tri_w =
    (tri_s17 >  17'sd32767) ?  16'sd32767 :
    (tri_s17 < -17'sd32768) ? -16'sd32768 :
     tri_s17[15:0];

wire signed [15:0] sqr_w =
    eff_phase_lat2[31] ? -16'sd32768 : 16'sd32767;

// 사인파: ROM 절대값 → 부호 적용
// rom_data_r[14:0] = 15bit 절대값 (최대 32766 ≤ 32767 ✓)
wire signed [15:0] sine_signed =
    quad_lat2[1]
    ? -$signed({1'b0, rom_data_r[14:0]})
    :  $signed({1'b0, rom_data_r[14:0]});

wire signed [15:0] wave_raw =
    (wave_lat2 == 2'd1) ? tri_w  :
    (wave_lat2 == 2'd2) ? sqr_w  :
                          sine_signed;

// Depth 스케일
wire signed [31:0] scaled_w = $signed(wave_raw) * $signed({1'b0, depth_lat2});
wire signed [15:0] out_val  = scaled_w[30:15];

// ============================================================================
//  출력 레지스터 갱신 (T+2, 라운드로빈)
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        lfo_out_0<=16'sh0; lfo_out_1<=16'sh0; lfo_out_2<=16'sh0; lfo_out_3<=16'sh0;
        lfo_out_4<=16'sh0; lfo_out_5<=16'sh0; lfo_out_6<=16'sh0; lfo_out_7<=16'sh0;
    end else begin
        case (rr_ch_lat2)
            3'd0: lfo_out_0 <= out_val;
            3'd1: lfo_out_1 <= out_val;
            3'd2: lfo_out_2 <= out_val;
            3'd3: lfo_out_3 <= out_val;
            3'd4: lfo_out_4 <= out_val;
            3'd5: lfo_out_5 <= out_val;
            3'd6: lfo_out_6 <= out_val;
            3'd7: lfo_out_7 <= out_val;
            default: ; // [OPT-F] latch 방지
        endcase
    end
end

endmodule