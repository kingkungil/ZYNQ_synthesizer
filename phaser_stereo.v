`timescale 1ns/1ps
// ============================================================================
//  phaser_stereo.v  -  4-stage Allpass Phaser  v2.1
//  HDL 9-1206 수정:
//    - wire 배열 → 개별 named wire (cd0~cd4, cv0~cv4, cr0~cr4)
//    - generate 제거 → 명시적 4개 APF 인스턴스
//    - (wr_widx - N)[2:0] → 중간 wire 경유
//    - LFO/DSP/AXI 분리 유지
// ============================================================================

module phaser_stereo #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 12,
    parameter integer COEF_W             = 24,
    parameter integer IO_W               = 16,
    parameter integer SHIFT              = 22,
    parameter integer NSTAGE             = 4,
    parameter integer IIR_LATENCY        = 1
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
    output wire [2*IO_W-1:0]               m_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TREADY" *)
    input  wire                            m_axis_tready
);

localparam integer DATA_W    = COEF_W;
localparam integer ACC_W     = 56;
localparam integer MIX_W     = DATA_W + 16 + 1;
localparam integer DLY_DEPTH = NSTAGE * IIR_LATENCY + 2;

// APF b1 = -1 in Q(SHIFT): -(2^SHIFT) in COEF_W bits
localparam signed [COEF_W-1:0] APF_B1_NEG =
    -($signed({{(COEF_W-SHIFT-1){1'b0}}, 1'b1, {SHIFT{1'b0}}}));

// ============================================================================
// 1. LFO - 삼각파
// ============================================================================
reg  [15:0] lfo_phase;
reg  [15:0] lfo_rate;
reg         lfo_en;

wire [14:0] lfo_tri_raw = lfo_phase[15] ? ~lfo_phase[14:0] : lfo_phase[14:0];
wire signed [15:0] lfo_tri_s = $signed({1'b0, lfo_tri_raw}) - 16'sh4000;
wire signed [COEF_W-1:0] lfo_c =
    {{(COEF_W-16){lfo_tri_s[15]}}, lfo_tri_s} << (SHIFT - 14);

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) lfo_phase <= 16'h0;
    else if (lfo_en)    lfo_phase <= lfo_phase + lfo_rate;
end

// ============================================================================
// 2. AXI4-Lite 쓰기 FSM
// ============================================================================
reg [C_S_AXI_ADDR_WIDTH-1:0]  wr_addr_r;
reg [C_S_AXI_DATA_WIDTH-1:0]  wdata_r;
reg [3:0]                      wstrb_r;
reg                             aw_got, w_got;

wire [C_S_AXI_ADDR_WIDTH-3:0] wr_widx = wr_addr_r[C_S_AXI_ADDR_WIDTH-1:2];

wire wr_is_coef     = (wr_widx < 10'd20);
wire wr_is_ctrl_ph  = (wr_widx == 10'd64);
wire wr_is_ctrl_rt  = (wr_widx == 10'd65);
wire wr_is_ctrl_shp = (wr_widx == 10'd66);
wire wr_is_ctrl_mn  = (wr_widx == 10'd67);

wire [C_S_AXI_ADDR_WIDTH-3:0] wr_w_m5  = wr_widx - 10'd5;
wire [C_S_AXI_ADDR_WIDTH-3:0] wr_w_m10 = wr_widx - 10'd10;
wire [C_S_AXI_ADDR_WIDTH-3:0] wr_w_m15 = wr_widx - 10'd15;

wire [1:0] wr_stage_idx = (wr_widx < 10'd5)  ? 2'd0 :
                          (wr_widx < 10'd10) ? 2'd1 :
                          (wr_widx < 10'd15) ? 2'd2 : 2'd3;
wire [2:0] wr_coef_idx  = (wr_widx < 10'd5)  ? wr_widx[2:0]    :
                          (wr_widx < 10'd10) ? wr_w_m5[2:0]   :
                          (wr_widx < 10'd15) ? wr_w_m10[2:0]  :
                          (wr_widx < 10'd20) ? wr_w_m15[2:0]  : 3'd0;

// WSTRB 마스크
wire [COEF_W-1:0] wstrb_mask;
genvar mi;
generate
    for (mi = 0; mi < COEF_W; mi = mi + 1) begin : GEN_WMASK
        assign wstrb_mask[mi] = wstrb_r[mi/8];
    end
endgenerate
wire signed [COEF_W-1:0] wr_coef_data = wdata_r[COEF_W-1:0] & wstrb_mask;

wire wr_do = aw_got && w_got && !S_AXI_BVALID;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        S_AXI_AWREADY <= 1'b0; S_AXI_WREADY <= 1'b0;
        S_AXI_BVALID  <= 1'b0; S_AXI_BRESP  <= 2'b00;
        wr_addr_r <= {C_S_AXI_ADDR_WIDTH{1'b0}};
        wdata_r   <= {C_S_AXI_DATA_WIDTH{1'b0}};
        wstrb_r <= 4'h0; aw_got <= 1'b0; w_got <= 1'b0;
    end else begin
        S_AXI_AWREADY <= 1'b0; S_AXI_WREADY <= 1'b0;
        if (S_AXI_AWVALID && !aw_got) begin
            S_AXI_AWREADY <= 1'b1; wr_addr_r <= S_AXI_AWADDR; aw_got <= 1'b1;
        end
        if (S_AXI_WVALID && !w_got) begin
            S_AXI_WREADY <= 1'b1; wdata_r <= S_AXI_WDATA; wstrb_r <= S_AXI_WSTRB; w_got <= 1'b1;
        end
        if (wr_do) begin
            S_AXI_BVALID <= 1'b1; S_AXI_BRESP <= 2'b00; aw_got <= 1'b0; w_got <= 1'b0;
        end
        if (S_AXI_BVALID && S_AXI_BREADY) S_AXI_BVALID <= 1'b0;
    end
end

// ============================================================================
// 3. 제어 레지스터 + 정적 계수
// ============================================================================
reg signed [COEF_W-1:0] apf_coef_s0 [0:4];
reg signed [COEF_W-1:0] apf_coef_s1 [0:4];
reg signed [COEF_W-1:0] apf_coef_s2 [0:4];
reg signed [COEF_W-1:0] apf_coef_s3 [0:4];

reg [15:0]  ph_depth;
reg [1:0]   lfo_shape;
reg         bypass;
reg         stereo_invert;

integer pi;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        ph_depth <= 16'h8000; lfo_rate <= 16'd0;
        lfo_en <= 1'b0; lfo_shape <= 2'd0;
        bypass <= 1'b0; stereo_invert <= 1'b1;
        for (pi = 0; pi < 5; pi = pi + 1) begin
            apf_coef_s0[pi] <= {COEF_W{1'b0}};
            apf_coef_s1[pi] <= {COEF_W{1'b0}};
            apf_coef_s2[pi] <= {COEF_W{1'b0}};
            apf_coef_s3[pi] <= {COEF_W{1'b0}};
        end
    end else if (wr_do) begin
        if (wr_is_coef) begin
            case (wr_stage_idx)
                2'd0: apf_coef_s0[wr_coef_idx] <= wr_coef_data;
                2'd1: apf_coef_s1[wr_coef_idx] <= wr_coef_data;
                2'd2: apf_coef_s2[wr_coef_idx] <= wr_coef_data;
                2'd3: apf_coef_s3[wr_coef_idx] <= wr_coef_data;
                default: ;
            endcase
        end
        if (wr_is_ctrl_ph)  ph_depth      <= wdata_r[15:0];
        if (wr_is_ctrl_rt)  lfo_rate      <= wdata_r[15:0];
        if (wr_is_ctrl_shp) lfo_shape     <= wdata_r[1:0];
        if (wr_is_ctrl_mn)  begin
            bypass        <= wdata_r[0];
            lfo_en        <= wdata_r[1];
            stereo_invert <= wdata_r[2];
        end
    end
end

// ============================================================================
// 4. AXI4-Lite 읽기 FSM
// ============================================================================
reg [1:0]                      rd_state;
reg [C_S_AXI_ADDR_WIDTH-1:0]  ar_addr_r;
wire [C_S_AXI_ADDR_WIDTH-3:0] ar_widx = ar_addr_r[C_S_AXI_ADDR_WIDTH-1:2];

wire [C_S_AXI_ADDR_WIDTH-3:0] ar_w_m5  = ar_widx - 10'd5;
wire [C_S_AXI_ADDR_WIDTH-3:0] ar_w_m10 = ar_widx - 10'd10;
wire [C_S_AXI_ADDR_WIDTH-3:0] ar_w_m15 = ar_widx - 10'd15;

wire [1:0] ar_stage_idx = (ar_widx < 10'd5)  ? 2'd0 :
                           (ar_widx < 10'd10) ? 2'd1 :
                           (ar_widx < 10'd15) ? 2'd2 : 2'd3;
wire [2:0] ar_coef_idx  = (ar_widx < 10'd5)  ? ar_widx[2:0]   :
                           (ar_widx < 10'd10) ? ar_w_m5[2:0]  :
                           (ar_widx < 10'd15) ? ar_w_m10[2:0] :
                           (ar_widx < 10'd20) ? ar_w_m15[2:0] : 3'd0;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rd_state <= 2'd0; S_AXI_ARREADY <= 1'b0; S_AXI_RVALID <= 1'b0;
        S_AXI_RDATA <= {C_S_AXI_DATA_WIDTH{1'b0}}; S_AXI_RRESP <= 2'b00;
        ar_addr_r <= {C_S_AXI_ADDR_WIDTH{1'b0}};
    end else begin
        S_AXI_ARREADY <= 1'b0;
        case (rd_state)
            2'd0: if (S_AXI_ARVALID) begin
                S_AXI_ARREADY <= 1'b1; ar_addr_r <= S_AXI_ARADDR; rd_state <= 2'd1;
            end
            2'd1: begin
                rd_state <= 2'd2;
                if (ar_widx < 10'd20) begin
                    case (ar_stage_idx)
                        2'd0: S_AXI_RDATA <= {{(32-COEF_W){apf_coef_s0[ar_coef_idx][COEF_W-1]}}, apf_coef_s0[ar_coef_idx]};
                        2'd1: S_AXI_RDATA <= {{(32-COEF_W){apf_coef_s1[ar_coef_idx][COEF_W-1]}}, apf_coef_s1[ar_coef_idx]};
                        2'd2: S_AXI_RDATA <= {{(32-COEF_W){apf_coef_s2[ar_coef_idx][COEF_W-1]}}, apf_coef_s2[ar_coef_idx]};
                        2'd3: S_AXI_RDATA <= {{(32-COEF_W){apf_coef_s3[ar_coef_idx][COEF_W-1]}}, apf_coef_s3[ar_coef_idx]};
                        default: S_AXI_RDATA <= 32'hDEAD_BEEF;
                    endcase
                end else if (ar_widx == 10'd64) begin S_AXI_RDATA <= {16'h0, ph_depth};
                end else if (ar_widx == 10'd65) begin S_AXI_RDATA <= {16'h0, lfo_rate};
                end else if (ar_widx == 10'd66) begin S_AXI_RDATA <= {30'h0, lfo_shape};
                end else if (ar_widx == 10'd67) begin S_AXI_RDATA <= {29'h0, stereo_invert, lfo_en, bypass};
                end else begin S_AXI_RDATA <= 32'hDEAD_BEEF; end
            end
            2'd2: begin
                S_AXI_RVALID <= 1'b1; S_AXI_RRESP <= 2'b00;
                if (S_AXI_RVALID && S_AXI_RREADY) begin
                    S_AXI_RVALID <= 1'b0; rd_state <= 2'd0;
                end
            end
            default: rd_state <= 2'd0;
        endcase
    end
end

// ============================================================================
// 5. LFO 계수 갱신 FSM (glitch-safe)
// ============================================================================
reg [2:0]              coef_upd_cnt;
reg                    apf_coef_busy;
reg signed [COEF_W-1:0] lfo_c_latch;

// chain 시작 tready (APF stage0 입력 tready)
wire apf_chain_idle; // 아래 chain wire 선언 후 연결

reg [3:0] lfo_upd_we;   // stage 0~3
reg [2:0] lfo_upd_addr;
reg signed [COEF_W-1:0] lfo_upd_data;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        coef_upd_cnt  <= 3'd5;
        apf_coef_busy <= 1'b0;
        lfo_c_latch   <= {COEF_W{1'b0}};
        lfo_upd_we    <= 4'h0;
        lfo_upd_addr  <= 3'd0;
        lfo_upd_data  <= {COEF_W{1'b0}};
    end else begin
        lfo_upd_we <= 4'h0;
        // 갱신 시작 조건: LFO 활성 + chain IDLE + 이전 갱신 완료
        if (lfo_en && apf_chain_idle && !apf_coef_busy && coef_upd_cnt >= 3'd5) begin
            apf_coef_busy <= 1'b1;
            coef_upd_cnt  <= 3'd0;
            lfo_c_latch   <= lfo_c;
        end
        // 5사이클 갱신 시퀀스
        if (apf_coef_busy && coef_upd_cnt < 3'd5) begin
            lfo_upd_we   <= 4'hF;
            lfo_upd_addr <= coef_upd_cnt;
            case (coef_upd_cnt)
                3'd0: lfo_upd_data <=  lfo_c_latch;
                3'd1: lfo_upd_data <=  APF_B1_NEG;
                3'd2: lfo_upd_data <=  lfo_c_latch;
                3'd3: lfo_upd_data <= -lfo_c_latch;
                3'd4: lfo_upd_data <=  {COEF_W{1'b0}};
                default: lfo_upd_data <= {COEF_W{1'b0}};
            endcase
            coef_upd_cnt <= coef_upd_cnt + 3'd1;
        end
        if (apf_coef_busy && coef_upd_cnt >= 3'd5)
            apf_coef_busy <= 1'b0;
    end
end

// 정적 모드 write gate
wire static_gate = !lfo_en && !apf_coef_busy;
wire static_we0 = wr_do && wr_is_coef && (wr_stage_idx == 2'd0) && static_gate;
wire static_we1 = wr_do && wr_is_coef && (wr_stage_idx == 2'd1) && static_gate;
wire static_we2 = wr_do && wr_is_coef && (wr_stage_idx == 2'd2) && static_gate;
wire static_we3 = wr_do && wr_is_coef && (wr_stage_idx == 2'd3) && static_gate;

// 각 stage coef MUX
wire apf_we0 = lfo_en ? lfo_upd_we[0] : static_we0;
wire apf_we1 = lfo_en ? lfo_upd_we[1] : static_we1;
wire apf_we2 = lfo_en ? lfo_upd_we[2] : static_we2;
wire apf_we3 = lfo_en ? lfo_upd_we[3] : static_we3;
wire [2:0] apf_addr_mux = lfo_en ? lfo_upd_addr : wr_coef_idx;
wire signed [COEF_W-1:0] apf_data_mux = lfo_en ? lfo_upd_data : wr_coef_data;

// ============================================================================
// 6. APF chain - 개별 named wire (HDL 9-1206 방지)
// ============================================================================
wire signed [DATA_W-1:0] cd0, cd1, cd2, cd3, cd4;
wire cv0, cv1, cv2, cv3, cv4;
wire cr0, cr1, cr2, cr3, cr4;

wire signed [DATA_W-1:0] in_ext = {{(DATA_W-IO_W){s_axis_tdata[IO_W-1]}}, s_axis_tdata};

assign cd0 = in_ext;
assign cv0 = s_axis_tvalid && !bypass && !apf_coef_busy;
assign s_axis_tready = bypass ? m_axis_tready : (cr0 && !apf_coef_busy);

// apf_chain_idle 연결 (chain_r0 = cr0)
assign apf_chain_idle = cr0 && !cv0;

// 체인 끝 ready
wire wet_ready_int = m_axis_tready || !m_tvalid_r;
assign cr4 = bypass ? 1'b1 : wet_ready_int;

iir_df1_v2 #(.DATA_W(DATA_W),.COEF_W(COEF_W),.ACC_W(ACC_W),.SHIFT(SHIFT)) u_apf0 (
    .clk(S_AXI_ACLK),.rstn(S_AXI_ARESETN),
    .coef_we(apf_we0),.coef_addr(apf_addr_mux),.coef_wdata(apf_data_mux),
    .s_axis_tdata(cd0),.s_axis_tvalid(cv0),.s_axis_tready(cr0),
    .m_axis_tdata(cd1),.m_axis_tvalid(cv1),.m_axis_tready(cr1));
iir_df1_v2 #(.DATA_W(DATA_W),.COEF_W(COEF_W),.ACC_W(ACC_W),.SHIFT(SHIFT)) u_apf1 (
    .clk(S_AXI_ACLK),.rstn(S_AXI_ARESETN),
    .coef_we(apf_we1),.coef_addr(apf_addr_mux),.coef_wdata(apf_data_mux),
    .s_axis_tdata(cd1),.s_axis_tvalid(cv1),.s_axis_tready(cr1),
    .m_axis_tdata(cd2),.m_axis_tvalid(cv2),.m_axis_tready(cr2));
iir_df1_v2 #(.DATA_W(DATA_W),.COEF_W(COEF_W),.ACC_W(ACC_W),.SHIFT(SHIFT)) u_apf2 (
    .clk(S_AXI_ACLK),.rstn(S_AXI_ARESETN),
    .coef_we(apf_we2),.coef_addr(apf_addr_mux),.coef_wdata(apf_data_mux),
    .s_axis_tdata(cd2),.s_axis_tvalid(cv2),.s_axis_tready(cr2),
    .m_axis_tdata(cd3),.m_axis_tvalid(cv3),.m_axis_tready(cr3));
iir_df1_v2 #(.DATA_W(DATA_W),.COEF_W(COEF_W),.ACC_W(ACC_W),.SHIFT(SHIFT)) u_apf3 (
    .clk(S_AXI_ACLK),.rstn(S_AXI_ARESETN),
    .coef_we(apf_we3),.coef_addr(apf_addr_mux),.coef_wdata(apf_data_mux),
    .s_axis_tdata(cd3),.s_axis_tvalid(cv3),.s_axis_tready(cr3),
    .m_axis_tdata(cd4),.m_axis_tvalid(cv4),.m_axis_tready(cr4));

// ============================================================================
// 7. dry 지연 라인
// ============================================================================
wire fire_in = s_axis_tvalid && cr0 && !apf_coef_busy && !bypass;

reg signed [DATA_W-1:0] dry_dly [0:DLY_DEPTH-1];
integer di;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        for (di = 0; di < DLY_DEPTH; di = di + 1)
            dry_dly[di] <= {DATA_W{1'b0}};
    end else if (fire_in) begin
        dry_dly[0] <= in_ext;
        for (di = 1; di < DLY_DEPTH; di = di + 1)
            dry_dly[di] <= dry_dly[di-1];
    end
end

wire signed [DATA_W-1:0] dry_synced = dry_dly[DLY_DEPTH-1];

// ============================================================================
// 8. wet/dry 믹스 + 출력
// ============================================================================
wire signed [DATA_W-1:0] wet_data  = cd4;
wire                      wet_valid = cv4;

wire signed [MIX_W-1:0] diff_wide =
    $signed({{(MIX_W-DATA_W){wet_data[DATA_W-1]}}, wet_data})
  - $signed({{(MIX_W-DATA_W){dry_synced[DATA_W-1]}}, dry_synced});

wire signed [MIX_W-1:0] wet_contrib =
    ($signed(diff_wide) * $signed({1'b0, ph_depth})) >>> 15;

wire signed [MIX_W-1:0] dry_wide =
    $signed({{(MIX_W-DATA_W){dry_synced[DATA_W-1]}}, dry_synced});

wire signed [MIX_W-1:0] mix_L = dry_wide + wet_contrib;
wire signed [MIX_W-1:0] mix_R = stereo_invert ? (dry_wide - wet_contrib) : mix_L;

localparam signed [MIX_W-1:0] CLAMP_MAX =
    {{(MIX_W-IO_W){1'b0}}, {1'b0, {(IO_W-1){1'b1}}}};
localparam signed [MIX_W-1:0] CLAMP_MIN =
    {{(MIX_W-IO_W){1'b1}}, {1'b1, {(IO_W-1){1'b0}}}};

wire signed [IO_W-1:0] out_L =
    ($signed(mix_L) > $signed(CLAMP_MAX)) ? CLAMP_MAX[IO_W-1:0] :
    ($signed(mix_L) < $signed(CLAMP_MIN)) ? CLAMP_MIN[IO_W-1:0] :
    mix_L[IO_W-1:0];
wire signed [IO_W-1:0] out_R =
    ($signed(mix_R) > $signed(CLAMP_MAX)) ? CLAMP_MAX[IO_W-1:0] :
    ($signed(mix_R) < $signed(CLAMP_MIN)) ? CLAMP_MIN[IO_W-1:0] :
    mix_R[IO_W-1:0];

reg [2*IO_W-1:0] m_tdata_r;
reg              m_tvalid_r;

assign m_axis_tdata  = bypass ? {s_axis_tdata, s_axis_tdata} : m_tdata_r;
assign m_axis_tvalid = bypass ? s_axis_tvalid                : m_tvalid_r;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        m_tdata_r <= {2*IO_W{1'b0}}; m_tvalid_r <= 1'b0;
    end else if (!bypass) begin
        if (wet_valid && wet_ready_int) begin
            m_tdata_r <= {out_R, out_L}; m_tvalid_r <= 1'b1;
        end else if (m_tvalid_r && m_axis_tready) begin
            m_tvalid_r <= 1'b0;
        end
    end else begin
        m_tvalid_r <= 1'b0;
    end
end

endmodule