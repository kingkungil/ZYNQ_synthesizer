`timescale 1ns / 1ps

module psg_fm_interpol #(parameter calcw=18, inw=16, 
    n=2,    // number of stages
    m=1,    // depth of comb filter
    rate=2  // it will stuff with as many as (rate-1) zeros
)(
    input                rst,
    input                clk,
(* direct_enable *)    input                cen_in,
(* direct_enable *)    input                cen_out,
    input  signed [inw-1:0] snd_in,
    output reg signed [inw-1:0] snd_out
);

reg signed  [calcw-1:0] inter6;
wire signed [calcw-1:0] comb_op, integ_op;
localparam wdiff = calcw - inw;

// wire for assign logic within generate (added definition)
wire [calcw-1:0] integ_data_in_0;

generate
    genvar k;
    wire [calcw-1:0] comb_data[0:n];
    assign comb_data[0] = { {wdiff{snd_in[inw-1]}}, snd_in };
    assign comb_op = comb_data[n];
    
    for(k=0;k<n;k=k+1) begin : comb_gen
        
        psg_fm_comb #(.w(calcw),.m(m)) u_comb(
            .rst    ( rst            ),
            .clk    ( clk            ),
            .cen    ( cen_in         ),
            .snd_in ( comb_data[k]   ),
            .snd_out( comb_data[k+1] )
        );
    end
endgenerate

reg [rate-1:0] inter_cnt;

// interpolator 
always @(posedge clk) 
    if(rst) begin
        inter6 <= {calcw{1'b0}};
        inter_cnt <= { {(rate-1){1'b0}}, 1'b1};
    end else if(cen_out) begin
        inter6 <= inter_cnt[0] ? comb_op : {calcw{1'b0}};
        inter_cnt <= { inter_cnt[0], inter_cnt[rate-1:1] };
    end

// integrator at clk x cen sampling rate
generate
    genvar k2;
    reg [calcw-1:0] integ_data[0:n];
    assign integ_op = integ_data[n];
    
    assign integ_data_in_0 = inter6; 

    for(k2=1;k2<=n;k2=k2+1) begin : integ_gen
        always @(posedge clk) 
            if(rst) begin
                integ_data[k2] <= {calcw{1'b0}};
            end else if(cen_out) begin
                if (k2 == 1)
                    integ_data[k2] <= integ_data[k2] + inter6;
                else
                    integ_data[k2] <= integ_data[k2] + integ_data[k2-1];
            end
    end
endgenerate

always @(posedge clk) 
    if(rst) begin
        snd_out <= {inw{1'b0}};
    end else if(cen_out) begin
        snd_out <= integ_op[calcw-1:wdiff];
    end

endmodule