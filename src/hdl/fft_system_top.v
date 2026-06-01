`timescale 1ns / 1ps

module fft_system_top #(
    parameter DATA_WIDTH_16 = 16,
    parameter DATA_WIDTH_32 = 32,
    parameter FFT_POINTS    = 1024,
    parameter ADDR_W        = 10   // 2^10 = 1024
)(
    input  wire        rstn,        // Active Low
    
    // [Input Domain]
    input  wire        in_clk,
    input  wire        in_push,
    input  wire [15:0] in_data,
    output wire        in_full,

    // [System Domain]
    input  wire        sys_clk,

    // [Output Domain]
    input  wire        out_clk,
    input  wire        out_pop,
    output wire [15:0] out_data,
    output wire        out_empty
);

    // ============================================================
    // 1. INPUT FIFO & FFT Control
    // ============================================================
    wire [15:0] ip_fifo_dout;
    wire [11:0] ip_fifo_count; 
    reg         ip_fifo_pop;
    reg         fft_en_reg;
    reg [10:0]  burst_cnt;

    fft_fifo #(.DEPTH(2048), .WIDTH(16), .ADDR_W(11)) input_buffer (
        .rstn(rstn), .wclk(in_clk), .push(in_push), .din(in_data), .full(in_full),
        .rclk(sys_clk), .pop(ip_fifo_pop), .dout(ip_fifo_dout), .empty(), .rd_data_count(ip_fifo_count)
    );

    always @(posedge sys_clk or negedge rstn) begin
        if (!rstn) {ip_fifo_pop, fft_en_reg, burst_cnt} <= 0;
        else begin
            fft_en_reg <= ip_fifo_pop;
            if (burst_cnt == 0 && ip_fifo_count >= FFT_POINTS) begin
                ip_fifo_pop <= 1'b1;
                burst_cnt   <= 1;
            end else if (burst_cnt == FFT_POINTS) begin
                ip_fifo_pop <= 1'b0;
                burst_cnt   <= 0;
            end else if (burst_cnt > 0) burst_cnt <= burst_cnt + 1;
        end
    end

    // ============================================================
    // 2. FFT CORE
    // ============================================================
    wire        fft_do_en;
    wire [31:0] fft_do_re, fft_do_im;

    FFT #(.WIDTH(32)) fft_inst (
        .clock(sys_clk), .reset(~rstn),
        .di_en(fft_en_reg), .di_re({ip_fifo_dout, 16'h0000}), .di_im(32'h0000),
        .do_en(fft_do_en), .do_re(fft_do_re), .do_im(fft_do_im)
    );

    // ============================================================
    // 3. Bit-Reversal Reorder Buffer (Ping-Pong)
    // ============================================================
    reg [ADDR_W-1:0] write_ptr;
    reg [ADDR_W-1:0] read_ptr;
    reg              ping_pong_sel;
    reg              read_active;
    reg [ADDR_W:0]   read_cnt;

    // Bit-reversal function (10-bit)
    function [9:0] bit_rev;
        input [9:0] addr;
        integer k;
        begin
            for (k=0; k<10; k=k+1) bit_rev[9-k] = addr[k];
        end
    endfunction

    // Write Logic (FFT Output -> RAM)
    // DIF FFT 출력이 Bit-reversed이므로, Bit-reversed 주소에 쓰면 RAM에는 순차적으로 쌓임
    always @(posedge sys_clk or negedge rstn) begin
        if (!rstn) begin
            write_ptr <= 0;
            ping_pong_sel <= 0;
        end else if (fft_do_en) begin
            write_ptr <= write_ptr + 1;
            if (write_ptr == FFT_POINTS - 1) begin
                ping_pong_sel <= ~ping_pong_sel;
            end
        end
    end

    // Ping-Pong RAM (Simple Dual Port BRAM x 2)
    // 실제 구현 시에는 BRAM IP나 인스턴스 사용 권장
    reg [15:0] ram0 [0:1023];
    reg [15:0] ram1 [0:1023];
    reg [15:0] reorder_dout;

    always @(posedge sys_clk) begin
        if (fft_do_en) begin
            if (ping_pong_sel == 0) ram0[bit_rev(write_ptr)] <= fft_do_re[31:16];
            else                   ram1[bit_rev(write_ptr)] <= fft_do_re[31:16];
        end
        // Read during the NEXT frame
        reorder_dout <= (ping_pong_sel == 1) ? ram0[read_ptr] : ram1[read_ptr];
    end

    // Read Logic (RAM -> Output FIFO)
    always @(posedge sys_clk or negedge rstn) begin
        if (!rstn) begin
            read_ptr <= 0;
            read_active <= 0;
            read_cnt <= 0;
        end else begin
            // FFT 출력이 한 프레임 끝나면 읽기 시작
            if (fft_do_en && write_ptr == FFT_POINTS - 1) begin
                read_active <= 1;
                read_ptr <= 0;
                read_cnt <= 0;
            end else if (read_active) begin
                if (read_cnt == FFT_POINTS - 1) begin
                    read_active <= 0;
                end
                read_ptr <= read_ptr + 1;
                read_cnt <= read_cnt + 1;
            end
        end
    end

    // ============================================================
    // 4. OUTPUT FIFO (Final Natural Order Data)
    // ============================================================
    reg out_fifo_push;
    always @(posedge sys_clk) out_fifo_push <= read_active;

    fft_fifo #(.DEPTH(2048), .WIDTH(16), .ADDR_W(11)) output_buffer (
        .rstn(rstn), .wclk(sys_clk), .push(out_fifo_push), .din(reorder_dout), .full(),
        .rclk(out_clk), .pop(out_pop), .dout(out_data), .empty(out_empty), .rd_data_count()
    );

endmodule