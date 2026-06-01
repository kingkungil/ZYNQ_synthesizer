`timescale 1ns / 1ps
// ============================================================================
//  warm_engine.v  v3.1  "ASYM_PRE Leak Fix / Drive Reduction"
//
//  ── v3.1 수정 (오디오 분석 기반 자글 근본 제거) ──────────────────────────
//    [BUG-1] ASYM_PRE LPF α=0x1800이 자글자글의 직접 원인
//        실측: asym=0.7% 무음 구간에서 2500Hz 대역 에너지 지속 (RMS 0.004~0.010)
//        원인: α=0x1800(9.375%) → time constant 길어 신호 오프 후에도 상태 누출
//        수정: α 0x1800 → 0x6000 (37.5%, fc ~3kHz)
//              상태가 입력에 빠르게 수렴 → 무음 시 즉시 drain → 자글 소멸
//    [BUG-2] drive=0x0500에서 asym=0인데도 THD 37% 측정됨
//        원인: FM 신호 + drive×0.625 → waveshaper tanh 포화 영역 진입
//              2차 고조파 -8.6dB = 37% THD → 의도된 warm이 아닌 명확한 왜곡
//        수정: drive 0x0500 → 0x0280 (×0.313) → waveshaper 선형 영역 유지
//    [SMOOTH] ASYM_LPF α 0x2000 → 0x1000 (~1kHz)
//        ASYM_PRE fc 상승(→3kHz)에 맞춰 post-LPF 추가 조임
//        alias/residue 완전 제거, warm 짝수 고조파 질감만 통과
//
//  ── v2.9 수정 ─────────────────────────────────────────────────────────────
//    [TIMING-FIX-2] ST_WAVSHP → ST_WAVSHP + ST_WAVSHP2 분리
//        WNS -0.811ns → 충족. 레이턴시 +1 클럭 (14 clocks/sample)
//    [TEXTURE-v2.9] texture_mode 2bit→3bit 확장, 6단계 0.5%~12%
//
//  ── v2.6 수정 (절대 안정 / 스피커 보호) ──────────────────────────────────
//    [FIX-A] asym_pre 상태·출력 분리 (phase-skew intermodulation 제거)
//        asym_pre_z = LPF 내부 상태만 갱신
//        asym_pre_out = 현 클럭 출력 (ST_ASYM_MUL에서 이것을 읽음)
//        → sample N-1 기반 harmonic 생성 문제 완전 해결
//    [FIX-B] mag_s 반올림 (truncation chatter 제거)
//        기존: mag_s = mag_abs[23:11]  (11bit 버림 → 저레벨 0/1 토글 → 자글)
//        변경: mag_s = (mag_abs + 24'h000400) >> 11  (반올림 → chatter 소멸)
//    [FIX-C] ASYM 전 DC 재차단 1-pole HPF 추가
//        asym_dc_z 상태 레지스터 신설
//        asym_in = asym_pre_out - asym_dc_z  (bias 기인 DC 완전 차단)
//        → tape_bias×x·|x| 저주파 펌핑 → speaker thump 원인 제거
//    [FIX-D] 출력 후단 DC HPF 추가 (speaker thump 최종 방어선)
//        out_dc_z 상태 레지스터 신설
//        y_hpf = y_out - out_dc_x1 + α_out·out_dc_z  (α=0x7F00≈HPF)
//        → 최종 출력 DC 성분 원천 제거
//    [FIX-E] reg_asym_amt reset 대폭 인하: 0x4000→0x0200 (50%→1.6%)
//        소량 asym에서도 자글 발생 → 기본값을 극소로 설정
//    [FIX-F] reg_tape_bias reset: 0x0020→0x0000
//        tape_bias가 DC 오프셋을 만들어 x·|x|와 결합 → thump 원인
//        기본값 0으로 안전하게, 필요 시 AXI로 조정
//    [FIX-G] warmup ramp-in: 즉시 뮤트해제→64샘플 선형 페이드인
//        mute step → broadband impulse 발생 → speaker thump
//        64샘플 ramp으로 부드럽게 해제
//
//    [FIX-1 CRITICAL] soft_sq 완전 제거
//        기존: soft_sq = sq - sq²>>>26  (4차 비선형 → 비균일 harmonic density)
//        변경: sq = mag_s² 그대로 사용  (순수 2차: clean even harmonic만 생성)
//        효과: 지글 원인 1순위 제거. low-level grain noise 소멸.
//    [FIX-2] ASYM_PRE α 강화: 0x3000 → 0x1800 (~1.5kHz)
//        nonlinear 입력 대역을 확실히 제한
//        3차 고조파 최대 = 3 × 1.5kHz = 4.5kHz → Nyquist(25kHz) 훨씬 아래
//        → alias foldback 구조적 차단
//    [FIX-3] ASYM_LPF α 조정: 0x5000 → 0x3800 (~4.5kHz)
//        pre-LPF의 3차 고조파 최대(~4.5kHz)에 post-LPF cutoff을 정렬
//        → harmonic 질감 유지 + 남은 alias residue 정리
//
//  ── 신호 흐름 (v2.5) ───────────────────────────────────────────────────────
//    x_in(16b signed) → <<8 → Q24
//    → [DCBLK]     DC Blocker HPF (α ≈ 0.999)               1 DSP48
//    → [DRIVE]     Soft Drive (sat24, Q4.11 gain)            1 DSP48
//    → [SKNEE]     Soft Knee 2-stage                         1 DSP48
//    → [WAVSHP]    tape_bias + tanh LUT (linear interp)      0 DSP48
//    → [LPF1]      Tape LPF 1단 (leaky integrator)           1 DSP48
//    → [LPF2]      Tape LPF 2단                              1 DSP48
//    → [ASYM_PRE]  pre-alias LPF (α=0x1800,fc≈1.5kHz) ★강화 1 DSP48
//    → [ASYM_MUL]  x·|x| clean even harmonic ★soft_sq제거   1 DSP48
//    → [ASYM_ADD]  parallel blend: z2+(asym_z>>>2)           0 DSP48
//    → [ASYM_LPF]  post smoothing LPF (α=0x3800,fc≈4.5kHz)  1 DSP48
//    → [GAIN]      Makeup gain (Q2.14)                       1 DSP48
//    → [OUT]       sat16 출력
//    → [DRAIN]     tready 대기
//
//  ── AXI 레지스터 맵 ────────────────────────────────────────────────────────
//    0x00 reg_drive       Q4.11  WaveShaper 드라이브   (리셋 0x0600 ≈ 0.75x)
//    0x04 reg_lpf_alpha   Q1.15  Tape LPF 1단 α       (리셋 0x6800, fc≈15kHz)
//    0x08 reg_makeup      Q2.14  출력 makeup gain      (리셋 0x4800 ≈ 1.125x)
//    0x0C reg_tape_bias   s16    DC 바이어스 (<<8→Q24) (리셋 0x0020)
//    0x10 reg_bypass      [0]    전체 바이패스          (리셋 0)
//    0x14 reg_dc_alpha    Q1.15  DC블로커 α            (리셋 0x7F80)
//    0x18 reg_soft_knee   Q1.15  Soft knee width        (리셋 0x1000 ≈ 12.5%)
//    0x30 reg_lpf_alpha2  Q1.15  Tape LPF 2단 α        (리셋 0x5800, fc≈9kHz)
//    0x34 reg_asym_amt    Q1.15  x·|x| fine trim       (리셋 0x0100 = 0.8%)
//    0x38 reg_texture_mode [2:0] perceptual texture level (리셋 3'b001 = tape 1.5%)
//         0=air(0.5%) 1=tape(1.5%) 2=warm(3%) 3=analog(5%) 4=drive(8%) 5+=crush(12%)
//
//  ★ reg_lpf_alpha / reg_lpf_alpha2: HW가 bit15를 마스킹 (≥0x8000 → 발진 방지)
//  ★ asym_amt=0 → ASYM 4 state 전부 스킵 → ST_GAIN 직행
//  ★ 2단 gain: base_gain(texture_mode) × reg_asym_amt >>> 15
//     perceptual gain LUT: 00→0x0028, 01→0x00A0, 10→0x0280, 11→0x0800
//  ★ ASYM 해상도: mag[23:11] 13bit → sq 26bit (순수 x·|x|, soft_sq 없음)
//     최대 출력 < Q24 max (수학적 보장)
//
//  ── 지글 zero-tolerance 구조 요약 ─────────────────────────────────────────
//    nonlinear 입력 대역: ~1.5kHz (ASYM_PRE)
//    최대 harmonic 주파수: 3차 × 1.5kHz = 4.5kHz
//    post-LPF cutoff: ~4.5kHz (ASYM_LPF)
//    alias foldback 가능성: 수학적 0 (Nyquist=25kHz 대비 충분한 여유)
// ============================================================================

module warm_engine #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 6
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
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TDATA"   *) output reg  signed [15:0]            m_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TREADY"  *) input  wire                          m_axis_tready
);

// ============================================================================
//  내부 파라미터
// ============================================================================
localparam DW = 24;
localparam signed [DW-1:0] SAT16_MAX = 24'sh7FFF00;
localparam signed [DW-1:0] SAT16_MIN = 24'sh800000;
localparam signed [47:0]   SAT24P    = 48'h0000007FFFFF;
localparam signed [47:0]   SAT24N    = 48'hFFFFFF800000;

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
//  apply_wstrb
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
//  AXI 레지스터
// ============================================================================
reg [15:0]        reg_drive;
reg [15:0]        reg_lpf_alpha;
reg [15:0]        reg_makeup;
reg signed [15:0] reg_tape_bias;
reg               reg_bypass;
reg [15:0]        reg_dc_alpha;
reg [15:0]        reg_soft_knee;   // 0x18  Q1.15  Soft knee width  (reset 0x1000 = 12.5%)
reg [15:0]        reg_lpf_alpha2;
reg [15:0]        reg_asym_amt;
reg [2:0]         reg_texture_mode;  // 0x38: 6단계 texture (v2.9: 3bit)
reg signed [DW-1:0] pre_out;
reg signed [DW-1:0] out_pad;
// v2.5: ASYM pre-LPF 상태
reg signed [DW-1:0] asym_pre_z;
// v2.6 NEW: LPF 상태와 출력 분리 (phase-skew 제거)
reg signed [DW-1:0] asym_pre_out;
// v2.6 NEW: ASYM 전 DC 재차단 HPF 상태
reg signed [DW-1:0] asym_dc_z;
// v2.6 NEW: 출력 후단 DC HPF 상태 (speaker thump 방어)
reg signed [DW-1:0] out_dc_z;
reg signed [DW-1:0] out_dc_x1;
// v2.6 NEW: warmup ramp-in 카운터 (64샘플 페이드인)
reg [6:0] warmup_ramp;

// ============================================================================
//  AXI Write FSM
// ============================================================================
localparam [1:0] WR_IDLE = 2'd0, WR_AW = 2'd1, WR_W = 2'd2, WR_RESP = 2'd3;
localparam [1:0] RD_IDLE = 2'd0, RD_SEND = 2'd1;
reg [1:0] wr_state;
reg [C_S_AXI_ADDR_WIDTH-1:0] wr_addr;
reg [C_S_AXI_DATA_WIDTH-1:0] wdata_r;
reg wr_en, wr_en_d;

always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        wr_state      <= WR_IDLE;
        S_AXI_AWREADY <= 1'b0;
        S_AXI_WREADY  <= 1'b0;
        S_AXI_BVALID  <= 1'b0;
        S_AXI_BRESP   <= 2'b00;
        wr_addr       <= 0;
        wdata_r       <= 0;
        wr_en         <= 1'b0;
        wr_en_d       <= 1'b0;
        reg_drive     <= 16'h0280;   // v3.1: 0x0500→0x0280 (0.313x) waveshaper 포화 방지
        reg_lpf_alpha <= 16'h6800;
        reg_makeup    <= 16'h4800;
        reg_tape_bias <= 16'h0000;   // v2.6: 0x0020→0 (DC offset 제거)
        reg_bypass    <= 1'b0;
        reg_dc_alpha  <= 16'h7F80;
        reg_soft_knee <= 16'h1000;   // 12.5% knee
        reg_lpf_alpha2<= 16'h5800;
        reg_asym_amt  <= 16'h0100;   // v3.0: 0x0200→0x0100 (0.8%, grain noise 최소화)
        reg_texture_mode <= 3'b001;   // tape(0.5%) 기본
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
        if (wr_en_d) begin
            case (wr_addr[5:2])
                4'h0: reg_drive     <= wdata_r[15:0];
                4'h1: reg_lpf_alpha <= wdata_r[15:0];
                4'h2: reg_makeup    <= wdata_r[15:0];
                4'h3: reg_tape_bias <= wdata_r[15:0];
                4'h4: reg_bypass    <= wdata_r[0];
                4'h5: reg_dc_alpha  <= wdata_r[15:0];
                4'h6: reg_soft_knee <= wdata_r[15:0];
                4'hC: reg_lpf_alpha2<= wdata_r[15:0];
                4'hD: reg_asym_amt  <= wdata_r[15:0];
                4'hE: reg_texture_mode <= wdata_r[2:0];  // 0x38, 3bit
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
        rd_state <= RD_IDLE; S_AXI_ARREADY <= 1'b0;
        S_AXI_RVALID <= 1'b0; S_AXI_RDATA <= 0; S_AXI_RRESP <= 2'b00;
    end else begin
        S_AXI_ARREADY <= 1'b0;
        case (rd_state)
            RD_IDLE: if (S_AXI_ARVALID) begin
                S_AXI_ARREADY <= 1'b1;
                case (S_AXI_ARADDR[5:2])
                    4'h0: S_AXI_RDATA <= {16'h0, reg_drive};
                    4'h1: S_AXI_RDATA <= {16'h0, reg_lpf_alpha};
                    4'h2: S_AXI_RDATA <= {16'h0, reg_makeup};
                    4'h3: S_AXI_RDATA <= {{16{reg_tape_bias[15]}}, reg_tape_bias};
                    4'h4: S_AXI_RDATA <= {31'h0, reg_bypass};
                    4'h5: S_AXI_RDATA <= {16'h0, reg_dc_alpha};
                    4'h6: S_AXI_RDATA <= {16'h0, reg_soft_knee};
                    4'hC: S_AXI_RDATA <= {16'h0, reg_lpf_alpha2};
                    4'hD: S_AXI_RDATA <= {16'h0, reg_asym_amt};
                    4'hE: S_AXI_RDATA <= {29'h0, reg_texture_mode};
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
//  WaveShaper LUT - tanh(i/128), 256 entries
// ============================================================================
(* ram_style = "distributed" *)
reg [15:0] ws_lut [0:255];
initial begin
    ws_lut[  0]=    0; ws_lut[  1]=  266; ws_lut[  2]=  531; ws_lut[  3]=  797;
    ws_lut[  4]= 1062; ws_lut[  5]= 1328; ws_lut[  6]= 1593; ws_lut[  7]= 1858;
    ws_lut[  8]= 2123; ws_lut[  9]= 2387; ws_lut[ 10]= 2652; ws_lut[ 11]= 2916;
    ws_lut[ 12]= 3179; ws_lut[ 13]= 3442; ws_lut[ 14]= 3705; ws_lut[ 15]= 3967;
    ws_lut[ 16]= 4229; ws_lut[ 17]= 4490; ws_lut[ 18]= 4751; ws_lut[ 19]= 5012;
    ws_lut[ 20]= 5271; ws_lut[ 21]= 5530; ws_lut[ 22]= 5788; ws_lut[ 23]= 6046;
    ws_lut[ 24]= 6303; ws_lut[ 25]= 6559; ws_lut[ 26]= 6815; ws_lut[ 27]= 7069;
    ws_lut[ 28]= 7323; ws_lut[ 29]= 7576; ws_lut[ 30]= 7828; ws_lut[ 31]= 8079;
    ws_lut[ 32]= 8330; ws_lut[ 33]= 8579; ws_lut[ 34]= 8827; ws_lut[ 35]= 9074;
    ws_lut[ 36]= 9321; ws_lut[ 37]= 9566; ws_lut[ 38]= 9810; ws_lut[ 39]=10053;
    ws_lut[ 40]=10295; ws_lut[ 41]=10536; ws_lut[ 42]=10775; ws_lut[ 43]=11014;
    ws_lut[ 44]=11251; ws_lut[ 45]=11487; ws_lut[ 46]=11722; ws_lut[ 47]=11955;
    ws_lut[ 48]=12187; ws_lut[ 49]=12418; ws_lut[ 50]=12648; ws_lut[ 51]=12876;
    ws_lut[ 52]=13103; ws_lut[ 53]=13329; ws_lut[ 54]=13553; ws_lut[ 55]=13776;
    ws_lut[ 56]=13997; ws_lut[ 57]=14217; ws_lut[ 58]=14436; ws_lut[ 59]=14653;
    ws_lut[ 60]=14868; ws_lut[ 61]=15083; ws_lut[ 62]=15295; ws_lut[ 63]=15507;
    ws_lut[ 64]=15716; ws_lut[ 65]=15924; ws_lut[ 66]=16131; ws_lut[ 67]=16336;
    ws_lut[ 68]=16540; ws_lut[ 69]=16742; ws_lut[ 70]=16943; ws_lut[ 71]=17142;
    ws_lut[ 72]=17339; ws_lut[ 73]=17535; ws_lut[ 74]=17729; ws_lut[ 75]=17922;
    ws_lut[ 76]=18113; ws_lut[ 77]=18302; ws_lut[ 78]=18490; ws_lut[ 79]=18677;
    ws_lut[ 80]=18862; ws_lut[ 81]=19045; ws_lut[ 82]=19226; ws_lut[ 83]=19406;
    ws_lut[ 84]=19585; ws_lut[ 85]=19761; ws_lut[ 86]=19937; ws_lut[ 87]=20110;
    ws_lut[ 88]=20282; ws_lut[ 89]=20453; ws_lut[ 90]=20621; ws_lut[ 91]=20789;
    ws_lut[ 92]=20954; ws_lut[ 93]=21118; ws_lut[ 94]=21281; ws_lut[ 95]=21442;
    ws_lut[ 96]=21601; ws_lut[ 97]=21759; ws_lut[ 98]=21915; ws_lut[ 99]=22069;
    ws_lut[100]=22222; ws_lut[101]=22374; ws_lut[102]=22524; ws_lut[103]=22672;
    ws_lut[104]=22819; ws_lut[105]=22964; ws_lut[106]=23108; ws_lut[107]=23251;
    ws_lut[108]=23391; ws_lut[109]=23531; ws_lut[110]=23668; ws_lut[111]=23805;
    ws_lut[112]=23939; ws_lut[113]=24073; ws_lut[114]=24205; ws_lut[115]=24335;
    ws_lut[116]=24464; ws_lut[117]=24591; ws_lut[118]=24717; ws_lut[119]=24842;
    ws_lut[120]=24965; ws_lut[121]=25087; ws_lut[122]=25208; ws_lut[123]=25327;
    ws_lut[124]=25444; ws_lut[125]=25561; ws_lut[126]=25675; ws_lut[127]=25789;
    ws_lut[128]=25901; ws_lut[129]=26012; ws_lut[130]=26122; ws_lut[131]=26230;
    ws_lut[132]=26337; ws_lut[133]=26443; ws_lut[134]=26547; ws_lut[135]=26650;
    ws_lut[136]=26752; ws_lut[137]=26853; ws_lut[138]=26952; ws_lut[139]=27051;
    ws_lut[140]=27148; ws_lut[141]=27243; ws_lut[142]=27338; ws_lut[143]=27432;
    ws_lut[144]=27524; ws_lut[145]=27615; ws_lut[146]=27705; ws_lut[147]=27794;
    ws_lut[148]=27881; ws_lut[149]=27968; ws_lut[150]=28053; ws_lut[151]=28138;
    ws_lut[152]=28221; ws_lut[153]=28303; ws_lut[154]=28384; ws_lut[155]=28464;
    ws_lut[156]=28544; ws_lut[157]=28622; ws_lut[158]=28699; ws_lut[159]=28775;
    ws_lut[160]=28850; ws_lut[161]=28924; ws_lut[162]=28997; ws_lut[163]=29069;
    ws_lut[164]=29140; ws_lut[165]=29210; ws_lut[166]=29279; ws_lut[167]=29347;
    ws_lut[168]=29415; ws_lut[169]=29481; ws_lut[170]=29547; ws_lut[171]=29612;
    ws_lut[172]=29676; ws_lut[173]=29738; ws_lut[174]=29801; ws_lut[175]=29862;
    ws_lut[176]=29922; ws_lut[177]=29982; ws_lut[178]=30041; ws_lut[179]=30099;
    ws_lut[180]=30156; ws_lut[181]=30212; ws_lut[182]=30268; ws_lut[183]=30323;
    ws_lut[184]=30377; ws_lut[185]=30430; ws_lut[186]=30483; ws_lut[187]=30535;
    ws_lut[188]=30586; ws_lut[189]=30636; ws_lut[190]=30686; ws_lut[191]=30735;
    ws_lut[192]=30783; ws_lut[193]=30831; ws_lut[194]=30878; ws_lut[195]=30924;
    ws_lut[196]=30970; ws_lut[197]=31015; ws_lut[198]=31060; ws_lut[199]=31103;
    ws_lut[200]=31147; ws_lut[201]=31189; ws_lut[202]=31231; ws_lut[203]=31272;
    ws_lut[204]=31313; ws_lut[205]=31353; ws_lut[206]=31393; ws_lut[207]=31432;
    ws_lut[208]=31470; ws_lut[209]=31508; ws_lut[210]=31546; ws_lut[211]=31583;
    ws_lut[212]=31619; ws_lut[213]=31655; ws_lut[214]=31690; ws_lut[215]=31725;
    ws_lut[216]=31759; ws_lut[217]=31793; ws_lut[218]=31826; ws_lut[219]=31859;
    ws_lut[220]=31891; ws_lut[221]=31923; ws_lut[222]=31954; ws_lut[223]=31985;
    ws_lut[224]=32016; ws_lut[225]=32046; ws_lut[226]=32075; ws_lut[227]=32104;
    ws_lut[228]=32133; ws_lut[229]=32161; ws_lut[230]=32189; ws_lut[231]=32217;
    ws_lut[232]=32244; ws_lut[233]=32270; ws_lut[234]=32297; ws_lut[235]=32323;
    ws_lut[236]=32348; ws_lut[237]=32373; ws_lut[238]=32398; ws_lut[239]=32422;
    ws_lut[240]=32446; ws_lut[241]=32470; ws_lut[242]=32493; ws_lut[243]=32516;
    ws_lut[244]=32539; ws_lut[245]=32561; ws_lut[246]=32583; ws_lut[247]=32605;
    ws_lut[248]=32626; ws_lut[249]=32647; ws_lut[250]=32668; ws_lut[251]=32688;
    ws_lut[252]=32709; ws_lut[253]=32728; ws_lut[254]=32748; ws_lut[255]=32767;
end

// ============================================================================
//  파이프라인 레지스터
// ============================================================================
reg signed [DW-1:0] x_hold, x_dc, x_drv, x_ws, x_ws2, y_out;
reg signed [DW-1:0] dc_x_z1, dc_y_z1, tape_z1, tape_z2;
// v2.2: ASYM_LPF 1차 leaky integrator 상태 변수
// α = 0x2000 (25%), Fc ≈ Fs × 0.25 / (2π) ≈ 2kHz @ 50kHz
// → ASYM 비선형이 만든 alias/quantization residue 제거, 순수 짝수 고조파만 통과
reg signed [DW-1:0] asym_z;

// Soft Knee 파이프라인 중간 래치 (ST_SKNEE → ST_SKNEE2)
reg signed [DW-1:0] sk_tanh_r;   // tanh(x_drv) Q24
reg signed [DW-1:0] sk_dist_r;   // (|x| - knee_lo), signed Q24, 25bit 범위
reg                 sk_active_r;  // 1=knee 구간 안에 있음

// WaveShaper 파이프라인 중간 래치 (ST_WAVSHP → ST_WAVSHP2)
// v2.9: abs+LUT 결과 래치 → DSP interp를 별도 클럭으로 분리 (타이밍)
reg signed [DW-1:0] ws_bias_sig_r;  // bias 가산 후 부호 있는 신호
reg [DW-1:0]        ws_mag_r;       // 절대값
reg [7:0]           ws_idx_r;       // LUT 인덱스 (mag[22:16])
reg [7:0]           ws_frac_r;      // 보간 frac (mag[15:8])

// DC Blocker 파이프라인 중간 래치 (ST_DCBLK → ST_DCBLK2)
reg signed [47:0]   dc_fb_r;     // alpha × y_z1 (raw DSP48 출력)
reg signed [47:0]   dc_diff_r;   // x[n] - x[n-1]

reg signed [47:0] tmp48;
reg signed [DW-1:0] tmp_sig;
reg [DW-1:0] tmp_mag;
reg [7:0]    tmp_ws_idx;
reg [15:0]   tmp_lut_val;
// v3.0: ASYM_MUL 중간값 래치 (타이밍 분리용)
reg [23:0]   asym_mid_r;    // sq_norm × asym_amt >> 15 결과
reg [15:0]   asym_gain_r;   // base_gain (texture LUT 결과)
reg          asym_neg_r;    // 부호 (x_ws 음수 여부)

// ============================================================================
//  FSM 상태 정의
// ============================================================================
localparam [4:0]
    ST_IDLE       = 5'd0,
    ST_DCBLK      = 5'd1,
    ST_DCBLK2     = 5'd2,
    ST_DRIVE      = 5'd3,
    ST_SKNEE      = 5'd4,
    ST_SKNEE2     = 5'd5,
    ST_WAVSHP     = 5'd6,   // v2.9: abs + LUT 인덱스 래치만 (타이밍 분리)
    ST_WAVSHP2    = 5'd17,  // v2.9: NEW - DSP interp + sat24 → x_ws 확정
    ST_LPF1       = 5'd7,
    ST_LPF2       = 5'd8,
    ST_ASYM_PRE   = 5'd9,
    ST_ASYM_PRE2  = 5'd16,
    ST_ASYM_MUL   = 5'd10,
    ST_ASYM_MUL2  = 5'd18,  // v3.0: NEW - mid×base_gain→tmp48 (타이밍 분리)
    ST_ASYM_ADD   = 5'd11,
    ST_ASYM_LPF   = 5'd12,
    ST_GAIN       = 5'd13,
    ST_OUT        = 5'd14,
    ST_DRAIN      = 5'd15;

reg [4:0] state;
reg       tready_r;
reg [6:0] warmup_cnt;
wire      warmup_done = warmup_cnt[6];
assign    s_axis_tready = tready_r;
wire      fire_in = s_axis_tvalid && tready_r;

// ============================================================================
//  메인 FSM
// ============================================================================
always @(posedge S_AXI_ACLK) begin
    if (!S_AXI_ARESETN) begin
        state         <= ST_IDLE;
        tready_r      <= 1'b1;
        m_axis_tvalid <= 1'b0;
        m_axis_tdata  <= 16'h0;
        warmup_cnt    <= 7'h0;
        x_hold <= 0; x_dc <= 0; x_drv <= 0; x_ws <= 0; x_ws2 <= 0; y_out <= 0;
        dc_x_z1 <= 0; dc_y_z1 <= 0; tape_z1 <= 0; tape_z2 <= 0; asym_z <= 0;
        asym_pre_z <= 0; asym_pre_out <= 0;
        asym_dc_z  <= 0;
        out_dc_z   <= 0; out_dc_x1 <= 0;
        warmup_ramp <= 7'h0;
        sk_tanh_r <= 0; sk_dist_r <= 0; sk_active_r <= 0;
        ws_bias_sig_r <= 0; ws_mag_r <= 0; ws_idx_r <= 0; ws_frac_r <= 0;
        dc_fb_r   <= 0; dc_diff_r <= 0;
        asym_mid_r <= 0; asym_gain_r <= 0; asym_neg_r <= 0;
    end else begin
        case (state)

        // ── IDLE: 입력 래치 + <<8 + bypass fast-path ─────────────────────
        ST_IDLE: begin
            tready_r <= 1'b1;
            if (fire_in) begin
                x_hold   <= {s_axis_tdata, 8'h00};
                tready_r <= 1'b0;
                if (reg_bypass) begin
                    m_axis_tdata  <= s_axis_tdata;
                    m_axis_tvalid <= 1'b1;
                    state <= ST_DRAIN;
                end else begin
                    state <= ST_DCBLK;
                end
            end
        end

        // ── DC Blocker 1단: diff + DSP48 곱셈 래치 ───────── 1 DSP48 ──
        // diff = x[n] - x[n-1], fb = alpha × y[n-1]  → 각각 래치
        ST_DCBLK: begin
            if (!warmup_done) warmup_cnt <= warmup_cnt + 7'h1;
            if (reg_dc_alpha == 16'h0) begin
                // alpha=0: 바이패스 경로
                x_dc    <= x_hold;
                dc_x_z1 <= x_hold;
                dc_y_z1 <= x_hold;
                state   <= ST_DRIVE;
            end else begin
                dc_diff_r <= $signed({{24{x_hold[DW-1]}},   x_hold})
                           - $signed({{24{dc_x_z1[DW-1]}}, dc_x_z1});
                dc_fb_r   <= $signed({32'h0, reg_dc_alpha})
                           * $signed({{24{dc_y_z1[DW-1]}}, dc_y_z1});
                dc_x_z1   <= x_hold;   // z1 업데이트는 여기서
                state     <= ST_DCBLK2;
            end
        end

        // ── DC Blocker 2단: acc + sat24 → x_dc 확정 ─────── 0 DSP48 ──
        // y[n] = diff + (fb >>> 15)  - 덧셈 + 시프트만, 새 DSP48 불필요
        ST_DCBLK2: begin
            begin : dcblk2
                reg signed [DW-1:0] y_new;
                y_new   = sat24(dc_diff_r + (dc_fb_r >>> 15));
                x_dc    <= y_new;
                dc_y_z1 <= y_new;
            end
            state <= ST_DRIVE;
        end

        // ── Drive + sat24 ─────────────────────────────────── 1 DSP48 ──
        // x_drv = sat24(x_dc × drive >>> 11)
        ST_DRIVE: begin
            tmp48 = $signed({{24{x_dc[DW-1]}}, x_dc})
                  * $signed({32'h0, reg_drive});
            x_drv <= sat24(tmp48 >>> 11);
            state <= ST_SKNEE;
        end

        // ── Soft Knee 1단: |x|, LUT 조회, knee 판정 → 래치 ─── 0 DSP48 ──
        // 나눗셈/곱셈 없음. 조합 경로: abs→LUT→비교(24bit 감산) 만.
        // 결과는 sk_tanh_r / sk_dist_r / sk_active_r 에 저장 후 ST_SKNEE2 진행.
        ST_SKNEE: begin
            if (reg_soft_knee == 16'h0) begin
                // knee=0: 스킵
                sk_active_r <= 1'b0;
                state       <= ST_WAVSHP;
            end else begin
                // ── 1. 절대값 ──
                begin : sk1_abs
                    reg [DW-1:0] sk_mag;
                    reg [7:0]    sk_idx;
                    reg [15:0]   sk_lut_v;
                    reg signed [DW-1:0] sk_tanh_v;
                    reg [DW-1:0] knee_lo;
                    reg signed [DW:0]   dist_v;    // 25bit signed

                    if (x_drv == 24'sh800000)      sk_mag = 24'h7FFFFF;
                    else if (x_drv[DW-1])          sk_mag = $unsigned(-x_drv);
                    else                            sk_mag = $unsigned(x_drv);

                    // ── 2. LUT 인덱스 (상위 7bit만 → 시프트) ──
                    sk_idx    = sk_mag[22:16];      // mag>>16, 범위 0..127

                    sk_lut_v  = (sk_idx == 8'h00 && x_drv != 24'sh0)
                                ? 16'h0001 : ws_lut[sk_idx];
                    // tanh 부호 복원
                    sk_tanh_v = x_drv[DW-1]
                                ? -$signed({sk_lut_v, 8'h00})
                                :  $signed({sk_lut_v, 8'h00});

                    // ── 3. knee_lo = 0x7FFFFF - (knee_w << 7) ──
                    //    knee_w(16bit)<<7 → 최대 0x3FFF80, 항상 24bit 이하 안전
                    knee_lo = 24'h7FFFFF - {reg_soft_knee[15:0], 7'h00};

                    // ── 4. dist = |x| - knee_lo  (순수 감산, 25bit) ──
                    dist_v = $signed({1'b0, sk_mag}) - $signed({1'b0, knee_lo});

                    // 래치
                    sk_tanh_r  <= sk_tanh_v;
                    // dist_r: knee 구간 내에서만 양수, 아닐 때 0 래치
                    if (dist_v <= 0) begin
                        sk_dist_r  <= 24'sh0;
                        sk_active_r <= 1'b0;
                    end else begin
                        // dist를 24bit로 포화 (최대 knee_w<<7 ≈ 0x3FFF80)
                        sk_dist_r  <= (dist_v[DW]) ? 24'sh7FFFFF
                                                    : dist_v[DW-1:0];
                        sk_active_r <= 1'b1;
                    end
                end
                state <= ST_SKNEE2;
            end
        end

        // ── Soft Knee 2단: blend 시프트 근사 + lerp ──────── 1 DSP48 ──
        // blend = clamp(dist >> log2(knee_w), 0, 1) Q15 근사
        // knee_w를 2의 거듭제곱으로 올림 → blend = dist >> shift_n
        // lerp = x_drv + (tanh - x_drv)*blend >> 15  (1 DSP48 사용)
        // FF 추가: sk_tanh_r(24b) + sk_dist_r(24b) + sk_active_r(1b) = 49 FF
        ST_SKNEE2: begin
            if (!sk_active_r) begin
                // knee 하한 이하: x_drv 유지
                state <= ST_WAVSHP;
            end else begin : sk2_lerp
                // ── blend 근사: dist >> (15 - floor_log2(knee_w)) ──
                // knee_w Q1.15: 0x1000=12.5%, 0x2000=25%, 0x0800=6.25%
                // floor_log2 를 reg_soft_knee 상위 비트 위치로 결정
                // knee_w 비트 범위 4..14 → shift_n = 15 - bit_pos
                // 실용 근사: blend = dist >> shift_n, shift_n은 4비트 LUT
                reg [3:0]  shift_n;
                reg [14:0] blend_q15;
                reg signed [47:0] lerp48;
                reg signed [DW-1:0] diff;

                // shift_n 결정: knee_w MSB 위치 기반 (순수 LUT)
                // reg_soft_knee 범위: 0x0400..0x2800 실용 범위
                casez (reg_soft_knee[15:8])
                    8'b1???????: shift_n = 4'd7;   // ≥0x8000 (발진방지용, 사실상 미사용)
                    8'b01??????: shift_n = 4'd8;
                    8'b001?????: shift_n = 4'd9;
                    8'b0001????: shift_n = 4'd10;
                    8'b00001???: shift_n = 4'd11;
                    8'b000001??: shift_n = 4'd12;
                    8'b0000001?: shift_n = 4'd13;
                    default:     shift_n = 4'd14;
                endcase

                // blend_q15 = clamp(dist >> shift_n, 0, 32767)
                // dist는 최대 24bit → dist>>shift_n 최대 24bit
                // shift_n >= 7 이므로 결과 최대 17bit → 15bit 포화
                begin : blend_calc
                    reg [23:0] blend_raw;
                    blend_raw = $unsigned(sk_dist_r) >> shift_n;
                    blend_q15 = (blend_raw > 24'd32767) ? 15'h7FFF
                                                        : blend_raw[14:0];
                end

                // lerp: x_drv + (tanh - x_drv) * blend_q15 >> 15
                // (tanh - x_drv) 최대 ±Q24, blend_q15 15bit → 39bit 곱
                // → 1 DSP48 (A×B: 30×18 → P)
                diff   = sat24($signed({{24{sk_tanh_r[DW-1]}}, sk_tanh_r})
                              - $signed({{24{x_drv[DW-1]}},    x_drv}));
                lerp48 = $signed({{24{x_drv[DW-1]}}, x_drv})
                        + ($signed({{24{diff[DW-1]}},  diff})
                           * $signed({33'h0, blend_q15}) >>> 15);
                x_drv <= sat24(lerp48);
                state <= ST_WAVSHP;
            end
        end

        // ── WaveShaper 1단: bias 가산 + abs + LUT 인덱스 래치 ─ 0 DSP48 ──
        // v2.9: [타이밍 분리] abs·LUT조회만 수행, DSP interp는 ST_WAVSHP2에서
        //   기존: abs→LUT→DSP interp→sat24→x_ws 31 LL → WNS -0.811ns
        //   분리: ST_WAVSHP(abs+LUT인덱스 래치) / ST_WAVSHP2(DSP+sat24→x_ws)
        //   각 스테이트 ~15 LL → 타이밍 충족
        ST_WAVSHP: begin : wavshp_logic
            reg signed [DW-1:0] biased;
            reg [DW-1:0] abs_biased; // 절대값을 저장할 중간 변수
        
            // 1. Bias 가산 (Bit-width를 DW에 맞춰 명시)
            biased = sat24($signed({{24{x_drv[DW-1]}}, x_drv})
                   + $signed({{8{reg_tape_bias[15]}}, reg_tape_bias, 8'h00}));
        
            // 2. 절대값 계산 (24'sh800000 대신 명시적 비트 비교)
            if (biased == {1'b1, {(DW-1){1'b0}}}) begin // 최솟값(2's complement min) 체크
                abs_biased = {1'b0, {(DW-1){1'b1}}}; // Max Positive
            end else if (biased[DW-1]) begin
                abs_biased = -biased;
            end else begin
                abs_biased = biased;
            end
        
            // 3. 레지스터 할당
            ws_mag_r      <= abs_biased;
            ws_bias_sig_r <= biased;
            
            // LUT 인덱스 및 Frac (abs_biased를 사용하면 코드가 훨씬 간결해집니다)
            ws_idx_r      <= abs_biased[22:16];
            ws_frac_r     <= abs_biased[15:8];
        
            state <= ST_WAVSHP2;
        end

        // ── WaveShaper 2단: DSP48 interp + sat24 → x_ws ─── 1 DSP48 ──
        // v2.9: ws_bias_sig_r / ws_idx_r / ws_frac_r 래치 기반 계산
        //   frac(8bit) × (y1-y0)(16bit) → delta / interp → 부호 복원 → x_ws
        ST_WAVSHP2: begin
            begin : wavshp2_blk
                reg [15:0] y0_v, y1_v;
                reg [31:0] delta_v;
                reg [15:0] interp_v;

                // LUT 조회 (래치된 인덱스 기반 → 조합 경로 단절)
                y0_v = (ws_idx_r == 8'h00 && ws_bias_sig_r != 24'sh0)
                       ? 16'h0001 : ws_lut[ws_idx_r];
                y1_v = (ws_idx_r == 8'h7F) ? 16'hFFFF : ws_lut[ws_idx_r + 8'h1];

                // linear interp
                delta_v  = ({16'h0, ws_frac_r} * {16'h0, y1_v - y0_v}) >> 8;
                interp_v = y0_v + delta_v[15:0];

                // 부호 복원
                if (ws_bias_sig_r[DW-1])
                    x_ws <= -$signed({interp_v, 8'h00});
                else
                    x_ws <=  $signed({interp_v, 8'h00});
            end
            state <= ST_LPF1;
        end

        // ── LPF1: Tape LPF 1단 (leaky integrator) ────────── 1 DSP48 ──
        // v2.1: ASYM 앞에 배치 → LPF가 먼저 고주파 잡음 제거 후 ASYM 적용
        // z1 += alpha × (x_ws - z1) >>> 15
        ST_LPF1: begin
            tmp_sig = sat24($signed({{24{x_ws[DW-1]}}, x_ws})
                          - $signed({{24{tape_z1[DW-1]}}, tape_z1}));
            // alpha × diff (bit15 마스킹으로 발진 방지)
            tmp48 = $signed({32'h0, 1'b0, reg_lpf_alpha[14:0]})
                  * $signed({{24{tmp_sig[DW-1]}}, tmp_sig});
            tape_z1 <= sat24($signed({{24{tape_z1[DW-1]}}, tape_z1})
                      + (tmp48 >>> 15));
            state <= ST_LPF2;
        end

        // ── LPF2: Tape LPF 2단 ───────────────────────────── 1 DSP48 ──
        // z2 += alpha2 × (z1 - z2) >>> 15
        ST_LPF2: begin
            tmp_sig = sat24($signed({{24{tape_z1[DW-1]}}, tape_z1})
                          - $signed({{24{tape_z2[DW-1]}}, tape_z2}));
            tmp48 = $signed({32'h0, 1'b0, reg_lpf_alpha2[14:0]})
                  * $signed({{24{tmp_sig[DW-1]}}, tmp_sig});
            tape_z2 <= sat24($signed({{24{tape_z2[DW-1]}}, tape_z2})
                      + (tmp48 >>> 15));
            // v2.4: asym_amt=0 → ASYM 4 state 전부 스킵, ST_GAIN 직행
            if (reg_asym_amt == 16'h0)
                state <= ST_GAIN;
            else
                state <= ST_ASYM_PRE;
        end

        // ── ASYM_PRE: pre-LPF before nonlinear ───────────── 1 DSP48 ──
        // v3.1: α = 0x6000 (37.5%, fc≈50kHz×0.375/(2π)≈3kHz)
        //   [변경] 0x1800(9.375%, ~750Hz) → 0x6000(37.5%, ~3kHz)
        //   이유: α가 작을수록 상태(asym_pre_z)의 time constant가 길어짐
        //         신호 오프 후에도 수십ms 동안 ~2500Hz 에너지가 누출 → 자글자글 직접 원인
        //         α 크게 → 상태가 입력에 빠르게 수렴 → 무음 시 즉시 0으로 drain
        //         fc 3kHz: 비선형 입력 대역 3kHz (충분), ASYM_LPF(1kHz)가 alias 정리
        ST_ASYM_PRE: begin
            begin : asym_pre_blk
                reg signed [DW-1:0] filtered;

                // LPF: tape_z2 → filtered (α=0x6000)
                tmp_sig  = sat24($signed({{24{tape_z2[DW-1]}}, tape_z2})
                               - $signed({{24{asym_pre_z[DW-1]}}, asym_pre_z}));
                tmp48    = $signed(48'h0000_0000_6000)
                         * $signed({{24{tmp_sig[DW-1]}}, tmp_sig});
                filtered = sat24($signed({{24{asym_pre_z[DW-1]}}, asym_pre_z})
                         + (tmp48 >>> 15));

                // 상태·출력 래치만 → DC HPF는 다음 스테이트
                asym_pre_z   <= filtered;
                asym_pre_out <= filtered;
            end
            state <= ST_ASYM_PRE2;
        end

        // ── ASYM_PRE2: DC HPF (타이밍 분리, v2.8 NEW) ────── 1 DSP48 ──
        // filtered(=asym_pre_out)가 이미 래치된 후 DC HPF 계산
        // α_dc=0x7E00(≈0.992, fc≈640Hz), asym_in=filtered-asym_dc_z
        ST_ASYM_PRE2: begin
            begin : asym_pre2_blk
                reg signed [47:0] dc_tmp;

                // DC HPF: asym_dc_z LP 업데이트
                dc_tmp    = $signed(48'h0000_0000_7E00)
                          * $signed($signed({{24{asym_pre_out[DW-1]}}, asym_pre_out})
                          - $signed({{24{asym_dc_z[DW-1]}}, asym_dc_z}));
                asym_dc_z <= sat24($signed({{24{asym_dc_z[DW-1]}}, asym_dc_z})
                           + (dc_tmp >>> 15));
                // DC 제거된 nonlinear 입력 → x_ws 재활용
                x_ws      <= sat24($signed({{24{asym_pre_out[DW-1]}}, asym_pre_out})
                           - $signed({{24{asym_dc_z[DW-1]}}, asym_dc_z}));
            end
            state <= ST_ASYM_MUL;
        end

        // ── ASYM_MUL: sq_norm + mid 래치 ─────────────────── 1 DSP48 ──
        // v3.0: [TIMING-FIX-3] 2단 곱셈을 두 클럭으로 분리
        //   이 스테이트: mag_abs → sq_norm → mid(=sq×asym_amt>>15) 래치
        //   DSP48 사용: sq_norm × asym_amt 1개만
        //   base_gain / 부호도 함께 래치 → 다음 클럭에 DSP48 1개만 사용
        ST_ASYM_MUL: begin
            begin : asym_mul_blk
                reg [DW-1:0]  mag_abs;
                reg [12:0]    mag_s;
                reg [25:0]    sq;
                reg [23:0]    sq_norm;
                reg [15:0]    base_gain;

                // DC 제거된 입력 절대값
                if (x_ws == 24'sh800000)     mag_abs = 24'h7FFFFF;
                else if (x_ws[DW-1])         mag_abs = $unsigned(-x_ws);
                else                         mag_abs = $unsigned(x_ws);

                // 반올림 (truncation chatter 제거)
                mag_s   = (mag_abs + 24'h000400) >> 11;
                sq      = mag_s * mag_s;               // 26bit
                sq_norm = sq[25:2];                    // >>2 → 24bit Q24 정규화

                // ── texture LUT v2.9: 6단계 0.5%~12% ──
                case (reg_texture_mode)
                    3'd0:    base_gain = 16'h0040;  // air    0.5%
                    3'd1:    base_gain = 16'h00C0;  // tape   1.5%
                    3'd2:    base_gain = 16'h0180;  // warm   3.0%
                    3'd3:    base_gain = 16'h0280;  // analog 5.0%
                    3'd4:    base_gain = 16'h0666;  // drive  8.0%
                    default: base_gain = 16'h0999;  // crush 12.0%
                endcase

                // 1단 곱셈만: sq_norm × asym_amt >> 15 → mid 래치
                asym_mid_r  <= ({8'h0, sq_norm} * {16'h0, reg_asym_amt}) >> 15;
                asym_gain_r <= base_gain;
                asym_neg_r  <= x_ws[DW-1];
            end
            state <= ST_ASYM_MUL2;
        end

        // ── ASYM_MUL2: mid×base_gain → tmp48 ─────────────── 1 DSP48 ──
        // v3.0: [TIMING-FIX-3] 2단 곱셈 분리 2단계
        //   래치된 mid(asym_mid_r) × base_gain(asym_gain_r) → tmp48
        //   조합 경로: 래치→DSP48→래치 (단일 DSP48, ~8ns)
        ST_ASYM_MUL2: begin
            if (asym_neg_r)
                tmp48 <= -($signed({8'h0, asym_mid_r}) * $signed({16'h0, asym_gain_r}));
            else
                tmp48 <=  ($signed({8'h0, asym_mid_r}) * $signed({16'h0, asym_gain_r}));
            state <= ST_ASYM_ADD;
        end

        // ── ASYM_ADD: scaled term → x_ws2 래치 ──────────── 0 DSP48 ──
        // v2.7: term = mid × base_gain (40bit) → >>>15 → 25bit → sat24
        //   mid 최대 0x3FFF80(22bit), base_gain 최대 0x0800(11bit)
        //   곱 최대 2^33 → >>>15 → 18bit → sat24 여유 충분
        ST_ASYM_ADD: begin
            begin : asym_add_blk
                reg signed [47:0] asym_term;
                asym_term = tmp48 >>> 15;
                x_ws2 <= sat24(
                    $signed({{24{tape_z2[DW-1]}}, tape_z2}) + asym_term
                );
            end
            state <= ST_ASYM_LPF;
        end

        // ── ASYM_LPF: post-harmonic smoothing LPF ─────────── 1 DSP48 ──
        // v3.1: α = 0x1000 (6.25%, fc≈50kHz×0.0625/(2π)≈500Hz → 실효 ~1kHz)
        //   [변경] 0x2000(~2kHz) → 0x1000(~1kHz)
        //   ASYM_PRE α 대폭 증가(→3kHz)로 인해 harmonic 대역도 올라감
        //   post-LPF를 더 조여 alias/residue 완전 제거, warm 질감만 남김
        ST_ASYM_LPF: begin
            tmp_sig = sat24($signed({{24{x_ws2[DW-1]}}, x_ws2})
                          - $signed({{24{asym_z[DW-1]}}, asym_z}));
            // α = 0x1000 (6.25%, bit15=0 → 발진 방지 OK)
            tmp48 = $signed(48'h0000_0000_1000) * $signed({{24{tmp_sig[DW-1]}}, tmp_sig});
            asym_z <= sat24($signed({{24{asym_z[DW-1]}}, asym_z})
                     + (tmp48 >>> 15));
            state <= ST_GAIN;
        end
        // ── Makeup Gain ───────────────────────────────────── 1 DSP48 ──
        // v3.0: blend >>>3(12.5%) → >>>4(6.25%)
        //   ASYM_LPF α 강화(0x2000)와 함께 asym 혼합비 절반
        //   → 미세 asym_amt(0.7%) 구간 자글 완전 소멸
        ST_GAIN: begin
            begin : gain_blk
                reg signed [DW-1:0] mix_sig;
                reg signed [47:0] gain_mul;

                if (reg_asym_amt == 16'h0)
                    mix_sig = tape_z2;
                else
                    // parallel blend: dry + 6.25% asym harmonic
                    mix_sig = sat24(
                        $signed({{24{tape_z2[DW-1]}}, tape_z2}) +
                        ($signed({{24{asym_z[DW-1]}}, asym_z}) >>> 4)
                    );

                gain_mul =
                    $signed({{24{mix_sig[DW-1]}}, mix_sig}) *
                    $signed({32'h0, reg_makeup});

                y_out <= sat24(gain_mul >>> 14);
            end

            state <= ST_OUT;
        end

        // ── Output: Q24 → 16-bit 포화 ────────────────────────────────
        // v2.6:
        //   [G] warmup ramp-in: 즉시 뮤트해제 → 64샘플 선형 페이드인
        //       step mute → broadband impulse 방지
        //   [D] 출력 후단 DC HPF: out_dc_z 1-pole HPF (α=0x7F00≈0.996, fc≈160Hz)
        //       y_hpf = y_out - out_dc_x1 + α·out_dc_z
        //       → 최종 출력 DC 성분 완전 소거 → speaker thump 방어
        ST_OUT: begin
            begin : out_blk
                reg signed [DW-1:0] y_hpf;
                reg signed [47:0]   hpf_tmp;
                reg signed [DW-1:0] y_scaled;
                reg signed [DW-1:0] y_ramped;
                reg [6:0]           ramp_scale;

                // [D] 출력 후단 DC HPF
                //   out_dc_z: y_out의 저주파 성분 추적 (LP)
                //   y_hpf   = y_out - out_dc_x1 + α·out_dc_z  (HP)
                hpf_tmp = $signed(48'h0000_0000_7F00)
                        * $signed({{24{out_dc_z[DW-1]}}, out_dc_z});
                y_hpf = sat24(
                    $signed({{24{y_out[DW-1]}}, y_out})
                  - $signed({{24{out_dc_x1[DW-1]}}, out_dc_x1})
                  + (hpf_tmp >>> 15)
                );
                // LP 상태 업데이트
                hpf_tmp   = $signed(48'h0000_0000_7F00)
                          * $signed($signed({{24{y_out[DW-1]}}, y_out})
                          - $signed({{24{out_dc_z[DW-1]}}, out_dc_z}));
                out_dc_z  <= sat24($signed({{24{out_dc_z[DW-1]}}, out_dc_z})
                           + (hpf_tmp >>> 15));
                out_dc_x1 <= y_out;

                // -6dB pad
                y_scaled = y_hpf >>> 1;

                // [G] warmup ramp-in (64샘플 선형 페이드인)
                if (!warmup_done) begin
                    m_axis_tdata <= 16'h0000;
                end else begin
                    if (warmup_ramp < 7'd64) begin
                        // ramp: output × warmup_ramp/64
                        warmup_ramp <= warmup_ramp + 7'd1;
                        y_ramped = ($signed({{24{y_scaled[DW-1]}}, y_scaled})
                                  * $signed({17'h0, warmup_ramp})) >>> 6;
                        if      ($signed(y_ramped) > $signed(SAT16_MAX)) m_axis_tdata <= 16'sh7FFF;
                        else if ($signed(y_ramped) < $signed(SAT16_MIN)) m_axis_tdata <= 16'sh8000;
                        else m_axis_tdata <= y_ramped[23:8];
                    end else begin
                        // 정상 출력
                        if      ($signed(y_scaled) > $signed(SAT16_MAX)) m_axis_tdata <= 16'sh7FFF;
                        else if ($signed(y_scaled) < $signed(SAT16_MIN)) m_axis_tdata <= 16'sh8000;
                        else m_axis_tdata <= y_scaled[23:8];
                    end
                end
            end
            m_axis_tvalid <= 1'b1;
            state <= ST_DRAIN;
        end

        // ── DRAIN: 하류 tready 대기 ──────────────────────────────────
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