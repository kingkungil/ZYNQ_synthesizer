// =============================================================================
//  ym2203_writer.v  v5
//
//  구조:
//    Linux → AXI GPIO → [axi_gpio_sync] → async_fifo → FSM → YM2203
//
//  v5 핵심 수정:
//    almost_full 로직을 올바르게 재설계.
//    이전 v4의 push_count 근사 방식(pop을 gpio_wr 부재로 추정)은 틀렸음.
//    → async_fifo 내부 wptr_bin / rptr_bin을 외부로 노출하거나
//      fifo_pop 신호를 rclk→wclk CDC해서 정확한 레벨 카운터 구성.
//
//    선택한 방식:
//      async_fifo의 wptr_bin(wclk), rptr_bin_w(wclk 동기화된 rptr)을
//      외부 포트로 노출 → level = wptr_bin - rptr_bin_w
//      → almost_full = (level >= DEPTH - ALMOST_MARGIN)
//
//    async_fifo v2 수정 필요:
//      wptr_bin, rptr_gray_w2(이미 wclk 동기화됨)를 output으로 추가.
//      rptr_gray_w2 → binary 변환으로 rptr_bin_w 계산.
//
//    대안 (async_fifo 수정 없이):
//      fifo_pop(rclk) → toggle CDC → wclk 도메인 pop_w 펄스
//      → wclk에서 push_cnt - pop_cnt = level 정확 추적
//      → 이 방식 채택 (async_fifo 수정 최소화)
//
//  통합된 모든 기능:
//    FIX-1  S_LATCH (BRAM 1cy latency 대응)
//    FIX-2  WAIT_CYCLES=16 (YM2203 busy 17cy 커버)
//    FIX-3  axi_gpio_sync 서브모듈 (CDC-A, 외부)
//    FIX-4  init_done stabilizer (reset 후 4cy 고정)
//    ADD-1  almost_full: fifo_pop toggle CDC + push/pop 카운터로 정확한 level
//    ADD-2  min_spacing: DATA_HOLD 후 SPACING_CYCLES 간격 강제
//
//  파라미터:
//    FIFO_DEPTH     = 64   (2의 거듭제곱)
//    FIFO_ADDR_W    = 6    (log2(64))
//    WAIT_CYCLES    = 16   (YM2203 addr→data 대기)
//    HOLD_CYCLES    = 1    (WR_N high hold)
//    SPACING_CYCLES = 4    (write 간 최소 간격)
//    ALMOST_MARGIN  = 8    (almost_full = level >= DEPTH-MARGIN)
//    INIT_CYCLES    = 4    (reset 후 FSM 안정화)
// =============================================================================

`timescale 1ns / 1ps

module ym2203_writer #(
    parameter FIFO_DEPTH     = 64,
    parameter FIFO_ADDR_W    = 6,
    parameter WAIT_CYCLES    = 20,
    parameter HOLD_CYCLES    = 1,
    parameter SPACING_CYCLES = 4,
    parameter ALMOST_MARGIN  = 8,
    parameter INIT_CYCLES    = 4
)(
    input  wire        rstn,

    // ── wclk 도메인 (axi_gpio_sync 출력이 들어옴) ───────────────────────────
    input  wire        wclk,
    input  wire [15:0] gpio_data,   // sync_data  : CDC 완료된 데이터
    input  wire        gpio_wr,     // sync_pulse : 1사이클 push 신호

    // ── rclk 도메인 (반드시 YM2203 클럭과 동일) ─────────────────────────────
    input  wire        rclk,

    // ── YM2203 출력 포트 ─────────────────────────────────────────────────────
    output reg  [7:0]  ym_din,
    output reg         ym_addr,
    output reg         ym_cs_n,
    output reg         ym_wr_n,

    // ── 상태 출력 ────────────────────────────────────────────────────────────
    output wire        fifo_full,
    output wire        almost_full,  // Linux backpressure 신호
    output wire        busy
);

    // =========================================================================
    // FIFO
    // =========================================================================
    wire [15:0] fifo_dout;
    wire        fifo_empty;
    reg         fifo_pop;           // rclk 도메인 pop 신호

    async_fifo #(
        .DEPTH  (FIFO_DEPTH),
        .WIDTH  (16),
        .ADDR_W (FIFO_ADDR_W)
    ) u_fifo (
        .rstn   (rstn),
        .wclk   (wclk),
        .push   (gpio_wr & ~fifo_full),
        .din    (gpio_data),
        .full   (fifo_full),
        .rclk   (rclk),
        .pop    (fifo_pop),
        .dout   (fifo_dout),
        .empty  (fifo_empty)
    );

    // =========================================================================
    // [ADD-1] almost_full: Toggle CDC 기반 정확한 레벨 카운터
    //
    //  문제:
    //    fifo_pop은 rclk 도메인 1사이클 펄스.
    //    wclk 도메인에서 2FF sync로 직접 포착하면:
    //      rclk < wclk 인 경우 → pop 펄스 폭이 1 wclk보다 짧아 miss 가능.
    //
    //  해결 - Toggle CDC:
    //    rclk 도메인에서 pop 발생마다 pop_toggle을 반전(toggle).
    //    toggle 신호는 DC 레벨이므로 2FF sync로 wclk에 안전하게 전달.
    //    wclk에서 edge detect → pop_w 펄스 (pop 1회 = pop_w 1펄스).
    //    rclk가 아무리 느려도 pop 이벤트 누락 없음.
    //
    //  level_cnt:
    //    push(gpio_wr & ~full) → +1
    //    pop_w                  → -1
    //    동시 발생              → 변화 없음
    //    → level_cnt = 현재 FIFO 사용량 (wclk 기준, 약간의 CDC 지연 있음)
    //
    //  almost_full:
    //    level_cnt >= FIFO_DEPTH - ALMOST_MARGIN
    //    → 실제 full보다 ALMOST_MARGIN 엔트리 앞서 경고
    //    → Linux 드라이버가 미리 대기 → overflow 방지
    // =========================================================================

    // ── rclk 도메인: pop toggle 생성 ─────────────────────────────────────────
    reg pop_toggle;
    always @(posedge rclk or negedge rstn)
        if (!rstn) pop_toggle <= 1'b0;
        else if (fifo_pop) pop_toggle <= ~pop_toggle;

    // ── wclk 도메인: 2FF sync + edge detect → pop_w 펄스 ────────────────────
    reg pop_tgl_w1, pop_tgl_w2, pop_tgl_w3;
    always @(posedge wclk or negedge rstn) begin
        if (!rstn) begin
            pop_tgl_w1 <= 1'b0;
            pop_tgl_w2 <= 1'b0;
            pop_tgl_w3 <= 1'b0;
        end else begin
            pop_tgl_w1 <= pop_toggle;  // FF1: metastability 포착
            pop_tgl_w2 <= pop_tgl_w1; // FF2: 안정화 (이후 사용)
            pop_tgl_w3 <= pop_tgl_w2; // FF3: edge detect용 1사이클 delay
        end
    end
    // XOR: toggle 변화(rising or falling) = pop 이벤트 1회
    wire pop_w = pop_tgl_w2 ^ pop_tgl_w3;

    // ── wclk 도메인: push/pop 카운터로 level 계산 ────────────────────────────
    reg [FIFO_ADDR_W:0] level_cnt;

    wire do_push = gpio_wr & ~fifo_full;

    always @(posedge wclk or negedge rstn) begin
        if (!rstn) begin
            level_cnt <= {(FIFO_ADDR_W+1){1'b0}};
        end else begin
            case ({do_push, pop_w})
                2'b10:   level_cnt <= level_cnt + 1'b1;          // push only
                2'b01:   level_cnt <= level_cnt - 1'b1;          // pop only
                default: level_cnt <= level_cnt;                  // 동시 or 없음
            endcase
        end
    end

    assign almost_full = (level_cnt >= (FIFO_DEPTH[FIFO_ADDR_W:0] - ALMOST_MARGIN[FIFO_ADDR_W:0]));

    // =========================================================================
    // rclk 도메인 리셋 동기화 (2FF, async assert / sync deassert)
    // =========================================================================
    reg [1:0] r_rst_sync;
    always @(posedge rclk or negedge rstn)
        if (!rstn) r_rst_sync <= 2'b00;
        else       r_rst_sync <= {r_rst_sync[0], 1'b1};
    wire rrstn = r_rst_sync[1];

    // =========================================================================
    // [FIX-4] reset 후 FSM 안정화 카운터
    //   rrstn deassert 후 INIT_CYCLES 동안 S_IDLE 고정.
    //   Gray code 포인터 sync, FIFO 내부 상태 안정화 대기.
    // =========================================================================
    reg [2:0] init_cnt;
    wire      init_done;

    always @(posedge rclk) begin
        if (!rrstn)
            init_cnt <= 3'd0;
        else if (init_cnt < INIT_CYCLES[2:0])
            init_cnt <= init_cnt + 3'd1;
    end
    assign init_done = (init_cnt >= INIT_CYCLES[2:0]);

    // =========================================================================
    // FSM (rclk 도메인)
    //
    //  상태 전이:
    //    IDLE       → !empty && spacing==0 && init_done
    //                 → fifo_pop=1 → S_LATCH
    //    LATCH      → latch ← fifo_dout (BRAM 1cy 흡수)
    //                 → S_ADDR_PHASE
    //    ADDR_PHASE → ym: addr=0, cs=0, wr=0, din=latch[15:8]
    //                 → S_ADDR_HOLD
    //    ADDR_HOLD  → ym: wr=1, cs=1 / wait_cnt 로드
    //                 → S_WAIT_READY
    //    WAIT_READY → 16사이클 대기
    //                 → S_DATA_PHASE
    //    DATA_PHASE → ym: addr=1, cs=0, wr=0, din=latch[7:0]
    //                 → S_DATA_HOLD
    //    DATA_HOLD  → ym: wr=1, cs=1 / spacing_cnt 로드
    //                 → S_IDLE
    //
    //  [ADD-2] spacing_cnt:
    //    DATA_HOLD 진입 시 SPACING_CYCLES 값 로드.
    //    매 rclk 사이클 감소 (FSM 상태 무관).
    //    S_IDLE에서 spacing_cnt > 0 이면 pop 금지.
    //    → 연속 write 간 최소 간격 = SPACING_CYCLES rclk 보장.
    // =========================================================================
    localparam S_IDLE       = 3'd0;
    localparam S_LATCH      = 3'd1;
    localparam S_ADDR_PHASE = 3'd2;
    localparam S_ADDR_HOLD  = 3'd3;
    localparam S_WAIT_READY = 3'd4;
    localparam S_DATA_PHASE = 3'd5;
    localparam S_DATA_HOLD  = 3'd6;

    reg [2:0]  state;
    reg [15:0] latch;
    reg [4:0]  wait_cnt;
    reg [3:0]  spacing_cnt;

    assign busy = (state != S_IDLE) || (spacing_cnt != 4'd0);

    always @(posedge rclk) begin
        if (!rrstn) begin
            state       <= S_IDLE;
            latch       <= 16'h0000;
            wait_cnt    <= 5'd0;
            spacing_cnt <= 4'd0;
            fifo_pop    <= 1'b0;
            ym_din      <= 8'h00;
            ym_addr     <= 1'b0;
            ym_cs_n     <= 1'b1;
            ym_wr_n     <= 1'b1;
        end else begin
            fifo_pop <= 1'b0;  // 기본값: 매 사이클 0, 필요 시 1 펄스

            if (!init_done) begin
                // [FIX-4] 안정화 완료 전: IDLE 고정, YM 출력 안전값 유지
                state   <= S_IDLE;
                ym_cs_n <= 1'b1;
                ym_wr_n <= 1'b1;
            end else begin

                // [ADD-2] spacing 카운터: FSM 상태 무관하게 매 사이클 감소
                if (spacing_cnt != 4'd0)
                    spacing_cnt <= spacing_cnt - 1'b1;

                case (state)

                    // ── S_IDLE ────────────────────────────────────────────────
                    //   pop 조건: FIFO 비지 않음 AND spacing 완료
                    //   → fifo_pop 1사이클 펄스 → S_LATCH
                    S_IDLE: begin
                        ym_cs_n <= 1'b1;
                        ym_wr_n <= 1'b1;
                        if (!fifo_empty && (spacing_cnt == 4'd0)) begin
                            fifo_pop <= 1'b1;
                            state    <= S_LATCH;
                        end
                    end

                    // ── S_LATCH ───────────────────────────────────────────────
                    //   [FIX-1] async_fifo BUG-A 수정 반영:
                    //   S_IDLE에서 fifo_pop=1 발행
                    //   → pop 발생 클럭에 raddr_reg = rptr_bin 등록
                    //   → 다음 클럭(S_LATCH)에 BRAM → dout_reg 출력 완료
                    //   → fifo_dout 이 사이클에 유효 → latch 포착
                    S_LATCH: begin
                        latch <= fifo_dout;   // [15:8]=addr_byte, [7:0]=data_byte
                        state <= S_ADDR_PHASE;
                    end

                    // ── S_ADDR_PHASE ──────────────────────────────────────────
                    //   YM2203 주소 write:
                    //   tAS (CS_N↓~WR_N↓) = 0ns → 동시 인가 허용
                    //   tWL (WR_N low)     = 1 rclk = 250ns ≥ 200ns ✓
                    S_ADDR_PHASE: begin
                        ym_din  <= latch[15:8];
                        ym_addr <= 1'b0;
                        ym_cs_n <= 1'b0;
                        ym_wr_n <= 1'b0;
                        state   <= S_ADDR_HOLD;
                    end

                    // ── S_ADDR_HOLD ───────────────────────────────────────────
                    //   tWH (WR_N high) = 1 rclk = 250ns ≥ 100ns ✓
                    //   wait_cnt에 WAIT_CYCLES-1 로드
                    S_ADDR_HOLD: begin
                        ym_wr_n  <= 1'b1;
                        ym_cs_n  <= 1'b1;
                        wait_cnt <= WAIT_CYCLES[4:0] - 1'b1;
                        state    <= S_WAIT_READY;
                    end

                    // ── S_WAIT_READY ──────────────────────────────────────────
                    //   [FIX-2] WAIT_CYCLES=16:
                    //   YM2203 addr write 후 내부 처리 최대 17 YM clk.
                    //   16 rclk @ 4MHz = 4us → 충분한 여유.
                    //   cs_n=1, wr_n=1 (버스 릴리즈 상태 유지)
                    S_WAIT_READY: begin
                        if (wait_cnt == 5'd0)
                            state <= S_DATA_PHASE;
                        else
                            wait_cnt <= wait_cnt - 1'b1;
                    end

                    // ── S_DATA_PHASE ──────────────────────────────────────────
                    //   YM2203 데이터 write:
                    //   tWL = 1 rclk = 250ns ≥ 200ns ✓
                    S_DATA_PHASE: begin
                        ym_din  <= latch[7:0];
                        ym_addr <= 1'b1;
                        ym_cs_n <= 1'b0;
                        ym_wr_n <= 1'b0;
                        state   <= S_DATA_HOLD;
                    end

                    // ── S_DATA_HOLD ───────────────────────────────────────────
                    //   [ADD-2] spacing_cnt 로드:
                    //   다음 pop까지 SPACING_CYCLES rclk 대기 강제.
                    //   YM2203 envelope/timer 레지스터 jitter 제거.
                    S_DATA_HOLD: begin
                        ym_wr_n     <= 1'b1;
                        ym_cs_n     <= 1'b1;
                        spacing_cnt <= SPACING_CYCLES[3:0];
                        state       <= S_IDLE;
                    end

                    default: state <= S_IDLE;

                endcase
            end
        end
    end

endmodule