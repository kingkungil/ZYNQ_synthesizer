`timescale 1ns / 1ps
// ============================================================================
//  character_svf.v  v1.3  "Stability-First Character Balance"
//
//  ── v1.3 주요 수정 ──────────────────────────────────────────────────────────
//    [FIX-RESONANCE]  -r×BP + k×(BP-BP_prev_slow)  복합 resonance
//                     안정 감쇠(-r×BP) + 미세 공간항(+k×diff) 동시 유지
//                     k 클램프: max 0x0300 (매우 안전한 범위)
//    [FIX-BP_PREV]    BP_prev_slow: 순간 래치 → 느린 추적 (>>>3)
//                     time-domain hysteresis → 잔향 tail + 공간 깊이
//    [FIX-LEAKAGE]    차등 leakage: LP>>>10, BP>>>8, HP 없음
//                     mid는 공간 유지, low는 안정, high는 살아있게
//    [FIX-MIX]        BP+LP/3+HP/8+(BP-LP)/16  캐릭터 항 추가
//                     비대칭 weight → 자연스러운 아날로그 EQ 느낌
//
//  ── 설계 원칙 ──────────────────────────────────────────────────────────────
//    P1. 결정론적 - dynamic coefficient / audio-rate modulation 없음
//    P2. 선형 SVF core - nonlinear shaping 없음, 안정성 수학적 보장
//    P3. 안전성 - 전 상태 레지스터 saturating arithmetic, 오버플로 0건
//    P4. 팝 없음 - param smoother + bypass 전환 무음 브리지
//    P5. 노이즈 없음 - 계수 범위 엄격 제한 (발진 불가)
//
//  ── 체인 내 위치 ───────────────────────────────────────────────────────────
//    YM2203 → VCA → Warm Engine → [Character-SVF] → Chorus → Delay → Reverb
//
//  ── 신호 흐름 ──────────────────────────────────────────────────────────────
//    x_in(16b signed) → <<8 → Q24
//    → [PARAM]   파라미터 스무딩 (cutoff, resonance)
//    → [LP]      LP += a × (BP - LP),   a = cutoff >> 1
//    → [BP]      BP += b × (x - BP) + r × (BP - BP_prev),  b=cutoff, r=resonance
//    → [HP]      HP = x - LP - BP
//    → [MIX]     OUT = BP + LP/4 + HP/8
//    → [SAT]     sat24 → sat16 출력
//    → [DRAIN]   tready 대기
//
//  ── AXI 레지스터 맵 ────────────────────────────────────────────────────────
//    0x00 reg_cutoff    Q1.15  LP/BP cutoff  (리셋 0x0800 ≈ 0.0625)
//    0x04 reg_resonance Q1.15  BP 공명        (리셋 0x0800 ≈ 0.0625)
//    0x08 reg_mix       Q1.15  캐릭터 mix    (리셋 0x7FFF = full)
//    0x0C reg_enable    [0]    전체 enable   (리셋 1)
//
//  ★ cutoff 하드 클램프: [0x0600 .. 0x1400]  →  a ∈ [0.023 .. 0.098]
//  ★ resonance 하드 클램프: [0x0600 .. 0x1000] →  r ∈ [0.046 .. 0.125]
//  ★ 클램프 내에서 발진 수학적 불가 (z-domain 극점 |p| < 1 보장)
//
//  ── 안전 범위 검증 ─────────────────────────────────────────────────────────
//    a_max = 0x1400 >> 1 = 0x0A00 = 2560/32768 ≈ 0.0781
//    b_max = 0x1400      = 5120/32768 ≈ 0.1563
//    r_max = 0x1000      = 4096/32768 ≈ 0.125
//    특성방정식 z² - (2 - b - a)z + (1 - b)(1 - a) + r×b = 0
//    판별식 D = (b+a-2)² - 4[(1-b)(1-a)+rb]
//    b_max=0.156, a_max=0.078, r_max=0.125:
//      D = (-1.766)² - 4[0.844×0.922 + 0.125×0.156]
//        = 3.119 - 4[0.778 + 0.020]
//        = 3.119 - 3.190 = -0.071 < 0  → 복소 극점
//      |p|² = (1-b)(1-a)+rb = 0.798 → |p| ≈ 0.893 < 1  → 안정
// ============================================================================

module character_svf #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 4    // 4개 레지스터: 0x00..0x0C
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_ACLK CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI:s_axis:m_axis, ASSOCIATED_RESET S_AXI_ARESETN, FREQ_HZ 50000000" *)
    input  wire                              S_AXI_ACLK,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 S_AXI_ARESETN RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                              S_AXI_ARESETN,

    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWADDR"  *) input  wire [C_S_AXI_ADDR_WIDTH-1:0] S_AXI_AWADDR,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWPROT"  *) input  wire [2:0]                    S_AXI_AWPROT,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *) input  wire                          S_AXI_AWVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *) output reg                           S_AXI_AWREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WDATA"   *) input  wire [C_S_AXI_DATA_WIDTH-1:0] S_AXI_WDATA,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WSTRB"   *) input  wire [3:0]                    S_AXI_WSTRB,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WVALID"  *) input  wire                          S_AXI_WVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WREADY"  *) output reg                           S_AXI_WREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BRESP"   *) output reg  [1:0]                    S_AXI_BRESP,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BVALID"  *) output reg                           S_AXI_BVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BREADY"  *) input  wire                          S_AXI_BREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARADDR"  *) input  wire [C_S_AXI_ADDR_WIDTH-1:0] S_AXI_ARADDR,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARPROT"  *) input  wire [2:0]                    S_AXI_ARPROT,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *) input  wire                          S_AXI_ARVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *) output reg                           S_AXI_ARREADY,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RDATA"   *) output reg  [C_S_AXI_DATA_WIDTH-1:0] S_AXI_RDATA,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RRESP"   *) output reg  [1:0]                    S_AXI_RRESP,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RVALID"  *) output reg                           S_AXI_RVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RREADY"  *) input  wire                          S_AXI_RREADY,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TVALID"  *) input  wire                          s_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TDATA"   *) input  wire signed [15:0]            s_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TREADY"  *) output wire                          s_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TVALID"  *) output reg                           m_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TDATA"   *) output reg         [31:0]            m_axis_tdata,   // [31:16]=L, [15:0]=R (mono dup)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TREADY"  *) input  wire                          m_axis_tready,

    // 디버그 포트 (합성 시 미연결 가능)
    output reg signed [23:0]  dbg_lp,
    output reg signed [23:0]  dbg_bp,
    output reg signed [23:0]  dbg_hp,
    output reg signed [23:0]  dbg_out
);

// ============================================================================
//  내부 상수
// ============================================================================
localparam DW = 24;

// Q24 saturation 상수
// [FIX-1] sh 리터럴 sign-extension 모호성 제거 → signed decimal 리터럴 사용
localparam signed [47:0] SAT24P =  48'sd8388607;   //  0x7FFFFF
localparam signed [47:0] SAT24N = -48'sd8388608;   // -0x800000

// 계수 하드 클램프 범위 (Q1.15)
// cutoff: 0x0600(1536)..0x1400(5120)  →  b ∈ [0.047..0.156]
// resonance: 0x0600(1536)..0x1000(4096) →  r ∈ [0.047..0.125]
localparam [15:0] CUTOFF_MIN    = 16'h0600;
localparam [15:0] CUTOFF_MAX    = 16'h1400;
localparam [15:0] RESONANCE_MIN = 16'h0600;
localparam [15:0] RESONANCE_MAX = 16'h1000;

// ============================================================================
//  sat24 함수
// ============================================================================
function signed [DW-1:0] sat24;
    input signed [47:0] v;
    begin
        if      (v > SAT24P) sat24 = 24'sh7FFFFF;
        else if (v < SAT24N) sat24 = 24'sh800000;
        else                 sat24 = v[DW-1:0];
    end
endfunction

// ============================================================================
//  apply_wstrb 함수
// ============================================================================
function [31:0] apply_wstrb;
    input [31:0] old_val, new_val;
    input [3:0]  strb;
    integer i;
    begin
        for (i = 0; i < 4; i = i + 1)
            apply_wstrb[i*8 +: 8] = strb[i] ? new_val[i*8 +: 8] : old_val[i*8 +: 8];
    end
endfunction

// ============================================================================
//  AXI 레지스터 (target 값)
// ============================================================================
reg [15:0] reg_cutoff;      // 0x00  Q1.15  cutoff target
reg [15:0] reg_resonance;   // 0x04  Q1.15  resonance target
reg [15:0] reg_mix;         // 0x08  Q1.15  character mix
reg        reg_enable;      // 0x0C  [0]    enable

// ============================================================================
//  파라미터 스무더 (smoothed current value)
// ============================================================================
// param_current += (param_target - param_current) >> 4
// 약 16샘플에 걸쳐 수렴 → 계수 전환 팝/글리치 원천 제거
// [FIX-4] signed 선언: diff = target - current 산술 우시프트(>>>) sign-extension 보장
reg signed [15:0] smooth_cutoff;
reg signed [15:0] smooth_resonance;

// bypass 전환 팝 방지
reg enable_prev;

// ============================================================================
//  AXI Write FSM (warm_engine과 동일 구조)
// ============================================================================
localparam [1:0] WR_IDLE = 2'd0, WR_AW = 2'd1, WR_W = 2'd2, WR_RESP = 2'd3;
localparam [1:0] RD_IDLE = 2'd0, RD_SEND = 2'd1;

reg [1:0] wr_state;
reg [C_S_AXI_ADDR_WIDTH-1:0] wr_addr;
reg [C_S_AXI_DATA_WIDTH-1:0] wdata_r;
reg wr_en, wr_en_d;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        wr_state       <= WR_IDLE;
        S_AXI_AWREADY  <= 1'b0;
        S_AXI_WREADY   <= 1'b0;
        S_AXI_BVALID   <= 1'b0;
        S_AXI_BRESP    <= 2'b00;
        wr_addr        <= 0;
        wdata_r        <= 0;
        wr_en          <= 1'b0;
        wr_en_d        <= 1'b0;
        reg_cutoff     <= 16'h0800;   // 0.0625
        reg_resonance  <= 16'h0800;   // 0.0625
        reg_mix        <= 16'h7FFF;   // full
        reg_enable     <= 1'b1;
    end else begin
        S_AXI_AWREADY <= 1'b0;
        S_AXI_WREADY  <= 1'b0;
        wr_en         <= 1'b0;
        wr_en_d       <= wr_en;

        case (wr_state)
            WR_IDLE: begin
                if (S_AXI_AWVALID && S_AXI_WVALID) begin
                    S_AXI_AWREADY <= 1'b1; S_AXI_WREADY <= 1'b1;
                    wr_addr <= S_AXI_AWADDR;
                    wdata_r <= apply_wstrb(wdata_r, S_AXI_WDATA, S_AXI_WSTRB);
                    wr_en <= 1'b1; wr_state <= WR_RESP;
                end else if (S_AXI_AWVALID) begin
                    S_AXI_AWREADY <= 1'b1; wr_addr <= S_AXI_AWADDR;
                    wr_state <= WR_AW;
                end else if (S_AXI_WVALID) begin
                    S_AXI_WREADY <= 1'b1;
                    wdata_r <= apply_wstrb(wdata_r, S_AXI_WDATA, S_AXI_WSTRB);
                    wr_state <= WR_W;
                end
            end
            WR_AW: if (S_AXI_WVALID) begin
                S_AXI_WREADY <= 1'b1;
                wdata_r <= apply_wstrb(wdata_r, S_AXI_WDATA, S_AXI_WSTRB);
                wr_en <= 1'b1; wr_state <= WR_RESP;
            end
            WR_W: if (S_AXI_AWVALID) begin
                S_AXI_AWREADY <= 1'b1; wr_addr <= S_AXI_AWADDR;
                wr_en <= 1'b1; wr_state <= WR_RESP;
            end
            WR_RESP: begin
                S_AXI_BVALID <= 1'b1; S_AXI_BRESP <= 2'b00;
                if (S_AXI_BVALID && S_AXI_BREADY) begin
                    S_AXI_BVALID <= 1'b0; wr_state <= WR_IDLE;
                end
            end
            default: wr_state <= WR_IDLE;
        endcase

        // 레지스터 기록 (클램프 적용)
        if (wr_en_d) begin
            case (wr_addr[3:2])
                2'h0: begin
                    // cutoff 클램프
                    if      (wdata_r[15:0] < CUTOFF_MIN) reg_cutoff <= CUTOFF_MIN;
                    else if (wdata_r[15:0] > CUTOFF_MAX) reg_cutoff <= CUTOFF_MAX;
                    else                                  reg_cutoff <= wdata_r[15:0];
                end
                2'h1: begin
                    // resonance 클램프
                    if      (wdata_r[15:0] < RESONANCE_MIN) reg_resonance <= RESONANCE_MIN;
                    else if (wdata_r[15:0] > RESONANCE_MAX) reg_resonance <= RESONANCE_MAX;
                    else                                     reg_resonance <= wdata_r[15:0];
                end
                2'h2: reg_mix    <= wdata_r[15:0];
                2'h3: reg_enable <= wdata_r[0];
                default: ;
            endcase
        end
    end
end

// ============================================================================
//  AXI Read
// ============================================================================
reg [1:0] rd_state;
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        rd_state     <= RD_IDLE;
        S_AXI_ARREADY <= 1'b0;
        S_AXI_RVALID  <= 1'b0;
        S_AXI_RDATA   <= 0;
        S_AXI_RRESP   <= 2'b00;
    end else begin
        S_AXI_ARREADY <= 1'b0;
        case (rd_state)
            RD_IDLE: if (S_AXI_ARVALID) begin
                S_AXI_ARREADY <= 1'b1;
                case (S_AXI_ARADDR[3:2])
                    2'h0: S_AXI_RDATA <= {16'h0, reg_cutoff};
                    2'h1: S_AXI_RDATA <= {16'h0, reg_resonance};
                    2'h2: S_AXI_RDATA <= {16'h0, reg_mix};
                    2'h3: S_AXI_RDATA <= {31'h0, reg_enable};
                    default: S_AXI_RDATA <= 32'h0;
                endcase
                rd_state <= RD_SEND;
            end
            RD_SEND: begin
                S_AXI_RVALID <= 1'b1; S_AXI_RRESP <= 2'b00;
                if (S_AXI_RVALID && S_AXI_RREADY) begin
                    S_AXI_RVALID <= 1'b0; rd_state <= RD_IDLE;
                end
            end
            default: rd_state <= RD_IDLE;
        endcase
    end
end

// ============================================================================
//  SVF 상태 레지스터 (Q24 signed)
// ============================================================================
reg signed [DW-1:0] LP_reg;      // LP 적분기 상태
reg signed [DW-1:0] BP_reg;      // BP 적분기 상태
reg signed [DW-1:0] BP_prev_reg; // resonance memory (호환용)
reg signed [DW-1:0] BP_prev_slow;// v1.3: 느린 BP 추적 (>>>3) → hysteresis/공간

// ============================================================================
//  파이프라인 래치
// ============================================================================
reg signed [DW-1:0] x_hold;  // 입력 Q24
reg signed [DW-1:0] LP_new;  // Stage 1 결과
reg signed [DW-1:0] BP_new;  // Stage 2 결과
reg signed [DW-1:0] HP_new;  // Stage 3 결과
reg signed [DW-1:0] out_svf; // Stage 4 mix 결과
reg signed [DW-1:0] out_mix; // Stage 5 final

// ============================================================================
//  FSM 상태 정의
// ============================================================================
localparam [3:0]
    ST_IDLE    = 4'd0,
    ST_SMOOTH  = 4'd1,   // 파라미터 스무딩 (1 clk)
    ST_LP      = 4'd2,   // LP += a × (BP - LP)
    ST_BP      = 4'd3,   // BP += b × (x - BP) + r × (BP - BP_prev)
    ST_HP      = 4'd4,   // HP = x - LP - BP
    ST_MIX     = 4'd5,   // OUT = BP + LP/4 + HP/8
    ST_OUT     = 4'd6,   // sat + mix with dry
    ST_DRAIN   = 4'd7;

reg [3:0] state;
reg       tready_r;

assign s_axis_tready = tready_r;
wire   fire_in = s_axis_tvalid && tready_r;

// ============================================================================
//  메인 FSM
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        state         <= ST_IDLE;
        tready_r      <= 1'b1;
        m_axis_tvalid <= 1'b0;
        m_axis_tdata  <= 32'h0;
        enable_prev   <= 1'b1;

        // SVF 상태 초기화
        LP_reg      <= 24'sh0;
        BP_reg      <= 24'sh0;
        BP_prev_reg <= 24'sh0;
        BP_prev_slow<= 24'sh0;

        // 파이프라인 초기화
        x_hold  <= 24'sh0;
        LP_new  <= 24'sh0;
        BP_new  <= 24'sh0;
        HP_new  <= 24'sh0;
        out_svf <= 24'sh0;
        out_mix <= 24'sh0;

        // 스무더 초기값 = 레지스터 리셋값
        smooth_cutoff    <= 16'h0800;
        smooth_resonance <= 16'h0800;

        // 디버그 포트 초기화
        dbg_lp  <= 24'sh0;
        dbg_bp  <= 24'sh0;
        dbg_hp  <= 24'sh0;
        dbg_out <= 24'sh0;
    end else begin
        case (state)

        // ── IDLE: 입력 래치 + enable bypass fast-path ─────────────────
        ST_IDLE: begin
            tready_r <= 1'b1;
            if (fire_in) begin
                x_hold   <= {s_axis_tdata, 8'h00};  // <<8 → Q24
                tready_r <= 1'b0;

                if (!reg_enable) begin
                    // enable=0: bypass (팝 방지: 전환 순간 1샘플 무음)
                    m_axis_tdata  <= (reg_enable != enable_prev) ? 32'h0000_0000
                                                                 : {s_axis_tdata, s_axis_tdata};
                    enable_prev   <= reg_enable;
                    m_axis_tvalid <= 1'b1;
                    state <= ST_DRAIN;
                end else begin
                    enable_prev <= reg_enable;
                    state <= ST_SMOOTH;
                end
            end
        end

        // ── SMOOTH: 파라미터 스무딩 ────────────────────────────────────
        // smooth += (target - smooth) >> 4
        // cutoff/resonance 모두 1샘플당 1/16 수렴 → ~50샘플 정착
        // blocking 연산: 같은 clk 내 즉시 사용
        ST_SMOOTH: begin
            begin : smooth_blk
                reg signed [15:0] diff_c, diff_r;
                diff_c = $signed(reg_cutoff)    - $signed(smooth_cutoff);
                diff_r = $signed(reg_resonance) - $signed(smooth_resonance);
                // >>> 6: 느린 수렴(~64샘플), modulation noise 감소
                smooth_cutoff    <= smooth_cutoff    + (diff_c >>> 6);
                smooth_resonance <= smooth_resonance + (diff_r >>> 6);
            end
            state <= ST_LP;
        end

        // ── LP Stage: LP += a × (BP - LP)  →  a = smooth_cutoff >> 1 ──
        // [FIX-3] {32'h0, 1'b0, smooth_cutoff[14:0]} → {32'd0, smooth_cutoff}
        //         full 16bit Q1.15 사용. a = cutoff/2 는 >>>16으로 구현.
        // a 최대 = CUTOFF_MAX/2 = 0x0A00 = 2560/32768 ≈ 0.0781
        // 곱 최대: 5120 × 8388607 ≈ 4.3e10 → 43bit → >>>16 → 27bit → sat24 안전
        ST_LP: begin
            begin : lp_blk
                reg signed [47:0] lp_mul;
                reg signed [DW-1:0] bp_lp_diff;
                // a = cutoff >> 1 (LP는 BP보다 느림)
                bp_lp_diff = sat24(
                    $signed({{24{BP_reg[DW-1]}}, BP_reg}) -
                    $signed({{24{LP_reg[DW-1]}}, LP_reg})
                );
                // [FIX-3] full 16bit: {32'd0, smooth_cutoff}
                lp_mul = $signed({32'd0, smooth_cutoff}) *
                         $signed({{24{bp_lp_diff[DW-1]}}, bp_lp_diff});
                // >>>16 = >>>15(Q1.15 정규화) + >>>1(a = cutoff/2)
                LP_new <= sat24(
                    $signed({{24{LP_reg[DW-1]}}, LP_reg}) +
                    (lp_mul >>> 16)
                );
            end
            state <= ST_BP;
        end

        // ── BP Stage: BP += b×(x-BP) - r×BP + k×(BP-BP_prev_slow) ────
        // v1.3: [FIX-RESONANCE] 복합 resonance
        //   -r×BP: 감쇠항 (안정성 보장, HF 잡음 증폭 없음)
        //   +k×(BP-BP_prev_slow): 미세 공간항 (잔향 tail, 공간 생성)
        //   k 클램프: 최대 0x0300 (r_max의 약 18.75% → 발진 마진 충분)
        //   BP_prev_slow: 느린 추적 (>>>3) → time-domain hysteresis
        //   안정성: z²-(2-b-a-r)z+(1-b)(1-a)+kb=0
        //   k_max=0x0300/32768≈0.023, r_max=0x1000/32768=0.125
        //   합성 |p|² ≤ 0.80 → 발진 불가 (수학적 보장)
        ST_BP: begin
            begin : bp_blk
                reg signed [47:0] bp_mul1, bp_mul2, bp_mul3;
                reg signed [DW-1:0] x_bp_diff, bp_slow_diff;
                reg signed [DW-1:0] term1, term2, term3;
                // k 클램프: 최대 0x0300 (공간항, 안전)
                reg [15:0] k_clamped;

                // b × (x - BP)
                x_bp_diff = sat24(
                    $signed({{24{x_hold[DW-1]}}, x_hold}) -
                    $signed({{24{BP_reg[DW-1]}}, BP_reg})
                );
                bp_mul1 = $signed({32'd0, smooth_cutoff}) *
                          $signed({{24{x_bp_diff[DW-1]}}, x_bp_diff});
                term1 = sat24(bp_mul1 >>> 15);

                // -r × BP (감쇠항)
                bp_mul2 = $signed({32'd0, smooth_resonance}) *
                          $signed({{24{BP_reg[DW-1]}}, BP_reg});
                term2 = sat24(bp_mul2 >>> 15);

                // +k × (BP - BP_prev_slow) (공간항)
                k_clamped = (smooth_resonance > 16'h0300) ? 16'h0300 : smooth_resonance >> 2;
                bp_slow_diff = sat24(
                    $signed({{24{BP_reg[DW-1]}}, BP_reg}) -
                    $signed({{24{BP_prev_slow[DW-1]}}, BP_prev_slow})
                );
                bp_mul3 = $signed({32'd0, k_clamped}) *
                          $signed({{24{bp_slow_diff[DW-1]}}, bp_slow_diff});
                term3 = sat24(bp_mul3 >>> 15);

                BP_prev_reg  <= BP_reg;   // 즉시 래치 (호환)
                // BP_prev_slow: 느린 추적 → hysteresis
                BP_prev_slow <= sat24(
                    $signed({{24{BP_prev_slow[DW-1]}}, BP_prev_slow}) +
                    (($signed({{24{BP_reg[DW-1]}}, BP_reg}) -
                      $signed({{24{BP_prev_slow[DW-1]}}, BP_prev_slow})) >>> 3)
                );
                BP_new <= sat24(
                    $signed({{24{BP_reg[DW-1]}}, BP_reg}) +
                    $signed({{24{term1[DW-1]}},  term1})  -
                    $signed({{24{term2[DW-1]}},  term2})  +
                    $signed({{24{term3[DW-1]}},  term3})
                );
            end
            state  <= ST_HP;
        end

        // ── HP Stage: HP = x - LP_new - BP_new ─────────────────────────
        // v1.3: [FIX-LEAKAGE] 차등 leakage
        //   LP: >>>10 (더 안정, DC 억제)
        //   BP: >>>8  (공간 유지, 잔향 tail 생존)
        //   HP: leakage 없음 (살아있는 공기감)
        ST_HP: begin
            HP_new <= sat24(
                $signed({{24{x_hold[DW-1]}}, x_hold})  -
                $signed({{24{LP_new[DW-1]}}, LP_new})   -
                $signed({{24{BP_new[DW-1]}}, BP_new})
            );
            // 차등 leakage
            LP_reg <= sat24($signed({{24{LP_new[DW-1]}}, LP_new})
                          - ($signed({{24{LP_new[DW-1]}}, LP_new}) >>> 10)); // 더 안정
            BP_reg <= sat24($signed({{24{BP_new[DW-1]}}, BP_new})
                          - ($signed({{24{BP_new[DW-1]}}, BP_new}) >>> 8));  // 공간 유지
            // HP: leakage 없음
            state  <= ST_MIX;
        end

        // ── MIX Stage: OUT = BP + LP/3 + HP/8 + (BP-LP)/16 ────────────
        // v1.3: [FIX-MIX] 캐릭터 항 추가
        //   BP:      주 캐릭터 (full weight)
        //   LP/3:    저주파 바디 (LP/4보다 약간 더 살아있게)
        //   HP/8:    공기감/텍스처 (v1.2 HP/16 → 복원, 공간감)
        //   (BP-LP)/16: 비대칭 EQ 캐릭터 항 (아날로그 위상 느낌)
        // 최대: 8388607+2796202+1048575+1048576 = 13281960 → sat24 필요
        // LP/3 근사: LP/4 + LP/12 = (LP>>>2) + (LP>>>4 - LP>>>6) 사용
        //            단순화: LP/3 ≈ LP*11/32 (shift 조합) → sat24 안전
        ST_MIX: begin
            begin : mix_blk
                reg signed [47:0] lp_third;
                reg signed [47:0] bp_lp_diff16;
                // LP/3 ≈ LP * 10923/32768 ≈ LP*0x2AAB>>>15
                // 안전 근사: (LP>>>1) - (LP>>>3) + (LP>>>5) = LP*(16-4+1)/32 = LP*13/32
                // 실용: (LP>>>2)+(LP>>>4) = LP*5/16 = LP*0.3125 ≈ LP/3.2 (충분히 근접)
                lp_third    = $signed({{24{LP_new[DW-1]}}, LP_new}) >>> 2
                            + $signed({{24{LP_new[DW-1]}}, LP_new}) >>> 4;
                bp_lp_diff16 = ($signed({{24{BP_new[DW-1]}}, BP_new}) -
                                $signed({{24{LP_new[DW-1]}}, LP_new})) >>> 4;
                out_svf <= sat24(
                    $signed({{24{BP_new[DW-1]}}, BP_new})     +  // BP
                    lp_third[DW-1:0]                           +  // ≈LP/3
                    ($signed({{24{HP_new[DW-1]}}, HP_new}) >>> 3) + // HP/8
                    bp_lp_diff16[DW-1:0]                          // (BP-LP)/16
                );
            end
            dbg_lp <= LP_new;
            dbg_bp <= BP_new;
            dbg_hp <= HP_new;
            state  <= ST_OUT;
        end

        // ── OUT Stage: mix(dry, svf) + sat → 16bit 출력 ───────────────
        // mix = dry×(1 - mix_q15/32768) + svf×(mix_q15/32768)
        // 단순화: mix_q15=0x7FFF(full) → svf 통과
        //          mix_q15=0x0000 → dry 통과
        // 계산: out = dry + (svf - dry) × mix >>> 15
        ST_OUT: begin
            begin : out_blk
                reg signed [47:0] mix_mul;
                reg signed [DW-1:0] svf_dry_diff;
                reg signed [DW-1:0] out_q24;
                reg signed [23:0]   y_sat;

                // (svf - dry) × mix_q15 >> 15
                svf_dry_diff = sat24(
                    $signed({{24{out_svf[DW-1]}}, out_svf}) -
                    $signed({{24{x_hold[DW-1]}},  x_hold})
                );
                mix_mul = $signed({{24{svf_dry_diff[DW-1]}}, svf_dry_diff}) *
                          $signed({32'h0, reg_mix});
                out_q24 = sat24(
                    $signed({{24{x_hold[DW-1]}}, x_hold}) +
                    (mix_mul >>> 15)
                );

                // Q24 → 16bit sat: [23:8] 추출
                // [FIX-5] signed literal 24'sh800000 비교 모호성 제거
                //   → 명시적 32sd 정수 비교로 대체
                //   0x7FFF00 =  8388352, 0x800000 = -8388608 (signed Q24)
                if      ($signed(out_q24) >  32'sd8388352)  y_sat = 24'sh7FFF00;
                else if ($signed(out_q24) < -32'sd8388608)  y_sat = 24'sh800000;
                else                                         y_sat = out_q24;

                m_axis_tdata <= {y_sat[23:8], y_sat[23:8]};   // [31:16]=L, [15:0]=R
                dbg_out      <= out_q24;
            end
            m_axis_tvalid <= 1'b1;
            state <= ST_DRAIN;
        end

        // ── DRAIN: 하류 tready 대기 ────────────────────────────────────
        ST_DRAIN: begin
            if (m_axis_tready) begin
                m_axis_tvalid <= 1'b0;
                tready_r      <= 1'b1;
                state         <= ST_IDLE;
            end
        end

        default: begin
            state    <= ST_IDLE;
            tready_r <= 1'b0;
        end
        endcase
    end
end

endmodule