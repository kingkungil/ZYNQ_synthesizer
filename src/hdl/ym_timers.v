`timescale 1ns / 1ps

module ym_timers(
  input     clk,
  input     rst,
  input     clk_en /* synthesis direct_enable */,
  input         zero,
  input [9:0] value_A,
  input [7:0] value_B,
  input     load_A,
  input     load_B,
  input     clr_flag_A,
  input     clr_flag_B,
  input     enable_irq_A,
  input     enable_irq_B,
  output    flag_A,
  output    flag_B,
  output    overflow_A,
  output    irq_n
);

parameter num_ch = 6;

assign irq_n = ~( (flag_A&enable_irq_A) | (flag_B&enable_irq_B) );

/*
reg zero2;

always @(posedge clk, posedge rst) begin
    if( rst )
        zero2 <= 0;
    else if(clk_en) begin
        if( zero ) zero2 <= ~zero;
    end
end

wire zero       = num_ch == 6 ? zero : (zero2&zero);
*/
ym_timer #(.CW(10)) timer_A(
    .clk        ( clk         ),
    .rst        ( rst         ),
    .cen        ( clk_en      ),
    .zero       ( zero        ),
    .start_value( value_A     ),
    .load       ( load_A      ),
    .clr_flag   ( clr_flag_A  ),
    .flag       ( flag_A      ),
    .overflow   ( overflow_A  )
);

ym_timer #(.CW(8),.FREE_EN(1)) timer_B(
    .clk        ( clk         ),
    .rst        ( rst         ),
    .cen        ( clk_en      ),
    .zero       ( zero        ),
    .start_value( value_B     ),
    .load       ( load_B      ),
    .clr_flag   ( clr_flag_B  ),
    .flag       ( flag_B      ),
    .overflow   (             )
);

endmodule

module ym_timer #(parameter
    CW      = 8, // counter bit width. This is the counter that can be loaded
    FW      = 4, // number of bits for the free-running counter
    FREE_EN = 0  // enables a 4-bit free enable count
) (
    input   rst,
    input   clk,
    input   cen,
    input   zero,
    input   [CW-1:0] start_value,
    input   load,
    input   clr_flag,
    output reg flag,
    output reg overflow
);
/* verilator lint_off WIDTH */
reg          load_l;
reg [CW-1:0] cnt, next;
reg [FW-1:0] free_cnt, free_next;
reg          free_ov;

always@(posedge clk, posedge rst)
    if( rst )
        flag <= 1'b0;
    else /*if(cen)*/ begin
        if( clr_flag )
            flag <= 1'b0;
        else if( cen && zero && load && overflow ) flag<=1'b1;
    end

always @(*) begin
    {free_ov, free_next} = { 1'b0, free_cnt} + 1'b1;
    {overflow, next }    = { 1'b0, cnt }     + (FREE_EN ? free_ov : 1'b1);
end

always @(posedge clk) begin
    load_l <= load;
    if( !load_l && load ) begin
        cnt <= start_value;
    end else if( cen && zero && load )
        cnt <= overflow ? start_value : next;
end

// Free running counter
always @(posedge clk) begin
    if( rst ) begin
        free_cnt <= 0;
    end else if( cen && zero ) begin
        free_cnt <= free_next;
    end
end
/* verilator lint_on WIDTH */
endmodule
