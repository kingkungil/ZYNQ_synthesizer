`timescale 1ns / 1ps
// =============================================================================
//  dac_bridge_stereo.v  v3
//
//  v2 대비 변경: LUT 감소 + WNS -0.127ns 추가 개선 (FF 3000개 추가 허용)
//
//  ── LUT 감소 / WNS 개선 포인트 ────────────────────────────────────────────
//  [A] tg_D3 상수 FF 제거 → pg6 = tg_D0+tg_D1+tg_D2 (3항, LUT 절감)
//  [B] FIR_PBPC 분리: FIR_PBC(pb/pc) + FIR_P56(p5/p6) + FIR_P8(p8) 3 always
//      → p5_scaled(37bit) 비교기가 별도 사이클, 조합 깊이 및 LUT 감소
//  [C] POLY_PB 패스스루 FF 완전 삭제 (37bit×8×2ch=592FF + 팬아웃 LUT 절감)
//      → POLY_PC가 pp_p*lo/hi 직접 참조
//  [D] NSD_GSD gain_shifted_d 54bit 지연 FF 삭제 (108FF + 연결 LUT 절감)
//      → NSD_NB에서 gain_shifted_r 직접 참조
//  [E] p*_sat 37bit wire 비교기 → pp_sat_r 16bit 등록 레지스터로 변환
//      → pp_out_r 직전 37bit 팬인 비교 LUT 4×2ch 제거
//
//  ── v2 파이프라인 계승 ─────────────────────────────────────────────────────
//  FIR_TAP_PIPE / FIR_PG / FIR_PA / POLY_PA_REG 모두 유지
//  v1 버그픽스 BUG-6/H/I/J/K/L/M/R/S 모두 유지
// =============================================================================

module dac_bridge #(
    parameter WIDTH        = 16,
    parameter INTERP_R     = 72,
    parameter CIC_N        = 3,
    parameter CIC_W        = 35,
    parameter CIC_K        = 1472897,
    parameter CIC_S        = 33,
    parameter POLY_L       = 4,
    parameter POLY_K       = 12,
    parameter POLY_Q       = 13,
    parameter FIR_TAPS     = 63,
    parameter FIR_ACC_W    = 37,
    parameter RST_HOLD_CYC = 16,
    parameter GUARD_MAX    = 32767,
    parameter HPF_ALPHA    = 32757
)(
    input  wire        dac_clk,
    input  wire        rst,
    // ── 채널 L (dac_bridge_0 대응)
    input  wire [15:0] fifo_dout_L,
    input  wire        fifo_empty_L,
    output reg         fifo_pop_L,
    output wire        dac_out_L,
    // ── 채널 R (dac_bridge_1 대응)
    input  wire [15:0] fifo_dout_R,
    input  wire        fifo_empty_R,
    output reg         fifo_pop_R,
    output wire        dac_out_R
);

// =============================================================================
// RST CDC + hold  [공유]
// =============================================================================
reg [1:0] rst_sync;
reg [4:0] hold_cnt;
reg       safe_rst;

always @(posedge dac_clk or posedge rst) begin
    if (rst) rst_sync <= 2'b11;
    else     rst_sync <= {rst_sync[0], 1'b0};
end
wire rst_cdc = rst_sync[1];

always @(posedge dac_clk) begin
    if (rst_cdc) begin
        hold_cnt <= 5'd0;
        safe_rst <= 1'b1;
    end else begin
        if (hold_cnt < RST_HOLD_CYC[4:0]) begin
            hold_cnt <= hold_cnt + 5'd1;
            safe_rst <= 1'b1;
        end else
            safe_rst <= 1'b0;
    end
end

// =============================================================================
// Pace 카운터 + Polyphase tick 생성  [공유]
// =============================================================================
reg [6:0] pace_cnt;
reg       tick, tick_b, tick_4m;
reg [3:0] pp_tick, pp_tick_b, pp_tick_4m;

always @(posedge dac_clk) begin
    if (safe_rst) begin
        pace_cnt   <= 7'd0;
        tick       <= 1'b0; tick_b  <= 1'b0; tick_4m   <= 1'b0;
        pp_tick    <= 4'b0; pp_tick_b <= 4'b0; pp_tick_4m <= 4'b0;
    end else begin
        tick      <= 1'b0;
        tick_b    <= tick;
        tick_4m   <= tick_b;
        pp_tick_b <= pp_tick;
        pp_tick_4m<= pp_tick_b;
        pp_tick[0] <= (pace_cnt == 7'd0);
        pp_tick[1] <= (pace_cnt == 7'd18);
        pp_tick[2] <= (pace_cnt == 7'd36);
        pp_tick[3] <= (pace_cnt == 7'd54);
        if (pace_cnt == (INTERP_R - 1)) begin
            pace_cnt <= 7'd0; tick <= 1'b1;
        end else
            pace_cnt <= pace_cnt + 7'd1;
    end
end

// =============================================================================
// FIR 계수 선언  [공유 - 합성 시 wire이므로 1벌만 존재]
// =============================================================================
wire signed [15:0] COEF [0:62];
assign COEF[ 0]=16'sd0;    assign COEF[ 1]=16'sd0;    assign COEF[ 2]=16'sd0;
assign COEF[ 3]=16'sd0;    assign COEF[ 4]=16'sd1;    assign COEF[ 5]=-16'sd2;
assign COEF[ 6]=16'sd2;    assign COEF[ 7]=16'sd0;    assign COEF[ 8]=-16'sd2;
assign COEF[ 9]=16'sd3;    assign COEF[10]=-16'sd2;   assign COEF[11]=16'sd2;
assign COEF[12]=-16'sd8;   assign COEF[13]=16'sd24;   assign COEF[14]=-16'sd45;
assign COEF[15]=16'sd52;   assign COEF[16]=-16'sd19;  assign COEF[17]=-16'sd71;
assign COEF[18]=16'sd198;  assign COEF[19]=-16'sd298; assign COEF[20]=16'sd277;
assign COEF[21]=-16'sd55;  assign COEF[22]=-16'sd361; assign COEF[23]=16'sd833;
assign COEF[24]=-16'sd1101;assign COEF[25]=16'sd862;  assign COEF[26]=16'sd84;
assign COEF[27]=-16'sd1673;assign COEF[28]=16'sd3442; assign COEF[29]=-16'sd4328;
assign COEF[30]=16'sd1629; assign COEF[31]=16'sd17497;assign COEF[32]=16'sd1629;
assign COEF[33]=-16'sd4328;assign COEF[34]=16'sd3442; assign COEF[35]=-16'sd1673;
assign COEF[36]=16'sd84;   assign COEF[37]=16'sd862;  assign COEF[38]=-16'sd1101;
assign COEF[39]=16'sd833;  assign COEF[40]=-16'sd361; assign COEF[41]=-16'sd55;
assign COEF[42]=16'sd277;  assign COEF[43]=-16'sd298; assign COEF[44]=16'sd198;
assign COEF[45]=-16'sd71;  assign COEF[46]=-16'sd19;  assign COEF[47]=16'sd52;
assign COEF[48]=-16'sd45;  assign COEF[49]=16'sd24;   assign COEF[50]=-16'sd8;
assign COEF[51]=16'sd2;    assign COEF[52]=-16'sd2;   assign COEF[53]=16'sd3;
assign COEF[54]=-16'sd2;   assign COEF[55]=16'sd0;    assign COEF[56]=16'sd2;
assign COEF[57]=-16'sd2;   assign COEF[58]=16'sd1;    assign COEF[59]=16'sd0;
assign COEF[60]=16'sd0;    assign COEF[61]=16'sd0;    assign COEF[62]=16'sd0;

// =============================================================================
//  채널별 신호 경로
// =============================================================================
wire [15:0] fifo_dout_ch  [0:1];
wire        fifo_empty_ch [0:1];
reg         fifo_pop_ch   [0:1];
wire        dac_out_ch    [0:1];

assign fifo_dout_ch[0]  = fifo_dout_L;
assign fifo_dout_ch[1]  = fifo_dout_R;
assign fifo_empty_ch[0] = fifo_empty_L;
assign fifo_empty_ch[1] = fifo_empty_R;

always @(*) begin
    fifo_pop_L = fifo_pop_ch[0];
    fifo_pop_R = fifo_pop_ch[1];
end

assign dac_out_L = dac_out_ch[0];
assign dac_out_R = dac_out_ch[1];

genvar ch;
generate
for (ch = 0; ch < 2; ch = ch + 1) begin : CHANNEL

    // =========================================================================
    // FIFO pop  [v13-FIX BUG-6]
    // =========================================================================
    reg             dout_valid_r;
    reg signed [15:0] sample_raw;

    always @(posedge dac_clk) begin
        if (safe_rst) begin
            fifo_pop_ch[ch] <= 1'b0;
            dout_valid_r    <= 1'b0;
            sample_raw      <= 16'sd0;
        end else begin
            fifo_pop_ch[ch] <= 1'b0;
            dout_valid_r    <= fifo_pop_ch[ch];
            if (dout_valid_r)
                sample_raw <= $signed(fifo_dout_ch[ch]);
            if (tick && !fifo_empty_ch[ch] && !fifo_pop_ch[ch] && !dout_valid_r)
                fifo_pop_ch[ch] <= 1'b1;
        end
    end

    // =========================================================================
    // HPF  [Q12, DSP-FREE]
    // =========================================================================
    reg signed [31:0] hpf_y;
    reg signed [15:0] hpf_xd;
    reg signed [15:0] hpf_out;

    always @(posedge dac_clk) begin
        if (safe_rst) begin
            hpf_y   <= 32'sd0;
            hpf_xd  <= 16'sd0;
            hpf_out <= 16'sd0;
        end else if (tick) begin
            hpf_xd <= sample_raw;
            hpf_y  <= (($signed(sample_raw) - $signed(hpf_xd)) <<< 12)
                     + ($signed(hpf_y)
                        - ($signed(hpf_y) >>> 12)
                        - ($signed(hpf_y) >>> 14)
                        - ($signed(hpf_y) >>> 15));
            if      (hpf_y > 32'sh07FFF000) hpf_out <= 16'sh7FFF;
            else if (hpf_y < -32'sh08000000) hpf_out <= 16'sh8000;
            else                              hpf_out <= hpf_y[27:12];
        end
    end

    // =========================================================================
    // FIR Stage-A  (tick 기반)
    // =========================================================================
    reg signed [15:0] fir_sreg [0:62];
    reg signed [16:0] sym_reg  [0:30];
    reg signed [15:0] center_reg;
    reg               fir_valid_a;
    reg [6:0]         valid_cnt;
    reg               pipe_ready;
    integer fir_i;

    always @(posedge dac_clk) begin : FIR_STAGE_A
        if (safe_rst) begin
            for (fir_i = 0; fir_i < 63; fir_i = fir_i + 1)
                fir_sreg[fir_i] <= 16'sd0;
            for (fir_i = 0; fir_i < 31; fir_i = fir_i + 1)
                sym_reg[fir_i] <= 17'sd0;
            center_reg  <= 16'sd0;
            fir_valid_a <= 1'b0;
            valid_cnt   <= 7'd0;
            pipe_ready  <= 1'b0;
        end else if (tick) begin
            for (fir_i = 62; fir_i > 0; fir_i = fir_i - 1)
                fir_sreg[fir_i] <= fir_sreg[fir_i-1];
            fir_sreg[0] <= hpf_out;

            for (fir_i = 0; fir_i < 31; fir_i = fir_i + 1)
                sym_reg[fir_i] <= $signed({fir_sreg[fir_i][15],   fir_sreg[fir_i]})
                                 + $signed({fir_sreg[62-fir_i][15], fir_sreg[62-fir_i]});
            center_reg <= fir_sreg[31];

            if (!pipe_ready) begin
                if (valid_cnt == 7'd65) pipe_ready <= 1'b1;
                else                    valid_cnt  <= valid_cnt + 7'd1;
            end
            fir_valid_a <= pipe_ready;
        end
    end

    // =========================================================================
    // FIR Stage-B valid pipe (tick_b 기반, 10bit)
    // =========================================================================
    reg [9:0] fva_pipe;
    always @(posedge dac_clk) begin
        if (safe_rst)       fva_pipe <= 10'b0;
        else if (tick_b)    fva_pipe <= {fva_pipe[8:0], fir_valid_a};
    end
    wire fir_valid = fva_pipe[9];

    // =========================================================================
    // FIR Stage-B  sym_reg 캡처 레지스터 (tick_b 래치)
    // =========================================================================
    reg signed [16:0] sr [0:30];
    reg signed [15:0] cr;
    integer fir_k;
    always @(posedge dac_clk) begin
        if (safe_rst) begin
            for (fir_k = 0; fir_k < 31; fir_k = fir_k + 1) sr[fir_k] <= 17'sd0;
            cr <= 16'sd0;
        end else if (tick_b) begin
            for (fir_k = 0; fir_k < 31; fir_k = fir_k + 1) sr[fir_k] <= sym_reg[fir_k];
            cr <= center_reg;
        end
    end

    // ── shift-add tap wires (37bit, sr/cr 기반) ─────────────────────────────
    wire signed [36:0] t4  = {{20{sr[4][16]}},  sr[4]};
    wire signed [36:0] t5  = -{{19{sr[5][16]}},  sr[5], 1'b0};
    wire signed [36:0] t6  = {{19{sr[6][16]}},  sr[6], 1'b0};
    wire signed [36:0] t7  = 37'sd0;
    wire signed [36:0] t8  = -{{19{sr[8][16]}},  sr[8], 1'b0};
    wire signed [36:0] t9  = {{19{sr[9][16]}},  sr[9], 1'b0}
                            + {{20{sr[9][16]}},  sr[9]};
    wire signed [36:0] t10 = -{{19{sr[10][16]}}, sr[10], 1'b0};
    wire signed [36:0] t11 = {{19{sr[11][16]}}, sr[11], 1'b0};
    wire signed [36:0] t12 = -{{17{sr[12][16]}}, sr[12], 3'b000};
    wire signed [36:0] t13 = {{16{sr[13][16]}}, sr[13], 4'b0000}
                            + {{17{sr[13][16]}}, sr[13], 3'b000};
    wire signed [36:0] t14 = -({{14{sr[14][16]}}, sr[14], 5'b00000}
                               +{{17{sr[14][16]}}, sr[14], 3'b000}
                               +{{18{sr[14][16]}}, sr[14], 2'b00}
                               +{{20{sr[14][16]}}, sr[14]});
    wire signed [36:0] t15 = {{14{sr[15][16]}}, sr[15], 5'b00000}
                            + {{15{sr[15][16]}}, sr[15], 4'b0000}
                            + {{18{sr[15][16]}}, sr[15], 2'b00};
    wire signed [36:0] t16 = -({{15{sr[16][16]}}, sr[16], 4'b0000}
                               +{{19{sr[16][16]}}, sr[16], 1'b0}
                               +{{20{sr[16][16]}}, sr[16]});
    wire signed [36:0] t17 = -({{13{sr[17][16]}}, sr[17], 6'b000000}
                               +{{18{sr[17][16]}}, sr[17], 2'b00}
                               +{{19{sr[17][16]}}, sr[17], 1'b0}
                               +{{20{sr[17][16]}}, sr[17]});
    wire signed [36:0] t18 = {{12{sr[18][16]}}, sr[18], 7'b0000000}
                            + {{13{sr[18][16]}}, sr[18], 6'b000000}
                            + {{18{sr[18][16]}}, sr[18], 2'b00}
                            + {{19{sr[18][16]}}, sr[18], 1'b0};
    wire signed [36:0] t19 = -({{11{sr[19][16]}}, sr[19], 8'b00000000}
                               +{{14{sr[19][16]}}, sr[19], 5'b00000}
                               +{{17{sr[19][16]}}, sr[19], 3'b000}
                               +{{19{sr[19][16]}}, sr[19], 1'b0});
    wire signed [36:0] t20 = {{11{sr[20][16]}}, sr[20], 8'b00000000}
                            + {{15{sr[20][16]}}, sr[20], 4'b0000}
                            + {{18{sr[20][16]}}, sr[20], 2'b00}
                            + {{20{sr[20][16]}}, sr[20]};
    wire signed [36:0] t21 = -({{14{sr[21][16]}}, sr[21], 5'b00000}
                               +{{15{sr[21][16]}}, sr[21], 4'b0000}
                               +{{18{sr[21][16]}}, sr[21], 2'b00}
                               +{{19{sr[21][16]}}, sr[21], 1'b0}
                               +{{20{sr[21][16]}}, sr[21]});
    wire signed [36:0] t22 = -({{11{sr[22][16]}}, sr[22], 8'b00000000}
                               +{{13{sr[22][16]}}, sr[22], 6'b000000}
                               +{{14{sr[22][16]}}, sr[22], 5'b00000}
                               +{{17{sr[22][16]}}, sr[22], 3'b000}
                               +{{20{sr[22][16]}}, sr[22]});
    wire signed [36:0] t23 = {{10{sr[23][16]}}, sr[23], 9'b000000000}
                            + {{11{sr[23][16]}}, sr[23], 8'b00000000}
                            + {{13{sr[23][16]}}, sr[23], 6'b000000}
                            + {{20{sr[23][16]}}, sr[23]};
    wire signed [36:0] t24 = -({{ 9{sr[24][16]}}, sr[24], 10'b0000000000}
                               +{{13{sr[24][16]}}, sr[24], 6'b000000}
                               +{{17{sr[24][16]}}, sr[24], 3'b000}
                               +{{18{sr[24][16]}}, sr[24], 2'b00}
                               +{{20{sr[24][16]}}, sr[24]});
    wire signed [36:0] t25 = {{ 9{sr[25][16]}}, sr[25], 10'b0000000000}
                            - {{12{sr[25][16]}}, sr[25], 7'b0000000}
                            - {{14{sr[25][16]}}, sr[25], 5'b00000}
                            - {{19{sr[25][16]}}, sr[25], 1'b0};
    wire signed [36:0] t26 = {{13{sr[26][16]}}, sr[26], 6'b000000}
                            + {{15{sr[26][16]}}, sr[26], 4'b0000}
                            + {{18{sr[26][16]}}, sr[26], 2'b00};
    wire signed [36:0] t27 = -({{ 9{sr[27][16]}}, sr[27], 10'b0000000000}
                               +{{10{sr[27][16]}}, sr[27], 9'b000000000}
                               +{{12{sr[27][16]}}, sr[27], 7'b0000000}
                               +{{17{sr[27][16]}}, sr[27], 3'b000}
                               +{{20{sr[27][16]}}, sr[27]});
    wire signed [36:0] t28 = {{ 7{sr[28][16]}}, sr[28], 12'b000000000000}
                            - {{10{sr[28][16]}}, sr[28], 9'b000000000}
                            - {{12{sr[28][16]}}, sr[28], 7'b0000000}
                            - {{15{sr[28][16]}}, sr[28], 4'b0000}
                            + {{19{sr[28][16]}}, sr[28], 1'b0};
    wire signed [36:0] t29 = -({{ 7{sr[29][16]}}, sr[29], 12'b000000000000}
                               +{{11{sr[29][16]}}, sr[29], 8'b00000000}
                               -{{15{sr[29][16]}}, sr[29], 4'b0000}
                               -{{17{sr[29][16]}}, sr[29], 3'b000});
    wire signed [36:0] t30 = {{ 9{sr[30][16]}}, sr[30], 10'b0000000000}
                            + {{10{sr[30][16]}}, sr[30], 9'b000000000}
                            + {{13{sr[30][16]}}, sr[30], 6'b000000}
                            + {{15{sr[30][16]}}, sr[30], 4'b0000}
                            + {{17{sr[30][16]}}, sr[30], 3'b000}
                            + {{18{sr[30][16]}}, sr[30], 2'b00}
                            + {{20{sr[30][16]}}, sr[30]};
    wire signed [36:0] tc  = {{  7{cr[15]}}, cr, 14'b00000000000000}
                            + {{  5{cr[15]}}, cr, 10'b0000000000}
                            + {{  9{cr[15]}}, cr, 6'b000000}
                            + {{ 11{cr[15]}}, cr, 4'b0000}
                            + {{ 12{cr[15]}}, cr, 3'b000}
                            + {{ 20{cr[15]}}, cr};

    // ── [파이프라인 추가 1] tap wire → 4그룹 중간합 레지스터 (tick_b 기준) ──
    // 목적: sr[] → t* → pg* 의 콤비네이션 경로를 2사이클로 분할
    // t4~t11 (그룹A-lo), t12~t19 (그룹A-hi), t20~t27 (그룹B-lo), t28~tc (그룹B-hi)
    // 각 그룹을 2개씩 묶어 중간합 → 4개의 37bit 레지스터
    // 추가 FF: 37bit × 8 × 2ch = 592 FF
    // [A] tg_D3 상수 FF 제거 (LUT 절감)
    reg signed [36:0] tg_A0, tg_A1, tg_A2, tg_A3;  // t4+t5, t6+t7, t8+t9, t10+t11
    reg signed [36:0] tg_B0, tg_B1, tg_B2, tg_B3;  // t12+t13, t14+t15, t16+t17, t18+t19
    reg signed [36:0] tg_C0, tg_C1, tg_C2, tg_C3;  // t20+t21, t22+t23, t24+t25, t26+t27
    reg signed [36:0] tg_D0, tg_D1, tg_D2;          // t28+t29, t30, tc

    always @(posedge dac_clk) begin : FIR_TAP_PIPE
        if (safe_rst) begin
            tg_A0<=37'sd0; tg_A1<=37'sd0; tg_A2<=37'sd0; tg_A3<=37'sd0;
            tg_B0<=37'sd0; tg_B1<=37'sd0; tg_B2<=37'sd0; tg_B3<=37'sd0;
            tg_C0<=37'sd0; tg_C1<=37'sd0; tg_C2<=37'sd0; tg_C3<=37'sd0;
            tg_D0<=37'sd0; tg_D1<=37'sd0; tg_D2<=37'sd0;
        end else if (tick_b) begin
            tg_A0 <= t4  + t5;
            tg_A1 <= t6  + t7;
            tg_A2 <= t8  + t9;
            tg_A3 <= t10 + t11;
            tg_B0 <= t12 + t13;
            tg_B1 <= t14 + t15;
            tg_B2 <= t16 + t17;
            tg_B3 <= t18 + t19;
            tg_C0 <= t20 + t21;
            tg_C1 <= t22 + t23;
            tg_C2 <= t24 + t25;
            tg_C3 <= t26 + t27;
            tg_D0 <= t28 + t29;
            tg_D1 <= t30;
            tg_D2 <= tc;
        end
    end

    // ── Stage-B 파이프라인: pg/pa/pb/pc 각각 별도 always로 분리 ─────────────
    // [주의] 위 FIR_TAP_PIPE가 tick_b 기준 1사이클 선행하므로
    //        pg* always도 tick_b 기준으로 유지 (tg_* 는 이미 1사이클 전 캡처됨)
    (* use_dsp = "no" *) reg signed [36:0] dsp_guard_0;
    (* use_dsp = "no" *) reg signed [36:0] dsp_guard_1;

    // Stage PG: tg 그룹합 → pg (tick_b 클럭)
    reg signed [36:0] pg0, pg1, pg2, pg3, pg4, pg5, pg6;
    always @(posedge dac_clk) begin : FIR_PG
        if (safe_rst) begin
            pg0<=37'sd0; pg1<=37'sd0; pg2<=37'sd0; pg3<=37'sd0;
            pg4<=37'sd0; pg5<=37'sd0; pg6<=37'sd0;
            dsp_guard_0<=37'sd0; dsp_guard_1<=37'sd0;
        end else if (tick_b) begin
            // 4쌍 → 7 그룹 (tg_* 는 이미 등록된 값)
            pg0 <= tg_A0 + tg_A1;   // t4+t5+t6+t7
            pg1 <= tg_A2 + tg_A3;   // t8+t9+t10+t11
            pg2 <= tg_B0 + tg_B1;   // t12+t13+t14+t15
            pg3 <= tg_B2 + tg_B3;   // t16+t17+t18+t19
            pg4 <= tg_C0 + tg_C1;   // t20+t21+t22+t23
            pg5 <= tg_C2 + tg_C3;   // t24+t25+t26+t27
            pg6 <= tg_D0 + tg_D1 + tg_D2; // t28+t29+t30+tc
            dsp_guard_0 <= 37'sd0;
            dsp_guard_1 <= 37'sd0;
        end
    end

    // Stage PA: pg → pa (tick_b 클럭)
    reg signed [36:0] pa01, pa23, pa45, pa6_r;
    always @(posedge dac_clk) begin : FIR_PA
        if (safe_rst) begin
            pa01<=37'sd0; pa23<=37'sd0; pa45<=37'sd0; pa6_r<=37'sd0;
        end else if (tick_b) begin
            pa01 <= pg0 + pg1;
            pa23 <= pg2 + pg3;
            pa45 <= pg4 + pg5;
            pa6_r <= pg6;
        end
    end

    // [B] FIR_PBPC → 3단 분리: pb/pc | p5/p6 | p8
    // → p5_scaled 37bit 비교기가 별도 사이클로 분리 (LUT 감소 + WNS 개선)
    reg signed [36:0] pb0, pb1, pc_acc;
    reg signed [36:0] p5_scaled;
    reg signed [15:0] p6_sat;
    reg signed [15:0] p8_out;
    reg               p8_valid;

    always @(posedge dac_clk) begin : FIR_PBC
        if (safe_rst) begin
            pb0<=37'sd0; pb1<=37'sd0; pc_acc<=37'sd0;
        end else if (tick_b) begin
            pb0    <= pa01 + pa23;
            pb1    <= pa45 + pa6_r;
            pc_acc <= pb0  + pb1;
        end
    end

    always @(posedge dac_clk) begin : FIR_P56
        if (safe_rst) begin
            p5_scaled<=37'sd0; p6_sat<=16'sd0;
        end else if (tick_b) begin
            p5_scaled <= $signed(pc_acc) >>> 14;
            if      (p5_scaled > 37'sh0000007FFF) p6_sat <= 16'sh7FFF;
            else if (p5_scaled < -37'sh0000008000) p6_sat <= 16'sh8000;
            else                                   p6_sat <= p5_scaled[15:0];
        end
    end

    always @(posedge dac_clk) begin : FIR_P8
        if (safe_rst) begin
            p8_out<=16'sd0; p8_valid<=1'b0;
        end else if (tick_b) begin
            p8_out   <= p6_sat;
            p8_valid <= fva_pipe[7];
        end
    end

    wire signed [15:0] fir_out = p8_out;

    // =========================================================================
    // Polyphase ×4 입력 버퍼  [v13-FIX BUG-I/H]
    // =========================================================================
    reg signed [15:0] poly_sreg [0:11];
    reg signed [15:0] px_r      [0:11];
    integer pxi;

    always @(posedge dac_clk) begin : POLY_SREG
        if (safe_rst) begin
            for (pxi = 0; pxi < 12; pxi = pxi + 1) poly_sreg[pxi] <= 16'sd0;
        end else if (tick && fva_pipe[8]) begin
            poly_sreg[0]  <= p8_out;
            poly_sreg[1]  <= poly_sreg[0];  poly_sreg[2]  <= poly_sreg[1];
            poly_sreg[3]  <= poly_sreg[2];  poly_sreg[4]  <= poly_sreg[3];
            poly_sreg[5]  <= poly_sreg[4];  poly_sreg[6]  <= poly_sreg[5];
            poly_sreg[7]  <= poly_sreg[6];  poly_sreg[8]  <= poly_sreg[7];
            poly_sreg[9]  <= poly_sreg[8];  poly_sreg[10] <= poly_sreg[9];
            poly_sreg[11] <= poly_sreg[10];
        end
    end

    always @(posedge dac_clk) begin : POLY_PA
        if (safe_rst) begin
            for (pxi = 0; pxi < 12; pxi = pxi + 1) px_r[pxi] <= 16'sd0;
        end else if (pp_tick[0]) begin
            for (pxi = 0; pxi < 12; pxi = pxi + 1) px_r[pxi] <= poly_sreg[pxi];
        end
    end

    wire signed [31:0] px [0:11];
    assign px[0]  = {{16{px_r[0][15]}},  px_r[0]};
    assign px[1]  = {{16{px_r[1][15]}},  px_r[1]};
    assign px[2]  = {{16{px_r[2][15]}},  px_r[2]};
    assign px[3]  = {{16{px_r[3][15]}},  px_r[3]};
    assign px[4]  = {{16{px_r[4][15]}},  px_r[4]};
    assign px[5]  = {{16{px_r[5][15]}},  px_r[5]};
    assign px[6]  = {{16{px_r[6][15]}},  px_r[6]};
    assign px[7]  = {{16{px_r[7][15]}},  px_r[7]};
    assign px[8]  = {{16{px_r[8][15]}},  px_r[8]};
    assign px[9]  = {{16{px_r[9][15]}},  px_r[9]};
    assign px[10] = {{16{px_r[10][15]}}, px_r[10]};
    assign px[11] = {{16{px_r[11][15]}}, px_r[11]};

    // ── Phase 0 탭 곱 (33bit) ────────────────────────────────────────────────
    wire signed [32:0] p0t0  = $signed(px[0])  + ($signed(px[0])<<<2)  + ($signed(px[0])<<<5);
    wire signed [32:0] p0t1  = ($signed(px[1])<<<1) + ($signed(px[1])<<<2) + ($signed(px[1])<<<5) + ($signed(px[1])<<<7);
    wire signed [32:0] p0t2  = $signed(px[2])  + ($signed(px[2])<<<1)  + ($signed(px[2])<<<2)  + ($signed(px[2])<<<3)  + ($signed(px[2])<<<7)  + ($signed(px[2])<<<8);
    wire signed [32:0] p0t3  = $signed(px[3])  + ($signed(px[3])<<<2)  + ($signed(px[3])<<<6)  + ($signed(px[3])<<<7)  + ($signed(px[3])<<<9);
    wire signed [32:0] p0t4  = ($signed(px[4])<<<2) + ($signed(px[4])<<<10);
    wire signed [32:0] p0t5  = ($signed(px[5])<<<1) + ($signed(px[5])<<<2) + ($signed(px[5])<<<3) + ($signed(px[5])<<<5) + ($signed(px[5])<<<6) + ($signed(px[5])<<<7) + ($signed(px[5])<<<10);
    wire signed [32:0] p0t6  = $signed(px[6])  + ($signed(px[6])<<<2)  + ($signed(px[6])<<<4)  + ($signed(px[6])<<<5)  + ($signed(px[6])<<<8)  + ($signed(px[6])<<<10);
    wire signed [32:0] p0t7  = $signed(px[7])  + ($signed(px[7])<<<6)  + ($signed(px[7])<<<7)  + ($signed(px[7])<<<10);
    wire signed [32:0] p0t8  = $signed(px[8])  + ($signed(px[8])<<<3)  + ($signed(px[8])<<<4)  + ($signed(px[8])<<<5)  + ($signed(px[8])<<<7)  + ($signed(px[8])<<<8)  + ($signed(px[8])<<<9);
    wire signed [32:0] p0t9  = $signed(px[9])  + ($signed(px[9])<<<1)  + ($signed(px[9])<<<4)  + ($signed(px[9])<<<5)  + ($signed(px[9])<<<6)  + ($signed(px[9])<<<9);
    wire signed [32:0] p0t10 = $signed(px[10]) + ($signed(px[10])<<<1) + ($signed(px[10])<<<3) + ($signed(px[10])<<<6) + ($signed(px[10])<<<8);
    wire signed [32:0] p0t11 = $signed(px[11]) + ($signed(px[11])<<<2) + ($signed(px[11])<<<3) + ($signed(px[11])<<<4) + ($signed(px[11])<<<5) + ($signed(px[11])<<<6);

    // ── Phase 1 탭 곱 ────────────────────────────────────────────────────────
    wire signed [32:0] p1t0  = ($signed(px[0])<<<2) + ($signed(px[0])<<<3) + ($signed(px[0])<<<4) + ($signed(px[0])<<<5);
    wire signed [32:0] p1t1  = $signed(px[1])  + ($signed(px[1])<<<1)  + ($signed(px[1])<<<2)  + ($signed(px[1])<<<4)  + ($signed(px[1])<<<6)  + ($signed(px[1])<<<7);
    wire signed [32:0] p1t2  = $signed(px[2])  + ($signed(px[2])<<<1)  + ($signed(px[2])<<<2)  + ($signed(px[2])<<<4)  + ($signed(px[2])<<<6)  + ($signed(px[2])<<<7)  + ($signed(px[2])<<<8);
    wire signed [32:0] p1t3  = ($signed(px[3])<<<3) + ($signed(px[3])<<<4) + ($signed(px[3])<<<8) + ($signed(px[3])<<<9);
    wire signed [32:0] p1t4  = ($signed(px[4])<<<1) + ($signed(px[4])<<<3) + ($signed(px[4])<<<6) + ($signed(px[4])<<<10);
    wire signed [32:0] p1t5  = $signed(px[5])  + ($signed(px[5])<<<4)  + ($signed(px[5])<<<8)  + ($signed(px[5])<<<10);
    wire signed [32:0] p1t6  = $signed(px[6])  + ($signed(px[6])<<<3)  + ($signed(px[6])<<<5)  + ($signed(px[6])<<<8)  + ($signed(px[6])<<<10);
    wire signed [32:0] p1t7  = ($signed(px[7])<<<1) + ($signed(px[7])<<<3) + ($signed(px[7])<<<7) + ($signed(px[7])<<<10);
    wire signed [32:0] p1t8  = ($signed(px[8])<<<1) + ($signed(px[8])<<<3) + ($signed(px[8])<<<5) + ($signed(px[8])<<<6) + ($signed(px[8])<<<8) + ($signed(px[8])<<<9);
    wire signed [32:0] p1t9  = ($signed(px[9])<<<2) + ($signed(px[9])<<<5) + ($signed(px[9])<<<9);
    wire signed [32:0] p1t10 = ($signed(px[10])<<<1) + ($signed(px[10])<<<2) + ($signed(px[10])<<<3) + ($signed(px[10])<<<8);
    wire signed [32:0] p1t11 = $signed(px[11]) + ($signed(px[11])<<<3) + ($signed(px[11])<<<4) + ($signed(px[11])<<<6);

    // ── Phase 2 탭 곱 ────────────────────────────────────────────────────────
    wire signed [32:0] p2t0  = $signed(px[0])  + ($signed(px[0])<<<3)  + ($signed(px[0])<<<4)  + ($signed(px[0])<<<6);
    wire signed [32:0] p2t1  = ($signed(px[1])<<<1) + ($signed(px[1])<<<2) + ($signed(px[1])<<<3) + ($signed(px[1])<<<8);
    wire signed [32:0] p2t2  = ($signed(px[2])<<<2) + ($signed(px[2])<<<5) + ($signed(px[2])<<<9);
    wire signed [32:0] p2t3  = ($signed(px[3])<<<1) + ($signed(px[3])<<<3) + ($signed(px[3])<<<5) + ($signed(px[3])<<<6) + ($signed(px[3])<<<8) + ($signed(px[3])<<<9);
    wire signed [32:0] p2t4  = ($signed(px[4])<<<1) + ($signed(px[4])<<<3) + ($signed(px[4])<<<7) + ($signed(px[4])<<<10);
    wire signed [32:0] p2t5  = $signed(px[5])  + ($signed(px[5])<<<3)  + ($signed(px[5])<<<5)  + ($signed(px[5])<<<8)  + ($signed(px[5])<<<10);
    wire signed [32:0] p2t6  = $signed(px[6])  + ($signed(px[6])<<<4)  + ($signed(px[6])<<<8)  + ($signed(px[6])<<<10);
    wire signed [32:0] p2t7  = ($signed(px[7])<<<1) + ($signed(px[7])<<<3) + ($signed(px[7])<<<6) + ($signed(px[7])<<<10);
    wire signed [32:0] p2t8  = ($signed(px[8])<<<3) + ($signed(px[8])<<<4) + ($signed(px[8])<<<8) + ($signed(px[8])<<<9);
    wire signed [32:0] p2t9  = $signed(px[9])  + ($signed(px[9])<<<1)  + ($signed(px[9])<<<2)  + ($signed(px[9])<<<4)  + ($signed(px[9])<<<6)  + ($signed(px[9])<<<7)  + ($signed(px[9])<<<8);
    wire signed [32:0] p2t10 = $signed(px[10]) + ($signed(px[10])<<<1) + ($signed(px[10])<<<2) + ($signed(px[10])<<<4) + ($signed(px[10])<<<6) + ($signed(px[10])<<<7);
    wire signed [32:0] p2t11 = ($signed(px[11])<<<2) + ($signed(px[11])<<<3) + ($signed(px[11])<<<4) + ($signed(px[11])<<<5);

    // ── Phase 3 탭 곱 ────────────────────────────────────────────────────────
    wire signed [32:0] p3t0  = $signed(px[0])  + ($signed(px[0])<<<2)  + ($signed(px[0])<<<3)  + ($signed(px[0])<<<4)  + ($signed(px[0])<<<5)  + ($signed(px[0])<<<6);
    wire signed [32:0] p3t1  = $signed(px[1])  + ($signed(px[1])<<<1)  + ($signed(px[1])<<<3)  + ($signed(px[1])<<<6)  + ($signed(px[1])<<<8);
    wire signed [32:0] p3t2  = $signed(px[2])  + ($signed(px[2])<<<1)  + ($signed(px[2])<<<4)  + ($signed(px[2])<<<5)  + ($signed(px[2])<<<6)  + ($signed(px[2])<<<9);
    wire signed [32:0] p3t3  = $signed(px[3])  + ($signed(px[3])<<<3)  + ($signed(px[3])<<<4)  + ($signed(px[3])<<<5)  + ($signed(px[3])<<<7)  + ($signed(px[3])<<<8)  + ($signed(px[3])<<<9);
    wire signed [32:0] p3t4  = $signed(px[4])  + ($signed(px[4])<<<6)  + ($signed(px[4])<<<7)  + ($signed(px[4])<<<10);
    wire signed [32:0] p3t5  = $signed(px[5])  + ($signed(px[5])<<<2)  + ($signed(px[5])<<<4)  + ($signed(px[5])<<<5)  + ($signed(px[5])<<<8)  + ($signed(px[5])<<<10);
    wire signed [32:0] p3t6  = ($signed(px[6])<<<1) + ($signed(px[6])<<<2) + ($signed(px[6])<<<3) + ($signed(px[6])<<<5) + ($signed(px[6])<<<6) + ($signed(px[6])<<<7) + ($signed(px[6])<<<10);
    wire signed [32:0] p3t7  = ($signed(px[7])<<<2) + ($signed(px[7])<<<10);
    wire signed [32:0] p3t8  = $signed(px[8])  + ($signed(px[8])<<<2)  + ($signed(px[8])<<<6)  + ($signed(px[8])<<<7)  + ($signed(px[8])<<<9);
    wire signed [32:0] p3t9  = $signed(px[9])  + ($signed(px[9])<<<1)  + ($signed(px[9])<<<2)  + ($signed(px[9])<<<3)  + ($signed(px[9])<<<7)  + ($signed(px[9])<<<8);
    wire signed [32:0] p3t10 = ($signed(px[10])<<<1) + ($signed(px[10])<<<2) + ($signed(px[10])<<<5) + ($signed(px[10])<<<7);
    wire signed [32:0] p3t11 = $signed(px[11]) + ($signed(px[11])<<<2) + ($signed(px[11])<<<5);

    // ── [파이프라인 추가 2] Polyphase 탭곱 중간합 레지스터 (PP_PA_REG) ────────
    // 목적: px_r[] → p*t* → POLY_PB의 콤비네이션 깊이를 2사이클로 분할
    // 각 Phase의 t0..t5(lo), t6..t11(hi) 를 별도 FF로 캡처
    // 추가 FF: 37bit × 8개 × 4phase × 2ch = 2368FF
    // (예산 3000FF 중 FIR_TAP_PIPE 592FF + 이 블록 2368FF = 2960FF)
    `define SE37(x) $signed({{4{x[32]}},x})
    reg signed [36:0] pp_p0lo, pp_p0hi;
    reg signed [36:0] pp_p1lo, pp_p1hi;
    reg signed [36:0] pp_p2lo, pp_p2hi;
    reg signed [36:0] pp_p3lo, pp_p3hi;

    always @(posedge dac_clk) begin : POLY_PA_REG
        if (safe_rst) begin
            pp_p0lo<=37'sd0; pp_p0hi<=37'sd0;
            pp_p1lo<=37'sd0; pp_p1hi<=37'sd0;
            pp_p2lo<=37'sd0; pp_p2hi<=37'sd0;
            pp_p3lo<=37'sd0; pp_p3hi<=37'sd0;
        end else begin
            pp_p0lo <= `SE37(p0t0)+`SE37(p0t1)+`SE37(p0t2)+`SE37(p0t3)+`SE37(p0t4)+`SE37(p0t5);
            pp_p0hi <= `SE37(p0t6)+`SE37(p0t7)+`SE37(p0t8)+`SE37(p0t9)+`SE37(p0t10)+`SE37(p0t11);
            pp_p1lo <= `SE37(p1t0)+`SE37(p1t1)+`SE37(p1t2)+`SE37(p1t3)+`SE37(p1t4)+`SE37(p1t5);
            pp_p1hi <= `SE37(p1t6)+`SE37(p1t7)+`SE37(p1t8)+`SE37(p1t9)+`SE37(p1t10)+`SE37(p1t11);
            pp_p2lo <= `SE37(p2t0)+`SE37(p2t1)+`SE37(p2t2)+`SE37(p2t3)+`SE37(p2t4)+`SE37(p2t5);
            pp_p2hi <= `SE37(p2t6)+`SE37(p2t7)+`SE37(p2t8)+`SE37(p2t9)+`SE37(p2t10)+`SE37(p2t11);
            pp_p3lo <= `SE37(p3t0)+`SE37(p3t1)+`SE37(p3t2)+`SE37(p3t3)+`SE37(p3t4)+`SE37(p3t5);
            pp_p3hi <= `SE37(p3t6)+`SE37(p3t7)+`SE37(p3t8)+`SE37(p3t9)+`SE37(p3t10)+`SE37(p3t11);
        end
    end

    // [C] POLY_PB 패스스루 FF 삭제 → POLY_PC가 pp_p*lo/hi 직접 참조
    // ── PC: lo+hi 합산 FF ────────────────────────────────────────────────────
    reg signed [36:0] p0_mac_r, p1_mac_r, p2_mac_r, p3_mac_r;
    always @(posedge dac_clk) begin : POLY_PC
        if (safe_rst) begin
            p0_mac_r<=37'sd0; p1_mac_r<=37'sd0;
            p2_mac_r<=37'sd0; p3_mac_r<=37'sd0;
        end else begin
            p0_mac_r <= pp_p0lo + pp_p0hi;
            p1_mac_r <= pp_p1lo + pp_p1hi;
            p2_mac_r <= pp_p2lo + pp_p2hi;
            p3_mac_r <= pp_p3lo + pp_p3hi;
        end
    end

    // [E] p*_sat wire 비교기 → pp_sat_r 16bit 등록 레지스터
    // → pp_out_r 직전 37bit 팬인 비교 LUT 제거 (LUT 절감 + WNS 개선)
    wire signed [36:0] p0_sh = $signed(p0_mac_r) >>> 13;
    wire signed [36:0] p1_sh = $signed(p1_mac_r) >>> 13;
    wire signed [36:0] p2_sh = $signed(p2_mac_r) >>> 13;
    wire signed [36:0] p3_sh = $signed(p3_mac_r) >>> 13;

    reg signed [15:0] pp_sat_r [0:3];
    always @(posedge dac_clk) begin : POLY_SAT
        if (safe_rst) begin
            pp_sat_r[0]<=16'sd0; pp_sat_r[1]<=16'sd0;
            pp_sat_r[2]<=16'sd0; pp_sat_r[3]<=16'sd0;
        end else begin
            pp_sat_r[0] <= ($signed(p0_sh) > 37'sh0000007FFF) ? 16'sh7FFF :
                           ($signed(p0_sh) < -37'sh0000008000) ? 16'sh8000 : p0_sh[15:0];
            pp_sat_r[1] <= ($signed(p1_sh) > 37'sh0000007FFF) ? 16'sh7FFF :
                           ($signed(p1_sh) < -37'sh0000008000) ? 16'sh8000 : p1_sh[15:0];
            pp_sat_r[2] <= ($signed(p2_sh) > 37'sh0000007FFF) ? 16'sh7FFF :
                           ($signed(p2_sh) < -37'sh0000008000) ? 16'sh8000 : p2_sh[15:0];
            pp_sat_r[3] <= ($signed(p3_sh) > 37'sh0000007FFF) ? 16'sh7FFF :
                           ($signed(p3_sh) < -37'sh0000008000) ? 16'sh8000 : p3_sh[15:0];
        end
    end

    reg signed [15:0] pp_out_r [0:3];
    always @(posedge dac_clk) begin
        if (safe_rst) begin
            pp_out_r[0]<=16'sd0; pp_out_r[1]<=16'sd0;
            pp_out_r[2]<=16'sd0; pp_out_r[3]<=16'sd0;
        end else begin
            if (pp_tick[0] && fir_valid) pp_out_r[0] <= pp_sat_r[0];
            if (pp_tick[1] && fir_valid) pp_out_r[1] <= pp_sat_r[1];
            if (pp_tick[2] && fir_valid) pp_out_r[2] <= pp_sat_r[2];
            if (pp_tick[3] && fir_valid) pp_out_r[3] <= pp_sat_r[3];
        end
    end

    wire signed [15:0] pp_cic_in =
        pp_tick_b[0] ? pp_out_r[0] :
        pp_tick_b[1] ? pp_out_r[1] :
        pp_tick_b[2] ? pp_out_r[2] :
        pp_tick_b[3] ? pp_out_r[3] : 16'sd0;
    wire pp_any_tick_b = |pp_tick_b;

    // =========================================================================
    // CIC 콤 3단
    // =========================================================================
    wire signed [34:0] pp_ext = {{19{pp_cic_in[15]}}, pp_cic_in};
    reg signed [34:0] comb_dly0, comb_dly1, comb_dly2;
    reg signed [34:0] comb_y0, comb_y1, comb_y2;

    always @(posedge dac_clk) begin
        if (safe_rst) begin
            comb_dly0<=35'sd0; comb_y0<=35'sd0;
            comb_dly1<=35'sd0; comb_y1<=35'sd0;
            comb_dly2<=35'sd0; comb_y2<=35'sd0;
        end else if (pp_any_tick_b) begin
            comb_y0   <= pp_ext  - comb_dly0; comb_dly0 <= pp_ext;
            comb_y1   <= comb_y0 - comb_dly1; comb_dly1 <= comb_y0;
            comb_y2   <= comb_y1 - comb_dly2; comb_dly2 <= comb_y1;
        end
    end

    reg signed [34:0] comb_out_r;
    always @(posedge dac_clk) begin
        if (safe_rst) comb_out_r <= 35'sd0;
        else if (|pp_tick_4m) comb_out_r <= comb_y2;
    end

    // =========================================================================
    // CIC 적분기 3단
    // =========================================================================
    wire signed [34:0] integ_in = (|pp_tick_4m) ? comb_out_r : 35'sd0;
    reg signed [34:0] integ0, integ1, integ2;

    always @(posedge dac_clk) begin
        if (safe_rst) begin
            integ0<=35'sd0; integ1<=35'sd0; integ2<=35'sd0;
        end else begin
            integ0 <= integ0 + integ_in;
            integ1 <= integ1 + integ0;
            integ2 <= integ2 + integ1;
        end
    end

    // =========================================================================
    // CIC Gain 보정  [v11-GA/GB/GC + v13-FIX BUG-J]
    // =========================================================================
    wire signed [53:0] i2e54 = {{19{integ2[34]}}, integ2};

    // K_new = 26,511,334
    wire signed [53:0] gk_t24 = $signed(i2e54) <<< 24;
    wire signed [53:0] gk_t23 = $signed(i2e54) <<< 23;
    wire signed [53:0] gk_t20 = $signed(i2e54) <<< 20;
    wire signed [53:0] gk_t18 = $signed(i2e54) <<< 18;
    wire signed [53:0] gk_t15 = $signed(i2e54) <<< 15;
    wire signed [53:0] gk_t10 = $signed(i2e54) <<< 10;
    wire signed [53:0] gk_t9  = $signed(i2e54) <<<  9;
    wire signed [53:0] gk_t8  = $signed(i2e54) <<<  8;
    wire signed [53:0] gk_t7  = $signed(i2e54) <<<  7;
    wire signed [53:0] gk_t6  = $signed(i2e54) <<<  6;
    wire signed [53:0] gk_t5  = $signed(i2e54) <<<  5;
    wire signed [53:0] gk_t2  = $signed(i2e54) <<<  2;
    wire signed [53:0] gk_t1  = $signed(i2e54) <<<  1;

    reg signed [53:0] gain_hi_r, gain_lo_r;
    always @(posedge dac_clk) begin : GAIN_GA
        if (safe_rst) begin
            gain_hi_r <= 54'sd0; gain_lo_r <= 54'sd0;
        end else begin
            gain_hi_r <= $signed(gk_t24)+$signed(gk_t23)+$signed(gk_t20)
                        +$signed(gk_t18)+$signed(gk_t15)+$signed(gk_t10)
                        +$signed(gk_t9);
            gain_lo_r <= $signed(gk_t8)+$signed(gk_t7)+$signed(gk_t6)
                        +$signed(gk_t5)+$signed(gk_t2)+$signed(gk_t1)
                        +$signed(i2e54);
        end
    end

    reg signed [53:0] gain_mid_r;
    always @(posedge dac_clk) begin : GAIN_GB
        if (safe_rst) gain_mid_r <= 54'sd0;
        else          gain_mid_r <= gain_hi_r + gain_lo_r;
    end

    reg signed [53:0] gain_shifted_r;
    always @(posedge dac_clk) begin : GAIN_GC
        if (safe_rst) gain_shifted_r <= 54'sd0;
        else          gain_shifted_r <= $signed(gain_mid_r) >>> CIC_S;
    end

    // =========================================================================
    // NSD 2차  [v12: PATH-2 완전 분리, v13-FIX BUG-K]
    // =========================================================================
    reg [15:0] lfsr_a, lfsr_b;
    wire lfsr_a_fb = lfsr_a[15] ^ lfsr_a[14] ^ lfsr_a[12] ^ lfsr_a[3];
    wire lfsr_b_fb = lfsr_b[15] ^ lfsr_b[14] ^ lfsr_b[12] ^ lfsr_b[3];

    localparam [15:0] LFSR_A_SEED = (ch == 0) ? 16'hACE1 : 16'h57C4;
    localparam [15:0] LFSR_B_SEED = (ch == 0) ? 16'h3527 : 16'hB9A2;

    always @(posedge dac_clk) begin
        if (safe_rst) begin
            lfsr_a <= LFSR_A_SEED; lfsr_b <= LFSR_B_SEED;
        end else begin
            lfsr_a <= {lfsr_a[14:0], lfsr_a_fb};
            lfsr_b <= {lfsr_b[14:0], lfsr_b_fb};
        end
    end

    wire signed [53:0] nsd_raw =
        $signed({{52{1'b0}}, lfsr_a[1:0]}) - $signed({{52{1'b0}}, lfsr_b[1:0]});

    reg signed [15:0] nsd_err_d1, nsd_err_d2;
    reg signed [53:0] nsd_shaped_r;

    always @(posedge dac_clk) begin : NSD_NA
        if (safe_rst) nsd_shaped_r <= 54'sd0;
        else nsd_shaped_r <=  $signed(nsd_raw)
                             - ($signed({{38{nsd_err_d1[15]}}, nsd_err_d1}) <<< 1)
                             + $signed({{38{nsd_err_d2[15]}}, nsd_err_d2});
    end

    // [D] gain_shifted_d 54bit 지연 FF 삭제 → NSD_NB에서 gain_shifted_r 직접 참조
    reg signed [53:0] gain_dithered_r;
    always @(posedge dac_clk) begin : NSD_NB
        if (safe_rst) gain_dithered_r <= 54'sd0;
        else gain_dithered_r <= $signed(gain_shifted_r) + $signed(nsd_shaped_r);
    end

    wire signed [15:0] nsd_sat16 =
        ($signed(gain_dithered_r) > 54'sh0000000007FFF) ? 16'sh7FFF :
        ($signed(gain_dithered_r) < -54'sh0000000008000) ? 16'sh8000 :
        gain_dithered_r[15:0];

    wire signed [16:0] err17 = $signed(gain_dithered_r[16:0]) - $signed({nsd_sat16[15], nsd_sat16});
    wire signed [15:0] nsd_err_next =
        (err17 > 17'sh7FFF)  ? 16'sh7FFF  :
        (err17 < -17'sh8000) ? 16'sh8000  :
        err17[15:0];

    always @(posedge dac_clk) begin
        if (safe_rst) begin
            nsd_err_d1 <= 16'sd0; nsd_err_d2 <= 16'sd0;
        end else begin
            nsd_err_d2 <= nsd_err_d1;
            nsd_err_d1 <= nsd_err_next;
        end
    end

    // =========================================================================
    // GUARD_MAX clamp + cic_sat_r
    // =========================================================================
    wire signed [15:0] guard_pos = $signed(GUARD_MAX[15:0]);
    wire signed [15:0] guard_neg = -$signed(GUARD_MAX[15:0]);

    wire signed [15:0] cic_sat_w =
        (nsd_sat16 > guard_pos) ? guard_pos :
        (nsd_sat16 < guard_neg) ? guard_neg :
        nsd_sat16;

    reg signed [15:0] cic_sat_r;
    always @(posedge dac_clk) begin
        if (safe_rst) cic_sat_r <= 16'sd0;
        else          cic_sat_r <= cic_sat_w;
    end

    // =========================================================================
    // dac2 인스턴스
    // =========================================================================
    dac2 #(.width(WIDTH)) u_dac2 (
        .clk  (dac_clk),
        .rst  (safe_rst),
        .din  (cic_sat_r),
        .dout (dac_out_ch[ch])
    );

end // CHANNEL generate
endgenerate

endmodule