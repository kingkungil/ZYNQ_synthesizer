// ============================================================================
//  sig_gen.v  v1.0
//
//  범용 신호 발생기 (fdn_reverb_top 리버브 디버깅용)
//
//  ─── 인터페이스 개요 ────────────────────────────────────────────────────
//
//  [입력]
//    clk      : 시스템 클럭 (50 MHz 등)
//    rst_n    : Active-low reset
//    tick     : 1클럭 폭 펄스, 오디오 샘플레이트(Fs)와 동기
//               외부에서 분주기로 생성 → sig_gen에 주입
//    gpio0~3  : AXI GPIO IP 출력 32비트 × 4  (읽기 전용)
//
//  [출력]
//    dout       : signed 16비트 샘플 (2의 보수)
//    dout_valid : tick과 동기하는 1클럭 폭 write-enable
//                 → FIFO 의 wr_en 핀에 직결
//                 → FIFO 출력 측을 AXI-Stream master로 연결
//
//  ─── GPIO 비트 맵 ──────────────────────────────────────────────────────
//
//  gpio0
//    [ 3: 0]  sig_type    신호 종류
//               0  SILENCE      : 0 출력
//               1  IMPULSE      : 단발 또는 주기 반복 임펄스
//               2  SWEEP        : 선형 주파수 스윕 사인파
//               3  TONE         : 단일 주파수 (sin / 구형 / 삼각)
//               4  NOISE        : 백색 노이즈 (16비트 LFSR)
//               5  STEP         : DC 스텝 / 방형파 토글
//               6  BURST        : 주기적 임펄스 버스트
//               7  LFO_TONE     : 사인파 + LFO AM 변조
//               8  SWEEP_LOG    : 로그(지수) 주파수 스윕
//               9  PINK_NOISE   : 핑크 노이즈 (3-pole IIR)
//              10  CHIRP_BURST  : 버스트 내 주파수 스윕 처프
//    [ 7: 4]  sub_opt     서브 옵션 (각 모드별 설명 참조)
//    [31: 8]  base_inc    기본 위상 증분 Q16
//               = round( f_Hz / Fs_Hz * 65536 )
//               예) Fs=48kHz, f=1kHz → 1365
//
//  gpio1
//    [15: 0]  amplitude   출력 스케일 (0x7FFF = 0 dBFS)
//    [31:16]  lfo_inc     LFO 위상 증분 Q16  (LFO_TONE 전용)
//
//  gpio2
//    [23: 0]  sweep_start 스윕/처프 시작 주파수 증분 Q16
//
//  gpio3
//    [23: 0]  sweep_end   스윕/처프 종료 주파수 증분 Q16
//                          (BURST/IMPULSE 반복의 경우: 반복 주기 샘플 수)
//    [31:24]  burst_len   버스트 샘플 길이 (BURST/CHIRP_BURST)
//
//  ─── sub_opt 상세 ───────────────────────────────────────────────────────
//    IMPULSE     [0]: 0=원샷  1=주기 반복 (주기=sweep_end[23:0] 샘플)
//    SWEEP       [0]: 0=업    1=다운
//                [1]: 0=1회   1=루프
//    TONE        [1:0]: 0=사인  1=구형파  2=삼각파
//    NOISE       [0]: 0=flat  1=HPF shaped
//    STEP        [0]: 0=원샷(high 고정)  1=토글 방형파
//    BURST       [1:0]: 반복 주기 승수  0→1x 1→2x 2→4x 3→8x
//    LFO_TONE    [1:0]: 캐리어 파형  0=사인 1=구형파 2=삼각파
//    SWEEP_LOG   [0]: 0=업  1=다운   [2]: 루프
//    PINK_NOISE  사용 안 함
//    CHIRP_BURST 사용 안 함
//
// ============================================================================

`timescale 1ns / 1ps

module sig_gen (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        tick,           // Fs 주기 1클럭 폭 펄스

    // AXI GPIO (axi_gpio IP의 gpio_io_o 출력 직결)
    input  wire [31:0] gpio0,
    input  wire [31:0] gpio1,
    input  wire [31:0] gpio2,
    input  wire [31:0] gpio3,

    // FIFO 직결 출력
    output reg  signed [15:0] dout,
    output reg                dout_valid
);

// ============================================================================
//  GPIO 필드 분해
// ============================================================================
wire [3:0]  sig_type    = gpio0[3:0];
wire [3:0]  sub_opt     = gpio0[7:4];
wire [23:0] base_inc    = gpio0[31:8];   // 주파수 증분 Q16

wire [15:0] amplitude   = gpio1[15:0];
wire [15:0] lfo_inc_w   = gpio1[31:16];

wire [23:0] sweep_start = gpio2[23:0];
wire [23:0] sweep_end_w = gpio3[23:0];
wire [7:0]  burst_len   = gpio3[31:24];

// ============================================================================
//  신호 타입 상수
// ============================================================================
localparam [3:0]
    T_SILENCE     = 4'd0,
    T_IMPULSE     = 4'd1,
    T_SWEEP       = 4'd2,
    T_TONE        = 4'd3,
    T_NOISE       = 4'd4,
    T_STEP        = 4'd5,
    T_BURST       = 4'd6,
    T_LFO_TONE    = 4'd7,
    T_SWEEP_LOG   = 4'd8,
    T_PINK        = 4'd9,
    T_CHIRP_BURST = 4'd10;

localparam signed [15:0] FULL_SCALE = 16'sh7FFF;

// ============================================================================
//  256엔트리 사인 LUT  (0 ~ π/2,  1/4 주기 대칭)
//  sin_lut[i] = round(32767 * sin(i * π/2 / 256))
// ============================================================================
(* rom_style = "distributed" *)
reg signed [15:0] sin_lut [0:255];

integer lut_i;
initial begin
    // 핵심 값 직접 기재 (IEEE 754 sin 근사)
    sin_lut[  0]=16'sd0;     sin_lut[  1]=16'sd402;   sin_lut[  2]=16'sd804;
    sin_lut[  3]=16'sd1206;  sin_lut[  4]=16'sd1608;  sin_lut[  5]=16'sd2009;
    sin_lut[  6]=16'sd2410;  sin_lut[  7]=16'sd2811;  sin_lut[  8]=16'sd3212;
    sin_lut[  9]=16'sd3612;  sin_lut[ 10]=16'sd4011;  sin_lut[ 11]=16'sd4410;
    sin_lut[ 12]=16'sd4808;  sin_lut[ 13]=16'sd5205;  sin_lut[ 14]=16'sd5602;
    sin_lut[ 15]=16'sd5998;  sin_lut[ 16]=16'sd6393;  sin_lut[ 17]=16'sd6787;
    sin_lut[ 18]=16'sd7180;  sin_lut[ 19]=16'sd7571;  sin_lut[ 20]=16'sd7962;
    sin_lut[ 21]=16'sd8351;  sin_lut[ 22]=16'sd8739;  sin_lut[ 23]=16'sd9126;
    sin_lut[ 24]=16'sd9512;  sin_lut[ 25]=16'sd9896;  sin_lut[ 26]=16'sd10279;
    sin_lut[ 27]=16'sd10660; sin_lut[ 28]=16'sd11039; sin_lut[ 29]=16'sd11417;
    sin_lut[ 30]=16'sd11793; sin_lut[ 31]=16'sd12167; sin_lut[ 32]=16'sd12539;
    sin_lut[ 33]=16'sd12910; sin_lut[ 34]=16'sd13279; sin_lut[ 35]=16'sd13646;
    sin_lut[ 36]=16'sd14010; sin_lut[ 37]=16'sd14373; sin_lut[ 38]=16'sd14733;
    sin_lut[ 39]=16'sd15090; sin_lut[ 40]=16'sd15446; sin_lut[ 41]=16'sd15799;
    sin_lut[ 42]=16'sd16150; sin_lut[ 43]=16'sd16499; sin_lut[ 44]=16'sd16846;
    sin_lut[ 45]=16'sd17190; sin_lut[ 46]=16'sd17531; sin_lut[ 47]=16'sd17870;
    sin_lut[ 48]=16'sd18205; sin_lut[ 49]=16'sd18538; sin_lut[ 50]=16'sd18868;
    sin_lut[ 51]=16'sd19195; sin_lut[ 52]=16'sd19519; sin_lut[ 53]=16'sd19841;
    sin_lut[ 54]=16'sd20159; sin_lut[ 55]=16'sd20475; sin_lut[ 56]=16'sd20787;
    sin_lut[ 57]=16'sd21097; sin_lut[ 58]=16'sd21403; sin_lut[ 59]=16'sd21706;
    sin_lut[ 60]=16'sd22005; sin_lut[ 61]=16'sd22301; sin_lut[ 62]=16'sd22594;
    sin_lut[ 63]=16'sd22884; sin_lut[ 64]=16'sd23170; sin_lut[ 65]=16'sd23453;
    sin_lut[ 66]=16'sd23732; sin_lut[ 67]=16'sd24008; sin_lut[ 68]=16'sd24279;
    sin_lut[ 69]=16'sd24548; sin_lut[ 70]=16'sd24812; sin_lut[ 71]=16'sd25073;
    sin_lut[ 72]=16'sd25330; sin_lut[ 73]=16'sd25583; sin_lut[ 74]=16'sd25832;
    sin_lut[ 75]=16'sd26077; sin_lut[ 76]=16'sd26319; sin_lut[ 77]=16'sd26557;
    sin_lut[ 78]=16'sd26790; sin_lut[ 79]=16'sd27020; sin_lut[ 80]=16'sd27245;
    sin_lut[ 81]=16'sd27466; sin_lut[ 82]=16'sd27684; sin_lut[ 83]=16'sd27897;
    sin_lut[ 84]=16'sd28106; sin_lut[ 85]=16'sd28311; sin_lut[ 86]=16'sd28511;
    sin_lut[ 87]=16'sd28707; sin_lut[ 88]=16'sd28898; sin_lut[ 89]=16'sd29085;
    sin_lut[ 90]=16'sd29268; sin_lut[ 91]=16'sd29447; sin_lut[ 92]=16'sd29621;
    sin_lut[ 93]=16'sd29791; sin_lut[ 94]=16'sd29956; sin_lut[ 95]=16'sd30117;
    sin_lut[ 96]=16'sd30273; sin_lut[ 97]=16'sd30425; sin_lut[ 98]=16'sd30572;
    sin_lut[ 99]=16'sd30714; sin_lut[100]=16'sd30852; sin_lut[101]=16'sd30985;
    sin_lut[102]=16'sd31114; sin_lut[103]=16'sd31238; sin_lut[104]=16'sd31357;
    sin_lut[105]=16'sd31471; sin_lut[106]=16'sd31581; sin_lut[107]=16'sd31686;
    sin_lut[108]=16'sd31786; sin_lut[109]=16'sd31881; sin_lut[110]=16'sd31972;
    sin_lut[111]=16'sd32058; sin_lut[112]=16'sd32138; sin_lut[113]=16'sd32214;
    sin_lut[114]=16'sd32285; sin_lut[115]=16'sd32351; sin_lut[116]=16'sd32412;
    sin_lut[117]=16'sd32468; sin_lut[118]=16'sd32520; sin_lut[119]=16'sd32567;
    sin_lut[120]=16'sd32609; sin_lut[121]=16'sd32646; sin_lut[122]=16'sd32678;
    sin_lut[123]=16'sd32706; sin_lut[124]=16'sd32729; sin_lut[125]=16'sd32747;
    sin_lut[126]=16'sd32761; sin_lut[127]=16'sd32770; sin_lut[128]=16'sd32767;
    // 129~255 : 거울 대칭
    for (lut_i = 129; lut_i <= 255; lut_i = lut_i + 1)
        sin_lut[lut_i] = sin_lut[256 - lut_i];
end

// ============================================================================
//  sin_val 함수  (위상 32비트 → signed 16비트)
//  phase[31:30]=사분면, phase[29:22]=LUT 인덱스(8비트)
// ============================================================================
function signed [15:0] sin_val;
    input [31:0] ph;
    reg [1:0]  quad;
    reg [7:0]  idx;
    begin
        quad = ph[31:30];
        idx  = ph[29:22];
        case (quad)
            2'd0: sin_val =  sin_lut[idx];
            2'd1: sin_val =  sin_lut[8'd255 - idx];
            2'd2: sin_val = -sin_lut[idx];
            2'd3: sin_val = -sin_lut[8'd255 - idx];
        endcase
    end
endfunction

// ============================================================================
//  삼각파
// ============================================================================
function signed [15:0] tri_val;
    input [31:0] ph;
    reg [1:0]  quad;
    reg [14:0] ramp;
    begin
        quad = ph[31:30];
        ramp = ph[29:15];
        case (quad)
            2'd0: tri_val =  $signed({1'b0, ramp});
            2'd1: tri_val =  $signed({1'b0, 15'h7FFF}) - $signed({1'b0, ramp});
            2'd2: tri_val = -$signed({1'b0, ramp});
            2'd3: tri_val = -$signed({1'b0, 15'h7FFF}) + $signed({1'b0, ramp});
        endcase
    end
endfunction

// ============================================================================
//  구형파
// ============================================================================
function signed [15:0] sq_val;
    input [31:0] ph;
    begin
        sq_val = ph[31] ? 16'sh8001 : 16'sh7FFF;
    end
endfunction

// ============================================================================
//  진폭 적용  (16bit × 16bit → 상위 16bit)
// ============================================================================
function signed [15:0] apply_amp;
    input signed [15:0] sig;
    input        [15:0] amp;
    reg signed [31:0] prod;
    begin
        prod = $signed(sig) * $signed({1'b0, amp});
        apply_amp = prod[30:15];
    end
endfunction

// ============================================================================
//  포화 클램프
// ============================================================================
function signed [15:0] sat16;
    input signed [31:0] v;
    begin
        if      (v >  32'sh00007FFF) sat16 =  16'sh7FFF;
        else if (v < -32'sh00008000) sat16 = -16'sh8000;
        else                         sat16 =  v[15:0];
    end
endfunction

// ============================================================================
//  내부 레지스터
// ============================================================================
reg [31:0] phase_acc;       // 메인 위상 누산기
reg [31:0] lfo_acc;         // LFO 위상 누산기
reg [15:0] lfsr_r;          // 16비트 Galois LFSR
reg [23:0] sweep_cur_inc;   // 현재 스윕 증분
reg [31:0] burst_period;    // 버스트/임펄스 반복 카운터
reg [15:0] burst_cnt;       // 버스트 내 샘플 카운터
reg        impulse_done;
reg        step_toggle;
reg [3:0]  sig_type_prev;

// 핑크 노이즈 IIR 누산
reg signed [31:0] pink_b0, pink_b1, pink_b2;

// ============================================================================
//  16비트 Galois LFSR  (탭: x^16+x^14+x^13+x^11+1 → 0xB400)
// ============================================================================
wire lfsr_fb     = lfsr_r[0];
wire [15:0] lfsr_next = {1'b0, lfsr_r[15:1]} ^ (lfsr_fb ? 16'hB400 : 16'h0000);

// ============================================================================
//  메인 always 블록
// ============================================================================
always @(posedge clk) begin
    if (!rst_n) begin
        phase_acc     <= 32'd0;
        lfo_acc       <= 32'd0;
        lfsr_r        <= 16'hACE1;
        sweep_cur_inc <= 24'd0;
        burst_period  <= 32'd0;
        burst_cnt     <= 16'd0;
        impulse_done  <= 1'b0;
        step_toggle   <= 1'b0;
        sig_type_prev <= 4'hF;
        pink_b0       <= 32'sd0;
        pink_b1       <= 32'sd0;
        pink_b2       <= 32'sd0;
        dout          <= 16'sd0;
        dout_valid    <= 1'b0;

    end else begin
        dout_valid <= 1'b0;

        // ── 신호 타입 변경 시 상태 초기화 ──────────────────────────────
        if (sig_type != sig_type_prev) begin
            phase_acc     <= 32'd0;
            lfo_acc       <= 32'd0;
            sweep_cur_inc <= sweep_start;
            burst_period  <= 32'd0;
            burst_cnt     <= 16'd0;
            impulse_done  <= 1'b0;
            step_toggle   <= 1'b0;
            pink_b0       <= 32'sd0;
            pink_b1       <= 32'sd0;
            pink_b2       <= 32'sd0;
            sig_type_prev <= sig_type;
        end

        if (tick) begin
            dout_valid <= 1'b1;

            // LFSR 매 샘플 갱신 (노이즈 소스 공통)
            lfsr_r <= lfsr_next;

            case (sig_type)

            // ──────────────────────────────────────────────────────────────
            //  0: SILENCE
            // ──────────────────────────────────────────────────────────────
            T_SILENCE: begin
                dout <= 16'sd0;
            end

            // ──────────────────────────────────────────────────────────────
            //  1: IMPULSE
            //     원샷: 첫 샘플만 full-scale, 이후 0
            //     반복(sub_opt[0]=1): sweep_end[23:0] 샘플 주기마다 재발사
            // ──────────────────────────────────────────────────────────────
            T_IMPULSE: begin
                if (!impulse_done) begin
                    dout         <= apply_amp(FULL_SCALE, amplitude);
                    impulse_done <= 1'b1;
                    burst_period <= 32'd1;
                end else begin
                    dout <= 16'sd0;
                    if (sub_opt[0]) begin
                        if (burst_period >= {8'd0, sweep_end_w}) begin
                            burst_period <= 32'd0;
                            impulse_done <= 1'b0;
                        end else
                            burst_period <= burst_period + 32'd1;
                    end
                end
            end

            // ──────────────────────────────────────────────────────────────
            //  2: SWEEP  (선형 주파수 스윕 사인파)
            //     sub_opt[0]=0: 업  / 1: 다운
            //     sub_opt[1]=1: 루프
            //     스윕 속도: base_inc[7:0] 가 매 샘플 증분 변화량
            // ──────────────────────────────────────────────────────────────
            T_SWEEP: begin
                dout      <= apply_amp(sin_val(phase_acc), amplitude);
                phase_acc <= phase_acc + {8'd0, sweep_cur_inc};

                if (!sub_opt[0]) begin
                    // 업 스윕
                    if (sweep_cur_inc < sweep_end_w) begin
                        sweep_cur_inc <= sweep_cur_inc + {16'd0, base_inc[7:0]};
                        if (sweep_cur_inc + {16'd0, base_inc[7:0]} > sweep_end_w)
                            sweep_cur_inc <= sweep_end_w;
                    end else begin
                        if (sub_opt[1]) begin
                            sweep_cur_inc <= sweep_start;
                            phase_acc     <= 32'd0;
                        end
                    end
                end else begin
                    // 다운 스윕
                    if (sweep_cur_inc > sweep_start) begin
                        if (sweep_cur_inc >= sweep_start + {16'd0, base_inc[7:0]})
                            sweep_cur_inc <= sweep_cur_inc - {16'd0, base_inc[7:0]};
                        else
                            sweep_cur_inc <= sweep_start;
                    end else begin
                        if (sub_opt[1]) begin
                            sweep_cur_inc <= sweep_end_w;
                            phase_acc     <= 32'd0;
                        end
                    end
                end
            end

            // ──────────────────────────────────────────────────────────────
            //  3: TONE  (단일 주파수)
            //     sub_opt[1:0]: 0=사인 / 1=구형파 / 2=삼각파
            // ──────────────────────────────────────────────────────────────
            T_TONE: begin
                case (sub_opt[1:0])
                    2'd0: dout <= apply_amp(sin_val(phase_acc), amplitude);
                    2'd1: dout <= apply_amp(sq_val (phase_acc), amplitude);
                    2'd2: dout <= apply_amp(tri_val(phase_acc), amplitude);
                    default: dout <= apply_amp(sin_val(phase_acc), amplitude);
                endcase
                phase_acc <= phase_acc + {8'd0, base_inc};
            end

            // ──────────────────────────────────────────────────────────────
            //  4: NOISE  (LFSR 백색 노이즈)
            //     sub_opt[0]=0: flat
            //     sub_opt[0]=1: 1차 HPF shaped (차분 → 고역 강조)
            // ──────────────────────────────────────────────────────────────
            T_NOISE: begin
                if (!sub_opt[0]) begin
                    dout <= apply_amp($signed(lfsr_r), amplitude);
                end else begin
                    // y[n] = x[n] - x[n-1]  (HPF)
                    dout <= apply_amp(
                        sat16($signed(lfsr_next) - $signed(lfsr_r)),
                        amplitude);
                end
            end

            // ──────────────────────────────────────────────────────────────
            //  5: STEP
            //     sub_opt[0]=0: DC 스텝 (0 → full-scale 원샷)
            //     sub_opt[0]=1: 방형파 토글 (주기=sweep_end[23:0] 샘플)
            // ──────────────────────────────────────────────────────────────
            T_STEP: begin
                if (!sub_opt[0]) begin
                    dout <= apply_amp(FULL_SCALE, amplitude);
                end else begin
                    if (burst_period >= {8'd0, sweep_end_w}) begin
                        burst_period <= 32'd0;
                        step_toggle  <= ~step_toggle;
                    end else
                        burst_period <= burst_period + 32'd1;
                    dout <= step_toggle
                            ?  apply_amp(FULL_SCALE, amplitude)
                            : -apply_amp(FULL_SCALE, amplitude);
                end
            end

            // ──────────────────────────────────────────────────────────────
            //  6: BURST  (주기적 임펄스 버스트)
            //     버스트 길이: burst_len 샘플  (gpio3[31:24])
            //     반복 주기 : sweep_end_w 샘플 × (1 << sub_opt[1:0])
            // ──────────────────────────────────────────────────────────────
            T_BURST: begin
                begin : burst_blk
                    reg [31:0] period;
                    period = {8'd0, sweep_end_w} << {28'd0, sub_opt[1:0]};

                    if (burst_period >= period) begin
                        burst_period <= 32'd0;
                        burst_cnt    <= 16'd0;
                    end else
                        burst_period <= burst_period + 32'd1;

                    if (burst_cnt <= {8'd0, burst_len}) begin
                        dout      <= apply_amp(FULL_SCALE, amplitude);
                        burst_cnt <= burst_cnt + 16'd1;
                    end else
                        dout <= 16'sd0;
                end
            end

            // ──────────────────────────────────────────────────────────────
            //  7: LFO_TONE  (AM 변조)
            //     캐리어: base_inc 주파수
            //     LFO   : lfo_inc_w 주파수, 진폭 0~1 (단극 sin)
            //     sub_opt[1:0]: 캐리어 파형 (0=sin 1=sq 2=tri)
            // ──────────────────────────────────────────────────────────────
            T_LFO_TONE: begin
                begin : lfo_blk
                    reg signed [15:0] carrier;
                    reg signed [15:0] lfo_env;   // 0 ~ +32767
                    reg signed [31:0] am_prod;

                    // LFO 단극화: (sin + 32767) / 2 → 0 ~ 32767
                    lfo_env = ($signed(sin_val(lfo_acc)) + 16'sd32767) >>> 1;

                    case (sub_opt[1:0])
                        2'd0: carrier = sin_val(phase_acc);
                        2'd1: carrier = sq_val (phase_acc);
                        2'd2: carrier = tri_val(phase_acc);
                        default: carrier = sin_val(phase_acc);
                    endcase

                    // carrier × lfo_env × amplitude / (32768 × 32768)
                    am_prod = ($signed(carrier) * $signed({1'b0, lfo_env})) >>> 15;
                    dout <= apply_amp(sat16(am_prod), amplitude);
                end
                phase_acc <= phase_acc + {8'd0, base_inc};
                lfo_acc   <= lfo_acc   + {16'd0, lfo_inc_w};
            end

            // ──────────────────────────────────────────────────────────────
            //  8: SWEEP_LOG  (지수 / 로그 스윕)
            //     sweep_cur_inc 를 매 샘플 sweep_cur_inc >> sh 씩 증가
            //     sh = 6 + sub_opt[1:0]  → 6~9 비트 시프트
            //     sub_opt[0]=0: 업  / 1: 다운
            //     sub_opt[2]=1: 루프
            // ──────────────────────────────────────────────────────────────
            T_SWEEP_LOG: begin
                dout      <= apply_amp(sin_val(phase_acc), amplitude);
                phase_acc <= phase_acc + {8'd0, sweep_cur_inc};

                begin : log_sweep_blk
                    reg [3:0]  sh;
                    reg [23:0] delta;
                    sh    = 4'd6 + {2'd0, sub_opt[1:0]};
                    delta = sweep_cur_inc >> sh;
                    if (delta == 24'd0) delta = 24'd1;  // 최소 1 증가 보장

                    if (!sub_opt[0]) begin
                        if (sweep_cur_inc < sweep_end_w)
                            sweep_cur_inc <= sweep_cur_inc + delta;
                        else begin
                            if (sub_opt[2]) begin
                                sweep_cur_inc <= sweep_start;
                                phase_acc     <= 32'd0;
                            end else
                                sweep_cur_inc <= sweep_end_w;
                        end
                    end else begin
                        if (sweep_cur_inc > sweep_start) begin
                            if (sweep_cur_inc > delta)
                                sweep_cur_inc <= sweep_cur_inc - delta;
                            else
                                sweep_cur_inc <= sweep_start;
                        end else begin
                            if (sub_opt[2]) begin
                                sweep_cur_inc <= sweep_end_w;
                                phase_acc     <= 32'd0;
                            end else
                                sweep_cur_inc <= sweep_start;
                        end
                    end
                end
            end

            // ──────────────────────────────────────────────────────────────
            //  9: PINK_NOISE  (Paul Kellet 3-pole 근사)
            //     Q16 고정소수 계수
            //     b0: a=0.99765(65372), w=0.09905(6490)
            //     b1: a=0.96300(63112), w=0.29652(19433)
            //     b2: a=0.57000(37355), w=1.05270(69002)
            //     mix gain ≈ 0.11 (>>3 근사)
            // ──────────────────────────────────────────────────────────────
            T_PINK: begin
                begin : pink_blk
                    reg signed [15:0] wn;
                    reg signed [47:0] b0_new, b1_new, b2_new, psum;
                    wn     = $signed(lfsr_r);
                    b0_new = (pink_b0 * 32'sd65372 + $signed(wn) * 32'sd6490)  >>> 16;
                    b1_new = (pink_b1 * 32'sd63112 + $signed(wn) * 32'sd19433) >>> 16;
                    b2_new = (pink_b2 * 32'sd37355 + $signed(wn) * 32'sd69002) >>> 16;
                    pink_b0 <= b0_new[31:0];
                    pink_b1 <= b1_new[31:0];
                    pink_b2 <= b2_new[31:0];
                    // 핑크 합산 + 화이트 소량 혼합 (× 0.1848 ≈ 12107/65536)
                    psum = b0_new + b1_new + b2_new +
                           ($signed(wn) * 32'sd12107 >>> 16);
                    // 정규화 (약 >>2 스케일 다운)
                    dout <= apply_amp(
                        (psum[47:1] > 48'sh7FFF)  ? 16'sh7FFF  :
                        (psum[47:1] < -48'sh8000) ? 16'sh8000  :
                        psum[16:1],
                        amplitude);
                end
            end

            // ──────────────────────────────────────────────────────────────
            // 10: CHIRP_BURST  (버스트 내 주파수 스윕 처프)
            //     반복 주기: sweep_end_w 샘플  (gpio3[23:0])
            //     버스트 길이: burst_len 샘플  (gpio3[31:24])
            //     버스트 내 스윕: sweep_start → sweep_start + base_inc * burst_len
            //     base_inc[7:0]: 처프 속도 (버스트 내 증분 변화량)
            // ──────────────────────────────────────────────────────────────
            T_CHIRP_BURST: begin
                begin : chirp_blk
                    if (burst_period >= {8'd0, sweep_end_w}) begin
                        burst_period  <= 32'd0;
                        burst_cnt     <= 16'd0;
                        sweep_cur_inc <= sweep_start;
                        phase_acc     <= 32'd0;
                    end else
                        burst_period <= burst_period + 32'd1;

                    if (burst_cnt <= {8'd0, burst_len}) begin
                        dout      <= apply_amp(sin_val(phase_acc), amplitude);
                        phase_acc <= phase_acc + {8'd0, sweep_cur_inc};
                        if (sweep_cur_inc + {16'd0, base_inc[7:0]} < sweep_end_w)
                            sweep_cur_inc <= sweep_cur_inc + {16'd0, base_inc[7:0]};
                        else
                            sweep_cur_inc <= sweep_end_w;
                        burst_cnt <= burst_cnt + 16'd1;
                    end else
                        dout <= 16'sd0;
                end
            end

            default: dout <= 16'sd0;
            endcase
        end // tick
    end // rst_n
end

endmodule