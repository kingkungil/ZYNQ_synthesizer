/**
 * @file    vca_compressor.h
 * @brief   VCA Compressor — Single Truth DSP Engine v1.1
 *
 * ┌─────────────────────────────────────────────────────────────┐
 * │  Single Source of Truth :  vca_state_t  (Canonical State)  │
 * │                                                             │
 * │  GR(x) = max(0, (x−T) − (x−T)/R)   [dB domain]           │
 * │                                                             │
 * │  모든 Layer는 이 State 하나를 만들거나 투영하는 변환기다.   │
 * │  Macro / Profile / Scene → vca_state_t → VCA_Commit()      │
 * └─────────────────────────────────────────────────────────────┘
 *
 * Layer 0 : Canonical State   — vca_state_t  (단일 진실)
 * Layer 1 : Raw Register      — RTL 직결 (no clamp, no meaning)
 * Layer 2 : HW Mapping        — state → register 변환기
 * Layer 3 : Time Semantic     — Attack/Release ms (τ = 2^s·ln2/Fs)
 * Layer 4 : Detector Semantic — Peak / RMS / (future) Hybrid
 * Layer 5 : Dynamic Profile   — 압축 철학 → state 생성
 * Layer 6 : Performance Macro — 실시간 의미 조작 → state 생성
 * Layer 7 : Morph             — state 간 선형 보간
 * Layer 8 : Scene Bank        — state 슬롯 저장/로드
 * Layer 9 : Meter             — [future-only] Gain Reduction / Level
 * Layer 10: Safety            — 파라미터 가드 + pop-free 갱신
 *
 * RTL : vca_compressor Rev.5  (AXI4-Lite 32b/6b, AXI4-Stream 16b mono)
 * CLK : 50 MHz  /  Fs : 48 kHz
 */

#ifndef VCA_COMPRESSOR_H
#define VCA_COMPRESSOR_H

#include <stdint.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

    /* ======================================================================== */
    /*  시스템 상수                                                              */
    /* ======================================================================== */

#ifndef VCA_CLK_HZ
#  define VCA_CLK_HZ      50000000UL
#endif
#ifndef VCA_SAMPLE_RATE
#  define VCA_SAMPLE_RATE  48000UL
#endif
#define VCA_SCENE_SLOTS    8          /**< Scene Bank 슬롯 수            */

/* ======================================================================== */
/*  Layer 0 — Canonical State  (Single Source of Truth)                    */
/* ======================================================================== */
/*                                                                           *
 *  GR(x) = max(0, (x − T) − (x − T) / R)   [dB]                          *
 *                                                                           *
 *  RULE 1: RTL은 state의 projection 결과다. state가 아니다.                *
 *  RULE 2: Macro / Profile / Scene은 state를 직접 write하지 않는다.        *
 *          반드시 vca_state_t를 만들고 VCA_Commit()을 호출한다.            *
 *  RULE 3: vca_state_t가 변하면 VCA_Commit()만 호출하면 된다.             */

    typedef struct {
        float   threshold_dbfs;   /**< T : -60 ~ 0 dBFS                       */
        float   ratio;            /**< R : 1 ~ 20, or INFINITY (limiter)       */
        float   knee_db;          /**< K : 0 ~ 12 dB (soft knee width)        */
        float   attack_ms;        /**< A : 0.1 ~ 200 ms                       */
        float   release_ms;       /**< D : 5 ~ 3000 ms                        */
        float   makeup_db;        /**< M : 0 ~ 24 dB                          */
        uint8_t detector_mode;    /**< 0 = Peak, 1 = RMS                      */
    } vca_state_t;

    /**
     * VCA_GainReduction_dB — Canonical GR curve (static model, hard knee)
     *
     *   GR(x) = max(0, (x − T) − (x − T) / R)
     *
     * @param x_dbfs   입력 레벨 (dBFS)
     * @param s        현재 canonical state
     * @return         gain reduction (dB, 양수)
     *
     * @note soft knee는 RTL이 처리. 이 함수는 GR curve 수렴 검증용 레퍼런스.
     */
    static inline float VCA_GainReduction_dB(float x_dbfs, const vca_state_t* s)
    {
        float over = x_dbfs - s->threshold_dbfs;
        if (over <= 0.0f) return 0.0f;
        float r = (s->ratio <= 1.0f) ? 1.0f : s->ratio;
        return over - over / r;            /* = over × (1 − 1/R) */
    }

    /* Forward declaration — VCA_Commit은 Layer 2 이후 정의됨 */
    static inline void VCA_Commit(uintptr_t base, const vca_state_t* s);

    /* ======================================================================== */
    /*  Layer 1 — Raw Register                                                  */
    /* ======================================================================== */
    /*  RTL 레지스터 맵 (AXI4-Lite 오프셋)                                      *
     *  0x00 reg_thresh   Q5.8  [12:0]  기본 0x0D00                            *
     *  0x04 reg_cs       Q1.15 [15:0]  기본 24576 (4:1)                       *
     *  0x08 reg_makeup   Q2.14 [15:0]  기본 0x4000 (0 dB)                     *
     *  0x0C reg_atk      [4:0]         기본 4                                  *
     *  0x10 reg_rel      [4:0]         기본 6                                  *
     *  0x14 reg_env_mode [0]   0=Peak 1=RMS                                   *
     *  0x18 reg_knee     Q5.8  [12:0]  기본 0x0200                            *
     *  0x1C reg_dither   [0]           기본 0                                  */

#define VCA_REG_THRESH    0x00u
#define VCA_REG_CS        0x04u
#define VCA_REG_MAKEUP    0x08u
#define VCA_REG_ATK       0x0Cu
#define VCA_REG_REL       0x10u
#define VCA_REG_ENV_MODE  0x14u
#define VCA_REG_KNEE      0x18u
#define VCA_REG_DITHER    0x1Cu

#define VCA_THRESH_MASK   0x00001FFFu
#define VCA_CS_MASK       0x0000FFFFu
#define VCA_MAKEUP_MASK   0x0000FFFFu
#define VCA_ATK_MASK      0x0000001Fu
#define VCA_REL_MASK      0x0000001Fu
#define VCA_ENV_MASK      0x00000001u
#define VCA_KNEE_MASK     0x00001FFFu
#define VCA_DITHER_MASK   0x00000001u

     /* RTL 기본값 */
#define VCA_DEFAULT_THRESH    0x0D00u
#define VCA_DEFAULT_CS        24576u
#define VCA_DEFAULT_MAKEUP    0x4000u
#define VCA_DEFAULT_ATK       4u
#define VCA_DEFAULT_REL       6u
#define VCA_DEFAULT_ENV_MODE  0u
#define VCA_DEFAULT_KNEE      0x0200u
#define VCA_DEFAULT_DITHER    0u

/* --- 버스 I/O ----------------------------------------------------------- */

    static inline void     VCA_WriteReg(uintptr_t base, uint32_t off, uint32_t v)
    {
        *(volatile uint32_t*)(base + off) = v;
    }

    static inline uint32_t VCA_ReadReg(uintptr_t base, uint32_t off)
    {
        return *(volatile uint32_t*)(base + off);
    }

    /* --- Raw Write / Read --------------------------------------------------- */

    static inline void VCA_WriteThresholdRaw(uintptr_t b, uint16_t v)
    {
        VCA_WriteReg(b, VCA_REG_THRESH, (uint32_t)v & VCA_THRESH_MASK);
    }

    static inline void VCA_WriteCSRaw(uintptr_t b, uint16_t v)
    {
        VCA_WriteReg(b, VCA_REG_CS, (uint32_t)v & VCA_CS_MASK);
    }

    static inline void VCA_WriteMakeupRaw(uintptr_t b, uint16_t v)
    {
        VCA_WriteReg(b, VCA_REG_MAKEUP, (uint32_t)v & VCA_MAKEUP_MASK);
    }

    static inline void VCA_WriteAttackRaw(uintptr_t b, uint8_t v)
    {
        VCA_WriteReg(b, VCA_REG_ATK, (uint32_t)v & VCA_ATK_MASK);
    }

    static inline void VCA_WriteReleaseRaw(uintptr_t b, uint8_t v)
    {
        VCA_WriteReg(b, VCA_REG_REL, (uint32_t)v & VCA_REL_MASK);
    }

    static inline void VCA_WriteEnvModeRaw(uintptr_t b, uint8_t v)
    {
        VCA_WriteReg(b, VCA_REG_ENV_MODE, (uint32_t)v & VCA_ENV_MASK);
    }

    static inline void VCA_WriteKneeRaw(uintptr_t b, uint16_t v)
    {
        VCA_WriteReg(b, VCA_REG_KNEE, (uint32_t)v & VCA_KNEE_MASK);
    }

    static inline void VCA_WriteDitherRaw(uintptr_t b, uint8_t v)
    {
        VCA_WriteReg(b, VCA_REG_DITHER, (uint32_t)v & VCA_DITHER_MASK);
    }

    static inline uint16_t VCA_ReadThresholdRaw(uintptr_t b)
    {
        return (uint16_t)(VCA_ReadReg(b, VCA_REG_THRESH) & VCA_THRESH_MASK);
    }

    static inline uint16_t VCA_ReadCSRaw(uintptr_t b)
    {
        return (uint16_t)(VCA_ReadReg(b, VCA_REG_CS) & VCA_CS_MASK);
    }

    static inline uint16_t VCA_ReadMakeupRaw(uintptr_t b)
    {
        return (uint16_t)(VCA_ReadReg(b, VCA_REG_MAKEUP) & VCA_MAKEUP_MASK);
    }

    static inline uint8_t  VCA_ReadAttackRaw(uintptr_t b)
    {
        return (uint8_t)(VCA_ReadReg(b, VCA_REG_ATK) & VCA_ATK_MASK);
    }

    static inline uint8_t  VCA_ReadReleaseRaw(uintptr_t b)
    {
        return (uint8_t)(VCA_ReadReg(b, VCA_REG_REL) & VCA_REL_MASK);
    }

    static inline uint8_t  VCA_ReadEnvModeRaw(uintptr_t b)
    {
        return (uint8_t)(VCA_ReadReg(b, VCA_REG_ENV_MODE) & VCA_ENV_MASK);
    }

    static inline uint16_t VCA_ReadKneeRaw(uintptr_t b)
    {
        return (uint16_t)(VCA_ReadReg(b, VCA_REG_KNEE) & VCA_KNEE_MASK);
    }

    static inline uint8_t  VCA_ReadDitherRaw(uintptr_t b)
    {
        return (uint8_t)(VCA_ReadReg(b, VCA_REG_DITHER) & VCA_DITHER_MASK);
    }

    /* ======================================================================== */
    /*  Layer 2 — HW Mapping  (state → register 변환기)                        */
    /* ======================================================================== */
    /*  모든 변환 방향:  vca_state_t  →  RTL register                          *
     *                                                                           *
     *  Threshold : dBFS → Q5.8  :  val = (T/6.0206 + 23) × 256               *
     *  Ratio     : R    → CS    :  CS  = (1 − 1/R) × 32768                    *
     *  Makeup    : dB   → Q2.14 :  lin = 10^(M/20) × 16384                    *
     *  Knee      : dB   → Q5.8  :  동일 포맷 (log2 domain)                    *
     *  Time      : ms   → shift :  Layer 3에서 처리                           */

     /* --- Threshold --------------------------------------------------------- */
     /*  RTL internal representation:  cur_log ≈ log2(|x|) × 256              *
      *    0 dBFS = log2(2^23) × 256 = 5888  → offset = 23 octaves            *
      *                                                                          *
      *  VCA_DBFS_Q58_OFFSET = 23.0  (RTL fixed-point origin, NOT headroom)   *
      *                                                                          *
      *  dBFS → Q5.8:  q = (dbfs/6.0206 + VCA_DBFS_Q58_OFFSET) × 256         *
      *  Q5.8 → dBFS:  dbfs = (q/256 − VCA_DBFS_Q58_OFFSET) × 6.0206         *
      *                                                                          *
      *  유효 범위: q ∈ [0, 0x1FFF]  →  dBFS ∈ [−138, +18] (실용: −60~0)    */

#define VCA_DBFS_Q58_OFFSET  23.0f    /**< RTL log2 origin (octave offset) */
#define VCA_LOG2_PER_DB      (1.0f / 6.0206f)   /**< log2 steps per dB   */

    static inline uint16_t VCA_ThresholdReg_From_dBFS(float dbfs)
    {
        if (dbfs > 0.0f) dbfs = 0.0f;
        if (dbfs < -60.0f) dbfs = -60.0f;
        int q = (int)((dbfs * VCA_LOG2_PER_DB + VCA_DBFS_Q58_OFFSET) * 256.0f + 0.5f);
        return (uint16_t)(q < 0 ? 0 : (q > 0x1FFF ? 0x1FFF : q));
    }

    static inline float VCA_ThresholdReg_To_dBFS(uint16_t reg)
    {
        return ((float)reg / 256.0f - VCA_DBFS_Q58_OFFSET) / VCA_LOG2_PER_DB;
    }

    static inline void  VCA_SetThresholdDb(uintptr_t b, float dbfs)
    {
        VCA_WriteThresholdRaw(b, VCA_ThresholdReg_From_dBFS(dbfs));
    }

    static inline float VCA_GetThresholdDb(uintptr_t b)
    {
        return VCA_ThresholdReg_To_dBFS(VCA_ReadThresholdRaw(b));
    }

    /* --- Ratio ------------------------------------------------------------- */
    /*  R → CS :  CS = (1 − 1/R) × 32768                                      *
     *  CS → R :  R  = 1 / (1 − CS/32768)                                      *
     *                                                                           *
     *  경계 처리:                                                               *
     *    CS ≥ VCA_CS_LIMIT  → R = INFINITY  (hard clamp, binary limiter)      *
     *    denominator < 1e-6 → R = INFINITY  (epsilon guard, blow-up 방지)     */

#define VCA_CS_LIMIT         32767u         /**< limiter threshold (CS)     */
     /** limiter 판정 매크로 — float 비교 대신 flag로 사용
      *  ratio=20:1은 유효 일반 ratio이므로 임계값을 20.5 이상으로 설정 */
#define VCA_RATIO_IS_LIMITER(r)  ((r) > 20.5f)

    static inline uint16_t VCA_RatioReg_From_Float(float r)
    {
        if (VCA_RATIO_IS_LIMITER(r)) return (uint16_t)VCA_CS_LIMIT;
        if (r < 1.0f) r = 1.0f;
        return (uint16_t)((1.0f - 1.0f / r) * 32768.0f + 0.5f);
    }

    /** ratio = ∞ 표준 심볼 — SetLimiterRatio / GetRatio 반환값 일치 */
#define VCA_RATIO_INFINITY  INFINITY

/* CS 정수 상수 (참조용) */
#define VCA_CS_1_1   0u
#define VCA_CS_2_1   16384u
#define VCA_CS_4_1   24576u
#define VCA_CS_8_1   28672u
#define VCA_CS_20_1  31130u
#define VCA_CS_INF   VCA_CS_LIMIT

    static inline float VCA_RatioReg_To_Float(uint16_t reg)
    {
        if (reg >= VCA_CS_LIMIT) return VCA_RATIO_INFINITY;   /* hard clamp   */
        float s = (float)reg / 32768.0f;
        float denom = 1.0f - s;
        if (denom < 1e-6f) return VCA_RATIO_INFINITY;         /* epsilon guard */
        return 1.0f / denom;
    }

    static inline void  VCA_SetRatio(uintptr_t b, float ratio)
    {
        VCA_WriteCSRaw(b, VCA_RatioReg_From_Float(ratio));
    }

    static inline float VCA_GetRatio(uintptr_t b)
    {
        return VCA_RatioReg_To_Float(VCA_ReadCSRaw(b));
    }

    /** ratio = ∞ semantic — brickwall limiter slope */
    static inline void VCA_SetLimiterRatio(uintptr_t b)
    {
        VCA_WriteCSRaw(b, VCA_CS_INF);
    }   /* CS = 32767 ≈ slope 1.0 */

/* --- Makeup ------------------------------------------------------------ */

    static inline uint16_t VCA_MakeupReg_From_dB(float db)
    {
        if (db < 0.0f) db = 0.0f;
        if (db > 24.0f) db = 24.0f;
        int v = (int)(powf(10.0f, db / 20.0f) * 16384.0f + 0.5f);
        return (uint16_t)(v > 0xFFFF ? 0xFFFF : v);
    }

    static inline float VCA_MakeupReg_To_dB(uint16_t reg)
    {
        return reg == 0u ? -96.0f : 20.0f * log10f((float)reg / 16384.0f);
    }

    static inline void  VCA_SetMakeupDb(uintptr_t b, float db)
    {
        VCA_WriteMakeupRaw(b, VCA_MakeupReg_From_dB(db));
    }

    static inline float VCA_GetMakeupDb(uintptr_t b)
    {
        return VCA_MakeupReg_To_dB(VCA_ReadMakeupRaw(b));
    }

    /* --- Knee -------------------------------------------------------------- */

    static inline uint16_t VCA_KneeReg_From_dB(float db)
    {
        if (db < 0.0f) db = 0.0f;
        if (db > 12.0f) db = 12.0f;
        int v = (int)(db / 6.0206f * 256.0f + 0.5f);
        return (uint16_t)(v > 0x1FFF ? 0x1FFF : v);
    }

    static inline float VCA_KneeReg_To_dB(uint16_t reg)
    {
        return (float)reg / 256.0f * 6.0206f;
    }

    static inline void  VCA_SetKneeDb(uintptr_t b, float db)
    {
        VCA_WriteKneeRaw(b, VCA_KneeReg_From_dB(db));
    }

    static inline float VCA_GetKneeDb(uintptr_t b)
    {
        return VCA_KneeReg_To_dB(VCA_ReadKneeRaw(b));
    }

    /* Forward declaration — Layer 3에서 정의됨 */
    static inline uint8_t vca_ms2shift_(float ms, uint32_t fs);

    /* ----------------------------------------------------------------------- */
    /*  VCA_Commit — Canonical State → RTL (단일 진입점)                       */
    /*                                                                           *
     *  모든 상위 Layer(Profile / Macro / Scene / Morph)의 최종 출력.           *
     *  이 함수만이 RTL 레지스터를 의미 있는 방식으로 갱신한다.                 */
     /* ----------------------------------------------------------------------- */
    static inline void VCA_Commit(uintptr_t base, const vca_state_t* s)
    {
        /* Threshold : T (dBFS → Q5.8) */
        VCA_WriteThresholdRaw(base, VCA_ThresholdReg_From_dBFS(s->threshold_dbfs));

        /* Ratio : R → CS
         *   float INF 비교 대신 VCA_RATIO_IS_LIMITER 매크로로 결정론적 판정 */
        if (VCA_RATIO_IS_LIMITER(s->ratio))
            VCA_WriteCSRaw(base, VCA_CS_INF);
        else
            VCA_WriteCSRaw(base, VCA_RatioReg_From_Float(s->ratio));

        /* Makeup : M (dB → Q2.14) */
        VCA_WriteMakeupRaw(base, VCA_MakeupReg_From_dB(s->makeup_db));

        /* Knee : K (dB → Q5.8) */
        VCA_WriteKneeRaw(base, VCA_KneeReg_From_dB(s->knee_db));

        /* Attack : ms → shift (canonical ms → RTL projection) */
        {
            float a = s->attack_ms;
            if (a < 0.1f) a = 0.1f;
            if (a > 200.0f) a = 200.0f;
            VCA_WriteAttackRaw(base, vca_ms2shift_(a, VCA_SAMPLE_RATE));
        }

        /* Release : ms → shift */
        {
            float d = s->release_ms;
            if (d < 5.0f) d = 5.0f;
            if (d > 3000.0f) d = 3000.0f;
            VCA_WriteReleaseRaw(base, vca_ms2shift_(d, VCA_SAMPLE_RATE));
        }

        /* Detector mode */
        VCA_WriteEnvModeRaw(base, s->detector_mode & 1u);
    }

    /* ======================================================================== */
    /*  Layer 3 — Time Semantic                                                 */
    /* ======================================================================== */
    /*  RTL IIR:  env += (target − env) >> shift                               *
     *                                                                           *
     *  정확한 시정수:  τ = 2^shift × ln2 / Fs                                  *
     *    (단순 2^shift/Fs 대비 ~30% 오차 수정)                                 *
     *                                                                           *
     *  shift2ms:  τ_ms = 2^shift × ln2 × 1000 / Fs                            *
     *  ms2shift:  shift = round(log2(ms × Fs / (1000 × ln2)))                 *
     *                                                                           *
     *  ms ↔ shift는 rounding loss 존재 (단일 진실 깨짐 방지):                  *
     *    state에는 ms 저장 (canonical),                                        *
     *    commit 시 shift 생성 (projection),                                    *
     *    read-back 시 ms_approx 유지 (vca_time_pair_t).                       *
     *                                                                           *
     *  클램프 범위:  Attack 0.1~200 ms  /  Release 5~3000 ms                  */

#define VCA_LN2  0.693147181f   /**< ln(2) — τ 보정 상수 */

     /**
      * vca_time_pair_t — ms (canonical) + shift (RTL projection) 동시 보유
      *
      * Morph 시 ms 도메인에서 선형 보간 → drift 없음.
      * read-back 시 shift → ms_approx 복원으로 round-trip 근사 유지.
      */
    typedef struct {
        float   ms_approx;   /**< canonical ms (state에 저장, 보간 기준)   */
        uint8_t shift;       /**< RTL projection (commit 시 갱신)           */
    } vca_time_pair_t;

    static inline float vca_shift2ms_(uint8_t s, uint32_t fs)
    {
        if (!s) s = 1u; return (float)(1UL << s) * VCA_LN2 * 1000.0f / (float)fs;
    }

    static inline uint8_t vca_ms2shift_(float ms, uint32_t fs)
    {
        if (ms <= 0.0f) return 1u;
        int s = (int)(log2f(ms * (float)fs / (1000.0f * VCA_LN2)) + 0.5f);
        return (uint8_t)(s < 1 ? 1 : (s > 31 ? 31 : s));
    }

    /** ms → vca_time_pair_t 생성 (canonical + projection 동시) */
    static inline vca_time_pair_t vca_time_from_ms_(float ms, uint32_t fs)
    {
        vca_time_pair_t p;
        p.shift = vca_ms2shift_(ms, fs);
        p.ms_approx = vca_shift2ms_(p.shift, fs);  /* round-trip 근사 보존 */
        return p;
    }

    static inline void  VCA_SetAttackMs(uintptr_t b, float ms)
    {
        if (ms < 0.1f) ms = 0.1f;
        if (ms > 200.0f) ms = 200.0f;
        VCA_WriteAttackRaw(b, vca_ms2shift_(ms, VCA_SAMPLE_RATE));
    }

    static inline float VCA_GetAttackMs(uintptr_t b)
    {
        return vca_shift2ms_(VCA_ReadAttackRaw(b), VCA_SAMPLE_RATE);
    }

    static inline void  VCA_SetReleaseMs(uintptr_t b, float ms)
    {
        if (ms < 5.0f) ms = 5.0f;
        if (ms > 3000.0f) ms = 3000.0f;
        VCA_WriteReleaseRaw(b, vca_ms2shift_(ms, VCA_SAMPLE_RATE));
    }

    static inline float VCA_GetReleaseMs(uintptr_t b)
    {
        return vca_shift2ms_(VCA_ReadReleaseRaw(b), VCA_SAMPLE_RATE);
    }

    /* ======================================================================== */
    /*  Layer 4 — Detector Semantic                                             */
    /* ======================================================================== */
    /*  RTL 지원:  Peak(0), RMS(1)                                              *
     *  future:    Hybrid                                                        *
     *                                                                           *
     *  Detector layer는 env_mode만 제어한다.                                   *
     *  Attack/Release 시간은 Layer 3 (Time Semantic) 에서 별도 설정할 것.      */

#define VCA_DETECTOR_PEAK   0u
#define VCA_DETECTOR_RMS    1u

     /** Peak detector 활성화 — 시간 파라미터는 SetAttackMs/SetReleaseMs로 별도 설정 */
    static inline void VCA_SetDetectorPeak(uintptr_t b)
    {
        VCA_WriteEnvModeRaw(b, VCA_DETECTOR_PEAK);
    }

    /** RMS detector 활성화 */
    static inline void VCA_SetDetectorRMS(uintptr_t b)
    {
        VCA_WriteEnvModeRaw(b, VCA_DETECTOR_RMS);
    }

    /** @note VCA_SetDetectorHybrid — future-only, RTL 미지원 */
    /* static inline void VCA_SetDetectorHybrid(uintptr_t b) { (void)b; } */

    /* ======================================================================== */
    /*  Layer 5 — Dynamic Profile                                               */
    /* ======================================================================== */

    /**
     * vca_profile_t — vca_state_t의 alias (하위 호환)
     *
     * 기존 코드는 vca_profile_t를 계속 사용 가능.
     * 내부 표현은 vca_state_t와 동일하다.
     */
    typedef vca_state_t vca_profile_t;

    /* --- 프로파일 정의  (vca_state_t 필드 순서: T, R, K, A, D, M, det) ----- */

    /** 투명한 버스 글루 — 음색 중립 */
#define VCA_PROFILE_CLEAN    { -10.0f,  2.0f,  4.0f, 20.0f,  300.0f,  0.0f, VCA_DETECTOR_RMS  }
/** 어택 부각 — 타악기, 드럼 버스 */
#define VCA_PROFILE_PUNCH    { -18.0f,  4.0f,  2.0f, 10.0f,   80.0f,  2.0f, VCA_DETECTOR_PEAK }
/** 느린 글루 — 버스, 마스터 */
#define VCA_PROFILE_GLUE     { -20.0f,  2.0f,  6.0f, 30.0f,  250.0f,  2.0f, VCA_DETECTOR_RMS  }
/** 서스테인 연장 — 기타, 키보드 */
#define VCA_PROFILE_SUSTAIN  { -24.0f,  4.0f,  5.0f,  2.0f,  400.0f,  3.0f, VCA_DETECTOR_RMS  }
/** 과감한 압축 — 빈티지 느낌 */
#define VCA_PROFILE_SMASH    { -30.0f,  8.0f,  1.0f,  0.5f,   50.0f,  4.0f, VCA_DETECTOR_PEAK }
/** 리미터 프리셋 */
#define VCA_PROFILE_LIMITER  {  -3.0f, 20.0f,  0.5f,  0.1f,   50.0f,  0.0f, VCA_DETECTOR_PEAK }
/** 패러렐 압축 베이스 */
#define VCA_PROFILE_PARALLEL { -30.0f,  4.0f,  3.0f,  5.0f,  200.0f,  6.0f, VCA_DETECTOR_RMS  }
/** Neutral — bypass 대체. Morph/Scene 용도로 구조체 형태 제공 */
#define VCA_PROFILE_NEUTRAL  {   0.0f,  1.0f,  0.0f,  0.1f,    5.0f,  0.0f, VCA_DETECTOR_PEAK }

/* --- SetNeutral / SetLimiter — Commit 경유 ------------------------------ */

/**
 * VCA_SetNeutral — 공식 bypass 대체
 * threshold=0dBFS, ratio=1:1, makeup=0dB, knee=0dB, fastest time
 * 정확한 투명 bypass가 아니라 중립 dynamics path임에 주의.
 */
    static inline void VCA_SetNeutral(uintptr_t b)
    {
        vca_state_t s = VCA_PROFILE_NEUTRAL; VCA_Commit(b, &s);
    }

    /**
     * VCA_SetLimiter — brickwall-like semantic
     * ratio=∞, attack=minimum, release=fast, knee=soft
     */
    static inline void VCA_SetLimiter(uintptr_t b)
    {
        vca_state_t s = VCA_PROFILE_LIMITER; VCA_Commit(b, &s);
    }

    /* --- VCA_LoadProfile — Commit 경유 (단일 진입점 보장) ------------------- */

    static inline void VCA_LoadProfile(uintptr_t b, const vca_state_t* s)
    {
        VCA_Commit(b, s);
    }

    /* --- Profile 명명 편의 -------------------------------------------------- */

    static inline void VCA_ProfileClean(uintptr_t b)
    {
        vca_state_t s = VCA_PROFILE_CLEAN;    VCA_Commit(b, &s);
    }

    static inline void VCA_ProfilePunch(uintptr_t b)
    {
        vca_state_t s = VCA_PROFILE_PUNCH;    VCA_Commit(b, &s);
    }

    static inline void VCA_ProfileGlue(uintptr_t b)
    {
        vca_state_t s = VCA_PROFILE_GLUE;     VCA_Commit(b, &s);
    }

    static inline void VCA_ProfileSustain(uintptr_t b)
    {
        vca_state_t s = VCA_PROFILE_SUSTAIN;  VCA_Commit(b, &s);
    }

    static inline void VCA_ProfileSmash(uintptr_t b)
    {
        vca_state_t s = VCA_PROFILE_SMASH;    VCA_Commit(b, &s);
    }

    static inline void VCA_ProfileLimiter(uintptr_t b)
    {
        VCA_SetLimiter(b);
    }

    static inline void VCA_ProfileParallel(uintptr_t b)
    {
        vca_state_t s = VCA_PROFILE_PARALLEL; VCA_Commit(b, &s);
    }

    static inline void VCA_ProfileNeutral(uintptr_t b)
    {
        VCA_SetNeutral(b);
    }

    /* ======================================================================== */
    /*  Layer 6 — Performance Macro                                             */
    /* ======================================================================== */
    /*  amount : 0.0 (neutral) ~ 1.0 (maximum effect)                          */

    static inline float vca_lerp_(float a, float b, float t) { return a + (b - a) * t; }
    static inline float vca_clamp_(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    /**
     * VCA_MacroPunch — 어택 강조, 펀치감
     * 변경: threshold↓, attack↓, release↓, ratio↑
     */
    static inline void VCA_MacroPunch(uintptr_t b, float amount)
    {
        float t = vca_clamp_(amount, 0.0f, 1.0f);
        vca_state_t s = {
            vca_lerp_(-10.0f, -30.0f, t),   /* T */
            vca_lerp_(2.0f,   8.0f, t),   /* R */
            vca_lerp_(4.0f,   1.0f, t),   /* K */
            vca_lerp_(20.0f,   0.5f, t),   /* A */
            vca_lerp_(300.0f,  60.0f, t),   /* D */
            vca_lerp_(1.0f,   6.0f, t),   /* M */
            VCA_DETECTOR_PEAK
        };
        VCA_Commit(b, &s);
    }

    /**
     * VCA_MacroGlue — 버스 결속, 글루
     * 변경: threshold↓, ratio↑, knee↑
     */
    static inline void VCA_MacroGlue(uintptr_t b, float amount)
    {
        float t = vca_clamp_(amount, 0.0f, 1.0f);
        vca_state_t s = {
            vca_lerp_(-10.0f, -24.0f, t),   /* T */
            vca_lerp_(1.5f,   3.0f, t),   /* R */
            vca_lerp_(4.0f,  10.0f, t),   /* K */
            vca_lerp_(30.0f,  50.0f, t),   /* A */
            vca_lerp_(250.0f, 400.0f, t),   /* D */
            vca_lerp_(0.5f,   3.0f, t),   /* M */
            VCA_DETECTOR_RMS
        };
        VCA_Commit(b, &s);
    }

    /**
     * VCA_MacroDensity — 밀도/두께
     * 변경: threshold↓, makeup↑
     */
    static inline void VCA_MacroDensity(uintptr_t b, float amount)
    {
        float t = vca_clamp_(amount, 0.0f, 1.0f);
        vca_state_t s = {
            vca_lerp_(-10.0f, -30.0f, t),   /* T */
            2.0f,                            /* R (고정) */
            2.0f,                            /* K (고정) */
            20.0f,                           /* A (고정) */
            300.0f,                          /* D (고정) */
            vca_lerp_(0.0f,   8.0f, t),   /* M */
            VCA_DETECTOR_RMS
        };
        VCA_Commit(b, &s);
    }

    /**
     * VCA_MacroSustain — 어택 뒤 꼬리 연장
     * 변경: release↑, makeup↑
     */
    static inline void VCA_MacroSustain(uintptr_t b, float amount)
    {
        float t = vca_clamp_(amount, 0.0f, 1.0f);
        vca_state_t s = {
            -18.0f,                          /* T (고정) */
            2.0f,                            /* R (고정) */
            4.0f,                            /* K (고정) */
            20.0f,                           /* A (고정) */
            vca_lerp_(300.0f, 3000.0f, t),  /* D */
            vca_lerp_(0.0f,    5.0f, t),  /* M */
            VCA_DETECTOR_RMS
        };
        VCA_Commit(b, &s);
    }

    /**
     * VCA_MacroLevel — Makeup 단독 조절
     * 변경: makeup only
     */
    static inline void VCA_MacroLevel(uintptr_t b, float db)
    {
        VCA_SetMakeupDb(b, vca_clamp_(db, 0.0f, 24.0f));
    }

    /* ======================================================================== */
    /*  Layer 7 — Morph                                                         */
    /* ======================================================================== */

    /**
     * VCA_Morph — 두 state 사이 선형 보간 후 즉시 Commit
     *
     * - ratio: ms 도메인에서 보간 → Morph drift 없음
     * - detector_mode: roundf 보간 → hard switch 클릭 제거
     *
     * @param t  0.0 = a, 1.0 = b
     */
    static inline void VCA_Morph(uintptr_t b,
        const vca_state_t* a,
        const vca_state_t* bst,
        float t)
    {
        t = vca_clamp_(t, 0.0f, 1.0f);
        vca_state_t m = {
            vca_lerp_(a->threshold_dbfs, bst->threshold_dbfs, t),  /* T */
            vca_lerp_(a->ratio,          bst->ratio,          t),  /* R */
            vca_lerp_(a->knee_db,        bst->knee_db,        t),  /* K */
            vca_lerp_(a->attack_ms,      bst->attack_ms,      t),  /* A */
            vca_lerp_(a->release_ms,     bst->release_ms,     t),  /* D */
            vca_lerp_(a->makeup_db,      bst->makeup_db,      t),  /* M */
            /* detector: step switch 제거 — roundf 선형 보간 */
            (uint8_t)roundf(vca_lerp_((float)a->detector_mode,
                                       (float)bst->detector_mode, t))
        };
        VCA_Commit(b, &m);
    }

    /* ======================================================================== */
    /*  Layer 8 — Scene Bank                                                    */
    /* ======================================================================== */
    /*  Scene = 슬롯 기반 프리셋 Bank.  Automation 아님.                        *
     *  VCA_SCENE_SLOTS 슬롯 (기본 8). 소프트웨어 RAM — RTL 저장 아님.         */

    typedef struct {
        vca_profile_t slot[VCA_SCENE_SLOTS];
        uint8_t       valid[VCA_SCENE_SLOTS];
    } vca_scene_bank_t;

    /** VCA_InitSceneBank — Bank 사용 전 필수 호출. valid[] 미초기화 방지. */
    static inline void VCA_InitSceneBank(vca_scene_bank_t* bank)
    {
        memset(bank, 0, sizeof(vca_scene_bank_t));
    }

    static inline void VCA_SaveScene(vca_scene_bank_t* bank, uint8_t idx,
        const vca_profile_t* p)
    {
        if (idx >= VCA_SCENE_SLOTS) return;
        bank->slot[idx] = *p;
        bank->valid[idx] = 1u;
    }

    static inline int VCA_LoadScene(uintptr_t b, const vca_scene_bank_t* bank, uint8_t idx)
    {
        if (idx >= VCA_SCENE_SLOTS || !bank->valid[idx]) return -1;
        VCA_LoadProfile(b, &bank->slot[idx]);
        return 0;
    }

    /**
     * VCA_MorphToScene — 현재 하드웨어 상태 → 저장된 Scene 보간 적용
     * @param t  0.0 = 현재, 1.0 = Scene
     */
    static inline int VCA_MorphToScene(uintptr_t b, const vca_scene_bank_t* bank,
        uint8_t idx, float t,
        const vca_profile_t* current)
    {
        if (idx >= VCA_SCENE_SLOTS || !bank->valid[idx]) return -1;
        VCA_Morph(b, current, &bank->slot[idx], t);
        return 0;
    }

    /* ======================================================================== */
    /*  Layer 9 — Meter  [future-only — RTL 미지원]                            */
    /* ======================================================================== */
    /*  현재 RTL에 메터 레지스터 없음.                                           *
     *  VCA_ENABLE_METER=1 정의 시에만 컴파일. 기본 비활성화.                   */

#define VCA_REG_GR_METER   0x20u   /**< Gain Reduction Q5.8  [예약] */
#define VCA_REG_IN_LEVEL   0x24u   /**< Input Level    Q5.8  [예약] */
#define VCA_REG_OUT_LEVEL  0x28u   /**< Output Level   Q5.8  [예약] */

#if defined(VCA_ENABLE_METER) && VCA_ENABLE_METER

    static inline float VCA_GetGainReduction(uintptr_t b)
    {
        return (float)(VCA_ReadReg(b, VCA_REG_GR_METER) & VCA_THRESH_MASK) / 256.0f * 6.0206f;
    }

    static inline float VCA_GetInputLevel(uintptr_t b)
    {
        return (float)(VCA_ReadReg(b, VCA_REG_IN_LEVEL) & VCA_THRESH_MASK) / 256.0f * 6.0206f;
    }

    static inline float VCA_GetOutputLevel(uintptr_t b)
    {
        return (float)(VCA_ReadReg(b, VCA_REG_OUT_LEVEL) & VCA_THRESH_MASK) / 256.0f * 6.0206f;
    }

#endif /* VCA_ENABLE_METER */

    /* ======================================================================== */
    /*  Layer 10 — Safety                                                       */
    /* ======================================================================== */

    /** VCA_ClampThreshold — Threshold를 유효 범위로 클램프 */
    static inline float VCA_ClampThreshold(float dbfs)
    {
        return vca_clamp_(dbfs, -60.0f, 0.0f);
    }

    /** VCA_ClampRatio — Ratio를 유효 범위로 클램프 */
    static inline float VCA_ClampRatio(float r)
    {
        return vca_clamp_(r, 1.0f, 20.0f);
    }

    /** VCA_ClampTime — Attack/Release ms 클램프 (is_attack: 1=attack, 0=release) */
    static inline float VCA_ClampTime(float ms, int is_attack)
    {
        if (is_attack) return vca_clamp_(ms, 0.1f, 200.0f);
        else           return vca_clamp_(ms, 5.0f, 3000.0f);
    }

    /** VCA_ValidateProfile — state 전체 클램프 (in-place), VCA_Commit 전 호출 */
    static inline void VCA_ValidateProfile(vca_state_t* s)
    {
        s->threshold_dbfs = VCA_ClampThreshold(s->threshold_dbfs);
        s->ratio = VCA_ClampRatio(s->ratio);
        s->makeup_db = vca_clamp_(s->makeup_db, 0.0f, 24.0f);
        s->attack_ms = VCA_ClampTime(s->attack_ms, 1);
        s->release_ms = VCA_ClampTime(s->release_ms, 0);
        s->knee_db = vca_clamp_(s->knee_db, 0.0f, 12.0f);
        s->detector_mode &= 1u;
    }

    /** VCA_SafeLoadProfile — 클램프 후 Commit */
    static inline void VCA_SafeLoadProfile(uintptr_t b, vca_state_t* s)
    {
        VCA_ValidateProfile(s); VCA_Commit(b, s);
    }

    /**
     * VCA_SmoothWrite — 단일 레지스터 pop-free 스텝 갱신
     *
     * 허용 레지스터: THRESH, CS, MAKEUP, ATK, REL, KNEE
     * ENV_MODE, DITHER는 불연속 값이므로 SmoothWrite 대상 아님.
     * 허용 목록 외 reg는 무시 (Safety Layer 보호).
     *
     * @param dfn  딜레이 콜백 (NULL 허용)
     */
    typedef void (*vca_delay_fn_t)(uint32_t us);

    static inline int vca_smooth_reg_allowed_(uint32_t reg)
    {
        return (reg == VCA_REG_THRESH ||
            reg == VCA_REG_CS ||
            reg == VCA_REG_MAKEUP ||
            reg == VCA_REG_ATK ||
            reg == VCA_REG_REL ||
            reg == VCA_REG_KNEE);
    }

    static inline void VCA_SmoothWrite(uintptr_t base, uint32_t reg,
        uint32_t cur, uint32_t tgt,
        uint32_t steps, vca_delay_fn_t dfn)
    {
        if (!vca_smooth_reg_allowed_(reg)) return;   /* whitelist guard */
        if (!steps) steps = 1u;
        int32_t d = (int32_t)tgt - (int32_t)cur;
        for (uint32_t i = 1u; i <= steps; i++) {
            VCA_WriteReg(base, reg,
                (uint32_t)((int32_t)cur + d * (int32_t)i / (int32_t)steps));
            if (dfn && i < steps) dfn(50u);
        }
    }

    /* ======================================================================== */
    /*  초기화                                                                   */
    /* ======================================================================== */

    /** VCA_Init — RTL 기본값으로 전체 레지스터 초기화 */
    static inline void VCA_Init(uintptr_t b)
    {
        VCA_WriteThresholdRaw(b, (uint16_t)VCA_DEFAULT_THRESH);
        VCA_WriteCSRaw(b, (uint16_t)VCA_DEFAULT_CS);
        VCA_WriteMakeupRaw(b, (uint16_t)VCA_DEFAULT_MAKEUP);
        VCA_WriteAttackRaw(b, (uint8_t)VCA_DEFAULT_ATK);
        VCA_WriteReleaseRaw(b, (uint8_t)VCA_DEFAULT_REL);
        VCA_WriteEnvModeRaw(b, (uint8_t)VCA_DEFAULT_ENV_MODE);
        VCA_WriteKneeRaw(b, (uint16_t)VCA_DEFAULT_KNEE);
        VCA_WriteDitherRaw(b, (uint8_t)VCA_DEFAULT_DITHER);
    }

#ifdef __cplusplus
}
#endif
#endif /* VCA_COMPRESSOR_H */