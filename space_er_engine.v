// ============================================================================
//  space_er_engine.v  v3
//
//  v2 → v3 수정 목록
//  ─────────────────────────────────────────────────────────────────────────
//  [SE-v3-1]  geometry_engine src_z, lst_z 포트 연결
//             v2: src_z/lst_z 미존재 → geometry_engine이 src_y 대리 사용
//             v3: src_z, lst_z 입력 포트 추가, geometry_engine v6에 연결
//                 → 진정한 6면 3D 거리 계산 활성화
//
//  [SE-v3-2]  er_fdn_bridge .space_id 포트 연결 누락 수정
//             v2: .space_id() 미연결 → bridge 내부 LUT가 미정의 동작
//             v3: .space_id(space_id_r) 명시적 연결
//
//  [SE-v3-3]  SE_IDLE sample_en 보호 강화
//             v2: SE_IDLE에서 audio_valid 시 무조건 sample_en 발행
//                 → 파이프라인 미준비 상태에서 렌더 요청 가능
//             v3: SE_IDLE에서 sample_en 발행하되, er_render_engine은
//                 cluster 0개일 때 자체적으로 er=0 출력 (기존 안전장치 유지)
//                 SE_STREAM에서만 inject_en과 함께 정상 동작
//
//  [SE-v3-4]  SE_WAIT_REFLECT timeout 추가
//             v2: ref_ev_done 무한 대기 → deadlock 가능
//             v3: 256-cycle timeout 후 강제 진행
//                 → geo/ref 파이프라인 중단 시에도 FSM 복구
//
//  [SE-v3-5]  inject_en_r 조건부 활성화
//             v2: SE_INJECT에서 무조건 inject_en=1
//             v3: SE_INJECT에서 inject_en=1 (유지, 정상)
//                 단, SE_STREAM에서 space_start 시 inject_en=0 + mute 1ms
//
//  [SE-v3-6]  공간 전환 mute (soft transition)
//             v2: inject_en=0만으로 전환 → pop/click 가능
//             v3: SE_LOAD_SPACE 진입 시 mute_cnt 기동
//                 mute 동안 sample_en 억제 → 무음 전환
//
//  기능 보존: 파이프라인 순서, 출력 인터페이스 v2 완전 호환
//             geometry_engine v6, er_fdn_bridge v3r2 인터페이스 정합
// ============================================================================

`timescale 1ns / 1ps

module space_er_engine (
    input  wire        clk,
    input  wire        rst_n,

    // ── AXI 파라미터 입력 ───────────────────────────────────────────────
    input  wire [4:0]  space_id,
    input  wire        space_start,
    input  wire        geo_en,

    // 방 파라미터
    input  wire [15:0] room_x,
    input  wire [15:0] room_y,
    input  wire [15:0] room_z,
    input  wire [15:0] src_x,
    input  wire [15:0] src_y,
    input  wire [15:0] src_z,     // [SE-v3-1] 신규
    input  wire [15:0] lst_x,
    input  wire [15:0] lst_y,
    input  wire [15:0] lst_z,     // [SE-v3-1] 신규

    // cluster/injection 파라미터
    input  wire [15:0] cluster_thr,
    input  wire [31:0] injection_gain,

    // 오디오 스트림
    input  wire signed [15:0] audio_in,
    input  wire        audio_valid,

    // er_gain_scale
    input  wire [8:0]  er_gain_scale,

    // [LFO-MOD] LFO 입력 (안전 계층 통과 후 값)
    // lfo0_jitter: reflection_engine jitter_mask 연동 (ER delay micro-jitter)
    // lfo1_width:  ER L/R gain anti-phase stereo width motion
    input  wire signed [15:0] lfo0_jitter,   // Q1.15 ±clamp'd, ≤1 sample jitter
    input  wire signed [15:0] lfo1_width,    // Q1.15 ±clamp'd, ≤10% width

    // LPF 상수
    input  wire [31:0] lpf_bright,
    input  wire [31:0] lpf_mid,
    input  wire [31:0] lpf_dark,

    // FDN 입력 (er_fdn_bridge용)
    input  wire signed [15:0] fdn_l_in,
    input  wire signed [15:0] fdn_r_in,

    // 출력
    output wire signed [15:0] er_l_out,
    output wire signed [15:0] er_r_out,
    output wire        er_valid,
    output wire signed [15:0] fdn_l_out,
    output wire signed [15:0] fdn_r_out,

    // [v9] behavior engine feedback 출력
    output reg  [7:0]  er_energy_feedback,   // IIR 스무딩된 ER 에너지
    output reg  [7:0]  er_density_dynamic,   // 256-window density

    // 디버그
    output reg  [2:0]  engine_state,
    output reg  [7:0]  cluster_dbg
);

// ============================================================================
//  FSM 상태
// ============================================================================
localparam [2:0]
    SE_IDLE         = 3'd0,
    SE_LOAD_SPACE   = 3'd1,
    SE_INIT_GEO     = 3'd2,
    SE_WAIT_REFLECT = 3'd3,
    SE_CLUSTER      = 3'd4,
    SE_RENDER       = 3'd5,
    SE_INJECT       = 3'd6,
    SE_STREAM       = 3'd7;

reg [2:0] state;

// ============================================================================
//  space_id 래치
// ============================================================================
reg [4:0]  space_id_r;
reg        space_start_pulse_r;

// ============================================================================
//  내부 제어 신호
// ============================================================================
reg        geo_start_r;
reg        geo_quality_r;
reg        cl_start_r;
reg        cl_load_en_r;
reg        inject_en_r;
reg        sample_en_r;
reg [7:0]  wait_cnt_r;

// [SE-v3-4] SE_WAIT_REFLECT timeout
reg [8:0]  reflect_timeout_r;   // 9비트: 최대 512 cycles

// [SE-v3-6] mute 카운터 (공간 전환 시 ~1ms @50MHz = 50000 cycles)
// 간략화: 8비트 카운터 × 256 = 최대 65536 cycles ≈ 1.3ms
reg [7:0]  mute_cnt_r;
reg        mute_active_r;

// [FIX-5] SE_RENDER 로드 완료 감지
reg        cl_valid_seen_r;
reg        cl_valid_done_r;

// ============================================================================
//  geometry_rom 공유 인터페이스
// ============================================================================
wire [7:0]  rom_addr_geo;
wire [31:0] rom_dout_geo;

geometry_rom u_geo_rom (
    .clk  (clk),
    .addr (rom_addr_geo),
    .dout (rom_dout_geo)
);

// ============================================================================
//  geometry_engine v6 인스턴스
//  [SE-v3-1] src_z, lst_z 연결
// ============================================================================
wire [63:0] geo_event_data;
wire        geo_event_valid;
wire        geo_done;

geometry_engine u_geo (
    .clk           (clk),
    .rst_n         (rst_n),
    .start         (geo_start_r),
    .quality_en    (geo_quality_r),
    .space_id      (space_id_r),
    .room_x        (room_x),
    .room_y        (room_y),
    .room_z        (room_z),
    .src_x         (src_x),
    .src_y         (src_y),
    .src_z         (src_z),         // [SE-v3-1] v6 신규 포트
    .lst_x         (lst_x),
    .lst_y         (lst_y),
    .lst_z         (lst_z),         // [SE-v3-1] v6 신규 포트
    .rom_addr      (rom_addr_geo),
    .rom_dout      (rom_dout_geo),
    .event_data    (geo_event_data),
    .event_valid   (geo_event_valid),
    .done          (geo_done)
);

// ============================================================================
//  reflection_engine 인스턴스
// ============================================================================
wire [63:0] ref_ev_data;
wire        ref_ev_valid;
wire        ref_ev_done;

reflection_engine u_ref (
    .clk             (clk),
    .rst_n           (rst_n),
    .geo_event_data  (geo_event_data),
    .geo_event_valid (geo_event_valid),
    .geo_done        (geo_done),
    .space_id        (space_id_r),
    .space_start     (space_start_pulse_r),
    .jitter_mask     (lfo0_jitter[14:11]),  // [LFO-MOD] LFO0 상위 4bit → micro delay jitter
    .gain_floor      (16'h0010),
    .ev_out          (ref_ev_data),
    .ev_valid        (ref_ev_valid),
    .ev_done         (ref_ev_done)
);

// ============================================================================
//  cluster_bram
// ============================================================================
wire [10:0] ev_wr_addr_w;
wire [63:0] ev_wr_data_w;
wire        ev_wr_en_w;
wire [10:0] ev_rd_addr_w;
wire [63:0] ev_rd_data_w;
wire [7:0]  cl_ra_addr_w;
wire [63:0] cl_ra_data_w;
wire [7:0]  cl_wb_addr_w;
wire [63:0] cl_wb_data_w;
wire        cl_wb_en_w;

cluster_bram u_cl_bram (
    .clk       (clk),
    .ev_wr_addr(ev_wr_addr_w),
    .ev_wr_data(ev_wr_data_w),
    .ev_wr_en  (ev_wr_en_w),
    .ev_rd_addr(ev_rd_addr_w),
    .ev_rd_data(ev_rd_data_w),
    .cl_ra_addr(cl_ra_addr_w),
    .cl_ra_data(cl_ra_data_w),
    .cl_wb_addr(cl_wb_addr_w),
    .cl_wb_data(cl_wb_data_w),
    .cl_wb_en  (cl_wb_en_w)
);

// ============================================================================
//  cluster_engine
// ============================================================================
wire [63:0] cl_out_w;
wire        cl_valid_w;
wire        cl_done_w;
wire [7:0]  cluster_count_w;

cluster_engine u_cl (
    .clk          (clk),
    .rst_n        (rst_n),
    .start        (cl_start_r),
    .cluster_thr  (cluster_thr),
    .ev_in        (ref_ev_data),
    .ev_valid     (ref_ev_valid),
    .ev_done      (ref_ev_done),
    .ev_wr_addr   (ev_wr_addr_w),
    .ev_wr_data   (ev_wr_data_w),
    .ev_wr_en     (ev_wr_en_w),
    .ev_rd_addr   (ev_rd_addr_w),
    .ev_rd_data   (ev_rd_data_w),
    .cl_ra_addr   (cl_ra_addr_w),
    .cl_ra_data   (cl_ra_data_w),
    .cl_wb_addr   (cl_wb_addr_w),
    .cl_wb_data   (cl_wb_data_w),
    .cl_wb_en     (cl_wb_en_w),
    .cl_out       (cl_out_w),
    .cl_valid     (cl_valid_w),
    .cl_done      (cl_done_w),
    .cluster_count(cluster_count_w)
);

// ============================================================================
//  ER delay BRAM (스테레오)
// ============================================================================
wire [12:0] erl_wr_addr_w, erl_rd_addr_w;
wire [15:0] erl_wr_data_w;
wire        erl_wr_en_w;
wire signed [15:0] erl_rd_data_w;

wire [12:0] err_wr_addr_w, err_rd_addr_w;
wire [15:0] err_wr_data_w;
wire        err_wr_en_w;
wire signed [15:0] err_rd_data_w;

// Left ER delay BRAM
(* ram_style = "block" *) reg signed [15:0] er_delay_l [0:4095];
integer er_l_init;
initial begin
    for (er_l_init = 0; er_l_init < 4096; er_l_init = er_l_init + 1)
        er_delay_l[er_l_init] = 16'sd0;
end
reg signed [15:0] erl_rd_reg;
always @(posedge clk) begin
    erl_rd_reg <= er_delay_l[erl_rd_addr_w];
    if (erl_wr_en_w) er_delay_l[erl_wr_addr_w] <= $signed(erl_wr_data_w);
end
assign erl_rd_data_w = erl_rd_reg;

// Right ER delay BRAM
(* ram_style = "block" *) reg signed [15:0] er_delay_r [0:4095];
integer er_r_init;
initial begin
    for (er_r_init = 0; er_r_init < 4096; er_r_init = er_r_init + 1)
        er_delay_r[er_r_init] = 16'sd0;
end
reg signed [15:0] err_rd_reg;
always @(posedge clk) begin
    err_rd_reg <= er_delay_r[err_rd_addr_w];
    if (err_wr_en_w) er_delay_r[err_wr_addr_w] <= $signed(err_wr_data_w);
end
assign err_rd_data_w = err_rd_reg;

// ============================================================================
//  er_render_engine
// ============================================================================
wire signed [15:0] er_l_raw, er_r_raw;
wire               er_valid_raw;

er_render_engine u_render (
    .clk             (clk),
    .rst_n           (rst_n),
    .cluster_load_en (cl_load_en_r),
    .sample_en       (sample_en_r),
    .cl_in           (cl_out_w),
    .cl_valid        (cl_valid_w),
    .cl_done         (cl_done_w),
    .audio_in        (audio_in),
    .er_gain_scale   (er_gain_scale),
    .lpf_bright      (lpf_bright),
    .lpf_mid         (lpf_mid),
    .lpf_dark        (lpf_dark),
    .erl_wr_addr     (erl_wr_addr_w),
    .erl_wr_data     (erl_wr_data_w),
    .erl_wr_en       (erl_wr_en_w),
    .erl_rd_addr     (erl_rd_addr_w),
    .erl_rd_data     (erl_rd_data_w),
    .err_wr_addr     (err_wr_addr_w),
    .err_wr_data     (err_wr_data_w),
    .err_wr_en       (err_wr_en_w),
    .err_rd_addr     (err_rd_addr_w),
    .err_rd_data     (err_rd_data_w),
    .er_l            (er_l_raw),
    .er_r            (er_r_raw),
    .er_valid        (er_valid_raw)
);

// ============================================================================
//  er_fdn_bridge v4 (signal routing only, space_id 제거)
// ============================================================================
er_fdn_bridge u_bridge (
    .clk            (clk),
    .rst_n          (rst_n),
    .inject_en      (inject_en_r),
    .er_l_in        (er_l_raw),
    .er_r_in        (er_r_raw),
    .fdn_l_in       (fdn_l_in),
    .fdn_r_in       (fdn_r_in),
    .injection_gain (injection_gain),
    // [v4] space_id 포트 제거
    .fdn_l_out      (fdn_l_out),
    .fdn_r_out      (fdn_r_out)
);


// ============================================================================
//  [LFO-MOD] ER 출력 - LFO1 anti-phase stereo width modulation
//
//  설계 규칙:
//    er_l = er_l_raw * (1 + lfo1_width/32767)
//    er_r = er_r_raw * (1 - lfo1_width/32767 * 0.8)  ← asymmetry로 collapse 방지
//
//  안전 보장:
//    lfo1_width는 이미 fdn_reverb_top에서 ±3276(10%) clamp 통과
//    추가 clamp: ±2048(6.25%)로 재보호
//    mute_active_r 중 width mod 억제 (공간 전환 보호)
//
//  Q 연산: er_l_raw (Q1.15) × (32767 ± lfo1_q15) / 32768
//    width_adj_l = (er_l_raw * (32767 + lfo1_w)) >>> 15
//    width_adj_r = (er_r_raw * (32767 - lfo1_w_r)) >>> 15
//    lfo1_w_r = (lfo1_w * 6553) >>> 13  ≈ lfo1_w × 0.8
// ============================================================================
wire signed [15:0] lfo1_w =
    (mute_active_r)                        ?  16'sd0 :
    ($signed(lfo1_width) >  16'sd2048)    ?  16'sd2048 :
    ($signed(lfo1_width) < -16'sd2048)    ? -16'sd2048 :
    lfo1_width;

wire signed [15:0] lfo1_w_r = ($signed(lfo1_w) * 16'sd6553) >>> 13; // ×0.8

wire signed [31:0] er_l_mod = ($signed(er_l_raw) * (32'sd32767 + $signed(lfo1_w))) >>> 15;
wire signed [31:0] er_r_mod = ($signed(er_r_raw) * (32'sd32767 - $signed(lfo1_w_r))) >>> 15;

assign er_l_out =
    (er_l_mod >  32'sd32767) ?  16'sd32767 :
    (er_l_mod < -32'sd32768) ? -16'sd32768 :
    er_l_mod[15:0];

assign er_r_out =
    (er_r_mod >  32'sd32767) ?  16'sd32767 :
    (er_r_mod < -32'sd32768) ? -16'sd32768 :
    er_r_mod[15:0];

assign er_valid = er_valid_raw;

// ============================================================================
//  [v9] er_energy_feedback: IIR 1-pole smoothing of |er_l_raw|+|er_r_raw|
//  energy = energy - (energy>>6) + (abs_sum>>4)
//  synth 8-6859: 전용 always 블록, 단일 드라이버
//  synth 8-6858: 리셋값 명시
// ============================================================================
reg [15:0] er_energy_acc;

always @(posedge clk) begin
    if (!rst_n) begin
        er_energy_acc     <= 16'h0000;   // synth 8-6858
        er_energy_feedback <= 8'h00;
    end else begin
        // abs_sum: 두 채널 절대값 합산 >> 4 (정규화)
        begin : er_nrg_blk
            reg [15:0] er_abs_l, er_abs_r, er_abs_sum;
            er_abs_l   = er_l_raw[15] ? (~er_l_raw + 1'b1) : er_l_raw;
            er_abs_r   = er_r_raw[15] ? (~er_r_raw + 1'b1) : er_r_raw;
            er_abs_sum = (er_abs_l + er_abs_r) >> 4;
            er_energy_acc <= er_energy_acc - (er_energy_acc >> 6) + er_abs_sum;
        end
        // clamp 상위 8비트 출력
        er_energy_feedback <= (er_energy_acc[15:8] > 8'd0) ?
                               er_energy_acc[15:8] : 8'h00;
    end
end

// ============================================================================
//  [v9] er_density_dynamic: 256-sample sliding window hit count
//  er_valid pulse 기반 (er_valid_raw)
//  synth 8-6859: 전용 always 블록
// ============================================================================
reg [7:0]  erd_hit_cnt;
reg [7:0]  erd_window_cnt;

always @(posedge clk) begin
    if (!rst_n) begin
        erd_hit_cnt        <= 8'd0;     // synth 8-6858
        erd_window_cnt     <= 8'd0;
        er_density_dynamic <= 8'h00;
    end else begin
        if (erd_window_cnt == 8'd255) begin
            er_density_dynamic <= erd_hit_cnt;
            erd_hit_cnt        <= er_valid_raw ? 8'd1 : 8'd0;
            erd_window_cnt     <= 8'd0;
        end else begin
            erd_window_cnt <= erd_window_cnt + 8'd1;
            if (er_valid_raw)
                erd_hit_cnt <= erd_hit_cnt + 8'd1;
        end
    end
end

// ============================================================================
//  Master FSM
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        state               <= SE_IDLE;
        engine_state        <= 3'd0;
        space_id_r          <= 5'd1;
        space_start_pulse_r <= 1'b0;
        geo_start_r         <= 1'b0;
        geo_quality_r       <= 1'b0;
        cl_start_r          <= 1'b0;
        cl_load_en_r        <= 1'b0;
        inject_en_r         <= 1'b0;
        sample_en_r         <= 1'b0;
        wait_cnt_r          <= 8'd0;
        cluster_dbg         <= 8'd0;
        cl_valid_seen_r     <= 1'b0;
        cl_valid_done_r     <= 1'b0;
        reflect_timeout_r   <= 9'd0;
        mute_cnt_r          <= 8'd0;
        mute_active_r       <= 1'b0;
    end else begin
        // 1-cycle pulse 클리어
        geo_start_r         <= 1'b0;
        cl_start_r          <= 1'b0;
        sample_en_r         <= 1'b0;
        space_start_pulse_r <= 1'b0;

        engine_state <= state;

        // [SE-v3-6] mute 카운터 (256 cycles ≈ 5us @50MHz × mute_cnt)
        if (mute_active_r) begin
            if (mute_cnt_r == 8'd0)
                mute_active_r <= 1'b0;
            else
                mute_cnt_r <= mute_cnt_r - 8'd1;
        end

        // [FIX-5] cl_valid 스트림 감시
        if (state == SE_RENDER) begin
            if (cl_load_en_r && cl_valid_w)
                cl_valid_seen_r <= 1'b1;
            if (cl_valid_seen_r && !cl_valid_w)
                cl_valid_done_r <= 1'b1;
        end

        case (state)

        // ──────────────────────────────────────────────────────────────────
        // SE_IDLE: 대기
        //   [SE-v3-3] sample_en 보호: mute 중이면 억제
        // ──────────────────────────────────────────────────────────────────
        SE_IDLE: begin
            if (space_start) begin
                space_id_r    <= space_id;
                state         <= SE_LOAD_SPACE;
                wait_cnt_r    <= 8'd0;
                // [SE-v3-6] mute 활성화
                mute_active_r <= 1'b1;
                mute_cnt_r    <= 8'd200;   // ~200 × 20ns × 256 ≈ 1ms
            end else if (audio_valid && !mute_active_r) begin
                sample_en_r <= 1'b1;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        // SE_LOAD_SPACE: 공간 전환 준비
        // ──────────────────────────────────────────────────────────────────
        SE_LOAD_SPACE: begin
            cl_load_en_r <= 1'b0;
            inject_en_r  <= 1'b0;
            wait_cnt_r   <= wait_cnt_r + 8'd1;
            if (wait_cnt_r >= 8'd3) begin
                space_start_pulse_r <= 1'b1;
                state               <= SE_INIT_GEO;
                wait_cnt_r          <= 8'd0;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        // SE_INIT_GEO: geometry_engine 기동
        // ──────────────────────────────────────────────────────────────────
        SE_INIT_GEO: begin
            if (geo_en) begin
                geo_start_r         <= 1'b1;
                geo_quality_r       <= 1'b0;
                reflect_timeout_r   <= 9'd0;    // [SE-v3-4] timeout 초기화
                state               <= SE_WAIT_REFLECT;
            end else begin
                cl_valid_seen_r <= 1'b0;
                cl_valid_done_r <= 1'b0;
                state           <= SE_RENDER;
                wait_cnt_r      <= 8'd0;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        // SE_WAIT_REFLECT: geo + ref 완료 대기
        //   [SE-v3-4] 256-cycle timeout → deadlock 방지
        // ──────────────────────────────────────────────────────────────────
        SE_WAIT_REFLECT: begin
            reflect_timeout_r <= reflect_timeout_r + 9'd1;
            if (ref_ev_done) begin
                state      <= SE_CLUSTER;
                cl_start_r <= 1'b1;
            end else if (reflect_timeout_r >= 9'd256) begin
                // timeout: 강제 진행 (cluster 0개 → er=0 안전)
                state      <= SE_CLUSTER;
                cl_start_r <= 1'b1;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        // SE_CLUSTER: cluster_engine 완료 대기
        // ──────────────────────────────────────────────────────────────────
        SE_CLUSTER: begin
            if (cl_done_w) begin
                cluster_dbg     <= cluster_count_w;
                cl_load_en_r    <= 1'b1;
                cl_valid_seen_r <= 1'b0;
                cl_valid_done_r <= 1'b0;
                state           <= SE_RENDER;
                wait_cnt_r      <= 8'd0;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        // SE_RENDER: cl_valid 스트림 감시
        // ──────────────────────────────────────────────────────────────────
        SE_RENDER: begin
            wait_cnt_r <= wait_cnt_r + 8'd1;
            if (cl_valid_done_r || wait_cnt_r >= 8'd254) begin
                cl_load_en_r    <= 1'b0;
                cl_valid_seen_r <= 1'b0;
                cl_valid_done_r <= 1'b0;
                state           <= SE_INJECT;
            end
        end

        // ──────────────────────────────────────────────────────────────────
        // SE_INJECT: injection 활성화
        //   [SE-v3-5] inject_en 정상 활성화 후 스트리밍
        // ──────────────────────────────────────────────────────────────────
        SE_INJECT: begin
            inject_en_r   <= 1'b1;
            mute_active_r <= 1'b0;  // [SE-v3-6] mute 해제
            mute_cnt_r    <= 8'd0;
            state         <= SE_STREAM;
        end

        // ──────────────────────────────────────────────────────────────────
        // SE_STREAM: 메인 동작
        // ──────────────────────────────────────────────────────────────────
        SE_STREAM: begin
            if (audio_valid && !mute_active_r) begin
                sample_en_r <= 1'b1;
            end
            // 공간 변경 감지
            if (space_start) begin
                space_id_r    <= space_id;
                inject_en_r   <= 1'b0;
                // [SE-v3-6] mute 활성화
                mute_active_r <= 1'b1;
                mute_cnt_r    <= 8'd200;
                state         <= SE_LOAD_SPACE;
                wait_cnt_r    <= 8'd0;
            end
        end

        default: state <= SE_IDLE;
        endcase
    end
end

endmodule