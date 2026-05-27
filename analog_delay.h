/* =============================================================================
 * analog_delay.h  —  UIO 드라이버 헤더  v6.0
 * RTL:  analog_delay_stereo_top.v  Rev.9  (analog_delay_top)
 *       nco_multi_lfo.v            Rev.4  (LFO)
 *
 * ── v6.0 변경사항 ─────────────────────────────────────────────────────────
 *   [ADD] §13  딜레이 길이 제어 API 완전 구현
 *              · analog_delay_set_time_* 계열 (ms / samp / Q16.8)
 *              · analog_delay_set_slew_rate_*  계열 (raw / 수렴시간 지정)
 *              · analog_delay_set_range()  — 딜레이+슬루 한 번에
 *              · ADLY_SLEW_FOR_CONV()  매크로 (컴파일타임 슬루값 계산)
 *              · 슬루 수렴시간 표 (1ms 단위까지)
 *   [FIX] lfo_set_for_delay() depth 역산 수식 수정
 *              · 구 수식: cur_peak = g_swing / depth_apf  (scale 오류)
 *              · 신 수식: peak_q = g_swing * 32768 / depth_apf  (Q15 단위)
 *              · d0_q / d4_q / d5_q 계산 및 overflow 검사 수정
 *   [FIX] LFO FCW 상수 RTL 실제값으로 완전 갱신 (LFO.v L69~77)
 *              · CH2=0x1A(0.30Hz), CH3=0x0D(0.15Hz) 헤더에 추가
 *   [FIX] lfo_mux 동작 명확화 (RTL: 1'b0 ? lfo_1 : lfo_0 → 항상 lfo_0)
 *   [FIX] 슬루 수렴시간 공식 정확화:
 *              conv_samp = Δdt_Q16.8 / slew_rate,  Δdt_Q16.8 = Δms × 12288
 *   [ADD] §24  1초/최대 딜레이 프리셋  (preset_1sec, preset_max_range)
 *   [ADD] §25  딜레이 범위별 슬루 속도 가이드라인 상수
 *
 * ── v5.1 구조적 버그 수정 (유지) ─────────────────────────────────────────
 *   [FIX-CRITICAL] DIFFUSION_START: echo_idx 항상 0 → 0=ON, ≥1=OFF
 *   [FIX-CRITICAL] damp_alpha/hf ≥ 0x8000 → IIR 이득 2.0 → 발진
 *   [FIX-CRITICAL] dry_wet ≥ 0x8000 → 믹스 이득 왜곡
 *   [FIX] IIR TDM 누화, DMA 패킹, LFO 유효채널 문서화
 *
 * ── RTL 핵심 구조 ─────────────────────────────────────────────────────────
 *   클럭:       50 MHz
 *   샘플레이트: 48 kHz (AXI-Stream 주기 기반)
 *   구조:       모노 입력 → TDM L/R 처리 → 스테레오 출력
 *   BRAM:       BRAM_ADDR_W=17, 총 131072 슬롯
 *               L: addr[16]=0 → [0x00000~0x0FFFF] (65536슬롯)
 *               R: addr[16]=1 → [0x10000~0x1FFFF] (65536슬롯)
 *   Q포맷:      딜레이 = Q16.8 24b,  정수부[23:8]=16b,  소수부[7:0]=8b
 *               1ms = 48000/1000 × 256 = 12288 Q단위
 *   딜레이범위: MIN 0.083ms(4samp) ~ MAX 1365.3ms(65535samp)
 *
 * ── 신호 처리 경로 (FSM 순서) ─────────────────────────────────────────────
 *   IDLE → MOD_LATCH → ADDR → RD0 → RD1 → INTERP → INTERP2
 *        → [APF_TRIG → APF_WAIT] → DIFF_MIX → SAT → FBMIX → OUTPUT → DRAIN
 *
 *   echo = linear_interp(BRAM[wr_ptr - dt_int], frac=dt_frac)
 *   apf  = schroeder_ap(echo, g=apf_g_mod)   [diffusion_start=0 시]
 *   diff = echo + (apf - echo) × diffusion_mix
 *   sat  = soft_clip(diff, thresh)
 *   BRAM[wr_ptr] = sat16(in + sat × feedback/32768)
 *   mix  = in×(1-dry_wet) + sat×dry_wet      [IIR 댐핑 후]
 *   out  = M/S_widening(L, R)
 *
 * ── LFO 유효 배선 (analog_delay_top.v L318~329) ──────────────────────────
 *   lfo_mux      = lfo_0  (RTL: "1'b0 ? lfo_1 : lfo_0" → 항상 lfo_0)
 *   lfo_apf_L    = lfo_0 + (lfo_4 >>> 3)
 *   lfo_apf_R    = lfo_0 + (lfo_4 >>> 3) + (lfo_5 >>> 2)
 *   CH0: APF g 주 변조 (무감쇠)
 *   CH4: L+R 공통 offset (÷8 감쇠)
 *   CH5: R 전용 추가 offset (÷4 감쇠)
 *   CH1,2,3,6,7: delay_top에 배선 없음 → 완전 사용 불가
 *
 * ── 발진/발산 안전 조건 ───────────────────────────────────────────────────
 *   damp_alpha/hf: ≤ 0x7FFF  (이상이면 IIR 이득 2.0 → 즉시 발진)
 *   dry_wet:       ≤ 0x7FFF  (이상이면 믹스 이득 wrap → 왜곡)
 *   feedback:      RTL 내부 FB_MAX=0x6000(75%) 하드클램프
 *   diffusion_g:   RTL [0,0x7FFF] 클램프,  실용 상한 0x6666(g=0.800)
 *   sat_thresh:    bit[15] RTL 무시됨. 0x0000 → 전체 클리핑(금지)
 *   slew_rate:     0 → 딜레이 수렴 불가. 최솟값 1
 *
 * ── 구조적 동작 특성 (SW 보정 불가) ──────────────────────────────────────
 *   IIR TDM 누화: fb_damp_r/damp_out_r 단일 레지스터 (L379)
 *                 L IIR 결과가 R IIR 초기값으로 사용됨
 *                 damp_alpha=0이면 echo_r 직결 → 누화 없음
 *   diffusion_start: APF 평가 시점 echo_idx=0 (매 샘플 L640에서 리셋)
 *                 0=APF ON,  ≥1=APF OFF
 *   부팅 슬루:    리셋 dt_slewed=0x200000(170.7ms) ≠ 기본 target=0x300000(256ms)
 *                 slew=0x0010 기준 수렴 1.37초
 *   DMA 패킹:     tdata[31:16]=R채널, [15:0]=L채널 (L815)
 * =============================================================================
 */

#ifndef ANALOG_DELAY_H
#define ANALOG_DELAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

    /* =============================================================================
     * §0  UIO 매핑 구조체
     * =============================================================================
     * 사용 예:
     *   analog_delay_t delay;
     *   analog_delay_open(&delay, "/dev/uio0", "/dev/uio1");
     *   analog_delay_init_with_lfo(&delay, 0.30f);
     */
    typedef struct {
        volatile uint32_t* base_delay;   /* analog_delay_top AXI-Lite 베이스 */
        volatile uint32_t* base_lfo;     /* nco_multi_lfo     AXI-Lite 베이스 */
        size_t             map_size_delay;
        size_t             map_size_lfo;
    } analog_delay_t;

    /* =============================================================================
     * §1  analog_delay_top AXI-Lite 레지스터 오프셋
     *     RTL: wr_addr[7:2] 기준. 바이트 오프셋 = 인덱스 × 4
     *     출처: analog_delay_stereo_top.v L194~213, L229~246
     * =============================================================================
     *
     *  오프셋  레지스터명           폭    유효비트  RTL 변수명
     *  0x00    delay_time           24b   [23:0]    reg_delay_time    Q16.8
     *  0x04    bypass               1b    [0]       reg_bypass
     *  0x08    feedback             16b   [15:0]    reg_feedback      Q0.15, RTL 상한 0x6000
     *  0x14    dry_wet              16b   [15:0]    reg_dry_wet       Q0.15, ≤ 0x7FFF
     *  0x30    diffusion_start      8b    [7:0]     reg_diffusion_start
     *  0x34    diffusion_mix        16b   [15:0]    reg_diffusion_mix Q0.15
     *  0x38    diffusion_g          16b   [15:0]    reg_diffusion_g   Q0.15, bit[15] 무시
     *  0x3C    lfo_depth_apf        16b   [15:0]    reg_lfo_depth_apf Q0.15
     *  0x40    slew_rate            16b   [15:0]    reg_slew_rate     최솟값 1
     *  0x44    sat_thresh           16b   [15:0]    reg_sat_thresh    bit[15] 무시
     *  0x48    bypass (alias)       1b    [0]       reg_bypass        ← 0x04와 동일, 사용금지
     *  0x4C    ms_width             16b   [15:0]    reg_ms_width      Q0.15
     *  0x50    outhpf_bypass        1b    [0]       reg_outhpf_bypass
     *  0x54    damp_alpha           16b   [15:0]    reg_damp_alpha    Q0.15, ≤ 0x7FFF
     *  0x58    damp_hf              16b   [15:0]    reg_damp_hf       Q0.15, ≤ 0x7FFF
     */
#define ADLY_OFF_DELAY_TIME       0x00u
#define ADLY_OFF_BYPASS           0x04u
#define ADLY_OFF_FEEDBACK         0x08u
#define ADLY_OFF_DRY_WET          0x14u
#define ADLY_OFF_DIFFUSION_START  0x30u
#define ADLY_OFF_DIFFUSION_MIX    0x34u
#define ADLY_OFF_DIFFUSION_G      0x38u
#define ADLY_OFF_LFO_DEPTH_APF    0x3Cu
#define ADLY_OFF_SLEW_RATE        0x40u
#define ADLY_OFF_SAT_THRESH       0x44u
#define ADLY_OFF_BYPASS_ALIAS     0x48u   /* 사용 금지 — 0x04와 동일 레지스터 */
#define ADLY_OFF_MS_WIDTH         0x4Cu
#define ADLY_OFF_OUTHPF_BYPASS    0x50u
#define ADLY_OFF_DAMP_ALPHA       0x54u
#define ADLY_OFF_DAMP_HF          0x58u

     /* DMA 출력 패킹 (RTL L815)
      * m_axis_dma_tdata = {ms_out_R[15:0], ms_out_L[15:0]}
      * tdata[31:16] = R채널,  tdata[15:0] = L채널 */
#define ADLY_DMA_L_SHIFT          0u
#define ADLY_DMA_R_SHIFT          16u
#define ADLY_DMA_L_MASK           0x0000FFFFu
#define ADLY_DMA_R_MASK           0xFFFF0000u
#define ADLY_DMA_EXTRACT_L(d)     ((int16_t)((d) & ADLY_DMA_L_MASK))
#define ADLY_DMA_EXTRACT_R(d)     ((int16_t)(((d) >> ADLY_DMA_R_SHIFT) & 0xFFFFu))

      /* =============================================================================
       * §2  딜레이 시간 상수
       *     RTL 근거: L115~117 (DT_W=24, BRAM_ADDR_W=17), L655 (addr계산)
       *
       *   DT_W = MAX_DELAY_BITS + 8 = 16 + 8 = 24
       *   정수부 [23:8] = 16b → 최대 65535샘플
       *   소수부 [7:0]  = 8b  → 1/256샘플 분해능
       *
       *   BRAM 파티션:
       *     BRAM_ADDR_W = 17,  BRAM_DEPTH = 131072
       *     rd_addr_r = {ch_sel(1b), addr(16b)}
       *     L채널: addr[16]=0 → 슬롯 0x00000~0x0FFFF
       *     R채널: addr[16]=1 → 슬롯 0x10000~0x1FFFF
       *
       *   Q16.8 변환:
       *     Q = ms × 48000/1000 × 256  = ms × 12288
       *     Q = samp × 256
       *     검증: 1000ms → Q=0xBB8000 → samp=48000 (≤ 65535 OK)
       * =============================================================================*/
#define ADLY_SAMPLE_RATE          48000u
#define ADLY_DT_W                 24u
#define ADLY_DT_INT_BITS          16u
#define ADLY_DT_FRAC_BITS         8u

#define ADLY_BRAM_ADDR_W          17u
#define ADLY_BRAM_DEPTH           131072u
#define ADLY_BRAM_CH_DEPTH        65536u

#define ADLY_MIN_DELAY_SAMP       4u           /* RTL MIN_DELAY_Q=0x000400 */
#define ADLY_MAX_DELAY_SAMP       65535u        /* 정수부 16b 최대          */
#define ADLY_MIN_DELAY_MS         0.0833f       /* 4 / 48000 × 1000         */
#define ADLY_MAX_DELAY_MS         1365.3f       /* 65535 / 48000 × 1000     */
#define ADLY_MIN_DELAY_Q          0x000400u     /* 4 << 8                   */
#define ADLY_MAX_DELAY_Q          0xFFFF00u     /* 65535 << 8               */

       /* Q16.8 변환 매크로 */
#define ADLY_MS_TO_Q168(ms)       ((uint32_t)((float)(ms) * 12288.0f))
#define ADLY_Q168_TO_MS(q)        ((float)((q) & 0xFFFFFFu) / 12288.0f)
#define ADLY_Q168_TO_SAMP(q)      (((q) & 0xFFFFFFu) >> 8)
#define ADLY_SAMP_TO_Q168(s)      ((uint32_t)((uint32_t)(s) << 8))

/* =============================================================================
 * §3  안전 경계 상수
 * =============================================================================*/
#define ADLY_DAMP_MAX_Q15         0x7FFFu   /* damp_alpha/hf 절대 상한 — 초과 시 발진  */
#define ADLY_DRY_WET_MAX_Q15      0x7FFFu   /* dry_wet 절대 상한 — 초과 시 믹스 왜곡   */
#define ADLY_DIFFUSION_G_MAX      0x7FFEu   /* APF g 이론 상한                         */
#define ADLY_DIFFUSION_G_SAFE     0x6666u   /* APF g 실용 상한 (g=0.800, 공명 방지)    */
#define ADLY_FB_MAX_Q15           0x6000u   /* RTL FB_MAX=0.75, L131                   */
#define ADLY_SAT_THRESH_MIN       0x0100u   /* 최솟값 — 0x0000이면 전체 클리핑         */
#define ADLY_SAT_THRESH_MAX       0x7FFFu   /* bit[15] RTL 무시 → 실효 상한 [14:0]    */
#define ADLY_SLEW_RATE_MIN        0x0001u   /* 최솟값 — 0이면 슬루 영구 정지           */
#define ADLY_LFO_DEPTH_APF_MAX    0x2FFFu   /* g_base=0x5000 기준 APF depth 상한        */
#define ADLY_DIFFUSION_OFF        0xFFu     /* diffusion_start=0xFF → APF 완전 OFF     */

 /* =============================================================================
  * §4  Q 스케일 / 변환 매크로
  * =============================================================================*/
#define ADLY_Q15_FULL             0x7FFFu
#define ADLY_Q15_HALF             0x4000u

#define ADLY_FLOAT_TO_Q15(f)      ((uint16_t)((float)(f) * 32767.0f))
#define ADLY_Q15_TO_FLOAT(q)      ((float)((q) & 0x7FFFu) / 32767.0f)
#define ADLY_PCT_TO_Q15(p)        ((uint16_t)((float)(p) / 100.0f * 32767.0f))
#define ADLY_Q15_TO_PCT(q)        ((float)((q) & 0x7FFFu) / 32767.0f * 100.0f)

  /* =============================================================================
   * §5  하드웨어 리셋 기본값 (RTL L168~181)
   * =============================================================================*/
#define ADLY_DEF_DELAY_TIME       0x270000u /* Q16.8: 10000샘플 = 320ms (tape echo 기본) */
#define ADLY_DEF_FEEDBACK         0x6000u   /* Q0.15: 0.750 = 75% (RTL 상한 풀 활용)    */
#define ADLY_DEF_DRY_WET          0x5999u   /* Q0.15: ~0.700 = 70% (에코 존재감 확보)    */
#define ADLY_DEF_DIFFUSION_START  0x00u     /* 0xFF=APF 완전 OFF → 에코 선명, 흐려짐 없음 */
#define ADLY_DEF_DIFFUSION_MIX    0x0900u   /* 0% → APF OFF 상태에서 mix 무의미, 0으로   */
#define ADLY_DEF_DIFFUSION_G      0x1000u   /* APF OFF이므로 g계수 무의미, 0으로          */
#define ADLY_DEF_LFO_DEPTH_APF    0x0400u   /* 0 = LFO 변조 OFF → 피치 안정, 에코 선명   */
#define ADLY_DEF_SLEW_RATE        0xFFFFu   /* INSTANT → reset_defaults 후 즉각 수렴     */
#define ADLY_DEF_SAT_THRESH       30000u    /* 높게 → 클리핑 거의 없음, 클린한 에코 반복  */
#define ADLY_DEF_MS_WIDTH         0x2000u   /* Q0.15: 0.250 = 25% (좁은 스테레오)        */
#define ADLY_DEF_DAMP_ALPHA       0x0200u   /* 0 = 피드백 루프 댐핑 OFF → HF 유지        */
#define ADLY_DEF_DAMP_HF          0x0300u   /* 0 = 출력 HF 댐핑 OFF                      */
#define ADLY_DEF_BYPASS           0u
#define ADLY_DEF_OUTHPF_BYPASS    0u

   /* =============================================================================
    * §6  IO 접근 인라인 함수
    * =============================================================================*/
    static inline void adly_wr(const analog_delay_t* d, uint32_t off, uint32_t val)
    {
        d->base_delay[off >> 2] = val;
    }
    static inline uint32_t adly_rd(const analog_delay_t* d, uint32_t off)
    {
        return d->base_delay[off >> 2];
    }

    /* =============================================================================
     * §7  딜레이 시간 제어
     *
     *   RTL 슬루 구조 (L741~748):
     *     매 샘플 처리마다 dt_slewed가 dt_target을 slew_rate Q단위씩 추적.
     *     slew_rate = 1이면 1/256샘플(≈0.021ms)씩 이동.
     *     실제 출력 딜레이는 즉시 바뀌지 않고 slew_rate 속도로 부드럽게 변화.
     *
     *   Q16.8 수치 예:
     *     100ms → Q=0x186000  (4800samp)
     *     500ms → Q=0x5DC000  (24000samp)
     *     1000ms → Q=0xBB8000 (48000samp)    ← 1초 딜레이 OK
     *     1365ms → Q=0xFFF000 (65520samp)    ← 하드웨어 최대
     * =============================================================================*/
    static inline void analog_delay_set_time_q168(const analog_delay_t* d, uint32_t q168)
    {
        if (q168 < ADLY_MIN_DELAY_Q) q168 = ADLY_MIN_DELAY_Q;
        if (q168 > ADLY_MAX_DELAY_Q) q168 = ADLY_MAX_DELAY_Q;
        adly_wr(d, ADLY_OFF_DELAY_TIME, q168 & 0xFFFFFFu);
    }
    static inline void analog_delay_set_time_ms(const analog_delay_t* d, float ms)
    {
        analog_delay_set_time_q168(d, ADLY_MS_TO_Q168(ms));
    }
    static inline void analog_delay_set_time_samp(const analog_delay_t* d, uint32_t samp)
    {
        analog_delay_set_time_q168(d, ADLY_SAMP_TO_Q168(samp));
    }
    static inline uint32_t analog_delay_get_time_q168(const analog_delay_t* d)
    {
        return adly_rd(d, ADLY_OFF_DELAY_TIME) & 0xFFFFFFu;
    }
    static inline float analog_delay_get_time_ms(const analog_delay_t* d)
    {
        return ADLY_Q168_TO_MS(analog_delay_get_time_q168(d));
    }
    static inline uint32_t analog_delay_get_time_samp(const analog_delay_t* d)
    {
        return ADLY_Q168_TO_SAMP(analog_delay_get_time_q168(d));
    }

    /* =============================================================================
     * §8  피드백
     *     RTL L131: FB_MAX=0x6000(75%) 하드클램프. SW에서도 동일 상한 적용.
     *     RTL 내부 |feedback| 절댓값 처리 → 음수 write는 무의미(절댓값과 동일 동작).
     * =============================================================================*/
    static inline void analog_delay_set_feedback_q15(const analog_delay_t* d, uint16_t q15)
    {
        if (q15 > ADLY_FB_MAX_Q15) q15 = ADLY_FB_MAX_Q15;
        adly_wr(d, ADLY_OFF_FEEDBACK, (uint32_t)q15);
    }
    /** @note 0~75.0%. 75% 초과는 RTL에서 75%로 클램프됨. */
    static inline void analog_delay_set_feedback_pct(const analog_delay_t* d, float pct)
    {
        if (pct < 0.0f)  pct = 0.0f;
        if (pct > 75.0f) pct = 75.0f;
        analog_delay_set_feedback_q15(d, ADLY_PCT_TO_Q15(pct));
    }
    static inline uint16_t analog_delay_get_feedback_q15(const analog_delay_t* d)
    {
        return (uint16_t)(adly_rd(d, ADLY_OFF_FEEDBACK) & 0xFFFFu);
    }

    /* =============================================================================
     * §9  드라이/웨트 믹스
     *     RTL L564: coeff_dry = 0x7FFF − dry_wet  (unsigned 16b 연산)
     *     dry_wet ≥ 0x8000 → wrap → dry 이득 2.0 → 믹스 완전 왜곡
     *     절대 상한: 0x7FFF
     * =============================================================================*/
    static inline void analog_delay_set_dry_wet_q15(const analog_delay_t* d, uint16_t q15)
    {
        if (q15 > ADLY_DRY_WET_MAX_Q15) q15 = ADLY_DRY_WET_MAX_Q15;
        adly_wr(d, ADLY_OFF_DRY_WET, (uint32_t)q15);
    }
    /** @note 0.0=완전 드라이, 100.0=완전 웻(≈99.997%). */
    static inline void analog_delay_set_dry_wet_pct(const analog_delay_t* d, float pct)
    {
        if (pct < 0.0f)   pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;
        analog_delay_set_dry_wet_q15(d, ADLY_PCT_TO_Q15(pct));
    }
    static inline uint16_t analog_delay_get_dry_wet_q15(const analog_delay_t* d)
    {
        return (uint16_t)(adly_rd(d, ADLY_OFF_DRY_WET) & 0xFFFFu);
    }

    /* =============================================================================
     * §10  피드백 IIR 댐핑 α (damp_alpha)
     *      RTL L574~579: iir_fb = prev×alpha + echo×(0x7FFF − alpha[14:0])
     *      alpha=0x0000: IIR 완전 바이패스 (echo_r 직결, TDM 누화 없음)
     *      alpha 클수록: 피드백 경로 HF 롤오프 강함 (테이프/BBD 시뮬레이션)
     *      [발진] alpha ≥ 0x8000 → 이득 2.0 → 즉시 발진. 절대 상한: 0x7FFF
     * =============================================================================*/
    static inline void analog_delay_set_damp_alpha_q15(const analog_delay_t* d, uint16_t q15)
    {
        if (q15 > ADLY_DAMP_MAX_Q15) q15 = ADLY_DAMP_MAX_Q15;
        adly_wr(d, ADLY_OFF_DAMP_ALPHA, (uint32_t)q15);
    }
    /** @note 0.0=IIR OFF, 최대 99.997% (100%=0x8000 금지). */
    static inline void analog_delay_set_damp_alpha_pct(const analog_delay_t* d, float pct)
    {
        if (pct < 0.0f)    pct = 0.0f;
        if (pct > 99.997f) pct = 99.997f;
        analog_delay_set_damp_alpha_q15(d, ADLY_PCT_TO_Q15(pct));
    }
    static inline uint16_t analog_delay_get_damp_alpha_q15(const analog_delay_t* d)
    {
        return (uint16_t)(adly_rd(d, ADLY_OFF_DAMP_ALPHA) & 0xFFFFu);
    }

    /* =============================================================================
     * §11  출력 HF 댐핑 α (damp_hf)
     *      RTL L583~588: damp_alpha와 동일한 IIR 구조
     *      damp_hf=0x0000: IIR 바이패스 (sat_r 직결)
     *      [발진] damp_hf ≥ 0x8000 → 즉시 발진. 절대 상한: 0x7FFF
     * =============================================================================*/
    static inline void analog_delay_set_damp_hf_q15(const analog_delay_t* d, uint16_t q15)
    {
        if (q15 > ADLY_DAMP_MAX_Q15) q15 = ADLY_DAMP_MAX_Q15;
        adly_wr(d, ADLY_OFF_DAMP_HF, (uint32_t)q15);
    }
    static inline void analog_delay_set_damp_hf_pct(const analog_delay_t* d, float pct)
    {
        if (pct < 0.0f)    pct = 0.0f;
        if (pct > 99.997f) pct = 99.997f;
        analog_delay_set_damp_hf_q15(d, ADLY_PCT_TO_Q15(pct));
    }
    static inline uint16_t analog_delay_get_damp_hf_q15(const analog_delay_t* d)
    {
        return (uint16_t)(adly_rd(d, ADLY_OFF_DAMP_HF) & 0xFFFFu);
    }

    /* =============================================================================
     * §12  디퓨전 (Schroeder APF × 4: cascade)
     *
     *  ┌─ diffusion_start 동작 ─────────────────────────────────────────────────┐
     *  │  ST_IDLE (L640): echo_idx=0  ← 매 샘플 리셋                           │
     *  │  ST_INTERP2 (L688): apply_diff = (echo_idx >= diffusion_start)         │
     *  │  → APF 평가 시점의 echo_idx는 항상 0                                   │
     *  │  → diffusion_start = 0x00: 0>=0 = TRUE  → APF 항상 ON                 │
     *  │  → diffusion_start ≥ 0x01: 0>=N = FALSE → APF 항상 OFF                │
     *  └────────────────────────────────────────────────────────────────────────┘
     *
     *  apf_g_mod = clamp(diffusion_g[14:0] + lfo_scaled, 0, 0x7FFF)
     *    bit[15]: RTL에서 무시. g→1.0 근방에서 강한 공명. 실용 상한: 0x6666(g=0.800)
     *
     *  diff_out = echo + (apf − echo) × diffusion_mix  (L521~539)
     * =============================================================================*/
    static inline void analog_delay_set_diffusion_start(const analog_delay_t* d, uint8_t idx)
    {
        adly_wr(d, ADLY_OFF_DIFFUSION_START, (uint32_t)idx);
    }
    static inline void analog_delay_set_diffusion_mix_q15(const analog_delay_t* d, uint16_t q15)
    {
        if (q15 > ADLY_Q15_FULL) q15 = ADLY_Q15_FULL;
        adly_wr(d, ADLY_OFF_DIFFUSION_MIX, (uint32_t)q15);
    }
    /** @param g_f  0.0 ~ 0.800 권장. 1.0 근방에서 강한 공명 발생. */
    static inline void analog_delay_set_diffusion_g_f(const analog_delay_t* d, float g_f)
    {
        if (g_f < 0.0f)   g_f = 0.0f;
        if (g_f > 0.800f) g_f = 0.800f;
        adly_wr(d, ADLY_OFF_DIFFUSION_G, (uint32_t)ADLY_FLOAT_TO_Q15(g_f));
    }
    /** @brief Schroeder APF on/off (RTL: echo_idx=0 시점 apply_diff = start==0)
     *  @param on  1=APF 활성(diffusion_start=0), 0=APF 비활성(0xFF) */
    static inline void analog_delay_set_apf_enable(const analog_delay_t* d, int on)
    {
        analog_delay_set_diffusion_start(d, on ? 0x00u : ADLY_DIFFUSION_OFF);
    }

    /** @brief 디퓨전 세 파라미터 한 번에 설정
     *  @param apf_on  1=APF 활성(start=0), 0=APF 비활성(start=0xFF)
     *                 (에코 인덱스가 아님 — raw 인덱스는 set_diffusion_start 사용) */
    static inline void analog_delay_set_diffusion(const analog_delay_t* d,
        int apf_on, float mix_f, float g_f)
    {
        analog_delay_set_apf_enable(d, apf_on);
        if (mix_f < 0.0f) mix_f = 0.0f;
        if (mix_f > 1.0f) mix_f = 1.0f;
        analog_delay_set_diffusion_mix_q15(d, ADLY_FLOAT_TO_Q15(mix_f));
        analog_delay_set_diffusion_g_f(d, g_f);
    }

    /* =============================================================================
     * §13  딜레이 길이 제어 (슬루 포함 완전판)
     *
     *  ── 슬루 수렴 공식 ────────────────────────────────────────────────────────
     *
     *  RTL (L741~748): 매 샘플마다 dt_slewed를 slew_rate Q단위씩 dt_target으로 이동.
     *
     *    수렴 스텝 수 = Δdt_Q16.8 / slew_rate
     *    Δdt_Q16.8   = Δms × 12288    (1ms = 12288 Q단위)
     *    수렴 시간   = 스텝 수 / 48000 (초)
     *
     *    → slew_rate = Δdt_Q16.8 / (conv_ms × 48000 / 1000)
     *                = Δms × 12288 / (conv_ms × 48)
     *                = Δms × 256 / conv_ms
     *
     *  ── 슬루 속도 가이드라인 (Δ=1000ms 기준) ──────────────────────────────────
     *
     *   slew_rate   수렴 시간   용도
     *  ─────────────────────────────────────────────────────────────────────────
     *   0x0001       256.0 s    초저속 드론 효과 (극단적)
     *   0x0008        32.0 s    매우 느린 테이프 드래그
     *   0x0010        16.0 s    느린 워블 (기본값)
     *   0x0040         4.0 s    느린 딜레이 스위프
     *   0x0100         1.0 s    표준 딜레이 변화 (1초)
     *   0x0400         0.25 s   빠른 딜레이 변화
     *   0x1400         0.05 s   거의 즉각 (<50ms)
     *   0xFFFF         ~4ms     즉각 전환 (pitch glitch 없음 보장 불가)
     *  ─────────────────────────────────────────────────────────────────────────
     *
     *  ── 부팅 슬루 주의 ──────────────────────────────────────────────────────
     *    리셋: dt_slewed = 0x200000 (8192samp = 170.7ms)
     *    기본 target = 0x300000 (12288samp = 256ms), Δ = 4096samp = 85.3ms
     *    slew=0x0010: 수렴 스텝 = 0x100000/0x10 = 65536 = 1.37초
     *    즉각 안정 필요 시: analog_delay_boot_wait() 사용
     *
     *  ── 컴파일타임 slew_rate 계산 매크로 ────────────────────────────────────
     *    ADLY_SLEW_FOR_CONV(delta_ms, conv_ms):
     *      Δ=delta_ms 변화를 conv_ms 안에 완료하는 slew_rate
     *      예: ADLY_SLEW_FOR_CONV(1000, 200) = 5120 = 0x1400
     * =============================================================================*/

     /* 컴파일타임 slew_rate 계산 (정수 산술) */
#define ADLY_SLEW_FOR_CONV(delta_ms, conv_ms) \
    ((uint16_t)(((uint32_t)(delta_ms) * 256u) / (uint32_t)(conv_ms) + 1u))

/* slew_rate 범위 프리셋 상수 */
#define ADLY_SLEW_INSTANT         0xFFFFu   /* ~4ms 수렴 (Δ=1000ms 기준)        */
#define ADLY_SLEW_FAST            0x1400u   /* ~50ms 수렴                        */
#define ADLY_SLEW_MEDIUM          0x0500u   /* ~200ms 수렴                       */
#define ADLY_SLEW_SLOW            0x0100u   /* ~1000ms(1초) 수렴                 */
#define ADLY_SLEW_DRIFT           0x0040u   /* ~4초 수렴 (느린 테이프 드리프트)  */
#define ADLY_SLEW_ULTRA_SLOW      0x0010u   /* ~16초 수렴 (기본값)               */

/**
 * @brief 슬루 레이트 직접 설정
 * @param rate  Q단위/샘플. 최솟값 1.
 */
    static inline void analog_delay_set_slew_rate(const analog_delay_t* d, uint16_t rate)
    {
        if (rate < ADLY_SLEW_RATE_MIN) rate = ADLY_SLEW_RATE_MIN;
        adly_wr(d, ADLY_OFF_SLEW_RATE, (uint32_t)rate);
    }

    /**
     * @brief 수렴 시간 지정 방식으로 슬루 레이트 설정
     *
     * @param delta_ms  예상 딜레이 변화량 (ms). 실제 변화가 이보다 크면 느려짐.
     * @param conv_ms   delta_ms 변화를 완료할 목표 시간 (ms). 최솟값 1ms.
     *
     * 예: analog_delay_set_slew_by_time(d, 1000.0f, 500.0f)
     *   → 딜레이가 1000ms 바뀔 때 500ms 안에 수렴하도록 slew_rate 계산
     */
    static inline void analog_delay_set_slew_by_time(const analog_delay_t* d,
        float delta_ms, float conv_ms)
    {
        if (conv_ms < 1.0f) conv_ms = 1.0f;
        if (delta_ms < 1.0f) delta_ms = 1.0f;
        /* slew = Δdt_Q / conv_samp = (delta_ms × 12288) / (conv_ms × 48) */
        float slew_f = (delta_ms * 256.0f) / conv_ms;
        uint32_t slew = (uint32_t)(slew_f + 0.5f);
        if (slew < ADLY_SLEW_RATE_MIN) slew = ADLY_SLEW_RATE_MIN;
        if (slew > 0xFFFFu) slew = 0xFFFFu;
        adly_wr(d, ADLY_OFF_SLEW_RATE, slew);
    }

    /**
     * @brief 딜레이 시간 + 슬루 레이트를 한 번에 설정
     *
     * @param target_ms  목표 딜레이 시간 (ms). 범위: 0.083~1365.3ms
     * @param conv_ms    수렴 목표 시간 (ms). 0이면 현재 slew_rate 유지.
     *                   예: 0 → 현재 속도 유지,  200 → 200ms 안에 수렴
     *
     * 내부적으로 현재값과 목표값의 차이를 delta_ms로 사용해 slew_rate 계산.
     * 즉각 전환이 필요하면 conv_ms를 작게 (예: 10.0f) 설정.
     */
    static inline void analog_delay_set_range(const analog_delay_t* d,
        float target_ms, float conv_ms)
    {
        float current_ms = analog_delay_get_time_ms(d);
        float delta_ms = target_ms - current_ms;
        if (delta_ms < 0.0f) delta_ms = -delta_ms;
        if (delta_ms < 1.0f) delta_ms = 1.0f;

        if (conv_ms > 0.0f)
            analog_delay_set_slew_by_time(d, delta_ms, conv_ms);

        analog_delay_set_time_ms(d, target_ms);
    }

    /**
     * @brief 딜레이 길이를 0~1 노브값으로 제어 (범위 지정)
     *
     * 하드웨어 노브, MIDI CC 등 0.0~1.0 범위 제어에 사용.
     * 선형 스케일: knob=0.0 → min_ms, knob=1.0 → max_ms
     *
     * @param knob    0.0 ~ 1.0
     * @param min_ms  노브 최소값에 대응하는 딜레이 (ms). 기본: 10.0ms
     * @param max_ms  노브 최대값에 대응하는 딜레이 (ms). 기본: 1000.0ms
     * @param conv_ms 수렴 시간 (ms). 0이면 현재 slew_rate 유지.
     *
     * 예: analog_delay_set_knob(d, 0.5f, 50.0f, 1000.0f, 0.0f)
     *   → 525ms (50~1000ms 범위의 50%)
     */
    static inline void analog_delay_set_knob(const analog_delay_t* d,
        float knob, float min_ms, float max_ms, float conv_ms)
    {
        if (knob < 0.0f) knob = 0.0f;
        if (knob > 1.0f) knob = 1.0f;
        if (min_ms < ADLY_MIN_DELAY_MS) min_ms = ADLY_MIN_DELAY_MS;
        if (max_ms > ADLY_MAX_DELAY_MS) max_ms = ADLY_MAX_DELAY_MS;
        float target_ms = min_ms + knob * (max_ms - min_ms);
        analog_delay_set_range(d, target_ms, conv_ms);
    }

    /**
     * @brief 딜레이 길이를 BPM과 박자로 설정
     *
     * @param bpm         템포 (BPM). 예: 120.0f
     * @param beat_div    박자 분모. 예: 4=4분음표, 8=8분음표, 3=3연음(♩×2/3)
     * @param beats       박자 수. 예: 1=1박, 2=2박
     * @param conv_ms     슬루 수렴 시간 (ms). 0이면 현재 slew_rate 유지.
     *
     * 수식: delay_ms = beats × (60000 / bpm) / beat_div × 4
     * 예: bpm=120, beat_div=4, beats=1 → 500ms (4분음표 1박)
     *     bpm=120, beat_div=8, beats=1 → 250ms (8분음표 1박)
     *     bpm=120, beat_div=3, beats=1 → 667ms (부점4분음표)
     */
    static inline void analog_delay_set_tempo(const analog_delay_t* d,
        float bpm, int beat_div, int beats, float conv_ms)
    {
        if (bpm < 20.0f)   bpm = 20.0f;
        if (bpm > 300.0f)  bpm = 300.0f;
        if (beat_div < 1)  beat_div = 1;
        if (beats < 1)     beats = 1;
        /* 4분음표 ms = 60000/bpm */
        float quarter_ms = 60000.0f / bpm;
        float delay_ms = (float)beats * quarter_ms * 4.0f / (float)beat_div;
        analog_delay_set_range(d, delay_ms, conv_ms);
    }

    /**
     * @brief 부팅 후 슬루 수렴 대기
     *
     * 리셋 직후 dt_slewed=0x200000(170.7ms) ≠ 기본 target=0x300000(256ms)
     * slew=0x0010 기준 수렴에 1.37초 소요.
     *
     * @param fast_rate  임시 빠른 슬루 레이트 (추천: 0x1000). 0이면 현재 유지.
     */
    static inline void analog_delay_boot_wait(const analog_delay_t* d, uint16_t fast_rate)
    {
        uint16_t orig_rate = (uint16_t)(adly_rd(d, ADLY_OFF_SLEW_RATE) & 0xFFFFu);
        uint16_t use_rate = (fast_rate > 0u) ? fast_rate : orig_rate;
        if (use_rate < ADLY_SLEW_RATE_MIN) use_rate = ADLY_SLEW_RATE_MIN;
        if (fast_rate > 0u) analog_delay_set_slew_rate(d, use_rate);
        /* Δmax = 0x100000 Q단위, 마진 ×4 */
        volatile uint32_t i;
        uint32_t wait = ((0x100000u * 4u) / (uint32_t)use_rate) + 512u;
        for (i = 0; i < wait; i++) { (void)adly_rd(d, ADLY_OFF_DELAY_TIME); }
        if (fast_rate > 0u) analog_delay_set_slew_rate(d, orig_rate);
    }

    /* =============================================================================
     * §14  소프트 클립 (포화 임계값)
     *      RTL L477~495: t_pos = thresh[14:0], bit[15] 완전 무시
     *      thresh=0x0000: 전체 클리핑 → 출력 거의 0 (금지)
     *      thresh=0x7FFF: 클리핑 없음 (bypass 동작)
     * =============================================================================*/
    static inline void analog_delay_set_sat_thresh(const analog_delay_t* d, uint16_t thresh)
    {
        if (thresh < ADLY_SAT_THRESH_MIN) thresh = ADLY_SAT_THRESH_MIN;
        if (thresh > ADLY_SAT_THRESH_MAX) thresh = ADLY_SAT_THRESH_MAX;
        adly_wr(d, ADLY_OFF_SAT_THRESH, (uint32_t)thresh);
    }
    /** @param pct 클리핑 시작 임계값 (풀스케일 대비 %). 최솟값 ~0.5%. */
    static inline void analog_delay_set_sat_thresh_pct(const analog_delay_t* d, float pct)
    {
        if (pct < 0.5f)   pct = 0.5f;
        if (pct > 100.0f) pct = 100.0f;
        uint16_t t = (uint16_t)(pct / 100.0f * 32767.0f);
        analog_delay_set_sat_thresh(d, t);
    }

    /* =============================================================================
     * §15  M/S 와이드닝
     *      RTL L785~801: ÷2 생략 → 완전 상관 신호(L=R) 시 최대 6dB 이득 증가,
     *      무상관 스테레오 평균 3dB. sat16 하드클립으로 보호됨 (의도된 설계)
     *      ms_width=0: L=R=mono, ms_width=0x7FFF: 최대 확산+최대 이득
     * =============================================================================*/
    static inline void analog_delay_set_ms_width_q15(const analog_delay_t* d, uint16_t q15)
    {
        if (q15 > ADLY_Q15_FULL) q15 = ADLY_Q15_FULL;
        adly_wr(d, ADLY_OFF_MS_WIDTH, (uint32_t)q15);
    }
    static inline void analog_delay_set_ms_width_pct(const analog_delay_t* d, float pct)
    {
        if (pct < 0.0f)   pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;
        analog_delay_set_ms_width_q15(d, ADLY_PCT_TO_Q15(pct));
    }

    /* =============================================================================
     * §16  바이패스 / HPF 바이패스
     *      bypass=1: 입력 신호 그대로 출력 (딜레이 루프 동결)
     *      [주의] 0x04와 0x48가 동일 레지스터 — ADLY_OFF_BYPASS_ALIAS 사용 금지
     * =============================================================================*/
    static inline void analog_delay_set_bypass(const analog_delay_t* d, int on)
    {
        adly_wr(d, ADLY_OFF_BYPASS, on ? 1u : 0u);
    }
    static inline void analog_delay_set_outhpf_bypass(const analog_delay_t* d, int on)
    {
        adly_wr(d, ADLY_OFF_OUTHPF_BYPASS, on ? 1u : 0u);
    }
    static inline int analog_delay_get_bypass(const analog_delay_t* d)
    {
        return (int)(adly_rd(d, ADLY_OFF_BYPASS) & 1u);
    }

    /* =============================================================================
     * §17  APF LFO 깊이
     *      RTL L422~427: apf_g_mod = clamp(g_base + lfo_apf × depth_apf, 0, 0x7FFF)
     *      depth_apf_f 와 lfo_set_for_delay()의 depth_apf_f 인자는 반드시 일치해야 함
     * =============================================================================*/
    static inline void analog_delay_set_lfo_depth_apf(const analog_delay_t* d, float depth_f)
    {
        if (depth_f < 0.0f) depth_f = 0.0f;
        uint32_t raw = (uint32_t)ADLY_FLOAT_TO_Q15(depth_f);
        if (raw > ADLY_LFO_DEPTH_APF_MAX) raw = ADLY_LFO_DEPTH_APF_MAX;
        adly_wr(d, ADLY_OFF_LFO_DEPTH_APF, raw);
    }

    /* =============================================================================
     * §18  전체 초기화 / 기본값 복원
     * =============================================================================*/
    static inline void analog_delay_reset_defaults(const analog_delay_t* d)
    {
        analog_delay_set_bypass(d, 0);
        analog_delay_set_time_q168(d, ADLY_DEF_DELAY_TIME);
        analog_delay_set_feedback_q15(d, ADLY_DEF_FEEDBACK);
        analog_delay_set_dry_wet_q15(d, ADLY_DEF_DRY_WET);
        analog_delay_set_diffusion(d, 1,
            ADLY_DEF_DIFFUSION_MIX / 32767.0f,
            ADLY_DEF_DIFFUSION_G / 32767.0f);
        analog_delay_set_lfo_depth_apf(d, ADLY_DEF_LFO_DEPTH_APF / 32767.0f);
        analog_delay_set_slew_rate(d, ADLY_DEF_SLEW_RATE);
        analog_delay_set_sat_thresh(d, ADLY_DEF_SAT_THRESH);
        analog_delay_set_ms_width_q15(d, ADLY_DEF_MS_WIDTH);
        analog_delay_set_outhpf_bypass(d, ADLY_DEF_OUTHPF_BYPASS);
        analog_delay_set_damp_alpha_q15(d, ADLY_DEF_DAMP_ALPHA);
        analog_delay_set_damp_hf_q15(d, ADLY_DEF_DAMP_HF);
    }

    /* =============================================================================
     * §19  nco_multi_lfo Rev.4 — AXI-Lite 레지스터 맵
     *      RTL 근거: LFO.v L227~237, L295~303
     *
     *  주소 디코딩 (LFO.v L235~236):
     *    ch  = addr[6:4]  → 채널 0~7
     *    reg = addr[3:2]  → 레지스터 종류 0~3
     *    오프셋 = (ch << 4) | (reg << 2)
     *
     *  레지스터 종류:
     *    reg=0 (오프셋 +0x00): FCW    32b  주파수 제어 워드
     *    reg=1 (오프셋 +0x04): DEPTH  16b  진폭 (0x0000=0, 0x8000=1.0, 0xFFFF≈2.0)
     *    reg=2 (오프셋 +0x08): POFF   32b  위상 오프셋 (전주기=2^32)
     *    reg=3 (오프셋 +0x0C): WAVE   2b   0=Sine, 1=Triangle, 2=Square
     *
     *  채널별 베이스 오프셋:
     *    CH0=0x00, CH1=0x10, CH2=0x20, CH3=0x30
     *    CH4=0x40, CH5=0x50, CH6=0x60, CH7=0x70
     *
     *  ── delay_top 유효 배선 ───────────────────────────────────────────────────
     *    [유효] CH0 (lfo_0): APF g 주 변조 (무감쇠 직결)
     *    [유효] CH4 (lfo_4): lfo_apf_L/R 공통 offset — RTL >>>3(÷8) 감쇠 후 합산
     *    [유효] CH5 (lfo_5): lfo_apf_R 전용 추가 offset — RTL >>>2(÷4) 감쇠
     *    [DEAD] CH1,2,3,6,7: delay_top에 배선 없음 → 설정해도 완전 무효
     *
     *  ── RTL 실제 FCW값 (LFO.v L69~77, 검증됨) ───────────────────────────────
     *    CH0: 0x45  (0.8033Hz), CH1: 0x2B (0.5006Hz)
     *    CH2: 0x1A  (0.3027Hz), CH3: 0x0D (0.1513Hz)
     *    CH4: 0x1A  (0.3027Hz), CH5: 0x1A (0.3027Hz)
     *    CH6: 0x183 (4.5053Hz), CH7: 0x183 (4.5053Hz, +180° 역위상)
     *
     *  ── Depth Q포맷 (LFO.v L515~516) ─────────────────────────────────────────
     *    out_val = wave_raw(16b signed) × {1'b0, depth} → [30:15]
     *    depth=0x0000: 출력 0
     *    depth=0x8000: 이득 정확히 1.0
     *    depth=0xFFFF: 이득 ≈ 2.0 (수치 발산 없음, 단 오버플로 주의)
     *    권장 상한: 0x8000 (이득 1.0)
     *
     *  ── FCW 변환 (LFO.v L13): FCW = f_hz × 2^32 / 50_000_000 ─────────────────
     *    double 정밀도 필수 (32b 전체 범위 커버)
     *    오차: CH7(4.50Hz)=FCW 0x183 → 실제 4.5053Hz (+0.12%) — 허용 범위
     *
     *  ── 위상 오프셋 (LFO.v L357~365) ─────────────────────────────────────────
     *    32b, 오버플로=자연 위상 순환 (2^32 = 360°)
     *    45°=0x20000000, 90°=0x40000000, 120°=0x55555555
     *    180°=0x80000000, 270°=0xC0000000
     * =============================================================================*/

     /* 채널 번호 */
#define LFO_CH_APF_MAIN     0u   /* CH0: APF g 주 변조 (유효)          */
#define LFO_CH_L_OFFSET     4u   /* CH4: L+R APF offset ÷8 (유효)      */
#define LFO_CH_R_OFFSET     5u   /* CH5: R 전용 APF offset ÷4 (유효)   */
#define LFO_CH_DEAD_1       1u   /* DEAD — delay_top 배선 없음          */
#define LFO_CH_DEAD_2       2u
#define LFO_CH_DEAD_3       3u
#define LFO_CH_DEAD_6       6u
#define LFO_CH_DEAD_7       7u

/* 오프셋 계산: LFO.v L235~236 */
#define LFO_OFF(ch, reg)    (((uint32_t)(ch) << 4) | ((uint32_t)(reg) << 2))

/* 파형 선택 (LFO.v L509~512) */
#define LFO_WAVE_SINE       0u
#define LFO_WAVE_TRIANGLE   1u
#define LFO_WAVE_SQUARE     2u
/* wave=3 → RTL default: sine_signed (sine으로 fallthrough) */

/* 위상 오프셋 상수 (전주기 2^32 = 360°) */
#define LFO_POFF_0DEG       0x00000000u
#define LFO_POFF_45DEG      0x20000000u
#define LFO_POFF_90DEG      0x40000000u
#define LFO_POFF_120DEG     0x55555555u
#define LFO_POFF_180DEG     0x80000000u
#define LFO_POFF_270DEG     0xC0000000u

/* FCW 변환: FCW = f_hz × 2^32 / CLK_HZ */
#define LFO_CLK_HZ          50000000u
#define LFO_HZ_TO_FCW(hz)   ((uint32_t)((double)(hz) * 4294967296.0 / 50000000.0 + 0.5))
#define LFO_FCW_TO_HZ(fw)   ((float)((uint32_t)(fw)) * 50000000.0f / 4294967296.0f)

/* RTL 하드코딩 기본 FCW (LFO.v L69~77, 실측값) */
#define LFO_FCW_0_80HZ      0x00000045u   /* 0.8033Hz (+0.41%)  CH0 기본 */
#define LFO_FCW_0_50HZ      0x0000002Bu   /* 0.5006Hz (+0.12%)           */
#define LFO_FCW_0_30HZ      0x0000001Au   /* 0.3027Hz (+0.89%)  CH4,CH5  */
#define LFO_FCW_0_15HZ      0x0000000Du   /* 0.1513Hz (+0.89%)  CH3      */
#define LFO_FCW_4_50HZ      0x00000183u   /* 4.5053Hz (+0.12%)  CH6      */
#define LFO_FCW_0_20HZ      0x00000011u   /* 0.1979Hz (-1.05%)  (CH7 기본값 아님: CH7=0x183)  */

/* Depth Q포맷 (LFO.v L78~87, L515~516) */
#define LFO_DEPTH_MAX       0x8000u    /* 이득 1.0 권장 상한 */
#define LFO_DEPTH_OFF       0x0000u    /* 출력 0              */
#define LFO_FLOAT_TO_DEPTH(f) \
    ((uint16_t)((float)(f) < 0.0f ? 0u : \
                (float)(f) > 1.0f ? LFO_DEPTH_MAX : \
                (uint16_t)((float)(f) * 32768.0f)))
#define LFO_DEPTH_TO_FLOAT(d) ((float)((d) & 0xFFFFu) / 32768.0f)

/* =============================================================================
 * §20  LFO IO 접근
 * =============================================================================*/
    static inline void lfo_wr(const analog_delay_t* d, uint32_t off, uint32_t val)
    {
        d->base_lfo[off >> 2] = val;
    }
    static inline uint32_t lfo_rd(const analog_delay_t* d, uint32_t off)
    {
        return d->base_lfo[off >> 2];
    }

    static inline void lfo_set_fcw(const analog_delay_t* d, uint8_t ch, uint32_t fcw)
    {
        lfo_wr(d, LFO_OFF(ch, 0u), fcw);
    }
    static inline void lfo_set_depth(const analog_delay_t* d, uint8_t ch, uint16_t depth)
    {
        if (depth > LFO_DEPTH_MAX) depth = LFO_DEPTH_MAX;
        lfo_wr(d, LFO_OFF(ch, 1u), (uint32_t)depth);
    }
    static inline void lfo_set_poff(const analog_delay_t* d, uint8_t ch, uint32_t poff)
    {
        lfo_wr(d, LFO_OFF(ch, 2u), poff);
    }
    static inline void lfo_set_wave(const analog_delay_t* d, uint8_t ch, uint8_t wave)
    {
        lfo_wr(d, LFO_OFF(ch, 3u), (uint32_t)(wave & 0x3u));
    }
    static inline uint32_t lfo_get_fcw(const analog_delay_t* d, uint8_t ch) { return lfo_rd(d, LFO_OFF(ch, 0u)); }
    static inline uint16_t lfo_get_depth(const analog_delay_t* d, uint8_t ch) { return (uint16_t)(lfo_rd(d, LFO_OFF(ch, 1u)) & 0xFFFFu); }
    static inline uint32_t lfo_get_poff(const analog_delay_t* d, uint8_t ch) { return lfo_rd(d, LFO_OFF(ch, 2u)); }
    static inline uint8_t  lfo_get_wave(const analog_delay_t* d, uint8_t ch) { return (uint8_t)(lfo_rd(d, LFO_OFF(ch, 3u)) & 0x3u); }

    /* =============================================================================
     * §21  LFO 채널 통합 설정
     * =============================================================================*/
    static inline void lfo_set_channel(const analog_delay_t* d, uint8_t ch,
        float hz, float depth_f, uint32_t poff, uint8_t wave)
    {
        lfo_set_fcw(d, ch, LFO_HZ_TO_FCW(hz));
        lfo_set_depth(d, ch, LFO_FLOAT_TO_DEPTH(depth_f));
        lfo_set_poff(d, ch, poff);
        lfo_set_wave(d, ch, wave);
    }

    /* =============================================================================
     * §22  딜레이 전용 LFO 설정
     *
     *  APF g 변조 수식 (RTL 완전 검증, L318~329, L422~427):
     *    lfo_apf_L = lfo_0 + (lfo_4 >>> 3)           = CH0 + CH4_reg/8
     *    lfo_apf_R = lfo_0 + (lfo_4 >>> 3) + (lfo_5 >>> 2) = CH0 + CH4_reg/8 + CH5_reg/4
     *
     *    apf_g_swing ≈ (CH0_depth + CH4_depth/8) × depth_apf / 32768   [L채널 기준]
     *
     *  lfo_set_for_delay() depth 역산 (수정된 정확한 수식):
     *    필요 peak_q [Q0.15 단위] = g_swing × 32768 / depth_apf
     *    CH0/CH4 분배 2:1:
     *      d0_q = peak_q × 2/3                         (무감쇠 직결)
     *      d4_q = peak_q × 1/3 × 8                     (레지스터값, RTL이 /8 적용)
     *    CH5 스테레오 확산:
     *      d5_q = stereo_spread × peak_q × 4           (레지스터값, RTL이 /4 적용)
     *
     *  오버플로 안전 조건: d0_q + d4_q/8 ≤ 32767
     *    위반 시 lfo_apf_cur wrap → g_mod 일시 0 (APF 투명 구간 발생)
     * =============================================================================*/

     /**
      * @brief 딜레이 APF 변조에 최적화된 LFO 설정
      *
      * @param g_swing_f       APF g 변조 진폭 ±값 (0.05~0.20 권장, 최대 0.30)
      * @param rate_hz         CH0 주 변조 주파수 (Hz, 0.3~1.5 권장)
      * @param stereo_spread_f L/R 스테레오 확산 비율 (0.0=모노, 1.0=최대)
      * @param depth_apf_f     delay_top reg_lfo_depth_apf float값 (이 함수 전에 설정)
      *
      * analog_delay_set_lfo_depth_apf(d, depth_apf_f) 와 반드시 같은 값 사용.
      */
    static inline void lfo_set_for_delay(const analog_delay_t* d,
        float g_swing_f, float rate_hz,
        float stereo_spread_f, float depth_apf_f)
    {
        /* 인자 클램프 */
        if (g_swing_f < 0.0f)  g_swing_f = 0.0f;
        if (g_swing_f > 0.30f) g_swing_f = 0.30f;
        if (rate_hz < 0.01f) rate_hz = 0.01f;
        if (rate_hz > 5.0f)  rate_hz = 5.0f;
        if (stereo_spread_f < 0.0f)  stereo_spread_f = 0.0f;
        if (stereo_spread_f > 1.0f)  stereo_spread_f = 1.0f;
        if (depth_apf_f < 0.001f)depth_apf_f = 0.001f;
        if (depth_apf_f > 0.999f)depth_apf_f = 0.999f;

        /* g_swing에 필요한 lfo_apf_L peak [Q0.15 단위] 역산
         *   g_swing = (d0_q + d4_q/8) / 32768 × depth_apf
         *   → peak_q = g_swing × 32768 / depth_apf                              */
        float peak_q = g_swing_f * 32768.0f / depth_apf_f;
        if (peak_q > 32767.0f) peak_q = 32767.0f;

        /* CH0/CH4 2:1 분배 */
        float d0_q = peak_q * (2.0f / 3.0f);
        float d4_q = peak_q * (1.0f / 3.0f) * 8.0f;   /* 레지스터값 (RTL이 ÷8 적용) */

        /* CH4 overflow 보정 */
        if (d4_q > 32768.0f) {
            float scale = 32768.0f / d4_q * 0.98f;
            d0_q *= scale;
            d4_q = 32768.0f;
        }
        /* L채널 오버플로 검사: d0_q + d4_q/8 ≤ 32767 */
        float actual_peak = d0_q + d4_q / 8.0f;
        if (actual_peak > 32767.0f) {
            float sc = 32767.0f / actual_peak * 0.99f;
            d0_q *= sc;
            d4_q *= sc;
        }

        /* CH5 스테레오 확산 (레지스터값; RTL이 ÷4 적용) */
        float d5_q = stereo_spread_f * peak_q * 4.0f;
        if (d5_q > 32768.0f) d5_q = 32768.0f;

        /* float → Q0.15 */
        float d0_f = d0_q / 32768.0f;
        float d4_f = d4_q / 32768.0f;
        float d5_f = d5_q / 32768.0f;
        if (d0_f > 1.0f) d0_f = 1.0f;
        if (d4_f > 1.0f) d4_f = 1.0f;
        if (d5_f > 1.0f) d5_f = 1.0f;

        /* CH0: 주 변조 (Sine, 0°) */
        lfo_set_channel(d, LFO_CH_APF_MAIN,
            rate_hz, d0_f, LFO_POFF_0DEG, LFO_WAVE_SINE);

        /* CH4: L+R 공통 offset (rate × 0.375, 0°) — 속도 차별화로 beating 효과 */
        lfo_set_channel(d, LFO_CH_L_OFFSET,
            rate_hz * 0.375f, d4_f, LFO_POFF_0DEG, LFO_WAVE_SINE);

        /* CH5: R 전용 추가 offset (rate × 0.375, +45° — 스테레오 위상차) */
        lfo_set_channel(d, LFO_CH_R_OFFSET,
            rate_hz * 0.375f, d5_f, LFO_POFF_45DEG, LFO_WAVE_SINE);

        /* DEAD 채널 0 초기화 */
        lfo_set_channel(d, LFO_CH_DEAD_1, 0.0f, 0.0f, LFO_POFF_0DEG, LFO_WAVE_SINE);
        lfo_set_channel(d, LFO_CH_DEAD_2, 0.0f, 0.0f, LFO_POFF_0DEG, LFO_WAVE_SINE);
        lfo_set_channel(d, LFO_CH_DEAD_3, 0.0f, 0.0f, LFO_POFF_0DEG, LFO_WAVE_SINE);
        lfo_set_channel(d, LFO_CH_DEAD_6, 0.0f, 0.0f, LFO_POFF_0DEG, LFO_WAVE_SINE);
        lfo_set_channel(d, LFO_CH_DEAD_7, 0.0f, 0.0f, LFO_POFF_0DEG, LFO_WAVE_SINE);
    }

    /**
     * @brief LFO 기본값 설정 (depth_apf=0.30 기준)
     *
     * g_swing=±0.08, rate=0.80Hz, stereo=0.60
     * analog_delay_set_lfo_depth_apf(d, 0.30f) 와 함께 사용.
     */
    static inline void lfo_reset_defaults(const analog_delay_t* d)
    {
        lfo_set_for_delay(d, 0.08f, 0.80f, 0.60f, 0.30f);
    }

    /* =============================================================================
     * §23  LFO + 딜레이 통합 초기화 시퀀스
     *
     *  올바른 초기화 순서:
     *    1. analog_delay_reset_defaults(d)
     *    2. analog_delay_set_lfo_depth_apf(d, depth_apf_f)
     *    3. lfo_set_for_delay(d, ..., depth_apf_f)     ← 동일한 depth_apf_f
     *    4. analog_delay_boot_wait(d, 0x1000)          ← 슬루 수렴 대기
     *    5. 프리셋 또는 수동 파라미터 적용
     * =============================================================================*/

     /** @brief 딜레이+LFO 통합 초기화 (권장 시퀀스)
      *  @param depth_apf_f  APF depth (0.10~0.40 권장) */
    static inline void analog_delay_init_with_lfo(analog_delay_t* d, float depth_apf_f)
    {
        if (depth_apf_f < 0.10f) depth_apf_f = 0.10f;
        if (depth_apf_f > 0.40f) depth_apf_f = 0.40f;

        analog_delay_reset_defaults(d);
        analog_delay_set_lfo_depth_apf(d, depth_apf_f);
        lfo_set_for_delay(d, 0.08f, 0.80f, 0.60f, depth_apf_f);
        analog_delay_boot_wait(d, 0x1000u);
    }

    /* =============================================================================
     * §24  프리셋
     *      모든 프리셋: set 함수 경유 → 클램프 보장
     *      slew_rate: 각 프리셋 특성에 맞는 수렴 속도 적용
     * =============================================================================*/

     /** 클린 디지털 딜레이 — 250ms, 피드백 50%, IIR/APF 없음 */
    static inline void analog_delay_preset_clean(analog_delay_t* d)
    {
        analog_delay_set_bypass(d, 0);
        analog_delay_set_time_ms(d, 250.0f);
        analog_delay_set_feedback_pct(d, 50.0f);
        analog_delay_set_dry_wet_pct(d, 40.0f);
        analog_delay_set_diffusion(d, 0, 0.0f, 0.5f);
        analog_delay_set_lfo_depth_apf(d, 0.0f);
        analog_delay_set_slew_rate(d, ADLY_SLEW_FAST);       /* 즉각 전환 */
        analog_delay_set_sat_thresh(d, 0x7FFFu);
        analog_delay_set_ms_width_q15(d, 0x2000u);
        analog_delay_set_damp_alpha_q15(d, 0x0000u);
        analog_delay_set_damp_hf_q15(d, 0x0000u);
    }

    /** 테이프 딜레이 — 320ms, HF 롤오프, APF 확산 */
    static inline void analog_delay_preset_tape(analog_delay_t* d)
    {
        analog_delay_set_bypass(d, 0);
        analog_delay_set_time_ms(d, 320.0f);
        analog_delay_set_feedback_pct(d, 45.0f);
        analog_delay_set_dry_wet_pct(d, 45.0f);
        analog_delay_set_diffusion(d, 1, 0.35f, 0.50f);
        analog_delay_set_lfo_depth_apf(d, 0.008f);
        analog_delay_set_slew_rate(d, ADLY_SLEW_DRIFT);      /* 느린 테이프 드래그 */
        analog_delay_set_sat_thresh(d, 20000u);
        analog_delay_set_ms_width_q15(d, 0x3000u);
        analog_delay_set_damp_alpha_q15(d, 0x5800u);
        analog_delay_set_damp_hf_q15(d, 0x2000u);
    }

    /** BBD 시뮬레이션 — 짧은 딜레이, 강한 HF 롤오프 */
    static inline void analog_delay_preset_bbd(analog_delay_t* d)
    {
        analog_delay_set_bypass(d, 0);
        analog_delay_set_time_ms(d, 180.0f);
        analog_delay_set_feedback_pct(d, 60.0f);
        analog_delay_set_dry_wet_pct(d, 50.0f);
        analog_delay_set_diffusion(d, 1, 0.60f, 0.55f);
        analog_delay_set_lfo_depth_apf(d, 0.012f);
        analog_delay_set_slew_rate(d, ADLY_SLEW_DRIFT);
        analog_delay_set_sat_thresh(d, 18000u);
        analog_delay_set_ms_width_q15(d, 0x4000u);
        analog_delay_set_damp_alpha_q15(d, 0x6800u);
        analog_delay_set_damp_hf_q15(d, 0x3000u);
    }

    /** 슬랩백 — 짧은 단발 딜레이, 피드백 없음 */
    static inline void analog_delay_preset_slapback(analog_delay_t* d)
    {
        analog_delay_set_bypass(d, 0);
        analog_delay_set_time_ms(d, 80.0f);
        analog_delay_set_feedback_pct(d, 0.0f);
        analog_delay_set_dry_wet_pct(d, 35.0f);
        analog_delay_set_diffusion(d, 0, 0.0f, 0.5f);
        analog_delay_set_lfo_depth_apf(d, 0.0f);
        analog_delay_set_slew_rate(d, ADLY_SLEW_INSTANT);
        analog_delay_set_sat_thresh(d, 0x7FFFu);
        analog_delay_set_ms_width_q15(d, 0x6000u);
        analog_delay_set_damp_alpha_q15(d, 0x0000u);
        analog_delay_set_damp_hf_q15(d, 0x0000u);
    }

    /** 긴 에코 — 800ms, 자기 공진 직전 피드백 */
    static inline void analog_delay_preset_long_echo(analog_delay_t* d)
    {
        analog_delay_set_bypass(d, 0);
        analog_delay_set_time_ms(d, 800.0f);
        analog_delay_set_feedback_pct(d, 68.0f);
        analog_delay_set_dry_wet_pct(d, 40.0f);
        analog_delay_set_diffusion(d, 1, 0.40f, 0.48f);
        analog_delay_set_lfo_depth_apf(d, 0.006f);
        analog_delay_set_slew_rate(d, ADLY_SLEW_SLOW);       /* 1초 수렴 */
        analog_delay_set_sat_thresh(d, 22000u);
        analog_delay_set_ms_width_q15(d, 0x5000u);
        analog_delay_set_damp_alpha_q15(d, 0x4000u);
        analog_delay_set_damp_hf_q15(d, 0x1000u);
    }

    /** 워밍 딜레이 — 국악 기반, 자연스러운 공간감 */
    static inline void analog_delay_preset_warm(analog_delay_t* d)
    {
        analog_delay_set_bypass(d, 0);
        analog_delay_set_time_ms(d, 420.0f);
        analog_delay_set_feedback_pct(d, 55.0f);
        analog_delay_set_dry_wet_pct(d, 42.0f);
        analog_delay_set_diffusion(d, 1, 0.55f, 0.52f);
        analog_delay_set_lfo_depth_apf(d, 0.010f);
        analog_delay_set_slew_rate(d, ADLY_SLEW_DRIFT);
        analog_delay_set_sat_thresh(d, 21000u);
        analog_delay_set_ms_width_q15(d, 0x3800u);
        analog_delay_set_damp_alpha_q15(d, 0x5000u);
        analog_delay_set_damp_hf_q15(d, 0x1800u);
    }

    /**
     * @brief 1초 딜레이 프리셋 — 명확하게 청취 가능한 1초 에코
     *
     * 딜레이 1000ms (48000샘플) = 하드웨어 최대 65535샘플의 73%.
     * 피드백 60%: 약 6~7번 에코 후 자연 감쇠.
     * 슬루=0x0100(SLOW): 딜레이 변화 시 1초에 걸쳐 부드럽게 이동.
     * dry_wet 35%: 원음 존재감 유지하면서 에코 뚜렷하게 청취.
     */
    static inline void analog_delay_preset_1sec(analog_delay_t* d)
    {
        analog_delay_set_bypass(d, 0);
        analog_delay_set_time_ms(d, 1000.0f);              /* Q=0xBB8000 / 48000samp */
        analog_delay_set_feedback_pct(d, 60.0f);
        analog_delay_set_dry_wet_pct(d, 35.0f);
        analog_delay_set_diffusion(d, 1, 0.35f, 0.45f);
        analog_delay_set_lfo_depth_apf(d, 0.005f);
        analog_delay_set_slew_rate(d, ADLY_SLEW_SLOW);    /* 수렴 1초 */
        analog_delay_set_sat_thresh(d, 24000u);
        analog_delay_set_ms_width_q15(d, 0x4800u);
        analog_delay_set_damp_alpha_q15(d, 0x3800u);
        analog_delay_set_damp_hf_q15(d, 0x0800u);
    }

    /**
     * @brief 최대 딜레이 프리셋 — 1365ms (BRAM 최대)
     *
     * 딜레이 1365ms (65520샘플) ≈ BRAM 한계.
     * 매우 긴 공간감, 강한 확산.
     * 슬루=SLOW: 1초 이상 수렴 시간 (긴 딜레이 변화 보호).
     */
    static inline void analog_delay_preset_max_range(analog_delay_t* d)
    {
        analog_delay_set_bypass(d, 0);
        analog_delay_set_time_ms(d, 1365.0f);              /* Q=0xFFF000 / 65520samp */
        analog_delay_set_feedback_pct(d, 55.0f);
        analog_delay_set_dry_wet_pct(d, 30.0f);
        analog_delay_set_diffusion(d, 1, 0.50f, 0.45f);
        analog_delay_set_lfo_depth_apf(d, 0.004f);
        analog_delay_set_slew_rate(d, ADLY_SLEW_SLOW);
        analog_delay_set_sat_thresh(d, 25000u);
        analog_delay_set_ms_width_q15(d, 0x5000u);
        analog_delay_set_damp_alpha_q15(d, 0x3000u);
        analog_delay_set_damp_hf_q15(d, 0x0600u);
    }

    /* =============================================================================
     * §25  상태 덤프 (디버그용)
     * =============================================================================*/
#ifdef ADLY_DEBUG
#include <stdio.h>
    static inline void analog_delay_dump(const analog_delay_t* d)
    {
        uint32_t dt = analog_delay_get_time_q168(d);
        uint16_t slew = (uint16_t)(adly_rd(d, ADLY_OFF_SLEW_RATE) & 0xFFFFu);
        /* 슬루 수렴시간 추정: Δ=1000ms 기준 */
        float conv_ms = (slew > 0u) ? (1000.0f * 256.0f / (float)slew) : 0.0f;

        printf("=== analog_delay_stereo_top Rev.9 상태 ===\n");
        printf("  delay_time : 0x%06X  (%.2f ms / %u samp)\n",
            dt, ADLY_Q168_TO_MS(dt), ADLY_Q168_TO_SAMP(dt));
        printf("  bypass     : %u\n", adly_rd(d, ADLY_OFF_BYPASS) & 1u);
        printf("  feedback   : 0x%04X (%.1f%%)\n",
            adly_rd(d, ADLY_OFF_FEEDBACK) & 0xFFFFu,
            ADLY_Q15_TO_PCT(adly_rd(d, ADLY_OFF_FEEDBACK)));
        printf("  dry_wet    : 0x%04X (%.1f%%)\n",
            adly_rd(d, ADLY_OFF_DRY_WET) & 0xFFFFu,
            ADLY_Q15_TO_PCT(adly_rd(d, ADLY_OFF_DRY_WET)));
        printf("  diff_start : %u  [0=APF ON, >=1=APF OFF]\n",
            adly_rd(d, ADLY_OFF_DIFFUSION_START));
        printf("  diff_mix   : 0x%04X (%.3f)\n",
            adly_rd(d, ADLY_OFF_DIFFUSION_MIX) & 0xFFFFu,
            ADLY_Q15_TO_FLOAT(adly_rd(d, ADLY_OFF_DIFFUSION_MIX)));
        printf("  diffusion_g: 0x%04X (g=%.3f) [bit15 무시, 유효[14:0]]\n",
            adly_rd(d, ADLY_OFF_DIFFUSION_G) & 0xFFFFu,
            (float)(adly_rd(d, ADLY_OFF_DIFFUSION_G) & 0x7FFFu) / 32767.0f);
        printf("  lfo_depth  : 0x%04X (%.4f)\n",
            adly_rd(d, ADLY_OFF_LFO_DEPTH_APF) & 0xFFFFu,
            ADLY_Q15_TO_FLOAT(adly_rd(d, ADLY_OFF_LFO_DEPTH_APF)));
        printf("  slew_rate  : 0x%04X  → Δ1000ms 수렴 %.0fms\n", slew, conv_ms);
        printf("  sat_thresh : %u [유효[14:0]=%u]\n",
            adly_rd(d, ADLY_OFF_SAT_THRESH) & 0xFFFFu,
            adly_rd(d, ADLY_OFF_SAT_THRESH) & 0x7FFFu);
        printf("  ms_width   : 0x%04X (%.1f%%)\n",
            adly_rd(d, ADLY_OFF_MS_WIDTH) & 0xFFFFu,
            ADLY_Q15_TO_PCT(adly_rd(d, ADLY_OFF_MS_WIDTH)));
        printf("  outhpf_byp : %u\n", adly_rd(d, ADLY_OFF_OUTHPF_BYPASS) & 1u);
        printf("  damp_alpha : 0x%04X (%.1f%%) [≤0x7FFF 필수]\n",
            adly_rd(d, ADLY_OFF_DAMP_ALPHA) & 0xFFFFu,
            ADLY_Q15_TO_PCT(adly_rd(d, ADLY_OFF_DAMP_ALPHA)));
        printf("  damp_hf    : 0x%04X (%.1f%%) [≤0x7FFF 필수]\n",
            adly_rd(d, ADLY_OFF_DAMP_HF) & 0xFFFFu,
            ADLY_Q15_TO_PCT(adly_rd(d, ADLY_OFF_DAMP_HF)));

        /* LFO 유효 채널 */
        printf("--- LFO 유효 채널 ---\n");
        for (int ch = 0; ch <= 5; ch += (ch == 0 ? 4 : 1)) {
            uint32_t fcw = lfo_get_fcw(d, (uint8_t)ch);
            uint16_t depth = lfo_get_depth(d, (uint8_t)ch);
            float hz = LFO_FCW_TO_HZ(fcw);
            printf("  CH%d: FCW=0x%08X (%.3fHz)  depth=0x%04X (%.3f)\n",
                ch, fcw, hz, depth, LFO_DEPTH_TO_FLOAT(depth));
        }
    }
#endif /* ADLY_DEBUG */

#ifdef __cplusplus
}
#endif
#endif /* ANALOG_DELAY_H */