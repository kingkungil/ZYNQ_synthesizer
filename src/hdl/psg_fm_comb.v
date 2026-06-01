`timescale 1ns / 1ps

module psg_fm_comb #(parameter 
    w=16,    // bit width
    m=1      // depth of comb filter
)(
    input                rst,
    input                clk,
(* direct_enable *)    input cen,
    input  signed [w-1:0] snd_in,
    output reg signed [w-1:0] snd_out
);

wire signed [w-1:0] prev;

// m-delay stage
generate
    genvar k;
    reg signed [w-1:0] mem[0:m-1];
    assign prev = mem[m-1];
    
    for(k=0; k<m; k=k+1) begin : mem_gen
        always @(posedge clk)
            if(rst) begin
                mem[k] <= {w{1'b0}};
            end else if(cen) begin
                if (k == 0)
                    mem[k] <= snd_in;
                else
                    mem[k] <= mem[k-1];
            end
    end
endgenerate

// Comb filter (Derivative) at synthesizer sampling rate
always @(posedge clk)
    if(rst) begin
        snd_out <= {w{1'b0}};
    end else if(cen) begin
        // 현재 입력에서 m-delay된 이전 값을 뺍니다.
        snd_out <= snd_in - prev;
    end

endmodule