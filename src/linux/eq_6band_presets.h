/* =============================================================================
 *  eq_6band_presets.h
 *
 *  풀 파라메트릭 6밴드 EQ 프리셋 & 실시간 계수 계산 헬퍼
 *
 *  대상 HW : eq_6band_top.v  (COEF_W=24, SCALE=22, Fs=50000Hz)
 *  사용 코드: effect_bd_uio_v13.c  →  eq_apply(preset, name)
 *
 *  Q포맷 : Q2.22  →  int32 = round(float_val × 2^22) & 0xFFFFFF
 *           Q22_ONE = 0x00400000 (= 1.0)
 *           범위 -2.0 ~ +1.9999... (부호 있는 24비트)
 *
 *  레지스터 맵 (EQ_REG_OFF(band, coef) = (band*8 + coef) * 4)
 *    band 0..5,  coef: 0=b0  1=b1  2=b2  3=a1  4=a2
 *    byte offset = band * 0x20 + coef * 0x04
 *    devmem: devmem <BASE+offset> 32 <value>
 *
 *  필터 설계 (Fs=50kHz, RBJ Audio EQ Cookbook)
 *    Bell(peaking) : fc, dB_gain, Q
 *    Low  Shelf    : fc, dB_gain, S=0.707
 *    High Shelf    : fc, dB_gain, S=0.707
 *    HPF/LPF       : fc, Q=0.707 (butterworth)
 *
 *  포함 내용
 *  ─────────────────────────────────────────────────────────────
 *  § 1.  BiquadCoef 구조체 (v13.c 호환)
 *  § 2.  EQ_REG_OFF 매크로
 *  § 3.  Q22 변환 헬퍼 (eq_float_to_q22, eq_apply_float)
 *  § 4.  실시간 벨 필터 계수 계산 (eq_calc_bell / shelf / hpf)
 *  § 5.  ADC 노브 → gain 변환 (eq_adc_to_gain_db)
 *  § 6.  정적 프리셋 테이블 (15개)
 *  § 7.  프리셋 인덱스 enum + 이름 테이블
 *  § 8.  YM2203 악기별 권장 프리셋 매핑
 *  § 9.  apply_eq_preset_idx() 인라인 헬퍼
 * =============================================================================
 */

#ifndef EQ_6BAND_PRESETS_H
#define EQ_6BAND_PRESETS_H

#include <stdint.h>
#include <math.h>     /* sin, cos, sqrt, pow — eq_calc_* 함수군 */
#include <string.h>   /* memcpy */

#ifdef __cplusplus
extern "C" {
#endif


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 1.  BiquadCoef 구조체
     *        v13.c의 BiquadCoef와 동일 레이아웃 — 재정의 방지를 위해
     *        BIQUAD_COEF_DEFINED 가드 사용
     * ─────────────────────────────────────────────────────────────────────────── */
#ifndef BIQUAD_COEF_DEFINED
#define BIQUAD_COEF_DEFINED
    typedef struct {
        uint32_t b0, b1, b2;  /* 피드포워드 계수 (Q2.22 unsigned 24bit) */
        uint32_t a1, a2;      /* 피드백 계수    (Q2.22 unsigned 24bit, 부호 2의보수) */
    } BiquadCoef;
#endif


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 2.  레지스터 오프셋 매크로
     *        eq_6band_top.v:  wr_widx = addr[11:2]
     *                         wr_band = wr_widx[5:3]  →  band × 8
     *                         wr_coef = wr_widx[2:0]  →  coef
     *        byte offset = (band*8 + coef) * 4
     * ─────────────────────────────────────────────────────────────── */
#ifndef EQ_REG_OFF
#define EQ_REG_OFF(b, c)  (((uint32_t)((b)*8u + (c))) * 4u)
#endif

     /* 기본 상수 */
#define EQ_FS            50000.0    /* 샘플레이트 Hz      */
#define EQ_COEF_W        24u        /* 계수 비트폭        */
#define EQ_SCALE         22u        /* 소수점 이하 비트   */
#define EQ_Q22_ONE       0x00400000u
#define EQ_Q22_MAX       0x007FFFFFu  /* +1.9999... */
#define EQ_Q22_MIN       0xFF800000u  /* -2.0       */
#define EQ_BANDS         6u


/* ─────────────────────────────────────────────────────────────────────────────
 * § 3.  Q22 변환 헬퍼
 * ─────────────────────────────────────────────────────────────── */

 /* float → Q2.22 24bit 2의보수, uint32_t 하위 24비트로 반환 */
    static inline uint32_t eq_float_to_q22(double v)
    {
        long long i = (long long)round(v * (double)(1u << EQ_SCALE));
        if (i > 0x7FFFFFL) i = 0x7FFFFFL;
        if (i < -0x800000L) i = -0x800000L;
        return (uint32_t)(i & 0xFFFFFFu);
    }

    /* BiquadCoef 하나를 HW에 직접 쓰기 (p_eq 포인터와 reg_wr 필요) */
    /* 사용 예:  eq_write_band(p_eq, 2, &my_coef);                   */
#define eq_write_band(base_ptr, band_idx, coef_ptr)              \
    do {                                                          \
        volatile uint8_t* _b = (volatile uint8_t*)(base_ptr);   \
        uint32_t _i = (uint32_t)(band_idx);                      \
        const BiquadCoef* _c = (coef_ptr);                       \
        *((volatile uint32_t*)(_b + EQ_REG_OFF(_i,0))) = (_c)->b0 & 0x00FFFFFFu; \
        *((volatile uint32_t*)(_b + EQ_REG_OFF(_i,1))) = (_c)->b1 & 0x00FFFFFFu; \
        *((volatile uint32_t*)(_b + EQ_REG_OFF(_i,2))) = (_c)->b2 & 0x00FFFFFFu; \
        *((volatile uint32_t*)(_b + EQ_REG_OFF(_i,3))) = (_c)->a1 & 0x00FFFFFFu; \
        *((volatile uint32_t*)(_b + EQ_REG_OFF(_i,4))) = (_c)->a2 & 0x00FFFFFFu; \
    } while(0)


/* ─────────────────────────────────────────────────────────────────────────────
 * § 4.  실시간 계수 계산 함수
 *        가변저항(ADC) 값을 받아 실시간으로 계수를 업데이트하는 용도
 *        모두 인라인 — math.h 필요
 * ─────────────────────────────────────────────────────────────── */

 /*
  * eq_calc_bell() — 피킹(Bell) 필터
  *   fc   : 중심 주파수 [Hz]   예) 1000.0
  *   gain_db : 게인 [dB]       범위: -18.0 ~ +18.0 권장 (Q22 포화 주의)
  *   Q    : 대역폭 품질계수    좁은 피크=2.0, 넓은 쉘프=0.5
  *
  *  gain_db = 0 이면 바이패스와 동일 (b0=1, b1=b2=a1=a2=0)
  */
    static inline BiquadCoef eq_calc_bell(double fc, double gain_db, double Q)
    {
        BiquadCoef r;
        if (fc <= 0.0 || fc >= EQ_FS * 0.5) {
            r.b0 = EQ_Q22_ONE; r.b1 = 0; r.b2 = 0; r.a1 = 0; r.a2 = 0;
            return r;
        }
        double A = pow(10.0, gain_db / 40.0);
        double w0 = 2.0 * M_PI * fc / EQ_FS;
        double alpha = sin(w0) / (2.0 * Q);
        double a0 = 1.0 + alpha / A;
        r.b0 = eq_float_to_q22((1.0 + alpha * A) / a0);
        r.b1 = eq_float_to_q22((-2.0 * cos(w0)) / a0);
        r.b2 = eq_float_to_q22((1.0 - alpha * A) / a0);
        r.a1 = eq_float_to_q22((-2.0 * cos(w0)) / a0);
        r.a2 = eq_float_to_q22((1.0 - alpha / A) / a0);
        return r;
    }

    /*
     * eq_calc_low_shelf() — 로우 쉘프 필터
     *   fc      : 쉘프 주파수 [Hz]
     *   gain_db : 쉘프 게인 [dB]
     *   S       : 쉘프 기울기 (0.5=완만, 1.0=급함, 0.707=기본)
     */
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

    /*
     * eq_calc_high_shelf() — 하이 쉘프 필터
     */
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

    /*
     * eq_calc_hpf() — 2차 하이패스 버터워스
     */
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

    /*
     * eq_calc_lpf() — 2차 로우패스 버터워스
     */
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

    /* 바이패스 단일 밴드 */
    static inline BiquadCoef eq_calc_bypass(void)
    {
        BiquadCoef r = { EQ_Q22_ONE, 0, 0, 0, 0 };
        return r;
    }


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 5.  ADC 노브 → 게인 변환
     *
     *  eq_adc_to_gain_db(adc, center, range_db)
     *    adc       : 12비트 ADC 값 (0~4095)
     *    center    : 중립점 ADC 값 (보통 2048 = 0dB)
     *    range_db  : 노브 최대 범위 (예: 12.0 → -12 ~ +12dB)
     *
     *  반환: dB 게인 (double)
     *
     *  사용 예:
     *    double gain = eq_adc_to_gain_db(adc_buf[ADC_IDX_VOL], 2048, 12.0);
     *    BiquadCoef c = eq_calc_bell(1000.0, gain, 1.4);
     *    eq_write_band(p_eq, 3, &c);
     * ─────────────────────────────────────────────────────────────── */
    static inline double eq_adc_to_gain_db(uint16_t adc, uint16_t center,
        double range_db)
    {
        double norm = ((double)adc - (double)center) / (double)center;
        /* norm: -1.0 ~ +1.0 (center=2048 기준) */
        if (norm > 1.0) norm = 1.0;
        if (norm < -1.0) norm = -1.0;
        return norm * range_db;
    }

    /*
     *  eq_adc_to_gain_db_sym(adc, range_db)
     *    ADC 0..4095, 중립=2048 고정 버전 (ADC_CENTER 상수 사용 시)
     */
    static inline double eq_adc_to_gain_db_sym(uint16_t adc, double range_db)
    {
        return eq_adc_to_gain_db(adc, 2048u, range_db);
    }

    /*
     *  eq_build_parametric_6band()
     *  가변저항 6개(또는 ADC 배열)로 6밴드 벨 필터를 실시간 구성
     *
     *  fc_hz[6]   : 각 밴드 중심 주파수 [Hz]
     *  Q_val[6]   : 각 밴드 Q값
     *  gains_db[6]: 각 밴드 게인 [dB]  (ADC → eq_adc_to_gain_db 결과)
     *  out[6]     : 출력 BiquadCoef 배열
     *
     *  사용 예 (메인 루프 안):
     *    static const double FC[6]  = {80, 250, 800, 2500, 6000, 14000};
     *    static const double QV[6]  = {0.707, 1.4, 1.4, 1.4, 1.4, 0.707};
     *    double gains[6];
     *    for (int i=0; i<6; i++)
     *        gains[i] = eq_adc_to_gain_db_sym(adc_knobs[i], 12.0);
     *    BiquadCoef bands[6];
     *    eq_build_parametric_6band(FC, QV, gains, bands);
     *    eq_apply(bands, "LIVE");
     */
    static inline void eq_build_parametric_6band(
        const double fc_hz[6], const double Q_val[6],
        const double gains_db[6], BiquadCoef out[6])
    {
        for (int i = 0; i < 6; i++)
            out[i] = eq_calc_bell(fc_hz[i], gains_db[i], Q_val[i]);
    }

    /*
     *  권장 실시간 밴드 설정 (ADC 노브 6개 → 각 밴드 게인 -12 ~ +12dB)
     *
     *  EQ_LIVE_FC[6]  : 중심 주파수 (ISO 1/1 옥타브 표준)
     *  EQ_LIVE_Q[6]   : Q값 (1/1 옥타브 = Q ≈ 1.414)
     */
    static const double EQ_LIVE_FC[6] = { 80.0, 250.0, 800.0, 2500.0, 6000.0, 14000.0 };
    static const double EQ_LIVE_Q[6] = { 0.707, 1.414, 1.414, 1.414, 1.414, 0.707 };

    /* 고밀도 1/2옥타브 배치 (더 섬세한 조정) */
    static const double EQ_LIVE_FC_FINE[6] = { 120.0, 400.0, 1200.0, 3500.0, 8000.0, 16000.0 };
    static const double EQ_LIVE_Q_FINE[6] = { 1.414, 2.0, 2.0, 2.0, 2.0, 1.414 };


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 6.  정적 프리셋 테이블
     *        모두 Fs=50kHz, Q2.22 24비트, RBJ Cookbook 설계
     * ─────────────────────────────────────────────────────────────── */

     /* (0) BYPASS — 전 밴드 통과 (b0=Q22_ONE, 나머지 0) */
    static const BiquadCoef EQ_BYPASS[6] = {
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
    };

    /* (1) TELEPHONE — 300~3kHz 대역통과, 전화기 음색
     *   band0: HPF 300Hz / band1~4: 미드 패스 / band5: LPF 3kHz
     *   (v13.c EQ_TELEPHONE 원본 유지)                               */
    static const BiquadCoef EQ_TELEPHONE[6] = {
        {0x3EC9EBu,0x826C2Au,0x3EC9EBu,0x827590u,0x3D9D3Cu},
        {0x3DFC8Eu,0x8406E5u,0x3DFC8Eu,0x8420ADu,0x3C12E4u},
        {0x435937u,0x859569u,0x3838D1u,0x859569u,0x3B9208u},
        {0x44B38Au,0x8DDB05u,0x31D9EDu,0x8DDB05u,0x368D77u},
        {0x01F614u,0x03EC28u,0x01F614u,0x9E0E29u,0x29CA27u},
        {0x016043u,0x02C087u,0x016043u,0x9880FAu,0x2D0013u},
    };

    /* (2) PRESENCE_BOOST — 보컬/리드 존재감: 2.5kHz +4dB, HPF 120Hz
     *   band0: HPF 120Hz  band1: 250Hz -2dB  band2: 800Hz +1.5dB
     *   band3: 2500Hz +4dB  band4: 6000Hz +2dB  band5: HiShelf 10kHz -1dB */
    static const BiquadCoef EQ_PRESENCE_BOOST[6] = {
        {0x3F5233u,0x815B99u,0x3F5233u,0x815D71u,0x3EA63Fu},  /* HPF 120Hz */
        {0x3FCF3Cu,0x81EA29u,0x3E568Au,0x81EA29u,0x3E25C6u},  /* Bell 250Hz -2dB */
        {0x405BF3u,0x84701Eu,0x3BD474u,0x84701Eu,0x3C3067u},  /* Bell 800Hz +1.5dB */
        {0x44178Cu,0x939279u,0x2DEA71u,0x939279u,0x3201FDu},  /* Bell 2.5kHz +4dB */
        {0x435BD6u,0xB59AAAu,0x22B293u,0xB59AAAu,0x260E6Au},  /* Bell 6kHz +2dB */
        {0x3BD367u,0xED4681u,0x066550u,0xE817C9u,0x07676Fu},  /* HiShelf 10kHz -1dB */
    };

    /* (3) WARM_VINTAGE — 아날로그 워밍: 로우 부스트 + 하이 롤오프
     *   band0: LoShelf 100Hz +3dB  band1: Bell 200Hz +2dB
     *   band2: Bell 500Hz +1dB     band3: Bell 3kHz -1.5dB
     *   band4: Bell 8kHz -3dB      band5: HiShelf 12kHz -4dB          */
    static const BiquadCoef EQ_WARM_VINTAGE[6] = {
        {0x401DF0u,0x813DDEu,0x3EA73Eu,0x813D6Cu,0x3EC4BCu},  /* LoShelf 100Hz +3dB */
        {0x401F70u,0x80FD19u,0x3EEDBDu,0x80FD19u,0x3F0D2Du},  /* Bell 200Hz +2dB */
        {0x401D32u,0x821E3Bu,0x3E044Bu,0x821E3Bu,0x3E217Cu},  /* Bell 500Hz +1dB */
        {0x3E8BB9u,0x9A09D7u,0x2F1DDDu,0x9A09D7u,0x2DA996u},  /* Bell 3kHz -1.5dB */
        {0x38CBABu,0xD5D98Fu,0x15DE42u,0xD5D98Fu,0x0EA9EDu},  /* Bell 8kHz -3dB */
        {0x3270B2u,0x02DB8Du,0x044B5Eu,0xF3AFB5u,0x05E7E7u},  /* HiShelf 12kHz -4dB */
    };

    /* (4) BRIGHT_AIR — 에어 고역 개방감: 8~14kHz 강조
     *   band0: HPF 80Hz    band1: Bell 200Hz -1dB
     *   band2: Bell 1kHz +1dB  band3: Bell 4kHz +3dB
     *   band4: Bell 8kHz +5dB  band5: HiShelf 14kHz +4dB              */
    static const BiquadCoef EQ_BRIGHT_AIR[6] = {
        {0x3F8BEEu,0x80E825u,0x3F8BEEu,0x80E8F7u,0x3F18AEu},  /* HPF 80Hz */
        {0x3FF054u,0x812A74u,0x3EEF7Au,0x812A74u,0x3EDFCEu},  /* Bell 200Hz -1dB */
        {0x403970u,0x84A869u,0x3C191Eu,0x84A869u,0x3C528Eu},  /* Bell 1kHz +1dB */
        {0x447305u,0xA2BC29u,0x25FAF7u,0xA2BC29u,0x2A6DFCu},  /* Bell 4kHz +3dB */
        {0x4CF627u,0xCD42F8u,0x11BAF3u,0xCD42F8u,0x1EB11Au},  /* Bell 8kHz +5dB */
        {0x4EB214u,0x065A8Bu,0x06C148u,0x14F2E2u,0x06DB05u},  /* HiShelf 14kHz +4dB */
    };

    /* (5) DEEP_BASS — 베이스/킥 강조
     *   band0: LoShelf 60Hz +6dB  band1: Bell 100Hz +4dB
     *   band2: Bell 250Hz -2dB    band3: Bell 1kHz -1dB
     *   band4: Bell 4kHz -2dB     band5: HiShelf 8kHz -3dB             */
    static const BiquadCoef EQ_DEEP_BASS[6] = {
        {0x402458u,0x80B064u,0x3F2C93u,0x80B011u,0x3F5097u},  /* LoShelf 60Hz +6dB */
        {0x401FC7u,0x806F3Eu,0x3F738Fu,0x806F3Eu,0x3F9356u},  /* Bell 100Hz +4dB */
        {0x3FC5A6u,0x824752u,0x3E02ECu,0x824752u,0x3DC892u},  /* Bell 250Hz -2dB */
        {0x3FC6C4u,0x8516B7u,0x3C1C9Bu,0x8516B7u,0x3BE35Fu},  /* Bell 1kHz -1dB */
        {0x3D9492u,0xA4739Cu,0x2AE3F0u,0xA4739Cu,0x287882u},  /* Bell 4kHz -2dB */
        {0x33046Au,0xE467CCu,0x078381u,0xD2A3F5u,0x0C4BC1u},  /* HiShelf 8kHz -3dB */
    };

    /* (6) SCOOPED_METAL — 미드 스쿱: 저역+고역 부스트, 500~1.5kHz 억제
     *   일렉 기타 디스토션 사운드에 최적                              */
    static const BiquadCoef EQ_SCOOPED_METAL[6] = {
        {0x40200Au,0x80F7B0u,0x3EEA58u,0x80F74Eu,0x3F0A00u},  /* LoShelf 80Hz +4dB */
        {0x402739u,0x813939u,0x3EA9CFu,0x813939u,0x3ED108u},  /* Bell 200Hz +2dB */
        {0x3EDFDBu,0x8562CFu,0x3BFB66u,0x8562CFu,0x3ADB41u},  /* Bell 500Hz -5dB */
        {0x3B77F2u,0x941D4Au,0x325CCAu,0x941D4Au,0x2DD4BCu},  /* Bell 1.5kHz -6dB */
        {0x4482D6u,0xAA2390u,0x259E7Bu,0xAA2390u,0x2A2150u},  /* Bell 5kHz +3dB */
        {0x53CF6Au,0xD942BEu,0x0ADF62u,0xF1E144u,0x061047u},  /* HiShelf 10kHz +4dB */
    };

    /* (7) VOCAL_CLARITY — 보컬 명료도 + de-essing
     *   band3: 3.5kHz +5dB (존재감)  band4: 7kHz -3dB (치찰음 억제)  */
    static const BiquadCoef EQ_VOCAL_CLARITY[6] = {
        {0x3F270Au,0x81B1EBu,0x3F270Au,0x81B4CBu,0x3E50F4u},  /* HPF 150Hz */
        {0x3FD129u,0x81DE73u,0x3E675Au,0x81DE73u,0x3E3883u},  /* Bell 300Hz -2dB */
        {0x409849u,0x859169u,0x3ACF6Au,0x859169u,0x3B67B3u},  /* Bell 1kHz +2dB */
        {0x45D937u,0x99C7F4u,0x2B1F48u,0x99C7F4u,0x30F87Fu},  /* Bell 3.5kHz +5dB */
        {0x3A2129u,0xC808F1u,0x1DAB56u,0xC808F1u,0x17CC80u},  /* Bell 7kHz -3dB (de-ess) */
        {0x43ECF2u,0xF93E3Cu,0x060053u,0xFDA158u,0x058A29u},  /* HiShelf 12kHz +1dB */
    };

    /* (8) PIANO_BRIGHT — YM2203 FM 피아노 최적화
     *   피아노 배음 특성: 저역 펀치 + 고역 존재감                    */
    static const BiquadCoef EQ_PIANO_BRIGHT[6] = {
        {0x3FA8DEu,0x80AE43u,0x3FA8DEu,0x80AEBAu,0x3F5233u},  /* HPF 60Hz */
        {0x400BD8u,0x805E12u,0x3F98AAu,0x805E12u,0x3FA483u},  /* Bell 100Hz +2dB */
        {0x3FE89Bu,0x81D71Cu,0x3E6921u,0x81D71Cu,0x3E51BCu},  /* Bell 400Hz -1dB */
        {0x40B53Eu,0x86DBEAu,0x39D2D1u,0x86DBEAu,0x3A880Fu},  /* Bell 1.2kHz +2dB */
        {0x4715B2u,0xAC0B7Fu,0x20B080u,0xAC0B7Fu,0x27C632u},  /* Bell 5kHz +4dB */
        {0x4E5969u,0xDE1336u,0x09CA0Bu,0xEFE64Du,0x06505Du},  /* HiShelf 10kHz +3dB */
    };

    /* (9) STRINGS_WARM — YM2203 바이올린/현악기 최적화
     *   현악기 레조넌스(200~600Hz) 강조 + 고역 자연감                */
    static const BiquadCoef EQ_STRINGS_WARM[6] = {
        {0x3F8BEEu,0x80E825u,0x3F8BEEu,0x80E8F7u,0x3F18AEu},  /* HPF 80Hz */
        {0x401F70u,0x80FD19u,0x3EEDBDu,0x80FD19u,0x3F0D2Du},  /* Bell 200Hz +2dB */
        {0x403471u,0x8287EFu,0x3D9F23u,0x8287EFu,0x3DD395u},  /* Bell 600Hz +1.5dB */
        {0x4294ABu,0x9229F7u,0x30E863u,0x9229F7u,0x337D0Eu},  /* Bell 2.5kHz +3dB */
        {0x3E5903u,0xB107A0u,0x2343CDu,0xB107A0u,0x219CD0u},  /* Bell 5kHz -1dB */
        {0x37ECA9u,0xF03574u,0x05BE7Bu,0xE62E3Fu,0x07B259u},  /* HiShelf 10kHz -2dB */
    };

    /* (10) WIND_FLUTE — YM2203 플루트/관악기 최적화
     *    플루트 특성: HPF 200Hz + 중고역 에어리니스                   */
    static const BiquadCoef EQ_WIND_FLUTE[6] = {
        {0x3EDF5Cu,0x824147u,0x3EDF5Cu,0x82465Du,0x3DC3CEu},  /* HPF 200Hz */
        {0x3FE89Bu,0x81D71Cu,0x3E6921u,0x81D71Cu,0x3E51BCu},  /* Bell 400Hz -1dB */
        {0x407AD3u,0x845560u,0x3BD075u,0x845560u,0x3C4B48u},  /* Bell 800Hz +2dB */
        {0x421E09u,0x8DF6B7u,0x339E2Au,0x8DF6B7u,0x35BC32u},  /* Bell 2kHz +3dB */
        {0x43DF9Cu,0xB880A1u,0x1E34FAu,0xB880A1u,0x221496u},  /* Bell 6kHz +2dB */
        {0x3C4D22u,0xFDC467u,0x053834u,0xF9A234u,0x05A789u},  /* HiShelf 12kHz -1dB */
    };

    /* (11) MASTERING — 마스터링 A-weighting 라우드니스 보정
     *    ISO 226 등청감 곡선 역보정: 저역/고역 약간 올리기            */
    static const BiquadCoef EQ_MASTERING[6] = {
        {0x4012ECu,0x813639u,0x3EB8B3u,0x813608u,0x3ECB6Eu},  /* LoShelf 80Hz +2dB */
        {0x3FF439u,0x80E2E7u,0x3F3328u,0x80E2E7u,0x3F2761u},  /* Bell 200Hz -1dB */
        {0x403970u,0x84A869u,0x3C191Eu,0x84A869u,0x3C528Eu},  /* Bell 1kHz +1dB */
        {0x41A236u,0x94B896u,0x31BF71u,0x94B896u,0x3361A7u},  /* Bell 3kHz +2dB */
        {0x481E49u,0xD080CFu,0x10861Bu,0xD080CFu,0x18A464u},  /* Bell 8kHz +3dB */
        {0x3A2D8Cu,0x177877u,0x06FF1Bu,0x12124Cu,0x0692D2u},  /* HiShelf 15kHz -2dB */
    };

    /* ─── 화성학 응용 프리셋 ──────────────────────────────────────────────────────
     *
     *  배음 이론 기반 설계:
     *    - 기본음 f0의 배음 열: f0, 2f0, 3f0, 4f0, 5f0 ...
     *    - 홀수 배음 우세: 클라리넷, 구형파 오실레이터 (폐관 악기)
     *    - 짝수 배음 우세: 바이올린, 피아노, 삼각파 (개관 악기)
     *    - 완전5도 (3:2 비율): 가장 협화적인 음정 → 해당 배음 강조
     *    - 증4도 (√2:1 비율): 가장 불협화적 → 억제로 협화감 향상
     * ─────────────────────────────────────────────────────────────── */

     /* (12) HARMONIC_ODD — 홀수 배음 강조 (클라리넷/구형파 스펙트럼)
      *    기준 f0=220Hz: 1f=220, 3f=660, 5f=1100, 7f=1540Hz 강조
      *    YM2203 SSG 구형파 음색에 적합                               */
    static const BiquadCoef EQ_HARMONIC_ODD[6] = {
        {0x401159u,0x809278u,0x3F68A8u,0x809278u,0x3F7A01u},  /* Bell 220Hz +2dB Q=3.0 */
        {0x407C7Bu,0x8218D4u,0x3DD9DDu,0x8218D4u,0x3E5658u},  /* Bell 660Hz +4dB Q=2.5 */
        {0x40992Cu,0x84180Fu,0x3C8040u,0x84180Fu,0x3D196Bu},  /* Bell 1100Hz +3dB Q=2.5 */
        {0x408C9Bu,0x868D7Au,0x3B3553u,0x868D7Au,0x3BC1EEu},  /* Bell 1540Hz +2dB Q=2.5 */
        {0x3E6833u,0x976428u,0x321A41u,0x976428u,0x308274u},  /* Bell 3kHz -2dB */
        {0x33046Au,0xE467CCu,0x078381u,0xD2A3F5u,0x0C4BC1u},  /* HiShelf 8kHz -3dB */
    };

    /* (13) HARMONIC_EVEN — 짝수 배음 강조 (바이올린/삼각파 스펙트럼)
     *    기준 f0=440Hz: 1f=440, 2f=880, 4f=1760, 6f=2640Hz 강조
     *    YM2203 FM 현악기 음색에 적합                                */
    static const BiquadCoef EQ_HARMONIC_EVEN[6] = {
        {0x40228Au,0x813C79u,0x3ED2A8u,0x813C79u,0x3EF533u},  /* Bell 440Hz +2dB Q=3.0 */
        {0x407B3Cu,0x8319F2u,0x3D2F53u,0x8319F2u,0x3DAA8Fu},  /* Bell 880Hz +3dB Q=2.5 */
        {0x40F0A1u,0x879054u,0x3A80CCu,0x879054u,0x3B716Du},  /* Bell 1760Hz +3dB Q=2.5 */
        {0x40E8C6u,0x8D9EB2u,0x38113Au,0x8D9EB2u,0x38FA01u},  /* Bell 2640Hz +2dB Q=2.5 */
        {0x40CC19u,0x9B48C4u,0x322284u,0x9B48C4u,0x32EE9Du},  /* Bell 4kHz +1dB */
        {0x447728u,0xE66CC2u,0x07EBAFu,0xEBF80Cu,0x06D78Eu},  /* HiShelf 10kHz +1dB */
    };

    /* (14) TRITONE_CUT — 불협화음(증4도) 억제 + 완전5도 강조
     *    화성학: 440Hz 기준
     *      완전5도 = 660Hz (3:2 비율, 가장 협화) → +4dB 강조
     *      증4도   = 622Hz (√2:1 비율, tritone)  → -6dB 억제
     *    YM2203 화음 연주 시 협화감 향상 목적                        */
    static const BiquadCoef EQ_TRITONE_CUT[6] = {
        {0x3F6F0Au,0x8121ECu,0x3F6F0Au,0x812334u,0x3EDF5Cu},  /* HPF 100Hz */
        {0x404DABu,0x81AA0Bu,0x3E39CAu,0x81AA0Bu,0x3E8776u},  /* Bell 440Hz +3dB Q=2.0 */
        {0x3F6C3Fu,0x82B2F9u,0x3E4355u,0x82B2F9u,0x3DAF95u},  /* Bell 622Hz -6dB Q=3.0 (tritone) */
        {0x4067F6u,0x81D2E5u,0x3E348Fu,0x81D2E5u,0x3E9C85u},  /* Bell 660Hz +4dB Q=3.0 (5도) */
        {0x409689u,0x863C92u,0x3ADEB5u,0x863C92u,0x3B753Eu},  /* Bell 1320Hz +2dB Q=2.0 (옥타브+5도) */
        {0x4506A2u,0xD2D2ECu,0x0C38CFu,0xD9B2BAu,0x0A5FA3u},  /* HiShelf 8kHz +1dB */
    };



    /* ─────────────────────────────────────────────────────────────────────────────
     * § 6-B.  악기별 정밀 프리셋 테이블 (신규 추가 #15~#29)
     *
     *  화성학 설계 원칙 (RBJ Audio EQ Cookbook + 음향음악이론 적용):
     *    ① 개관악기(트럼펫·색소폰·대금·오보에)
     *         → 완전 배음열 (f0, 2f0, 3f0, 4f0...) 포르만트 강조
     *    ② 폐관악기(클라리넷)
     *         → 홀수배음 우세 (f0, 3f0, 5f0, 7f0...) 특유 음색 강화
     *    ③ 현악기(피아노·기타·바이올린·첼로)
     *         → 짝수배음 우세 + 인하모닉(피아노 스트레치) 특성 반영
     *    ④ 완전5도(3:2 비율) 배음 강조 → 가장 협화적 배음 지지
     *    ⑤ 증4도/감5도(√2:1 비율) 배음 억제 → 불협화 제거, 음색 순화
     *    ⑥ 포르만트 주파수 부스트 → 악기 고유 음색 정체성 강화
     *    ⑦ 국악(대금·가야금) : 5음계 순정율 배음 (완전4도 4:3, 완전5도 3:2)
     *
     *  모든 계수: Fs=50kHz, Q2.22 24비트, Python RBJ 정밀 계산
     * ─────────────────────────────────────────────────────────────── */

     /* (15) PIANO_CONCERT — 콘서트 그랜드 피아노
      *  화성: 짝수배음+인하모닉 스트레치, A3=220Hz 기준 배음열
      *        220→440→660→880→1760Hz, 타건 어택 3kHz 강조
      */
    static const BiquadCoef EQ_PIANO_CONCERT[6] = {
        {0x3FB75Cu,0x809149u,0x3FB75Cu,0x80919Bu,0x3F6F0Au},  /* HPF   50Hz Q=0.707 — 서브 럼블 차단 */
        {0x401DE3u,0x818277u,0x3E63E6u,0x8181EFu,0x3E8141u},  /* LShf 120Hz +2.5dB  — 저역 따뜻함 */
        {0x3FDA6Bu,0x81F0DAu,0x3E4BADu,0x81F0DAu,0x3E2618u},  /* Bell 300Hz -1.5dB Q=1.4 — 머디 정리 */
        {0x41E5C4u,0x918AB9u,0x34E738u,0x918AB9u,0x36CCFCu},  /* Bell  3kHz +3dB   Q=2.0 — 타건 어택 (3f@1kHz 배음) */
        {0x43E23Au,0xBD4134u,0x24D3C9u,0xBD4134u,0x28B603u},  /* Bell  7kHz +2.5dB Q=1.5 — 현 존재감 */
        {0x48177Du,0xF695F9u,0x0673B9u,0xFFA18Du,0x057FA1u},  /* HShf 12kHz +2dB         — 에어 개방감 */
    };

    /* (16) ACOUSTIC_GUITAR — 어쿠스틱 기타 (스틸현)
     *  화성: 개방현 E2=82Hz, A2=110Hz → 완전4도(4:3) 배음 관계
     *        바디공명 100~200Hz (브레이싱 공진), 핑거노이즈 8kHz
     */
    static const BiquadCoef EQ_ACOUSTIC_GUITAR[6] = {
        {0x3F9A64u,0x80CB37u,0x3F9A64u,0x80CBD9u,0x3F356Au},  /* HPF   70Hz          — 럼블 제거 */
        {0x401725u,0x807491u,0x3F78A5u,0x807491u,0x3F8FCAu},  /* Bell 130Hz +3dB   Q=2.0 — 바디공명 (E2 지지) */
        {0x401C37u,0x813A21u,0x3EB86Cu,0x813A21u,0x3ED4A3u},  /* Bell 240Hz +1.5dB Q=1.5 — 바디 워밍 */
        {0x413470u,0x8B04F0u,0x3791FCu,0x8B04F0u,0x38C66Cu},  /* Bell  2kHz +2.5dB Q=1.8 — 프레젠스 (피크 어택) */
        {0x4189DFu,0xA9F3C4u,0x2E232Cu,0xA9F3C4u,0x2FAD0Bu},  /* Bell 5.5kHz+1.5dB Q=2.0 — 스트링 고역 */
        {0x49D5B0u,0xD80441u,0x0AEC93u,0xE4D837u,0x07EE4Eu},  /* HShf  9kHz +2dB         — 핑거노이즈 & 에어 */
    };

    /* (17) BASS_GUITAR — 베이스 기타 (일렉트릭)
     *  화성: E1=41Hz 기본음, 완전5도(3:2) E1-B1 배음 관계
     *        배음: 41→82(2f0)→164(4f0)→328Hz, 픽 어택 800Hz
     */
    static const BiquadCoef EQ_BASS_GUITAR[6] = {
        {0x3FCD1Eu,0x8065C4u,0x3FCD1Eu,0x8065EDu,0x3F9A64u},  /* HPF   35Hz          — 서브 럼블 제거 */
        {0x402832u,0x80F14Eu,0x3EE8B2u,0x80F0D3u,0x3F1069u},  /* LShf  80Hz +5dB     — 서브 펀치 */
        {0x4025E6u,0x80BE52u,0x3F225Eu,0x80BE52u,0x3F4844u},  /* Bell 160Hz +3dB   Q=1.5 — 2f0 옥타브 배음 */
        {0x4099D9u,0x843B59u,0x3BCB98u,0x843B59u,0x3C6571u},  /* Bell 800Hz +2.5dB Q=1.5 — 픽 어택 존재감 */
        {0x3EA2E5u,0x92E06Au,0x341A52u,0x92E06Au,0x32BD36u},  /* Bell 2.5kHz -2dB  Q=1.5 — 미드 스쿱 (믹스 공간) */
        {0x31B84Bu,0xD5B902u,0x0BEAE9u,0xC049CEu,0x131268u},  /* HShf  6kHz -3dB         — 하이 롤오프 (저역 집중) */
    };

    /* (18) TRUMPET — Bb 트럼펫 (개관악기, 완전 배음열)
     *  화성: Bb3=233Hz 기음, 포르만트 880Hz(2f0@440), 1760Hz(4f0)
     *        금관악기 공통: 완전5도+옥타브 반복 배음 특성
     */
    static const BiquadCoef EQ_TRUMPET[6] = {
        {0x3F1BDCu,0x81C849u,0x3F1BDCu,0x81CD63u,0x3E3CD2u},  /* HPF 200Hz Q=0.9    — 기음 이하 차단 */
        {0x40232Eu,0x819E2Bu,0x3E6792u,0x819E2Bu,0x3E8AC0u},  /* Bell 400Hz +1.5dB Q=2.0 — 몸통 공명 */
        {0x40A51Du,0x82F94Du,0x3D264Au,0x82F94Du,0x3DCB67u},  /* Bell 880Hz +4dB   Q=2.5 — 포르만트1 (2f0@440Hz) */
        {0x40F0A1u,0x879054u,0x3A80CCu,0x879054u,0x3B716Du},  /* Bell 1.76kHz+3dB  Q=2.5 — 포르만트2 (4f0) 브라스컷 */
        {0x419B39u,0x9AB4ACu,0x31FC63u,0x9AB4ACu,0x33979Cu},  /* Bell  4kHz +2dB   Q=2.0 — 밝음 & 존재감 */
        {0x39D795u,0xEEC7C6u,0x060FB7u,0xE72269u,0x078CA8u},  /* HShf 10kHz -1.5dB       — 치찰음 억제 */
    };

    /* (19) SAXOPHONE_ALTO — 알토 색소폰
     *  화성: Eb4=311Hz 중심음역, 단3도(6:5) 배음 특성 (재즈/블루스)
     *        포르만트: 650Hz(모음 /오/ 공명), 2200Hz(리드 존재감)
     */
    static const BiquadCoef EQ_SAXOPHONE_ALTO[6] = {
        {0x3F264Cu,0x81B367u,0x3F264Cu,0x81B718u,0x3E5049u},  /* HPF 170Hz Q=0.8    — 저역 차단 */
        {0x4024A1u,0x812F06u,0x3EC075u,0x812F06u,0x3EE515u},  /* Bell 280Hz +2dB   Q=1.8 — 리드 바디 */
        {0x40855Bu,0x8284ECu,0x3D612Fu,0x8284ECu,0x3DE68Au},  /* Bell 650Hz +3.5dB Q=2.0 — 색소폰 공명 (모음 포르만트) */
        {0x41ECB8u,0x8B311Du,0x377E78u,0x8B311Du,0x396B30u},  /* Bell 2.2kHz+4dB   Q=2.0 — 존재감 & 리드 특성 */
        {0x416ED8u,0xA4BEF9u,0x2F5CFAu,0xA4BEF9u,0x30CBD2u},  /* Bell  5kHz +1.5dB Q=2.0 — 키 클릭 & 에어 */
        {0x3B95BCu,0xE4F14Du,0x07BBE4u,0xDF2F4Du,0x0913A1u},  /* HShf  9kHz -1dB         — 하이 롤오프 */
    };

    /* (20) DAEGEUM — 대금 (한국 전통 대나무 피리, 개관악기)
     *  화성: 황종(D4=293Hz) 기음, 한국 5음계 순정율
     *        궁(D)·상(E)·각(G)·치(A)·우(B) — 완전5도(3:2) 배음 강조
     *        청공: 갈대 울림판 고유 배음 6.5kHz — 대금만의 특색!
     */
    static const BiquadCoef EQ_DAEGEUM[6] = {
        {0x3ED9F8u,0x824C11u,0x3ED9F8u,0x8252C9u,0x3DBAA8u},  /* HPF 230Hz Q=0.8    — 대금 기음 이하 차단 */
        {0x402928u,0x815D49u,0x3E98F1u,0x815D49u,0x3EC219u},  /* Bell 350Hz +2dB   Q=2.0 — 기음 지지 (황종 D4) */
        {0x406277u,0x825A38u,0x3DC02Au,0x825A38u,0x3E22A2u},  /* Bell 700Hz +3dB   Q=2.5 — 완전5도 배음 (3:2 비율) */
        {0x411146u,0x8E1B4Eu,0x36AFE4u,0x8E1B4Eu,0x37C12Au},  /* Bell 2.5kHz+2dB   Q=2.0 — 존재감 */
        {0x4428BEu,0xAFB1C8u,0x31271Bu,0xAFB1C8u,0x354FDAu},  /* Bell 6.5kHz+5dB   Q=3.0 — 청공 특색 (대금 고유음색!) */
        {0x46D03Eu,0xE47CBBu,0x085A68u,0xECF279u,0x06B4E9u},  /* HShf 10kHz +1.5dB       — 에어 & 브레스 */
    };

    /* (21) VIOLIN — 바이올린 (짝수배음 우세 현악기)
     *  화성: G3=196Hz(G현), A4=440Hz(A현), 짝수배음 강조
     *        볼프음(Wolf tone) 200~250Hz 억제 필수
     *        A현 440Hz(1f), 880Hz(2f) 짝수배음 강조
     */
    static const BiquadCoef EQ_VIOLIN[6] = {
        {0x3F329Eu,0x819AC5u,0x3F329Eu,0x819EE9u,0x3E695Fu},  /* HPF 180Hz Q=0.9    — G현 이하 차단 */
        {0x3FE95Au,0x80EB0Cu,0x3F3A67u,0x80EB0Cu,0x3F23C1u},  /* Bell 240Hz -2dB   Q=2.5 — 볼프음 억제 (G현 공명) */
        {0x403E47u,0x815F8Bu,0x3E93CCu,0x815F8Bu,0x3ED213u},  /* Bell 440Hz +3dB   Q=2.5 — A현 공명 (짝수배음 1f) */
        {0x40667Cu,0x832AF6u,0x3D32F5u,0x832AF6u,0x3D9971u},  /* Bell 880Hz +2.5dB Q=2.5 — 2f 짝수배음 강조 */
        {0x428ADFu,0x957433u,0x3335E6u,0x957433u,0x35C0C5u},  /* Bell 3.5kHz+3.5dB Q=2.0 — 프레젠스 (보잉 어택) */
        {0x4506A2u,0xD2D2ECu,0x0C38CFu,0xD9B2BAu,0x0A5FA3u},  /* HShf  8kHz +1dB         — 에어감 */
    };

    /* (22) CELLO — 첼로 (현악기, 저역 현악기)
     *  화성: C2=65Hz(C현) 기음, 완전5도(3:2) C-G 배음 관계
     *        몸통 공명(Body resonance): 90~250Hz 포근한 챔버톤
     */
    static const BiquadCoef EQ_CELLO[6] = {
        {0x3FB75Cu,0x809149u,0x3FB75Cu,0x80919Bu,0x3F6F0Au},  /* HPF  50Hz           — 서브 제거 */
        {0x401AF3u,0x811E1Fu,0x3EC966u,0x811DC3u,0x3EE3FDu},  /* LShf  90Hz +3dB     — C현 지지 (몸통감) */
        {0x4028FAu,0x8105C7u,0x3EE14Bu,0x8105C7u,0x3F0A45u},  /* Bell 250Hz +2.5dB Q=1.8 — 몸통 공명 (챔버) */
        {0x3FA490u,0x8520A7u,0x3BDA69u,0x8520A7u,0x3B7EF9u},  /* Bell 800Hz -1.5dB Q=1.5 — 미드 스쿱 (먹먹함 제거) */
        {0x41172Bu,0x8A5AF2u,0x385EC3u,0x8A5AF2u,0x3975EEu},  /* Bell  2kHz +2.5dB Q=2.0 — 활 어택 존재감 */
        {0x429B30u,0xCB9A15u,0x0E84F0u,0xCF5624u,0x0D6412u},  /* HShf  7kHz +0.5dB       — 자연스러운 에어 */
    };

    /* (23) CLARINET — 클라리넷 (폐관악기 → 홀수배음 우세!)
     *  화성: D3=147Hz 기음, 홀수배음 열: 1f≈220, 3f≈660, 5f≈1100, 7f≈1540Hz
     *        짝수배음 거의 없음 → 특유의 공허하고 깊은 음색
     *        Bell 660Hz +4dB = 클라리넷 음색 정체성의 핵심 처리
     */
    static const BiquadCoef EQ_CLARINET[6] = {
        {0x3F602Bu,0x813FAAu,0x3F602Bu,0x81422Du,0x3EC2D9u},  /* HPF 140Hz Q=0.9    — 기음 이하 차단 */
        {0x4014CDu,0x80AD20u,0x3F4A89u,0x80AD20u,0x3F5F56u},  /* Bell 220Hz +2dB   Q=2.5 — 기음 지지 (1f) */
        {0x407C7Bu,0x8218D4u,0x3DD9DDu,0x8218D4u,0x3E5658u},  /* Bell 660Hz +4dB   Q=2.5 — 3f 홀수배음 (클라리넷 핵심!) */
        {0x40992Cu,0x84180Fu,0x3C8040u,0x84180Fu,0x3D196Bu},  /* Bell 1.1kHz+3dB   Q=2.5 — 5f 홀수배음 */
        {0x416F96u,0x9637B5u,0x33791Cu,0x9637B5u,0x34E8B1u},  /* Bell 3.5kHz+2dB   Q=2.0 — 리드 존재감 */
        {0x397E43u,0xE6BF31u,0x074AF5u,0xDE42B3u,0x0945B5u},  /* HShf  9kHz -1.5dB       — 리드 노이즈 억제 */
    };

    /* (24) OBOE — 오보에 (더블리드, 짝수+홀수 배음 혼합)
     *  화성: Bb3=233Hz 기음, 포르만트 1.5kHz 강
     *        4kHz 날카로움(nasality) 억제 → 귀 피로감 감소
     */
    static const BiquadCoef EQ_OBOE[6] = {
        {0x3F051Du,0x81F5C5u,0x3F051Du,0x81FBF0u,0x3E1065u},  /* HPF 220Hz Q=0.9    — 저역 차단 */
        {0x403386u,0x81545Cu,0x3E9782u,0x81545Cu,0x3ECB08u},  /* Bell 350Hz +2.5dB Q=2.0 — 리드 몸통 */
        {0x40F1B1u,0x860140u,0x3B4038u,0x860140u,0x3C31E8u},  /* Bell 1.5kHz+3.5dB Q=2.5 — 오보에 포르만트 */
        {0x3D71ECu,0x9BF775u,0x34B558u,0x9BF775u,0x322744u},  /* Bell  4kHz -4dB   Q=2.5 — 날카로움 억제 (귀 보호) */
        {0x426D98u,0xBA5D5Fu,0x2AD116u,0xBA5D5Fu,0x2D3EADu},  /* Bell  7kHz +2dB   Q=2.0 — 에어감 */
        {0x44319Cu,0xEFE6D9u,0x06AED9u,0xF4D936u,0x05EE19u},  /* HShf 11kHz +1dB         — 개방감 */
    };

    /* (25) USER_CUSTOM_A — 사용자 커스텀 슬롯 A
     *  기본값: 바이패스. 실시간 교체 가이드:
     */
    static const BiquadCoef EQ_USER_CUSTOM_A[6] = {
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band0: HPF/LPF → eq_calc_hpf(fc,Q) / eq_calc_lpf(fc,Q)로 교체 */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band1: LoShelf → eq_calc_low_shelf(fc,gain_db,S)로 교체 */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band2: Bell 200~800Hz → eq_calc_bell(fc,db,Q), Q=1.0~2.0 권장 */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band3: Bell 1k~4kHz  → 존재감/포르만트 영역 */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band4: Bell 4k~10kHz → 에어/존재감 영역 */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band5: HiShelf → eq_calc_high_shelf(fc,gain_db,S)로 교체 */
    };

    /* (26) USER_CUSTOM_B — 사용자 커스텀 슬롯 B (악기 2 전용)
     */
    static const BiquadCoef EQ_USER_CUSTOM_B[6] = {
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band0: bypass */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band1: bypass */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band2: bypass */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band3: bypass */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band4: bypass */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band5: bypass */
    };

    /* (27) USER_CUSTOM_C — 사용자 커스텀 슬롯 C (앙상블/보정 전용)
     */
    static const BiquadCoef EQ_USER_CUSTOM_C[6] = {
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band0: bypass */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band1: bypass */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band2: bypass */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band3: bypass */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band4: bypass */
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},  /* band5: bypass */
    };

    /* (28) JAZZ_ENSEMBLE — 재즈 앙상블 믹스 최적화
     *  화성: 장7화음(maj7)/단7화음(m7) 배음 공간 공존 설계
     *        단7도(16:9 비율) 텐션 노트가 충돌하지 않도록 미드 개방
     *        피아노+알토색소폰+베이스기타+드럼 최적 믹스
     */
    static const BiquadCoef EQ_JAZZ_ENSEMBLE[6] = {
        {0x3FA8DEu,0x80AE43u,0x3FA8DEu,0x80AEBAu,0x3F5233u},  /* HPF   60Hz          — 서브 제거 */
        {0x4011E6u,0x818D1Au,0x3E6503u,0x818CC9u,0x3E7697u},  /* LShf 120Hz +1.5dB   — 베이스 워밍 */
        {0x3FDEC4u,0x828BD7u,0x3DBE02u,0x828BD7u,0x3D9CC6u},  /* Bell 400Hz -1dB   Q=1.4 — 머디 정리 */
        {0x4123DCu,0x8C8D0Au,0x360DBEu,0x8C8D0Au,0x37319Au},  /* Bell  2kHz +2dB   Q=1.5 — 재즈 존재감 */
        {0x416ED8u,0xA4BEF9u,0x2F5CFAu,0xA4BEF9u,0x30CBD2u},  /* Bell  5kHz +1.5dB Q=2.0 — 에어 & 밝음 */
        {0x4231F4u,0xE844D7u,0x078273u,0xEAFE80u,0x06FABEu},  /* HShf 10kHz +0.5dB       — 자연스러운 개방감 */
    };

    /* (29) GUGAK_ENSEMBLE — 국악 앙상블 믹스 최적화
     *  화성: 한국 5음계 순정율 — 궁(D):상(E):각(G):치(A):우(B)
     *        완전5도(3:2) 배음 700Hz 강조 (궁→치 관계)
     *        완전4도(4:3): 가야금 현 진동 특성 지지
     *        대금+가야금+해금+아쟁+장구 혼합 최적화
     */
    static const BiquadCoef EQ_GUGAK_ENSEMBLE[6] = {
        {0x3F994Au,0x80CD6Cu,0x3F994Au,0x80CE3Eu,0x3F3367u},  /* HPF   80Hz Q=0.8    — 저역 차단 */
        {0x401F70u,0x80FD19u,0x3EEDBDu,0x80FD19u,0x3F0D2Du},  /* Bell 200Hz +2dB   Q=1.5 — 가야금 바디 */
        {0x407AA3u,0x82CEF1u,0x3D32D1u,0x82CEF1u,0x3DAD74u},  /* Bell 700Hz +3dB   Q=2.0 — 완전5도 배음 (3:2 비율) */
        {0x3F28A0u,0x8E4C08u,0x363B7Du,0x8E4C08u,0x35641Du},  /* Bell  2kHz -1.5dB Q=1.5 — 미드 정리 */
        {0x4268DAu,0xA422B6u,0x2F241Du,0xA422B6u,0x318CF7u},  /* Bell  5kHz +2.5dB Q=2.0 — 청명감 */
        {0x47AF3Eu,0xD01965u,0x0CF654u,0xDA9C4Cu,0x0A22ABu},  /* HShf  8kHz +1.5dB       — 국악 특유 에어 */
    };

    /* ─────────────────────────────────────────────────────────────────────────────
     * § 7.  프리셋 인덱스 enum + 이름/포인터 테이블
     * ─────────────────────────────────────────────────────────────── */
    typedef enum {
        EQ_IDX_BYPASS = 0,
        EQ_IDX_TELEPHONE = 1,
        EQ_IDX_PRESENCE_BOOST = 2,
        EQ_IDX_WARM_VINTAGE = 3,
        EQ_IDX_BRIGHT_AIR = 4,
        EQ_IDX_DEEP_BASS = 5,
        EQ_IDX_SCOOPED_METAL = 6,
        EQ_IDX_VOCAL_CLARITY = 7,
        EQ_IDX_PIANO_BRIGHT = 8,   /* 기존 FM 피아노 프리셋 (유지) */
        EQ_IDX_STRINGS_WARM = 9,
        EQ_IDX_WIND_FLUTE = 10,
        EQ_IDX_MASTERING = 11,
        EQ_IDX_HARMONIC_ODD = 12,
        EQ_IDX_HARMONIC_EVEN = 13,
        EQ_IDX_TRITONE_CUT = 14,
        /* ── 신규 악기별 정밀 프리셋 (#15~#29) ── */
        EQ_IDX_PIANO_CONCERT = 15,   /* 콘서트 그랜드 피아노 */
        EQ_IDX_ACOUSTIC_GUITAR = 16,   /* 어쿠스틱 기타 (스틸현) */
        EQ_IDX_BASS_GUITAR = 17,   /* 베이스 기타 */
        EQ_IDX_TRUMPET = 18,   /* Bb 트럼펫 */
        EQ_IDX_SAXOPHONE_ALTO = 19,   /* 알토 색소폰 */
        EQ_IDX_DAEGEUM = 20,   /* 대금 (한국 전통) */
        EQ_IDX_VIOLIN = 21,   /* 바이올린 */
        EQ_IDX_CELLO = 22,   /* 첼로 */
        EQ_IDX_CLARINET = 23,   /* 클라리넷 */
        EQ_IDX_OBOE = 24,   /* 오보에 */
        EQ_IDX_USER_CUSTOM_A = 25,   /* 사용자 커스텀 슬롯 A */
        EQ_IDX_USER_CUSTOM_B = 26,   /* 사용자 커스텀 슬롯 B */
        EQ_IDX_USER_CUSTOM_C = 27,   /* 사용자 커스텀 슬롯 C */
        EQ_IDX_JAZZ_ENSEMBLE = 28,   /* 재즈 앙상블 믹스 */
        EQ_IDX_GUGAK_ENSEMBLE = 29,   /* 국악 앙상블 믹스 */
        EQ_PRESET_COUNT = 30,
    } eq_preset_idx_t;

    /* v13.c EQ_PRESET_NAMES / EQ_PRESET_DATA 형식과 동일하게 확장 */
    static const char* const EQ_PRESET_NAMES[EQ_PRESET_COUNT] = {
        /* 기존 15개 */
        "BYPASS",        /*  0 */
        "TELEPHONE",     /*  1 */
        "PRESENCE",      /*  2 */
        "WARM VNT",      /*  3 */
        "BRIGHT AIR",    /*  4 */
        "DEEP BASS",     /*  5 */
        "SCOOP MTL",     /*  6 */
        "VOCAL CLR",     /*  7 */
        "PIANO BRT",     /*  8 */
        "STRINGS",       /*  9 */
        "FLUTE",         /* 10 */
        "MASTERING",     /* 11 */
        "HARM ODD",      /* 12 */
        "HARM EVEN",     /* 13 */
        "TRITONE-",      /* 14 */
        /* 신규 15개 */
        "PIANO CON",     /* 15 콘서트 피아노 */
        "ACST GTR",      /* 16 어쿠스틱 기타 */
        "BASS GTR",      /* 17 베이스 기타 */
        "TRUMPET",       /* 18 트럼펫 */
        "ALTO SAX",      /* 19 알토 색소폰 */
        "DAEGEUM",       /* 20 대금 */
        "VIOLIN",        /* 21 바이올린 */
        "CELLO",         /* 22 첼로 */
        "CLARINET",      /* 23 클라리넷 */
        "OBOE",          /* 24 오보에 */
        "USER A",        /* 25 사용자 커스텀 A */
        "USER B",        /* 26 사용자 커스텀 B */
        "USER C",        /* 27 사용자 커스텀 C */
        "JAZZ ENS",      /* 28 재즈 앙상블 */
        "GUGAK ENS",     /* 29 국악 앙상블 */
    };

    static const BiquadCoef* const EQ_PRESET_DATA[EQ_PRESET_COUNT] = {
        /* 기존 15개 (0~14) */
        EQ_BYPASS,
        EQ_TELEPHONE,
        EQ_PRESENCE_BOOST,
        EQ_WARM_VINTAGE,
        EQ_BRIGHT_AIR,
        EQ_DEEP_BASS,
        EQ_SCOOPED_METAL,
        EQ_VOCAL_CLARITY,
        EQ_PIANO_BRIGHT,
        EQ_STRINGS_WARM,
        EQ_WIND_FLUTE,
        EQ_MASTERING,
        EQ_HARMONIC_ODD,
        EQ_HARMONIC_EVEN,
        EQ_TRITONE_CUT,
        /* 신규 15개 (15~29) */
        EQ_PIANO_CONCERT,
        EQ_ACOUSTIC_GUITAR,
        EQ_BASS_GUITAR,
        EQ_TRUMPET,
        EQ_SAXOPHONE_ALTO,
        EQ_DAEGEUM,
        EQ_VIOLIN,
        EQ_CELLO,
        EQ_CLARINET,
        EQ_OBOE,
        EQ_USER_CUSTOM_A,
        EQ_USER_CUSTOM_B,
        EQ_USER_CUSTOM_C,
        EQ_JAZZ_ENSEMBLE,
        EQ_GUGAK_ENSEMBLE,
    };


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 8.  YM2203 악기 → 권장 EQ 프리셋 매핑
     *
     *  ym2203_patches.h의 INST_* 열거형 값을 키로 사용
     *  (악기 인덱스가 변경될 경우 여기서만 수정)
     *
     *  사용 예:
     *    uint8_t eq_idx = YM2203_EQ_MAP[g_preset.ym_inst[0]];
     *    apply_eq_preset(eq_idx);
     * ─────────────────────────────────────────────────────────────── */

     /*  악기 카테고리별 매핑 상수  */
#define EQ_FOR_PIANO        EQ_IDX_PIANO_BRIGHT
#define EQ_FOR_EPIANO       EQ_IDX_PRESENCE_BOOST
#define EQ_FOR_ORGAN        EQ_IDX_HARMONIC_ODD
#define EQ_FOR_STRINGS      EQ_IDX_STRINGS_WARM
#define EQ_FOR_VIOLIN       EQ_IDX_STRINGS_WARM
#define EQ_FOR_CELLO        EQ_IDX_WARM_VINTAGE
#define EQ_FOR_CHOIR        EQ_IDX_VOCAL_CLARITY
#define EQ_FOR_FLUTE        EQ_IDX_WIND_FLUTE
#define EQ_FOR_CLARINET     EQ_IDX_HARMONIC_ODD
#define EQ_FOR_BRASS        EQ_IDX_PRESENCE_BOOST
#define EQ_FOR_GUITAR       EQ_IDX_WARM_VINTAGE
#define EQ_FOR_GUITAR_DIST  EQ_IDX_SCOOPED_METAL
#define EQ_FOR_BASS         EQ_IDX_DEEP_BASS
#define EQ_FOR_DRUM         EQ_IDX_DEEP_BASS
#define EQ_FOR_BELL         EQ_IDX_BRIGHT_AIR
#define EQ_FOR_MARIMBA      EQ_IDX_HARMONIC_EVEN
#define EQ_FOR_SFX          EQ_IDX_BYPASS
#define EQ_FOR_TRUMPET      EQ_IDX_TRUMPET
#define EQ_FOR_SAXOPHONE    EQ_IDX_SAXOPHONE_ALTO
#define EQ_FOR_OBOE         EQ_IDX_OBOE
#define EQ_FOR_CLARINET_HD  EQ_IDX_CLARINET  /* 고정밀 클라리넷 (기존 HARMONIC_ODD 대체 가능) */
#define EQ_FOR_DAEGEUM      EQ_IDX_DAEGEUM
#define EQ_FOR_CELLO        EQ_IDX_CELLO
#define EQ_FOR_VIOLIN_HD    EQ_IDX_VIOLIN    /* 고정밀 바이올린 (기존 STRINGS_WARM 대체 가능) */
#define EQ_FOR_PIANO_HD     EQ_IDX_PIANO_CONCERT  /* 고정밀 콘서트 피아노 */
#define EQ_FOR_GUITAR_ACST  EQ_IDX_ACOUSTIC_GUITAR
#define EQ_FOR_BASS_HD      EQ_IDX_BASS_GUITAR
#define EQ_FOR_JAZZ         EQ_IDX_JAZZ_ENSEMBLE
#define EQ_FOR_GUGAK        EQ_IDX_GUGAK_ENSEMBLE
#define EQ_FOR_USER_A       EQ_IDX_USER_CUSTOM_A
#define EQ_FOR_USER_B       EQ_IDX_USER_CUSTOM_B
#define EQ_FOR_USER_C       EQ_IDX_USER_CUSTOM_C


/*
 *  eq_recommend_for_inst(inst_idx)
 *  ym2203_patches.h의 악기 분류명을 보고 EQ 인덱스 반환
 *  (ym2203_patches.h의 YM2203_INSTRUMENTS[].category 필드가 있다면
 *   switch로 정확히 연결 가능, 없으면 인덱스 범위 기반 추정)
 *
 *  ── 기본값: 악기 인덱스 0~7이면 PIANO군, 8~15이면 STRINGS군 등
 *     실제 ym2203_patches.h 구조에 맞게 조정 필요               */
    static inline uint8_t eq_recommend_for_inst(uint8_t inst_idx)
    {
        /*  ym2203_patches.h에 category 문자열이 있다면:
         *  const char* cat = YM2203_INSTRUMENTS[inst_idx].category;
         *  if (strstr(cat,"PIANO"))   return EQ_FOR_PIANO;
         *  if (strstr(cat,"VIOLIN"))  return EQ_FOR_VIOLIN;
         *  ...
         *  아래는 인덱스 범위 기반 폴백                              */
        (void)inst_idx;
        return EQ_IDX_BYPASS;  /* 기본값: BYPASS (사용자가 직접 선택) */
    }


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 9.  apply_eq_preset_idx() — v13.c apply_eq_preset() 확장판
     *
     *  기존 v13.c:
     *    static void apply_eq_preset(uint8_t idx) {
     *        if (idx >= EQ_PRESET_COUNT) idx = 0;
     *        eq_apply(EQ_PRESET_DATA[idx], EQ_PRESET_NAMES[idx]);
     *    }
     *
     *  이 헤더로 교체 시:
     *    #include "eq_6band_presets.h"
     *    — EQ_PRESET_COUNT, EQ_PRESET_NAMES, EQ_PRESET_DATA 자동 확장
     *    — 기존 apply_eq_preset() 함수 그대로 사용 가능 (15개로 확장됨)
     *
     *  또는 아래 인라인 헬퍼 직접 사용:
     *    eq_apply_idx(p_eq, EQ_IDX_PIANO_BRIGHT);
     * ─────────────────────────────────────────────────────────────── */

     /*
      *  eq_apply_idx() — p_eq 포인터에 직접 쓰기
      *  v13.c의 eq_apply()가 있으면 그것을 사용하고,
      *  없는 환경(테스트 등)을 위해 매크로 기반 fallback도 제공
      */
#ifdef eq_apply   /* v13.c에서 정의된 경우 */
    static inline void eq_apply_idx(uint8_t idx)
    {
        if (idx >= EQ_PRESET_COUNT) idx = 0;
        eq_apply(EQ_PRESET_DATA[idx], EQ_PRESET_NAMES[idx]);
    }
#else
      /* eq_apply가 없는 독립 사용 시: p_eq 포인터 직접 전달 */
    static inline void eq_apply_idx_raw(volatile uint8_t* base, uint8_t idx)
    {
        if (idx >= EQ_PRESET_COUNT) idx = 0;
        const BiquadCoef* p = EQ_PRESET_DATA[idx];
        for (int b = 0; b < 6; b++) {
            *((volatile uint32_t*)(base + EQ_REG_OFF(b, 0))) = p[b].b0 & 0x00FFFFFFu;
            *((volatile uint32_t*)(base + EQ_REG_OFF(b, 1))) = p[b].b1 & 0x00FFFFFFu;
            *((volatile uint32_t*)(base + EQ_REG_OFF(b, 2))) = p[b].b2 & 0x00FFFFFFu;
            *((volatile uint32_t*)(base + EQ_REG_OFF(b, 3))) = p[b].a1 & 0x00FFFFFFu;
            *((volatile uint32_t*)(base + EQ_REG_OFF(b, 4))) = p[b].a2 & 0x00FFFFFFu;
        }
    }
#endif /* eq_apply */


    /* ─────────────────────────────────────────────────────────────────────────────
     * § (부록)  devmem 디버그 출력 헬퍼
     *
     *  eq_dump_devmem(preset_idx, base_addr)
     *  → 해당 프리셋의 devmem 커맨드를 stdout에 출력
     *  → 셸에서 직접 붙여넣어 동작 확인 가능
     *
     *  사용 예:
     *    eq_dump_devmem(EQ_IDX_PIANO_BRIGHT, 0x40001000);
     * ─────────────────────────────────────────────────────────────── */
#include <stdio.h>
    static inline void eq_dump_devmem(uint8_t idx, uint32_t base)
    {
        if (idx >= EQ_PRESET_COUNT) { printf("# invalid idx %u\n", idx); return; }
        const BiquadCoef* p = EQ_PRESET_DATA[idx];
        printf("# EQ preset: %s  (base=0x%08X)\n", EQ_PRESET_NAMES[idx], base);
        for (int b = 0; b < 6; b++) {
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

#endif /* EQ_6BAND_PRESETS_H */