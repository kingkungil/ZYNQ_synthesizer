`timescale 1ns / 1ps
// ============================================================================
//  vca_compressor.v  Rev.5
//
//  Rev.4 → Rev.5 변경 요약:
//  [FIX-1] MSB detector: for-loop linear scan → casez priority encoder
//          (24-bit 전체를 단일 casez로, 합성기가 2-LUT-level로 최적화 가능)
//  [FIX-2] sqrt21() 제거: iterative multiply loop → BRAM LUT (512×11)
//          (sqrt_lut.hex, 별도 Python 생성 스크립트 동봉)
//  [FIX-3] knee u 계산 개선: 시프트 근사 → reciprocal LUT + 정규화 곱셈
//          (recip_lut.hex, 별도 Python 생성 스크립트 동봉)
//          FSM에 ST_KNEE_U 스테이트 1개 추가 (총 14 state, ~14 cycle/sample)
//
//  Rev.4 수정 사항 유지:
//  [FIX-A] log LUT: leading 1 제거, fractional 8-bit 추출
//  [FIX-B] RMS: IIR leaky integrator (32-bit sq_acc)
//  [FIX-C] RMS → log: LUT sqrt 후 mantissa 정규화 경로
//  [FIX-D] soft knee: SSL quadratic, BRAM knee_quad_lut
//  [FIX-E] exp int_shift clamp → 15
//  [FIX-F] zero input: msb=0 → cur_log=0
//  [FIX-G] dither: >>>14 직전(bit-reduction point)에 적용
//  [FIX-H] multiplier 2단 rounding (>>>8, >>>14)
//
//  필요한 외부 파일 (gen_luts.py로 생성):
//    log2_frac_lut.hex  (256 entries, 2 hex chars)
//    exp2_frac_lut.hex  (256 entries, 3 hex chars)
//    knee_quad_lut.hex  (256 entries, 3 hex chars)
//    sqrt_lut.hex       (512 entries, 3 hex chars)  ← 신규
//    recip_lut.hex      (256 entries, 2 hex chars)  ← 신규
//
//  AXI 레지스터 맵:
//    0x00 reg_thresh  Q5.8  기본 0x0D00
//    0x04 reg_cs      Q1.15 기본 24576 (4:1)
//    0x08 reg_makeup  Q2.14 기본 0x4000 (1.0)
//    0x0C reg_atk     [4:0] 기본 4
//    0x10 reg_rel     [4:0] 기본 6
//    0x14 reg_env_mode [0]  0=peak / 1=RMS
//    0x18 reg_knee    Q5.8  기본 0x0200
//    0x1C reg_dither  [0]   기본 0
//
//  FSM 14 states:
//  IDLE → LOG_REQ → LOG_WAIT → ENV → GAIN → KNEE_REQ → KNEE_U
//       → EXP_REQ → EXP_WAIT → EXP_USE → MULT → SCALE → OUTPUT → DRAIN
// ============================================================================

module vca_compressor #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 6
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI:s_axis:m_axis, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
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
    input  wire signed [15:0]                s_axis_tdata,
    output wire                              s_axis_tready,
    output reg                               m_axis_tvalid,
    output reg  signed [15:0]                m_axis_tdata,
    input  wire                              m_axis_tready
);

// ============================================================================
//  파라미터
// ============================================================================
localparam DATA_W    = 24;
localparam RMS_SHIFT = 6;

// ============================================================================
//  AXI-Lite 레지스터
// ============================================================================
reg [12:0] reg_thresh;
reg [15:0] reg_cs;
reg [15:0] reg_makeup;
reg [4:0]  reg_atk;
reg [4:0]  reg_rel;
reg        reg_env_mode;
reg [12:0] reg_knee;
reg        reg_dither;

localparam [1:0] WR_IDLE=2'd0, WR_ACTIVE=2'd1, WR_RESP=2'd2;
localparam [1:0] RD_IDLE=2'd0, RD_SEND  =2'd1;

reg [1:0]                     wr_state;
reg [C_S_AXI_ADDR_WIDTH-1:0] wr_addr;
reg [C_S_AXI_DATA_WIDTH-1:0] wdata_r;
reg                           wr_en;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        wr_state<=WR_IDLE; S_AXI_AWREADY<=0; S_AXI_WREADY<=0;
        S_AXI_BVALID<=0; S_AXI_BRESP<=0; wr_en<=0;
        reg_thresh<=13'h0D00; reg_cs<=16'd24576; reg_makeup<=16'h4000;
        reg_atk<=5'd4; reg_rel<=5'd6; reg_env_mode<=0;
        reg_knee<=13'h0200; reg_dither<=0;
    end else begin
        S_AXI_AWREADY<=0; S_AXI_WREADY<=0; wr_en<=0;
        case(wr_state)
            WR_IDLE: if(S_AXI_AWVALID) begin
                S_AXI_AWREADY<=1; wr_addr<=S_AXI_AWADDR; wr_state<=WR_ACTIVE; end
            WR_ACTIVE: if(S_AXI_WVALID) begin
                wdata_r<=S_AXI_WDATA; S_AXI_WREADY<=1; wr_en<=1; wr_state<=WR_RESP; end
            WR_RESP: begin
                S_AXI_BVALID<=1; S_AXI_BRESP<=0;
                if(S_AXI_BVALID&&S_AXI_BREADY) begin S_AXI_BVALID<=0; wr_state<=WR_IDLE; end
            end
            default: wr_state<=WR_IDLE;
        endcase
        if(wr_en) case(wr_addr[5:2])
            4'h0: reg_thresh   <= wdata_r[12:0];
            4'h1: reg_cs       <= wdata_r[15:0];
            4'h2: reg_makeup   <= wdata_r[15:0];
            4'h3: reg_atk      <= wdata_r[4:0];
            4'h4: reg_rel      <= wdata_r[4:0];
            4'h5: reg_env_mode <= wdata_r[0];
            4'h6: reg_knee     <= wdata_r[12:0];
            4'h7: reg_dither   <= wdata_r[0];
            default: ;
        endcase
    end
end

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
                case(S_AXI_ARADDR[5:2])
                    4'h0: S_AXI_RDATA<={{19{1'b0}},reg_thresh};
                    4'h1: S_AXI_RDATA<={{16{1'b0}},reg_cs};
                    4'h2: S_AXI_RDATA<={{16{1'b0}},reg_makeup};
                    4'h3: S_AXI_RDATA<={{27{1'b0}},reg_atk};
                    4'h4: S_AXI_RDATA<={{27{1'b0}},reg_rel};
                    4'h5: S_AXI_RDATA<={{31{1'b0}},reg_env_mode};
                    4'h6: S_AXI_RDATA<={{19{1'b0}},reg_knee};
                    4'h7: S_AXI_RDATA<={{31{1'b0}},reg_dither};
                    default: S_AXI_RDATA<=32'hDEADBEEF;
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
//  BRAM: log2 frac LUT (256×8)  [FIX-A]
// ============================================================================
(* ram_style = "block" *) reg [7:0] log2_rom [0:255];
initial $readmemh("log2_frac_lut.hex", log2_rom);
reg [7:0] log_addr_r, log_data_r;
always @(posedge S_AXI_ACLK) log_data_r <= log2_rom[log_addr_r];

// ============================================================================
//  BRAM: exp2 frac LUT (256×9)
// ============================================================================
(* ram_style = "block" *) reg [8:0] exp2_rom [0:255];
initial $readmemh("exp2_frac_lut.hex", exp2_rom);
reg [7:0] exp_addr_r;
reg [8:0] exp_data_r;
always @(posedge S_AXI_ACLK) exp_data_r <= exp2_rom[exp_addr_r];

// ============================================================================
//  BRAM: soft knee quadratic LUT (256×9)  [FIX-D]
// ============================================================================
(* ram_style = "block" *) reg [8:0] knee_rom [0:255];
initial $readmemh("knee_quad_lut.hex", knee_rom);
reg [7:0] knee_addr_r;
reg [8:0] knee_data_r;
always @(posedge S_AXI_ACLK) knee_data_r <= knee_rom[knee_addr_r];

// ============================================================================
//  BRAM: sqrt LUT (512×11)  [FIX-2]
//    addr[8:0] = rms_sq_acc[20:12]  (상위 9bit, 512 entries)
//    data[10:0] = round(sqrt(addr/512) * 1024)  범위 [0..1023]
// ============================================================================
(* ram_style = "block" *) reg [10:0] sqrt_rom [0:511];
initial $readmemh("sqrt_lut.hex", sqrt_rom);
reg [8:0]  sqrt_addr_r;
reg [10:0] sqrt_data_r;
always @(posedge S_AXI_ACLK) sqrt_data_r <= sqrt_rom[sqrt_addr_r];

// ============================================================================
//  BRAM: reciprocal LUT (256×8)  [FIX-3]
//    addr[7:0] = knee_half 정규화 fractional (leading 1 제거 후 8bit)
//    data[7:0] = min(round(256 / (1 + addr/256)), 255)  범위 [128..255]
// ============================================================================
(* ram_style = "block" *) reg [7:0] recip_rom [0:255];
initial $readmemh("recip_lut.hex", recip_rom);
reg [7:0] recip_addr_r;
reg [7:0] recip_data_r;
always @(posedge S_AXI_ACLK) recip_data_r <= recip_rom[recip_addr_r];

// ============================================================================
//  attack/release guard
// ============================================================================
wire [4:0] atk_s = (reg_atk==5'd0) ? 5'd1 : reg_atk;
wire [4:0] rel_s = (reg_rel==5'd0) ? 5'd1 : reg_rel;

// ============================================================================
//  파이프라인 레지스터
// ============================================================================
reg signed [DATA_W-1:0] x_hold;
reg        [DATA_W-1:0] abs_x_r;
reg        [4:0]        msb_r;
reg [20:0]              env_acc;
reg [31:0]              rms_sq_acc;
reg [12:0]              cur_log_r;
reg [12:0]              gain_neg_r;
reg [7:0]               exp_int_r;
reg [8:0]               gain_lin_r;
reg                     in_knee_r;
reg [12:0]              gain_full_r;
reg [7:0]               knee_u_r;
reg signed [39:0]       mult_r;
// [FIX-3] knee 계산용 보조 레지스터
reg [4:0]               kh_msb_r;
reg [12:0]              over_raw_r;

// ============================================================================
//  LFSR-16 TPDF dither
// ============================================================================
reg [15:0] lfsr_r;
wire lfsr_fb = lfsr_r[15]^lfsr_r[13]^lfsr_r[12]^lfsr_r[10];
always @(posedge S_AXI_ACLK)
    if(!S_AXI_ARESETN) lfsr_r<=16'hACE1;
    else               lfsr_r<={lfsr_r[14:0],lfsr_fb};
wire signed [1:0] dither_v = reg_dither ?
    ($signed({1'b0,lfsr_r[0]})-$signed({1'b0,lfsr_r[1]})) : 2'sh0;

// ============================================================================
//  abs_x combinational
// ============================================================================
wire [DATA_W-1:0] abs_x_c =
    (x_hold=={1'b1,{(DATA_W-1){1'b0}}}) ? {1'b0,{(DATA_W-1){1'b1}}} :
     x_hold[DATA_W-1]                    ? $unsigned(-x_hold)          :
                                            $unsigned(x_hold);

// ============================================================================
//  [FIX-1] MSB priority encoder: casez (24-bit)
//  Rev.4 for-loop 대체. 합성기 2-LUT-level 최적화 가능.
// ============================================================================
reg [4:0] msb_c;
always @(*) begin
    casez (abs_x_c)
        24'b1???????????????????????: msb_c = 5'd23;
        24'b01??????????????????????: msb_c = 5'd22;
        24'b001?????????????????????: msb_c = 5'd21;
        24'b0001????????????????????: msb_c = 5'd20;
        24'b00001???????????????????: msb_c = 5'd19;
        24'b000001??????????????????: msb_c = 5'd18;
        24'b0000001?????????????????: msb_c = 5'd17;
        24'b00000001????????????????: msb_c = 5'd16;
        24'b000000001???????????????: msb_c = 5'd15;
        24'b0000000001??????????????: msb_c = 5'd14;
        24'b00000000001?????????????: msb_c = 5'd13;
        24'b000000000001????????????: msb_c = 5'd12;
        24'b0000000000001???????????: msb_c = 5'd11;
        24'b00000000000001??????????: msb_c = 5'd10;
        24'b000000000000001?????????: msb_c = 5'd9;
        24'b0000000000000001????????: msb_c = 5'd8;
        24'b00000000000000001???????: msb_c = 5'd7;
        24'b000000000000000001??????: msb_c = 5'd6;
        24'b0000000000000000001?????: msb_c = 5'd5;
        24'b00000000000000000001????: msb_c = 5'd4;
        24'b000000000000000000001???: msb_c = 5'd3;
        24'b0000000000000000000001??: msb_c = 5'd2;
        24'b00000000000000000000001?: msb_c = 5'd1;
        default:                      msb_c = 5'd0;
    endcase
end

// ============================================================================
//  [FIX-3] knee_half MSB encoder (13-bit casez)
// ============================================================================
wire [12:0] knee_half_w = {1'b0, reg_knee[12:1]};
reg  [3:0]  kh_msb_c;
always @(*) begin
    casez (knee_half_w)
        13'b1????????????: kh_msb_c = 4'd12;
        13'b01???????????: kh_msb_c = 4'd11;
        13'b001??????????: kh_msb_c = 4'd10;
        13'b0001?????????: kh_msb_c = 4'd9;
        13'b00001????????: kh_msb_c = 4'd8;
        13'b000001???????: kh_msb_c = 4'd7;
        13'b0000001??????: kh_msb_c = 4'd6;
        13'b00000001?????: kh_msb_c = 4'd5;
        13'b000000001????: kh_msb_c = 4'd4;
        13'b0000000001???: kh_msb_c = 4'd3;
        13'b00000000001??: kh_msb_c = 4'd2;
        13'b000000000001?: kh_msb_c = 4'd1;
        default:           kh_msb_c = 4'd0;
    endcase
end

// ============================================================================
//  FSM state 정의 (14 states)
// ============================================================================
localparam [3:0]
    ST_IDLE     = 4'd0,
    ST_LOG_REQ  = 4'd1,
    ST_LOG_WAIT = 4'd2,
    ST_ENV      = 4'd3,
    ST_GAIN     = 4'd4,
    ST_KNEE_REQ = 4'd5,   // recip BRAM 주소 hold (bubble)
    ST_KNEE_U   = 4'd6,   // recip 결과 읽기 + u 계산 + knee BRAM 주소 구동
    ST_EXP_REQ  = 4'd7,   // knee 결과 읽기 + blending + exp BRAM 주소 구동
    ST_EXP_WAIT = 4'd8,
    ST_EXP_USE  = 4'd9,
    ST_MULT     = 4'd10,
    ST_SCALE    = 4'd11,
    ST_OUTPUT   = 4'd12,
    ST_DRAIN    = 4'd13;

reg [3:0] state;
reg       tready_r;
assign s_axis_tready = tready_r;

// ============================================================================
//  메인 FSM
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        state        <= ST_IDLE;
        tready_r     <= 0;
        x_hold       <= 0;
        abs_x_r      <= 0;
        msb_r        <= 0;
        env_acc      <= 0;
        rms_sq_acc   <= 0;
        cur_log_r    <= 0;
        gain_neg_r   <= 0;
        exp_int_r    <= 0;
        gain_lin_r   <= 9'd256;
        in_knee_r    <= 0;
        gain_full_r  <= 0;
        knee_u_r     <= 0;
        mult_r       <= 0;
        kh_msb_r     <= 0;
        over_raw_r   <= 0;
        log_addr_r   <= 0;
        exp_addr_r   <= 0;
        knee_addr_r  <= 0;
        sqrt_addr_r  <= 0;
        recip_addr_r <= 0;
        m_axis_tvalid<= 0;
        m_axis_tdata <= 0;
    end else begin
        case (state)

        // ──────────────────────────────────────────────────────────────
        ST_IDLE: begin
            tready_r <= 1;
            if (s_axis_tvalid && tready_r) begin
                x_hold   <= {{(DATA_W-16){s_axis_tdata[15]}}, s_axis_tdata};
                tready_r <= 0;
                state    <= ST_LOG_REQ;
            end
        end

        // ──────────────────────────────────────────────────────────────
        // [FIX-1] casez MSB 사용, [FIX-2] sqrt BRAM 미리 구동
        ST_LOG_REQ: begin
            abs_x_r <= abs_x_c;
            msb_r   <= msb_c;

            if (msb_c == 5'd0)
                log_addr_r <= 8'd0;
            else if (msb_c >= 5'd8)
                log_addr_r <= (abs_x_c >> (msb_c - 5'd8)) & 8'hFF;
            else
                log_addr_r <= (abs_x_c << (5'd8 - msb_c)) & 8'hFF;

            sqrt_addr_r <= rms_sq_acc[20:12];
            state <= ST_LOG_WAIT;
        end

        // ──────────────────────────────────────────────────────────────
        // [FIX-B] RMS IIR 갱신
        ST_LOG_WAIT: begin
            begin : rms_iir_blk
                reg [47:0] sq_full;
                reg [31:0] sq_sc;
                sq_full = abs_x_r * abs_x_r;
                sq_sc   = (sq_full[47:12] > 32'hFFFFFFFF) ? 32'hFFFFFFFF
                                                           : sq_full[47:12];
                if (sq_sc > rms_sq_acc)
                    rms_sq_acc <= rms_sq_acc + ((sq_sc - rms_sq_acc) >> RMS_SHIFT);
                else
                    rms_sq_acc <= rms_sq_acc - ((rms_sq_acc - sq_sc) >> RMS_SHIFT);
            end
            state <= ST_ENV;
        end

        // ──────────────────────────────────────────────────────────────
        // [FIX-C] envelope + log 변환, [FIX-2] LUT sqrt 사용
        ST_ENV: begin
            begin : env_main_blk
                reg [12:0] track_log;
                reg [20:0] target_acc;

                if (reg_env_mode) begin
                    // RMS 경로: LUT sqrt → MSB 정규화 → log
                    begin : rms_log_blk
                        reg [4:0] rms_msb;
                        reg [7:0] rms_frac;
                        reg [4:0] rms_adj;

                        casez (sqrt_data_r)
                            11'b1??????????: rms_msb = 5'd10;
                            11'b01?????????: rms_msb = 5'd9;
                            11'b001????????: rms_msb = 5'd8;
                            11'b0001???????: rms_msb = 5'd7;
                            11'b00001??????: rms_msb = 5'd6;
                            11'b000001?????: rms_msb = 5'd5;
                            11'b0000001????: rms_msb = 5'd4;
                            11'b00000001???: rms_msb = 5'd3;
                            11'b000000001??: rms_msb = 5'd2;
                            11'b0000000001?: rms_msb = 5'd1;
                            default:         rms_msb = 5'd0;
                        endcase

                        if (rms_msb == 5'd0)
                            rms_frac = 8'd0;
                        else if (rms_msb >= 5'd8)
                            rms_frac = (sqrt_data_r >> (rms_msb - 5'd8)) & 8'hFF;
                        else
                            rms_frac = (sqrt_data_r << (5'd8 - rms_msb)) & 8'hFF;

                        rms_adj  = ((rms_msb + 5'd6) > 5'd22) ? 5'd22
                                                               : (rms_msb + 5'd6);
                        track_log = (rms_msb == 5'd0) ? 13'd0 : {rms_adj, rms_frac};
                    end
                end else begin
                    // Peak 경로
                    track_log = (msb_r == 5'd0) ? 13'd0 : {msb_r, log_data_r};
                end

                cur_log_r  <= track_log;
                target_acc  = {track_log, 8'h00};

                if (target_acc > env_acc)
                    env_acc <= env_acc + ((target_acc - env_acc) >> atk_s);
                else begin : rel_blk
                    reg [20:0] delta;
                    delta   = (env_acc - target_acc) >> rel_s;
                    env_acc <= (delta >= env_acc) ? 21'd0 : (env_acc - delta);
                end
            end
            state <= ST_GAIN;
        end

        // ──────────────────────────────────────────────────────────────
        // [FIX-D, FIX-3] gain 계산 + knee 판정 + recip BRAM 주소 구동
        ST_GAIN: begin
            begin : gain_main_blk
                reg [12:0] env_v, over_raw, knee_half, gain_full;
                reg [28:0] gain_full_mul;

                env_v     = env_acc[20:8];
                knee_half = knee_half_w;

                if (env_v <= reg_thresh) begin
                    gain_neg_r   <= 0; in_knee_r   <= 0;
                    gain_full_r  <= 0; knee_u_r    <= 0;
                    kh_msb_r     <= 0; over_raw_r  <= 0;
                    recip_addr_r <= 0;
                end else begin
                    over_raw      = env_v - reg_thresh;
                    gain_full_mul = over_raw * reg_cs;
                    gain_full     = gain_full_mul[27:15];
                    if (gain_full_mul[28]) gain_full = 13'h1FFF;

                    if (knee_half == 13'd0 || over_raw >= knee_half) begin
                        gain_neg_r   <= gain_full; in_knee_r  <= 0;
                        gain_full_r  <= gain_full; knee_u_r   <= 0;
                        kh_msb_r     <= 0;         over_raw_r <= 0;
                        recip_addr_r <= 0;
                    end else begin
                        // [FIX-3] knee_half 정규화 fractional → recip addr
                        begin : recip_addr_blk
                            reg [7:0] kh_frac;
                            if (kh_msb_c == 4'd0)
                                kh_frac = 8'd0;
                            else if (kh_msb_c >= 4'd8)
                                kh_frac = (knee_half >> (kh_msb_c - 4'd8)) & 8'hFF;
                            else
                                kh_frac = (knee_half << (4'd8 - kh_msb_c)) & 8'hFF;
                            recip_addr_r <= kh_frac;
                        end
                        kh_msb_r    <= {1'b0, kh_msb_c};
                        over_raw_r  <= over_raw;
                        gain_full_r <= gain_full;
                        gain_neg_r  <= 0;
                        in_knee_r   <= 1;
                        knee_u_r    <= 0;
                    end
                end
            end
            state <= ST_KNEE_REQ;
        end

        // ──────────────────────────────────────────────────────────────
        // ST_KNEE_REQ: recip BRAM latency bubble
        //   non-knee 경로: exp_addr 미리 구동
        ST_KNEE_REQ: begin
            if (!in_knee_r) begin
                exp_addr_r <= gain_neg_r[7:0];
                exp_int_r  <= {3'b0, gain_neg_r[12:8]};
            end
            state <= ST_KNEE_U;
        end

        // ──────────────────────────────────────────────────────────────
        // [FIX-3] ST_KNEE_U: recip 결과로 u 계산 + knee_addr 구동
        //
        //  recip_data_r = round(256 / normalized_knee_half)
        //               ≈ 256 / (knee_half / 2^kh_msb_r)
        //               = 256 * 2^kh_msb_r / knee_half
        //
        //  u_prod = over_raw * recip_data_r
        //         ≈ over_raw * 256 * 2^kh_msb_r / knee_half
        //
        //  u8 = u_prod >> (kh_msb_r + 1)
        //     = over_raw * 128 / knee_half  (≈ [0..127])
        //  단, knee_half 기준 정규화이므로 over_raw∈[0,knee_half) → u8∈[0,127]
        //  knee_addr에는 u8을 그대로 사용 (LUT addr [0..255], 실효 범위 [0..127])
        //
        //  NOTE: 위 설명에서 "256"은 recip LUT의 Q 스케일,
        //        "+1" 시프트는 leading 1 포함을 고려한 보정
        ST_KNEE_U: begin
            if (in_knee_r) begin
                begin : knee_u_blk
                    reg [20:0] u_prod;
                    reg [5:0]  shift_amt;
                    reg [7:0]  u8;

                    u_prod    = over_raw_r * recip_data_r;          // 13×8 = 21-bit
                    shift_amt = (kh_msb_r >= 5'd19) ? 6'd20
                                                     : ({1'b0,kh_msb_r} + 6'd1);
                    u8        = u_prod[19:0] >> shift_amt;          // clamp implicit

                    // 상위 비트에 잔량이 있으면 최대값으로 클램프
                    if (|u_prod[20:8])  u8 = 8'hFF;

                    knee_u_r    <= u8;
                    knee_addr_r <= u8;
                end
            end else begin
                knee_addr_r <= 8'hFF;   // don't-care for non-knee path
            end
            state <= ST_EXP_REQ;
        end

        // ──────────────────────────────────────────────────────────────
        // [FIX-D] knee_data 읽기 + blending + exp_addr 구동
        ST_EXP_REQ: begin
            if (in_knee_r) begin
                begin : knee_blend_blk
                    reg [21:0] blended;
                    blended    = gain_full_r * knee_data_r;
                    gain_neg_r <= blended[20:8];
                    exp_addr_r <= blended[15:8];
                    exp_int_r  <= {3'b0, blended[20:16]};
                end
            end
            state <= ST_EXP_WAIT;
        end

        // ──────────────────────────────────────────────────────────────
        ST_EXP_WAIT: state <= ST_EXP_USE;

        // ──────────────────────────────────────────────────────────────
        // [FIX-E] exp int clamp → 15
        ST_EXP_USE: begin
            begin : exp_use_blk
                reg [7:0] sh;
                sh         = (exp_int_r > 8'd15) ? 8'd15 : exp_int_r;
                gain_lin_r <= exp_data_r >> sh;
            end
            state <= ST_MULT;
        end

        // ──────────────────────────────────────────────────────────────
        ST_MULT: begin
            mult_r <= $signed({{(40-DATA_W){x_hold[DATA_W-1]}}, x_hold}) *
                      $signed({31'h0, gain_lin_r});
            state  <= ST_SCALE;
        end

        ST_SCALE: state <= ST_OUTPUT;

        // ──────────────────────────────────────────────────────────────
        // [FIX-G, FIX-H] makeup + dither + clamp
        ST_OUTPUT: begin
            begin : output_blk
                reg signed [31:0] gain_out, scaled, scaled_d;
                reg signed [47:0] pre_mk;

                gain_out = ($signed(mult_r)   + $signed(40'sh80))   >>> 8;
                pre_mk   =  $signed(gain_out) * $signed({16'h0, reg_makeup});
                scaled   = ($signed(pre_mk)   + $signed(48'sh2000)) >>> 14;
                scaled_d =  scaled + {{30{dither_v[1]}}, dither_v};

                if      ($signed(scaled_d) > $signed(32'sh00007FFF))
                    m_axis_tdata <= 16'sh7FFF;
                else if ($signed(scaled_d) < $signed(32'shFFFF8000))
                    m_axis_tdata <= 16'sh8000;
                else
                    m_axis_tdata <= scaled_d[15:0];
            end
            m_axis_tvalid <= 1;
            state         <= ST_DRAIN;
        end

        // ──────────────────────────────────────────────────────────────
        ST_DRAIN: begin
            if (m_axis_tready) begin
                m_axis_tvalid <= 0;
                state         <= ST_IDLE;
                tready_r      <= 1;
            end
        end

        default: begin
            state         <= ST_IDLE;
            tready_r      <= 0;
            m_axis_tvalid <= 0;
        end
        endcase
    end
end

endmodule