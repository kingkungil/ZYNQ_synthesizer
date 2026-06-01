`timescale 1ns/1ps
// ============================================================================
//  pre_eq_4band.v  -  4밴드 모노 Pre-EQ  v3.0  (TDM 엔진)
//
//  ── v3.0 변경 사항 (DSP 리소스 최적화) ────────────────────────────────────
//
//  v2.1: iir_df1_v2 인스턴스 4개 병렬 → DSP48E1 × 20
//  v3.0: 단일 biquad 엔진 시분할(TDM) → DSP48E1 × 5  (75% 절감)
//
//  [원리]
//    Fs=48kHz, FCLK=50MHz → 샘플 주기 ~1042 클럭.
//    단일 biquad 처리 = LOAD(1) + MULT(1) + ACCUM(1) + OUTPUT(1) = 4 사이클.
//    4밴드 직렬 = 4 × 4 + DONE(1) + DRAIN(≥1) = 최대 19 사이클.
//    사용률: 19 / 1042 ≈ 1.8%  → 타이밍 여유 충분.
//
//  [타이밍 안전성 설계]
//    1. ST_LOAD 에서 coef_shadow → r_bX/r_aX 워킹 레지스터 래치
//       → DSP 곱셈기 입력에 MUX 직접 걸리지 않음 (1-stage 격리)
//    2. ST_LOAD 에서 st_xN/yN → cur_xN/yN 워킹 레지스터 래치
//       → 밴드 저장소 읽기와 DSP 입력 사이에 1-stage 격리
//    3. DSP 곱셈 출력 전부 registered (mb0~ma2)
//    4. 포화/반올림은 acc(reg) → 조합 → y_new → ST_OUTPUT 에서 reg 저장
//       50MHz 에서 조합 경로 여유 충분 (XC7Z020 기준 ~3ns)
//
//  [호환성]
//    - 외부 인터페이스(AXI-Lite, AXI-Stream) 완전 동일
//    - 레지스터 맵 동일 (coef 0x00~0x4C, ctrl 0x100)
//    - 기능적 bit-exact 동일 (같은 biquad DF-I, 같은 Q22 포화)
//    - iir_df1_v2 모듈 인스턴스 불필요 (자체 내장)
//
//  [리소스 비교] (XC7Z020 기준)
//    항목       v2.1(4inst)   v3.0(TDM)   절감
//    DSP48E1    20            5           -75%
//    FF         ~960          ~480        -50%
//    LUT        ~200          ~250        +25% (MUX 증가)
//    레이턴시   5 clk         19 clk      +14 clk (0.38us @50MHz, 무시)
// ============================================================================

module pre_eq_4band #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 12,
    parameter integer COEF_W             = 24,
    parameter integer IO_W               = 16,
    parameter integer SHIFT              = 22,
    parameter integer NBAND              = 4
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI:s_axis:m_axis, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
    input  wire                            S_AXI_ACLK,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 S_AXI_ARESETN RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                            S_AXI_ARESETN,
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]  S_AXI_AWADDR,
    input  wire [2:0]                      S_AXI_AWPROT,
    input  wire                            S_AXI_AWVALID,
    output reg                             S_AXI_AWREADY,
    input  wire [C_S_AXI_DATA_WIDTH-1:0]  S_AXI_WDATA,
    input  wire [3:0]                      S_AXI_WSTRB,
    input  wire                            S_AXI_WVALID,
    output reg                             S_AXI_WREADY,
    output reg  [1:0]                      S_AXI_BRESP,
    output reg                             S_AXI_BVALID,
    input  wire                            S_AXI_BREADY,
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]  S_AXI_ARADDR,
    input  wire [2:0]                      S_AXI_ARPROT,
    input  wire                            S_AXI_ARVALID,
    output reg                             S_AXI_ARREADY,
    output reg  [C_S_AXI_DATA_WIDTH-1:0]  S_AXI_RDATA,
    output reg  [1:0]                      S_AXI_RRESP,
    output reg                             S_AXI_RVALID,
    input  wire                            S_AXI_RREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TVALID" *)
    input  wire                            s_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TDATA" *)
    input  wire signed [IO_W-1:0]          s_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TREADY" *)
    output wire                            s_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TVALID" *)
    output wire                            m_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TDATA" *)
    output wire signed [IO_W-1:0]          m_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TREADY" *)
    input  wire                            m_axis_tready
);

// ── 공통 파라미터 ──────────────────────────────────────────────────────────
localparam integer DATA_W     = COEF_W;           // 내부 연산 폭 = 24
localparam integer ACC_W      = 56;               // COEF_W*2 + 8
localparam [1:0]   LAST_BAND  = NBAND - 1;        // 2'd3

// ============================================================================
// 1. AXI4-Lite 쓰기 - AW/W 독립 래치 (v2.1과 동일)
// ============================================================================
reg [C_S_AXI_ADDR_WIDTH-1:0]  wr_addr_r;
reg [C_S_AXI_DATA_WIDTH-1:0]  wdata_r;
reg [3:0]                      wstrb_r;
reg                             aw_got, w_got;

wire [C_S_AXI_ADDR_WIDTH-3:0] wr_widx = wr_addr_r[C_S_AXI_ADDR_WIDTH-1:2];

wire wr_is_coef = (wr_widx < 10'd20);
wire wr_is_ctrl = (wr_widx == 10'd64);

wire [C_S_AXI_ADDR_WIDTH-3:0] wr_widx_m5  = wr_widx - 10'd5;
wire [C_S_AXI_ADDR_WIDTH-3:0] wr_widx_m10 = wr_widx - 10'd10;
wire [C_S_AXI_ADDR_WIDTH-3:0] wr_widx_m15 = wr_widx - 10'd15;

wire [2:0] wr_band_idx = (wr_widx < 10'd5)  ? 3'd0 :
                         (wr_widx < 10'd10) ? 3'd1 :
                         (wr_widx < 10'd15) ? 3'd2 :
                         (wr_widx < 10'd20) ? 3'd3 : 3'd0;

wire [2:0] wr_coef_idx = (wr_widx < 10'd5)  ? wr_widx[2:0] :
                         (wr_widx < 10'd10) ? wr_widx_m5[2:0]  :
                         (wr_widx < 10'd15) ? wr_widx_m10[2:0] :
                         (wr_widx < 10'd20) ? wr_widx_m15[2:0] : 3'd0;

wire [COEF_W-1:0] wstrb_mask;
genvar mi;
generate
    for (mi = 0; mi < COEF_W; mi = mi + 1) begin : GEN_WMASK
        assign wstrb_mask[mi] = wstrb_r[mi/8];
    end
endgenerate

wire [COEF_W-1:0] wr_coef_data = wdata_r[COEF_W-1:0] & wstrb_mask;

wire wr_do = aw_got && w_got && !S_AXI_BVALID;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        S_AXI_AWREADY <= 1'b0;
        S_AXI_WREADY  <= 1'b0;
        S_AXI_BVALID  <= 1'b0;
        S_AXI_BRESP   <= 2'b00;
        wr_addr_r     <= {C_S_AXI_ADDR_WIDTH{1'b0}};
        wdata_r       <= {C_S_AXI_DATA_WIDTH{1'b0}};
        wstrb_r       <= 4'h0;
        aw_got        <= 1'b0;
        w_got         <= 1'b0;
    end else begin
        S_AXI_AWREADY <= 1'b0;
        S_AXI_WREADY  <= 1'b0;
        if (S_AXI_AWVALID && !aw_got) begin
            S_AXI_AWREADY <= 1'b1;
            wr_addr_r     <= S_AXI_AWADDR;
            aw_got        <= 1'b1;
        end
        if (S_AXI_WVALID && !w_got) begin
            S_AXI_WREADY <= 1'b1;
            wdata_r      <= S_AXI_WDATA;
            wstrb_r      <= S_AXI_WSTRB;
            w_got        <= 1'b1;
        end
        if (wr_do) begin
            S_AXI_BVALID <= 1'b1;
            S_AXI_BRESP  <= 2'b00;
            aw_got       <= 1'b0;
            w_got        <= 1'b0;
        end
        if (S_AXI_BVALID && S_AXI_BREADY)
            S_AXI_BVALID <= 1'b0;
    end
end

wire coef_we_pulse = wr_do && wr_is_coef;
wire ctrl_we_pulse = wr_do && wr_is_ctrl;

wire band_we_0 = coef_we_pulse && (wr_band_idx == 3'd0);
wire band_we_1 = coef_we_pulse && (wr_band_idx == 3'd1);
wire band_we_2 = coef_we_pulse && (wr_band_idx == 3'd2);
wire band_we_3 = coef_we_pulse && (wr_band_idx == 3'd3);

// ============================================================================
// 2. 계수 섀도 레지스터
// ============================================================================
reg [COEF_W-1:0] coef_shadow [0:NBAND-1][0:4];

// [BUG FIX v3.1]
// coef_bypass=1 의 설계 의도:
//   C 드라이버가 "coef_bypass=1 → shadow 20-write → coef_bypass=0" 순서로
//   계수를 atomic 하게 갱신하는 동안, TDM 엔진의 ST_LOAD 에서 shadow 읽기를
//   freeze 하여 반쪽 계수가 1샘플 적용되는 것을 방지.
//
// 이전 코드의 버그:
//   wire coef_we_en = !coef_bypass;  →  coef_bypass=1 이면 shadow write 차단.
//   C 드라이버가 coef_bypass=1 을 세팅한 직후 쓰는 20개 계수가 전부 무시되어
//   coef_shadow 가 리셋값(0) 으로 유지됨.  결과: b0=b1=b2=a1=a2=0 → 출력 무음.
//
// 수정:
//   shadow write 는 coef_bypass 상태와 무관하게 항상 허용.
//   ST_LOAD 에서 coef_bypass=1 이면 r_bX/r_aX 래치를 freeze (하단 참조).

integer si, sj;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        for (si = 0; si < NBAND; si = si + 1)
            for (sj = 0; sj < 5; sj = sj + 1)
                coef_shadow[si][sj] <= {COEF_W{1'b0}};
    end else begin
        // coef_bypass 값에 무관하게 항상 write 허용
        if (band_we_0) coef_shadow[0][wr_coef_idx] <= wr_coef_data;
        if (band_we_1) coef_shadow[1][wr_coef_idx] <= wr_coef_data;
        if (band_we_2) coef_shadow[2][wr_coef_idx] <= wr_coef_data;
        if (band_we_3) coef_shadow[3][wr_coef_idx] <= wr_coef_data;
    end
end

// ============================================================================
// 3. 제어 레지스터
// ============================================================================
reg bypass;
reg coef_bypass;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        bypass      <= 1'b0;
        coef_bypass <= 1'b0;
    end else if (ctrl_we_pulse) begin
        bypass      <= wdata_r[0];
        coef_bypass <= wdata_r[1];
    end
end

// ============================================================================
// 4. AXI4-Lite 읽기 FSM (v2.1과 동일)
// ============================================================================
reg [1:0]                      rd_state;
reg [C_S_AXI_ADDR_WIDTH-1:0]  ar_addr_r;
wire [C_S_AXI_ADDR_WIDTH-3:0] ar_widx = ar_addr_r[C_S_AXI_ADDR_WIDTH-1:2];

wire [C_S_AXI_ADDR_WIDTH-3:0] ar_widx_m5  = ar_widx - 10'd5;
wire [C_S_AXI_ADDR_WIDTH-3:0] ar_widx_m10 = ar_widx - 10'd10;
wire [C_S_AXI_ADDR_WIDTH-3:0] ar_widx_m15 = ar_widx - 10'd15;

wire [2:0] ar_band_idx = (ar_widx < 10'd5)  ? 3'd0 :
                          (ar_widx < 10'd10) ? 3'd1 :
                          (ar_widx < 10'd15) ? 3'd2 :
                          (ar_widx < 10'd20) ? 3'd3 : 3'd0;
wire [2:0] ar_coef_idx = (ar_widx < 10'd5)  ? ar_widx[2:0] :
                          (ar_widx < 10'd10) ? ar_widx_m5[2:0]  :
                          (ar_widx < 10'd15) ? ar_widx_m10[2:0] :
                          (ar_widx < 10'd20) ? ar_widx_m15[2:0] : 3'd0;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rd_state      <= 2'd0;
        S_AXI_ARREADY <= 1'b0;
        S_AXI_RVALID  <= 1'b0;
        S_AXI_RDATA   <= {C_S_AXI_DATA_WIDTH{1'b0}};
        S_AXI_RRESP   <= 2'b00;
        ar_addr_r     <= {C_S_AXI_ADDR_WIDTH{1'b0}};
    end else begin
        S_AXI_ARREADY <= 1'b0;
        case (rd_state)
            2'd0: if (S_AXI_ARVALID) begin
                S_AXI_ARREADY <= 1'b1;
                ar_addr_r     <= S_AXI_ARADDR;
                rd_state      <= 2'd1;
            end
            2'd1: begin
                rd_state <= 2'd2;
                if (ar_widx < 10'd20) begin
                    case (ar_band_idx)
                        3'd0: S_AXI_RDATA <= {{(32-COEF_W){coef_shadow[0][ar_coef_idx][COEF_W-1]}}, coef_shadow[0][ar_coef_idx]};
                        3'd1: S_AXI_RDATA <= {{(32-COEF_W){coef_shadow[1][ar_coef_idx][COEF_W-1]}}, coef_shadow[1][ar_coef_idx]};
                        3'd2: S_AXI_RDATA <= {{(32-COEF_W){coef_shadow[2][ar_coef_idx][COEF_W-1]}}, coef_shadow[2][ar_coef_idx]};
                        3'd3: S_AXI_RDATA <= {{(32-COEF_W){coef_shadow[3][ar_coef_idx][COEF_W-1]}}, coef_shadow[3][ar_coef_idx]};
                        default: S_AXI_RDATA <= 32'hDEAD_BEEF;
                    endcase
                end else if (ar_widx == 10'd64) begin
                    S_AXI_RDATA <= {30'h0, coef_bypass, bypass};
                end else begin
                    S_AXI_RDATA <= 32'hDEAD_BEEF;
                end
            end
            2'd2: begin
                S_AXI_RVALID <= 1'b1;
                S_AXI_RRESP  <= 2'b00;
                if (S_AXI_RVALID && S_AXI_RREADY) begin
                    S_AXI_RVALID <= 1'b0;
                    rd_state     <= 2'd0;
                end
            end
            default: rd_state <= 2'd0;
        endcase
    end
end

// ============================================================================
// 5. TDM IIR 엔진  -  단일 biquad (5 DSP) 으로 4밴드 순차 처리
//
//    파이프라인 (밴드당 4사이클):
//      ST_LOAD   : coef_shadow + state → 워킹 레지스터 래치    (MUX 격리)
//      ST_MULT   : 5곱셈 병렬 (DSP48E1), x 딜레이 갱신
//      ST_ACCUM  : acc = Σ products
//      ST_OUTPUT : sat/round → y 딜레이 갱신 + chain 전파
//
//    전체 경로:
//      ST_IDLE(1) → [ST_LOAD→ST_MULT→ST_ACCUM→ST_OUTPUT]×4 → ST_DONE(1) → ST_DRAIN(≥1)
//      = 최대 19 사이클 / 샘플
//
//    타이밍 경로 (최장 조합):
//      acc(56b reg) → +ROUND_HALF → >>>SHIFT → 비교 → y_new(24b wire)
//      XC7Z020 @50MHz (20ns period): 약 3ns → 마진 17ns
// ============================================================================

// ── FSM 상태 ──────────────────────────────────────────────────────────────
localparam [2:0]
    ST_IDLE   = 3'd0,
    ST_LOAD   = 3'd1,
    ST_MULT   = 3'd2,
    ST_ACCUM  = 3'd3,
    ST_OUTPUT = 3'd4,
    ST_DONE   = 3'd5,
    ST_DRAIN  = 3'd6;

// ── biquad 포화/반올림 상수 (DATA_W 정밀도, 내부 biquad 용) ─────────────
localparam signed [DATA_W-1:0] SAT_MAX = {1'b0, {(DATA_W-1){1'b1}}};
localparam signed [DATA_W-1:0] SAT_MIN = {1'b1, {(DATA_W-1){1'b0}}};
localparam [ACC_W-1:0] ROUND_HALF =
    {{(ACC_W-1){1'b0}}, 1'b1} << (SHIFT - 1);

// ── 최종 출력 포화 상수 (DATA_W → IO_W 축소) ────────────────────────────
localparam signed [DATA_W-1:0] SAT16_MAX =
    {{(DATA_W-IO_W){1'b0}}, {1'b0, {(IO_W-1){1'b1}}}};
localparam signed [DATA_W-1:0] SAT16_MIN =
    {{(DATA_W-IO_W){1'b1}}, {1'b1, {(IO_W-1){1'b0}}}};

// ── 밴드별 상태 저장소 ──────────────────────────────────────────────────
//    4밴드 × 4워드 × 24bit = 384 bit → 레지스터로 구현 (BRAM 미사용)
reg signed [DATA_W-1:0] st_x1 [0:NBAND-1];
reg signed [DATA_W-1:0] st_x2 [0:NBAND-1];
reg signed [DATA_W-1:0] st_y1 [0:NBAND-1];
reg signed [DATA_W-1:0] st_y2 [0:NBAND-1];

// ── 워킹 레지스터 (ST_LOAD 에서 래치, ST_MULT 에서 DSP 입력) ──────────
//    이 1-stage 가 coef_shadow MUX 와 DSP 입력을 격리 → 타이밍 확보
reg signed [DATA_W-1:0] cur_in;
reg signed [DATA_W-1:0] cur_x1, cur_x2;
reg signed [DATA_W-1:0] cur_y1, cur_y2;

// ── 계수 워킹 레지스터 ─────────────────────────────────────────────────
//    coef_shadow[band_sel][k] → r_bX/r_aX  (ST_LOAD 에서 래치)
reg signed [COEF_W-1:0] r_b0, r_b1, r_b2, r_a1, r_a2;

// ── DSP 곱셈/누산 레지스터 ──────────────────────────────────────────────
(* use_dsp = "yes" *) reg signed [ACC_W-1:0] mb0, mb1, mb2, ma1, ma2;
(* use_dsp = "no"  *) reg signed [ACC_W-1:0] acc;

// ── 포화+반올림 조합 로직 (acc → y_new) ────────────────────────────────
wire signed [ACC_W-1:0] sat_tmp = acc + $signed(ROUND_HALF);
wire signed [ACC_W-1:0] sat_shr = $signed(sat_tmp) >>> SHIFT;

wire signed [ACC_W-1:0] sat_max_ext = {{(ACC_W-DATA_W){1'b0}}, SAT_MAX};
wire signed [ACC_W-1:0] sat_min_ext = {{(ACC_W-DATA_W){1'b1}}, SAT_MIN};

wire signed [DATA_W-1:0] y_new =
    ($signed(sat_shr) > sat_max_ext) ? SAT_MAX :
    ($signed(sat_shr) < sat_min_ext) ? SAT_MIN :
    sat_shr[DATA_W-1:0];

// ── FSM / 제어 신호 ────────────────────────────────────────────────────
reg [2:0] state;
reg [1:0] band_sel;
reg       tready_r;
reg signed [DATA_W-1:0] chain_data;     // 밴드 간 캐스케이드 전달

// ── 출력 레지스터 (단일 드라이버: 아래 always 블록) ────────────────────
reg signed [IO_W-1:0] m_tdata_r;
reg                    m_tvalid_r;

// ── 최종 출력 포화 (chain_data: DATA_W → IO_W) ─────────────────────────
wire signed [IO_W-1:0] sat_out =
    ($signed(chain_data) > $signed(SAT16_MAX)) ? SAT16_MAX[IO_W-1:0] :
    ($signed(chain_data) < $signed(SAT16_MIN)) ? SAT16_MIN[IO_W-1:0] :
    chain_data[IO_W-1:0];

// ── AXI-Stream 핸드셰이크 ─────────────────────────────────────────────
wire eq_out_ready_int = m_axis_tready || !m_tvalid_r;
wire signed [DATA_W-1:0] in_ext =
    {{(DATA_W-IO_W){s_axis_tdata[IO_W-1]}}, s_axis_tdata};

assign s_axis_tready = bypass ? m_axis_tready : tready_r;
assign m_axis_tdata  = bypass ? s_axis_tdata  : m_tdata_r;
assign m_axis_tvalid = bypass ? s_axis_tvalid : m_tvalid_r;

// ── TDM FSM + 데이터패스 ──────────────────────────────────────────────
integer ki;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        state      <= ST_IDLE;
        band_sel   <= 2'd0;
        tready_r   <= 1'b0;

        cur_in     <= {DATA_W{1'b0}};
        cur_x1     <= {DATA_W{1'b0}};
        cur_x2     <= {DATA_W{1'b0}};
        cur_y1     <= {DATA_W{1'b0}};
        cur_y2     <= {DATA_W{1'b0}};
        chain_data <= {DATA_W{1'b0}};

        r_b0 <= {COEF_W{1'b0}};
        r_b1 <= {COEF_W{1'b0}};
        r_b2 <= {COEF_W{1'b0}};
        r_a1 <= {COEF_W{1'b0}};
        r_a2 <= {COEF_W{1'b0}};

        mb0 <= {ACC_W{1'b0}};  mb1 <= {ACC_W{1'b0}};  mb2 <= {ACC_W{1'b0}};
        ma1 <= {ACC_W{1'b0}};  ma2 <= {ACC_W{1'b0}};
        acc <= {ACC_W{1'b0}};

        m_tdata_r  <= {IO_W{1'b0}};
        m_tvalid_r <= 1'b0;

        for (ki = 0; ki < NBAND; ki = ki + 1) begin
            st_x1[ki] <= {DATA_W{1'b0}};
            st_x2[ki] <= {DATA_W{1'b0}};
            st_y1[ki] <= {DATA_W{1'b0}};
            st_y2[ki] <= {DATA_W{1'b0}};
        end
    end else begin

        // ── m_tvalid 핸드셰이크 (default) ───────────────────────────
        // ST_DONE 에서 assert, 그 외 상태에서 downstream 소비 시 deassert.
        // 이 줄이 case 보다 먼저 평가되어, case 내부의 tvalid 할당이
        // 우선 적용됨 (Verilog last-assignment-wins 규칙).
        if (m_tvalid_r && m_axis_tready && state != ST_DONE)
            m_tvalid_r <= 1'b0;

        case (state)
        // ════════════════════════════════════════════════════════════
        // IDLE: 입력 대기, fire 시 band 0 시작
        // ════════════════════════════════════════════════════════════
        ST_IDLE: begin
            tready_r <= 1'b1;
            if (s_axis_tvalid && tready_r && !bypass) begin
                chain_data <= in_ext;       // 첫 밴드 입력 = 오디오 샘플
                band_sel   <= 2'd0;
                tready_r   <= 1'b0;
                state      <= ST_LOAD;
            end
        end

        // ════════════════════════════════════════════════════════════
        // LOAD: 계수 + 상태 → 워킹 레지스터 래치
        //       이 stage 가 coef_shadow MUX 를 DSP 입력에서 격리
        //
        // [BUG FIX v3.1]
        // coef_bypass=1 이면 r_bX/r_aX 를 이전 값으로 freeze.
        //   → C 드라이버가 shadow 를 갱신하는 도중 TDM 이 끼어들어도
        //     반쪽 계수가 DSP 에 들어가지 않음.
        // coef_bypass=0 이면 shadow 에서 정상 래치.
        // ════════════════════════════════════════════════════════════
        ST_LOAD: begin
            // 딜레이 라인 상태 래치 (coef_bypass 와 무관)
            cur_in <= chain_data;
            cur_x1 <= st_x1[band_sel];
            cur_x2 <= st_x2[band_sel];
            cur_y1 <= st_y1[band_sel];
            cur_y2 <= st_y2[band_sel];

            // 계수 래치 - coef_bypass=0 일 때만 shadow 에서 갱신
            //            coef_bypass=1 이면 r_bX/r_aX 이전 값 유지 (freeze)
            if (!coef_bypass) begin
                case (band_sel)
                    2'd0: begin
                        r_b0 <= coef_shadow[0][0];
                        r_b1 <= coef_shadow[0][1];
                        r_b2 <= coef_shadow[0][2];
                        r_a1 <= coef_shadow[0][3];
                        r_a2 <= coef_shadow[0][4];
                    end
                    2'd1: begin
                        r_b0 <= coef_shadow[1][0];
                        r_b1 <= coef_shadow[1][1];
                        r_b2 <= coef_shadow[1][2];
                        r_a1 <= coef_shadow[1][3];
                        r_a2 <= coef_shadow[1][4];
                    end
                    2'd2: begin
                        r_b0 <= coef_shadow[2][0];
                        r_b1 <= coef_shadow[2][1];
                        r_b2 <= coef_shadow[2][2];
                        r_a1 <= coef_shadow[2][3];
                        r_a2 <= coef_shadow[2][4];
                    end
                    2'd3: begin
                        r_b0 <= coef_shadow[3][0];
                        r_b1 <= coef_shadow[3][1];
                        r_b2 <= coef_shadow[3][2];
                        r_a1 <= coef_shadow[3][3];
                        r_a2 <= coef_shadow[3][4];
                    end
                endcase
            end
            // coef_bypass=1: r_b0~r_a2 는 이전 클럭 값 유지 (implicit freeze)

            state <= ST_MULT;
        end

        // ════════════════════════════════════════════════════════════
        // MULT: 5× DSP48E1 곱셈 병렬 + x 딜레이 갱신
        //       모든 입력(cur_*, r_*)이 registered → DSP 추론 최적
        // ════════════════════════════════════════════════════════════
        ST_MULT: begin
            mb0 <= $signed(cur_in) * $signed(r_b0);
            mb1 <= $signed(cur_x1) * $signed(r_b1);
            mb2 <= $signed(cur_x2) * $signed(r_b2);
            ma1 <= $signed(cur_y1) * $signed(r_a1);
            ma2 <= $signed(cur_y2) * $signed(r_a2);

            // x 딜레이 라인 갱신
            st_x2[band_sel] <= cur_x1;
            st_x1[band_sel] <= cur_in;

            state <= ST_ACCUM;
        end

        // ════════════════════════════════════════════════════════════
        // ACCUM: 5개 곱셈 결과 누산
        // ════════════════════════════════════════════════════════════
        ST_ACCUM: begin
            acc <= mb0 + mb1 + mb2 - ma1 - ma2;
            state <= ST_OUTPUT;
        end

        // ════════════════════════════════════════════════════════════
        // OUTPUT: sat/round → y 딜레이 갱신 + chain 전파
        //         마지막 밴드(3) 이면 ST_DONE, 아니면 다음 밴드
        // ════════════════════════════════════════════════════════════
        ST_OUTPUT: begin
            // y 딜레이 갱신
            st_y2[band_sel] <= cur_y1;
            st_y1[band_sel] <= y_new;

            // chain 전파 (다음 밴드의 입력 or 최종 결과)
            chain_data <= y_new;

            if (band_sel == LAST_BAND) begin
                state <= ST_DONE;
            end else begin
                band_sel <= band_sel + 2'd1;
                state    <= ST_LOAD;
            end
        end

        // ════════════════════════════════════════════════════════════
        // DONE: 최종 결과를 출력 레지스터에 적재
        //       downstream busy → 여기서 back-pressure 대기
        // ════════════════════════════════════════════════════════════
        ST_DONE: begin
            if (eq_out_ready_int) begin
                m_tdata_r  <= sat_out;
                m_tvalid_r <= 1'b1;
                state      <= ST_DRAIN;
            end
        end

        // ════════════════════════════════════════════════════════════
        // DRAIN: downstream 소비 완료 대기 → IDLE 복귀
        // ════════════════════════════════════════════════════════════
        ST_DRAIN: begin
            if (m_tvalid_r && m_axis_tready) begin
                m_tvalid_r <= 1'b0;
                tready_r   <= 1'b1;
                state      <= ST_IDLE;
            end else if (!m_tvalid_r) begin
                // default deassert 에 의해 이미 소비된 경우
                tready_r <= 1'b1;
                state    <= ST_IDLE;
            end
        end

        default: begin
            state    <= ST_IDLE;
            tready_r <= 1'b0;
        end
        endcase

    end
end

endmodule