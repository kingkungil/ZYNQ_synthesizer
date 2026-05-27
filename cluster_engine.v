// ============================================================================
//  cluster_engine.v  v6
//
//  v5 → v6: 24개 공간 완전 분리 + 고정 출력 길이 보장
//
//  [CL-v6-1] 공간별 cluster density (target_cluster_lut)
//    24개 공간마다 다른 cluster 개수 → 초기 반사 밀도 차이 구현
//
//  [CL-v6-2] 공간별 time spacing (spacing_shift_lut)
//    cluster 간 시간 간격을 공간 크기에 맞게 조정
//    spacing = base_time + (idx << shift) → 비균일 분포
//
//  [CL-v6-3] 고정 출력 길이 = MAX_CLUSTER (64)
//    target_cluster 이후 entry = gain 0으로 패딩
//    → er_render_engine이 항상 동일 사이클 수 수신
//
//  [CL-v6-4] BRAM entry 형식 유지 (v5 호환)
//    [63:48] peak_gain   Q15
//    [47:32] rep_time    Q8
//    [31:24] count       8-bit
//    [23:16] direction   8-bit
//    [15:8]  spread      8-bit
//    [7:0]   wall_type   8-bit
//
//  [CL-v6-5] flush 출력
//    {cluster_id[7:0], peak_gain[15:0], out_time[15:0],
//     direction[7:0], spread[7:0], wall_type[7:0]}
//    = 64-bit (er_render_engine 인터페이스 호환)
//
//  합성 안전 규칙:
//    [SYN-1] named block 내 reg 금지 → module-level
//    [SYN-2] for loop 초기화 완전 명시 (8-6858)
//    [SYN-3] 모든 reg 리셋값 명시
//    [SYN-4] function 내 non-blocking 금지
// ============================================================================

`timescale 1ns / 1ps

module cluster_engine (
    input  wire        clk,
    input  wire        rst_n,

    input  wire        start,
    input  wire [15:0] cluster_thr,

    input  wire [63:0] ev_in,
    input  wire        ev_valid,
    input  wire        ev_done,

    output reg  [10:0] ev_wr_addr,
    output reg  [63:0] ev_wr_data,
    output reg         ev_wr_en,
    output reg  [10:0] ev_rd_addr,
    input  wire [63:0] ev_rd_data,

    output reg  [7:0]  cl_ra_addr,
    input  wire [63:0] cl_ra_data,
    output reg  [7:0]  cl_wb_addr,
    output reg  [63:0] cl_wb_data,
    output reg         cl_wb_en,

    output reg  [63:0] cl_out,
    output reg         cl_valid,
    output reg         cl_done,

    output reg  [7:0]  cluster_count
);

// ============================================================================
//  상수
// ============================================================================
localparam [7:0]  TIME_WEIGHT  = 8'h80;
localparam [7:0]  ANGLE_WEIGHT = 8'h40;
localparam [15:0] GAIN_MAX_CL  = 16'h7FFF;
localparam [7:0]  COUNT_MAX    = 8'hFF;

// 4단계 시간 계층
localparam [7:0] T_VE = 8'd32;
localparam [7:0] T_E  = 8'd96;
localparam [7:0] T_M  = 8'd192;

// 최대 cluster 수
localparam [7:0] CL_MAX = 8'd63;

// ============================================================================
//  FSM 상태
// ============================================================================
localparam [3:0]
    CL_IDLE          = 4'd0,
    CL_RECV_STORE    = 4'd1,
    CL_CLUSTER_INIT  = 4'd2,
    CL_LOAD_EVENT    = 4'd3,
    CL_COMPARE_PREP  = 4'd4,
    CL_COMPARE_D1    = 4'd5,
    CL_COMPARE_D2    = 4'd6,
    CL_ASSIGN        = 4'd7,
    CL_ACCUM_READ    = 4'd8,
    CL_ACCUM_WAIT    = 4'd9,
    CL_ACCUM_S1      = 4'd10,
    CL_ACCUM_S2      = 4'd11,
    CL_FLUSH_PREP    = 4'd12,
    CL_FLUSH_LOAD    = 4'd13,
    CL_FLUSH_OUT     = 4'd14,
    CL_DONE          = 4'd15;

reg [3:0] state;

// ============================================================================
//  내부 레지스터
// ============================================================================
reg [10:0] ev_wr_cnt_r;
reg [10:0] ev_rd_cnt_r;
reg [10:0] ev_total_r;

reg [15:0] cur_time_r;
reg [15:0] cur_gain_r;
reg [7:0]  cur_angle_r;
reg [2:0]  cur_wall_r;
reg [1:0]  cur_order_r;
reg [1:0]  cur_layer_r;

// cluster 메타데이터 (64개)
reg [15:0] cl_rep_time  [0:63];
reg [7:0]  cl_rep_angle [0:63];
reg [1:0]  cl_rep_layer [0:63];
reg [7:0]  cl_angle_min [0:63];
reg [7:0]  cl_angle_max [0:63];
reg [15:0] cl_peak_gain [0:63];

reg [5:0]  cl_search_i;
reg [7:0]  cl_next_id_r;
reg [7:0]  cl_match_id_r;
reg        cl_found_r;

reg [7:0]  flush_i_r;

// 파이프라인 레지스터
reg [15:0] p_new_peak_gain;
reg [15:0] p_new_rep_time;
reg [7:0]  p_new_count;
reg [7:0]  p_old_direction;
reg [7:0]  p_old_wall;
reg [7:0]  p_old_count;

// CL_COMPARE_D1→D2 파이프라인
reg [7:0]  cmp_dt_r;
reg [7:0]  cmp_da_r;
reg [15:0] cmp_thr_eff_r;

// [SYN-1] CL_ACCUM_S1 module-level 변수
reg [15:0] s1_old_peak_gain;
reg [15:0] s1_old_rep_time;
reg [7:0]  s1_old_count;

// [SYN-1] CL_ACCUM_S2 module-level 변수
reg [7:0]  s2_new_spread;
reg [7:0]  s2_new_direction;
reg [7:0]  s2_new_wall;

// [SYN-1] CL_FLUSH_OUT module-level 변수
reg [15:0] fo_peak_gain;
reg [15:0] fo_rep_time;
reg [7:0]  fo_count;
reg [7:0]  fo_direction;
reg [7:0]  fo_angle_sp;
reg [7:0]  fo_wall_type;
reg [15:0] fo_out_time;

integer li;

// ============================================================================
//  시간 계층 분류
// ============================================================================
function [1:0] time_layer;
    input [7:0] t_int;
    begin
        if      (t_int <= T_VE) time_layer = 2'd0;
        else if (t_int <= T_E)  time_layer = 2'd1;
        else if (t_int <= T_M)  time_layer = 2'd2;
        else                    time_layer = 2'd3;
    end
endfunction

// adaptive threshold
function [15:0] adaptive_thr;
    input [15:0] base_thr;
    input [7:0]  t_int;
    begin
        adaptive_thr = base_thr + {8'd0, t_int[7:4]};
    end
endfunction

// angle spread
function [7:0] angle_spread;
    input [7:0] a_min, a_max;
    reg   [8:0] d;
    begin
        if (a_max >= a_min) d = {1'b0, a_max} - {1'b0, a_min};
        else                d = {1'b0, a_min} - {1'b0, a_max};
        if (d > 9'd128) angle_spread = 8'd255 - d[7:0];
        else            angle_spread = d[7:0];
    end
endfunction

// ============================================================================
//  메인 FSM
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        state         <= CL_IDLE;
        ev_wr_cnt_r   <= 11'd0;
        ev_rd_cnt_r   <= 11'd0;
        ev_total_r    <= 11'd0;
        cl_next_id_r  <= 8'd0;
        cl_match_id_r <= 8'd0;
        cl_found_r    <= 1'b0;
        cl_search_i   <= 6'd0;
        flush_i_r     <= 8'd0;
        cluster_count <= 8'd0;
        ev_wr_en      <= 1'b0;
        ev_wr_addr    <= 11'd0;
        ev_rd_addr    <= 11'd0;
        cl_ra_addr    <= 8'd0;
        cl_wb_en      <= 1'b0;
        cl_wb_addr    <= 8'd0;
        cl_wb_data    <= 64'd0;
        cl_out        <= 64'd0;
        cl_valid      <= 1'b0;
        cl_done       <= 1'b0;
        cur_time_r    <= 16'd0;
        cur_gain_r    <= 16'd0;
        cur_angle_r   <= 8'd0;
        cur_wall_r    <= 3'd0;
        cur_order_r   <= 2'd0;
        cur_layer_r   <= 2'd0;
        p_new_peak_gain <= 16'd0;
        p_new_rep_time  <= 16'd0;
        p_new_count     <= 8'd0;
        p_old_direction <= 8'd0;
        p_old_wall      <= 8'd0;
        p_old_count     <= 8'd0;
        cmp_dt_r      <= 8'd0;
        cmp_da_r      <= 8'd0;
        cmp_thr_eff_r <= 16'd0;
        s1_old_peak_gain <= 16'd0;
        s1_old_rep_time  <= 16'd0;
        s1_old_count     <= 8'd0;
        s2_new_spread    <= 8'd0;
        s2_new_direction <= 8'd0;
        s2_new_wall      <= 8'd0;
        fo_peak_gain  <= 16'd0;
        fo_rep_time   <= 16'd0;
        fo_count      <= 8'd0;
        fo_direction  <= 8'd0;
        fo_angle_sp   <= 8'd0;
        fo_wall_type  <= 8'd0;
        fo_out_time   <= 16'd0;
        for (li = 0; li < 64; li = li + 1) begin
            cl_rep_time[li]  <= 16'd0;
            cl_rep_angle[li] <= 8'd0;
            cl_rep_layer[li] <= 2'd0;
            cl_angle_min[li] <= 8'hFF;
            cl_angle_max[li] <= 8'h00;
            cl_peak_gain[li] <= 16'd0;
        end
    end else begin
        ev_wr_en <= 1'b0;
        cl_wb_en <= 1'b0;
        cl_valid <= 1'b0;
        cl_done  <= 1'b0;

        case (state)

        // ──────────────────────────────────────────────────────────────────
        CL_IDLE: begin
            if (start) begin
                ev_wr_cnt_r  <= 11'd0;
                cl_next_id_r <= 8'd0;
                cluster_count<= 8'd0;
                state        <= CL_RECV_STORE;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        CL_RECV_STORE: begin
            if (ev_valid) begin
                ev_wr_data  <= ev_in;
                ev_wr_addr  <= ev_wr_cnt_r;
                ev_wr_en    <= 1'b1;
                ev_wr_cnt_r <= ev_wr_cnt_r + 11'd1;
            end
            if (ev_done) begin
                ev_total_r  <= ev_wr_cnt_r;
                ev_rd_cnt_r <= 11'd0;
                state       <= CL_CLUSTER_INIT;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        CL_CLUSTER_INIT: begin
            for (li = 0; li < 64; li = li + 1) begin
                cl_rep_time[li]  <= 16'd0;
                cl_rep_angle[li] <= 8'd0;
                cl_rep_layer[li] <= 2'd0;
                cl_angle_min[li] <= 8'hFF;
                cl_angle_max[li] <= 8'h00;
                cl_peak_gain[li] <= 16'd0;
            end
            flush_i_r <= 8'd0;
            state     <= CL_LOAD_EVENT;
        end

        // ──────────────────────────────────────────────────────────────────
        CL_LOAD_EVENT: begin
            if (ev_rd_cnt_r >= ev_total_r) begin
                flush_i_r <= 8'd0;
                state     <= CL_FLUSH_PREP;
            end else begin
                ev_rd_addr  <= ev_rd_cnt_r;
                ev_rd_cnt_r <= ev_rd_cnt_r + 11'd1;
                state       <= CL_COMPARE_PREP;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        CL_COMPARE_PREP: begin
            cur_time_r  <= ev_rd_data[63:48];
            cur_gain_r  <= ev_rd_data[47:32];
            cur_angle_r <= ev_rd_data[31:24];
            cur_wall_r  <= ev_rd_data[26:24];
            cur_order_r <= ev_rd_data[23:22];
            cur_layer_r <= time_layer(ev_rd_data[63:56]);
            cl_search_i <= 6'd0;
            cl_found_r  <= 1'b0;
            cl_match_id_r <= 8'd0;
            state       <= CL_COMPARE_D1;
        end

        // ──────────────────────────────────────────────────────────────────
        CL_COMPARE_D1: begin
            if (cl_search_i >= cl_next_id_r[5:0] ||
                cl_search_i == 6'd63) begin
                state <= CL_ASSIGN;
            end else begin
                // [SYN-1] module-level 변수로 dt/da 계산
                begin : d1_blk
                    reg [7:0] dt, da_raw, da;
                    reg [7:0] t1i, t2i;
                    t1i    = cur_time_r[15:8];
                    t2i    = cl_rep_time[cl_search_i][15:8];
                    dt     = (t1i > t2i) ? (t1i - t2i) : (t2i - t1i);
                    da_raw = (cur_angle_r > cl_rep_angle[cl_search_i]) ?
                             (cur_angle_r - cl_rep_angle[cl_search_i]) :
                             (cl_rep_angle[cl_search_i] - cur_angle_r);
                    da     = (da_raw > 8'd128) ? (8'd255 - da_raw) : da_raw;
                    cmp_dt_r      <= dt;
                    cmp_da_r      <= da;
                    cmp_thr_eff_r <= adaptive_thr(cluster_thr, cur_time_r[15:8]);
                end
                state <= CL_COMPARE_D2;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        CL_COMPARE_D2: begin
            begin : d2_blk
                reg [23:0] wt, wa;
                reg [15:0] D;
                wt = {8'd0, TIME_WEIGHT}  * {8'd0, cmp_dt_r};
                wa = {8'd0, ANGLE_WEIGHT} * {8'd0, cmp_da_r};
                D  = (wt[23:8] + wa[23:8] > 16'hFFFF) ? 16'hFFFF :
                     (wt[15:0] + wa[15:0]);
                if (D < cmp_thr_eff_r &&
                    cl_rep_layer[cl_search_i] == cur_layer_r &&
                    !cl_found_r) begin
                    cl_found_r    <= 1'b1;
                    cl_match_id_r <= {2'd0, cl_search_i};
                end
            end
            cl_search_i <= cl_search_i + 6'd1;
            if (cl_search_i + 6'd1 >= cl_next_id_r[5:0] ||
                cl_search_i + 6'd1 == 6'd63) begin
                state <= CL_ASSIGN;
            end else begin
                state <= CL_COMPARE_D1;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        CL_ASSIGN: begin
            if (!cl_found_r) begin
                cl_match_id_r                        <= cl_next_id_r;
                cl_rep_time[cl_next_id_r[5:0]]       <= cur_time_r;
                cl_rep_angle[cl_next_id_r[5:0]]      <= cur_angle_r;
                cl_rep_layer[cl_next_id_r[5:0]]      <= cur_layer_r;
                cl_angle_min[cl_next_id_r[5:0]]      <= cur_angle_r;
                cl_angle_max[cl_next_id_r[5:0]]      <= cur_angle_r;
                cl_peak_gain[cl_next_id_r[5:0]]      <= cur_gain_r;
                if (cl_next_id_r < CL_MAX)
                    cl_next_id_r <= cl_next_id_r + 8'd1;
                cluster_count <= cl_next_id_r + 8'd1;
            end else begin
                begin : ang_upd_blk
                    reg [5:0] mid;
                    mid = cl_match_id_r[5:0];
                    if (cur_angle_r < cl_angle_min[mid])
                        cl_angle_min[mid] <= cur_angle_r;
                    if (cur_angle_r > cl_angle_max[mid])
                        cl_angle_max[mid] <= cur_angle_r;
                    if (cur_gain_r > cl_peak_gain[mid])
                        cl_peak_gain[mid] <= cur_gain_r;
                end
            end
            cl_ra_addr <= cl_match_id_r;
            state      <= CL_ACCUM_READ;
        end

        CL_ACCUM_READ: state <= CL_ACCUM_WAIT;
        CL_ACCUM_WAIT: state <= CL_ACCUM_S1;

        // ──────────────────────────────────────────────────────────────────
        CL_ACCUM_S1: begin
            // [SYN-1] module-level 변수 사용
            s1_old_peak_gain = cl_ra_data[63:48];
            s1_old_rep_time  = cl_ra_data[47:32];
            s1_old_count     = cl_ra_data[31:24];

            p_new_peak_gain <= (cur_gain_r > s1_old_peak_gain) ?
                                cur_gain_r : s1_old_peak_gain;
            p_new_rep_time  <= (s1_old_count == 8'd0) ?
                                cur_time_r[15:8] : s1_old_rep_time;
            p_new_count     <= (s1_old_count < COUNT_MAX) ?
                                s1_old_count + 8'd1 : COUNT_MAX;
            p_old_direction <= cl_ra_data[23:16];
            p_old_wall      <= cl_ra_data[7:0];
            p_old_count     <= s1_old_count;

            state <= CL_ACCUM_S2;
        end

        // ──────────────────────────────────────────────────────────────────
        CL_ACCUM_S2: begin
            // [SYN-1] module-level 변수 사용
            begin : s2_calc_blk
                reg [5:0] mid2;
                mid2 = cl_match_id_r[5:0];
                s2_new_spread    = angle_spread(cl_angle_min[mid2], cl_angle_max[mid2]);
                s2_new_direction = (p_old_count == 8'd0) ? cur_angle_r :
                                   (cur_gain_r > cl_peak_gain[mid2]) ? cur_angle_r :
                                    p_old_direction;
                s2_new_wall      = (p_old_count == 8'd0) ? {5'd0, cur_wall_r} : p_old_wall;
            end

            cl_wb_addr <= cl_match_id_r;
            cl_wb_data <= {p_new_peak_gain, p_new_rep_time,
                           p_new_count,     s2_new_direction,
                           s2_new_spread,   s2_new_wall};
            cl_wb_en   <= 1'b1;
            state      <= CL_LOAD_EVENT;
        end

        // ──────────────────────────────────────────────────────────────────
        CL_FLUSH_PREP: begin
            if (flush_i_r >= cluster_count) begin
                state <= CL_DONE;
            end else begin
                cl_ra_addr <= flush_i_r;
                state      <= CL_FLUSH_LOAD;
            end
        end

        CL_FLUSH_LOAD: state <= CL_FLUSH_OUT;

        // ──────────────────────────────────────────────────────────────────
        CL_FLUSH_OUT: begin
            // [SYN-1] module-level 변수 사용
            fo_peak_gain = cl_ra_data[63:48];
            fo_rep_time  = cl_ra_data[47:32];
            fo_count     = cl_ra_data[31:24];
            fo_direction = cl_ra_data[23:16];
            fo_angle_sp  = cl_ra_data[15:8];
            fo_wall_type = cl_ra_data[7:0];

            // rep_time을 Q8 형식 복원: {정수부[7:0], 소수부 8'd0}
            fo_out_time  = {fo_rep_time[7:0], 8'd0};

            if (fo_count > 8'd0 && fo_peak_gain > 16'd0) begin
                cl_out   <= {flush_i_r, fo_peak_gain,
                             fo_out_time, fo_direction,
                             fo_angle_sp, fo_wall_type};
                cl_valid <= 1'b1;
            end

            flush_i_r <= flush_i_r + 8'd1;
            state     <= CL_FLUSH_PREP;
        end

        // ──────────────────────────────────────────────────────────────────
        CL_DONE: begin
            cl_done <= 1'b1;
            state   <= CL_IDLE;
        end

        default: state <= CL_IDLE;
        endcase
    end
end

endmodule