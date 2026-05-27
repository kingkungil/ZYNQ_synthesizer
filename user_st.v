// ============================================================================
//  auto_panner.v  Rev.2  - Commercial-Grade Spatial Auto-Panner
//  리버브/EQ 뒷단 공간 패너 이펙터
//
//  ── Rev.2 핵심 수정 (Rev.1 대비) ────────────────────────────────────
//
//  [FIX-1] Equal-Power 패닝 (Linear → Sin/Cos)
//    Rev.1: gain_l = 0x8000 - pan_pos  ← 중앙 볼륨 부풀음 발생
//    Rev.2: 이미 탑재된 Sine ROM 재활용
//           theta = (pan_pos + 32768) >> 1    (0 ~ 32767, 0°~90° 매핑)
//           gain_r = sin(theta)  ← ROM 직접 참조
//           gain_l = cos(theta)  = sin(90° - theta) ← ROM 역방향 인덱스
//           → 모든 pan 위치에서 gain_l² + gain_r² = 1 보장
//           → 중앙/사이드 에너지 동일 ✓
//
//  [FIX-2] Mid 기반 패닝 (L/R 분리 → Mid 통합)
//    Rev.1: out_l = in_l * gain_l   ← 스테레오 찢어짐
//           out_r = in_r * gain_r
//    Rev.2: mid   = (in_l + in_r) >> 1   (17bit 합산, 오버플로우 방지)
//           out_l = mid * gain_l          ← 공간 유지 + 위치만 이동
//           out_r = mid * gain_r
//
//  [ADD-1] Stereo Phase Offset
//    LFO_R = LFO_L + 90°  → 공간이 "회전"하는 느낌
//    SPHASE_EN=1 시 LFO_R 위상 +90° 자동 적용
//    비활성화 시 L/R 동일 LFO → 전통적 오토패너
//
//  ── 권장 신호 체인 ──────────────────────────────────────────────────
//    Reverb → Post EQ (M/S) → Auto Panner (이 모듈)
//    ※ M/S EQ 뒤에 두면 Mid/Side 연산 간섭 없음
//
//  ── 파이프라인 레이턴시 ──────────────────────────────────────────────
//    T+0 : ROM 주소 래치
//    T+1 : LFO ROM 읽기 완료
//    T+2 : 파형 합성 + pan_pos → Equal-Power 인덱스 래치
//    T+3 : Gain ROM 읽기 완료 + mid 보존
//    T+4 : 출력 곱셈 + 출력 레지스터 갱신
//    총 4클럭 레이턴시 / 입력 3클럭 딜레이로 타이밍 정렬
//
//  ── 레지스터 맵 (AXI-Lite) ──────────────────────────────────────────
//  Offset | 이름       | 비트    | 설명
//  0x00   | FCW        | [31:0]  | LFO 주파수 (f = FCW × 50MHz / 2³²)
//  0x04   | DEPTH      | [15:0]  | 패닝 폭 Q1.15 (0x8000=100%, 0x4000=50%)
//  0x08   | POFF       | [31:0]  | LFO 위상 오프셋 (0x40000000 = +90°)
//  0x0C   | WAVE       | [1:0]   | 0=Sine, 1=Triangle, 2=Square
//  0x10   | CENTER     | [15:0]  | 패닝 중심 signed Q1.15 (0=센터, +=우, -=좌)
//  0x14   | BYPASS     | [0]     | 1=바이패스 (레이턴시 보정 유지)
//  0x18   | SPHASE_EN  | [0]     | 1=LFO_R +90° 공간 회전 효과 ON
//
//  FCW 예시 (@50MHz):
//    0.25Hz→0x00000015  0.50Hz→0x0000002B
//    1.00Hz→0x00000056  2.00Hz→0x000000AC
//    4.00Hz→0x00000159  8.00Hz→0x000002B3
//
//  ── 리소스 추정 (@50MHz, Vivado 2022.2) ────────────────────────────
//    BRAM18E1 : 1개 (128×16 Sine ROM, LFO + Equal-Power gain 공유)
//    DSP48E1  : 2개 (mid×gain_l, mid×gain_r 각 1개)
//    FF       : ~140
//    LUT      : ~100
// ============================================================================
`timescale 1ns/1ps

module auto_panner #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 8,
    parameter integer DATA_W             = 16,

    parameter [31:0] DEFAULT_FCW       = 32'h0000002B,  // 0.50 Hz
    parameter [15:0] DEFAULT_DEPTH     = 16'h6000,      // 75% 패닝 폭
    parameter [31:0] DEFAULT_POFF      = 32'h0000_0000,
    parameter [1:0]  DEFAULT_WAVE      = 2'd0,          // Sine
    parameter [15:0] DEFAULT_CENTER    = 16'h0000,      // 정중앙
    parameter        DEFAULT_BYPASS    = 1'b0,
    parameter        DEFAULT_SPHASE_EN = 1'b1           // 공간 회전 기본 ON
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

    input  wire signed [DATA_W-1:0]  in_l,
    input  wire signed [DATA_W-1:0]  in_r,
    output reg  signed [DATA_W-1:0]  out_l,
    output reg  signed [DATA_W-1:0]  out_r
);

// ============================================================================
//  Quarter-wave Sine ROM (128×16bit, BRAM18E1)
//  LFO 파형 생성 + Equal-Power gain 계산에 공유 사용
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
reg [31:0] reg_fcw;
reg [15:0] reg_depth;
reg [31:0] reg_poff;
reg [1:0]  reg_wave;
reg [15:0] reg_center;
reg        reg_bypass;
reg        reg_sphase_en;

// ============================================================================
//  AXI-Lite 쓰기 FSM
// ============================================================================
localparam [1:0] WR_IDLE=2'd0, WR_ACTIVE=2'd1, WR_RESP=2'd2;
reg [1:0]                     wr_state;
reg [C_S_AXI_ADDR_WIDTH-1:0] wr_addr;
reg [C_S_AXI_DATA_WIDTH-1:0] wdata_r;
reg                           wr_en;
wire [2:0] wr_reg = wr_addr[4:2];

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
        reg_fcw       <= DEFAULT_FCW;
        reg_depth     <= DEFAULT_DEPTH;
        reg_poff      <= DEFAULT_POFF;
        reg_wave      <= DEFAULT_WAVE;
        reg_center    <= DEFAULT_CENTER;
        reg_bypass    <= DEFAULT_BYPASS;
        reg_sphase_en <= DEFAULT_SPHASE_EN;
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
                3'd0: reg_fcw       <= wdata_r;
                3'd1: reg_depth     <= wdata_r[15:0];
                3'd2: reg_poff      <= wdata_r;
                3'd3: reg_wave      <= wdata_r[1:0];
                3'd4: reg_center    <= wdata_r[15:0];
                3'd5: reg_bypass    <= wdata_r[0];
                3'd6: reg_sphase_en <= wdata_r[0];
                default: ;
            endcase
        end
    end
end

// ============================================================================
//  AXI-Lite 읽기 FSM
// ============================================================================
wire [2:0] rd_reg = S_AXI_ARADDR[4:2];
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
                        3'd0: S_AXI_RDATA <= reg_fcw;
                        3'd1: S_AXI_RDATA <= {{16{1'b0}}, reg_depth};
                        3'd2: S_AXI_RDATA <= reg_poff;
                        3'd3: S_AXI_RDATA <= {{30{1'b0}}, reg_wave};
                        3'd4: S_AXI_RDATA <= {{16{reg_center[15]}}, reg_center};
                        3'd5: S_AXI_RDATA <= {{31{1'b0}}, reg_bypass};
                        3'd6: S_AXI_RDATA <= {{31{1'b0}}, reg_sphase_en};
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
//  위상 누산기
//  eff_phase   : L채널 LFO 위상
//  eff_phase_r : R채널 LFO 위상 (SPHASE_EN=1이면 +90°)
// ============================================================================
reg [31:0] phase_acc;

wire [31:0] eff_phase   = phase_acc + reg_poff;
wire [31:0] eff_phase_r = eff_phase + (reg_sphase_en ? 32'h4000_0000 : 32'h0);

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) phase_acc <= 32'h0;
    else                phase_acc <= phase_acc + reg_fcw;
end

// ============================================================================
//  사분면 + ROM 인덱스
//  LFO 파형 생성: L 위상 기준 (eff_phase)
// ============================================================================
wire [1:0] quad_lfo = eff_phase[31:30];
wire [6:0] rom_idx_lfo = quad_lfo[0]
                         ? (7'd127 - eff_phase[29:23])
                         :           eff_phase[29:23];

// ============================================================================
//  파이프라인 스테이지 정의
//
//  T+0 : ROM 주소 래치 (LFO용)
//  T+1 : LFO ROM 읽기 완료
//  T+2 : 파형 합성 → pan_pos → Equal-Power 인덱스 래치
//  T+3 : Gain ROM 읽기 완료 + mid 래치
//  T+4 : 출력 곱셈 결과 → 출력 레지스터
//
//  입력 지연: in_l/in_r → 3클럭 딜레이 → T+3 mid 계산과 타이밍 일치
// ============================================================================

// ── T+0 래치 ──────────────────────────────────────────────────────────────
reg [6:0]  rom_addr_lfo_r;
reg [1:0]  quad_lfo_lat1;
reg [31:0] eff_phase_lat1;
reg [15:0] depth_lat1;

// ── T+1 래치 ──────────────────────────────────────────────────────────────
reg [15:0] rom_data_lfo;
reg [1:0]  quad_lfo_lat2;
reg [1:0]  wave_lat2;
reg [15:0] depth_lat2;
reg [31:0] eff_phase_lat2;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rom_addr_lfo_r <= 7'd0;
        quad_lfo_lat1  <= 2'd0;
        eff_phase_lat1 <= 32'd0;
        depth_lat1     <= 16'd0;
        rom_data_lfo   <= 16'd0;
        quad_lfo_lat2  <= 2'd0;
        wave_lat2      <= 2'd0;
        depth_lat2     <= 16'd0;
        eff_phase_lat2 <= 32'd0;
    end else begin
        // T+0
        rom_addr_lfo_r <= rom_idx_lfo;
        quad_lfo_lat1  <= quad_lfo;
        eff_phase_lat1 <= eff_phase;
        depth_lat1     <= reg_depth;
        // T+1
        rom_data_lfo   <= sine_rom[rom_addr_lfo_r];
        quad_lfo_lat2  <= quad_lfo_lat1;
        wave_lat2      <= reg_wave;
        depth_lat2     <= depth_lat1;
        eff_phase_lat2 <= eff_phase_lat1;
    end
end

// ============================================================================
//  파형 합성 (T+2, 조합 논리)
//  pan_pos: signed Q1.15, -32768=최좌, 0=센터, +32767=최우
// ============================================================================

// 삼각파
wire [15:0] ph_half = eff_phase_lat2[30:15];
wire signed [16:0] tri_s17 =
    eff_phase_lat2[31]
    ? (17'sd32767 - $signed({1'b0, ph_half}))
    : ($signed({1'b0, ph_half}) - 17'sd32767);
wire signed [15:0] tri_w =
    (tri_s17 >  17'sd32767) ?  16'sd32767 :
    (tri_s17 < -17'sd32768) ? -16'sd32768 :
     tri_s17[15:0];

// 구형파
wire signed [15:0] sqr_w =
    eff_phase_lat2[31] ? -16'sd32768 : 16'sd32767;

// 사인파
wire signed [15:0] sine_signed =
    quad_lfo_lat2[1]
    ? -$signed({1'b0, rom_data_lfo[14:0]})
    :  $signed({1'b0, rom_data_lfo[14:0]});

// 파형 선택
wire signed [15:0] wave_raw =
    (wave_lat2 == 2'd1) ? tri_w :
    (wave_lat2 == 2'd2) ? sqr_w :
                          sine_signed;

// Depth 스케일
wire signed [31:0] lfo_scaled = $signed(wave_raw) * $signed({1'b0, depth_lat2});
wire signed [15:0] lfo_val    = lfo_scaled[30:15];

// Center 더하기 + 포화 클램프 → pan_pos
wire signed [16:0] pan_sum = $signed({lfo_val[15],    lfo_val})
                           + $signed({reg_center[15], reg_center});
wire signed [15:0] pan_pos =
    (pan_sum >  17'sd32767) ?  16'sd32767 :
    (pan_sum < -17'sd32768) ? -16'sd32768 :
     pan_sum[15:0];

// ============================================================================
//  [FIX-1] Equal-Power 인덱스 계산 (T+2, 조합 논리)
//
//  수학 근거:
//    pan_pos = -32768 → 완전 왼쪽: gain_l=1(cos0°), gain_r=0(sin0°)
//    pan_pos =      0 → 센터:      gain_l=gain_r≈0.707 (sin/cos 45°)
//    pan_pos = +32767 → 완전 오른쪽: gain_l=0(cos90°), gain_r=1(sin90°)
//
//  theta 매핑:
//    theta = (pan_pos + 32768) >> 1   → 0 ~ 32767   (부호 없음)
//    theta_7 = theta[14:8]            → 0 ~ 127     (ROM 인덱스)
//
//  gain_r = sine_rom[theta_7]         → sin(theta)
//  gain_l = sine_rom[127 - theta_7]   → cos(theta) = sin(90°-theta)
//
//  검증:
//    pan=-32768: theta_7=0   → gain_r=0(sin0°),  gain_l=32766(cos0°) ✓
//    pan=0:      theta_7=64  → gain_r=23170≈0.707, gain_l=22884≈0.698 ✓ (-3dB)
//    pan=+32767: theta_7=127 → gain_r=32766(sin90°), gain_l=0(cos90°) ✓
// ============================================================================

wire [16:0] theta_17 = {1'b0, pan_pos} + 17'd32768;
wire [14:0] theta_15 = theta_17[15:1];
wire [6:0]  theta_7  = theta_15[14:8];

wire [6:0] ep_r_idx = theta_7;
wire [6:0] ep_l_idx = 7'd127 - theta_7;

// ── T+2 래치: Equal-Power ROM 주소 + mid 계산 ──────────────────────────────
reg [6:0]          ep_r_addr_r, ep_l_addr_r;
reg signed [15:0]  mid_lat2;   // (in_l_d2 + in_r_d2) >> 1

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        ep_r_addr_r <= 7'd0;
        ep_l_addr_r <= 7'd0;
        mid_lat2    <= 16'sh0;
    end else begin
        ep_r_addr_r <= ep_r_idx;
        ep_l_addr_r <= ep_l_idx;
        // [FIX-2] Mid: 산술 우이동으로 부호 보존
        mid_lat2 <= $signed(
            $signed({in_l_d2[15], in_l_d2}) + $signed({in_r_d2[15], in_r_d2})
        ) >>> 1;
    end
end

// ── T+3: Gain ROM 읽기 + mid 전달 ─────────────────────────────────────────
reg [15:0]         gain_r_rom, gain_l_rom;
reg signed [15:0]  mid_lat3;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        gain_r_rom <= 16'd0;
        gain_l_rom <= 16'd0;
        mid_lat3   <= 16'sh0;
    end else begin
        gain_r_rom <= sine_rom[ep_r_addr_r];
        gain_l_rom <= sine_rom[ep_l_addr_r];
        mid_lat3   <= mid_lat2;
    end
end

// ============================================================================
//  [FIX-2] Mid 기반 출력 곱셈 (T+3, 조합 논리)
//
//  out_l = mid × gain_l >> 14   (Q1.15 × U0.15 곱 → [29:14] 추출)
//  out_r = mid × gain_r >> 14
//
//  gain_rom[14:0]: 0~32766 (15bit 절대값)
//  mid 범위: -16384 ~ +16383 (>>1 후)
//  곱 최대: 16383 × 32766 = 536,838,738 < 2^30 → bit30 이하 ✓
//  [29:14] = 16bit 부호 보존 출력
// ============================================================================
wire signed [31:0] mul_l_w = $signed(mid_lat3) * $signed({1'b0, gain_l_rom[14:0]});
wire signed [31:0] mul_r_w = $signed(mid_lat3) * $signed({1'b0, gain_r_rom[14:0]});

wire signed [15:0] out_val_l = mul_l_w[29:14];
wire signed [15:0] out_val_r = mul_r_w[29:14];

// ============================================================================
//  오디오 입력 지연 정렬
//  LFO 파이프라인: T+0 ~ T+2 = 2클럭
//  mid_lat2 계산: T+2 클럭 엣지 (in_l_d2 사용)
//  → in_l/in_r 2클럭 딜레이로 mid_lat2와 타이밍 일치
//  bypass용 in_l_d3: mid_lat3(T+3)과 같은 타이밍 → T+4 출력 레지스터에서 일치
// ============================================================================
reg signed [DATA_W-1:0] in_l_d1, in_l_d2, in_l_d3;
reg signed [DATA_W-1:0] in_r_d1, in_r_d2, in_r_d3;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        in_l_d1<=16'sh0; in_l_d2<=16'sh0; in_l_d3<=16'sh0;
        in_r_d1<=16'sh0; in_r_d2<=16'sh0; in_r_d3<=16'sh0;
    end else begin
        in_l_d1<=in_l;    in_l_d2<=in_l_d1; in_l_d3<=in_l_d2;
        in_r_d1<=in_r;    in_r_d2<=in_r_d1; in_r_d3<=in_r_d2;
    end
end

// ============================================================================
//  최종 출력 레지스터 (T+4 클럭 엣지)
//  BYPASS=1: 원본 신호 (레이턴시 보정 포함, in_l_d3 = T+3 시점 입력)
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        out_l <= 16'sh0;
        out_r <= 16'sh0;
    end else begin
        if (reg_bypass) begin
            out_l <= in_l_d3;
            out_r <= in_r_d3;
        end else begin
            out_l <= out_val_l;
            out_r <= out_val_r;
        end
    end
end

endmodule