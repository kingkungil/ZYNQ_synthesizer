`timescale 1ns / 1ps
// ============================================================================
//  input_frontend.v  Rev.1
//
//  역할: CLEAN ONLY - 4가지만 수행
//    1. DC 제거      (1-pole HPF, fc≈5Hz@50MHz)
//    2. 입력 gain normalization  (AXI reg_gain, Q2.14)
//    3. Anti-alias soft clamp    (가벼운 hard clip, -6dBFS 이하 피크 보호)
//    4. Stereo/latency alignment placeholder (모노 통과, 나중에 확장)
//
//  금지 사항: compression / saturation / envelope / harmonic / LFO 전부 없음
//
//  AXI 레지스터 (베이스 + 오프셋)
//    0x00  reg_gain      Q2.14  입력 게인  (기본 0x4000 = 1.0)
//    0x04  reg_hpf_en    [0]    DC block enable (기본 1)
//    0x08  reg_clip_lvl  Q1.15  Anti-alias hard clip 레벨 (기본 0x7FFF = full)
//
//  포맷: AXI-Stream 16bit 입력 → 24bit 내부 → 16bit 출력
// ============================================================================

module input_frontend #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 5   // 3 reg × 4 = 0x0C → 5bit
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI:s_axis:m_axis, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
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

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TVALID" *)
    input  wire                              s_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TDATA" *)
    input  wire signed [15:0]                s_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TREADY" *)
    output wire                              s_axis_tready,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TVALID" *)
    output reg                               m_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TDATA" *)
    output reg  signed [15:0]                m_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TREADY" *)
    input  wire                              m_axis_tready
);

// ============================================================================
//  내부 파라미터
// ============================================================================
localparam DATA_W = 24;

// 24bit 포화 상수
localparam signed [DATA_W-1:0] SAT16_MAX = 24'sh007FFF;
localparam signed [DATA_W-1:0] SAT16_MIN = 24'shFF8000;
localparam signed [47:0] SAT24_MAX_48 = 48'sh0000000007FFFFF;
localparam signed [47:0] SAT24_MIN_48 = 48'shFFFFFFFF800000;

// DC block HPF 계수: α ≈ 1 - 2π·fc/fs = 1 - 2π·5/50000000
// Q1.23 표현: 0x7FFFED ≈ 0.9999994  (실질적으로 거의 1)
// 실용 근사: α = 1 - 2^-20 → 0x7FFFFE
localparam [23:0] HPF_ALPHA = 24'h7FFFFE;  // Q1.23

// ============================================================================
//  AXI-Lite 레지스터
// ============================================================================
reg [15:0] reg_gain;      // 0x00  Q2.14, 기본 0x4000 = 1.0
reg        reg_hpf_en;    // 0x04  [0]
reg [15:0] reg_clip_lvl;  // 0x08  Q1.15 hard clip 레벨, 기본 0x7FFF

// ============================================================================
//  AXI Write FSM
// ============================================================================
localparam [1:0] WR_IDLE = 2'd0, WR_ACTIVE = 2'd1, WR_RESP = 2'd2;
localparam [1:0] RD_IDLE = 2'd0, RD_SEND   = 2'd1;

reg [1:0]                     wr_state;
reg [C_S_AXI_ADDR_WIDTH-1:0] wr_addr;
reg [C_S_AXI_DATA_WIDTH-1:0] wdata_r;
reg                           wr_en;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        wr_state      <= WR_IDLE;
        S_AXI_AWREADY <= 1'b0;
        S_AXI_WREADY  <= 1'b0;
        S_AXI_BVALID  <= 1'b0;
        S_AXI_BRESP   <= 2'b00;
        wr_addr       <= {C_S_AXI_ADDR_WIDTH{1'b0}};
        wdata_r       <= {C_S_AXI_DATA_WIDTH{1'b0}};
        wr_en         <= 1'b0;
        reg_gain      <= 16'h4000;  // 1.0
        reg_hpf_en    <= 1'b1;
        reg_clip_lvl  <= 16'h7FFF;  // full range
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
            case (wr_addr[4:2])
                3'h0: reg_gain     <= wdata_r[15:0];
                3'h1: reg_hpf_en   <= wdata_r[0];
                3'h2: reg_clip_lvl <= wdata_r[15:0];
                default: ;
            endcase
        end
    end
end

// ── AXI Read ─────────────────────────────────────────────────────────────────
reg [1:0] rd_state;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rd_state      <= RD_IDLE;
        S_AXI_ARREADY <= 1'b0;
        S_AXI_RVALID  <= 1'b0;
        S_AXI_RDATA   <= {C_S_AXI_DATA_WIDTH{1'b0}};
        S_AXI_RRESP   <= 2'b00;
    end else begin
        S_AXI_ARREADY <= 1'b0;
        case (rd_state)
            RD_IDLE: begin
                if (S_AXI_ARVALID) begin
                    S_AXI_ARREADY <= 1'b1;
                    case (S_AXI_ARADDR[4:2])
                        3'h0: S_AXI_RDATA <= {{16{1'b0}}, reg_gain};
                        3'h1: S_AXI_RDATA <= {{31{1'b0}}, reg_hpf_en};
                        3'h2: S_AXI_RDATA <= {{16{1'b0}}, reg_clip_lvl};
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
//  DC Block HPF 상태 (1-pole IIR)
//  HPF: y[n] = α·(y[n-1] + x[n] - x[n-1])
//  상태: hpf_xz1 (이전 입력), hpf_yz1 (이전 출력) — 24bit Q
// ============================================================================
reg signed [DATA_W-1:0] hpf_xz1;
reg signed [DATA_W-1:0] hpf_yz1;

// ============================================================================
//  sat24 함수
// ============================================================================
function signed [DATA_W-1:0] sat24;
    input signed [47:0] v;
    begin
        if      (v > SAT24_MAX_48) sat24 = 24'sh007FFF;
        else if (v < SAT24_MIN_48) sat24 = 24'shFF8000;
        else                       sat24 = v[DATA_W-1:0];
    end
endfunction

// ============================================================================
//  스트리밍 FSM  (5상태: IDLE→GAIN→HPF→CLIP→DRAIN)
// ============================================================================
localparam [2:0]
    ST_IDLE  = 3'd0,
    ST_GAIN  = 3'd1,   // 입력 게인 normalization
    ST_HPF   = 3'd2,   // DC block HPF
    ST_CLIP  = 3'd3,   // anti-alias hard clip
    ST_OUT   = 3'd4,
    ST_DRAIN = 3'd5;

reg [2:0] state;
reg       tready_r;

assign s_axis_tready = tready_r;
wire fire_in = s_axis_tvalid && tready_r;

// 파이프라인 레지스터
reg signed [DATA_W-1:0] x_hold;   // 부호확장 입력
reg signed [DATA_W-1:0] x_gain;   // gain 적용 후
reg signed [DATA_W-1:0] x_hpf;    // DC block 후
reg signed [DATA_W-1:0] x_clip;   // clip 후 출력용

// 임시
reg signed [47:0] tmp_gain_mul;
reg signed [47:0] tmp_delta;
reg signed [47:0] tmp_hpf_new;

// clip 레벨 (24bit로 확장)
wire signed [DATA_W-1:0] clip_pos =
    {{(DATA_W-16){1'b0}}, reg_clip_lvl};
wire signed [DATA_W-1:0] clip_neg =
    {{(DATA_W-16){reg_clip_lvl[15]}}, ~reg_clip_lvl + 16'h1};

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        state    <= ST_IDLE;
        tready_r <= 1'b0;
        x_hold   <= {DATA_W{1'b0}};
        x_gain   <= {DATA_W{1'b0}};
        x_hpf    <= {DATA_W{1'b0}};
        x_clip   <= {DATA_W{1'b0}};
        hpf_xz1  <= {DATA_W{1'b0}};
        hpf_yz1  <= {DATA_W{1'b0}};
    end else begin
        case (state)

            ST_IDLE: begin
                tready_r <= 1'b1;
                if (fire_in) begin
                    // 16bit → 24bit 부호확장 (상위 8bit 부호 복사, 하위 0패딩)
                    x_hold   <= {{8{s_axis_tdata[15]}}, s_axis_tdata};
                    tready_r <= 1'b0;
                    state    <= ST_GAIN;
                end
            end

            // ── GAIN normalization ─────────────────────────────────────
            // x_gain = sat24(x_hold × reg_gain >> 14)
            // reg_gain Q2.14: 0x4000=1.0, 0x8000=2.0, 0x2000=0.5
            ST_GAIN: begin
                tmp_gain_mul = $signed({{(48-DATA_W){x_hold[DATA_W-1]}}, x_hold})
                               * $signed({16'h0, reg_gain});
                x_gain <= sat24(tmp_gain_mul >>> 14);
                state  <= ST_HPF;
            end

            // ── DC Block HPF ───────────────────────────────────────────
            // y[n] = α · (y[n-1] + x[n] - x[n-1])
            // α = HPF_ALPHA Q1.23 (≈1 - 2^-20)
            // en=0이면 패스스루
            ST_HPF: begin
                if (reg_hpf_en) begin
                    tmp_delta   = $signed({{(48-DATA_W){x_gain[DATA_W-1]}}, x_gain})
                                - $signed({{(48-DATA_W){hpf_xz1[DATA_W-1]}}, hpf_xz1})
                                + $signed({{(48-DATA_W){hpf_yz1[DATA_W-1]}}, hpf_yz1});
                    tmp_hpf_new = $signed({24'h0, HPF_ALPHA}) * tmp_delta;
                    hpf_xz1 <= x_gain;
                    hpf_yz1 <= sat24(tmp_hpf_new >>> 23);
                    x_hpf   <= sat24(tmp_hpf_new >>> 23);
                end else begin
                    x_hpf <= x_gain;
                end
                state <= ST_CLIP;
            end

            // ── Anti-alias Hard Clip ────────────────────────────────────
            // reg_clip_lvl 이상의 피크를 잘라내는 단순 hard clip
            // 목적: YM2203 글리치성 출력 스파이크 제거용
            // 비선형 왜곡 최소화를 위해 레벨은 기본 0x7FFF (full) 유지
            ST_CLIP: begin
                if ($signed(x_hpf) > $signed(clip_pos))
                    x_clip <= clip_pos;
                else if ($signed(x_hpf) < $signed(clip_neg))
                    x_clip <= clip_neg;
                else
                    x_clip <= x_hpf;
                state <= ST_OUT;
            end

            ST_OUT: begin
                state <= ST_DRAIN;
            end

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
//  출력 블록
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        m_axis_tvalid <= 1'b0;
        m_axis_tdata  <= 16'h0;
    end else begin
        if (state == ST_OUT) begin
            // 24bit → 16bit 포화 클리핑
            if      ($signed(x_clip) > $signed(SAT16_MAX))
                m_axis_tdata <= SAT16_MAX[15:0];
            else if ($signed(x_clip) < $signed(SAT16_MIN))
                m_axis_tdata <= SAT16_MIN[15:0];
            else
                m_axis_tdata <= x_clip[15:0];
            m_axis_tvalid <= 1'b1;
        end else if (m_axis_tvalid && m_axis_tready) begin
            m_axis_tvalid <= 1'b0;
        end
    end
end

endmodule
