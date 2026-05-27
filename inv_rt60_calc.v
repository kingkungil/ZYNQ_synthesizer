// ============================================================================
//  inv_rt60_calc.v  v3
//  목적: (1<<24) / rt60_ms 를 27클럭 순차 연산으로 처리
//  각 클럭 조합 depth: CARRY4 ~11개 (41비트 subtract 1회만)
//  v2 → v3: 인터페이스 변경 없음, 동작 동일
// ============================================================================
`timescale 1ns / 1ps
module inv_rt60_calc (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire [15:0] rt60_ms,
    output reg         done,
    output reg  [31:0] inv_out,
    output wire        busy
);

localparam [24:0] DIVIDEND = 25'd16777216;  // 1<<24

localparam [1:0] ST_IDLE  = 2'd0;
localparam [1:0] ST_CALC  = 2'd1;
localparam [1:0] ST_ROUND = 2'd2;
localparam [1:0] ST_DONE  = 2'd3;

reg [1:0]  state;
reg [4:0]  cnt;
reg [40:0] rem;
reg [15:0] div_r;
reg [23:0] quot;

// 41비트 감산 1회 (CARRY4×11)
wire [40:0] rem_sub = rem - {25'd0, div_r};

assign busy = (state != ST_IDLE);

always @(posedge clk) begin
    if (!rst_n) begin
        state   <= ST_IDLE;
        cnt     <= 5'd0;
        rem     <= 41'd0;
        div_r   <= 16'd1;
        quot    <= 24'd0;
        done    <= 1'b0;
        inv_out <= 32'd0;
    end else begin
        done <= 1'b0;
        case (state)

        ST_IDLE: begin
            if (start) begin
                div_r <= (rt60_ms == 16'd0) ? 16'd1 : rt60_ms;
                rem   <= {16'd0, DIVIDEND};
                quot  <= 24'd0;
                cnt   <= 5'd0;
                state <= ST_CALC;
            end
        end

        ST_CALC: begin
            if (!rem_sub[40]) begin
                quot <= {quot[22:0], 1'b1};
                rem  <= {rem_sub[39:0], 1'b0};
            end else begin
                quot <= {quot[22:0], 1'b0};
                rem  <= {rem[39:0],  1'b0};
            end
            if (cnt == 5'd23) begin
                cnt   <= 5'd0;
                state <= ST_ROUND;
            end else begin
                cnt <= cnt + 5'd1;
            end
        end

        ST_ROUND: begin
            // 반올림: 나머지 >= divisor/2 이면 +1
            inv_out <= !rem_sub[40] ? {8'd0, quot} + 32'd1
                                    : {8'd0, quot};
            state <= ST_DONE;
        end

        ST_DONE: begin
            done  <= 1'b1;
            state <= ST_IDLE;
        end

        default: state <= ST_IDLE;
        endcase
    end
end

endmodule