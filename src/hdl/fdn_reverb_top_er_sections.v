// ============================================================================
//  fdn_reverb_top_er_sections.v
//
//  fdn_reverb_top.v에 삽입할 완전한 코드 섹션들
//  각 섹션은 기존 v55 코드의 어느 위치에 삽입/교체하는지 표시됨
//
//  적용 순서:
//    1. SECTION-A: 레지스터 선언 추가
//    2. SECTION-B: 초기화 추가 (reset 블록)
//    3. SECTION-C: AXI write 핸들러 추가
//    4. SECTION-D: AXI read 핸들러 추가
//    5. SECTION-E: wire/assign 추가
//    6. SECTION-F: space_er_engine 인스턴스
//    7. SECTION-G: FS_ER 교체
// ============================================================================

`timescale 1ns / 1ps

// ============================================================================
//  SECTION-A: 레지스터 선언
//  삽입 위치: "reg [31:0] reg_lpf_hf_alpha, reg_lpf_air_alpha;" 다음
// ============================================================================

// ── [ER-PHYS] 공간 물리 ER 엔진 AXI 레지스터 ─────────────────────────────────
// AXI 0x60~0x67
reg [15:0] reg_room_x;          // Q16 meters (방 폭)
reg [15:0] reg_room_y;          // Q16 meters (방 깊이)
reg [15:0] reg_room_z;          // Q16 meters (방 높이)
reg [15:0] reg_src_x;           // 음원 x 위치 Q16
reg [15:0] reg_src_y;           // 음원 y 위치 Q16
reg [15:0] reg_lst_x;           // 청취자 x 위치 Q16
reg [15:0] reg_lst_y;           // 청취자 y 위치 Q16
reg [15:0] reg_cluster_thr;     // cluster 임계값
reg [31:0] reg_er_inject_gain;  // FDN injection gain Q24
reg        reg_geo_en;          // geometry engine 활성
reg        reg_geo_start;       // space 변경 trigger (1-cycle pulse)

// ── [ER-PHYS] 물리 ER 엔진 출력 wire ─────────────────────────────────────────
wire signed [15:0] er_l_phys;
wire signed [15:0] er_r_phys;
wire               er_phys_valid;
wire signed [15:0] x_prediff_injected;
wire signed [15:0] x_prediff_b_injected;

// ============================================================================
//  SECTION-B: 초기화 추가
//  삽입 위치: reset 블록 내 "reg_micro_mod_en <= 1'b1;" 다음
// ============================================================================

// [ER-PHYS] 초기화
// reg_room_x         <= 16'h1E00; // Hall 기본 30m Q16
// reg_room_y         <= 16'h1000; // 16m
// reg_room_z         <= 16'h0800; // 8m
// reg_src_x          <= 16'h0F00; // 15m
// reg_src_y          <= 16'h0A00; // 10m
// reg_lst_x          <= 16'h0F00;
// reg_lst_y          <= 16'h0500; // 5m
// reg_cluster_thr    <= 16'h0200;
// reg_er_inject_gain <= 32'h00200000; // P12_Q24
// reg_geo_en         <= 1'b1;
// reg_geo_start      <= 1'b0;
// space_preset_dirty <= 1'b1; // 부팅 후 한번 물리 엔진 초기화

// ============================================================================
//  SECTION-C: AXI Write 핸들러
//  삽입 위치: 기존 6'h4B 케이스 다음, default: ; 이전
// ============================================================================

// // ── [ER-PHYS] 0x60~0x67 ──────────────────────────────────────────────────
// 6'h60: reg_room_x <= wdata_r[15:0];
// 6'h61: reg_room_y <= wdata_r[15:0];
// 6'h62: reg_room_z <= wdata_r[15:0];
// 6'h63: begin reg_src_x <= wdata_r[31:16]; reg_src_y <= wdata_r[15:0]; end
// 6'h64: begin reg_lst_x <= wdata_r[31:16]; reg_lst_y <= wdata_r[15:0]; end
// 6'h65: reg_cluster_thr <= wdata_r[15:0];
// 6'h66: reg_er_inject_gain <=
//            (wdata_r > 32'h00400000) ? 32'h00400000 : wdata_r;
// 6'h67: begin
//     reg_geo_en <= wdata_r[1];
//     if (wdata_r[0]) begin
//         reg_geo_start      <= 1'b1;
//         space_preset_dirty <= 1'b1;
//     end
// end

// ── geo_start 1-cycle pulse 클리어 (별도 always block)
// always @(posedge S_AXI_ACLK) begin
//     if (!S_AXI_ARESETN) reg_geo_start <= 1'b0;
//     else if (reg_geo_start) reg_geo_start <= 1'b0;
// end

// ── space_preset_dirty가 세트될 때 geo_start도 함께 발생 (기존 dirty 처리 블록 수정)
// 기존 space_preset_dirty 처리 블록 (AXI write always):
//   if (space_preset_dirty && reg_space_preset_en && !preset_loading) begin
//       space_preset_dirty <= 1'b0;
//       er_density_r <= sm_er_den_w;
//       er_spread_r  <= sm_er_spr_w;
//       reg_lpf_alpha    <= sm_lpf_w;
//       reg_a_lpf_alpha  <= sm_lpf_w;
//       reg_pre_diff_g   <= sm_dpre_w;
//       reg_post_diff_g  <= sm_dpost_w;
//       rt60_dirty       <= 1'b1;
//       // [ER-PHYS] 추가: 물리 엔진 재초기화 트리거
//       reg_geo_start    <= 1'b1;  // ← 이 줄 추가
//   end

// ============================================================================
//  SECTION-D: AXI Read 핸들러
//  삽입 위치: 기존 6'h4B 읽기 케이스 다음, default 이전
// ============================================================================

// // ── [ER-PHYS] 0x60~0x67 읽기 ────────────────────────────────────────────
// 6'h60: S_AXI_RDATA <= {16'd0, reg_room_x};
// 6'h61: S_AXI_RDATA <= {16'd0, reg_room_y};
// 6'h62: S_AXI_RDATA <= {16'd0, reg_room_z};
// 6'h63: S_AXI_RDATA <= {reg_src_x, reg_src_y};
// 6'h64: S_AXI_RDATA <= {reg_lst_x, reg_lst_y};
// 6'h65: S_AXI_RDATA <= {16'd0, reg_cluster_thr};
// 6'h66: S_AXI_RDATA <= reg_er_inject_gain;
// 6'h67: S_AXI_RDATA <= {30'd0, reg_geo_en, 1'b0};
// // 버전 업데이트:
// // 6'h3F: S_AXI_RDATA <= 32'h00000038; // v55+ (0x38)

// ============================================================================
//  SECTION-E: wire / cur_prediff 교체
//  삽입 위치: 기존 "wire signed [DATA_W-1:0] cur_prediff" 정의 교체
// ============================================================================

// // 기존 삭제:
// // wire signed [DATA_W-1:0] cur_prediff =
// //     (is_space_b&&(reg_space_mode==SPACE_DUAL))?x_prediff_b:x_prediff;
//
// // [ER-PHYS] 교체:
// wire signed [DATA_W-1:0] cur_prediff_base =
//     (is_space_b && (reg_space_mode == SPACE_DUAL)) ?
//     x_prediff_b : x_prediff;
//
// // injection 활성 시 bridge 출력 사용
// wire signed [DATA_W-1:0] cur_prediff =
//     (reg_geo_en && (reg_er_inject_gain > 32'd0)) ?
//     ((is_space_b && (reg_space_mode == SPACE_DUAL)) ?
//      x_prediff_b_injected : x_prediff_injected) :
//     cur_prediff_base;

// ============================================================================
//  SECTION-F: space_er_engine 인스턴스
//  삽입 위치: endmodule 직전
// ============================================================================

// space_er_engine u_space_er (
//     .clk             (S_AXI_ACLK),
//     .rst_n           (S_AXI_ARESETN),
//
//     // 공간 파라미터
//     .space_id        (reg_space_preset),
//     .space_start     (reg_geo_start),
//     .geo_en          (reg_geo_en),
//
//     // 방 geometry
//     .room_x          (reg_room_x),
//     .room_y          (reg_room_y),
//     .room_z          (reg_room_z),
//     .src_x           (reg_src_x),
//     .src_y           (reg_src_y),
//     .lst_x           (reg_lst_x),
//     .lst_y           (reg_lst_y),
//
//     // cluster / injection
//     .cluster_thr     (reg_cluster_thr),
//     .injection_gain  (reg_er_inject_gain),
//
//     // 오디오 입력
//     .audio_in        (x_pd),
//     .audio_valid     ((state == FS_ER) && (sub == 6'd0)),
//
//     // v55 호환 파라미터
//     .er_gain_scale   (er_gain_scale),
//     .lpf_bright      (LPF_BRIGHT),
//     .lpf_mid         (LPF_MID),
//     .lpf_dark        (LPF_DARK),
//
//     // FDN bridge
//     .fdn_l_in        (x_prediff),
//     .fdn_r_in        (x_prediff_b),
//
//     // 출력
//     .er_l_out        (er_l_phys),
//     .er_r_out        (er_r_phys),
//     .er_valid        (er_phys_valid),
//     .fdn_l_out       (x_prediff_injected),
//     .fdn_r_out       (x_prediff_b_injected),
//
//     // 디버그 (필요 시)
//     .engine_state    (),
//     .cluster_dbg     ()
// );

// ============================================================================
//  SECTION-G: FS_ER 케이스 교체
//  삽입 위치: 메인 FSM case(state) 내부 FS_ER 케이스 전체 교체
//
//  기존 FS_ER (약 50줄, sub 0~3 루프):
//    6'd0: ... er_gl_r, er_gr_r, er_rd_addr ...
//    6'd1: ... er_rd_l ...
//    6'd2: ... er_rd_r ...
//    6'd3: ... er_acc 계산, er_lp_l/r LPF ...
//    → 전체 삭제
//
//  신규 FS_ER (4줄):
// ============================================================================

// FS_ER: begin
//     // [ER-PHYS] 물리 ER 엔진 출력 직결
//     // space_er_engine이 audio_valid pulse에 반응하여
//     // er_l_phys/er_r_phys를 자동 계산 (비동기 파이프라인)
//     // er_phys_valid는 약 8~12 cycle 후 어서팅됨
//
//     // ① er_out 갱신: phys_valid 시만 업데이트 (sample-and-hold)
//     if (er_phys_valid) begin
//         er_out_l <= er_l_phys;
//         er_out_r <= er_r_phys;
//     end
//     // ② phys 준비 전 (공간 전환 중): 이전 값 유지 (자동, er_out_l/r은 reg)
//
//     // ③ pre-diff로 진행
//     diff_rd_addr <= diff_base(5'd0) + diff_wp[0];
//     state        <= FS_PRE_DIFF;
//     sub          <= 6'd0;
// end

// ============================================================================
//  SECTION-H: FS_PREDELAY에서 불필요 라인 제거
//  삽입 위치: FS_PREDELAY case 내부
//  제거 대상 (아래 4줄 삭제):
// ============================================================================

// 기존 삭제 대상:
//   er_acc_l <= {ACC_W{1'b0}};
//   er_acc_r <= {ACC_W{1'b0}};
//   er_tap_i <= 3'd0;
//   er_rom_addr <= {er_pattern, 3'd0};

// ============================================================================
//  SECTION-I: FSM 리셋 블록에서 ER 관련 초기화 제거
//  삽입 위치: 메인 FSM always @(posedge clk) 리셋 블록
//  제거 대상:
// ============================================================================

// 기존 삭제 대상 (reset 블록 내):
//   er_acc_l <= {ACC_W{1'b0}};
//   er_acc_r <= {ACC_W{1'b0}};
//   er_lp_l <= {DATA_W{1'b0}};
//   er_lp_r <= {DATA_W{1'b0}};
//   er_out_l <= {DATA_W{1'b0}};    // → 유지 (er_out은 여전히 사용)
//   er_out_r <= {DATA_W{1'b0}};    // → 유지
//   er_rd_l <= {DATA_W{1'b0}};
//   er_rd_r <= {DATA_W{1'b0}};
//   er_rd_addr <= 13'd0;
//   er_rom_addr <= 7'd0;
//   er_tap_i <= 3'd0;              // → 삭제

// ============================================================================
//  요약: 변경 후 파일 구조
// ============================================================================
//
//  fdn_reverb_top.v v55+
//  ├── AXI 레지스터 (0x00~0x53: v55 기존)
//  ├── AXI 레지스터 (0x60~0x67: [NEW] 물리 ER 파라미터)
//  ├── space_er_engine 인스턴스 [NEW]
//  │   ├── geometry_rom.v
//  │   ├── geometry_engine.v
//  │   ├── reflection_engine.v
//  │   ├── cluster_bram.v
//  │   ├── cluster_engine.v
//  │   ├── er_render_engine.v
//  │   └── er_fdn_bridge.v
//  ├── FS_ER 단순화 [MODIFIED: 50줄 → 4줄]
//  ├── cur_prediff injection 연결 [MODIFIED]
//  └── 나머지 모든 v55 로직 [UNCHANGED]
//
//  BRAM 추가:
//    event_fifo_bram   (16-bit×2048, cluster_bram)
//    cluster_acc_bram  (64-bit×256,  cluster_bram)
//    er_delay_l_bram   (16-bit×4096, space_er_engine)
//    er_delay_r_bram   (16-bit×4096, space_er_engine)
//    geometry_rom_bram (32-bit×256,  geometry_rom)
//    총 5개 BRAM 추가 (기존 v55 BRAM 전부 유지)
// ============================================================================
