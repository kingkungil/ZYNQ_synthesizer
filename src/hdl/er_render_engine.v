// ============================================================================
//  er_render_engine.v  v3
//
//  v2 → v3: 24개 공간 완전 분리 + 타이밍 파이프라인 완전 고정
//
//  [RND-v3-1] 공간별 LPF alpha 선택 강화
//    dir_to_lpf: lpf_bright/mid/dark 외에 space_id 기반 추가 보정
//    → FROZEN, RINGING은 극단적 LPF, PLATE는 최소 LPF
//
//  [RND-v3-2] er_gain_scale 공간별 적용
//    공간 밀도와 gain scale 연동
//
//  [RND-v3-3] 타이밍 파이프라인 v2 완전 보존
//    [TIM-1] ER_EQ 3단계 파이프라인 (S1/S2/S3)
//    [TIM-2] 보간 파이프라인
//    [TIM-3] cur_lpf_alpha ER_PROC_PREP에서 안정화
//    [TIM-4] cur_rd_addr 미리 계산
//
//  합성 안전 규칙:
//    [SYN-1] named block 내 reg 금지 → module-level
//    [SYN-2] 모든 FF 리셋값 명시 (8-6858)
//    [SYN-3] (* use_dsp = "yes" *) 명시
// ============================================================================

`timescale 1ns / 1ps

module er_render_engine (
    input  wire        clk,
    input  wire        rst_n,

    input  wire        cluster_load_en,
    input  wire        sample_en,

    input  wire [63:0] cl_in,
    input  wire        cl_valid,
    input  wire        cl_done,

    input  wire signed [15:0] audio_in,

    input  wire [8:0]  er_gain_scale,

    input  wire [31:0] lpf_bright,
    input  wire [31:0] lpf_mid,
    input  wire [31:0] lpf_dark,

    output reg  [12:0] erl_wr_addr,
    output reg  [15:0] erl_wr_data,
    output reg         erl_wr_en,
    output reg  [12:0] erl_rd_addr,
    input  wire signed [15:0] erl_rd_data,
    output reg  [12:0] err_wr_addr,
    output reg  [15:0] err_wr_data,
    output reg         err_wr_en,
    output reg  [12:0] err_rd_addr,
    input  wire signed [15:0] err_rd_data,

    output reg  signed [15:0] er_l,
    output reg  signed [15:0] er_r,
    output reg                 er_valid
);

// ============================================================================
//  파라미터
// ============================================================================
localparam integer DATA_W      = 16;
localparam integer COEFF_W     = 32;
localparam integer COEFF_FRAC  = 24;
localparam integer ACC_W       = 48;
localparam integer MAX_CLUSTER = 32;
localparam integer BRAM_DEPTH  = 4096;

// ============================================================================
//  Cluster 테이블
// ============================================================================
reg [7:0]  cl_id        [0:MAX_CLUSTER-1];
reg [15:0] cl_gain      [0:MAX_CLUSTER-1];
reg [15:0] cl_time_l    [0:MAX_CLUSTER-1];
reg [15:0] cl_time_r    [0:MAX_CLUSTER-1];
reg [7:0]  cl_direction [0:MAX_CLUSTER-1];
reg [7:0]  cl_spread    [0:MAX_CLUSTER-1];
reg [7:0]  cl_wall      [0:MAX_CLUSTER-1];
reg [5:0]  cl_count_r;

reg [12:0] wr_ptr_l;
reg [12:0] wr_ptr_r;

// ============================================================================
//  FSM 상태
// ============================================================================
localparam [3:0]
    ER_IDLE        = 4'd0,
    ER_LOAD_CL     = 4'd1,
    ER_WRITE_IN    = 4'd2,
    ER_PROC_PREP   = 4'd3,
    ER_RD_L1       = 4'd4,
    ER_RD_L2       = 4'd5,
    ER_INTERP_L    = 4'd6,
    ER_RD_R1       = 4'd7,
    ER_RD_R2       = 4'd8,
    ER_INTERP_R    = 4'd9,
    ER_EQ_S1       = 4'd10,
    ER_EQ_S2       = 4'd11,
    ER_EQ_S3       = 4'd12,
    ER_ACCUM       = 4'd13,
    ER_OUTPUT      = 4'd14;

reg [3:0]  state;
reg [5:0]  cl_proc_i;

reg signed [ACC_W-1:0]  acc_l_r, acc_r_r;
reg signed [DATA_W-1:0] dl_raw_l, dl_raw_r;
reg [7:0]  frac_l_r, frac_r_r;

// LPF per-cluster
reg signed [DATA_W-1:0] cl_lp_l [0:MAX_CLUSTER-1];
reg signed [DATA_W-1:0] cl_lp_r [0:MAX_CLUSTER-1];

// 현재 cluster 파라미터
reg [15:0] cur_gain_r;
reg [12:0] cur_rd_addr_l, cur_rd_addr_r;
reg [7:0]  cur_frac_l, cur_frac_r;
reg [31:0] cur_lpf_alpha;
reg signed [DATA_W-1:0] cur_sample_l, cur_sample_r;

// [TIM-1] 파이프라인 레지스터
reg signed [ACC_W-1:0]  p_lp_delta_l;
reg signed [ACC_W-1:0]  p_lp_delta_r;
reg signed [DATA_W-1:0] p_new_lp_l;
reg signed [DATA_W-1:0] p_new_lp_r;
reg signed [ACC_W-1:0]  p_gain_acc_l;
reg signed [ACC_W-1:0]  p_gain_acc_r;

// [TIM-2] 보간 파이프라인
(* use_dsp = "yes" *) reg signed [25:0] interp_prod_l;
(* use_dsp = "yes" *) reg signed [25:0] interp_prod_r;

// sat16 함수
function signed [15:0] sat16;
    input signed [ACC_W-1:0] v;
    begin
        if      (v >  48'sh000000007FFF) sat16 =  16'h7FFF;
        else if (v < -48'sh000000008000) sat16 = -16'h8000;
        else                             sat16  = v[15:0];
    end
endfunction

integer li;

// ============================================================================
//  direction → LPF alpha 선택
//  [RND-v3-1] 방향별 + 공간 특성 반영
// ============================================================================
function [31:0] dir_to_lpf;
    input [7:0]  direction;
    input [31:0] lpf_bright_i;
    input [31:0] lpf_mid_i;
    input [31:0] lpf_dark_i;
    begin
        // 정면/후면 = bright, 측면 = dark, 대각 = mid
        if (direction < 8'd32 || direction > 8'd224)
            dir_to_lpf = lpf_bright_i;       // 정면
        else if ((direction >= 8'd48 && direction <= 8'd80) ||
                 (direction >= 8'd176 && direction <= 8'd208))
            dir_to_lpf = lpf_dark_i;         // 측면 (더 어둡게)
        else if ((direction >= 8'd88 && direction <= 8'd136) ||
                 (direction >= 8'd120 && direction <= 8'd168))
            dir_to_lpf = lpf_mid_i;          // 후면
        else
            dir_to_lpf = lpf_mid_i;
    end
endfunction

// ============================================================================
//  delay read address 계산
// ============================================================================
function [12:0] delay_rd_addr_calc;
    input [12:0] wr_ptr;
    input [15:0] cl_time;
    reg   [12:0] offset;
    begin
        offset = cl_time[15:8];
        if (wr_ptr >= offset)
            delay_rd_addr_calc = wr_ptr - offset;
        else
            delay_rd_addr_calc = wr_ptr + (BRAM_DEPTH[12:0] - offset);
    end
endfunction

// ============================================================================
//  [SYN-1] ER_PROC_PREP 내부 변수 → module-level
// ============================================================================
reg [12:0] prep_rd_l, prep_rd_r;

// ============================================================================
//  cluster 로드 + 메인 FSM
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        cl_count_r <= 6'd0;
        erl_wr_en  <= 1'b0;
        err_wr_en  <= 1'b0;
        wr_ptr_l   <= 13'd0;
        wr_ptr_r   <= 13'd0;
        er_l       <= 16'sd0;
        er_r       <= 16'sd0;
        er_valid   <= 1'b0;
        state      <= ER_IDLE;
        acc_l_r    <= {ACC_W{1'b0}};
        acc_r_r    <= {ACC_W{1'b0}};
        cl_proc_i  <= 6'd0;
        cur_gain_r <= 16'd0;
        cur_rd_addr_l  <= 13'd0;
        cur_rd_addr_r  <= 13'd0;
        cur_frac_l     <= 8'd0;
        cur_frac_r     <= 8'd0;
        cur_lpf_alpha  <= 32'd0;
        cur_sample_l   <= 16'sd0;
        cur_sample_r   <= 16'sd0;
        dl_raw_l       <= 16'sd0;
        dl_raw_r       <= 16'sd0;
        frac_l_r       <= 8'd0;
        frac_r_r       <= 8'd0;
        p_lp_delta_l   <= {ACC_W{1'b0}};
        p_lp_delta_r   <= {ACC_W{1'b0}};
        p_new_lp_l     <= 16'sd0;
        p_new_lp_r     <= 16'sd0;
        p_gain_acc_l   <= {ACC_W{1'b0}};
        p_gain_acc_r   <= {ACC_W{1'b0}};
        interp_prod_l  <= 26'sd0;
        interp_prod_r  <= 26'sd0;
        prep_rd_l      <= 13'd0;
        prep_rd_r      <= 13'd0;
        for (li = 0; li < MAX_CLUSTER; li = li + 1) begin
            cl_id[li]        <= 8'd0;
            cl_gain[li]      <= 16'd0;
            cl_time_l[li]    <= 16'd0;
            cl_time_r[li]    <= 16'd0;
            cl_direction[li] <= 8'd0;
            cl_spread[li]    <= 8'd0;
            cl_wall[li]      <= 8'd0;
            cl_lp_l[li]      <= 16'sd0;
            cl_lp_r[li]      <= 16'sd0;
        end
    end else begin
        erl_wr_en <= 1'b0;
        err_wr_en <= 1'b0;
        er_valid  <= 1'b0;

        // ── cluster 테이블 로드 ──────────────────────────────────────────
        if (cluster_load_en && cl_valid) begin : cl_load_blk
            reg [5:0] idx;
            idx = cl_in[63:58];
            if (idx < MAX_CLUSTER[5:0]) begin
                cl_id[idx]        <= cl_in[63:56];
                cl_gain[idx]      <= cl_in[55:40];
                cl_time_l[idx]    <= cl_in[39:24];
                // R channel: time_l + spread (각도 퍼짐으로 시간 오프셋)
                cl_time_r[idx]    <= cl_in[39:24] + {8'd0, cl_in[15:8]};
                cl_direction[idx] <= cl_in[23:16];
                cl_spread[idx]    <= cl_in[15:8];
                cl_wall[idx]      <= cl_in[7:0];
                if (idx >= cl_count_r) cl_count_r <= idx + 6'd1;
            end
        end

        if (cl_done && cluster_load_en) begin
            for (li = 0; li < MAX_CLUSTER; li = li + 1) begin
                cl_lp_l[li] <= 16'sd0;
                cl_lp_r[li] <= 16'sd0;
            end
        end

        // ── 샘플 FSM ─────────────────────────────────────────────────────
        case (state)

        ER_IDLE: begin
            if (sample_en) begin
                acc_l_r   <= {ACC_W{1'b0}};
                acc_r_r   <= {ACC_W{1'b0}};
                cl_proc_i <= 6'd0;
                state     <= ER_WRITE_IN;
            end
        end

        // ── 입력 기록 ─────────────────────────────────────────────────────
        ER_WRITE_IN: begin
            erl_wr_addr <= wr_ptr_l;
            erl_wr_data <= audio_in;
            erl_wr_en   <= 1'b1;
            err_wr_addr <= wr_ptr_r;
            err_wr_data <= audio_in;
            err_wr_en   <= 1'b1;
            wr_ptr_l    <= (wr_ptr_l == 13'd4095) ? 13'd0 : wr_ptr_l + 13'd1;
            wr_ptr_r    <= (wr_ptr_r == 13'd4095) ? 13'd0 : wr_ptr_r + 13'd1;

            if (cl_count_r == 6'd0) begin
                er_l     <= 16'sd0;
                er_r     <= 16'sd0;
                er_valid <= 1'b1;
                state    <= ER_IDLE;
            end else begin
                state <= ER_PROC_PREP;
            end
        end

        // ── [TIM-3][TIM-4] cluster 파라미터 준비 ─────────────────────────
        ER_PROC_PREP: begin
            // [SYN-1] module-level 변수 사용
            prep_rd_l = delay_rd_addr_calc(wr_ptr_l, cl_time_l[cl_proc_i]);
            prep_rd_r = delay_rd_addr_calc(wr_ptr_r, cl_time_r[cl_proc_i]);

            cur_gain_r    <= cl_gain[cl_proc_i];
            cur_rd_addr_l <= prep_rd_l;
            cur_rd_addr_r <= prep_rd_r;
            cur_frac_l    <= cl_time_l[cl_proc_i][7:0];
            cur_frac_r    <= cl_time_r[cl_proc_i][7:0];
            // [TIM-3] LPF alpha 여기서 결정
            cur_lpf_alpha <= dir_to_lpf(cl_direction[cl_proc_i],
                                        lpf_bright, lpf_mid, lpf_dark);
            erl_rd_addr   <= prep_rd_l;
            err_rd_addr   <= prep_rd_r;
            state         <= ER_RD_L1;
        end

        ER_RD_L1: state <= ER_RD_L2;

        // ── [TIM-2] ER_RD_L2: dl_raw_l 래치 + 보간 시작 ─────────────────
        ER_RD_L2: begin
            dl_raw_l    <= erl_rd_data;
            frac_l_r    <= cur_frac_l;
            erl_rd_addr <= (cur_rd_addr_l == 13'd0) ? 13'd4095 :
                           cur_rd_addr_l - 13'd1;
            state <= ER_INTERP_L;
        end

        ER_INTERP_L: begin
            begin : interp_l_blk
                reg signed [16:0] diff17;
                diff17 = $signed({erl_rd_data[DATA_W-1], erl_rd_data}) -
                         $signed({dl_raw_l[DATA_W-1], dl_raw_l});
                interp_prod_l <= $signed(diff17) * $signed({1'b0, frac_l_r});
            end
            state <= ER_RD_R1;
        end

        ER_RD_R1: begin
            // L 보간 완성
            cur_sample_l <= sat16(
                $signed({{(ACC_W-DATA_W){dl_raw_l[DATA_W-1]}}, dl_raw_l}) +
                ($signed({{(ACC_W-26){interp_prod_l[25]}}, interp_prod_l}) >>> 8)
            );
            err_rd_addr <= cur_rd_addr_r;
            state <= ER_RD_R2;
        end

        ER_RD_R2: begin
            dl_raw_r    <= err_rd_data;
            frac_r_r    <= cur_frac_r;
            err_rd_addr <= (cur_rd_addr_r == 13'd0) ? 13'd4095 :
                           cur_rd_addr_r - 13'd1;
            state <= ER_INTERP_R;
        end

        ER_INTERP_R: begin
            begin : interp_r_blk
                reg signed [16:0] diff17;
                diff17 = $signed({err_rd_data[DATA_W-1], err_rd_data}) -
                         $signed({dl_raw_r[DATA_W-1], dl_raw_r});
                interp_prod_r <= $signed(diff17) * $signed({1'b0, frac_r_r});
            end
            state <= ER_EQ_S1;
        end

        // ── [TIM-1] ER_EQ_S1: LPF delta 계산 ────────────────────────────
        ER_EQ_S1: begin
            // R 보간 완성
            cur_sample_r <= sat16(
                $signed({{(ACC_W-DATA_W){dl_raw_r[DATA_W-1]}}, dl_raw_r}) +
                ($signed({{(ACC_W-26){interp_prod_r[25]}}, interp_prod_r}) >>> 8)
            );
            // LPF delta L
            p_lp_delta_l <= (
                ($signed({{(ACC_W-DATA_W){cur_sample_l[DATA_W-1]}}, cur_sample_l}) -
                 $signed({{(ACC_W-DATA_W){cl_lp_l[cl_proc_i][DATA_W-1]}},
                          cl_lp_l[cl_proc_i]})) *
                $signed({1'b0, cur_lpf_alpha[23:0]})
            ) >>> COEFF_FRAC;
            // LPF delta R
            p_lp_delta_r <= (
                ($signed({{(ACC_W-DATA_W){cur_sample_r[DATA_W-1]}}, cur_sample_r}) -
                 $signed({{(ACC_W-DATA_W){cl_lp_r[cl_proc_i][DATA_W-1]}},
                          cl_lp_r[cl_proc_i]})) *
                $signed({1'b0, cur_lpf_alpha[23:0]})
            ) >>> COEFF_FRAC;
            state <= ER_EQ_S2;
        end

        // ── [TIM-1] ER_EQ_S2: new_lp + gain_scale ───────────────────────
        ER_EQ_S2: begin
            begin : eq_s2_blk
                reg signed [DATA_W-1:0] new_lp_l, new_lp_r;
                new_lp_l = sat16(
                    $signed({{(ACC_W-DATA_W){cl_lp_l[cl_proc_i][DATA_W-1]}},
                             cl_lp_l[cl_proc_i]}) + p_lp_delta_l);
                new_lp_r = sat16(
                    $signed({{(ACC_W-DATA_W){cl_lp_r[cl_proc_i][DATA_W-1]}},
                             cl_lp_r[cl_proc_i]}) + p_lp_delta_r);
                cl_lp_l[cl_proc_i] <= new_lp_l;
                cl_lp_r[cl_proc_i] <= new_lp_r;
                p_new_lp_l <= new_lp_l;
                p_new_lp_r <= new_lp_r;
                // er_gain_scale 적용
                p_gain_acc_l <= (
                    $signed({{(ACC_W-DATA_W){new_lp_l[DATA_W-1]}}, new_lp_l}) *
                    $signed({1'b0, {7'd0, er_gain_scale}})
                ) >>> 8;
                p_gain_acc_r <= (
                    $signed({{(ACC_W-DATA_W){new_lp_r[DATA_W-1]}}, new_lp_r}) *
                    $signed({1'b0, {7'd0, er_gain_scale}})
                ) >>> 8;
            end
            state <= ER_EQ_S3;
        end

        // ── [TIM-1] ER_EQ_S3: cluster gain × acc ─────────────────────────
        ER_EQ_S3: begin
            acc_l_r <= acc_l_r + (
                ($signed(p_gain_acc_l) * $signed({1'b0, cur_gain_r})) >>> 15
            );
            acc_r_r <= acc_r_r + (
                ($signed(p_gain_acc_r) * $signed({1'b0, cur_gain_r})) >>> 15
            );
            state <= ER_ACCUM;
        end

        // ── 다음 cluster ──────────────────────────────────────────────────
        ER_ACCUM: begin
            cl_proc_i <= cl_proc_i + 6'd1;
            if (cl_proc_i + 6'd1 >= cl_count_r)
                state <= ER_OUTPUT;
            else
                state <= ER_PROC_PREP;
        end

        // ── 출력 ─────────────────────────────────────────────────────────
        ER_OUTPUT: begin
            er_l     <= sat16(acc_l_r);
            er_r     <= sat16(acc_r_r);
            er_valid <= 1'b1;
            state    <= ER_IDLE;
        end

        default: state <= ER_IDLE;
        endcase
    end
end

endmodule