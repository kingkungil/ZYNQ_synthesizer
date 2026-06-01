`timescale 1ns/1ps
// ============================================================================
//  schroeder_ap.v  v3  (RESOURCE-OPT)
//
//  v2 → v3 변경:
//    [FF-CUT]  ap_mem initial 블록 제거
//              → Vivado 가 BRAM 을 FF array 로 폴백하는 원인 차단
//              → 리셋 후 포인터=0에서 시작, BRAM 값은 dont-care(오디오 fade-in
//                 으로 마스킹)
//    [DSP-FIX] (* use_dsp = "yes" *) 어트리뷰트 유지
//              (* ram_style = "block" *) 어트리뷰트 명시
//    [FF-FIX]  din_sB (미사용 디버그 레지스터) 제거 → 16 FF 절감
//    레이턴시: v2 동일 4 클럭 (Stage0→A→B→C)
// ============================================================================
module schroeder_ap #(
    parameter DATA_W = 16,
    parameter DELAY  = 241,
    parameter ADDR_W = 8
)(
    input  wire                      clk,
    input  wire                      rst_n,
    input  wire                      en,
    input  wire signed [DATA_W-1:0]  din,
    input  wire signed [DATA_W-1:0]  g,
    output reg  signed [DATA_W-1:0]  dout,
    output reg                       dout_valid
);

localparam ACC_W = 32;

// ── BRAM ─────────────────────────────────────────────────────────────────────
// [FF-CUT] initial 블록 없음 → Vivado BRAM 추론 보장
(* ram_style = "block" *)
reg signed [DATA_W-1:0] ap_mem [0:DELAY-1];

reg [ADDR_W-1:0] ptr;

// ── 포화 함수 ─────────────────────────────────────────────────────────────────
function signed [DATA_W-1:0] sat16;
    input signed [ACC_W-1:0] v;
    begin
        if      (v >  32'sh00007FFF) sat16 =  16'h7FFF;
        else if (v < -32'sh00008000) sat16 = -16'h8000;
        else                         sat16 = v[DATA_W-1:0];
    end
endfunction

// ── Stage 0: BRAM 읽기 + 포인터 전진 ─────────────────────────────────────────
reg                      stA_en;
reg signed [DATA_W-1:0]  din_sA;
reg [ADDR_W-1:0]         ptr_sA;
reg signed [DATA_W-1:0]  wD_r;

always @(posedge clk) begin
    if (!rst_n) begin
        ptr    <= {ADDR_W{1'b0}};
        stA_en <= 1'b0;
        din_sA <= {DATA_W{1'b0}};
        ptr_sA <= {ADDR_W{1'b0}};
        wD_r   <= {DATA_W{1'b0}};
    end else begin
        stA_en <= en;
        din_sA <= din;
        ptr_sA <= ptr;
        wD_r   <= ap_mem[ptr];   // 동기 BRAM 읽기
        if (en)
            ptr <= (ptr == DELAY-1) ? {ADDR_W{1'b0}} : ptr + 1'b1;
    end
end

// ── Stage A: g×wD + w[n] 계산 ─────────────────────────────────────────────
(* use_dsp = "yes" *)
reg signed [ACC_W-1:0]   gxwD_r;

reg                      stB_en;
reg signed [DATA_W-1:0]  w_n_r;
reg signed [DATA_W-1:0]  wD_sB;
reg [ADDR_W-1:0]         ptr_sB;

always @(posedge clk) begin
    if (!rst_n) begin
        gxwD_r <= {ACC_W{1'b0}};
        stB_en <= 1'b0;
        w_n_r  <= {DATA_W{1'b0}};
        wD_sB  <= {DATA_W{1'b0}};
        ptr_sB <= {ADDR_W{1'b0}};
    end else begin
        gxwD_r <= $signed(g) * $signed(wD_r);
        stB_en <= stA_en;
        wD_sB  <= wD_r;
        ptr_sB <= ptr_sA;
        if (stA_en)
            w_n_r <= sat16(
                {{(ACC_W-DATA_W){din_sA[DATA_W-1]}}, din_sA} +
                {{(ACC_W-16){gxwD_r[30]}}, gxwD_r[30:15]}
            );
    end
end

// ── Stage B: g×w[n] 계산 ─────────────────────────────────────────────────
(* use_dsp = "yes" *)
reg signed [ACC_W-1:0]   gxwn_r;

reg                      stC_en;
reg signed [DATA_W-1:0]  wD_sC;
reg [ADDR_W-1:0]         ptr_sC;
reg signed [DATA_W-1:0]  w_n_sC;

always @(posedge clk) begin
    if (!rst_n) begin
        gxwn_r <= {ACC_W{1'b0}};
        stC_en <= 1'b0;
        wD_sC  <= {DATA_W{1'b0}};
        ptr_sC <= {ADDR_W{1'b0}};
        w_n_sC <= {DATA_W{1'b0}};
    end else begin
        gxwn_r <= $signed(g) * $signed(w_n_r);
        stC_en <= stB_en;
        wD_sC  <= wD_sB;
        ptr_sC <= ptr_sB;
        w_n_sC <= w_n_r;
    end
end

// ── Stage C: 출력 + BRAM 쓰기 ─────────────────────────────────────────────
always @(posedge clk) begin
    if (!rst_n) begin
        dout       <= {DATA_W{1'b0}};
        dout_valid <= 1'b0;
    end else begin
        dout_valid <= stC_en;
        if (stC_en) begin
            ap_mem[ptr_sC] <= w_n_sC;
            dout <= sat16(
                {{(ACC_W-DATA_W){wD_sC[DATA_W-1]}}, wD_sC} -
                {{(ACC_W-16){gxwn_r[30]}}, gxwn_r[30:15]}
            );
        end
    end
end

endmodule