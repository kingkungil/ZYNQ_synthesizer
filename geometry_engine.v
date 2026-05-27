// ============================================================================
//  geometry_engine.v  v6
//
//  v5 → v6 수정 목록
//  ─────────────────────────────────────────────────────────────────────────
//  [GEO-v6-1]  wall_src_dist_q8 "4단계 계단" → reciprocal LUT 기반 연속값
//              v5: shift 근사 4-level → 중간 거리 구분 불가
//              v6: approx_recip_q22 (16-entry mantissa LUT)
//                  → dist_m_q8 = coord * inv_room * size_hint >> 22
//                  → 부드러운 연속 거리값 (≤ 6% 오차)
//
//  [GEO-v6-2]  src_z, lst_z 입력 포트 추가
//              v5: src_z 없음 → src_y 대리 (공간 왜곡)
//              v6: front = src_z, back = room_z - src_z (진정한 3D)
//
//  [GEO-v6-3]  reciprocal 사전계산 (GE_CALC_RECIP 상태)
//              ROM 로딩 후 inv_room_x/y/z 1회 계산 → wall별 곱셈만
//
//  [GEO-v6-4]  gain_from_wall 연속 감쇠 (16단계 보간)
//
//  합성 안전: (* use_dsp *), 나눗셈 없음, 인터페이스 v5 호환 + src_z/lst_z
// ============================================================================

`timescale 1ns / 1ps

module geometry_engine (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire        quality_en,
    input  wire [4:0]  space_id,
    input  wire [15:0] room_x,
    input  wire [15:0] room_y,
    input  wire [15:0] room_z,
    input  wire [15:0] src_x,
    input  wire [15:0] src_y,
    input  wire [15:0] src_z,     // [GEO-v6-2] 신규
    input  wire [15:0] lst_x,
    input  wire [15:0] lst_y,
    input  wire [15:0] lst_z,     // [GEO-v6-2] 신규
    output reg  [7:0]  rom_addr,
    input  wire [31:0] rom_dout,
    output reg  [63:0] event_data,
    output reg         event_valid,
    output reg         done
);

// ============================================================================
//  상수
// ============================================================================
localparam [15:0] SAMP_PER_M_Q8 = 16'h8BE7;  // 48000/343 * 256
localparam [15:0] GAIN_MAX      = 16'h7800;
localparam [15:0] GAIN_FLOOR    = 16'h0010;
localparam [15:0] GAIN_UNITY    = 16'h4000;  // 0.5 Q15

// ============================================================================
//  FSM 상태
// ============================================================================
localparam [3:0]
    GE_IDLE        = 4'd0,
    GE_ROM_WAIT    = 4'd1,
    GE_LATCH_ROM   = 4'd2,
    GE_CALC_RECIP  = 4'd3,   // [GEO-v6-3]
    GE_WALL_CALC   = 4'd4,
    GE_TOF_PIPE1   = 4'd5,
    GE_TOF_PIPE2   = 4'd6,
    GE_GAIN_CALC   = 4'd7,
    GE_EMIT_1ST    = 4'd8,
    GE_EMIT_2ND    = 4'd9,
    GE_2ND_PREP    = 4'd10,
    GE_2ND_EMIT    = 4'd11,
    GE_DONE        = 4'd12;

reg [3:0]  state;
reg [2:0]  wall_i, wall_j;

// ROM 로드
reg [7:0]  abs_rom_r [0:5];
reg [7:0]  sct_rom_r [0:5];
reg [15:0] room_size_hint_r;
reg [2:0]  rom_load_i;

// 공간 파라미터
reg [7:0]  cur_scale_r;
reg [3:0]  cur_max_ref_r;
reg [7:0]  cur_asym_r;
reg [15:0] lfsr_r;

// [GEO-v6-3] Reciprocal
reg [21:0] inv_room_x_r, inv_room_y_r, inv_room_z_r;
reg [1:0]  recip_step_r;

// wall 거리
reg [15:0] wall_dist_r, lst_wall_dist_r;

// TOF 파이프라인
(* use_dsp = "yes" *) reg [31:0] tof_prod_r;
reg [15:0] tof_base_r, tof_lst_r, tof_total_r;

// gain
reg [15:0] gain_raw_r;
reg [7:0]  angle_raw_r;
reg [19:0] gain_accum_r;
localparam [19:0] GAIN_SUM_MAX = 20'h08000;

// 2차 반사
reg [15:0] ev_tof_r   [0:5];
reg [15:0] ev_gain_r  [0:5];
reg [7:0]  ev_angle_r [0:5];

integer li;

// ============================================================================
//  공간 파라미터 함수 (v5 호환)
// ============================================================================
function [7:0] geo_scale;   input [4:0] sid; begin case(sid)
    5'd0:geo_scale=8'h40; 5'd1:geo_scale=8'hC0; 5'd2:geo_scale=8'hFF;
    5'd3:geo_scale=8'hD0; 5'd4:geo_scale=8'h20; 5'd5:geo_scale=8'h50;
    5'd6:geo_scale=8'h80; 5'd7:geo_scale=8'h18; 5'd8:geo_scale=8'h10;
    5'd9:geo_scale=8'h20; 5'd10:geo_scale=8'hB0;5'd11:geo_scale=8'hC0;
    5'd12:geo_scale=8'hA0;5'd13:geo_scale=8'h90;5'd14:geo_scale=8'hB8;
    5'd15:geo_scale=8'hD8;5'd16:geo_scale=8'h28;5'd17:geo_scale=8'hD0;
    5'd18:geo_scale=8'h80;5'd19:geo_scale=8'hC8;5'd20:geo_scale=8'hFF;
    5'd21:geo_scale=8'hC0;5'd22:geo_scale=8'hA8;5'd23:geo_scale=8'hE0;
    default:geo_scale=8'h80; endcase end
endfunction

function [3:0] geo_max_ref; input [4:0] sid; begin case(sid)
    5'd0:geo_max_ref=4'd4; 5'd1:geo_max_ref=4'd6; 5'd2:geo_max_ref=4'd6;
    5'd3:geo_max_ref=4'd6; 5'd4:geo_max_ref=4'd4; 5'd5:geo_max_ref=4'd4;
    5'd6:geo_max_ref=4'd6; 5'd7:geo_max_ref=4'd6; 5'd8:geo_max_ref=4'd6;
    5'd9:geo_max_ref=4'd6; 5'd10:geo_max_ref=4'd6;5'd11:geo_max_ref=4'd6;
    5'd12:geo_max_ref=4'd5;5'd13:geo_max_ref=4'd5;5'd14:geo_max_ref=4'd6;
    5'd15:geo_max_ref=4'd6;5'd16:geo_max_ref=4'd6;5'd17:geo_max_ref=4'd6;
    5'd18:geo_max_ref=4'd6;5'd19:geo_max_ref=4'd6;5'd20:geo_max_ref=4'd6;
    5'd21:geo_max_ref=4'd6;5'd22:geo_max_ref=4'd6;5'd23:geo_max_ref=4'd6;
    default:geo_max_ref=4'd6; endcase end
endfunction

function [7:0] geo_asym; input [4:0] sid; begin case(sid)
    5'd0:geo_asym=8'h03; 5'd1:geo_asym=8'h0F; 5'd2:geo_asym=8'h1F;
    5'd3:geo_asym=8'h17; 5'd4:geo_asym=8'h01; 5'd5:geo_asym=8'h05;
    5'd6:geo_asym=8'h0B; 5'd7:geo_asym=8'h01; 5'd8:geo_asym=8'h00;
    5'd9:geo_asym=8'h03; 5'd10:geo_asym=8'h0F;5'd11:geo_asym=8'h13;
    5'd12:geo_asym=8'h07;5'd13:geo_asym=8'h01;5'd14:geo_asym=8'h3F;
    5'd15:geo_asym=8'h2F;5'd16:geo_asym=8'h1F;5'd17:geo_asym=8'h17;
    5'd18:geo_asym=8'h7F;5'd19:geo_asym=8'h1F;5'd20:geo_asym=8'h07;
    5'd21:geo_asym=8'h2F;5'd22:geo_asym=8'h2F;5'd23:geo_asym=8'hFF;
    default:geo_asym=8'h0F; endcase end
endfunction

function [15:0] geo_seed; input [4:0] sid; begin case(sid)
    5'd0:geo_seed=16'hACE1; 5'd1:geo_seed=16'hB3D7; 5'd2:geo_seed=16'hC5F9;
    5'd3:geo_seed=16'hD2AB; 5'd4:geo_seed=16'hE4CD; 5'd5:geo_seed=16'hF1EF;
    5'd6:geo_seed=16'h7A13; 5'd7:geo_seed=16'h8B25; 5'd8:geo_seed=16'h9C37;
    5'd9:geo_seed=16'hAD49; 5'd10:geo_seed=16'hBE5B;5'd11:geo_seed=16'hCF6D;
    5'd12:geo_seed=16'hDE7F;5'd13:geo_seed=16'hEF81;5'd14:geo_seed=16'h1293;
    5'd15:geo_seed=16'h23A5;5'd16:geo_seed=16'h34B7;5'd17:geo_seed=16'h45C9;
    5'd18:geo_seed=16'h56DB;5'd19:geo_seed=16'h67ED;5'd20:geo_seed=16'h78FF;
    5'd21:geo_seed=16'h8911;5'd22:geo_seed=16'h9A23;5'd23:geo_seed=16'hAB35;
    default:geo_seed=16'hBCE1; endcase end
endfunction

function [15:0] lfsr_next; input [15:0] v; begin
    lfsr_next = {1'b0, v[15:1]} ^ (v[0] ? 16'hB400 : 16'h0000);
end endfunction

// ============================================================================
//  [GEO-v6-1] approx_recip_q22: 16-entry mantissa LUT reciprocal
//  입력: d[15:0], 출력: ≈ 2^22 / d (22비트, d=0 보호)
// ============================================================================
function [21:0] approx_recip_q22;
    input [15:0] d;
    reg [3:0] msb_pos;
    reg [3:0] idx;
    reg [15:0] base;
    reg [31:0] shifted;
    begin
        if (d == 16'd0) begin
            approx_recip_q22 = 22'h3FFFFF;
        end else begin
            casez (d)
                16'b1???_????_????_????: msb_pos = 4'd15;
                16'b01??_????_????_????: msb_pos = 4'd14;
                16'b001?_????_????_????: msb_pos = 4'd13;
                16'b0001_????_????_????: msb_pos = 4'd12;
                16'b0000_1???_????_????: msb_pos = 4'd11;
                16'b0000_01??_????_????: msb_pos = 4'd10;
                16'b0000_001?_????_????: msb_pos = 4'd9;
                16'b0000_0001_????_????: msb_pos = 4'd8;
                16'b0000_0000_1???_????: msb_pos = 4'd7;
                16'b0000_0000_01??_????: msb_pos = 4'd6;
                16'b0000_0000_001?_????: msb_pos = 4'd5;
                16'b0000_0000_0001_????: msb_pos = 4'd4;
                16'b0000_0000_0000_1???: msb_pos = 4'd3;
                16'b0000_0000_0000_01??: msb_pos = 4'd2;
                16'b0000_0000_0000_001?: msb_pos = 4'd1;
                default:                  msb_pos = 4'd0;
            endcase
            case (msb_pos)
                4'd15: idx = d[14:11]; 4'd14: idx = d[13:10];
                4'd13: idx = d[12:9];  4'd12: idx = d[11:8];
                4'd11: idx = d[10:7];  4'd10: idx = d[9:6];
                4'd9:  idx = d[8:5];   4'd8:  idx = d[7:4];
                4'd7:  idx = d[6:3];   4'd6:  idx = d[5:2];
                4'd5:  idx = d[4:1];   4'd4:  idx = d[3:0];
                default: idx = 4'd0;
            endcase
            // 1/(1+idx/16) in Q15
            case (idx)
                4'd0:  base = 16'h8000; 4'd1:  base = 16'h7879;
                4'd2:  base = 16'h71C7; 4'd3:  base = 16'h6BCB;
                4'd4:  base = 16'h6666; 4'd5:  base = 16'h6186;
                4'd6:  base = 16'h5D17; 4'd7:  base = 16'h590B;
                4'd8:  base = 16'h5555; 4'd9:  base = 16'h51EC;
                4'd10: base = 16'h4EC5; 4'd11: base = 16'h4BDA;
                4'd12: base = 16'h4924; 4'd13: base = 16'h469E;
                4'd14: base = 16'h4444; 4'd15: base = 16'h4211;
                default: base = 16'h8000;
            endcase
            // recip_q22 = base << (7-msb) or >> (msb-7)
            if (msb_pos <= 4'd7)
                shifted = {16'd0, base} << (4'd7 - msb_pos);
            else
                shifted = {16'd0, base} >> (msb_pos - 4'd7);
            approx_recip_q22 = (shifted > 32'h3FFFFF) ? 22'h3FFFFF : shifted[21:0];
        end
    end
endfunction

// ============================================================================
//  [GEO-v6-1] wall 거리 (reciprocal 곱셈)
// ============================================================================
function [15:0] wall_dist_recip;
    input [15:0] coord;
    input [21:0] inv_room;
    input [15:0] size_hint;
    reg [37:0] prod1;
    reg [15:0] ratio_q8;
    reg [31:0] prod2;
    begin
        if (coord == 16'd0) begin
            wall_dist_recip = 16'h0080;
        end else begin
            prod1 = {16'd0, coord} * {16'd0, inv_room};
            ratio_q8 = (prod1[37:14] > 24'h00FFFF) ? 16'hFF00 : prod1[29:14];
            prod2 = {16'd0, size_hint} * {16'd0, ratio_q8};
            if (prod2[23:8] > 16'hFF00)       wall_dist_recip = 16'hFF00;
            else if (prod2[23:8] < 16'h0080)   wall_dist_recip = 16'h0080;
            else                                wall_dist_recip = prod2[23:8];
        end
    end
endfunction

// wall 좌표 추출
function [15:0] wall_src_coord;
    input [2:0] wid;
    input [15:0] sx, sy, sz, rx, ry, rz;
    begin
        case (wid)
        3'd0: wall_src_coord = sx;
        3'd1: wall_src_coord = (rx > sx) ? (rx - sx) : 16'd1;
        3'd2: wall_src_coord = sy;
        3'd3: wall_src_coord = (ry > sy) ? (ry - sy) : 16'd1;
        3'd4: wall_src_coord = sz;
        3'd5: wall_src_coord = (rz > sz) ? (rz - sz) : 16'd1;
        default: wall_src_coord = rx >> 1;
        endcase
    end
endfunction

function [21:0] select_inv_room;
    input [2:0] wid;
    input [21:0] ix, iy, iz;
    begin
        case (wid)
        3'd0, 3'd1: select_inv_room = ix;
        3'd2, 3'd3: select_inv_room = iy;
        3'd4, 3'd5: select_inv_room = iz;
        default:     select_inv_room = ix;
        endcase
    end
endfunction

// ============================================================================
//  [GEO-v6-4] gain (연속 감쇠 + absorption)
// ============================================================================
function [15:0] gain_from_wall;
    input [15:0] dist_m_q8;
    input [7:0]  abs_q8;
    reg [3:0] shift_amt;
    reg [15:0] base_gain;
    reg [31:0] abs_factor;
    begin
        case (dist_m_q8[15:10])
            6'd0:            shift_amt = 4'd0;
            6'd1:            shift_amt = 4'd1;
            6'd2, 6'd3:     shift_amt = 4'd2;
            6'd4, 6'd5, 6'd6, 6'd7:
                             shift_amt = 4'd3;
            default:         shift_amt = 4'd4;
        endcase
        // 서브미터 보간
        case ({shift_amt, dist_m_q8[9:8]})
            {4'd0,2'd0}: base_gain = GAIN_UNITY;
            {4'd0,2'd1}: base_gain = GAIN_UNITY - (GAIN_UNITY >> 3);
            {4'd0,2'd2}: base_gain = GAIN_UNITY - (GAIN_UNITY >> 2);
            {4'd0,2'd3}: base_gain = GAIN_UNITY - (GAIN_UNITY >> 2) - (GAIN_UNITY >> 4);
            {4'd1,2'd0}: base_gain = GAIN_UNITY >> 1;
            {4'd1,2'd1}: base_gain = (GAIN_UNITY >> 1) - (GAIN_UNITY >> 4);
            {4'd1,2'd2}: base_gain = (GAIN_UNITY >> 1) - (GAIN_UNITY >> 3);
            {4'd1,2'd3}: base_gain = (GAIN_UNITY >> 1) - (GAIN_UNITY >> 3) - (GAIN_UNITY >> 5);
            {4'd2,2'd0}: base_gain = GAIN_UNITY >> 2;
            {4'd2,2'd1}: base_gain = (GAIN_UNITY >> 2) - (GAIN_UNITY >> 5);
            {4'd2,2'd2}: base_gain = (GAIN_UNITY >> 2) - (GAIN_UNITY >> 4);
            {4'd2,2'd3}: base_gain = (GAIN_UNITY >> 2) - (GAIN_UNITY >> 4) - (GAIN_UNITY >> 6);
            {4'd3,2'd0}: base_gain = GAIN_UNITY >> 3;
            {4'd3,2'd1}: base_gain = (GAIN_UNITY >> 3) - (GAIN_UNITY >> 6);
            {4'd3,2'd2}: base_gain = (GAIN_UNITY >> 3) - (GAIN_UNITY >> 5);
            {4'd3,2'd3}: base_gain = (GAIN_UNITY >> 3) - (GAIN_UNITY >> 5) - (GAIN_UNITY >> 7);
            default:      base_gain = GAIN_UNITY >> 4;
        endcase
        abs_factor = ({16'd0, base_gain} * {24'd0, 8'hFF - abs_q8}) >> 8;
        if      (abs_factor[15:0] > GAIN_MAX)   gain_from_wall = GAIN_MAX;
        else if (abs_factor[15:0] < GAIN_FLOOR) gain_from_wall = GAIN_FLOOR;
        else                                     gain_from_wall = abs_factor[15:0];
    end
endfunction

function [7:0] wall_angle;
    input [2:0] wid; input [7:0] asym_mask; input [15:0] rnd;
    reg [7:0] base_ang, jitter;
    begin
        case(wid) 3'd0:base_ang=8'd224; 3'd1:base_ang=8'd32;
            3'd2:base_ang=8'd192; 3'd3:base_ang=8'd64;
            3'd4:base_ang=8'd0; 3'd5:base_ang=8'd128;
            default:base_ang=8'd0; endcase
        jitter = rnd[7:0] & asym_mask;
        wall_angle = rnd[15] ? (base_ang+jitter) : (base_ang-jitter);
    end
endfunction

// ============================================================================
//  메인 FSM
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        state<=GE_IDLE; wall_i<=3'd0; wall_j<=3'd1;
        event_valid<=1'b0; done<=1'b0; rom_addr<=8'd0; rom_load_i<=3'd0;
        room_size_hint_r<=16'h0100;
        wall_dist_r<=16'd0; lst_wall_dist_r<=16'd0;
        tof_prod_r<=32'd0; tof_base_r<=16'd0; tof_lst_r<=16'd0; tof_total_r<=16'd0;
        gain_raw_r<=GAIN_FLOOR; angle_raw_r<=8'd0; gain_accum_r<=20'd0;
        cur_scale_r<=8'h80; cur_max_ref_r<=4'd6; cur_asym_r<=8'h0F;
        lfsr_r<=16'hACE1;
        inv_room_x_r<=22'd0; inv_room_y_r<=22'd0; inv_room_z_r<=22'd0;
        recip_step_r<=2'd0;
        for(li=0;li<6;li=li+1) begin
            abs_rom_r[li]<=8'd64; sct_rom_r[li]<=8'd32;
            ev_tof_r[li]<=16'd0; ev_gain_r[li]<=16'd0; ev_angle_r[li]<=8'd0;
        end
    end else begin
        event_valid<=1'b0; done<=1'b0;
        lfsr_r<=lfsr_next(lfsr_r);

        case (state)

        GE_IDLE: begin
            if (start) begin
                cur_scale_r<=geo_scale(space_id);
                cur_max_ref_r<=geo_max_ref(space_id);
                cur_asym_r<=geo_asym(space_id);
                lfsr_r<=geo_seed(space_id);
                gain_accum_r<=20'd0; rom_load_i<=3'd0;
                rom_addr<={space_id,3'd0};
                state<=GE_ROM_WAIT;
            end
        end

        GE_ROM_WAIT: state<=GE_LATCH_ROM;

        GE_LATCH_ROM: begin
            abs_rom_r[rom_load_i]<=rom_dout[23:16];
            sct_rom_r[rom_load_i]<=rom_dout[31:24];
            if (rom_load_i==3'd0) room_size_hint_r<=rom_dout[15:0];
            if (rom_load_i==3'd5) begin
                recip_step_r<=2'd0;
                state<=GE_CALC_RECIP;
            end else begin
                rom_load_i<=rom_load_i+3'd1;
                rom_addr<={space_id,rom_load_i+3'd1};
                state<=GE_ROM_WAIT;
            end
        end

        // [GEO-v6-3] Reciprocal 사전계산 (3 cycles)
        GE_CALC_RECIP: begin
            case (recip_step_r)
            2'd0: begin inv_room_x_r<=approx_recip_q22(room_x); recip_step_r<=2'd1; end
            2'd1: begin inv_room_y_r<=approx_recip_q22(room_y); recip_step_r<=2'd2; end
            2'd2: begin inv_room_z_r<=approx_recip_q22(room_z); wall_i<=3'd0; state<=GE_WALL_CALC; end
            default: begin wall_i<=3'd0; state<=GE_WALL_CALC; end
            endcase
        end

        // [GEO-v6-1][GEO-v6-2] wall 거리 (reciprocal, 3D)
        GE_WALL_CALC: begin
            if (room_size_hint_r==16'd0) begin
                begin : plate_blk
                    reg [15:0] pb;
                    pb = 16'd80 + {13'd0,wall_i}*16'd13 + {11'd0,space_id}*16'd7;
                    tof_total_r <= pb;
                end
                gain_raw_r<=gain_from_wall(16'h0080,abs_rom_r[wall_i]);
                angle_raw_r<=wall_angle(wall_i,cur_asym_r,lfsr_r);
                state<=GE_EMIT_1ST;
            end else begin
                begin : wd_blk
                    reg [15:0] sc, lc;
                    reg [21:0] inv_sel;
                    sc = wall_src_coord(wall_i,src_x,src_y,src_z,room_x,room_y,room_z);
                    lc = wall_src_coord(wall_i,lst_x,lst_y,lst_z,room_x,room_y,room_z);
                    inv_sel = select_inv_room(wall_i,inv_room_x_r,inv_room_y_r,inv_room_z_r);
                    wall_dist_r     <= wall_dist_recip(sc,inv_sel,room_size_hint_r);
                    lst_wall_dist_r <= wall_dist_recip(lc,inv_sel,room_size_hint_r);
                end
                state<=GE_TOF_PIPE1;
            end
        end

        GE_TOF_PIPE1: begin
            tof_prod_r<={16'd0,wall_dist_r}*{16'd0,SAMP_PER_M_Q8};
            angle_raw_r<=wall_angle(wall_i,cur_asym_r,lfsr_r);
            state<=GE_TOF_PIPE2;
        end

        GE_TOF_PIPE2: begin
            tof_base_r<=(tof_prod_r[31:8]>32'h0000FFFF)?16'hFFFF:tof_prod_r[23:8];
            begin : ltof
                reg [31:0] lp;
                lp={16'd0,lst_wall_dist_r}*{16'd0,SAMP_PER_M_Q8};
                tof_lst_r<=(lp[31:8]>32'h0000FFFF)?16'hFFFF:lp[23:8];
            end
            state<=GE_GAIN_CALC;
        end

        GE_GAIN_CALC: begin
            begin : tsum
                reg [16:0] ts;
                ts={1'b0,tof_base_r}+{1'b0,tof_lst_r};
                tof_total_r<=ts[16]?16'hFFF0:ts[15:0];
            end
            gain_raw_r<=gain_from_wall(
                (wall_dist_r+lst_wall_dist_r>16'hFFF0)?16'hFFF0:wall_dist_r+lst_wall_dist_r,
                abs_rom_r[wall_i]);
            state<=GE_EMIT_1ST;
        end

        GE_EMIT_1ST: begin
            begin : e1
                reg [15:0] fg; reg [7:0] sj, fa;
                sj = lfsr_r[7:0] & (sct_rom_r[wall_i]>>2);
                fa = lfsr_r[8]?(angle_raw_r+sj):(angle_raw_r-sj);
                if ((gain_accum_r+{4'd0,gain_raw_r})>GAIN_SUM_MAX)
                    fg=(GAIN_SUM_MAX[15:0]>gain_accum_r[15:0])?(GAIN_SUM_MAX[15:0]-gain_accum_r[15:0]):GAIN_FLOOR;
                else fg=gain_raw_r;
                gain_accum_r<=gain_accum_r+{4'd0,fg};
                ev_tof_r[wall_i]<=tof_total_r;
                ev_gain_r[wall_i]<=fg;
                ev_angle_r[wall_i]<=fa;
                event_data<={tof_total_r,fg,fa,2'b00,wall_i,2'b01,20'd0};
                event_valid<=1'b1;
            end
            state<=GE_EMIT_2ND;
        end

        GE_EMIT_2ND: begin
            begin : e2
                reg [16:0] t2e; 
                reg [7:0] sj2, a2; 
                reg [15:0] fg2;
                
                // 에러 해결을 위한 임시 레지스터 추가
                reg [15:0] room_hint_val; 
                reg [15:0] t2e_sat;
                reg [19:0] next_gain;     // gain_accum_r의 크기에 맞춘 임시 변수 (최소 20비트 가정)
                reg [15:0] gain_max_16;   // 파라미터 비트 슬라이싱 회피용
                reg [15:0] gain_accum_16; // 레지스터 비트 슬라이싱 회피용

                // 1. 결합 연산자({ }) 내부의 복잡한 삼항 연산 분리
                room_hint_val = (room_size_hint_r == 16'd0) ? 16'd13 : {4'd0, room_size_hint_r[15:4]};
                t2e = {1'b0, tof_total_r} + {1'b0, room_hint_val};
                
                // 결합 연산자에 직접 삼항 연산을 넣지 않도록 사전 포화(Saturation) 처리
                t2e_sat = t2e[16] ? 16'hFFF0 : t2e[15:0];

                // 2. 각도 및 초기 게인 계산
                sj2 = lfsr_r[7:0] & sct_rom_r[wall_i];
                a2 = lfsr_r[15] ? (angle_raw_r + sj2) : (angle_raw_r - sj2);
                
                fg2 = gain_raw_r >> 1; 
                if (fg2 < GAIN_FLOOR) fg2 = GAIN_FLOOR;

                // 3. 누적 게인(GAIN_SUM_MAX) 초과 조건 검사 및 보정
                if (gain_accum_r < GAIN_SUM_MAX) begin
                    next_gain = gain_accum_r + {4'd0, fg2};
                    
                    if (next_gain > GAIN_SUM_MAX) begin
                        // 파라미터(GAIN_SUM_MAX)에 직접 [15:0]을 쓰지 않고 할당을 통해 하위 16비트 추출
                        gain_max_16 = GAIN_SUM_MAX;
                        gain_accum_16 = gain_accum_r;
                        
                        fg2 = (gain_max_16 > gain_accum_16) ? (gain_max_16 - gain_accum_16) : GAIN_FLOOR;
                    end
                    
                    // 4. 최종 할당 (깔끔하게 정돈된 변수들만 결합)
                    gain_accum_r <= gain_accum_r + {4'd0, fg2};
                    event_data <= {t2e_sat, fg2, a2, 2'b00, wall_i, 2'b01, 20'd0};
                    event_valid <= 1'b1;
                end
            end
            if ({1'b0,wall_i}>={1'b0,cur_max_ref_r}-4'd1) begin
                if (quality_en) begin wall_i<=3'd0; wall_j<=3'd1; state<=GE_2ND_PREP; end
                else state<=GE_DONE;
            end else begin wall_i<=wall_i+3'd1; state<=GE_WALL_CALC; end
        end

        GE_2ND_PREP: begin
            begin : sp
                reg [16:0] t2; reg [31:0] g2e;
                t2={1'b0,ev_tof_r[wall_i]}+{1'b0,ev_tof_r[wall_j]};
                g2e=({16'd0,ev_gain_r[wall_i]}+{16'd0,ev_gain_r[wall_j]})>>2;
                tof_total_r<=t2[16]?16'hFFF0:t2[15:0];
                gain_raw_r<=g2e[15:0]<GAIN_FLOOR?GAIN_FLOOR:g2e[15:0]>GAIN_MAX?GAIN_MAX:g2e[15:0];
            end
            state<=GE_2ND_EMIT;
        end

        GE_2ND_EMIT: begin
            begin : se
                reg [8:0] aa; reg [7:0] sm, fa;
                aa={1'b0,ev_angle_r[wall_i]}+{1'b0,ev_angle_r[wall_j]};
                sm=lfsr_r[7:0]&sct_rom_r[wall_i];
                fa=lfsr_r[9]?(aa[8:1]+sm):(aa[8:1]-sm);
                if((gain_accum_r+{4'd0,gain_raw_r})<=GAIN_SUM_MAX) begin
                    gain_accum_r<=gain_accum_r+{4'd0,gain_raw_r};
                    event_data<={tof_total_r,gain_raw_r,fa,2'b00,wall_i,2'b10,20'd0};
                    event_valid<=1'b1;
                end
            end
            if (wall_j==3'd5) begin
                if (wall_i==3'd4) state<=GE_DONE;
                else begin wall_i<=wall_i+3'd1; wall_j<=wall_i+3'd2; state<=GE_2ND_PREP; end
            end else begin wall_j<=wall_j+3'd1; state<=GE_2ND_PREP; end
        end

        GE_DONE: begin done<=1'b1; state<=GE_IDLE; end
        default: state<=GE_IDLE;
        endcase
    end
end

endmodule
