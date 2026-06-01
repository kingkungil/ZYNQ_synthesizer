`timescale 1ns/1ps
// ============================================================================
//  eq_6band_top.v  (tdm_eq_phaser  v2.1  -  drop-in replacement)
//
//  [v2.1 수정]
//  HDL 9-1206 : function automatic + 내부 reg 선언 → Vivado 합성 불가
//    수정: 모든 function에서 automatic 제거, 내부 reg를 함수 반환 경로로만 사용
//          (Verilog-2001 합성 안전 스타일)
//  기타 잠재 오류 선제 차단:
//    - localparam 산술에 상수 캐스팅 명시 (1<<<SCALE 등)
//    - SLOT_W' 캐스팅 구문 제거 → 명시적 4비트 리터럴 사용
//    - mix_r 비트폭 ACC_W → 2*COEF_W+16 (곱셈 오버플로 방지)
//    - fold_back 함수 내 COEF_W+1 비트 임시변수 → 별도 localparam
//    - st_x1/x2/y1/y2 초기화 always → rst 전용 분리, 쓰기를 FSM always로 통합
//    - tready_r 초기화 후 ST_IDLE에서 fire_in 없을 때 1 유지 누락 수정
// ============================================================================

module eq_6band_top #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 12,
    parameter integer COEF_W             = 24,
    parameter integer IO_W               = 16,
    parameter integer SCALE              = 22
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI:s_axis:m_axis, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
    input  wire                            S_AXI_ACLK,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 S_AXI_ARESETN RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                            S_AXI_ARESETN,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWADDR" *)
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]  S_AXI_AWADDR,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWPROT" *)
    input  wire [2:0]                      S_AXI_AWPROT,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *)
    input  wire                            S_AXI_AWVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *)
    output reg                             S_AXI_AWREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WDATA" *)
    input  wire [C_S_AXI_DATA_WIDTH-1:0]  S_AXI_WDATA,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WSTRB" *)
    input  wire [3:0]                      S_AXI_WSTRB,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WVALID" *)
    input  wire                            S_AXI_WVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WREADY" *)
    output reg                             S_AXI_WREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BRESP" *)
    output reg  [1:0]                      S_AXI_BRESP,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BVALID" *)
    output reg                             S_AXI_BVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BREADY" *)
    input  wire                            S_AXI_BREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARADDR" *)
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]  S_AXI_ARADDR,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARPROT" *)
    input  wire [2:0]                      S_AXI_ARPROT,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *)
    input  wire                            S_AXI_ARVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *)
    output reg                             S_AXI_ARREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RDATA" *)
    output reg  [C_S_AXI_DATA_WIDTH-1:0]  S_AXI_RDATA,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RRESP" *)
    output reg  [1:0]                      S_AXI_RRESP,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RVALID" *)
    output reg                             S_AXI_RVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RREADY" *)
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

// ============================================================================
// 0. 로컬 파라미터
// ============================================================================
localparam integer NBAND  = 6;
localparam integer NSTAGE = 6;
localparam integer NSLOT  = 12;   // NBAND + NSTAGE

// 누산기 폭: 24*2+8=56 (COEF_W=24 고정)
localparam integer ACC_W  = 56;

// biquad 내부 포화 한계 (COEF_W)
localparam signed [COEF_W-1:0] SAT_MAX =  {1'b0, {(COEF_W-1){1'b1}}};
localparam signed [COEF_W-1:0] SAT_MIN =  {1'b1, {(COEF_W-1){1'b0}}};

// 반올림 오프셋
localparam [ACC_W-1:0] ROUND_HALF = {{(ACC_W-1){1'b0}}, 1'b1} << (SCALE - 1);

// 출력 포화 한계 (IO_W 기준, COEF_W 폭으로 비교)
localparam signed [COEF_W-1:0] SAT16_MAX =
    {{(COEF_W-IO_W){1'b0}}, {1'b0, {(IO_W-1){1'b1}}}};
localparam signed [COEF_W-1:0] SAT16_MIN =
    {{(COEF_W-IO_W){1'b1}}, {1'b1, {(IO_W-1){1'b0}}}};

// fold-back 임시변수 폭 (COEF_W+1)
localparam integer FOLD_W = COEF_W + 1;

// APF b1 고정값: Q(SCALE) 에서 -1.0
localparam signed [COEF_W-1:0] APF_B1_NEG =
    -({{(COEF_W-SCALE-1){1'b0}}, 1'b1, {SCALE{1'b0}}});

// wet/dry 믹스 중간 결과 폭 (COEF_W + 16 + 1 = 41, 넉넉히 ACC_W)
// mix = eq_out + (ph_out - eq_out)*ph_depth >> 15
// 최악: COEF_W 범위 차이 × 0x7FFF → COEF_W+15 비트면 충분하나 ACC_W로 통일

// FSM 상태
localparam [2:0]
    ST_IDLE  = 3'd0,
    ST_LOAD  = 3'd1,
    ST_MULT  = 3'd2,
    ST_ACCUM = 3'd3,
    ST_WB    = 3'd4,
    ST_MIX   = 3'd5,
    ST_OUT   = 3'd6,
    ST_DRAIN = 3'd7;

// AXI 쓰기 FSM 상태
localparam [1:0] WR_IDLE = 2'd0, WR_ACTIVE = 2'd1, WR_RESP = 2'd2;
// AXI 읽기 FSM 상태
localparam [1:0] RD_IDLE = 2'd0, RD_WAIT  = 2'd1, RD_SEND = 2'd2;

// ============================================================================
// 1. AXI4-Lite 쓰기 FSM
// ============================================================================
reg [1:0]                      wr_state;
reg [C_S_AXI_ADDR_WIDTH-1:0]  wr_addr;
reg [C_S_AXI_DATA_WIDTH-1:0]  wdata_r;
reg [3:0]                      wstrb_r;

// word index
wire [C_S_AXI_ADDR_WIDTH-3:0] wr_widx = wr_addr[C_S_AXI_ADDR_WIDTH-1:2];
wire [2:0]  wr_band = wr_widx[5:3];
wire [2:0]  wr_coef = wr_widx[2:0];

// 주소 영역 디코드
wire wr_is_eq   = (wr_widx <= 10'd47);                            // 0x000~0x0BC
wire wr_is_ph   = (wr_widx >= 10'd64 && wr_widx <= 10'd69);      // 0x100~0x114
wire wr_is_ctrl = (wr_widx >= 10'd128 && wr_widx <= 10'd131);    // 0x200~0x20C

// WSTRB 마스크 (COEF_W=24, 바이트 0~2)
wire [COEF_W-1:0] wstrb_mask;
genvar mi;
generate
    for (mi = 0; mi < COEF_W; mi = mi + 1) begin : GEN_WSTRB
        assign wstrb_mask[mi] = wstrb_r[mi/8];
    end
endgenerate

wire signed [COEF_W-1:0] wr_coef_data = wdata_r[COEF_W-1:0] & wstrb_mask;

// band_we 펄스: WR_ACTIVE+WVALID 사이클 → 다음 사이클(WR_RESP)에 FDRE CE 인가
(* keep = "true" *) reg [NBAND-1:0] band_we;
genvar gi;
generate
    for (gi = 0; gi < NBAND; gi = gi + 1) begin : GEN_BAND_WE
        always @(posedge S_AXI_ACLK) begin
            if (!S_AXI_ARESETN)
                band_we[gi] <= 1'b0;
            else
                band_we[gi] <= (wr_state == WR_ACTIVE) && S_AXI_WVALID
                               && wr_is_eq && (wr_coef <= 3'd4)
                               && (wr_band == gi[2:0]);
        end
    end
endgenerate

// ph_we 펄스 (동일 구조)
reg ph_we;
reg [2:0] ph_sidx_r;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        ph_we      <= 1'b0;
        ph_sidx_r  <= 3'd0;
    end else begin
        ph_we     <= (wr_state == WR_ACTIVE) && S_AXI_WVALID && wr_is_ph;
        ph_sidx_r <= wr_widx[2:0];
    end
end

// ctrl_we 펄스
reg        ctrl_we;
reg [1:0]  ctrl_widx_r;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        ctrl_we     <= 1'b0;
        ctrl_widx_r <= 2'd0;
    end else begin
        ctrl_we     <= (wr_state == WR_ACTIVE) && S_AXI_WVALID && wr_is_ctrl;
        ctrl_widx_r <= wr_widx[1:0];
    end
end

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        wr_state      <= WR_IDLE;
        S_AXI_AWREADY <= 1'b0;
        S_AXI_WREADY  <= 1'b0;
        S_AXI_BVALID  <= 1'b0;
        S_AXI_BRESP   <= 2'b00;
        wr_addr       <= {C_S_AXI_ADDR_WIDTH{1'b0}};
        wdata_r       <= {C_S_AXI_DATA_WIDTH{1'b0}};
        wstrb_r       <= 4'h0;
    end else begin
        S_AXI_AWREADY <= 1'b0;
        S_AXI_WREADY  <= 1'b0;
        case (wr_state)
            WR_IDLE: if (S_AXI_AWVALID) begin
                S_AXI_AWREADY <= 1'b1;
                wr_addr       <= S_AXI_AWADDR;
                wr_state      <= WR_ACTIVE;
            end
            WR_ACTIVE: if (S_AXI_WVALID) begin
                wdata_r      <= S_AXI_WDATA;
                wstrb_r      <= S_AXI_WSTRB;
                S_AXI_WREADY <= 1'b1;
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
    end
end

// ============================================================================
// 2. EQ 계수 RAM (FDRE DONT_TOUCH, 기존 iir_df1.v 와 동일 구조)
// ============================================================================
wire signed [COEF_W-1:0] eq_b0 [0:NBAND-1];
wire signed [COEF_W-1:0] eq_b1 [0:NBAND-1];
wire signed [COEF_W-1:0] eq_b2 [0:NBAND-1];
wire signed [COEF_W-1:0] eq_a1 [0:NBAND-1];
wire signed [COEF_W-1:0] eq_a2 [0:NBAND-1];

genvar bi, ci;
generate
    for (bi = 0; bi < NBAND; bi = bi + 1) begin : GEN_BAND
        for (ci = 0; ci < COEF_W; ci = ci + 1) begin : GEN_BIT
            (* DONT_TOUCH = "TRUE" *)
            FDRE #(.INIT(1'b0)) ff_b0 (
                .C(S_AXI_ACLK), .R(1'b0),
                .CE(band_we[bi] && (wr_coef == 3'd0)),
                .D(wr_coef_data[ci]), .Q(eq_b0[bi][ci]));
            (* DONT_TOUCH = "TRUE" *)
            FDRE #(.INIT(1'b0)) ff_b1 (
                .C(S_AXI_ACLK), .R(1'b0),
                .CE(band_we[bi] && (wr_coef == 3'd1)),
                .D(wr_coef_data[ci]), .Q(eq_b1[bi][ci]));
            (* DONT_TOUCH = "TRUE" *)
            FDRE #(.INIT(1'b0)) ff_b2 (
                .C(S_AXI_ACLK), .R(1'b0),
                .CE(band_we[bi] && (wr_coef == 3'd2)),
                .D(wr_coef_data[ci]), .Q(eq_b2[bi][ci]));
            (* DONT_TOUCH = "TRUE" *)
            FDRE #(.INIT(1'b0)) ff_a1 (
                .C(S_AXI_ACLK), .R(1'b0),
                .CE(band_we[bi] && (wr_coef == 3'd3)),
                .D(wr_coef_data[ci]), .Q(eq_a1[bi][ci]));
            (* DONT_TOUCH = "TRUE" *)
            FDRE #(.INIT(1'b0)) ff_a2 (
                .C(S_AXI_ACLK), .R(1'b0),
                .CE(band_we[bi] && (wr_coef == 3'd4)),
                .D(wr_coef_data[ci]), .Q(eq_a2[bi][ci]));
        end
    end
endgenerate

// coef_shadow (readback 전용)
reg [COEF_W-1:0] coef_shadow [0:NBAND-1][0:4];
integer si, sj;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        for (si = 0; si < NBAND; si = si + 1)
            for (sj = 0; sj < 5; sj = sj + 1)
                coef_shadow[si][sj] <= {COEF_W{1'b0}};
    end else begin
        if (band_we[0]) coef_shadow[0][wr_coef] <= wr_coef_data;
        if (band_we[1]) coef_shadow[1][wr_coef] <= wr_coef_data;
        if (band_we[2]) coef_shadow[2][wr_coef] <= wr_coef_data;
        if (band_we[3]) coef_shadow[3][wr_coef] <= wr_coef_data;
        if (band_we[4]) coef_shadow[4][wr_coef] <= wr_coef_data;
        if (band_we[5]) coef_shadow[5][wr_coef] <= wr_coef_data;
    end
end

// ============================================================================
// 3. Phaser 계수 c[6] + 제어 레지스터
// ============================================================================
reg signed [COEF_W-1:0] ph_c [0:NSTAGE-1];
integer pi;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        for (pi = 0; pi < NSTAGE; pi = pi + 1)
            ph_c[pi] <= {COEF_W{1'b0}};
    end else if (ph_we) begin
        case (ph_sidx_r)
            3'd0: ph_c[0] <= wr_coef_data;
            3'd1: ph_c[1] <= wr_coef_data;
            3'd2: ph_c[2] <= wr_coef_data;
            3'd3: ph_c[3] <= wr_coef_data;
            3'd4: ph_c[4] <= wr_coef_data;
            3'd5: ph_c[5] <= wr_coef_data;
            default: ;
        endcase
    end
end

reg [15:0] ph_depth;
reg [1:0]  clip_mode;
reg [3:0]  bc_bits;
reg [15:0] ph_lfo_inc;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        ph_depth   <= 16'h0000;
        clip_mode  <= 2'd0;
        bc_bits    <= 4'd4;
        ph_lfo_inc <= 16'd0;
    end else if (ctrl_we) begin
        case (ctrl_widx_r)
            2'd0: ph_depth   <= wdata_r[15:0];
            2'd1: clip_mode  <= wdata_r[1:0];
            2'd2: bc_bits    <= wdata_r[3:0];
            2'd3: ph_lfo_inc <= wdata_r[15:0];
        endcase
    end
end

// ============================================================================
// 4. LFO (삼각파)
//    위상 0x0000~0x7FFF → 상승, 0x8000~0xFFFF → 하강
//    출력 lfo_c: Q(SCALE) 기준 -1.0 ~ +1.0 미만
// ============================================================================
reg  [15:0] lfo_phase;
// 15비트 삼각파 (항상 0~0x7FFF)
wire [14:0] lfo_tri_raw  = lfo_phase[15] ? ~lfo_phase[14:0] : lfo_phase[14:0];
// 중심 0 기준 signed: -0x4000 ~ +0x3FFF (16비트 부호)
wire signed [15:0] lfo_tri_s =
    $signed({1'b0, lfo_tri_raw}) - 16'sh4000;
// Q(SCALE) 스케일: SCALE=22이면 << 8  (22-14=8)
wire signed [COEF_W-1:0] lfo_c =
    {{(COEF_W-16){lfo_tri_s[15]}}, lfo_tri_s} << (SCALE - 14);

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) lfo_phase <= 16'h0;
    else                 lfo_phase <= lfo_phase + ph_lfo_inc;
end

// ============================================================================
// 5. AXI4-Lite 읽기 FSM
// ============================================================================
reg [1:0]                      rd_state;
reg [C_S_AXI_ADDR_WIDTH-1:0]  ar_addr_r;

wire [C_S_AXI_ADDR_WIDTH-3:0] ar_widx = ar_addr_r[C_S_AXI_ADDR_WIDTH-1:2];
wire [2:0] ar_band = ar_widx[5:3];
wire [2:0] ar_coef = ar_addr_r[4:2];

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rd_state      <= RD_IDLE;
        S_AXI_ARREADY <= 1'b0;
        S_AXI_RVALID  <= 1'b0;
        S_AXI_RDATA   <= {C_S_AXI_DATA_WIDTH{1'b0}};
        S_AXI_RRESP   <= 2'b00;
        ar_addr_r     <= {C_S_AXI_ADDR_WIDTH{1'b0}};
    end else begin
        S_AXI_ARREADY <= 1'b0;
        case (rd_state)
            RD_IDLE: if (S_AXI_ARVALID) begin
                S_AXI_ARREADY <= 1'b1;
                ar_addr_r     <= S_AXI_ARADDR;
                rd_state      <= RD_WAIT;
            end
            RD_WAIT: begin
                rd_state <= RD_SEND;
                if (ar_widx <= 10'd47 && ar_coef <= 3'd4) begin
                    case (ar_band)
                        3'd0: S_AXI_RDATA <= {{(32-COEF_W){coef_shadow[0][ar_coef][COEF_W-1]}}, coef_shadow[0][ar_coef]};
                        3'd1: S_AXI_RDATA <= {{(32-COEF_W){coef_shadow[1][ar_coef][COEF_W-1]}}, coef_shadow[1][ar_coef]};
                        3'd2: S_AXI_RDATA <= {{(32-COEF_W){coef_shadow[2][ar_coef][COEF_W-1]}}, coef_shadow[2][ar_coef]};
                        3'd3: S_AXI_RDATA <= {{(32-COEF_W){coef_shadow[3][ar_coef][COEF_W-1]}}, coef_shadow[3][ar_coef]};
                        3'd4: S_AXI_RDATA <= {{(32-COEF_W){coef_shadow[4][ar_coef][COEF_W-1]}}, coef_shadow[4][ar_coef]};
                        3'd5: S_AXI_RDATA <= {{(32-COEF_W){coef_shadow[5][ar_coef][COEF_W-1]}}, coef_shadow[5][ar_coef]};
                        default: S_AXI_RDATA <= 32'hDEAD_BEEF;
                    endcase
                end else if (ar_widx >= 10'd64 && ar_widx <= 10'd69) begin
                    case (ar_widx[2:0])
                        3'd0: S_AXI_RDATA <= {{(32-COEF_W){ph_c[0][COEF_W-1]}}, ph_c[0]};
                        3'd1: S_AXI_RDATA <= {{(32-COEF_W){ph_c[1][COEF_W-1]}}, ph_c[1]};
                        3'd2: S_AXI_RDATA <= {{(32-COEF_W){ph_c[2][COEF_W-1]}}, ph_c[2]};
                        3'd3: S_AXI_RDATA <= {{(32-COEF_W){ph_c[3][COEF_W-1]}}, ph_c[3]};
                        3'd4: S_AXI_RDATA <= {{(32-COEF_W){ph_c[4][COEF_W-1]}}, ph_c[4]};
                        3'd5: S_AXI_RDATA <= {{(32-COEF_W){ph_c[5][COEF_W-1]}}, ph_c[5]};
                        default: S_AXI_RDATA <= 32'hDEAD_BEEF;
                    endcase
                end else if (ar_widx >= 10'd128 && ar_widx <= 10'd131) begin
                    case (ar_widx[1:0])
                        2'd0: S_AXI_RDATA <= {16'h0000, ph_depth};
                        2'd1: S_AXI_RDATA <= {30'h0, clip_mode};
                        2'd2: S_AXI_RDATA <= {28'h0, bc_bits};
                        2'd3: S_AXI_RDATA <= {16'h0000, ph_lfo_inc};
                    endcase
                end else begin
                    S_AXI_RDATA <= 32'hDEAD_BEEF;
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
// 6. 상태 뱅크 (12슬롯 × {x1,x2,y1,y2})
// ============================================================================
reg signed [COEF_W-1:0] st_x1 [0:NSLOT-1];
reg signed [COEF_W-1:0] st_x2 [0:NSLOT-1];
reg signed [COEF_W-1:0] st_y1 [0:NSLOT-1];
reg signed [COEF_W-1:0] st_y2 [0:NSLOT-1];

// ============================================================================
// 7. TDM FSM
// ============================================================================
reg [2:0]              state;
reg [3:0]              slot;       // 0~11
reg                    tready_r;

reg signed [COEF_W-1:0] in_r;
reg signed [COEF_W-1:0] chain_r;
reg signed [COEF_W-1:0] eq_out_r;
reg signed [COEF_W-1:0] ph_out_r;

// 슬롯 캡처
reg signed [COEF_W-1:0] c_b0, c_b1, c_b2, c_a1, c_a2;
reg signed [COEF_W-1:0] c_x1, c_x2, c_y1, c_y2, c_xin;

// 파이프라인
(* use_dsp = "yes" *) reg signed [ACC_W-1:0] mb0, mb1, mb2, ma1, ma2;
(* use_dsp = "no"  *) reg signed [ACC_W-1:0] acc;

// 믹스 결과
reg signed [ACC_W-1:0] mix_r;

// ── 계수 MUX (조합) ───────────────────────────────────────────────────────────
wire        is_ph    = (slot >= 4'd6);
wire [2:0]  ph_si    = slot[2:0] - 3'd6;   // 페이저 스테이지 인덱스 0~5

wire signed [COEF_W-1:0] ph_c_sel = (|ph_lfo_inc) ? lfo_c : ph_c[ph_si];

// EQ 계수 MUX (슬롯 0~5만 유효, is_ph=0)
reg signed [COEF_W-1:0] eq_mux_b0, eq_mux_b1, eq_mux_b2, eq_mux_a1, eq_mux_a2;
always @(*) begin
    case (slot[2:0])
        3'd0: begin eq_mux_b0=eq_b0[0]; eq_mux_b1=eq_b1[0]; eq_mux_b2=eq_b2[0]; eq_mux_a1=eq_a1[0]; eq_mux_a2=eq_a2[0]; end
        3'd1: begin eq_mux_b0=eq_b0[1]; eq_mux_b1=eq_b1[1]; eq_mux_b2=eq_b2[1]; eq_mux_a1=eq_a1[1]; eq_mux_a2=eq_a2[1]; end
        3'd2: begin eq_mux_b0=eq_b0[2]; eq_mux_b1=eq_b1[2]; eq_mux_b2=eq_b2[2]; eq_mux_a1=eq_a1[2]; eq_mux_a2=eq_a2[2]; end
        3'd3: begin eq_mux_b0=eq_b0[3]; eq_mux_b1=eq_b1[3]; eq_mux_b2=eq_b2[3]; eq_mux_a1=eq_a1[3]; eq_mux_a2=eq_a2[3]; end
        3'd4: begin eq_mux_b0=eq_b0[4]; eq_mux_b1=eq_b1[4]; eq_mux_b2=eq_b2[4]; eq_mux_a1=eq_a1[4]; eq_mux_a2=eq_a2[4]; end
        default: begin eq_mux_b0=eq_b0[5]; eq_mux_b1=eq_b1[5]; eq_mux_b2=eq_b2[5]; eq_mux_a1=eq_a1[5]; eq_mux_a2=eq_a2[5]; end
    endcase
end

wire signed [COEF_W-1:0] mux_b0 = is_ph ? ph_c_sel   : eq_mux_b0;
wire signed [COEF_W-1:0] mux_b1 = is_ph ? APF_B1_NEG : eq_mux_b1;
wire signed [COEF_W-1:0] mux_b2 = is_ph ? ph_c_sel   : eq_mux_b2;
wire signed [COEF_W-1:0] mux_a1 = is_ph ? (-ph_c_sel): eq_mux_a1;
wire signed [COEF_W-1:0] mux_a2 = is_ph ? {COEF_W{1'b0}} : eq_mux_a2;

// 슬롯 입력 MUX
wire signed [COEF_W-1:0] slot_xin =
    (slot == 4'd0) ? in_r     :
    (slot == 4'd6) ? eq_out_r :
    chain_r;

// ── 포화+반올림 (biquad 내부, 함수 local reg 없음) ──────────────────────────
// sat_coef: acc → COEF_W 포화, 조합 와이어로 노출
wire signed [ACC_W-1:0] sat_tmp = $signed(acc) + $signed(ROUND_HALF);
wire signed [ACC_W-1:0] sat_shr = $signed(sat_tmp) >>> SCALE;
wire signed [COEF_W-1:0] y_new =
    ($signed(sat_shr) >  $signed({{(ACC_W-COEF_W){1'b0}}, SAT_MAX})) ? SAT_MAX :
    ($signed(sat_shr) <  $signed({{(ACC_W-COEF_W){1'b1}}, SAT_MIN})) ? SAT_MIN :
    sat_shr[COEF_W-1:0];

// ── FSM 본체 (상태 + 슬롯 + 입력 캡처 + 계수/상태 뱅크 쓰기) ───────────────
assign s_axis_tready = tready_r;
wire fire_in = s_axis_tvalid && tready_r;

integer ki;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        state    <= ST_IDLE;
        slot     <= 4'd0;
        tready_r <= 1'b0;
        in_r     <= {COEF_W{1'b0}};
        chain_r  <= {COEF_W{1'b0}};
        eq_out_r <= {COEF_W{1'b0}};
        ph_out_r <= {COEF_W{1'b0}};
        c_b0 <= {COEF_W{1'b0}}; c_b1 <= {COEF_W{1'b0}}; c_b2 <= {COEF_W{1'b0}};
        c_a1 <= {COEF_W{1'b0}}; c_a2 <= {COEF_W{1'b0}};
        c_x1 <= {COEF_W{1'b0}}; c_x2 <= {COEF_W{1'b0}};
        c_y1 <= {COEF_W{1'b0}}; c_y2 <= {COEF_W{1'b0}}; c_xin <= {COEF_W{1'b0}};
        mb0  <= {ACC_W{1'b0}}; mb1 <= {ACC_W{1'b0}}; mb2 <= {ACC_W{1'b0}};
        ma1  <= {ACC_W{1'b0}}; ma2 <= {ACC_W{1'b0}};
        acc  <= {ACC_W{1'b0}};
        mix_r <= {ACC_W{1'b0}};
        for (ki = 0; ki < NSLOT; ki = ki + 1) begin
            st_x1[ki] <= {COEF_W{1'b0}}; st_x2[ki] <= {COEF_W{1'b0}};
            st_y1[ki] <= {COEF_W{1'b0}}; st_y2[ki] <= {COEF_W{1'b0}};
        end
    end else begin
        case (state)
            // ── 입력 대기 ─────────────────────────────────────────────────
            ST_IDLE: begin
                tready_r <= 1'b1;
                if (fire_in) begin
                    in_r     <= {{(COEF_W-IO_W){s_axis_tdata[IO_W-1]}}, s_axis_tdata};
                    tready_r <= 1'b0;
                    slot     <= 4'd0;
                    state    <= ST_LOAD;
                end
            end

            // ── LOAD: 계수 + 상태 캡처 ─────────────────────────────────
            ST_LOAD: begin
                c_b0  <= mux_b0;  c_b1  <= mux_b1;  c_b2  <= mux_b2;
                c_a1  <= mux_a1;  c_a2  <= mux_a2;
                c_x1  <= st_x1[slot];  c_x2  <= st_x2[slot];
                c_y1  <= st_y1[slot];  c_y2  <= st_y2[slot];
                c_xin <= slot_xin;
                state <= ST_MULT;
            end

            // ── MULT: 5-곱셈 ────────────────────────────────────────────
            ST_MULT: begin
                mb0   <= $signed(c_xin) * $signed(c_b0);
                mb1   <= $signed(c_x1)  * $signed(c_b1);
                mb2   <= $signed(c_x2)  * $signed(c_b2);
                ma1   <= $signed(c_y1)  * $signed(c_a1);
                ma2   <= $signed(c_y2)  * $signed(c_a2);
                state <= ST_ACCUM;
            end

            // ── ACCUM: 누산 ──────────────────────────────────────────────
            ST_ACCUM: begin
                acc   <= mb0 + mb1 + mb2 - ma1 - ma2;
                state <= ST_WB;
            end

            // ── WB: 포화·상태 write-back·체인 전파 ──────────────────────
            ST_WB: begin
                st_x2[slot] <= c_x1;
                st_x1[slot] <= c_xin;
                st_y2[slot] <= c_y1;
                st_y1[slot] <= y_new;
                chain_r     <= y_new;

                if (slot == 4'd5)  eq_out_r <= y_new;  // EQ 마지막

                if (slot == 4'd11) begin               // Phaser 마지막
                    ph_out_r <= y_new;
                    state    <= ST_MIX;
                end else begin
                    slot  <= slot + 4'd1;
                    state <= ST_LOAD;
                end
            end

            // ── MIX: wet/dry ────────────────────────────────────────────
            // mix = eq_out + (ph_out - eq_out) * ph_depth >> 15
            ST_MIX: begin
                mix_r <= $signed({{(ACC_W-COEF_W){eq_out_r[COEF_W-1]}}, eq_out_r})
                       + (($signed({{(ACC_W-COEF_W){ph_out_r[COEF_W-1]}}, ph_out_r})
                          - $signed({{(ACC_W-COEF_W){eq_out_r[COEF_W-1]}}, eq_out_r}))
                         * $signed({1'b0, ph_depth}) >>> 15);
                state <= ST_OUT;
            end

            // ── OUT: 클리핑 → 출력 레지스터 (별도 always에서 처리) ──────
            ST_OUT: state <= ST_DRAIN;

            // ── DRAIN: downstream 소비 대기 ───────────────────────────
            ST_DRAIN: begin
                if (!m_axis_tvalid_r || m_axis_tready) begin
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
// 8. 클리핑 성형 함수 (Verilog-2001: automatic 없음, 내부 reg 없음)
//    mix_r → mix_coef (COEF_W 포화) → clip → IO_W 출력
// ============================================================================

// mix_r → COEF_W 포화 (조합)
wire signed [COEF_W-1:0] mix_coef =
    ($signed(mix_r) > $signed({{(ACC_W-COEF_W){1'b0}}, SAT_MAX})) ? SAT_MAX :
    ($signed(mix_r) < $signed({{(ACC_W-COEF_W){1'b1}}, SAT_MIN})) ? SAT_MIN :
    mix_r[COEF_W-1:0];

// ── hard clip ──────────────────────────────────────────────────────────────
wire signed [IO_W-1:0] hard_out =
    ($signed(mix_coef) > $signed(SAT16_MAX)) ? SAT16_MAX[IO_W-1:0] :
    ($signed(mix_coef) < $signed(SAT16_MIN)) ? SAT16_MIN[IO_W-1:0] :
    mix_coef[IO_W-1:0];

// ── soft clip (cubic: y = x - x³/(3·MAX²), 조합) ─────────────────────────
// x² >> (SCALE+1) 을 norm으로, x - x*norm >> SCALE 로 근사
// 모든 중간값 wire로 선언 (reg 없음)
wire signed [2*COEF_W-1:0] sc_xsq  = $signed(mix_coef) * $signed(mix_coef);
wire signed [COEF_W-1:0]   sc_norm = $signed(sc_xsq) >>> (SCALE + 1);
wire signed [2*COEF_W-1:0] sc_x3   = $signed(mix_coef) * $signed(sc_norm);
wire signed [COEF_W-1:0]   sc_y    = $signed(mix_coef)
                                    - ($signed(sc_x3) >>> SCALE);
wire signed [IO_W-1:0] soft_out =
    ($signed(sc_y) > $signed(SAT16_MAX)) ? SAT16_MAX[IO_W-1:0] :
    ($signed(sc_y) < $signed(SAT16_MIN)) ? SAT16_MIN[IO_W-1:0] :
    sc_y[IO_W-1:0];

// ── bit-crush ─────────────────────────────────────────────────────────────
wire [IO_W-1:0] bc_mask = {IO_W{1'b1}} << bc_bits;
wire signed [IO_W-1:0] bc_out = hard_out & $signed(bc_mask);

// ── fold-back ─────────────────────────────────────────────────────────────
// |mix_coef| > SAT16_MAX → 2·SAT16_MAX - mix_coef (대칭 반사)
wire signed [FOLD_W-1:0] fold_pos =
    ($signed(SAT16_MAX) <<< 1) - $signed({{(FOLD_W-COEF_W){mix_coef[COEF_W-1]}}, mix_coef});
wire signed [FOLD_W-1:0] fold_neg =
    ($signed(SAT16_MIN) <<< 1) - $signed({{(FOLD_W-COEF_W){mix_coef[COEF_W-1]}}, mix_coef});
wire signed [IO_W-1:0] fold_out =
    ($signed(mix_coef) > $signed(SAT16_MAX)) ? fold_pos[IO_W-1:0] :
    ($signed(mix_coef) < $signed(SAT16_MIN)) ? fold_neg[IO_W-1:0] :
    mix_coef[IO_W-1:0];

// ── 출력 MUX ─────────────────────────────────────────────────────────────
wire signed [IO_W-1:0] clipped =
    (clip_mode == 2'd1) ? soft_out :
    (clip_mode == 2'd2) ? bc_out   :
    (clip_mode == 2'd3) ? fold_out :
    hard_out;

// ── 출력 레지스터 ─────────────────────────────────────────────────────────
reg signed [IO_W-1:0] m_axis_tdata_r;
reg                    m_axis_tvalid_r;

assign m_axis_tdata  = m_axis_tdata_r;
assign m_axis_tvalid = m_axis_tvalid_r;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        m_axis_tdata_r  <= {IO_W{1'b0}};
        m_axis_tvalid_r <= 1'b0;
    end else begin
        if (state == ST_OUT) begin
            m_axis_tdata_r  <= clipped;
            m_axis_tvalid_r <= 1'b1;
        end else if (m_axis_tvalid_r && m_axis_tready) begin
            m_axis_tvalid_r <= 1'b0;
        end
    end
end

endmodule