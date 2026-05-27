`timescale 1ns / 1ps
// =============================================================================
//  async_fifo.v  -  Dual-clock async FIFO (Gray-code CDC, BRAM)
//
//  [수정 내역]
//  BUG-1 [심각] waddr_reg 오프셋 버그:
//        기존: mem[waddr_reg]에 쓰고 같은 클럭에 waddr_reg <= wptr_bin
//              → waddr_reg는 이전 클럭의 wptr_bin이므로 두 번째 push부터
//                 mem[0]을 덮어씀 (1 오프셋 오류)
//        수정: waddr_reg 제거. mem 쓰기에 wptr_bin 직접 사용.
//
//  BUG-2 [심각] raddr_reg 2사이클 지연:
//        기존: pop 시 raddr_reg <= rptr_bin(현재값)
//              → dout_reg는 pop 후 2클럭 뒤에 유효 (너무 늦음)
//        수정: raddr_reg <= rptr_bin_next
//              → pop 직후(1클럭 후) dout_reg 유효 (fifo_to_axis와 타이밍 일치)
// =============================================================================

module fft_fifo #(
    parameter DEPTH  = 2048,
    parameter WIDTH  = 16,
    parameter ADDR_W = 10
)(
    input  wire              rstn,

    // write domain
    input  wire              wclk,
    input  wire              push,
    input  wire [WIDTH-1:0]  din,
    output wire              full,

    // read domain
    input  wire              rclk,
    input  wire              pop,
    output wire [WIDTH-1:0]  dout,
    output wire              empty,
    output wire [ADDR_W:0]    rd_data_count
);

    // ============================================================
    // Reset synchronizers (2-FF, async assert / sync deassert)
    // ============================================================
    reg [1:0] w_rst_sync, r_rst_sync;

    always @(posedge wclk or negedge rstn)
        if (!rstn) w_rst_sync <= 2'b00;
        else       w_rst_sync <= {w_rst_sync[0], 1'b1};

    always @(posedge rclk or negedge rstn)
        if (!rstn) r_rst_sync <= 2'b00;
        else       r_rst_sync <= {r_rst_sync[0], 1'b1};

    wire wrstn = w_rst_sync[1];
    wire rrstn = r_rst_sync[1];

    // ============================================================
    // BRAM (synchronous read)
    // ============================================================
    (* ram_style = "block" *)
    reg [WIDTH-1:0] mem [0:DEPTH-1];

    // BUG-1 수정: waddr_reg 제거, wptr_bin 직접 사용
    always @(posedge wclk) begin
        if (push && !full)
            mem[wptr_bin[ADDR_W-1:0]] <= din;
    end

    // BUG-2 수정: raddr_reg <= rptr_bin_next (미리 증가된 주소)
    //             → pop 후 1클럭에 dout_reg 유효
    reg [ADDR_W-1:0] raddr_reg;
    reg [WIDTH-1:0]  dout_reg;

    always @(posedge rclk) begin
        dout_reg <= mem[raddr_reg];
    end

    assign dout = dout_reg;

    // ============================================================
    // Binary pointers
    // ============================================================
    reg  [ADDR_W:0] wptr_bin, rptr_bin;
    wire [ADDR_W:0] wptr_bin_next = wptr_bin + (push && !full);
    wire [ADDR_W:0] rptr_bin_next = rptr_bin + (pop  && !empty);

    always @(posedge wclk) begin
        if (!wrstn)
            wptr_bin <= 0;
        else if (push && !full)
            wptr_bin <= wptr_bin_next;
    end

    always @(posedge rclk) begin
        if (!rrstn) begin
            rptr_bin  <= 0;
            raddr_reg <= 0;
        end else if (pop && !empty) begin
            // BUG-2 수정: rptr_bin_next로 raddr 미리 준비
            raddr_reg <= rptr_bin_next[ADDR_W-1:0];
            rptr_bin  <= rptr_bin_next;
        end
    end

    // ============================================================
    // Gray pointers
    // ============================================================
    wire [ADDR_W:0] wptr_gray      = (wptr_bin      >> 1) ^ wptr_bin;
    wire [ADDR_W:0] rptr_gray      = (rptr_bin      >> 1) ^ rptr_bin;
    wire [ADDR_W:0] wptr_gray_next = (wptr_bin_next >> 1) ^ wptr_bin_next;
    wire [ADDR_W:0] rptr_gray_next = (rptr_bin_next >> 1) ^ rptr_bin_next;

    // ============================================================
    // CDC sync (2-FF synchronizer)
    // ============================================================
    reg [ADDR_W:0] wptr_gray_r1, wptr_gray_r2;
    reg [ADDR_W:0] rptr_gray_w1, rptr_gray_w2;

    always @(posedge rclk) begin
        if (!rrstn) begin
            wptr_gray_r1 <= 0;
            wptr_gray_r2 <= 0;
        end else begin
            wptr_gray_r1 <= wptr_gray;
            wptr_gray_r2 <= wptr_gray_r1;
        end
    end

    always @(posedge wclk) begin
        if (!wrstn) begin
            rptr_gray_w1 <= 0;
            rptr_gray_w2 <= 0;
        end else begin
            rptr_gray_w1 <= rptr_gray;
            rptr_gray_w2 <= rptr_gray_w1;
        end
    end

    // ============================================================
    // FULL  (wclk domain)
    // wptr_gray_next와 sync된 rptr_gray_w2 비교
    // rptr gray의 상위 2비트를 반전하여 포인터 wrap 감지
    // ============================================================
    reg full_reg;
    wire full_next =
        (wptr_gray_next ==
        {~rptr_gray_w2[ADDR_W:ADDR_W-1],
          rptr_gray_w2[ADDR_W-2:0]});

    always @(posedge wclk) begin
        if (!wrstn)
            full_reg <= 1'b0;
        else
            full_reg <= full_next;
    end

    assign full = full_reg;

    // ============================================================
    // EMPTY  (rclk domain)
    // rptr_gray_next와 sync된 wptr_gray_r2 비교
    // ============================================================
    reg empty_reg;
    wire empty_next = (rptr_gray_next == wptr_gray_r2);

    always @(posedge rclk) begin
        if (!rrstn)
            empty_reg <= 1'b1;
        else
            empty_reg <= empty_next;
    end

    assign empty = empty_reg;

// ============================================================
    // READ DATA COUNT (rclk domain)
    // wptr_gray_r2를 Binary로 변환 후 rptr_bin과 빼서 개수 산출
    // ============================================================
    wire [ADDR_W:0] wptr_bin_r;
    assign wptr_bin_r[ADDR_W] = wptr_gray_r2[ADDR_W];
    
    genvar i;
    generate
        for (i = ADDR_W-1; i >= 0; i = i - 1) begin : g2b
            assign wptr_bin_r[i] = wptr_bin_r[i+1] ^ wptr_gray_r2[i];
        end
    endgenerate

    assign rd_data_count = wptr_bin_r - rptr_bin;

endmodule