/* =============================================================================
 *  eq_4band_presets.h  —  Rev.3
 *
 *  풀 파라메트릭 4밴드 EQ 프리셋 & 실시간 계수 계산 헬퍼
 *
 *  대상 HW : pre_eq_4band.v  v3.0  (COEF_W=24, SCALE=22, Fs=48000Hz)
 *  사용 코드: effect_bd_uio.c  →  eq_apply(preset, name)
 *
 *  Rev.3 변경사항 (vs Rev.2)
 *  ──────────────────────────────────────────────────────────────────
 *  [필터 타입 확장]
 *    · EQ_FTYPE_BPF    (밴드패스,  fc + Q,       파라미터 2개)
 *    · EQ_FTYPE_NOTCH  (노치,      fc + Q,       파라미터 2개)
 *    · EQ_FTYPE_TILT   (틸트/리라, fc + gain_dB, 파라미터 2개)
 *    · EQ_FTYPE_APF    (전역통과,  fc + Q,       파라미터 2개)
 *    · EQ_FTYPE_COUNT = 10  (기존 6 → 10)
 *
 *    RTL 관점: BPF/NOTCH/TILT/APF 모두 biquad 계수로 환산되어
 *    동일한 5계수(b0,b1,b2,a1,a2)로 기록 → HW 변경 없음.
 *
 *  [pot.c 호환]
 *    · EQ_FTYPE_PARAM_COUNT[] 확장 → Mode 4 EQ 편집기 자동 지원
 *    · eq_fc_range / eq_p2_name / eq_p2_range / eq_p2_default 확장
 *    · eq_calc_band() 디스패처에 신규 타입 추가
 *    · #ifndef EQ_GAIN_MIN_DB 블록 내 호환 정의도 동기화됨
 *      (pot.c 내부 보완 블록은 신규 헤더 include 후 스킵됨)
 *
 *  [프리셋 확장]
 *    · 48개 → 56개 (+8)
 *      추가: BPF_VOICE  NOTCH_60HZ  NOTCH_50HZ  BPF_INSTRUMENT
 *            TILT_WARM  TILT_BRIGHT  ANTI_FEEDBACK  HAEGEUM_POST
 *    · EQ_PRESET_COUNT = 56
 *
 *  [PRE EQ 전용 / POST EQ 전용 프리셋 범위 안내]
 *    공통 테이블(EQ_PRESET_DATA)을 PRE/POST 모두 공용으로 사용.
 *    단, 아래 범위를 참고하여 Mode 4 기본 인덱스를 설정 권장:
 *      PRE  EQ 권장: 0~30, 36~40, 43~47, 52~55   (악기/음색/보정)
 *      POST EQ 권장: 0, 31~35, 41~42, 48~51       (환경/공간/대역)
 *      모든 프리셋이 양쪽 HW에 적용 가능하며 선택은 자유.
 *
 *  Q포맷 : Q2.22  →  int32 = round(float_val × 2^22) & 0xFFFFFF
 *           Q22_ONE = 0x00400000 (= 1.0)
 *           표현 범위 -2.0 ~ +1.9999... (부호 있는 24비트 2의보수)
 *
 *  레지스터 맵 (pre_eq_4band.v wr_widx 디코딩 기반)
 *    wr_widx = addr[11:2]  (word index)
 *    band 0: wr_widx  0.. 4  → byte offset  0x00..0x10
 *    band 1: wr_widx  5.. 9  → byte offset  0x14..0x24
 *    band 2: wr_widx 10..14  → byte offset  0x28..0x38
 *    band 3: wr_widx 15..19  → byte offset  0x3C..0x4C
 *    ctrl  : wr_widx 64      → byte offset  0x100
 *      ctrl[0]=bypass  ctrl[1]=coef_bypass
 *    coef 순서: 0=b0  1=b1  2=b2  3=a1  4=a2
 *    EQ_REG_OFF(band, coef) = (band*5 + coef) * 4
 *
 *  포함 내용
 *  ─────────────────────────────────────────────────────────────────
 *  § 1.  BiquadCoef 구조체
 *  § 2.  EQ_REG_OFF 매크로 및 기본 상수
 *  § 3.  Q22 변환 헬퍼 (eq_float_to_q22, eq_write_band)
 *  § 4.  실시간 필터 계수 계산
 *          eq_calc_bell / low_shelf / high_shelf / hpf / lpf
 *          eq_calc_bpf  / notch / tilt / apf         ← Rev.3 신규
 *  § 5.  ADC 노브 → gain 변환
 *  § 5b. 필터 종류 enum + 파라미터 범위 / 디스패처  ← Rev.3 확장
 *  § 6.  정적 프리셋 테이블 (4밴드, 56개)
 *  § 7.  프리셋 인덱스 enum + 이름 테이블
 *  § 8.  YM2203 악기별 권장 프리셋 매핑
 *  § 9.  apply_eq_preset_idx() 인라인 헬퍼
 *  §10.  pot.c Mode 4 선형 시퀀스 지원 주석
 * =============================================================================
 */

#ifndef EQ_4BAND_PRESETS_H
#define EQ_4BAND_PRESETS_H

#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 1.  BiquadCoef 구조체
     *        BIQUAD_COEF_DEFINED 가드로 중복 정의 방지
     * ─────────────────────────────────────────────────────────────── */
#ifndef BIQUAD_COEF_DEFINED
#define BIQUAD_COEF_DEFINED
    typedef struct {
        uint32_t b0, b1, b2;   /* 피드포워드 계수 (Q2.22 unsigned 24bit) */
        uint32_t a1, a2;        /* 피드백 계수    (Q2.22 unsigned 24bit, 2의보수) */
    } BiquadCoef;
#endif


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 2.  레지스터 오프셋 매크로 & 기본 상수
     * ─────────────────────────────────────────────────────────────── */
#ifndef EQ_REG_OFF
#define EQ_REG_OFF(b, c)    (((uint32_t)((b)*5u + (c))) * 4u)
#endif

#define EQ_FS               48000.0     /* 샘플레이트 Hz */
#define EQ_COEF_W           24u
#define EQ_SCALE            22u
#define EQ_Q22_ONE          0x00400000u
#define EQ_Q22_MAX          0x007FFFFFu
#define EQ_Q22_MIN          0xFF800000u
#define EQ_BANDS            4u
#define EQ_REG_CTRL         0x100u      /* ctrl[0]=bypass  ctrl[1]=coef_bypass */


     /* ─────────────────────────────────────────────────────────────────────────────
      * § 3.  Q22 변환 헬퍼
      * ─────────────────────────────────────────────────────────────── */

    static inline uint32_t eq_float_to_q22(double v)
    {
        long long i = (long long)round(v * (double)(1u << EQ_SCALE));
        if (i > 0x7FFFFFL) i = 0x7FFFFFL;
        if (i < -0x800000L) i = -0x800000L;
        return (uint32_t)(i & 0xFFFFFFu);
    }

    /* 단일 밴드를 HW에 직접 기록 */
#define eq_write_band(base_ptr, band_idx, coef_ptr)                          \
    do {                                                                      \
        volatile uint8_t* _b = (volatile uint8_t*)(base_ptr);               \
        uint32_t _i = (uint32_t)(band_idx);                                  \
        const BiquadCoef* _c = (coef_ptr);                                   \
        *((volatile uint32_t*)(_b+EQ_REG_OFF(_i,0))) = (_c)->b0 & 0x00FFFFFFu; \
        *((volatile uint32_t*)(_b+EQ_REG_OFF(_i,1))) = (_c)->b1 & 0x00FFFFFFu; \
        *((volatile uint32_t*)(_b+EQ_REG_OFF(_i,2))) = (_c)->b2 & 0x00FFFFFFu; \
        *((volatile uint32_t*)(_b+EQ_REG_OFF(_i,3))) = (_c)->a1 & 0x00FFFFFFu; \
        *((volatile uint32_t*)(_b+EQ_REG_OFF(_i,4))) = (_c)->a2 & 0x00FFFFFFu; \
    } while(0)

/*
 *  eq_write_preset_atomic — coef_bypass 프로토콜로 4밴드 전체 원자적 기록
 *  시퀀스: ctrl[1]=1 → 20×shadow write → ctrl[1]=0
 */
#define eq_write_preset_atomic(base_ptr, preset_ptr)                         \
    do {                                                                      \
        volatile uint8_t* _b = (volatile uint8_t*)(base_ptr);               \
        const BiquadCoef* _p = (preset_ptr);                                 \
        *((volatile uint32_t*)(_b + EQ_REG_CTRL)) = 0x00000002u;            \
        for (int _band = 0; _band < 4; _band++) {                           \
            *((volatile uint32_t*)(_b+EQ_REG_OFF(_band,0))) = _p[_band].b0 & 0x00FFFFFFu; \
            *((volatile uint32_t*)(_b+EQ_REG_OFF(_band,1))) = _p[_band].b1 & 0x00FFFFFFu; \
            *((volatile uint32_t*)(_b+EQ_REG_OFF(_band,2))) = _p[_band].b2 & 0x00FFFFFFu; \
            *((volatile uint32_t*)(_b+EQ_REG_OFF(_band,3))) = _p[_band].a1 & 0x00FFFFFFu; \
            *((volatile uint32_t*)(_b+EQ_REG_OFF(_band,4))) = _p[_band].a2 & 0x00FFFFFFu; \
        }                                                                     \
        *((volatile uint32_t*)(_b + EQ_REG_CTRL)) = 0x00000000u;            \
    } while(0)

#define eq_set_bypass(base_ptr, on) \
    (*((volatile uint32_t*)((volatile uint8_t*)(base_ptr)+EQ_REG_CTRL)) = (on) ? 0x00000001u : 0x00000000u)


 /* ─────────────────────────────────────────────────────────────────────────────
  * § 4.  실시간 필터 계수 계산 함수  (모두 인라인, math.h 필요)
  *
  *  설계 기준: RBJ Audio EQ Cookbook (Robert Bristow-Johnson)
  *  Fs = 48000 Hz
  * ─────────────────────────────────────────────────────────────── */

  /* ── §4-1 Bell (피킹 EQ) ─────────────────────────────────────── */
    static inline BiquadCoef eq_calc_bell(double fc, double gain_db, double Q)
    {
        BiquadCoef r;
        if (fc <= 0.0 || fc >= EQ_FS * 0.5) {
            r.b0 = EQ_Q22_ONE; r.b1 = 0; r.b2 = 0; r.a1 = 0; r.a2 = 0; return r;
        }
        double A = pow(10.0, gain_db / 40.0);
        double w0 = 2.0 * M_PI * fc / EQ_FS;
        double alpha = sin(w0) / (2.0 * Q);
        double a0 = 1.0 + alpha / A;
        r.b0 = eq_float_to_q22((1.0 + alpha * A) / a0);
        r.b1 = eq_float_to_q22(-2.0 * cos(w0) / a0);
        r.b2 = eq_float_to_q22((1.0 - alpha * A) / a0);
        r.a1 = eq_float_to_q22(-2.0 * cos(w0) / a0);
        r.a2 = eq_float_to_q22((1.0 - alpha / A) / a0);
        return r;
    }

    /* ── §4-2 Low Shelf ─────────────────────────────────────────── */
    static inline BiquadCoef eq_calc_low_shelf(double fc, double gain_db, double S)
    {
        BiquadCoef r;
        double A = pow(10.0, gain_db / 40.0);
        double w0 = 2.0 * M_PI * fc / EQ_FS;
        double alpha = sin(w0) / 2.0 * sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        double sqA = sqrt(A);
        double a0 = (A + 1.0) + (A - 1.0) * cos(w0) + 2.0 * sqA * alpha;
        r.b0 = eq_float_to_q22(A * ((A + 1.0) - (A - 1.0) * cos(w0) + 2.0 * sqA * alpha) / a0);
        r.b1 = eq_float_to_q22(2.0 * A * ((A - 1.0) - (A + 1.0) * cos(w0)) / a0);
        r.b2 = eq_float_to_q22(A * ((A + 1.0) - (A - 1.0) * cos(w0) - 2.0 * sqA * alpha) / a0);
        r.a1 = eq_float_to_q22(-2.0 * ((A - 1.0) + (A + 1.0) * cos(w0)) / a0);
        r.a2 = eq_float_to_q22(((A + 1.0) + (A - 1.0) * cos(w0) - 2.0 * sqA * alpha) / a0);
        return r;
    }

    /* ── §4-3 High Shelf ────────────────────────────────────────── */
    static inline BiquadCoef eq_calc_high_shelf(double fc, double gain_db, double S)
    {
        BiquadCoef r;
        double A = pow(10.0, gain_db / 40.0);
        double w0 = 2.0 * M_PI * fc / EQ_FS;
        double alpha = sin(w0) / 2.0 * sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        double sqA = sqrt(A);
        double a0 = (A + 1.0) - (A - 1.0) * cos(w0) + 2.0 * sqA * alpha;
        r.b0 = eq_float_to_q22(A * ((A + 1.0) + (A - 1.0) * cos(w0) + 2.0 * sqA * alpha) / a0);
        r.b1 = eq_float_to_q22(-2.0 * A * ((A - 1.0) + (A + 1.0) * cos(w0)) / a0);
        r.b2 = eq_float_to_q22(A * ((A + 1.0) + (A - 1.0) * cos(w0) - 2.0 * sqA * alpha) / a0);
        r.a1 = eq_float_to_q22(2.0 * ((A - 1.0) - (A + 1.0) * cos(w0)) / a0);
        r.a2 = eq_float_to_q22(((A + 1.0) - (A - 1.0) * cos(w0) - 2.0 * sqA * alpha) / a0);
        return r;
    }

    /* ── §4-4 HPF (2차 Butterworth) ─────────────────────────────── */
    static inline BiquadCoef eq_calc_hpf(double fc, double Q)
    {
        BiquadCoef r;
        double w0 = 2.0 * M_PI * fc / EQ_FS;
        double alpha = sin(w0) / (2.0 * Q);
        double a0 = 1.0 + alpha;
        r.b0 = eq_float_to_q22((1.0 + cos(w0)) / 2.0 / a0);
        r.b1 = eq_float_to_q22(-(1.0 + cos(w0)) / a0);
        r.b2 = eq_float_to_q22((1.0 + cos(w0)) / 2.0 / a0);
        r.a1 = eq_float_to_q22(-2.0 * cos(w0) / a0);
        r.a2 = eq_float_to_q22((1.0 - alpha) / a0);
        return r;
    }

    /* ── §4-5 LPF (2차 Butterworth) ─────────────────────────────── */
    static inline BiquadCoef eq_calc_lpf(double fc, double Q)
    {
        BiquadCoef r;
        double w0 = 2.0 * M_PI * fc / EQ_FS;
        double alpha = sin(w0) / (2.0 * Q);
        double a0 = 1.0 + alpha;
        r.b0 = eq_float_to_q22((1.0 - cos(w0)) / 2.0 / a0);
        r.b1 = eq_float_to_q22((1.0 - cos(w0)) / a0);
        r.b2 = eq_float_to_q22((1.0 - cos(w0)) / 2.0 / a0);
        r.a1 = eq_float_to_q22(-2.0 * cos(w0) / a0);
        r.a2 = eq_float_to_q22((1.0 - alpha) / a0);
        return r;
    }

    /* ── §4-6 BPF (밴드패스, constant 0dB peak gain, RBJ) ────────
     *  fc  : 중심 주파수 [Hz]
     *  Q   : 대역폭 품질계수 (좁을수록 날카로운 피크)
     *  특성: fc ± fc/(2Q) 구간을 통과, 나머지 감쇄
     *  용도: 악기 대역 분리, 보컬 대역 추출, 효과음 처리
     * ─────────────────────────────────────────────────────────────── */
    static inline BiquadCoef eq_calc_bpf(double fc, double Q)
    {
        BiquadCoef r;
        double w0 = 2.0 * M_PI * fc / EQ_FS;
        double alpha = sin(w0) / (2.0 * Q);
        double a0 = 1.0 + alpha;
        r.b0 = eq_float_to_q22(alpha / a0);           /*  sin(w0)/(2Q) / a0 */
        r.b1 = eq_float_to_q22(0.0);               /*  0                  */
        r.b2 = eq_float_to_q22(-alpha / a0);          /* -sin(w0)/(2Q) / a0 */
        r.a1 = eq_float_to_q22(-2.0 * cos(w0) / a0);
        r.a2 = eq_float_to_q22((1.0 - alpha) / a0);
        return r;
    }

    /* ── §4-7 NOTCH (노치/대역제거, Band-Reject) ─────────────────
     *  fc  : 제거 중심 주파수 [Hz]
     *  Q   : 노치 폭 (높을수록 좁고 날카로운 노치)
     *  용도: 험 제거(50/60Hz), 공진 억제, 피드백 제거
     *  주의: Q≥10 이상 사용 시 고정점수 정밀도 한계 있음
     * ─────────────────────────────────────────────────────────────── */
    static inline BiquadCoef eq_calc_notch(double fc, double Q)
    {
        BiquadCoef r;
        double w0 = 2.0 * M_PI * fc / EQ_FS;
        double alpha = sin(w0) / (2.0 * Q);
        double a0 = 1.0 + alpha;
        r.b0 = eq_float_to_q22(1.0 / a0);
        r.b1 = eq_float_to_q22(-2.0 * cos(w0) / a0);
        r.b2 = eq_float_to_q22(1.0 / a0);
        r.a1 = eq_float_to_q22(-2.0 * cos(w0) / a0);
        r.a2 = eq_float_to_q22((1.0 - alpha) / a0);
        return r;
    }

    /* ── §4-8 TILT EQ (전체 스펙트럼 기울기) ────────────────────
     *  fc       : 기준 주파수 (중심점) [Hz]  권장: 500~2000
     *  gain_db  : 양수 → 저역 부스트/고역 컷  (Warm 방향)
     *             음수 → 저역 컷/고역 부스트  (Bright 방향)
     *  구현: Low Shelf +gain_db/2 + High Shelf -gain_db/2
     *        두 필터를 직렬 적용한 효과를 단일 biquad로 근사하지 않고
     *        eq_calc_tilt()가 반환하는 것은 Low Shelf 계수임.
     *        High Shelf 보완은 eq_calc_tilt_hi()를 별도 밴드에 할당.
     *  NOTE: TILT 프리셋은 band0 = Low Shelf, band1 = High Shelf 쌍으로 구성.
     * ─────────────────────────────────────────────────────────────── */
    static inline BiquadCoef eq_calc_tilt_lo(double fc, double gain_db, double S)
    {
        return eq_calc_low_shelf(fc, gain_db * 0.5, S);
    }
    static inline BiquadCoef eq_calc_tilt_hi(double fc, double gain_db, double S)
    {
        return eq_calc_high_shelf(fc, -gain_db * 0.5, S);
    }

    /* ── §4-9 APF (All-Pass, 위상 회전) ─────────────────────────
     *  fc  : 위상 천이 중심 주파수 [Hz]
     *  Q   : 위상 천이 기울기 (낮을수록 완만)
     *  특성: 진폭은 전 주파수에서 0dB, 위상만 fc 근방에서 회전
     *  용도: 위상 정렬, 페이저 효과, Linkwitz-Riley 필터 보완
     * ─────────────────────────────────────────────────────────────── */
    static inline BiquadCoef eq_calc_apf(double fc, double Q)
    {
        BiquadCoef r;
        double w0 = 2.0 * M_PI * fc / EQ_FS;
        double alpha = sin(w0) / (2.0 * Q);
        double a0 = 1.0 + alpha;
        r.b0 = eq_float_to_q22((1.0 - alpha) / a0);
        r.b1 = eq_float_to_q22(-2.0 * cos(w0) / a0);
        r.b2 = eq_float_to_q22(1.0);                /* b2 = 1 (정규화 전 = a0/a0) */
        r.a1 = eq_float_to_q22(-2.0 * cos(w0) / a0);
        r.a2 = eq_float_to_q22((1.0 - alpha) / a0);
        return r;
    }

    /* ── §4-10 바이패스 단일 밴드 ───────────────────────────────── */
    static inline BiquadCoef eq_calc_bypass(void)
    {
        BiquadCoef r = { EQ_Q22_ONE, 0, 0, 0, 0 };
        return r;
    }


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 5.  ADC 노브 → 게인 변환
     * ─────────────────────────────────────────────────────────────── */
    static inline double eq_adc_to_gain_db(uint16_t adc, uint16_t center, double range_db)
    {
        double norm = ((double)adc - (double)center) / (double)center;
        if (norm > 1.0) norm = 1.0;
        if (norm < -1.0) norm = -1.0;
        return norm * range_db;
    }
    static inline double eq_adc_to_gain_db_sym(uint16_t adc, double range_db)
    {
        return eq_adc_to_gain_db(adc, 2048u, range_db);
    }


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 5b.  필터 종류 enum + 파라미터 범위 / 디스패처
     *
     *  ┌──────────────┬──────┬────────────┬───────────────────────────────────────┐
     *  │ 타입          │ p0   │ p1         │ p2                                    │
     *  ├──────────────┼──────┼────────────┼───────────────────────────────────────┤
     *  │ BELL         │ fc   │ gain_dB    │ Q   (0.3~10)                          │
     *  │ LOW_SHELF    │ fc   │ gain_dB    │ S   (0.3~1.5)                         │
     *  │ HIGH_SHELF   │ fc   │ gain_dB    │ S   (0.3~1.5)                         │
     *  │ HPF          │ fc   │ ─          │ Q   (0.3~10)   gain_db 무시           │
     *  │ LPF          │ fc   │ ─          │ Q   (0.3~10)   gain_db 무시           │
     *  │ BPF    [Rev3]│ fc   │ ─          │ Q   (0.3~10)   gain_db 무시           │
     *  │ NOTCH  [Rev3]│ fc   │ ─          │ Q   (0.5~30)   gain_db 무시           │
     *  │ TILT   [Rev3]│ fc   │ gain_dB    │ S   (0.3~1.5)  lo+hi 쌍 사용 권장    │
     *  │ APF    [Rev3]│ fc   │ ─          │ Q   (0.3~10)   gain_db 무시           │
     *  │ BYPASS       │ ─    │ ─          │ ─                                     │
     *  └──────────────┴──────┴────────────┴───────────────────────────────────────┘
     *  PARAM_COUNT: 파라미터 수 (0=BYPASS, 2=HPF/LPF/BPF/NOTCH/APF, 3=BELL/SHELF/TILT)
     *
     *  pot.c Mode 4 EQ 편집기 (m4_update_eq_edit_improved)와의 연동:
     *    max_param = (np==0) ? 3 : np+1  (Type 행 포함)
     *    → BPF/NOTCH/APF는 Freq + Q만 편집, Gain 행 스킵됨
     * ─────────────────────────────────────────────────────────────── */
    typedef enum {
        EQ_FTYPE_BELL = 0,
        EQ_FTYPE_LOW_SHELF = 1,
        EQ_FTYPE_HIGH_SHELF = 2,
        EQ_FTYPE_HPF = 3,
        EQ_FTYPE_LPF = 4,
        EQ_FTYPE_BPF = 5,   /* Rev.3 신규: 밴드패스 */
        EQ_FTYPE_NOTCH = 6,   /* Rev.3 신규: 노치(대역제거) */
        EQ_FTYPE_TILT = 7,   /* Rev.3 신규: 틸트(기울기) */
        EQ_FTYPE_APF = 8,   /* Rev.3 신규: 전역통과(위상) */
        EQ_FTYPE_BYPASS = 9,
        EQ_FTYPE_COUNT = 10
    } eq_filter_type_t;

    static const char* const EQ_FTYPE_NAMES[EQ_FTYPE_COUNT] = {
        "BELL",     /* 0 */
        "LO-SHF",  /* 1 */
        "HI-SHF",  /* 2 */
        "HPF",      /* 3 */
        "LPF",      /* 4 */
        "BPF",      /* 5 */
        "NOTCH",    /* 6 */
        "TILT",     /* 7 */
        "APF",      /* 8 */
        "BYPASS",   /* 9 */
    };

    /*
     *  파라미터 개수: Mode 4 EQ 편집기가 편집 행을 몇 개 그릴지 결정
     *    3 → Freq / Gain / Q(or S)   (BELL, SHELF, TILT)
     *    2 → Freq / Q                 (HPF, LPF, BPF, NOTCH, APF)
     *    0 → 없음                     (BYPASS)
     */
    static const uint8_t EQ_FTYPE_PARAM_COUNT[EQ_FTYPE_COUNT] = {
        3,  /* BELL      */
        3,  /* LOW_SHELF */
        3,  /* HIGH_SHELF*/
        2,  /* HPF       */
        2,  /* LPF       */
        2,  /* BPF       */
        2,  /* NOTCH     */
        3,  /* TILT      */
        2,  /* APF       */
        0,  /* BYPASS    */
    };

    /* p0 fc 범위 */
#define EQ_FC_MIN_BELL        20.0
#define EQ_FC_MAX_BELL     20000.0
#define EQ_FC_MIN_LOSHELF     20.0
#define EQ_FC_MAX_LOSHELF   8000.0
#define EQ_FC_MIN_HISHELF   1000.0
#define EQ_FC_MAX_HISHELF  20000.0
#define EQ_FC_MIN_HPF         20.0
#define EQ_FC_MAX_HPF      18000.0
#define EQ_FC_MIN_LPF         20.0
#define EQ_FC_MAX_LPF      20000.0
#define EQ_FC_MIN_BPF         40.0   /* BPF: 너무 낮은 fc는 비실용적 */
#define EQ_FC_MAX_BPF      16000.0
#define EQ_FC_MIN_NOTCH       20.0
#define EQ_FC_MAX_NOTCH    18000.0
#define EQ_FC_MIN_TILT        100.0  /* TILT 기준 주파수 */
#define EQ_FC_MAX_TILT       5000.0
#define EQ_FC_MIN_APF         20.0
#define EQ_FC_MAX_APF      20000.0

/* p1 gain 범위 (BELL / SHELF / TILT 공용) */
#define EQ_GAIN_MIN_DB  (-18.0)
#define EQ_GAIN_MAX_DB  ( 18.0)

/* p2 Q/S 범위 */
#define EQ_Q_MIN           0.3
#define EQ_Q_MAX          10.0
#define EQ_Q_NOTCH_MAX    30.0      /* 노치는 고Q 허용 */
#define EQ_S_MIN           0.3
#define EQ_S_MAX           1.5
#define EQ_Q_DEFAULT_BELL  1.0
#define EQ_Q_DEFAULT_HPF   0.707
#define EQ_Q_DEFAULT_LPF   0.707
#define EQ_Q_DEFAULT_BPF   1.0
#define EQ_Q_DEFAULT_NOTCH 5.0     /* 기본 노치: 비교적 좁음 */
#define EQ_Q_DEFAULT_APF   0.707
#define EQ_S_DEFAULT       0.707

/* 밴드별 기본 fc */
#define EQ_FC_DEFAULT_BAND0   100.0
#define EQ_FC_DEFAULT_BAND1   500.0
#define EQ_FC_DEFAULT_BAND2  2000.0
#define EQ_FC_DEFAULT_BAND3  8000.0

    static inline void eq_fc_range(eq_filter_type_t t, double* lo, double* hi)
    {
        switch (t) {
        case EQ_FTYPE_LOW_SHELF:  *lo = EQ_FC_MIN_LOSHELF; *hi = EQ_FC_MAX_LOSHELF; break;
        case EQ_FTYPE_HIGH_SHELF: *lo = EQ_FC_MIN_HISHELF; *hi = EQ_FC_MAX_HISHELF; break;
        case EQ_FTYPE_HPF:        *lo = EQ_FC_MIN_HPF;     *hi = EQ_FC_MAX_HPF;     break;
        case EQ_FTYPE_LPF:        *lo = EQ_FC_MIN_LPF;     *hi = EQ_FC_MAX_LPF;     break;
        case EQ_FTYPE_BPF:        *lo = EQ_FC_MIN_BPF;     *hi = EQ_FC_MAX_BPF;     break;
        case EQ_FTYPE_NOTCH:      *lo = EQ_FC_MIN_NOTCH;   *hi = EQ_FC_MAX_NOTCH;   break;
        case EQ_FTYPE_TILT:       *lo = EQ_FC_MIN_TILT;    *hi = EQ_FC_MAX_TILT;    break;
        case EQ_FTYPE_APF:        *lo = EQ_FC_MIN_APF;     *hi = EQ_FC_MAX_APF;     break;
        case EQ_FTYPE_BELL:
        default:                  *lo = EQ_FC_MIN_BELL;    *hi = EQ_FC_MAX_BELL;    break;
        }
    }

    /*
     *  p2 이름: BPF/NOTCH/APF/HPF/LPF → "Q", SHELF/TILT → "S", BELL → "Q"
     */
    static inline const char* eq_p2_name(eq_filter_type_t t)
    {
        switch (t) {
        case EQ_FTYPE_LOW_SHELF:
        case EQ_FTYPE_HIGH_SHELF:
        case EQ_FTYPE_TILT:       return "S";
        default:                  return "Q";
        }
    }

    static inline void eq_p2_range(eq_filter_type_t t, double* lo, double* hi)
    {
        switch (t) {
        case EQ_FTYPE_LOW_SHELF:
        case EQ_FTYPE_HIGH_SHELF:
        case EQ_FTYPE_TILT:   *lo = EQ_S_MIN;  *hi = EQ_S_MAX;        break;
        case EQ_FTYPE_NOTCH:  *lo = EQ_Q_MIN;  *hi = EQ_Q_NOTCH_MAX;  break;
        default:              *lo = EQ_Q_MIN;  *hi = EQ_Q_MAX;         break;
        }
    }

    static inline double eq_p2_default(eq_filter_type_t t)
    {
        switch (t) {
        case EQ_FTYPE_LOW_SHELF:
        case EQ_FTYPE_HIGH_SHELF:
        case EQ_FTYPE_TILT:   return EQ_S_DEFAULT;
        case EQ_FTYPE_HPF:    return EQ_Q_DEFAULT_HPF;
        case EQ_FTYPE_LPF:    return EQ_Q_DEFAULT_LPF;
        case EQ_FTYPE_BPF:    return EQ_Q_DEFAULT_BPF;
        case EQ_FTYPE_NOTCH:  return EQ_Q_DEFAULT_NOTCH;
        case EQ_FTYPE_APF:    return EQ_Q_DEFAULT_APF;
        case EQ_FTYPE_BYPASS: return 0.0;
        default:              return EQ_Q_DEFAULT_BELL;
        }
    }

    /*
     *  eq_calc_band() — 필터 타입 디스패처
     *  TILT 타입: gain_db>0 → Warm(저역↑고역↓), gain_db<0 → Bright(저역↓고역↑)
     *  NOTE: TILT를 단일 band에 배치하면 Low Shelf만 적용됨.
     *        완전한 틸트 효과는 band0=TILT(fc,+g,S) + band1=HIGH_SHELF(fc,-g,S) 조합 권장.
     */
    static inline BiquadCoef eq_calc_band(eq_filter_type_t type,
        double fc, double gain_db, double p2)
    {
        switch (type) {
        case EQ_FTYPE_LOW_SHELF:  return eq_calc_low_shelf(fc, gain_db, p2);
        case EQ_FTYPE_HIGH_SHELF: return eq_calc_high_shelf(fc, gain_db, p2);
        case EQ_FTYPE_HPF:        return eq_calc_hpf(fc, p2);
        case EQ_FTYPE_LPF:        return eq_calc_lpf(fc, p2);
        case EQ_FTYPE_BPF:        return eq_calc_bpf(fc, p2);
        case EQ_FTYPE_NOTCH:      return eq_calc_notch(fc, p2);
        case EQ_FTYPE_TILT:       return eq_calc_tilt_lo(fc, gain_db, p2);
        case EQ_FTYPE_APF:        return eq_calc_apf(fc, p2);
        case EQ_FTYPE_BYPASS:     return eq_calc_bypass();
        default:                  return eq_calc_bell(fc, gain_db, p2);
        }
    }

    static inline void eq_clamp_params(eq_filter_type_t type,
        double* fc, double* gain_db, double* p2)
    {
        double fc_lo, fc_hi, p2_lo, p2_hi;
        eq_fc_range(type, &fc_lo, &fc_hi);
        eq_p2_range(type, &p2_lo, &p2_hi);
        if (*fc < fc_lo)         *fc = fc_lo;
        if (*fc > fc_hi)         *fc = fc_hi;
        if (*gain_db < EQ_GAIN_MIN_DB) *gain_db = EQ_GAIN_MIN_DB;
        if (*gain_db > EQ_GAIN_MAX_DB) *gain_db = EQ_GAIN_MAX_DB;
        if (*p2 < p2_lo)         *p2 = p2_lo;
        if (*p2 > p2_hi)         *p2 = p2_hi;
    }

    static inline void eq_build_parametric_4band(
        const double fc_hz[4], const double Q_val[4],
        const double gains_db[4], BiquadCoef out[4])
    {
        for (int i = 0; i < 4; i++)
            out[i] = eq_calc_bell(fc_hz[i], gains_db[i], Q_val[i]);
    }

#define EQ_LIVE_FC_0     100.0
#define EQ_LIVE_FC_1     500.0
#define EQ_LIVE_FC_2    2000.0
#define EQ_LIVE_FC_3    8000.0
#define EQ_LIVE_Q_0      0.707
#define EQ_LIVE_Q_1      1.414
#define EQ_LIVE_Q_2      1.414
#define EQ_LIVE_Q_3      0.707

    static inline void eq_get_live_fc(double out[4]) {
        out[0] = EQ_LIVE_FC_0; out[1] = EQ_LIVE_FC_1;
        out[2] = EQ_LIVE_FC_2; out[3] = EQ_LIVE_FC_3;
    }
    static inline void eq_get_live_q(double out[4]) {
        out[0] = EQ_LIVE_Q_0; out[1] = EQ_LIVE_Q_1;
        out[2] = EQ_LIVE_Q_2; out[3] = EQ_LIVE_Q_3;
    }


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 6.  정적 프리셋 테이블  (56개, Fs=48kHz Q2.22 RBJ Cookbook)
     *
     *  [0..30]  악기/장르/음색  (기존)
     *  [31..47] 환경/음향보정   (Rev.2 추가)
     *  [48..55] BPF/NOTCH/TILT  (Rev.3 추가)
     *
     *  표기: b0,b1,b2,a1,a2  모두 Q2.22 unsigned 24bit
     *         부호 있는 값은 2의보수 24bit (예: 0x800000 = -2.0)
     * ─────────────────────────────────────────────────────────────── */

     /* ═══════════════════════════════════════════════════════════════
      *  [0..30] 기존 프리셋 (악기/장르/음색)
      * ═══════════════════════════════════════════════════════════════ */

      /* (0) BYPASS — 전 밴드 1.0 pass-through */
    static const BiquadCoef EQ_BYPASS[4] = {
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
    };
    /* (1) DAEGEUM — 대금 */
    static const BiquadCoef EQ_DAEGEUM[4] = {
        {0x3ECDCDu,0x826466u,0x3ECDCDu,0x826BB0u,0x3DA2E3u},
        {0x402ADAu,0x816D0Bu,0x3E8A24u,0x816D0Bu,0x3EB4FFu},
        {0x3F4A64u,0x9325B0u,0x33A9A9u,0x9325B0u,0x32F40Eu},
        {0x372C3Fu,0xE342F7u,0x07E7DCu,0xD75FEDu,0x0AF725u},
    };
    /* (2) PIRI — 피리 */
    static const BiquadCoef EQ_PIRI[4] = {
        {0x3EEE9Cu,0x8222C8u,0x3EEE9Cu,0x822740u,0x3DE1B1u},
        {0x405814u,0x820EB2u,0x3DFCEDu,0x820EB2u,0x3E5500u},
        {0x414D4Du,0x9308D8u,0x34A432u,0x9308D8u,0x35F180u},
        {0x348A2Bu,0xF5DB0Fu,0x04E517u,0xE7EA92u,0x075FBFu},
    };
    /* (3) HAEGEUM — 해금 */
    static const BiquadCoef EQ_HAEGEUM[4] = {
        {0x3EF580u,0x821500u,0x3EF580u,0x821A86u,0x3DF087u},
        {0x403CEFu,0x821BCFu,0x3DEC69u,0x821BCFu,0x3E2958u},
        {0x4057FFu,0x87FC48u,0x3A05A3u,0x87FC48u,0x3A5DA2u},
        {0x416BE0u,0xB5F6EAu,0x2747D6u,0xB5F6EAu,0x28B3B6u},
    };
    /* (4) AJAENG — 아쟁 */
    static const BiquadCoef EQ_AJAENG[4] = {
        {0x401F2Eu,0x814B17u,0x3E9908u,0x814A9Cu,0x3EB7BBu},
        {0x40499Du,0x817DDEu,0x3E5182u,0x817DDEu,0x3E9B1Fu},
        {0x3FB920u,0x869AD4u,0x3B2F6Du,0x869AD4u,0x3AE88Du},
        {0x35B2B5u,0xCA1791u,0x10381Au,0xBA0FB2u,0x15F2ADu},
    };
    /* (5) GAYAGEUM — 가야금 */
    static const BiquadCoef EQ_GAYAGEUM[4] = {
        {0x4014BEu,0x815426u,0x3E9A3Bu,0x8153D4u,0x3EAEA6u},
        {0x401864u,0x81BC2Du,0x3E57CBu,0x81BC2Du,0x3E702Fu},
        {0x40E74Eu,0x8B1A4Eu,0x381E0Fu,0x8B1A4Eu,0x39055Cu},
        {0x453B66u,0xCB9907u,0x0E6719u,0xD2FEA6u,0x0C3CE1u},
    };
    /* (6) GEOMUNGO — 거문고 */
    static const BiquadCoef EQ_GEOMUNGO[4] = {
        {0x40215Fu,0x810200u,0x3EDEE0u,0x810196u,0x3EFFD5u},
        {0x4020BCu,0x810801u,0x3EE267u,0x810801u,0x3F0323u},
        {0x3F88FBu,0x8593CDu,0x3BF1A7u,0x8593CDu,0x3B7AA2u},
        {0x35B2B5u,0xCA1791u,0x10381Au,0xBA0FB2u,0x15F2ADu},
    };
    /* (7) VIOLIN — 바이올린 */
    static const BiquadCoef EQ_VIOLIN[4] = {
        {0x3EF580u,0x821500u,0x3EF580u,0x821A86u,0x3DF087u},
        {0x4040D8u,0x817030u,0x3E84C8u,0x817030u,0x3EC5A1u},
        {0x40E74Eu,0x8B1A4Eu,0x381E0Fu,0x8B1A4Eu,0x39055Cu},
        {0x4A3D4Cu,0xD0E001u,0x0CB843u,0xDEA9E7u,0x092BA9u},
    };
    /* (8) CELLO — 첼로 */
    static const BiquadCoef EQ_CELLO[4] = {
        {0x40109Au,0x81103Cu,0x3EE12Au,0x811007u,0x3EF190u},
        {0x4030E9u,0x8192C1u,0x3E554Fu,0x8192C1u,0x3E8638u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x3A5622u,0xB9FFF1u,0x172FF5u,0xB0C162u,0x1AC4A5u},
    };
    /* (9) PIANO — 피아노 */
    static const BiquadCoef EQ_PIANO[4] = {
        {0x3FA459u,0x80B74Eu,0x3FA459u,0x80B7CEu,0x3F4933u},
        {0x400C46u,0x80D45Du,0x3F2A86u,0x80D45Du,0x3F36CCu},
        {0x40A59Bu,0x938A1Bu,0x34BFFBu,0x938A1Bu,0x356596u},
        {0x44EE33u,0xD62A9Au,0x0B5270u,0xDCCE06u,0x099D36u},
    };
    /* (10) PIANO BRIGHT — 피아노 밝음 */
    static const BiquadCoef EQ_PIANO_BRIGHT[4] = {
        {0x3FA459u,0x80B74Eu,0x3FA459u,0x80B7CEu,0x3F4933u},
        {0x3FF3BCu,0x80ECBCu,0x3F2AAFu,0x80ECBCu,0x3F1E6Au},
        {0x414D4Du,0x9308D8u,0x34A432u,0x9308D8u,0x35F180u},
        {0x4DF5C7u,0xE2A9BAu,0x08FB83u,0xF3A499u,0x05F66Cu},
    };
    /* (11) PIANO WARM — 피아노 따뜻 */
    static const BiquadCoef EQ_PIANO_WARM[4] = {
        {0x402960u,0x82A6DAu,0x3D3C1Eu,0x82A595u,0x3D6439u},
        {0x4040F3u,0x8221E7u,0x3DC95Fu,0x8221E7u,0x3E0A51u},
        {0x3EB958u,0x953501u,0x34DE2Cu,0x953501u,0x339784u},
        {0x333AACu,0xE6D22Cu,0x06FD37u,0xD59E65u,0x0B6BABu},
    };
    /* (12) GUITAR ACOUSTIC — 어쿠스틱 기타 */
    static const BiquadCoef EQ_GUITAR_ACOUSTIC[4] = {
        {0x3F85EAu,0x80F42Cu,0x3F85EAu,0x80F510u,0x3F0CB8u},
        {0x400C46u,0x80D45Du,0x3F2A86u,0x80D45Du,0x3F36CCu},
        {0x411B87u,0x8EE4CAu,0x365674u,0x8EE4CAu,0x3771FBu},
        {0x4A3D4Cu,0xD0E001u,0x0CB843u,0xDEA9E7u,0x092BA9u},
    };
    /* (13) GUITAR CLEAN — 기타 클린 */
    static const BiquadCoef EQ_GUITAR_CLEAN[4] = {
        {0x3F678Au,0x8130EDu,0x3F678Au,0x813251u,0x3ED077u},
        {0x401864u,0x81BC2Du,0x3E57CBu,0x81BC2Du,0x3E702Fu},
        {0x407317u,0x8B7AADu,0x382E81u,0x8B7AADu,0x38A197u},
        {0x458B6Fu,0xC09E84u,0x12563Fu,0xC8D811u,0x0FA820u},
    };
    /* (14) GUITAR DRIVE — 기타 드라이브 */
    static const BiquadCoef EQ_GUITAR_DRIVE[4] = {
        {0x3F4938u,0x816D90u,0x3F4938u,0x816F8Fu,0x3E9470u},
        {0x4024CAu,0x813533u,0x3EBF0Fu,0x813533u,0x3EE3D8u},
        {0x40B0B9u,0x87B071u,0x39FA3Cu,0x87B071u,0x3AAAF5u},
        {0x3A9F57u,0xC3CC35u,0x1251BCu,0xBB6E3Eu,0x154F0Au},
    };
    /* (15) BASS — 베이스 */
    static const BiquadCoef EQ_BASS[4] = {
        {0x4018F5u,0x8108F4u,0x3EE035u,0x8108A5u,0x3EF8DBu},
        {0x4020BCu,0x810801u,0x3EE267u,0x810801u,0x3F0323u},
        {0x3F88FBu,0x8593CDu,0x3BF1A7u,0x8593CDu,0x3B7AA2u},
        {0x35B2B5u,0xCA1791u,0x10381Au,0xBA0FB2u,0x15F2ADu},
    };
    /* (16) DRUMS — 드럼 */
    static const BiquadCoef EQ_DRUMS[4] = {
        {0x400C75u,0x80CC43u,0x3F2868u,0x80CC25u,0x3F34C0u},
        {0x402503u,0x80BE9Cu,0x3F278Bu,0x80BE9Cu,0x3F4C8Fu},
        {0x41FAB0u,0xA69469u,0x2EBB88u,0xA69469u,0x30B638u},
        {0x48FFAAu,0xE6B7AAu,0x081E32u,0xF1A8BAu,0x062CCBu},
    };
    /* (17) VOCAL MALE — 보컬 남성 */
    static const BiquadCoef EQ_VOCAL_MALE[4] = {
        {0x3F678Au,0x8130EDu,0x3F678Au,0x813251u,0x3ED077u},
        {0x401616u,0x810367u,0x3EFF98u,0x810367u,0x3F15AEu},
        {0x40E74Eu,0x8B1A4Eu,0x381E0Fu,0x8B1A4Eu,0x39055Cu},
        {0x44EE33u,0xD62A9Au,0x0B5270u,0xDCCE06u,0x099D36u},
    };
    /* (18) VOCAL FEMALE — 보컬 여성 */
    static const BiquadCoef EQ_VOCAL_FEMALE[4] = {
        {0x3F4938u,0x816D90u,0x3F4938u,0x816F8Fu,0x3E9470u},
        {0x401863u,0x81D504u,0x3E57E7u,0x81D504u,0x3E7049u},
        {0x414D4Du,0x9308D8u,0x34A432u,0x9308D8u,0x35F180u},
        {0x48FFAAu,0xE6B7AAu,0x081E32u,0xF1A8BAu,0x062CCBu},
    };
    /* (19) GUGAK — 국악 일반 */
    static const BiquadCoef EQ_GUGAK[4] = {
        {0x3F37D4u,0x819058u,0x3F37D4u,0x819377u,0x3E72C7u},
        {0x407FA3u,0x82F1A7u,0x3D1594u,0x82F1A7u,0x3D9537u},
        {0x407317u,0x8B7AADu,0x382E81u,0x8B7AADu,0x38A197u},
        {0x3B29D3u,0xD66558u,0x0B501Eu,0xCF8ED4u,0x0D5074u},
    };
    /* (20) GUGAK WARM — 국악 따뜻 */
    static const BiquadCoef EQ_GUGAK_WARM[4] = {
        {0x402960u,0x82A6DAu,0x3D3C1Eu,0x82A595u,0x3D6439u},
        {0x4079C0u,0x829324u,0x3D3801u,0x829324u,0x3DB1C1u},
        {0x3F8DB7u,0x8C4ABFu,0x383C77u,0x8C4ABFu,0x37CA2Eu},
        {0x372C3Fu,0xE342F7u,0x07E7DCu,0xD75FEDu,0x0AF725u},
    };
    /* (21) GUGAK BRIGHT — 국악 밝음 */
    static const BiquadCoef EQ_GUGAK_BRIGHT[4] = {
        {0x3F37D4u,0x819058u,0x3F37D4u,0x819377u,0x3E72C7u},
        {0x3FDBBCu,0x82FDECu,0x3D894Du,0x82FDECu,0x3D6509u},
        {0x41AC10u,0x8E7821u,0x3638AAu,0x8E7821u,0x37E4BBu},
        {0x4FF454u,0xCB0DB8u,0x0E4481u,0xE08B4Fu,0x08BB3Fu},
    };
    /* (22) ORCH — 오케스트라 */
    static const BiquadCoef EQ_ORCH[4] = {
        {0x3FA459u,0x80B74Eu,0x3FA459u,0x80B7CEu,0x3F4933u},
        {0x401866u,0x81A8D9u,0x3E57B6u,0x81A8D9u,0x3E701Bu},
        {0x40AD09u,0x8B49DFu,0x382714u,0x8B49DFu,0x38D41Du},
        {0x4A3D4Cu,0xD0E001u,0x0CB843u,0xDEA9E7u,0x092BA9u},
    };
    /* (23) ELECTRONIC — 전자음악 */
    static const BiquadCoef EQ_ELECTRONIC[4] = {
        {0x4012BAu,0x80C6C7u,0x3F27B1u,0x80C69Au,0x3F3A3Eu},
        {0x3FA003u,0x8453CEu,0x3CBA93u,0x8453CEu,0x3C5A96u},
        {0x428339u,0x9BB2ADu,0x314E65u,0x9BB2ADu,0x33D19Fu},
        {0x4C106Cu,0xF8E09Cu,0x06A114u,0x05FE32u,0x0593EAu},
    };
    /* (24) AMBIENT — 앰비언트 */
    static const BiquadCoef EQ_AMBIENT[4] = {
        {0x4014BEu,0x815426u,0x3E9A3Bu,0x8153D4u,0x3EAEA6u},
        {0x402851u,0x82D98Fu,0x3D42DFu,0x82D98Fu,0x3D6B30u},
        {0x3F8DB7u,0x8C4ABFu,0x383C77u,0x8C4ABFu,0x37CA2Eu},
        {0x372C3Fu,0xE342F7u,0x07E7DCu,0xD75FEDu,0x0AF725u},
    };
    /* (25) TELEPHONE — 전화 음색 (300~3400Hz 대역통과) */
    static const BiquadCoef EQ_TELEPHONE[4] = {
        {0x3DA6C5u,0x84B276u,0x3DA6C5u,0x84C81Bu,0x3B632Fu},
        {0x40B49Eu,0x847C7Bu,0x3BDFBFu,0x847C7Bu,0x3C945Du},
        {0x026256u,0x04C4ADu,0x026256u,0xA7A3EFu,0x21E56Au},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
    };
    /* (26) RADIO — 라디오 */
    static const BiquadCoef EQ_RADIO[4] = {
        {0x3E3AF0u,0x838A20u,0x3E3AF0u,0x83966Au,0x3C8229u},
        {0x408EE3u,0x85D585u,0x3B2170u,0x85D585u,0x3BB052u},
        {0x0328B3u,0x065166u,0x0328B3u,0xAE51F4u,0x1E50D8u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
    };
    /* (27) SUBBASS — 서브베이스 */
    static const BiquadCoef EQ_SUBBASS[4] = {
        {0x401F8Eu,0x80991Du,0x3F4852u,0x8098DEu,0x3F67A1u},
        {0x401642u,0x806FF0u,0x3F7DD5u,0x806FF0u,0x3F9417u},
        {0x3FE1ABu,0x8272D5u,0x3DF077u,0x8272D5u,0x3DD222u},
        {0x3B6C15u,0xDF527Eu,0x08ED2Bu,0xD928A0u,0x0A831Eu},
    };
    /* (28) AIRY — 에어 */
    static const BiquadCoef EQ_AIRY[4] = {
        {0x3F85EAu,0x80F42Cu,0x3F85EAu,0x80F510u,0x3F0CB8u},
        {0x3FEDABu,0x816A2Cu,0x3EC12Au,0x816A2Cu,0x3EAED5u},
        {0x40A59Bu,0x938A1Bu,0x34BFFBu,0x938A1Bu,0x356596u},
        {0x50923Cu,0xF5F44Fu,0x0711CFu,0x07FAC5u,0x059D95u},
    };
    /* (29) MIDBOOST — 중역 강조 */
    static const BiquadCoef EQ_MIDBOOST[4] = {
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x4050DBu,0x82B560u,0x3D3E97u,0x82B560u,0x3D8F72u},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
    };
    /* (30) V-SHAPED — V자형 */
    static const BiquadCoef EQ_VSHAPED[4] = {
        {0x402566u,0x818D30u,0x3E5229u,0x818C7Eu,0x3E76DEu},
        {0x3FAF8Au,0x8352E4u,0x3D420Eu,0x8352E4u,0x3CF198u},
        {0x3F6ACFu,0x8EB6D1u,0x35DD6Du,0x8EB6D1u,0x35483Cu},
        {0x4A3D4Cu,0xD0E001u,0x0CB843u,0xDEA9E7u,0x092BA9u},
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [31..47] Rev.2 추가 프리셋 (환경 시뮬레이션 / 음향 보정)
     * ═══════════════════════════════════════════════════════════════ */

     /* (31) UNDERWATER — 물 속
      *  band0: LoShelf 200Hz +4dB   band1: Bell 400Hz +2dB Q0.8
      *  band2: LPF 800Hz Q0.6       band3: LPF 500Hz Q0.5 */
    static const BiquadCoef EQ_UNDERWATER[4] = {
        {0x405339u,0x828444u,0x3D3660u,0x8281B5u,0x3D870Au},
        {0x40782Bu,0x83CBDBu,0x3BE79Du,0x83CBDBu,0x3C5FC8u},
        {0x002948u,0x005290u,0x002948u,0x8AE6BDu,0x35BE63u},
        {0x001077u,0x0020EDu,0x001077u,0x881D6Bu,0x38246Fu},
    };
    /* (32) CAVE — 동굴
     *  band0: LoShelf 120Hz +5dB   band1: Bell 250Hz +4dB Q1.5
     *  band2: Bell 2000Hz -3dB Q0.8 band3: HiShelf 8kHz -6dB */
    static const BiquadCoef EQ_CAVE[4] = {
        {0x403EC1u,0x8178FCu,0x3E4D97u,0x8177D2u,0x3E8B2Du},
        {0x40524Eu,0x812AD5u,0x3E9441u,0x812AD5u,0x3EE690u},
        {0x3CFC67u,0x984C6Eu,0x2E5FAAu,0x984C6Eu,0x2B5C11u},
        {0x2906CEu,0xEF85DAu,0x04C609u,0xD0881Fu,0x0CCA92u},
    };
    /* (33) MEGAPHONE — 메가폰/혼
     *  band0: HPF 500Hz Q1.2       band1: Bell 1200Hz +6dB Q1.5
     *  band2: Bell 3000Hz +4dB Q1.2 band3: LPF 4000Hz Q0.8 */
    static const BiquadCoef EQ_MEGAPHONE[4] = {
        {0x3E3C49u,0x83876Eu,0x3E3C49u,0x83A994u,0x3C9AB8u},
        {0x424488u,0x8613A7u,0x392CE1u,0x8613A7u,0x3B7169u},
        {0x44354Bu,0x9709A1u,0x2D66FDu,0x9709A1u,0x319C47u},
        {0x034435u,0x068869u,0x034435u,0xAB8ABAu,0x218618u},
    };
    /* (34) BATHROOM — 욕실/타일
     *  band0: HPF 80Hz Q0.707      band1: Bell 500Hz +5dB Q2.5
     *  band2: Bell 1500Hz +3dB Q2.0 band3: HiShelf 6kHz -2dB */
    static const BiquadCoef EQ_BATHROOM[4] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x407BDDu,0x8183C7u,0x3E45D6u,0x8183C7u,0x3EC1B3u},
        {0x410A70u,0x876881u,0x39E9DFu,0x876881u,0x3AF44Eu},
        {0x3633F3u,0xD2ABFCu,0x0CBCEAu,0xC4231Du,0x1179BCu},
    };
    /* (35) CONCERT HALL — 콘서트홀
     *  band0: LoShelf 80Hz -2dB    band1: Bell 300Hz -1.5dB Q0.8
     *  band2: Bell 3000Hz +2dB Q1.2 band3: HiShelf 10kHz +3dB */
    static const BiquadCoef EQ_CONCERT_HALL[4] = {
        {0x3FEF6Au,0x8130ECu,0x3EE140u,0x813121u,0x3ED0DFu},
        {0x3FBC4Cu,0x836E52u,0x3CEDFCu,0x836E52u,0x3CAA49u},
        {0x420FDBu,0x98753Au,0x2E02DFu,0x98753Au,0x3012BAu},
        {0x4DF5C7u,0xE2A9BAu,0x08FB83u,0xF3A499u,0x05F66Cu},
    };
    /* (36) STUDIO — 스튜디오 모니터
     *  band0: HPF 30Hz             band1: Bell 200Hz -1.5dB Q1.4
     *  band2: Bell 3000Hz +1dB Q1.4 band3: HiShelf 12kHz +1.5dB */
    static const BiquadCoef EQ_STUDIO[4] = {
        {0x3FD290u,0x805AE1u,0x3FD290u,0x805B01u,0x3FA540u},
        {0x3FE5C8u,0x8155B8u,0x3ECF9Du,0x8155B8u,0x3EB566u},
        {0x40E477u,0x97420Cu,0x307ABFu,0x97420Cu,0x315F37u},
        {0x45C595u,0xFCBA9Bu,0x060A7Au,0x03001Fu,0x058A8Bu},
    };
    /* (37) LOUDNESS — 라우드니스 (Fletcher-Munson 보정)
     *  band0: LoShelf 60Hz +6dB    band1: Bell 200Hz +2dB Q0.8
     *  band2: Bell 3500Hz +1.5dB   band3: HiShelf 12kHz +4dB */
    static const BiquadCoef EQ_LOUDNESS[4] = {
        {0x4025DCu,0x80B7BEu,0x3F23D2u,0x80B763u,0x3F4953u},
        {0x403CF8u,0x81E201u,0x3DEC18u,0x81E201u,0x3E2910u},
        {0x4208D6u,0xA08F6Eu,0x28611Fu,0xA08F6Eu,0x2A69F5u},
        {0x50923Cu,0xF5F44Fu,0x0711CFu,0x07FAC5u,0x059D95u},
    };
    /* (38) DE-ESS — 탈치찰음
     *  band0: HPF 80Hz             band1: Bell 700Hz +1.5dB Q1.4
     *  band2: Bell 6000Hz -7dB Q2.5 band3: HiShelf 12kHz -2dB */
    static const BiquadCoef EQ_DEESS[4] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x4059E2u,0x843F20u,0x3BEC75u,0x843F20u,0x3C4657u},
        {0x39D0C0u,0xB54C21u,0x2FD47Cu,0xB54C21u,0x29A53Du},
        {0x390A41u,0x039078u,0x04F273u,0xFC0032u,0x058CFAu},
    };
    /* (39) WARMTH — 따뜻함
     *  band0: LoShelf 150Hz +4dB   band1: Bell 400Hz +2dB Q1.0
     *  band2: Bell 3000Hz -2dB Q1.0 band3: HiShelf 8kHz -3dB */
    static const BiquadCoef EQ_WARMTH[4] = {
        {0x403E7Bu,0x81E36Eu,0x3DE5EDu,0x81E1FCu,0x3E22F6u},
        {0x4060AFu,0x8316B1u,0x3CB483u,0x8316B1u,0x3D1532u},
        {0x3DAC6Cu,0x9EA502u,0x2BB408u,0x9EA502u,0x296074u},
        {0x333AACu,0xE6D22Cu,0x06FD37u,0xD59E65u,0x0B6BABu},
    };
    /* (40) PRESENCE — 존재감
     *  band0: HPF 60Hz             band1: Bell 300Hz -1.5dB Q1.0
     *  band2: Bell 2500Hz +5dB Q1.5 band3: HiShelf 10kHz +2dB */
    static const BiquadCoef EQ_PRESENCE[4] = {
        {0x3FA540u,0x80B581u,0x3FA540u,0x80B601u,0x3F4B00u},
        {0x3FC98Eu,0x82C749u,0x3D87E5u,0x82C749u,0x3D5173u},
        {0x43B45Au,0x8FCEB5u,0x32C69Au,0x8FCEB5u,0x367AF4u},
        {0x48FFAAu,0xE6B7AAu,0x081E32u,0xF1A8BAu,0x062CCBu},
    };
    /* (41) SCOOPED MIDS — 스쿱드 미드 (메탈)
     *  band0: LoShelf 100Hz +4dB   band1: Bell 400Hz -5dB Q1.0
     *  band2: Bell 1200Hz -4dB Q0.8 band3: HiShelf 8kHz +4dB */
    static const BiquadCoef EQ_SCOOPED_MIDS[4] = {
        {0x4029B2u,0x814270u,0x3E975Du,0x8141CBu,0x3EC06Au},
        {0x3F0E37u,0x847C4Bu,0x3CA0E2u,0x847C4Bu,0x3BAF19u},
        {0x3D6955u,0x8F6E7Eu,0x348F64u,0x8F6E7Eu,0x31F8B9u},
        {0x561ACDu,0xC4A869u,0x0FFAAEu,0xE271C4u,0x084C1Fu},
    };
    /* (42) NATURAL — 자연음
     *  band0: HPF 40Hz             band1: LoShelf 200Hz -2dB
     *  band2: Bell 1000Hz +1dB Q2.0 band3: HiShelf 6kHz +2.5dB */
    static const BiquadCoef EQ_NATURAL[4] = {
        {0x3FC371u,0x80791Du,0x3FC371u,0x807957u,0x3F871Cu},
        {0x3FD6BBu,0x82F66Bu,0x3D3CA2u,0x82F7AFu,0x3D14A1u},
        {0x403BBFu,0x84E33Fu,0x3BF0F7u,0x84E33Fu,0x3C2CB6u},
        {0x4EC564u,0xB5645Bu,0x15E024u,0xCB4EE0u,0x0EBB03u},
    };
    /* (43) VINTAGE — 빈티지 (테이프)
     *  band0: LoShelf 120Hz +3dB   band1: Bell 300Hz +2dB Q1.5
     *  band2: Bell 5000Hz -3dB Q1.0 band3: HiShelf 10kHz -5dB */
    static const BiquadCoef EQ_VINTAGE[4] = {
        {0x402566u,0x818D30u,0x3E5229u,0x818C7Eu,0x3E76DEu},
        {0x4030E9u,0x8192C1u,0x3E554Fu,0x8192C1u,0x3E8638u},
        {0x3B08D7u,0xB56D84u,0x22F631u,0xB56D84u,0x1DFF07u},
        {0x2E11D2u,0xF9F934u,0x040203u,0xE42422u,0x07E8E6u},
    };
    /* (44) CRISPY — 크리스피 (어택 극대화)
     *  band0: HPF 100Hz            band1: Bell 500Hz -2dB Q1.0
     *  band2: Bell 4000Hz +4dB Q1.5 band3: HiShelf 12kHz +6dB */
    static const BiquadCoef EQ_CRISPY[4] = {
        {0x3F6907u,0x812DF2u,0x3F6907u,0x812F56u,0x3ED372u},
        {0x3F88BCu,0x84CB70u,0x3BEF81u,0x84CB70u,0x3B783Du},
        {0x446057u,0x9E1BC2u,0x2CA8BDu,0x9E1BC2u,0x310914u},
        {0x5A6704u,0xEF28C5u,0x08153Eu,0x0BEC1Eu,0x05B8E9u},
    };
    /* (45) DEEP BASS — 딥 베이스
     *  band0: LoShelf 80Hz +8dB    band1: Bell 150Hz +5dB Q1.5
     *  band2: Bell 1000Hz -3dB Q0.8 band3: HiShelf 8kHz -4dB */
    static const BiquadCoef EQ_DEEP_BASS[4] = {
        {0x404408u,0x80E926u,0x3ED5A6u,0x80E84Cu,0x3F18D4u},
        {0x403E46u,0x80A651u,0x3F21B2u,0x80A651u,0x3F5FF8u},
        {0x3E5911u,0x8C4FD2u,0x3656ABu,0x8C4FD2u,0x34AFBCu},
        {0x2F91EBu,0xEA0829u,0x062AD2u,0xD3E460u,0x0BE086u},
    };
    /* (46) BROADCAST — 방송용 (EBU R68 참고)
     *  band0: HPF 60Hz             band1: Bell 200Hz -2dB Q1.0
     *  band2: Bell 3500Hz +2dB Q1.2 band3: HiShelf 10kHz +1dB */
    static const BiquadCoef EQ_BROADCAST[4] = {
        {0x3FA540u,0x80B581u,0x3FA540u,0x80B601u,0x3F4B00u},
        {0x3FCF3Bu,0x81E551u,0x3E5685u,0x81E551u,0x3E25C0u},
        {0x425679u,0x9D6549u,0x2B9AC9u,0x9D6549u,0x2DF142u},
        {0x445A0Cu,0xEA64CDu,0x0755E6u,0xEFAF1Du,0x0665A1u},
    };
    /* (47) HAEGEUM SOLO — 해금 솔로 강화 (국악 특화)
     *  band0: HPF 120Hz Q0.9       band1: Bell 600Hz +4dB Q2.0
     *  band2: Bell 2000Hz +5dB Q1.8 band3: HiShelf 8kHz +3dB */
    static const BiquadCoef EQ_HAEGEUM_SOLO[4] = {
        {0x3F7143u,0x811D79u,0x3F7143u,0x811F7Au,0x3EE488u},
        {0x409304u,0x825A2Du,0x3D7646u,0x825A2Du,0x3E094Au},
        {0x428C4Cu,0x8AAFADu,0x36E774u,0x8AAFADu,0x3973C0u},
        {0x4FF454u,0xCB0DB8u,0x0E4481u,0xE08B4Fu,0x08BB3Fu},
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [48..55] Rev.3 추가 프리셋 (BPF / NOTCH / TILT / 전용)
     * ═══════════════════════════════════════════════════════════════ */

     /* (48) BPF VOICE — 보컬 대역 분리 (300Hz~4kHz 통과)
      *  band0: HPF 300Hz Q0.707     band1: LPF 4000Hz Q0.707
      *  band2: Bell 1000Hz +3dB Q1.5 band3: Bell 2500Hz +2dB Q1.5
      *  용도: PRE EQ에서 보컬 대역만 강조, 저역/초고역 제거 */
    static const BiquadCoef EQ_BPF_VOICE[4] = {
        {0x3E3F3Eu,0x838185u,0x3E3F3Eu,0x838DCFu,0x3C8AC6u},
        {0x032AD0u,0x06559Fu,0x032AD0u,0xAE1B54u,0x1E8FEBu},
        {0x40EEB2u,0x8593A5u,0x3A8C18u,0x8593A5u,0x3B7ACBu},
        {0x4171CCu,0x915BCCu,0x3365D0u,0x915BCCu,0x34D79Cu},
    };

    /* (49) NOTCH 60HZ — 60Hz 험 제거 (해외 전원)
     *  band0: Notch 60Hz Q30       band1: Notch 120Hz Q20
     *  band2: HPF 30Hz Q0.707      band3: BYPASS
     *  용도: 60Hz 전원 험 + 2차 배음 제거 */
    static const BiquadCoef EQ_NOTCH_60HZ[4] = {
        {0x3FFDDBu,0x80054Du,0x3FFDDBu,0x80054Du,0x3FFBB6u},
        {0x3FF992u,0x8010E7u,0x3FF992u,0x8010E7u,0x3FF323u},
        {0x3FD290u,0x805AE1u,0x3FD290u,0x805B01u,0x3FA540u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
    };

    /* (50) NOTCH 50HZ — 50Hz 험 제거 (한국/유럽 전원)
     *  band0: Notch 50Hz Q30       band1: Notch 100Hz Q20
     *  band2: HPF 30Hz Q0.707      band3: BYPASS
     *  용도: 50Hz 전원 험 + 2차 배음 제거 */
    static const BiquadCoef EQ_NOTCH_50HZ[4] = {
        {0x3FFE37u,0x800447u,0x3FFE37u,0x800447u,0x3FFC6Du},
        {0x3FFAA4u,0x800D87u,0x3FFAA4u,0x800D87u,0x3FF548u},
        {0x3FD290u,0x805AE1u,0x3FD290u,0x805B01u,0x3FA540u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
    };

    /* (51) BPF INSTRUMENT — 악기 중역 분리 (200Hz~5kHz 통과)
     *  band0: HPF 200Hz Q0.707     band1: LPF 5000Hz Q0.707
     *  band2: Bell 500Hz +2dB Q1.2  band3: Bell 2000Hz +2dB Q1.2
     *  용도: 국악기 등 중역 악기의 배음 분리, 노이즈 제거 */
    static const BiquadCoef EQ_BPF_INSTRUMENT[4] = {
        {0x3ED372u,0x82591Cu,0x3ED372u,0x825E9Fu,0x3DAC67u},
        {0x049F60u,0x093EC1u,0x049F60u,0xB90339u,0x197A49u},
        {0x406497u,0x834D7Cu,0x3C926Bu,0x834D7Cu,0x3CF703u},
        {0x4173FCu,0x8F33EDu,0x3352BAu,0x8F33EDu,0x34C6B6u},
    };

    /* (52) TILT WARM — 틸트 따뜻 (저역↑고역↓)
     *  band0: LoShelf 500Hz +4dB S0.707   band1: HiShelf 3kHz -4dB S0.707
     *  band2: Bell 200Hz +1.5dB Q0.8      band3: Bell 6kHz -1.5dB Q0.8
     *  용도: 전체 스펙트럼을 따뜻한 방향으로 부드럽게 기울임 */
    static const BiquadCoef EQ_TILT_WARM[4] = {
        {0x40CEC8u,0x8645E5u,0x393F85u,0x86365Cu,0x39FEC4u},
        {0x2B4A7Au,0xC65581u,0x146BC8u,0xA2D662u,0x233560u},
        {0x402DAAu,0x81EF8Cu,0x3DEDDAu,0x81EF8Cu,0x3E1B84u},
        {0x3CB316u,0xC2EB45u,0x19AE97u,0xC2EB45u,0x1661ADu},
    };

    /* (53) TILT BRIGHT — 틸트 밝음 (저역↓고역↑)
     *  band0: LoShelf 500Hz -4dB S0.707   band1: HiShelf 3kHz +4dB S0.707
     *  band2: Bell 200Hz -1.5dB Q0.8      band3: Bell 6kHz +1.5dB Q0.8
     *  용도: 전체 스펙트럼을 밝고 선명한 방향으로 기울임 */
    static const BiquadCoef EQ_TILT_BRIGHT[4] = {
        {0x3F33CBu,0x87BAF3u,0x3945B9u,0x87CA4Bu,0x3888DCu},
        {0x5E9D9Eu,0x800000u,0x340D1Bu,0xAABF91u,0x1E3098u},
        {0x3FD277u,0x82493Eu,0x3DEF53u,0x82493Eu,0x3DC1CAu},
        {0x437ADBu,0xBF990Cu,0x179938u,0xBF990Cu,0x1B1412u},
    };

    /* (54) ANTI-FEEDBACK — 무대 피드백 억제
     *  band0: Notch 800Hz Q15      band1: Notch 1600Hz Q12
     *  band2: Notch 3200Hz Q10     band3: HPF 80Hz Q0.707
     *  용도: 무대 공연 시 피드백 주파수 선제 억제 */
    static const BiquadCoef EQ_ANTI_FEEDBACK[4] = {
        {0x3FC71Du,0x8124A9u,0x3FC71Du,0x8124A9u,0x3F8E39u},
        {0x3F7349u,0x83DF57u,0x3F7349u,0x83DF57u,0x3EE692u},
        {0x3EB971u,0x8D6598u,0x3EB971u,0x8D6598u,0x3D72E3u},
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
    };

    /* (55) HAEGEUM POST — 해금 POST EQ (잔향 후 음색 정리)
     *  band0: HPF 80Hz             band1: Bell 400Hz -2dB Q1.5
     *  band2: Bell 2500Hz +3dB Q2.0 band3: HiShelf 10kHz +2dB
     *  용도: FDN 리버브 후단에서 해금 음색의 잔향 과잉 중역 정리 */
    static const BiquadCoef EQ_HAEGEUM_POST[4] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x3FBF4Fu,0x82A122u,0x3DCB9Bu,0x82A122u,0x3D8AEAu},
        {0x41AC10u,0x8E7821u,0x3638AAu,0x8E7821u,0x37E4BBu},
        {0x48FFAAu,0xE6B7AAu,0x081E32u,0xF1A8BAu,0x062CCBu},
    };


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 7.  프리셋 인덱스 enum + 이름/포인터 테이블  (56개)
     * ─────────────────────────────────────────────────────────────── */
    typedef enum {
        EQ_IDX_BYPASS = 0,
        EQ_IDX_DAEGEUM = 1,
        EQ_IDX_PIRI = 2,
        EQ_IDX_HAEGEUM = 3,
        EQ_IDX_AJAENG = 4,
        EQ_IDX_GAYAGEUM = 5,
        EQ_IDX_GEOMUNGO = 6,
        EQ_IDX_VIOLIN = 7,
        EQ_IDX_CELLO = 8,
        EQ_IDX_PIANO = 9,
        EQ_IDX_PIANO_BRIGHT = 10,
        EQ_IDX_PIANO_WARM = 11,
        EQ_IDX_GUITAR_ACOUSTIC = 12,
        EQ_IDX_GUITAR_CLEAN = 13,
        EQ_IDX_GUITAR_DRIVE = 14,
        EQ_IDX_BASS = 15,
        EQ_IDX_DRUMS = 16,
        EQ_IDX_VOCAL_MALE = 17,
        EQ_IDX_VOCAL_FEMALE = 18,
        EQ_IDX_GUGAK = 19,
        EQ_IDX_GUGAK_WARM = 20,
        EQ_IDX_GUGAK_BRIGHT = 21,
        EQ_IDX_ORCH = 22,
        EQ_IDX_ELECTRONIC = 23,
        EQ_IDX_AMBIENT = 24,
        EQ_IDX_TELEPHONE = 25,
        EQ_IDX_RADIO = 26,
        EQ_IDX_SUBBASS = 27,
        EQ_IDX_AIRY = 28,
        EQ_IDX_MIDBOOST = 29,
        EQ_IDX_VSHAPED = 30,
        /* ── Rev.2 ── */
        EQ_IDX_UNDERWATER = 31,
        EQ_IDX_CAVE = 32,
        EQ_IDX_MEGAPHONE = 33,
        EQ_IDX_BATHROOM = 34,
        EQ_IDX_CONCERT_HALL = 35,
        EQ_IDX_STUDIO = 36,
        EQ_IDX_LOUDNESS = 37,
        EQ_IDX_DEESS = 38,
        EQ_IDX_WARMTH = 39,
        EQ_IDX_PRESENCE = 40,
        EQ_IDX_SCOOPED_MIDS = 41,
        EQ_IDX_NATURAL = 42,
        EQ_IDX_VINTAGE = 43,
        EQ_IDX_CRISPY = 44,
        EQ_IDX_DEEP_BASS = 45,
        EQ_IDX_BROADCAST = 46,
        EQ_IDX_HAEGEUM_SOLO = 47,
        /* ── Rev.3 ── */
        EQ_IDX_BPF_VOICE = 48,
        EQ_IDX_NOTCH_60HZ = 49,
        EQ_IDX_NOTCH_50HZ = 50,
        EQ_IDX_BPF_INSTRUMENT = 51,
        EQ_IDX_TILT_WARM = 52,
        EQ_IDX_TILT_BRIGHT = 53,
        EQ_IDX_ANTI_FEEDBACK = 54,
        EQ_IDX_HAEGEUM_POST = 55,
        EQ_PRESET_COUNT = 56,
    } eq_preset_idx_t;

    static const char* const EQ_PRESET_NAMES[EQ_PRESET_COUNT] = {
        "BYPASS",          /*  0  */
        "DAEGEUM",         /*  1  대금  */
        "PIRI",            /*  2  피리  */
        "HAEGEUM",         /*  3  해금  */
        "AJAENG",          /*  4  아쟁  */
        "GAYAGEUM",        /*  5  가야금 */
        "GEOMUNGO",        /*  6  거문고 */
        "VIOLIN",          /*  7  바이올린 */
        "CELLO",           /*  8  첼로  */
        "PIANO",           /*  9  피아노 */
        "PIANO-BRIGHT",    /* 10  */
        "PIANO-WARM",      /* 11  */
        "GTR-ACOU",        /* 12  어쿠스틱 기타 */
        "GTR-CLEAN",       /* 13  */
        "GTR-DRIVE",       /* 14  */
        "BASS",            /* 15  */
        "DRUMS",           /* 16  */
        "VOCAL-M",         /* 17  */
        "VOCAL-F",         /* 18  */
        "GUGAK",           /* 19  국악 */
        "GUGAK-WARM",      /* 20  */
        "GUGAK-BRIGHT",    /* 21  */
        "ORCH",            /* 22  오케스트라 */
        "ELECTRONIC",      /* 23  */
        "AMBIENT",         /* 24  */
        "TELEPHONE",       /* 25  */
        "RADIO",           /* 26  */
        "SUBBASS",         /* 27  */
        "AIRY",            /* 28  */
        "MIDBOOST",        /* 29  */
        "V-SHAPED",        /* 30  */
        /* Rev.2 */
        "UNDERWATER",      /* 31  */
        "CAVE",            /* 32  */
        "MEGAPHONE",       /* 33  */
        "BATHROOM",        /* 34  */
        "CONCERT-HALL",    /* 35  */
        "STUDIO",          /* 36  */
        "LOUDNESS",        /* 37  */
        "DE-ESS",          /* 38  */
        "WARMTH",          /* 39  */
        "PRESENCE",        /* 40  */
        "SCOOPED",         /* 41  */
        "NATURAL",         /* 42  */
        "VINTAGE",         /* 43  */
        "CRISPY",          /* 44  */
        "DEEP-BASS",       /* 45  */
        "BROADCAST",       /* 46  */
        "HAEGEUM-SOLO",    /* 47  */
        /* Rev.3 */
        "BPF-VOICE",       /* 48  보컬 대역통과 */
        "NOTCH-60HZ",      /* 49  60Hz 험 제거 */
        "NOTCH-50HZ",      /* 50  50Hz 험 제거 */
        "BPF-INST",        /* 51  악기 중역 분리 */
        "TILT-WARM",       /* 52  틸트 따뜻 */
        "TILT-BRIGHT",     /* 53  틸트 밝음 */
        "ANTI-FB",         /* 54  피드백 억제 */
        "HAEGEUM-POST",    /* 55  해금 POST */
    };

    static const BiquadCoef* const EQ_PRESET_DATA[EQ_PRESET_COUNT] = {
        EQ_BYPASS, EQ_DAEGEUM, EQ_PIRI, EQ_HAEGEUM, EQ_AJAENG,
        EQ_GAYAGEUM, EQ_GEOMUNGO, EQ_VIOLIN, EQ_CELLO,
        EQ_PIANO, EQ_PIANO_BRIGHT, EQ_PIANO_WARM,
        EQ_GUITAR_ACOUSTIC, EQ_GUITAR_CLEAN, EQ_GUITAR_DRIVE,
        EQ_BASS, EQ_DRUMS, EQ_VOCAL_MALE, EQ_VOCAL_FEMALE,
        EQ_GUGAK, EQ_GUGAK_WARM, EQ_GUGAK_BRIGHT,
        EQ_ORCH, EQ_ELECTRONIC, EQ_AMBIENT,
        EQ_TELEPHONE, EQ_RADIO, EQ_SUBBASS, EQ_AIRY,
        EQ_MIDBOOST, EQ_VSHAPED,
        /* Rev.2 */
        EQ_UNDERWATER, EQ_CAVE, EQ_MEGAPHONE, EQ_BATHROOM,
        EQ_CONCERT_HALL, EQ_STUDIO, EQ_LOUDNESS, EQ_DEESS,
        EQ_WARMTH, EQ_PRESENCE, EQ_SCOOPED_MIDS, EQ_NATURAL,
        EQ_VINTAGE, EQ_CRISPY, EQ_DEEP_BASS, EQ_BROADCAST,
        EQ_HAEGEUM_SOLO,
        /* Rev.3 */
        EQ_BPF_VOICE, EQ_NOTCH_60HZ, EQ_NOTCH_50HZ, EQ_BPF_INSTRUMENT,
        EQ_TILT_WARM, EQ_TILT_BRIGHT, EQ_ANTI_FEEDBACK, EQ_HAEGEUM_POST,
    };


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 8.  YM2203 악기 → 권장 EQ 프리셋 매핑
     * ─────────────────────────────────────────────────────────────── */
#define EQ_FOR_DAEGEUM          EQ_IDX_DAEGEUM
#define EQ_FOR_PIRI             EQ_IDX_PIRI
#define EQ_FOR_HAEGEUM          EQ_IDX_HAEGEUM
#define EQ_FOR_AJAENG           EQ_IDX_AJAENG
#define EQ_FOR_GAYAGEUM         EQ_IDX_GAYAGEUM
#define EQ_FOR_GEOMUNGO         EQ_IDX_GEOMUNGO
#define EQ_FOR_VIOLIN           EQ_IDX_VIOLIN
#define EQ_FOR_CELLO            EQ_IDX_CELLO
#define EQ_FOR_PIANO            EQ_IDX_PIANO
#define EQ_FOR_PIANO_BRIGHT     EQ_IDX_PIANO_BRIGHT
#define EQ_FOR_GUITAR           EQ_IDX_GUITAR_ACOUSTIC
#define EQ_FOR_BASS             EQ_IDX_BASS
#define EQ_FOR_DRUMS            EQ_IDX_DRUMS
#define EQ_FOR_VOCAL            EQ_IDX_VOCAL_MALE
#define EQ_FOR_GUGAK            EQ_IDX_GUGAK
#define EQ_FOR_ORCH             EQ_IDX_ORCH
#define EQ_FOR_ELECTRONIC       EQ_IDX_ELECTRONIC
#define EQ_FOR_BYPASS           EQ_IDX_BYPASS
     /* Rev.2 추가 */
#define EQ_FOR_HAEGEUM_SOLO     EQ_IDX_HAEGEUM_SOLO
#define EQ_FOR_BROADCAST        EQ_IDX_BROADCAST
#define EQ_FOR_STUDIO           EQ_IDX_STUDIO
/* Rev.3 추가 */
#define EQ_FOR_HAEGEUM_POST     EQ_IDX_HAEGEUM_POST  /* POST EQ 해금 전용 */
#define EQ_FOR_NOTCH_50HZ       EQ_IDX_NOTCH_50HZ    /* 전원 험 PRE EQ 사용 권장 */
#define EQ_FOR_ANTI_FEEDBACK    EQ_IDX_ANTI_FEEDBACK  /* 무대 PRE EQ 권장 */


/* ─────────────────────────────────────────────────────────────────────────────
 * § 9.  apply_eq_preset_idx() 인라인 헬퍼
 * ─────────────────────────────────────────────────────────────── */
#ifdef eq_apply
    static inline void eq_apply_idx(uint8_t idx)
    {
        if (idx >= EQ_PRESET_COUNT) idx = 0;
        eq_apply(EQ_PRESET_DATA[idx], EQ_PRESET_NAMES[idx]);
    }
#else
    static inline void eq_apply_idx_raw(volatile uint8_t* base, uint8_t idx)
    {
        if (idx >= EQ_PRESET_COUNT) idx = 0;
        eq_write_preset_atomic(base, EQ_PRESET_DATA[idx]);
    }
#endif


    /* ─────────────────────────────────────────────────────────────────────────────
     * §10.  pot.c Mode 4 선형 시퀀스 PRE/POST EQ 프리셋 선택 지원
     *
     *  pot.c에서 아래 매크로로 EQ_PRESET_COUNT를 공용 참조:
     *    #define pre_eq_preset_COUNT  EQ_PRESET_COUNT   (56)
     *    #define pre_eq_preset_NAMES  EQ_PRESET_NAMES
     *    #define pre_eq_preset_DATA   EQ_PRESET_DATA
     *    #define post_eq_preset_COUNT EQ_PRESET_COUNT   (56)
     *    #define post_eq_preset_NAMES EQ_PRESET_NAMES
     *    #define post_eq_preset_DATA  EQ_PRESET_DATA
     *
     *  Mode 4 선형 시퀀스: [YM]→[HARMONY]→[PRE EQ]→[VCA]→[POST EQ]→[DELAY]→[LFO]→[APPLY/EXIT]
     *
     *  JX 좌우로 프리셋 순환 선택 → apply_pre/post_eq_preset() 즉시 HW 적용
     *  SW3(SELECT) 진입 → M4_PRE_EQ_EDIT / M4_POST_EQ_EDIT 밴드별 편집 화면
     *
     *  [Rev.3 Mode 4 EQ 편집기 확장 사항]
     *  m4_update_eq_edit_improved() 내 Type 선택 시 EQ_FTYPE_COUNT=10 자동 반영.
     *  BPF/NOTCH/APF 선택 시 EQ_FTYPE_PARAM_COUNT[t]=2 이므로 편집기가
     *  Freq + Q 두 파라미터만 표시함 (Gain 행 자동 스킵).
     *  TILT 선택 시 Freq + Gain(dB) + S 세 파라미터 표시;
     *  완전한 틸트 효과를 원할 때는 band0=TILT + band1=HI-SHF 조합 권장.
     *
     *  [pot.c #ifndef EQ_GAIN_MIN_DB 블록 처리]
     *  이 헤더를 include하면 EQ_GAIN_MIN_DB가 정의되므로
     *  pot.c 내부의 보완 블록(구버전 6-타입 enum, calc 함수 등)이 자동으로
     *  스킵됨 → 10-타입 Rev.3 정의가 우선 적용됨.
     *  단, pot.c 내 eq_calc_band() switch에 신규 case가 없으면
     *  default(BELL)로 폴백하므로 해당 switch도 Rev.3에 맞게 업데이트 권장.
     * ─────────────────────────────────────────────────────────────── */


     /* ─────────────────────────────────────────────────────────────────────────────
      * §(부록)  devmem 디버그 출력 헬퍼
      * ─────────────────────────────────────────────────────────────── */
    static inline void eq_dump_devmem(uint8_t idx, uint32_t base)
    {
        if (idx >= EQ_PRESET_COUNT) { printf("# invalid idx %u\n", idx); return; }
        const BiquadCoef* p = EQ_PRESET_DATA[idx];
        printf("# EQ preset[%u]: %s  (base=0x%08X)\n", idx, EQ_PRESET_NAMES[idx], base);
        for (int b = 0; b < 4; b++) {
            printf("devmem 0x%08X 32 0x%06X  # band%d b0\n", base + EQ_REG_OFF(b, 0), p[b].b0 & 0xFFFFFFu, b);
            printf("devmem 0x%08X 32 0x%06X  # band%d b1\n", base + EQ_REG_OFF(b, 1), p[b].b1 & 0xFFFFFFu, b);
            printf("devmem 0x%08X 32 0x%06X  # band%d b2\n", base + EQ_REG_OFF(b, 2), p[b].b2 & 0xFFFFFFu, b);
            printf("devmem 0x%08X 32 0x%06X  # band%d a1\n", base + EQ_REG_OFF(b, 3), p[b].a1 & 0xFFFFFFu, b);
            printf("devmem 0x%08X 32 0x%06X  # band%d a2\n", base + EQ_REG_OFF(b, 4), p[b].a2 & 0xFFFFFFu, b);
        }
    }

#ifdef __cplusplus
}
#endif

#endif /* EQ_4BAND_PRESETS_H */