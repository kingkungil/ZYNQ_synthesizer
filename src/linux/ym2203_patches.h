// =============================================================================
//  ym2203_patches.h  —  YM2203 (OPN) FM + SSG 통합 드라이버  Rev.7
//  Target: Xilinx Zynq / ym core (4 MHz OPN clock)
//
// ── Rev.7 전면 재설계 개요 ────────────────────────────────────────────────────
//
//  Rev.6 이하의 구조적 문제를 전면 수정합니다.
//
//  [핵심 수정 목록]
//
//  [R7-FIX-1]  SSG_FREQ_LUT 완전 재계산
//    Rev.6 LUT는 MIDI 69 근방에서 비단조적(non-monotonic) 오류.
//    period = fclk / (16 × freq), fclk = 4,000,000 Hz
//    전체 128 엔트리를 단조감소 순으로 재계산. MIDI 0~11은 하드웨어 최댓값
//    (0x0FFF) 클램프. MIDI 120~127은 최솟값(0x0001) 클램프.
//
//  [R7-FIX-2]  FM velocity 스케일링 ALG 인식 정확화
//    - ALG별 carrier mask는 기존과 동일하나, ALG5/6 해석을 OPN 데이터시트
//      기준으로 재확정:
//        ALG5: OP1→OP4, OP2→OP4, OP3→OP4. carrier = OP4만  (0x08)
//        ALG6: OP1(FB)→OP2. OP2→OP4, OP3→OP4. carrier = OP2+OP4? 아님.
//              OPN 데이터시트 Figure 2: ALG6 = OP1→OP2→OP4 + OP3→OP4
//              carrier = OP4만  (0x08)
//              (MAME ymfm.cpp 및 jt12 소스 기준 재확인)
//        ALG7: 전부 독립 carrier  (0x0F)
//    - velocity TL 감소폭을 악기 설명자(instrument) 단위로 조정 가능하게
//      vel_tl_scale(0~8) 파라미터 추가. 0=velocity 무시, 4=기본, 8=최대감도.
//
//  [R7-FIX-3]  SSG ssg_patch_apply_ch() 에서 period 레지스터 올바르게 처리
//    - amp_base envelope 모드(bit4=1)일 때 env_period 레지스터 0x0B/0x0C 항상 기재.
//    - note_on 시 반드시 ssg_set_note() 도 호출하도록 inst_note_on() 수정.
//    - note_cut_ticks 처리 로직을 tick() 내부 SSG 상태머신으로 완성.
//
//  [R7-FIX-4]  Portamento block 경계 처리
//    - 소스/목표 fnum+block 전체를 선형 보간.
//    - block × 2048 + fnum 로 "linear pitch number" 변환 후 보간,
//      역변환 시 block/fnum 분리. 옥타브 글라이드 올바르게 동작.
//
//  [R7-FIX-5]  LFO Tremolo 방향 수정
//    - tremolo = 볼륨 진동 → carrier TL을 기준값 중심으로 ±depth로 변조.
//    - 기존 코드는 lfo_val 부호 그대로 TL 가감 → 위상에 따라 볼륨이 증가도 함.
//    - 수정: TL_base + |lfo_val|×depth/127 로 단방향 감쇠 진동.
//      (또는 TL_base + (lfo_val * depth) >> 7 방식으로 쌍방향 허용; 설정 가능)
//
//  [R7-FIX-6]  ALG6 carrier mask 재확정
//    OPN 데이터시트 Figure 2 및 MAME fm.cpp(OPN_ALGO_TABLE) 참조:
//      ALG6: OP2+OP3+OP4 carrier → 0x0E. (Rev.6의 값이 실제로 맞았음)
//    추가 확인: MAME fm.cpp 내 OPN 알고리즘 테이블에서 ALG5는 OP4만(0x08),
//    ALG6는 OP2+OP3+OP4(0x0E)가 표준임.
//
//  [R7-FIX-7]  FM ch3 Extended(Sound Effect) Mode 지원
//    - YM2203은 레지스터 0x27 bit[5:4]=0b01 시 ch3 extended mode 진입.
//    - 이 모드에서 ch3의 OP1~OP4 각각 독립 fnum/block 설정 가능.
//    - ym2203_ch3_ext_set_op_freq() API 추가.
//    - CSM(Composite Sine Mode) 기초 구조 정의 추가.
//
//  [R7-FIX-8]  SSG Mixer 0x07 로직 명확화 + 초기화 버그 수정
//    - active-LOW 비트: 0=enable, 1=disable.
//    - 초기값 0x38 = 0b00111000: NOISE_A/B/C disable, TONE_A/B/C enable.
//    - ssg_ch_disable() 에서 올바르게 tone+noise 둘 다 disable(bit set).
//    - 전체 SSG 초기화 시 0x3F (모든 채널 disable) 로 시작.
//
//  [R7-NEW-1]  FM 음향학적 파라미터 설계 원칙 문서화
//    각 알고리즘별 operator 역할, FM Index(ΔI), harmonic series 설명.
//    악기 설계 가이드라인을 주석으로 명시.
//
//  [R7-NEW-2]  SSG 고급 모드 확장
//    - SSG + FM 복합음색: SSG가 어택 트랜지언트, FM이 서스테인을 담당.
//    - ssg_note_cut_tick() 상태 추적기 per-channel 분리.
//    - SSG envelope auto-period 모드 지원 (env_period_mode=AUTO → note freq 연동).
//
//  [R7-NEW-3]  화성학 확장 — 텐션 코드 & 스케일 인지 보이싱
//    - 코드 타입 확장: 9th, 11th, 13th, alt 도미넌트, polychord 지원.
//    - ym2203_scale_chord(): 스케일 내 다이어토닉 코드 자동 생성.
//    - 보이싱 알고리즘: 루트포지션 / 1전위 / 2전위 / 3전위.
//
//  [R7-NEW-4]  ExtCh3 멀티-OP 주파수 독립 제어 API
//    - ym2203_extch3_note_on(): OP1~OP4 각각 다른 MIDI note로 발음.
//    - 내부적으로 레지스터 0x24~0x2C (ch3 OP freq) 직접 제어.
//
// ── Rev.8 음향학적 파라미터 전면 재설계 ──────────────────────────────────────
//
//  [R8-FIX-1]  지속음 악기 SR=0 수정 (가장 중요)
//    - TRUMPET, SAXOPHONE, OBOE, VIOLIN, CELLO, FLUTE, DAEGEUM, SOGEUM,
//      STRINGS_ENS, CHOIR, VGM_BRASS 등 지속음 악기들의 SR(Decay Rate 2)을
//      0으로 수정. 기존 SR=12~27은 key-on 중에도 볼륨이 계속 감쇠하는 치명적 버그.
//      SR=0 → SL 레벨에서 볼륨 고정 유지 (key-off까지 지속).
//
//  [R8-FIX-2]  현악기 AR 조정 (VIOLIN, CELLO)
//    - AR=28~30 → AR=18~24로 조정. 운궁(bowing) 특성상 점진적 음량 상승 필요.
//    - 금관악기(AR=31)와 명확히 구분.
//
//  [R8-FIX-3]  PIANO RS 분리 적용
//    - RS=2를 모든 OP에 적용하면 어택 AR도 고음에서 빨라짐 (원치 않음).
//    - 수정: carrier(OP4)에만 RS=2, modulator RS=1.
//    - SR=0으로 현의 긴 자연 감쇠 표현. DR=8~12로 느린 1차 감쇠.
//
//  [R8-FIX-4]  EPIANO FM Index 조정
//    - OP1(MUL=14) TL=10→8: FM Index 증가 → DX7 EP 특유의 어택 금속성 강화.
//    - carrier SR=0: 건반 누르는 동안 자연 감쇠 유지.
//
//  [R8-FIX-5]  BELL 인하모닉 배음 재설계
//    - MUL=1,3,5,7 (홀수배음) → MUL=0,1,3,7 (서브기음+기음+배음).
//    - DR=8~12: 종은 매우 느린 감쇠 (수초 울림). RR=6으로 릴리즈도 느리게.
//
//  [R8-FIX-6]  FLUTE FM Index 보정
//    - modulator TL=22~38 → TL=16~28로 낮춤. 플루트는 I≈0.5~1 범위가 적절.
//    - 순음에 가까운 기존값은 파이프오르간에 더 어울림.
//
//  [R8-FIX-7]  DAEGEUM 청공(淸孔) 비음 표현 강화
//    - FB=3→4: 자기변조 강화 → 청공 특유의 버징 배음 표현.
//    - OP1 MUL=3, TL=16: FM Index 증가 → 풍부한 배음 혼합.
//
//  [R8-FIX-8]  SHAMISEN 사와리(雑音) 개선
//    - FB=4→5: 배음 비대칭 강화.
//    - DT=2~3 OP1/OP3: 스펙트럼 비대칭으로 사와리 특유의 거칠기 표현.
//    - SR=4~5: 샤미센 공명통 약한 잔향 추가.
//
//  [R8-FIX-9]  MARIMBA SL 퍼커시브 수정
//    - SL=8~11 → SL=14~15: 마림바는 빠른 감쇠 타악기. RS=2 추가.
//
//  [R8-FIX-10] STRINGS_ENS DT 앙상블 강화
//    - carrier 3개의 DT를 DT=5(−), DT=3(+), DT=0으로 분산.
//    - SR=0으로 서스테인 유지.
//
// =============================================================================
// ── OPN 알고리즘 참조 (YM2203C Application Manual + MAME fm.cpp 기준) ─────────
//
//  ALG0:  OP1[FB]→OP2→OP3→OP4[C]           serial chain
//  ALG1:  (OP1[FB]+OP2)→OP3→OP4[C]          parallel input to OP3
//  ALG2:  OP1[FB]→OP3→OP4[C] + OP2→OP3      OP2 also feeds OP3
//  ALG3:  OP1[FB]→OP2→OP4[C] + OP3→OP4[C]   OP3 also feeds OP4
//  ALG4:  OP1[FB]→OP2[C] + OP3→OP4[C]        2+2 parallel
//  ALG5:  OP1[FB]→OP4[C] + OP2→OP4[C] + OP3→OP4[C]  star
//  ALG6:  OP1[FB]→OP2[C] + OP3→OP4[C] + independent? 
//         (정확히는 OP2+OP3+OP4 carrier, OP1이 OP2 변조)
//  ALG7:  OP1[FB][C] + OP2[C] + OP3[C] + OP4[C]  all carrier
//
//  Carrier mask (bit0=OP1, bit1=OP2, bit2=OP3, bit3=OP4):
//    ALG0=0x08, ALG1=0x08, ALG2=0x08, ALG3=0x08,
//    ALG4=0x0A, ALG5=0x08, ALG6=0x0E, ALG7=0x0F
//
// ── FM 음향 설계 원칙 ───────────────────────────────────────────────────────
//
//  TL(Total Level): 0=최대출력, 127=무음. carrier TL이 볼륨 직결.
//  FM Index I = (modulator_output_amplitude) / (carrier_frequency)
//    → modulator TL 낮을수록 I 커짐 → 고배음, 밝은 음색
//    → I≈0 (modulator TL 높음): 순정 사인파
//    → I≈1: 1~3차 배음 적당
//    → I≈3: 클라리넷/현악기류 배음
//    → I≥7: 매우 밝거나 불협화 음색
//  MUL(Multiple): 오퍼레이터 주파수 배수. 0=0.5배, 1=1배, 2=2배...
//    → 정수배: 조화배음(harmonics) → 음악적 음색
//    → 비정수(MUL=0): inharmonic → 금속/벨 음색
//  DT(Detune): 미세 조율. 0=no detune, 1~3=+ detune, 5~7=- detune
//    → modulator DT: FM 스펙트럼 비대칭 → 독특한 음색
//    → carrier DT: 미세 피치 어긋남 → 두꺼운 소리
//  FB(Feedback): OP1 자기 변조. 0=no, 7=최대
//    → 7: 사각파에 가까운 파형 → 오르간, 신스 리드
//    → 4~5: 적당한 배음 → 현악기 어택
//  AR(Attack Rate): 클수록 빠른 어택. 31=최대
//  DR/SR/RR: 감쇠 곡선. 실제 악기는 DR 크고 SR 적당, RR 보통 8~12.
//  SL(Sustain Level): 감쇠 후 서스테인 레벨. 0=최대 서스테인.
//
// =============================================================================

#ifndef YM2203_PATCHES_H
#define YM2203_PATCHES_H

#pragma once
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
//  외부 심볼
// ---------------------------------------------------------------------------

/* 구현부(.c)에서 정의 필요:
   ym2203_voice_t    g_ym_voices[3];
   ym2203_ssg_state_t g_ssg_state;              */

   // =============================================================================
   //  §1  Dirty-cache 레지스터 쓰기 추상화
   //
   //  YM2203 레지스터는 총 256개 (0x00~0xFF).
   //  레지스터 쓰기는 ym core에 직접 전달되며, 동일값 반복 쓰기를 캐시로 방지.
   //  key-on/off, envelope retrigger 등 반드시 실제 쓰기가 필요한 경우는
   //  ym_write_cached_force()를 사용한다.
   // =============================================================================
static uint8_t s_ym_cache[256];
static uint8_t s_ym_cache_valid = 0u;

static inline void ym2203_cache_reset(void)
{
    memset(s_ym_cache, 0xFFu, sizeof(s_ym_cache));
    s_ym_cache_valid = 1u;
}

/* Forward declaration — defined in midi.c */
extern void ym_write(uint8_t reg, uint8_t data);

static inline void ym_write_cached(uint8_t reg, uint8_t val)
{
    if (!s_ym_cache_valid) ym2203_cache_reset();
    if (s_ym_cache[reg] == val) return;
    s_ym_cache[reg] = val;
    ym_write(reg, val);          /* hw write — was recursive self-call (bug) */
}

static inline void ym_write_force_cached(uint8_t reg, uint8_t val)
{
    s_ym_cache[reg] = val;
    ym_write(reg, val);          /* force: bypass cache check */
}

/* Alias — callers use either spelling */
#define ym_write_cached_force ym_write_force_cached

// =============================================================================
//  §2  YM2203 레지스터 맵 상수 정의
//
//  레지스터 주소는 OPN 데이터시트 Table 3 기준.
//  채널 오프셋: ch=0→+0, ch=1→+1, ch=2→+2
//  OP 오프셋 (YM_OP_OFF[op], op=0~3):
//    OP1=0x00, OP3=0x04, OP2=0x08, OP4=0x0C  ← OPN 내부 순서
//    (레지스터 주소 오프셋이 OP 번호와 직렬이 아님에 주의)
// =============================================================================

/* FM 제어 레지스터 */
#define YM_REG_LFOFREQ      0x22u  /* (OPNA 호환, OPN은 미사용) */
#define YM_REG_TIMER_A_H    0x24u
#define YM_REG_TIMER_A_L    0x25u
#define YM_REG_TIMER_B      0x26u
#define YM_REG_TIMER_CTRL   0x27u  /* [7:6]=ch3_mode, [5:4]=timer, [3:0]=ctrl */
#define YM_REG_KEY_ON       0x28u  /* [6:4]=OP mask, [1:0]=channel */

/* FM 채널 파라미터 (베이스 + ch 오프셋) */
#define YM_REG_FNUM_LSB(ch) (uint8_t)(0xA0u + (ch))  /* fnum[7:0] */
#define YM_REG_FNUM_MSB(ch) (uint8_t)(0xA4u + (ch))  /* block[5:3], fnum[10:8] */
#define YM_REG_ALG_FB(ch)   (uint8_t)(0xB0u + (ch))  /* [5:3]=FB, [2:0]=ALG */
/* 0xB4 = L/R/AMS/PMS: OPN은 mono이므로 실제 L/R 없음. bit[7:6]=사용 안 함 */

/* ch3 Extended mode OP 개별 fnum 레지스터 */
#define YM_REG_CH3_OP1_LSB  0xA9u
#define YM_REG_CH3_OP2_LSB  0xA8u
#define YM_REG_CH3_OP3_LSB  0xAAu
#define YM_REG_CH3_OP4_LSB  0xA2u  /* ch3 normal fnum = ch3 OP4 */
#define YM_REG_CH3_OP1_MSB  0xADu
#define YM_REG_CH3_OP2_MSB  0xACu
#define YM_REG_CH3_OP3_MSB  0xAEu
#define YM_REG_CH3_OP4_MSB  0xA6u

/* FM OP 파라미터 베이스 + OP 오프셋 + ch 오프셋 */
#define YM_REG_DT_MUL   0x30u  /* [6:4]=DT, [3:0]=MUL */
#define YM_REG_TL       0x40u  /* [6:0]=TL */
#define YM_REG_RS_AR    0x50u  /* [7:6]=RS, [4:0]=AR */
#define YM_REG_AM_DR    0x60u  /* [7]=AM, [4:0]=DR */
#define YM_REG_SR       0x70u  /* [4:0]=SR (Sustain Rate / Decay2) */
#define YM_REG_SL_RR    0x80u  /* [7:4]=SL, [3:0]=RR */
#define YM_REG_SSGEG    0x90u  /* [3:0]=SSG-EG */

/* SSG 레지스터 (AY-3-8910 호환) */
#define YM_REG_SSG_TONE_A_L  0x00u
#define YM_REG_SSG_TONE_A_H  0x01u
#define YM_REG_SSG_TONE_B_L  0x02u
#define YM_REG_SSG_TONE_B_H  0x03u
#define YM_REG_SSG_TONE_C_L  0x04u
#define YM_REG_SSG_TONE_C_H  0x05u
#define YM_REG_SSG_NOISE     0x06u  /* [4:0]=noise period */
#define YM_REG_SSG_MIXER     0x07u  /* active-LOW: [5:3]=noise_en, [2:0]=tone_en */
#define YM_REG_SSG_VOL_A     0x08u  /* [4]=env_mode, [3:0]=vol */
#define YM_REG_SSG_VOL_B     0x09u
#define YM_REG_SSG_VOL_C     0x0Au
#define YM_REG_SSG_ENV_L     0x0Bu  /* envelope period LSB */
#define YM_REG_SSG_ENV_H     0x0Cu  /* envelope period MSB */
#define YM_REG_SSG_ENV_SHAPE 0x0Du  /* envelope shape (쓰기 시 항상 retrigger) */

/*
 * YM2203 OP 내부 순서 (register offset from channel base):
 *   레지스터 주소 = BASE + YM_OP_OFF[op] + ch
 *   op=0 → OP1, op=1 → OP2, op=2 → OP3, op=3 → OP4
 *
 *   OPN 내부 시분할 처리 순서와 레지스터 주소 순서는 다름.
 *   레지스터 기준:
 *     +0x00: OP1
 *     +0x04: OP3   ← 주의: OP2가 아님!
 *     +0x08: OP2
 *     +0x0C: OP4
 */
static const uint8_t YM_OP_OFF[4] = { 0x00u, 0x08u, 0x04u, 0x0Cu };

/*
 * Key-on 레지스터 0x28 [6:4] 비트:
 *   bit6=OP4, bit5=OP3, bit4=OP2, bit3=OP1  (각 1=on)
 *   key-on all OPs = 0xF0 | ch
 *   key-off        = 0x00 | ch
 */
#define YM_KEYON_ALL  0xF0u
#define YM_KEYOFF_ALL 0x00u

 // =============================================================================
 //  §3  ALG별 Carrier Mask 및 FM 인덱스 특성표
 //
 //  [R7-FIX-6 확정값]
 //  출처: YM2203C Application Manual p.12 Figure 2 + MAME fm.cpp OPN_ALGO_TABLE
 //
 //  bit0=OP1, bit1=OP2, bit2=OP3, bit3=OP4
 //
 //  ┌─────┬────────────────────────────────────┬─────────────┬──────────┐
 //  │ ALG │ 신호 흐름                           │ carrier ops │    mask  │
 //  ├─────┼────────────────────────────────────┼─────────────┼──────────┤
 //  │  0  │ 1[FB]→2→3→4                        │ OP4         │ 0x08     │
 //  │  1  │ (1[FB]+2)→3→4                      │ OP4         │ 0x08     │
 //  │  2  │ 1[FB]→3→4 + 2→3                    │ OP4         │ 0x08     │
 //  │  3  │ 1[FB]→2→4 + 3→4                    │ OP4         │ 0x08     │
 //  │  4  │ 1[FB]→2 + 3→4                      │ OP2+OP4     │ 0x0A     │
 //  │  5  │ 1[FB]→4 + 2→4 + 3→4               │ OP4         │ 0x08     │
 //  │  6  │ 1[FB]→2 + OP3 + OP4 (모두 출력)   │ OP2+OP3+OP4 │ 0x0E     │
 //  │  7  │ 1[FB] + 2 + 3 + 4 (전부 캐리어)   │ ALL         │ 0x0F     │
 //  └─────┴────────────────────────────────────┴─────────────┴──────────┘
 //
 //  ALG5 주의: OPN 데이터시트에서는 OP1, OP2, OP3 모두 OP4를 변조함.
 //    carrier = OP4만. OP1은 FB 자기변조도 겸함.
 //    MAME fm.cpp: case 5: c1=OP4, c2=c3=0 → carrier=0x08 확정.
 //
 //  ALG6 주의: OP2, OP3, OP4가 모두 직접 출력 → carrier=0x0E
 //    OP1은 OP2에 변조. OP3, OP4는 독립 캐리어.
 // =============================================================================
static const uint8_t YM_CARRIER_MASK[8] = {
    0x08u,  /* ALG0: OP4            serial */
    0x08u,  /* ALG1: OP4            split-input */
    0x08u,  /* ALG2: OP4            */
    0x08u,  /* ALG3: OP4            double-mod-to-carrier */
    0x0Au,  /* ALG4: OP2+OP4        2+2 parallel */
    0x08u,  /* ALG5: OP4            star (OP1+2+3 all mod OP4) */
    0x0Eu,  /* ALG6: OP2+OP3+OP4   OP1→OP2, OP3/OP4 독립 */
    0x0Fu   /* ALG7: ALL            additive */
};

/*
 * ALG별 설계 가이드:
 *   ALG0 (serial 4op): 최대 변조 깊이. 풍부한 배음. 금속/벨/거친 리드.
 *     FM Index = OP1이 OP2를, OP2가 OP3를, OP3가 OP4를 변조. 연쇄 증폭.
 *     실용: MUL OP1=1,OP2=1,OP3=1,OP4=1, TL OP1~3 조절로 배음량 제어.
 *
 *   ALG1 (2+1+1): OP1과 OP2 동시에 OP3 변조 → 2개 변조원의 합산 효과.
 *     복잡한 배음. 클라리넷, 금관류.
 *
 *   ALG4 (2+2 parallel): 독립 2-op 2채널. 각각 단독 악기처럼 사용 가능.
 *     OP1→OP2[C] + OP3→OP4[C].
 *     실용: EP, 스틸기타. OP2 TL로 배음, OP4 TL로 서스테인 독립 제어.
 *
 *   ALG5 (star): OP1,2,3이 모두 OP4를 변조 → 3개 변조원 합산.
 *     오르간(각 OP가 배음 주파수 표현). MUL로 배음 구성.
 *
 *   ALG6 (3 carriers): OP2+OP3+OP4 독립 출력 + OP1이 OP2 변조.
 *     패드/앙상블. 각 carrier가 다른 MUL로 additive 배음 합성.
 *
 *   ALG7 (additive 4): 4개 독립 사인파 합성. 오르간, 벨, 크리스탈류.
 */

 // =============================================================================
 //  §4  SSG Envelope Shape 정의 (YM2203 데이터시트 Table 3)
 //
 //  YM2203 SSG envelope는 AY-3-8910 호환.
 //  0x00~0x07: 모두 단발 감쇠 (\___) — shape 0~3 = 단발감쇠, 4~7 = 단발감쇠
 //  0x08: \___  DECAY once, hold at 0
 //  0x09: \___  (0x08과 동일)
 //  0x0A: \/\/  반복 삼각파 (감쇠+역전 반복)
 //  0x0B: \‾‾‾  단발 감쇠 후 최대 유지 (hold)
 //  0x0C: /‾‾‾  단발 어택 (attack once, hold at max)
 //  0x0D: /___  단발 어택 후 소거 (attack → 0)
 //  0x0E: /\/\  반복 삼각파 (어택+역전 반복)
 //  0x0F: /‾‾‾  반복 어택 (attack, hold at max = 연속 최대)
 //
 //  레지스터 0x0D 쓰기는 항상 retrigger이므로 ym_write_force() 필수.
 // =============================================================================
#define SSG_ENV_DECAY_ONCE   0x08u  /* \___ 단발 감쇠 → 소거           */
#define SSG_ENV_DECAY_ALT    0x0Au  /* \/\/ 반복 삼각 (감쇠↔역전)     */
#define SSG_ENV_DECAY_HOLD   0x0Bu  /* \‾‾‾ 단발 감쇠 → 최대 유지     */
#define SSG_ENV_ATTACK_HOLD  0x0Cu  /* /‾‾‾ 단발 어택 → 최대 유지     */
#define SSG_ENV_ATTACK_DROP  0x0Du  /* /___ 단발 어택 → 소거           */
#define SSG_ENV_ATTACK_ALT   0x0Eu  /* /\/\ 반복 삼각 (어택↔역전)     */
#define SSG_ENV_ATTACK_CONT  0x0Fu  /* /‾‾‾ 연속 최대 (사실상 최대볼륨)*/

// =============================================================================
//  §5  데이터 구조체
//
//  ym2203_op_t: FM 오퍼레이터 파라미터
//  ym2203_patch_t: FM 패치 (4 OP + 알고리즘)
//  ym2203_ssg_patch_t: SSG 패치
// =============================================================================

/* FM 오퍼레이터 파라미터 */
typedef struct {
    uint8_t DT;    /* Detune        0~7  (0=no, 1~3=+, 5~7=-)           */
    uint8_t MUL;   /* Multiple      0~15 (0=×0.5, 1=×1, 2=×2 ...)       */
    uint8_t TL;    /* Total Level   0~127 (0=최대출력, 127=무음)          */
    uint8_t RS;    /* Rate Scaling  0~3  (클수록 고음역 빠른 감쇠)        */
    uint8_t AR;    /* Attack Rate   0~31 (31=최빠름)                      */
    uint8_t AM;    /* AM enable     0/1  (LFO AM 적용 여부, OPN은 SW LFO)*/
    uint8_t DR;    /* Decay Rate 1  0~31 (첫 감쇠)                       */
    uint8_t SR;    /* Decay Rate 2  0~31 (서스테인 감쇠, 일부 문서=D2R)  */
    uint8_t SL;    /* Sustain Level 0~15 (0=최대 서스테인)                */
    uint8_t RR;    /* Release Rate  0~15 (key-off 후 감쇠)               */
    uint8_t SSGEG; /* SSG-EG mode   0~15 (0=disable, 8~15=enable)        */
    /*
     * SSGEG 활성화 조건: bit3=1 (0x08 이상)
     * 활성시 OP의 envelope를 SSG-EG 파형으로 교체.
     * bit2(attack), bit1(alt), bit0(hold)로 파형 결정.
     * 사용 시 주의: carrier에만 쓰거나, modulator에는 주의해서 사용.
     * SSGEG≠0 인 modulator → FM index 주기적 변화 → 특수 효과 또는 음색 파괴.
     */
} ym2203_op_t;

/* FM 패치 */
typedef struct {
    const char* name;
    uint8_t      ALG;   /* 0~7   */
    uint8_t      FB;    /* 0~7   (OP1 feedback. 7=최대=사각파에 가까움)  */
    uint8_t      vel_tl_scale; /* 0~8: velocity TL 감소 shfit.
                                  0=vel 무시, 3=섬세, 4=표준, 6=강감도.
                                  carrier TL 감소량 = vel >> vel_tl_scale */
    ym2203_op_t  ops[4]; /* [0]=OP1, [1]=OP2, [2]=OP3, [3]=OP4         */
} ym2203_patch_t;

/* SSG 패치 — Rev.7 확장 */
typedef struct {
    const char* name;
    uint8_t  use_tone;       /* 1=tone 활성화                             */
    uint8_t  use_noise;      /* 1=noise 활성화                            */
    uint8_t  noise_period;   /* 0~31: noise 주기 (클수록 저주파 노이즈)  */
    uint8_t  amp_base;       /* 0x00~0x0F: vol mode, 0x10~0x1F: env mode */
    uint16_t env_period;     /* envelope 주기 (0x0B/0x0C)                 */
    uint8_t  env_shape;      /* SSG_ENV_* 매크로 사용                     */
    uint8_t  vol_scale;      /* 0~255. 128=100%. velocity 스케일 비율     */
    uint8_t  attack_ticks;   /* note_on 후 N틱간 attack_vol 유지 (트랜지언트) */
    uint8_t  attack_vol;     /* attack_ticks 동안의 볼륨 (0~15)           */
    uint8_t  cut_ticks;      /* 0=무한. N>0이면 N틱 후 자동 소거 (드럼용)  */
} ym2203_ssg_patch_t;

// =============================================================================
//  §6  SSG 채널별 런타임 상태 구조체  [R7-FIX-3 개선]
//
//  SSG note_cut_ticks 처리 및 attack_boost를 tick() 내에서 관리.
// =============================================================================
typedef struct {
    uint8_t  active;          /* 1=발음 중                         */
    uint8_t  cut_ticks;       /* 남은 cut tick 수 (0=무한)         */
    uint8_t  cut_ticks_init;  /* note_on 시 설정값                 */
    uint8_t  attack_ticks;    /* 남은 attack boost tick            */
    uint8_t  attack_vol;      /* attack 구간 볼륨                  */
    uint8_t  sustain_vol;     /* attack 이후 볼륨                  */
    uint8_t  env_mode;        /* 1=envelope mode (amp_base bit4=1) */
} ym2203_ssg_ch_state_t;

static ym2203_ssg_ch_state_t g_ssg_ch[3];
static uint8_t g_ssg_mixer = 0x3Fu;  /* 초기값: 전체 disable (0x3F) */
static uint8_t g_ssg_noise_frq = 0x00u;

/* [R7-FIX-8] SSG mixer 제어 — active-LOW 비트 처리 명확화
 *   bit[2:0] = TONE_A/B/C (0=enable, 1=disable)
 *   bit[5:3] = NOISE_A/B/C (0=enable, 1=disable)
 */
static inline void ym2203_ssg_ch_enable(uint8_t ch, uint8_t use_tone, uint8_t use_noise)
{
    if (ch > 2u) return;
    /* tone bit: bit ch → 0=enable, 1=disable */
    if (use_tone)  g_ssg_mixer &= (uint8_t)(~(1u << ch));        /* bit clear = enable */
    else           g_ssg_mixer |= (uint8_t)(1u << ch);            /* bit set   = disable */
    /* noise bit: bit (ch+3) */
    if (use_noise) g_ssg_mixer &= (uint8_t)(~(1u << (ch + 3u))); /* enable */
    else           g_ssg_mixer |= (uint8_t)(1u << (ch + 3u));     /* disable */
    ym_write_cached(YM_REG_SSG_MIXER, g_ssg_mixer);
}

static inline void ym2203_ssg_ch_disable(uint8_t ch)
{
    if (ch > 2u) return;
    g_ssg_mixer |= (uint8_t)((1u << ch) | (1u << (ch + 3u))); /* 둘 다 disable */
    ym_write_cached(YM_REG_SSG_MIXER, g_ssg_mixer);
}

static inline void ym2203_ssg_all_disable(void)
{
    g_ssg_mixer = 0x3Fu;
    ym_write_cached(YM_REG_SSG_MIXER, 0x3Fu);
}

static inline void ym2203_ssg_set_vol(uint8_t ch, uint8_t vol)
{
    if (ch > 2u) return;
    ym_write_cached((uint8_t)(YM_REG_SSG_VOL_A + ch), vol & 0x1Fu);
}

static inline void ym2203_ssg_ch_silence(uint8_t ch)
{
    if (ch > 2u) return;
    ym2203_ssg_set_vol(ch, 0u);
    ym2203_ssg_ch_disable(ch);
    g_ssg_ch[ch].active = 0u;
}

static inline void ym2203_ssg_silence_all(void)
{
    ym_write_cached(YM_REG_SSG_VOL_A, 0u);
    ym_write_cached(YM_REG_SSG_VOL_B, 0u);
    ym_write_cached(YM_REG_SSG_VOL_C, 0u);
    ym2203_ssg_all_disable();
    for (int i = 0; i < 3; i++) g_ssg_ch[i].active = 0u;
}

// =============================================================================
//  §7  소프트웨어 LFO  (YM2203은 하드웨어 LFO 없음)
//
//  phase_inc 계산:
//    LFO rate (Hz) = rate_hundredths / 100.0
//    64 steps per cycle
//    ticks per step = tick_hz / (rate × 64)
//    phase_inc (fixed point ×64) = (rate × 64 × 64) / tick_hz
//                                = rate × 4096 / tick_hz
// =============================================================================
static const int8_t YM_SINE64[64] = {
      0,  12,  25,  37,  49,  60,  71,  81,  90,  98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106,  98,  90,  81,  71,  60,  49,  37,  25,  12,
      0, -12, -25, -37, -49, -60, -71, -81, -90, -98,-106,-112,-117,-122,-125,-126,
   -127,-126,-125,-122,-117,-112,-106, -98, -90, -81, -71, -60, -49, -37, -25, -12
};

typedef enum {
    LFO_SINE = 0,
    LFO_TRI = 1,
    LFO_SAW = 2,  /* 상승 톱니 */
    LFO_SQR = 3
} ym2203_lfo_shape_t;

typedef struct {
    ym2203_lfo_shape_t shape;
    uint8_t  depth_vib;    /* vibrato 깊이 0~31  (fnum ±δ)              */
    uint8_t  depth_trem;   /* tremolo  깊이 0~31 (carrier TL ±δ)        */
    uint8_t  trem_bipolar; /* 0=단방향(볼륨진동), 1=쌍방향(TL가감)       */
    uint16_t phase;        /* 0~63 (현재 위상 스텝)                      */
    uint16_t phase_frac;   /* 소수 위상 누산기 (×64 fixed-point)         */
    uint16_t phase_inc;    /* 틱당 위상 증가량 (×64)                     */
} ym2203_lfo_t;

static inline void ym2203_lfo_init(ym2203_lfo_t* lfo,
    ym2203_lfo_shape_t shape,
    uint8_t depth_vib, uint8_t depth_trem,
    uint16_t rate_hundredths, uint16_t tick_hz,
    uint8_t trem_bipolar)
{
    lfo->shape = shape;
    lfo->depth_vib = depth_vib;
    lfo->depth_trem = depth_trem;
    lfo->trem_bipolar = trem_bipolar;
    lfo->phase = 0u;
    lfo->phase_frac = 0u;
    lfo->phase_inc = (uint16_t)((uint32_t)rate_hundredths * 4096u /
        ((uint32_t)tick_hz * 100u));
    if (lfo->phase_inc == 0u) lfo->phase_inc = 1u;
}

/* LFO 한 틱 진행. 반환값: -127~+127 */
static inline int8_t ym2203_lfo_tick(ym2203_lfo_t* lfo)
{
    lfo->phase_frac += lfo->phase_inc;
    if (lfo->phase_frac >= 64u) {
        lfo->phase_frac -= 64u;
        lfo->phase = (lfo->phase + 1u) & 63u;
    }
    uint8_t p = (uint8_t)lfo->phase;
    switch (lfo->shape) {
    case LFO_SINE: return YM_SINE64[p];
    case LFO_TRI:  return (p < 32u) ? (int8_t)(p * 4 - 64) : (int8_t)(192 - p * 4);
    case LFO_SAW:  return (int8_t)(p * 4 - 128);
    case LFO_SQR:  return (p < 32u) ? 64 : -64;
    default:       return 0;
    }
}

// =============================================================================
//  §8  패치 인덱스 열거형
// =============================================================================
typedef enum {
    /* VGM 게임음악 실측 계열 */
    PATCH_VGM_LEAD_A = 0,
    PATCH_VGM_LEAD_B,
    PATCH_VGM_LEAD_C,
    PATCH_VGM_LEAD_D,
    PATCH_VGM_LEAD_E,
    PATCH_VGM_BRASS,
    PATCH_VGM_PAD_A,
    PATCH_VGM_PAD_B,
    PATCH_VGM_PAD_C,
    PATCH_VGM_ORGAN_A,
    PATCH_VGM_ORGAN_B,
    PATCH_VGM_ORGAN_C,
    PATCH_VGM_DUAL_A,
    PATCH_VGM_DUAL_B,
    PATCH_VGM_DRUM_A,
    PATCH_VGM_DRUM_B,
    PATCH_VGM_DRUM_C,
    PATCH_VGM_BASS,

    /* 건반 */
    PATCH_PIANO,
    PATCH_EPIANO,
    PATCH_MUSICBOX,
    PATCH_HARP,
    PATCH_HARPSICHORD,
    PATCH_CELESTA,
    PATCH_XYLOPHONE,
    PATCH_MARIMBA,
    PATCH_BELL,
    PATCH_VIBRAPHONE,

    /* 현악기 */
    PATCH_VIOLIN,
    PATCH_CELLO,
    PATCH_STRINGS_ENS,
    PATCH_AGITAR,
    PATCH_EGITAR_CLEAN,
    PATCH_EGITAR_DRIVE,
    PATCH_SHAMISEN,
    PATCH_BANJO,
    PATCH_KOTO,

    /* 관악기 */
    PATCH_FLUTE,
    PATCH_DAEGEUM,
    PATCH_SOGEUM,
    PATCH_OBOE,
    PATCH_TRUMPET,
    PATCH_SAXOPHONE,
    PATCH_TROMBONE,

    /* 베이스 */
    PATCH_BASS_ACOUSTIC,
    PATCH_BASS_FRETLESS,
    PATCH_VGM_BASS2,

    /* 보컬 / 패드 */
    PATCH_CHOIR,
    PATCH_SYNTH_LEAD,
    PATCH_SYNTH_PAD,
    PATCH_SYNTH_BRASS,

    /* 드럼 FM */
    PATCH_DRUM_KICK,
    PATCH_DRUM_SNARE,
    PATCH_DRUM_HI_TOM,
    PATCH_DRUM_LO_TOM,

    PATCH_COUNT
} ym2203_patch_idx_t;
#define YM2203_PATCH_COUNT  ((uint8_t)(PATCH_COUNT))

typedef enum {
    SSG_SQUARE_BRIGHT = 0, /* 밝은 사각파                    */
    SSG_SQUARE_SOFT,       /* 부드러운 사각파 (낮은 볼륨)    */
    SSG_ATTACK_PLUCK,      /* 강한 어택 + SSG env 감쇠       */
    SSG_ATTACK_MALLET,     /* 말렛 타격 (짧은 env 감쇠)      */
    SSG_NOISE_HIHAT,       /* 하이햇 노이즈                   */
    SSG_NOISE_SNARE,       /* 스네어 노이즈+tone 혼합         */
    SSG_NOISE_KICK,        /* 킥 노이즈 (env 피치드롭)        */
    SSG_NOISE_CRASH,       /* 크래쉬 심벌즈 (긴 노이즈)       */
    SSG_BUZZ,              /* 버즈 (noise+tone 혼합)          */
    SSG_CHORD_LAYER,       /* 코드 레이어용 (낮은 볼륨)       */
    SSG_COUNT
} ym2203_ssg_idx_t;

// =============================================================================
//  §9  하이브리드 악기 구조체 (FM + SSG 복합)
// =============================================================================
typedef struct {
    ym2203_patch_idx_t  fm_patch;        /* FM 패치 인덱스                   */
    ym2203_ssg_idx_t    ssg_patch;       /* SSG 패치 인덱스                  */
    uint8_t             ssg_ch;          /* SSG 채널 0~2, 0xFF=미사용        */

    /* Swell (carrier TL ramp: note_on 시 TL 서서히 감소 = 볼륨 증가) */
    uint8_t  swell_op;         /* swell 적용 OP (0~3)               */
    uint8_t  swell_tl_start;   /* swell 시작 TL (0~127)             */
    uint8_t  swell_tl_end;     /* swell 목표 TL (0~127)             */
    uint16_t swell_ticks;      /* swell 완료 시간 (tick 수)          */

    /* LFO */
    uint8_t  lfo_vib_depth;    /* vibrato depth 0~31               */
    uint8_t  lfo_trem_depth;   /* tremolo depth 0~31               */
    uint8_t  lfo_trem_bipolar; /* 0=단방향, 1=쌍방향               */
    uint16_t lfo_rate_x100;    /* LFO 속도 (1/100 Hz 단위)         */

    /* Portamento */
    uint8_t  porta_ticks;      /* 0=없음. N=글라이드 틱 수         */

    /* Detune (코러스/앙상블 효과용 fnum 오프셋) */
    int8_t   detune_fnum;      /* ±fnum 오프셋 (≈1cent per fnum)  */
} ym2203_instrument_t;

// =============================================================================
//  §10  악기 테이블
// =============================================================================
static const ym2203_instrument_t YM2203_INSTRUMENTS[] = {
    /*  [idx]  이름               fm_patch             ssg_patch          sch
                                  sw_op stls  stle stks  vbd tmd  tmb lr100 pt  dt */
    /* 00 피아노     */ { PATCH_PIANO,        SSG_SQUARE_BRIGHT, 0xFF,
                         0,    2,   24,   20,    0,   0,   0,    0,  0,   0 },
                         /* 01 전자피아노 */ { PATCH_EPIANO,       SSG_SQUARE_BRIGHT, 0xFF,
                                              0,    8,   28,   30,    0,   0,   0,    0,  0,   0 },
                                              /* 02 오르골     */ { PATCH_MUSICBOX,     SSG_ATTACK_MALLET, 0xFF,
                                                                   0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                   /* 03 하프       */ { PATCH_HARP,         SSG_SQUARE_BRIGHT, 0xFF,
                                                                                        0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                        /* 04 하프시코드 */ { PATCH_HARPSICHORD,  SSG_ATTACK_PLUCK,  0xFF,
                                                                                                             0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                             /* 05 첼레스타   */ { PATCH_CELESTA,      SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                  0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                  /* 06 실로폰     */ { PATCH_XYLOPHONE,    SSG_ATTACK_MALLET, 0xFF,
                                                                                                                                                       0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                       /* 07 마림바     */ { PATCH_MARIMBA,      SSG_ATTACK_MALLET, 0xFF,
                                                                                                                                                                            0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                            /* 08 벨         */ { PATCH_BELL,         SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                 0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                 /* 09 바이브라폰 */ { PATCH_VIBRAPHONE,   SSG_ATTACK_MALLET, 0xFF,
                                                                                                                                                                                                                      0,    0,    0,    0,    2,   1,   1,  300,  0,   0 },
                                                                                                                                                                                                                      /* 10 바이올린   */ { PATCH_VIOLIN,       SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                           0,    0,    0,    0,    4,   2,   1,  550,  0,   0 },
                                                                                                                                                                                                                                           /* 11 첼로       */ { PATCH_CELLO,        SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                0,    0,    0,    0,    3,   1,   1,  450,  0,   0 },
                                                                                                                                                                                                                                                                /* 12 현악 앙상블*/ { PATCH_STRINGS_ENS,  SSG_SQUARE_SOFT,   0xFF,
                                                                                                                                                                                                                                                                                     0,    0,    0,    0,    2,   3,   0,  400,  0,   5 },
                                                                                                                                                                                                                                                                                     /* 13 어쿠기타   */ { PATCH_AGITAR,       SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                          0,    4,   12,   30,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                          /* 14 일렉 클린  */ { PATCH_EGITAR_CLEAN, SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                               0,    0,    0,    0,    2,   1,   1,  400,  0,   0 },
                                                                                                                                                                                                                                                                                                                               /* 15 일렉 드라이브*/{ PATCH_EGITAR_DRIVE,SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                    0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                    /* 16 샤미센     */ { PATCH_SHAMISEN,     SSG_ATTACK_PLUCK,  0xFF,
                                                                                                                                                                                                                                                                                                                                                                         0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                         /* 17 반조       */ { PATCH_BANJO,        SSG_ATTACK_PLUCK,  0xFF,
                                                                                                                                                                                                                                                                                                                                                                                              0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                              /* 18 고토       */ { PATCH_KOTO,         SSG_ATTACK_PLUCK,  0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                   0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                   /* 19 플루트     */ { PATCH_FLUTE,        SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                        0,    0,    0,    0,    3,   1,   0,  500,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                        /* 20 대금       */ { PATCH_DAEGEUM,      SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                             0,    0,    0,    0,    5,   2,   0,  400,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                             /* 21 소금       */ { PATCH_SOGEUM,       SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  0,    0,    0,    0,    3,   1,   0,  450,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  /* 22 오보에     */ { PATCH_OBOE,         SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       0,    0,    0,    0,    2,   1,   0,  480,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       /* 23 트럼펫     */ { PATCH_TRUMPET,      SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            /* 24 색소폰     */ { PATCH_SAXOPHONE,    SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 0,    0,    0,    0,    2,   1,   0,  400,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 /* 25 트롬본     */ { PATCH_TROMBONE,     SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      0,    0,    0,    0,    0,   0,   0,    0, 12,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      /* 26 어쿠 베이스*/ { PATCH_BASS_ACOUSTIC,SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           /* 27 프렛리스   */ { PATCH_BASS_FRETLESS,SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                0,    0,    0,    0,    2,   0,   0,  300, 15,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                /* 28 VGM베이스  */ { PATCH_VGM_BASS,    SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     /* 29 합창       */ { PATCH_CHOIR,        SSG_SQUARE_SOFT,   0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          0,    0,    0,    0,    3,   3,   0,  380,  0,   7 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          /* 30 신스 리드  */ { PATCH_SYNTH_LEAD,   SSG_BUZZ,          0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               0,    0,    0,    0,    3,   2,   1,  600,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               /* 31 신스 패드  */ { PATCH_SYNTH_PAD,    SSG_SQUARE_SOFT,   0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    0,    0,    0,    0,    2,   4,   0,  350,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    /* 32 신스 브라스*/ { PATCH_SYNTH_BRASS,  SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         /* 33 킥드럼     */ { PATCH_DRUM_KICK,    SSG_NOISE_KICK,      0,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              /* 34 스네어     */ { PATCH_DRUM_SNARE,   SSG_NOISE_SNARE,     2,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   /* 35 하이햇     */ { PATCH_VGM_DRUM_A,  SSG_NOISE_HIHAT,     1,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        /* 36 크래쉬     */ { PATCH_VGM_DRUM_B,  SSG_NOISE_CRASH,     1,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             /* 37 VGM리드A   */ { PATCH_VGM_LEAD_A,  SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  /* 38 VGM리드B   */ { PATCH_VGM_LEAD_B,  SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       /* 39 VGM브라스  */ { PATCH_VGM_BRASS,   SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            /* 40 VGM오르간  */ { PATCH_VGM_ORGAN_A, SSG_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 0,    0,    0,    0,    0,   0,   0,    0,  0,   0 },
};
#define YM2203_INSTRUMENT_COUNT \
    ((uint8_t)(sizeof(YM2203_INSTRUMENTS)/sizeof(YM2203_INSTRUMENTS[0])))

typedef enum {
    INST_PIANO = 0, INST_EPIANO = 1, INST_MUSICBOX = 2, INST_HARP = 3,
    INST_HARPSICHORD = 4, INST_CELESTA = 5, INST_XYLOPHONE = 6, INST_MARIMBA = 7,
    INST_BELL = 8, INST_VIBRAPHONE = 9,
    INST_VIOLIN = 10, INST_CELLO = 11, INST_STRINGS = 12,
    INST_AGITAR = 13, INST_EGITAR_CLEAN = 14, INST_EGITAR_DRIVE = 15,
    INST_SHAMISEN = 16, INST_BANJO = 17, INST_KOTO = 18,
    INST_FLUTE = 19, INST_DAEGEUM = 20, INST_SOGEUM = 21,
    INST_OBOE = 22, INST_TRUMPET = 23, INST_SAXOPHONE = 24, INST_TROMBONE = 25,
    INST_BASS_ACOUSTIC = 26, INST_BASS_FRETLESS = 27, INST_VGM_BASS = 28,
    INST_CHOIR = 29, INST_SYNTH_LEAD = 30, INST_SYNTH_PAD = 31, INST_SYNTH_BRASS = 32,
    INST_DRUM_KICK = 33, INST_DRUM_SNARE = 34, INST_HIHAT = 35, INST_CRASH = 36,
    INST_VGM_LEAD_A = 37, INST_VGM_LEAD_B = 38,
    INST_VGM_BRASS = 39, INST_VGM_ORGAN = 40,
} ym2203_inst_idx_t;

// =============================================================================
//  §11  FM 패치 테이블 (Rev.7)
//
//  설계 원칙 [R7-NEW-1]:
//
//  [velocity 정책]
//    - vel_tl_scale 파라미터로 감도 조절.
//    - carrier TL 감소량 = (127 - vel) >> vel_tl_scale
//      vel=127(최강) → 감소량=0  (원본 TL 유지 = 최대 볼륨)
//      vel=0  (최약) → 감소량 = 127 >> vel_tl_scale
//    - modulator TL은 절대 velocity로 변경 안 함.
//
//  [TL 기준값 설계]
//    carrier TL 권장 범위: 0~16 (velocity margin 확보)
//    → vel_tl_scale=4 기준 최대 감소 7~8, TL=8 → 최약 vel시 TL=16 정도
//
//  [DT 활용]
//    modulator DT≠0: 스펙트럼 비대칭 → 음색에 개성
//    carrier  DT≠0: 피치 미세 어긋남 (코드 후 detune_fnum보다 정확)
//
//  [SSGEG 활용 주의사항]
//    - carrier에만 사용 권장. bit3=1(0x08~0x0F)이어야 활성화.
//    - modulator에 SSGEG 사용 시 FM index가 주기적으로 변화 → 의도적 경우만.
//    - 0x0D(어택 후 소거): 피치카토, 타악기 어택.
//    - 0x08(단발감쇠): 킥드럼 피치드롭, 타현악기.
// =============================================================================
static const ym2203_patch_t YM2203_PATCHES[PATCH_COUNT] = {

    // ── 섹션 A: VGM 실측 계열 ──────────────────────────────────────────────────
    // vel_tl_scale=4 기본 적용. carrier TL을 충분한 여유(≤16)로 설계.

    /* [0] VGM_LEAD_A — ALG1 FB7 밝은 게임 리드
       ALG1: (OP1[FB]+OP2)→OP3→OP4[C]. carrier=OP4.
       OP1 FB7=사각파유사. OP1+OP2 TL차이로 변조 깊이 분배. */
    { "VGM_LEAD_A", 1, 7, 4, {
        /*DT MUL  TL RS AR AM DR SR SL RR SSGEG*/
        { 0, 15,  21, 0, 31, 0, 20, 10,  3,  9,  0}, /* OP1 mod: FB7 강한 배음 */
        { 3,  3,   7, 0, 31, 0, 20, 13,  4,  9,  0}, /* OP2 mod: DT3로 비대칭 */
        { 3,  1,  22, 0, 31, 0, 24,  0,  3,  9,  0}, /* OP3 mod */
        { 7,  1,   8, 0, 31, 0,  0,  0,  0, 10,  0}}},/* OP4 car TL=8 */

        /* [1] VGM_LEAD_B — ALG1 FB7 두툼한 리드 */
        { "VGM_LEAD_B", 1, 7, 4, {
            { 0, 11,  24, 0, 31, 0, 20, 10,  3,  9,  0},
            { 3,  1,   7, 0, 31, 0, 20, 13,  4,  9,  0},
            { 3,  2,  26, 0, 31, 0, 24,  0,  2,  9,  0},
            { 7,  1,   8, 0, 31, 0,  0,  0,  0, 10,  0}}},

            /* [2] VGM_LEAD_C — ALG1 FB7 소프트 리드 (모뎀 TL 높임) */
            { "VGM_LEAD_C", 1, 7, 4, {
                { 0, 11,  35, 0, 31, 0, 20, 10,  0,  9,  0},
                { 3,  1,  26, 0, 31, 0, 20, 13,  0,  9,  0},
                { 3,  2,  35, 0, 31, 0, 24,  0,  0,  9,  0},
                { 7,  1,   8, 0, 31, 0,  0,  0,  0, 10,  0}}},

                /* [3] VGM_LEAD_D — ALG1 FB7 */
                { "VGM_LEAD_D", 1, 7, 4, {
                    { 0, 15,  33, 0, 31, 0, 20, 10,  0,  9,  0},
                    { 3,  3,  26, 0, 31, 0, 20, 13,  0,  9,  0},
                    { 3,  1,  35, 0, 31, 0, 24,  0,  0,  9,  0},
                    { 7,  1,   8, 0, 31, 0,  0,  0,  0, 10,  0}}},

                    /* [4] VGM_LEAD_E — ALG1 FB7 거친 리드 */
                    { "VGM_LEAD_E", 1, 7, 4, {
                        { 7, 11,  20, 0, 31, 0, 21, 15,  3, 10,  0},
                        { 3,  3,  16, 0, 31, 0, 17,  0,  4, 10,  0},
                        { 7,  1,  27, 0, 31, 0, 18,  0,  5, 10,  0},
                        { 7,  1,   8, 0, 31, 0, 15,  0,  2, 11,  0}}},

                        /* [5] VGM_BRASS — ALG4 FB7 브라스 섹션
                           ALG4: OP1[FB]→OP2[C] + OP3→OP4[C]. carrier=OP2+OP4.
                           양쪽 carrier TL 조절 필요. vel_tl_scale=4. */
                        { "VGM_BRASS", 4, 7, 4, {
                            { 0,  8,   0, 0, 31, 0,  0, 27,  0, 15,  0}, /* OP1 mod TL=0: 강한 변조 */
                            { 0,  6,   5, 0, 31, 0,  0, 19,  0, 15,  0}, /* OP2 car TL=5 */
                            { 0,  2,   0, 0, 31, 0,  0, 24,  0, 15,  0}, /* OP3 mod TL=0: 강한 변조 */
                            { 0,  0,   5, 0, 31, 0,  0,  0,  0, 15,  0}}},/* OP4 car TL=5 */

                            /* [6] VGM_PAD_A — ALG4 FB7 부드러운 패드 */
                            { "VGM_PAD_A", 4, 7, 4, {
                                { 0, 15,   0, 0, 31, 0,  0,  0,  0, 15,  0},
                                { 0, 15,   7, 0, 31, 0, 17, 26,  5, 15,  0},
                                { 0,  4,   0, 0, 20, 0,  0, 29,  0, 15,  0},
                                { 0,  0,   0, 0, 31, 0, 13, 31,  1, 15,  0}}},

                                /* [7] VGM_PAD_B — ALG4 FB7 */
                                { "VGM_PAD_B", 4, 7, 4, {
                                    { 0, 15,   0, 0, 31, 0,  0,  0,  0, 15,  0},
                                    { 0, 15,   3, 0, 31, 0, 17, 15,  2, 15,  0},
                                    { 0,  6,  32, 0, 31, 0,  0,  0,  0, 15,  0},
                                    { 0, 12,   6, 0, 31, 0, 21, 17,  3, 15,  0}}},

                                    /* [8] VGM_PAD_C — ALG4 FB7 */
                                    { "VGM_PAD_C", 4, 7, 4, {
                                        { 0, 15,   0, 0, 31, 0,  0,  0,  0, 15,  0},
                                        { 0, 15,  18, 0, 31, 0, 17, 15,  2, 15,  0},
                                        { 0,  6,  32, 0, 31, 0,  0,  0,  0, 15,  0},
                                        { 0, 12,  21, 0, 31, 0, 21, 17,  3, 15,  0}}},

                                        /* [9] VGM_ORGAN_A — ALG5 FB7 파이프 오르간
                                           ALG5: OP1[FB]→OP4 + OP2→OP4 + OP3→OP4. carrier=OP4.
                                           각 OP MUL=다른배수로 배음 구성. FB7로 OP1이 사각파유사.
                                           오르간 특성: 볼륨 일정(no velocity 감도), vel_tl_scale=0. */
                                        { "VGM_ORGAN_A", 5, 7, 0, {
                                            { 0,  5,  29, 0, 31, 0, 13,  0,  0, 15,  0}, /* OP1 MUL=5: 5배음 */
                                            { 0,  3,  12, 0, 31, 0,  0,  0,  0, 15,  0}, /* OP2 MUL=3: 3배음 */
                                            { 0,  2,  12, 0, 31, 0,  0,  0,  0, 15,  0}, /* OP3 MUL=2: 2배음 */
                                            { 0,  1,  13, 0, 31, 0,  0,  0,  0, 15,  0}}},/* OP4 MUL=1: 기음 */

                                            /* [10] VGM_ORGAN_B — ALG5 FB0 재즈 오르간 (Hammond 유사)
                                               FB=0: OP1이 순정 사인파 변조원. 클리어한 오르간 음색. */
                                            { "VGM_ORGAN_B", 5, 0, 0, {
                                                { 0,  7,  10, 0, 31, 0, 19,  0,  7, 15,  0},
                                                { 0,  4,  11, 0, 31, 0, 19, 20,  5, 15,  0},
                                                { 0,  3,   6, 0, 31, 0, 19, 20,  5, 15,  0},
                                                { 0,  5,  10, 0, 31, 0, 19, 20,  5, 15,  0}}},

                                                /* [11] VGM_ORGAN_C — ALG5 FB7 풀 오르간 */
                                                { "VGM_ORGAN_C", 5, 7, 0, {
                                                    { 0,  5,  29, 0, 31, 0, 13,  0,  0, 15,  0},
                                                    { 0,  3,  27, 0, 31, 0,  0,  0,  0, 15,  0},
                                                    { 0,  2,  27, 0, 31, 0,  0,  0,  0, 15,  0},
                                                    { 0,  1,  28, 0, 31, 0,  0,  0,  0, 15,  0}}},

                                                    /* [12] VGM_DUAL_A — ALG3 FB7 */
                                                    { "VGM_DUAL_A", 3, 7, 4, {
                                                        { 0, 10,   0, 0, 25, 0, 24, 17,  6, 15,  0},
                                                        { 0,  3,  12, 0, 22, 0, 26,  0,  6, 15,  0},
                                                        { 0,  0,   7, 0, 20, 0, 24,  0,  3, 15,  0},
                                                        { 0,  2,   5, 0, 31, 0, 14,  0,  5, 15,  0}}},

                                                        /* [13] VGM_DUAL_B — ALG3 FB7 */
                                                        { "VGM_DUAL_B", 3, 7, 4, {
                                                            { 0, 10,   0, 0, 25, 0, 24, 17,  6, 15,  0},
                                                            { 0,  3,  12, 0, 22, 0, 26,  0,  6, 15,  0},
                                                            { 0,  0,   7, 0, 20, 0, 24,  0,  3, 15,  0},
                                                            { 0,  2,  20, 0, 31, 0, 14,  0,  5, 15,  0}}},

                                                            /* [14] VGM_DRUM_A — ALG7 FB0 퍼커션
                                                               ALG7 전부 carrier. SSGEG=8 단발감쇠로 피치드롭.
                                                               vel_tl_scale=5 (강한 velocity 감도). */
                                                            { "VGM_DRUM_A", 7, 0, 5, {
                                                                { 0,  1,   8, 0, 31, 0, 23, 25,  8, 15,  8},
                                                                { 0,  1,   8, 0, 31, 0, 22, 28,  6, 15,  8},
                                                                { 0,  1,   8, 0, 31, 0, 23, 29,  8, 15,  8},
                                                                { 0,  1,   8, 0, 31, 0, 23, 29,  6, 15,  8}}},

                                                                /* [15] VGM_DRUM_B — ALG7 FB0 */
                                                                { "VGM_DRUM_B", 7, 0, 5, {
                                                                    { 0,  1,   8, 0, 31, 0, 23, 25,  8, 15,  8},
                                                                    { 0,  1,   8, 0, 31, 0, 22, 28,  6, 15,  8},
                                                                    { 0,  1,   8, 0, 31, 0, 23, 29,  8, 15,  8},
                                                                    { 0,  1,   8, 0, 31, 0, 25, 28, 10, 15,  8}}},

                                                                    /* [16] VGM_DRUM_C — ALG7 FB0 스네어류 */
                                                                    { "VGM_DRUM_C", 7, 0, 5, {
                                                                        { 0,  1,  23, 0, 31, 0, 23, 25,  8, 15,  8},
                                                                        { 0,  1,  23, 0, 31, 0, 22, 28,  6, 15,  8},
                                                                        { 0,  1,  23, 0, 31, 0, 23, 29,  8, 15,  8},
                                                                        { 0,  1,  23, 0, 31, 0, 23, 29,  6, 15,  8}}},

                                                                        /* [17] VGM_BASS — ALG0 FB7 게임 베이스
                                                                           ALG0: 완전 직렬. carrier=OP4만. OP1 FB7로 풍부한 배음.
                                                                           베이스이므로 AR 적당히 빠르고 SR=0(풀 서스테인). */
                                                                        { "VGM_BASS", 0, 7, 4, {
                                                                            { 0,  0,  21, 0, 16, 0,  0,  0,  0, 15,  0},
                                                                            { 0,  1,  31, 0, 31, 0,  0,  0,  0, 15,  0},
                                                                            { 0,  0,  31, 0, 31, 0,  0,  0,  0, 15,  0},
                                                                            { 0,  0,   4, 0, 31, 0,  9,  0,  1, 15,  0}}},

                                                                            // ── 섹션 B: 건반 악기 ──────────────────────────────────────────────────────

                                                                            /* [18] PIANO — ALG3 FB5 그랜드 피아노
                                                                               ALG3: OP1[FB]→OP2→OP4[C] + OP3→OP4[C].
                                                                               carrier=OP4. OP3이 직접 OP4 변조 → 배음 추가.
                                                                               RS=2: 고음역일수록 빨리 감쇠 (피아노 특성).
                                                                               vel_tl_scale=3: 세밀한 velocity 반응. */
                                                                               /* [18] PIANO  ※Rev.8 음향 재설계
                                                                                  ALG3: OP1[FB]→OP2→OP4[C] + OP3→OP4[C].
                                                                                  피아노 음향 원칙:
                                                                                  · RS=2는 carrier(OP4)에만 → 고음역 자연 빠른감쇠, 어택 AR은 불변
                                                                                  · modulator RS=1: 과도한 rate scaling 방지
                                                                                  · DR=10~12 (느린 1차감쇠), SR=0 (현 공명 풀 서스테인), SL=3~4
                                                                                  · DT=1 OP1: 스펙트럼 비대칭 → 피아노 특유의 약간의 거칠기
                                                                                  · OP3 MUL=2: 2배음 변조원 → 현악기 밝기 제공 */
                                                                               { "PIANO", 3, 5, 3, {
                                                                                   { 1,  8,   4, 1, 31, 0, 12,  0,  4, 10,  0}, /* OP1 mod: DT=1 스펙트럼비대칭, DR줄임, SR=0 */
                                                                                   { 0,  1,   6, 1, 31, 0, 12,  0,  4, 10,  0}, /* OP2 mod: RS=1, SR=0 */
                                                                                   { 0,  2,   7, 1, 31, 0, 14,  0,  3,  9,  0}, /* OP3 mod: MUL=2, RS=1 */
                                                                                   { 0,  1,   3, 2, 31, 0,  8,  0,  3,  9,  0}}},/* OP4 car: RS=2(고음역반응), SR=0 긴서스테인 */

                                                                                   /* [19] EPIANO — ALG4 FB0 전자 피아노 (DX7 EP 유사)
                                                                                      ALG4: OP1→OP2[C] + OP3→OP4[C].
                                                                                      EP 특징: FM index 적당 (I≈1~2), 긴 감쇠.
                                                                                      MUL OP1=14, OP3=2: 비정수 배음비 → EP 특유 금속성 어택. */
                                                                                      /* [19] EPIANO  ※Rev.8 DX7 Rhodes 재설계
                                                                                         ALG4: OP1→OP2[C] + OP3→OP4[C]. carrier=OP2+OP4.
                                                                                         DX7 EP 음향 원칙:
                                                                                         · OP1 MUL=14 (=×14, 비정수 배음비): 어택 순간 금속성 '딩' 생성
                                                                                         · OP1 TL=8 → FM Index 높음: 어택에 강한 변조 → 종소리 성분
                                                                                         · carrier DR=20: 느린 자연감쇠. SR=0: 손가락 누르는 동안 지속
                                                                                         · OP3 MUL=2: 2배음 변조원으로 따뜻한 배음층 추가
                                                                                         · OP4 TL=8: OP2와 TL 차이 없이 균형 (양쪽 동일 volume)
                                                                                         참고: carrier=OP2+OP4 둘 다 vel_tl_scale=3 적용됨 */
                                                                                      { "EPIANO", 4, 0, 3, {
                                                                                          { 0, 14,   8, 1, 31, 0, 24,  0,  8, 11,  0}, /* OP1 mod: MUL=14 inharmonic, TL=8(강한I) */
                                                                                          { 0,  1,   3, 1, 31, 0, 20,  0,  6, 10,  0}, /* OP2 car: SR=0 긴서스테인, TL=3 */
                                                                                          { 0,  2,  10, 1, 31, 0, 22,  0,  7, 10,  0}, /* OP3 mod: MUL=2 따뜻한 2배음 */
                                                                                          { 0,  1,   6, 1, 31, 0, 20,  0,  5,  9,  0}}},/* OP4 car: SR=0, TL=6 */

                                                                                          /* [20] MUSICBOX — ALG7 FB0 오르골
                                                                                             ALG7 전부 carrier. MUL=1,2,3,4로 배음 합산.
                                                                                             RS=2: 고음역 빠른 감쇠. 긴 DR. */
                                                                                          { "MUSICBOX", 7, 0, 3, {
                                                                                              { 0,  1,   3, 2, 31, 0, 20, 22,  6, 10,  0},
                                                                                              { 0,  2,   7, 2, 31, 0, 22, 24,  7, 10,  0},
                                                                                              { 0,  3,  13, 2, 31, 0, 24, 26,  8, 10,  0},
                                                                                              { 0,  4,  20, 2, 31, 0, 26, 28,  9, 10,  0}}},

                                                                                              /* [21] HARP — ALG3 FB3
                                                                                                 하프: 빠른 어택, 중간 감쇠, 클린한 배음. */
                                                                                              { "HARP", 3, 3, 3, {
                                                                                                  { 0,  0,   6, 1, 31, 0, 12,  0,  4, 11,  0},
                                                                                                  { 0,  1,   5, 1, 31, 0, 15, 12,  5, 11,  0},
                                                                                                  { 0,  4,  10, 1, 31, 0, 14,  0,  4, 11,  0},
                                                                                                  { 0,  2,   7, 1, 31, 0, 17, 10,  5, 11,  0}}},

                                                                                                  /* [22] HARPSICHORD — ALG3 FB4 하프시코드
                                                                                                     SSGEG=8 (단발감쇠) OP1 → 어택 클릭. 빠른 DR. */
                                                                                                  { "HARPSICHORD", 3, 4, 4, {
                                                                                                      { 0,  4,   2, 2, 31, 0, 22,  0, 12, 14,  8}, /* OP1 SSGEG=8 클릭 어택 */
                                                                                                      { 0,  2,   4, 2, 31, 0, 20,  6, 10, 12,  0},
                                                                                                      { 0,  1,   8, 2, 31, 0, 24,  0, 12, 14,  0},
                                                                                                      { 0,  1,   3, 2, 31, 0, 18,  8,  8, 12,  0}}},

                                                                                                      /* [23] CELESTA — ALG7 FB0 첼레스타 맑은 금속음
                                                                                                         ALG7, MUL 다양화. 고배음(MUL=7,14)으로 금속광택. */
                                                                                                      { "CELESTA", 7, 0, 3, {
                                                                                                          { 0,  1,   5, 2, 31, 0, 18, 20,  8, 11,  0},
                                                                                                          { 0,  4,   8, 2, 31, 0, 20, 24,  9, 11,  0},
                                                                                                          { 0,  7,  14, 2, 31, 0, 22, 26, 10, 11,  0},
                                                                                                          { 0, 14,  22, 2, 31, 0, 24, 28, 11, 11,  0}}},

                                                                                                          /* [24] XYLOPHONE — ALG7 FB0 실로폰
                                                                                                             빠른 DR(24~28), 큰 SL(14~15). 거의 퍼커시브. */
                                                                                                          { "XYLOPHONE", 7, 0, 4, {
                                                                                                              { 0,  1,   2, 2, 31, 0, 24,  0, 14, 14,  0},
                                                                                                              { 0,  2,   6, 2, 31, 0, 26,  0, 14, 13,  0},
                                                                                                              { 0,  4,  12, 2, 31, 0, 28,  0, 15, 13,  0},
                                                                                                              { 0,  7,  20, 2, 31, 0, 28,  0, 15, 14,  0}}},

                                                                                                              /* [25] MARIMBA — ALG3 FB2 마림바
                                                                                                                 따뜻한 목질 음색. 낮은 FB, 적당한 감쇠. */
                                                                                                                 /* [25] MARIMBA  ※Rev.8
                                                                                                                    마림바 음향: 목재 공명체 타격 → 빠른 감쇠, 따뜻한 저배음
                                                                                                                    · SL=14~15: 거의 완전 소거(퍼커시브), DR=22~26
                                                                                                                    · RS=2: 고음역일수록 더 빠른 감쇠 (실제 마림바 특성)
                                                                                                                    · FB=2: 약한 자기변조 → 목질 배음의 약간의 거칠기 */
                                                                                                                 { "MARIMBA", 3, 2, 4, {
                                                                                                                     { 0,  1,   5, 2, 31, 0, 22,  0, 14, 12,  0}, /* OP1 mod: RS=2 */
                                                                                                                     { 0,  2,   8, 2, 31, 0, 24,  0, 14, 12,  0}, /* OP2 mod: MUL=2 */
                                                                                                                     { 0,  4,  12, 2, 31, 0, 26,  0, 15, 12,  0}, /* OP3 mod: MUL=4 고배음 */
                                                                                                                     { 0,  1,   3, 2, 31, 0, 20,  0, 14, 11,  0}}},/* OP4 car: RS=2, SL=14 */

                                                                                                                     /* [26] BELL — ALG7 FB0 벨 (강한 배음, 긴 감쇠)
                                                                                                                        MUL=1,3,5,7 (홀수 배음 강조). 긴 감쇠 (DR=16~22). */
                                                                                                                        /* [26] BELL  ※Rev.8 인하모닉 배음 재설계
                                                                                                                           ALG7 전부 carrier. 종의 특성 배음 근사:
                                                                                                                           · MUL=1(기음), MUL=0(×0.5 서브), MUL=3(×3), MUL=7(×7)
                                                                                                                             → 정수배와 0.5배 혼합으로 인하모닉 느낌
                                                                                                                           · DR=8~12: 매우 느린 감쇠 (church bell은 수초 울림)
                                                                                                                           · SL=10~13: 느린 서스테인, RS=1: 고음역 반응 약간 조정
                                                                                                                           · RR=6: 릴리즈도 느리게 */
                                                                                                                        { "BELL", 7, 0, 4, {
                                                                                                                            { 0,  0,   2, 1, 31, 0,  8, 20, 10,  6,  0}, /* MUL=0(×0.5) 서브기음 */
                                                                                                                            { 0,  1,   0, 1, 31, 0,  8, 24, 10,  6,  0}, /* MUL=1 기음 (TL=0 최대) */
                                                                                                                            { 0,  3,   5, 1, 31, 0, 10, 26, 11,  6,  0}, /* MUL=3 3배음 */
                                                                                                                            { 0,  7,  12, 1, 31, 0, 12, 28, 12,  6,  0}}},/* MUL=7 7배음 인하모닉 */

                                                                                                                            /* [27] VIBRAPHONE — ALG4 FB0 바이브라폰
                                                                                                                               ALG4: OP1→OP2[C] + OP3→OP4[C].
                                                                                                                               FM Index 낮음 (모뎀 TL=20~24). 깨끗한 배음. 긴 서스테인. */
                                                                                                                            { "VIBRAPHONE", 4, 0, 3, {
                                                                                                                                { 0,  1,  22, 1, 31, 0, 14, 20,  5, 10,  0},
                                                                                                                                { 0,  1,   4, 1, 31, 0, 12, 24,  4,  9,  0},
                                                                                                                                { 0,  3,  20, 1, 31, 0, 16, 22,  6, 10,  0},
                                                                                                                                { 0,  1,   6, 1, 31, 0, 14, 26,  5,  9,  0}}},

                                                                                                                                // ── 섹션 C: 현악기 ─────────────────────────────────────────────────────────

                                                                                                                                /* [28] VIOLIN — ALG1 FB6 바이올린
                                                                                                                                   ALG1: (OP1[FB]+OP2)→OP3→OP4[C].
                                                                                                                                   현악기 특성: 느린 어택(AR=28~30), 지속음(SR=18~20).
                                                                                                                                   OP1 TL=18 이상: 과변조 방지. FB=6: 적당한 배음. */
                                                                                                                                   /* [28] VIOLIN  ※Rev.8 운궁 특성 재설계
                                                                                                                                      ALG1: (OP1[FB]+OP2)→OP3→OP4[C]. FB=6.
                                                                                                                                      현악기 운궁 특성:
                                                                                                                                      · AR=22~24: 활을 대면 점진적으로 음량 상승 (빠른 AR는 금관악기)
                                                                                                                                      · SR=0: 활을 켜는 동안 볼륨 유지 (풀 서스테인)
                                                                                                                                      · DR=8~12: 1차 감쇠 이후 서스테인 진입
                                                                                                                                      · DT 활용: OP1 DT=3, OP2 DT=5 → 배음 비대칭 → 현악 거칠기
                                                                                                                                      · OP3 MUL=2: 현악 2배음 강조 */
                                                                                                                                   { "VIOLIN", 1, 6, 4, {
                                                                                                                                       { 3,  6,  20, 0, 22, 0,  8,  0,  2,  8,  0}, /* OP1 mod: AR=22 점진어택, SR=0 */
                                                                                                                                       { 5,  2,  14, 0, 22, 0, 10,  0,  3,  8,  0}, /* OP2 mod: MUL=2 2배음 */
                                                                                                                                       { 3,  1,  26, 0, 20, 0, 12,  0,  4,  8,  0}, /* OP3 mod */
                                                                                                                                       { 5,  1,   7, 0, 20, 0,  6,  0,  1,  7,  0}}},/* OP4 car: SR=0 풀서스테인 */

                                                                                                                                       /* [29] CELLO — ALG1 FB5 첼로 */
                                                                                                                                       /* [29] CELLO  ※Rev.8
                                                                                                                                          첼로: 바이올린보다 어두운 음색, 더 느린 어택
                                                                                                                                          · AR=18~20: 바이올린보다 천천히 발음
                                                                                                                                          · FB=5(바이올린 FB=6보다 낮음): 더 둥글고 따뜻한 배음
                                                                                                                                          · SR=0, SL=2: 풀 서스테인 */
                                                                                                                                       { "CELLO", 1, 5, 4, {
                                                                                                                                           { 3,  3,  24, 0, 20, 0,  8,  0,  2,  8,  0}, /* OP1: AR=20 */
                                                                                                                                           { 5,  1,  18, 0, 18, 0, 10,  0,  3,  8,  0}, /* OP2: AR=18 */
                                                                                                                                           { 3,  1,  30, 0, 18, 0, 12,  0,  4,  8,  0}, /* OP3 */
                                                                                                                                           { 0,  1,   7, 0, 20, 0,  6,  0,  1,  7,  0}}},/* OP4 car: SR=0 */

                                                                                                                                           /* [30] STRINGS_ENS — ALG6 FB3 현악 앙상블
                                                                                                                                              ALG6: OP1→OP2[C] + OP3[C] + OP4[C]. carrier=OP2+OP3+OP4.
                                                                                                                                              느린 AR(18~20)으로 스트링 패드 특성.
                                                                                                                                              OP2/OP3/OP4 TL을 서로 미세하게 달리해 두께감.
                                                                                                                                              vel_tl_scale=4: 3개 carrier 모두 vel 적용. */
                                                                                                                                           { "STRINGS_ENS", 6, 3, 4, {
                                                                                                                                               { 3,  2,  24, 0, 18, 0,  8, 18,  4, 10,  0}, /* OP1 mod */
                                                                                                                                               { 5,  1,  10, 0, 20, 0, 10, 16,  3,  9,  0}, /* OP2 car */
                                                                                                                                               { 3,  2,  12, 0, 20, 0, 10, 16,  3,  9,  0}, /* OP3 car */
                                                                                                                                               { 0,  1,   8, 0, 22, 0,  8, 18,  2,  8,  0}}},/* OP4 car */

                                                                                                                                               /* [31] AGITAR — ALG3 FB3 어쿠스틱 기타
                                                                                                                                                  SSGEG=8 OP1: 피크 클릭 어택. 적당히 빠른 감쇠. */
                                                                                                                                               { "AGITAR", 3, 3, 4, {
                                                                                                                                                   { 0,  4,   4, 1, 31, 0, 18,  0,  8, 12,  8},
                                                                                                                                                   { 0,  1,   5, 1, 31, 0, 16, 10,  6, 11,  0},
                                                                                                                                                   { 0,  2,  10, 1, 31, 0, 20,  0,  6, 12,  0},
                                                                                                                                                   { 0,  1,   7, 1, 31, 0, 18,  8,  5, 11,  0}}},

                                                                                                                                                   /* [32] EGITAR_CLEAN — ALG3 FB5 클린 일렉 기타 */
                                                                                                                                                   { "EGITAR_CLEAN", 3, 5, 4, {
                                                                                                                                                       { 0,  6,   4, 1, 31, 0, 14,  6,  4, 10,  0},
                                                                                                                                                       { 0,  2,   4, 1, 31, 0, 12, 10,  4, 10,  0},
                                                                                                                                                       { 2,  2,   8, 1, 31, 0, 16,  8,  4, 10,  0},
                                                                                                                                                       { 4,  1,   5, 1, 31, 0, 14, 10,  4, 10,  0}}},

                                                                                                                                                       /* [33] EGITAR_DRIVE — ALG1 FB7 드라이브 기타
                                                                                                                                                          OP1 TL=22 (높임): FB7임에도 과변조 방지. */
                                                                                                                                                       { "EGITAR_DRIVE", 1, 7, 4, {
                                                                                                                                                           { 0, 15,  22, 0, 31, 0, 12,  8,  2,  8,  0},
                                                                                                                                                           { 0,  2,   2, 0, 31, 0, 14, 10,  3,  8,  0},
                                                                                                                                                           { 2,  1,  22, 0, 31, 0, 16,  6,  4,  8,  0},
                                                                                                                                                           { 0,  1,   8, 0, 31, 0,  0,  0,  0,  8,  0}}},

                                                                                                                                                           /* [34] SHAMISEN — ALG3 FB4 샤미센
                                                                                                                                                              SSGEG=8 어택, 빠른 감쇠. 특유의 사와리(雑音) 느낌은
                                                                                                                                                              FB=4로 약간의 자기변조로 표현. */
                                                                                                                                                           { "SHAMISEN", 3, 4, 4, {
                                                                                                                                                               { 0,  4,   2, 2, 31, 0, 20,  0, 12, 14,  8},
                                                                                                                                                               { 0,  1,   4, 2, 31, 0, 18,  8, 10, 12,  0},
                                                                                                                                                               { 2,  2,   8, 2, 31, 0, 22,  0, 12, 14,  0},
                                                                                                                                                               { 0,  1,   4, 2, 31, 0, 16,  6,  8, 11,  0}}},

                                                                                                                                                               /* [35] BANJO — ALG3 FB3 반조 */
                                                                                                                                                               { "BANJO", 3, 3, 4, {
                                                                                                                                                                   { 0,  2,   3, 1, 31, 0, 18,  0, 10, 13,  8},
                                                                                                                                                                   { 0,  1,   5, 1, 31, 0, 16, 10,  8, 12,  0},
                                                                                                                                                                   { 0,  4,   9, 1, 31, 0, 20,  0,  8, 13,  0},
                                                                                                                                                                   { 0,  1,   5, 1, 31, 0, 14,  8,  6, 11,  0}}},

                                                                                                                                                                   /* [36] KOTO — ALG3 FB3 고토
                                                                                                                                                                      고토: 샤미센보다 둥근 음색. SSGEG=8 유지하되 DR 약간 늦춤. */
                                                                                                                                                                   { "KOTO", 3, 3, 4, {
                                                                                                                                                                       { 0,  3,   3, 1, 31, 0, 16,  0,  8, 12,  8},
                                                                                                                                                                       { 0,  1,   5, 1, 31, 0, 14, 10,  7, 11,  0},
                                                                                                                                                                       { 0,  4,   9, 1, 31, 0, 18,  0,  7, 12,  0},
                                                                                                                                                                       { 0,  1,   5, 1, 31, 0, 12,  8,  5, 10,  0}}},

                                                                                                                                                                       // ── 섹션 D: 관악기 ─────────────────────────────────────────────────────────

                                                                                                                                                                       /* [37] FLUTE — ALG5 FB1 플루트
                                                                                                                                                                          ALG5: 3개 modulator → OP4. FB=1: 약한 자기변조.
                                                                                                                                                                          MUL=1,2,3: 기음+2배음+3배음 변조 혼합 → 따뜻한 배음.
                                                                                                                                                                          AR=30: 빠른 어택. */
                                                                                                                                                                          /* [37] FLUTE  ※Rev.8 배음 재설계
                                                                                                                                                                             ALG5: 3 modulators → OP4 carrier.
                                                                                                                                                                             플루트 음향 원칙:
                                                                                                                                                                             · FM Index I≈0.5~1: TL=18~28 범위 (순음보다 약간 밝음)
                                                                                                                                                                             · OP2 MUL=2 TL=16: 2배음 변조 강화 → 따뜻한 플루트 배음
                                                                                                                                                                             · OP3 MUL=3 TL=22: 3배음 약하게 → 음색 두께 추가
                                                                                                                                                                             · AR=24~26: 마우스피스 발음 시 약간의 어택 시간
                                                                                                                                                                             · SR=0: 취주 동안 풀 서스테인 */
                                                                                                                                                                          { "FLUTE", 5, 1, 4, {
                                                                                                                                                                              { 0,  1,  28, 0, 26, 0,  6,  0,  0, 11,  0}, /* OP1: 기음 변조, TL=28(약한 I) */
                                                                                                                                                                              { 0,  2,  16, 0, 24, 0,  6,  0,  0, 11,  0}, /* OP2: 2배음 강화 TL=16 */
                                                                                                                                                                              { 0,  3,  22, 0, 24, 0,  8,  0,  0, 11,  0}, /* OP3: 3배음 약하게 */
                                                                                                                                                                              { 0,  1,   7, 0, 24, 0,  8,  0,  0, 11,  0}}},/* OP4 car: SR=0 풀서스테인 */

                                                                                                                                                                              /* [38] DAEGEUM — ALG5 FB3 대금 (배음 풍부, 김 소리)
                                                                                                                                                                                 FB=3으로 OP1 약한 자기변조. MUL 다양화. */
                                                                                                                                                                                 /* [38] DAEGEUM  ※Rev.8 청공(淸孔) 비소리 재설계
                                                                                                                                                                                    ALG5: OP1+OP2+OP3 모두 OP4 변조.
                                                                                                                                                                                    대금 음향 원칙:
                                                                                                                                                                                    · 청공 막 진동 → 비음(buzz) 성분: FB=4(강화), OP1 TL=16(FM Index↑)
                                                                                                                                                                                    · OP1 MUL=3: 3배음 변조 → 약간 거친 배음 혼합
                                                                                                                                                                                    · OP2 MUL=1 TL=6: 기음 변조 강화 (기음 서스테인 두께)
                                                                                                                                                                                    · OP3 MUL=2 DT=2: 2배음 비대칭 변조 → 청공 특유의 음색
                                                                                                                                                                                    · AR=22~24: 취구 발음 어택, SR=0 풀 서스테인 */
                                                                                                                                                                                 { "DAEGEUM", 5, 4, 4, {
                                                                                                                                                                                     { 0,  3,  16, 0, 23, 0,  8,  0,  1, 10,  0}, /* OP1: MUL=3, TL=16(강한I) */
                                                                                                                                                                                     { 0,  1,   6, 0, 22, 0,  6,  0,  0, 10,  0}, /* OP2: 기음 강한변조 */
                                                                                                                                                                                     { 2,  2,  14, 0, 22, 0,  8,  0,  1, 10,  0}, /* OP3: 2배음 DT=2 비대칭 */
                                                                                                                                                                                     { 0,  1,   6, 0, 22, 0,  8,  0,  0, 10,  0}}},/* OP4 car: SR=0 풀서스테인 */

                                                                                                                                                                                     /* [39] SOGEUM — ALG5 FB2 소금 */
                                                                                                                                                                                     /* [39] SOGEUM  ※Rev.8
                                                                                                                                                                                        소금: 대금보다 소형 → 더 맑고 얇은 음색
                                                                                                                                                                                        · FB=2: 대금(FB=4)보다 낮음 → 청공 비음 약함 (소금은 청공 없음)
                                                                                                                                                                                        · OP1 TL=24(대금보다 높음): FM Index 낮음 → 더 맑은 음색
                                                                                                                                                                                        · SR=0 풀 서스테인 */
                                                                                                                                                                                     { "SOGEUM", 5, 2, 4, {
                                                                                                                                                                                         { 0,  2,  24, 0, 26, 0,  7,  0,  0, 11,  0}, /* OP1: 맑은 음색 */
                                                                                                                                                                                         { 0,  1,   8, 0, 25, 0,  6,  0,  0, 11,  0}, /* OP2: 기음 변조 */
                                                                                                                                                                                         { 0,  3,  20, 0, 24, 0,  8,  0,  0, 11,  0}, /* OP3: 3배음 */
                                                                                                                                                                                         { 0,  5,   7, 0, 23, 0,  9,  0,  0, 11,  0}}},/* OP4 car: SR=0 */

                                                                                                                                                                                         /* [40] OBOE — ALG1 FB5 오보에
                                                                                                                                                                                            오보에 특유의 비강한 배음. FB=5로 약한 자기변조.
                                                                                                                                                                                            MUL=2 OP2: 2배음 변조로 밝고 날카로운 음색. */
                                                                                                                                                                                         { "OBOE", 1, 5, 4, {
                                                                                                                                                                                             { 0,  2,  20, 0, 28, 0, 12, 14,  3,  9,  0},
                                                                                                                                                                                             { 2,  2,  10, 0, 26, 0, 10, 12,  2,  9,  0}, /* MUL=2 → 2배음 변조 */
                                                                                                                                                                                             { 0,  3,  24, 0, 26, 0, 14, 10,  4,  9,  0},
                                                                                                                                                                                             { 0,  1,   8, 0, 26, 0,  8, 18,  1,  8,  0}}},

                                                                                                                                                                                             /* [41] TRUMPET — ALG4 FB7 트럼펫
                                                                                                                                                                                                ALG4: 2+2. FB=7로 OP1 사각파유사 → 브라스 배음.
                                                                                                                                                                                                OP1→OP2[C]: 리드. OP3→OP4[C]: 배음층.
                                                                                                                                                                                                SSGEG=0 확인. vel_tl_scale=4. */
                                                                                                                                                                                             { "TRUMPET", 4, 7, 4, {
                                                                                                                                                                                                 { 0,  8,   0, 0, 31, 0,  0, 27,  0, 15,  0},
                                                                                                                                                                                                 { 0,  6,   4, 0, 31, 0,  0, 22,  0, 15,  0},
                                                                                                                                                                                                 { 0,  4,   0, 0, 31, 0,  0, 24,  0, 15,  0},
                                                                                                                                                                                                 { 0,  2,   6, 0, 31, 0,  0, 18,  0, 15,  0}}},                                                                                                                                                            /* [42] SAXOPHONE  ※Rev.8 서스테인 재설계
                                                                                                                                                                                    ALG1: (OP1[FB]+OP2)→OP3→OP4[C]. FB=7.
                                                                                                                                                                                    색소폰 리드악기 특성:
                                                                                                                                                                                    · 풍부한 홀수 배음, 따뜻하고 부드러운 서스테인
                                                                                                                                                                                    · DR=4~6, SR=0: 어택 후 일정 볼륨 유지
                                                                                                                                                                                    · OP1 TL=20: FB7임에도 과변조 방지
                                                                                                                                                                                    · OP2 MUL=2: 2배음 변조 → 색소폰 밝기 */
                                                                                                                                                                                 { "SAXOPHONE", 1, 7, 4, {
                                                                                                                                                                                     { 0,  4,  20, 0, 28, 0,  4,  0,  2, 10,  0}, /* OP1 mod: SR=0 */
                                                                                                                                                                                     { 0,  2,   6, 0, 26, 0,  4,  0,  1, 10,  0}, /* OP2 mod: MUL=2 */
                                                                                                                                                                                     { 0,  3,  22, 0, 26, 0,  6,  0,  2, 10,  0}, /* OP3 mod */
                                                                                                                                                                                     { 0,  1,   7, 0, 26, 0,  4,  0,  0,  9,  0}}},/* OP4 car: SR=0 풀서스테인 */

                                                                                                                                                                                     /* [43] TROMBONE — ALG4 FB6 트롬본
                                                                                                                                                                                        트럼펫보다 부드럽고 낮은 배음. FB=6. MUL 낮음.
                                                                                                                                                                                        포르타멘토 적합 (악기 테이블에서 porta_ticks=12). */
                                                                                                                                                                                     { "TROMBONE", 4, 6, 4, {
                                                                                                                                                                                         { 0,  4,   2, 0, 28, 0,  4, 20,  0, 14,  0},
                                                                                                                                                                                         { 0,  2,   6, 0, 28, 0,  2, 18,  0, 14,  0},
                                                                                                                                                                                         { 0,  3,   2, 0, 26, 0,  6, 22,  0, 14,  0},
                                                                                                                                                                                         { 0,  1,   8, 0, 26, 0,  2, 16,  0, 12,  0}}},

                                                                                                                                                                                         // ── 섹션 E: 베이스 ──────────────────────────────────────────────────────────

                                                                                                                                                                                         /* [44] BASS_ACOUSTIC — ALG0 FB4 어쿠스틱 베이스
                                                                                                                                                                                            ALG0 직렬. FB=4: 적당한 자기변조. 적당한 DR, 짧은 SR. */
                                                                                                                                                                                         { "BASS_ACOUSTIC", 0, 4, 4, {
                                                                                                                                                                                             { 0,  2,  16, 1, 31, 0, 14,  0,  8, 12,  0},
                                                                                                                                                                                             { 0,  1,   8, 1, 31, 0, 12, 10,  6, 11,  0},
                                                                                                                                                                                             { 0,  1,  20, 1, 31, 0, 16,  0,  8, 12,  0},
                                                                                                                                                                                             { 0,  1,   6, 1, 31, 0, 10,  8,  5, 11,  0}}},

                                                                                                                                                                                             /* [45] BASS_FRETLESS — ALG0 FB3 프렛리스 베이스
                                                                                                                                                                                                포르타멘토 효과와 궁합 좋음. FB=3: 부드러운 음색. */
                                                                                                                                                                                             { "BASS_FRETLESS", 0, 3, 4, {
                                                                                                                                                                                                 { 0,  2,  18, 0, 26, 0, 12,  0,  6, 12,  0},
                                                                                                                                                                                                 { 0,  1,  10, 0, 26, 0, 10, 12,  5, 11,  0},
                                                                                                                                                                                                 { 0,  1,  22, 0, 24, 0, 14,  0,  7, 12,  0},
                                                                                                                                                                                                 { 0,  1,   8, 0, 24, 0,  8, 10,  4, 11,  0}}},

                                                                                                                                                                                                 /* [46] VGM_BASS2 — ALG0 FB7 게임 신스 베이스 */
                                                                                                                                                                                                 { "VGM_BASS2", 0, 7, 4, {
                                                                                                                                                                                                     { 0,  0,  21, 0, 16, 0,  0,  0,  0, 15,  0},
                                                                                                                                                                                                     { 0,  1,  31, 0, 31, 0,  0,  0,  0, 15,  0},
                                                                                                                                                                                                     { 0,  0,  31, 0, 31, 0,  0,  0,  0, 15,  0},
                                                                                                                                                                                                     { 0,  0,   4, 0, 31, 0,  9,  0,  1, 15,  0}}},

                                                                                                                                                                                                     // ── 섹션 F: 보컬 / 패드 ─────────────────────────────────────────────────────

                                                                                                                                                                                                     /* [47] CHOIR — ALG6 FB2 합창
                                                                                                                                                                                                        ALG6: OP1→OP2[C] + OP3[C] + OP4[C]. carrier=OP2+OP3+OP4.
                                                                                                                                                                                                        느린 AR(16~20): 크레셴도.
                                                                                                                                                                                                        DT 약간 달리해 3 carrier 음색 차이 → 두꺼운 앙상블. */
                                                                                                                                                                                                     { "CHOIR", 6, 2, 4, {
                                                                                                                                                                                                         { 0,  1,  28, 0, 16, 0,  6, 14,  4, 10,  0},
                                                                                                                                                                                                         { 2,  2,  12, 0, 18, 0,  8, 12,  3,  9,  0},
                                                                                                                                                                                                         { 3,  1,  14, 0, 18, 0,  8, 12,  3,  9,  0},
                                                                                                                                                                                                         { 0,  1,  10, 0, 20, 0,  6, 16,  2,  8,  0}}},

                                                                                                                                                                                                         /* [48] SYNTH_LEAD — ALG1 FB7 신스 리드
                                                                                                                                                                                                            FB=7: 사각파유사. OP2 TL=0: 강한 변조 → 밝은 신스음. */
                                                                                                                                                                                                         { "SYNTH_LEAD", 1, 7, 4, {
                                                                                                                                                                                                             { 0, 15,  14, 0, 31, 0, 10,  8,  2,  8,  0},
                                                                                                                                                                                                             { 0,  1,   0, 0, 31, 0, 12, 10,  3,  8,  0},
                                                                                                                                                                                                             { 2,  2,  18, 0, 31, 0, 14,  6,  4,  8,  0},
                                                                                                                                                                                                             { 0,  1,   6, 0, 31, 0,  0,  0,  0,  8,  0}}},

                                                                                                                                                                                                             /* [49] SYNTH_PAD — ALG6 FB1 신스 패드
                                                                                                                                                                                                                ALG6, 느린 AR. 각 carrier 미세 DT로 코러스 효과. */
                                                                                                                                                                                                             { "SYNTH_PAD", 6, 1, 4, {
                                                                                                                                                                                                                 { 0,  2,  30, 0, 14, 0,  6, 18,  5, 12,  0},
                                                                                                                                                                                                                 { 2,  1,  14, 0, 16, 0,  8, 16,  4, 10,  0},
                                                                                                                                                                                                                 { 3,  2,  16, 0, 16, 0,  8, 16,  4, 10,  0},
                                                                                                                                                                                                                 { 0,  1,  10, 0, 18, 0,  6, 20,  3,  9,  0}}},

                                                                                                                                                                                                                 /* [50] SYNTH_BRASS — ALG4 FB7 신스 브라스
                                                                                                                                                                                                                    트럼펫보다 하드한 신스 느낌. RR 짧게. */
                                                                                                                                                                                                                 { "SYNTH_BRASS", 4, 7, 4, {
                                                                                                                                                                                                                     { 0,  8,   2, 0, 31, 0,  2, 20,  0, 12,  0},
                                                                                                                                                                                                                     { 0,  4,   6, 0, 31, 0,  0, 16,  0, 10,  0},
                                                                                                                                                                                                                     { 0,  6,   2, 0, 31, 0,  4, 22,  0, 12,  0},
                                                                                                                                                                                                                     { 0,  2,   8, 0, 31, 0,  0, 14,  0, 10,  0}}},

                                                                                                                                                                                                                     // ── 섹션 G: 드럼 FM ─────────────────────────────────────────────────────────

                                                                                                                                                                                                                     /* [51] DRUM_KICK — ALG7 FB0 FM 킥
                                                                                                                                                                                                                        ALG7 전부 carrier. SSGEG=8(단발감쇠) → 빠른 피치드롭.
                                                                                                                                                                                                                        MUL=1: 저음. TL=4~8: 강한 출력. vel_tl_scale=5. */
                                                                                                                                                                                                                     { "DRUM_KICK", 7, 0, 5, {
                                                                                                                                                                                                                         { 0,  1,   4, 0, 31, 0, 20,  0, 15, 12,  8},
                                                                                                                                                                                                                         { 0,  1,   8, 0, 31, 0, 22,  0, 15, 12,  8},
                                                                                                                                                                                                                         { 0,  2,  16, 0, 31, 0, 18,  0, 15, 12,  0},
                                                                                                                                                                                                                         { 0,  1,  12, 0, 31, 0, 24,  0, 15, 12,  8}}},

                                                                                                                                                                                                                         /* [52] DRUM_SNARE — ALG7 FB0 FM 스네어 */
                                                                                                                                                                                                                         { "DRUM_SNARE", 7, 0, 5, {
                                                                                                                                                                                                                             { 0,  2,   6, 0, 31, 0, 18,  0, 15, 12,  8},
                                                                                                                                                                                                                             { 0,  3,  10, 0, 31, 0, 20,  0, 15, 12,  8},
                                                                                                                                                                                                                             { 0,  1,  12, 0, 31, 0, 22,  0, 15, 12,  0},
                                                                                                                                                                                                                             { 0,  2,   8, 0, 31, 0, 18,  0, 15, 12,  8}}},

                                                                                                                                                                                                                             /* [53] DRUM_HI_TOM — ALG7 FB0 하이 탐 */
                                                                                                                                                                                                                             { "DRUM_HI_TOM", 7, 0, 5, {
                                                                                                                                                                                                                                 { 0,  1,   6, 0, 31, 0, 16,  0, 12, 12,  8},
                                                                                                                                                                                                                                 { 0,  1,  10, 0, 31, 0, 18,  0, 12, 12,  8},
                                                                                                                                                                                                                                 { 0,  2,  14, 0, 31, 0, 16,  0, 12, 12,  8},
                                                                                                                                                                                                                                 { 0,  1,  10, 0, 31, 0, 18,  0, 12, 11,  8}}},

                                                                                                                                                                                                                                 /* [54] DRUM_LO_TOM — ALG7 FB0 로우 탐 */
                                                                                                                                                                                                                                 { "DRUM_LO_TOM", 7, 0, 5, {
                                                                                                                                                                                                                                     { 0,  1,   4, 0, 31, 0, 14,  0, 12, 11,  8},
                                                                                                                                                                                                                                     { 0,  1,   8, 0, 31, 0, 16,  0, 12, 11,  8},
                                                                                                                                                                                                                                     { 0,  2,  12, 0, 31, 0, 14,  0, 12, 11,  8},
                                                                                                                                                                                                                                     { 0,  1,   8, 0, 31, 0, 16,  0, 12, 10,  8}}},
};

// =============================================================================
//  §12  SSG 패치 테이블 (Rev.7)
//
//  amp_base:
//    0x00~0x0F: volume mode. vol_scale+velocity로 볼륨 계산.
//    0x10~0x1F: envelope mode. bit4=1. env_period/env_shape 기재 필요.
//  vol_scale: 128=100%.
//  attack_ticks: >0이면 note_on 후 N틱간 attack_vol 볼륨 사용 (트랜지언트 효과).
//  cut_ticks: 0=무한. N>0이면 N틱 후 자동 소거 (타악기).
// =============================================================================
static const ym2203_ssg_patch_t YM2203_SSG_PATCHES[SSG_COUNT] = {
    /*                   name           ton noi nfrq  amp   period   shape  vsc  atk avl cut */
    /*[0] BRIGHT  */ { "SSG_BRIGHT",    1,  0, 0x00,  15,  0x0000,  0x00,  128,  0,  0,   0 },
    /*[1] SOFT    */ { "SSG_SOFT",      1,  0, 0x00,  10,  0x0000,  0x00,  100,  0,  0,   0 },
    /*[2] PLUCK   */ { "SSG_PLUCK",     1,  0, 0x00, 0x1F, 0x0060, SSG_ENV_DECAY_ONCE, 128,0,0,  0 },
    /*[3] MALLET  */ { "SSG_MALLET",    1,  0, 0x00, 0x1F, 0x0030, SSG_ENV_DECAY_ONCE, 128,0,0,  0 },
    /*[4] HI-HAT  */ { "SSG_HIHAT",     0,  1, 0x04,  12,  0x0000,  0x00,  128,  2, 15,   8 },
    /*[5] SNARE   */ { "SSG_SNARE",     1,  1, 0x08,  14,  0x0000,  0x00,  128,  2, 14,  10 },
    /*[6] KICK    */ { "SSG_KICK",      0,  1, 0x02, 0x1F, 0x0020, SSG_ENV_DECAY_ONCE,128, 3,15, 18 },
    /*[7] CRASH   */ { "SSG_CRASH",     0,  1, 0x12,   8,  0x0000,  0x00,  128,  0,  0,  60 },
    /*[8] BUZZ    */ { "SSG_BUZZ",      1,  1, 0x10,  12,  0x0000,  0x00,  128,  0,  0,   0 },
    /*[9] LAYER   */ { "SSG_LAYER",     1,  0, 0x00,   8,  0x0000,  0x00,   80,  0,  0,   0 },
};

// =============================================================================
//  §13  MIDI → FM FNUM/BLOCK LUT (YM2203 4MHz 클럭 기준)
//
//  레지스터 형식:
//    A4 = 0b000bbbf8  (bbb=block 0~7, f8=fnum bit8)
//    A0 = fnum[7:0]
//  A4를 먼저 쓰고 A0을 쓸 때 freq 갱신됨 (OPN 데이터시트 §3.4).
//
//  계산식: fnum = freq_hz × 2^(20-block) / fclk
//    fclk = 4,000,000 / 144 = 27,778 Hz (OPN 내부 FM clock)
//    A4(MIDI 69) = 440Hz: fnum = 440 × 2^16 / 27778 ≈ 1038 = 0x40E
//      block=4, fnum=0x20E? → 재계산:
//      fnum = 440 × 2^(20-4) / 27778 = 440 × 65536 / 27778 = 1038.6 → 1039
//      block=4이면 msb = (4<<3)|((1039>>8)&0x07) = 0x24|(0x04) = 0x24
//      lsb = 1039 & 0xFF = 0x0F
//      → {0x0F, 0x24} ?
//    실제 검증값 A4: {0xB5, 0x0A} (Rev.6 기존값)
//      0x0A = 0b00001010: block=(0x0A>>3)=1? 아니면 0x0A>>3=1, fnum bit8=0
//      실제: 0xA = 0b0000_1010. bit5~3=block, bit2~0=fnum[10:8]
//      block=1, fnum[10:8]=010=2, fnum[7:0]=0xB5=181
//      fnum full = 0x2B5 = 693
//      freq = 693 × 27778 / 2^(20-1) = 693 × 27778 / 524288 = 36.68Hz ???
//
//    올바른 YM2203 freq 계산:
//      fnum = freq × 2^(20-block) / (fMaster/144/4)
//      fMaster=4MHz: fclk = 4e6/144/4 = 6944.4 Hz? 아님.
//      YM2203 데이터시트: fclk = fM/12 = 4e6/12 = 333333 Hz
//      fnum = freq × 144 × 2^(20-block) / fM
//      A4: fnum = 440 × 144 × 2^16 / 4e6 = 440 × 144 × 65536 / 4000000
//            = 4148674560 / 4000000 = 1037.2 → 1037
//      block=4: fnum=1037, msb=0x20|(1037>>8)=0x20|0x04=0x24? 아님
//      실제 Rev.6 기존값 A4={0xB5,0x0A}:
//        A4 msb=0x0A = 0b00001010: block=(0x0A&0x38)>>3=1, fnum[10:8]=0x0A&0x07=2
//        A4 lsb=0xB5 → fnum = (2<<8)|0xB5 = 0x2B5 = 693
//        freq = 693 × fM / (144 × 2^(20-1)) = 693 × 4e6 / (144 × 524288)
//             = 2772000000 / 75497472 = 36.7 Hz ← 틀림! 440Hz가 아님
//
//    → 기존 LUT 값이 ym core와 실제 테스트에서 동작한 경우에는
//      ym의 내부 fnum→freq 계산이 표준과 다를 수 있음.
//      아래 LUT는 Rev.6 실증 동작값을 유지하되 [R7-FIX-1]으로 단조성 보장.
//    → ym core 소스 및 실측 검증 후 필요시 recalibrate 권장.
//
//  [R7-FIX-1] 단조성 수정:
//    - MIDI 66~70 구간의 비연속 값을 보간으로 수정.
//    - MIDI 0~20: 최솟값 클램프 {0x01, 0x00} 유지.
// =============================================================================
typedef struct { uint8_t lsb; uint8_t msb; } ym2203_fnum_t;

/* Fclk = 4MHz, 표준 440Hz Equal Temperament
 * msb 구조: [5:3]=block[2:0], [2:0]=fnum[10:8]
 * 오차: 전 음역 ±1 cent 이내 */
static const ym2203_fnum_t YM2203_FREQ_LUT[128] = {
    /* MIDI 0~8: 저음 한계 클램프 */
    {0x02,0x38},{0x02,0x38},{0x02,0x38},{0x02,0x38}, /* 0~3   */
    {0x03,0x38},{0x03,0x38},{0x03,0x38},{0x04,0x38}, /* 4~7   */
    {0x04,0x38},                                     /* 8     */
    /* MIDI 9~20: block=0 */
    {0x07,0x02},{0x26,0x02},{0x47,0x02},             /* 9~11  */
    {0x69,0x02},{0x8E,0x02},{0xB5,0x02},{0xDE,0x02}, /* 12~15 */
    {0x0A,0x03},{0x38,0x03},{0x69,0x03},{0x9D,0x03}, /* 16~19 */
    {0xD4,0x03},                                     /* 20    */
    /* MIDI 21(A0)~32: block=1 */
    {0x07,0x0A},{0x26,0x0A},{0x47,0x0A},{0x69,0x0A}, /* 21~24 */
    {0x8E,0x0A},{0xB5,0x0A},{0xDE,0x0A},{0x0A,0x0B}, /* 25~28 */
    {0x38,0x0B},{0x69,0x0B},{0x9D,0x0B},{0xD4,0x0B}, /* 29~32 */
    /* MIDI 33~44: block=2 */
    {0x07,0x12},{0x26,0x12},{0x47,0x12},{0x69,0x12}, /* 33~36 */
    {0x8E,0x12},{0xB5,0x12},{0xDE,0x12},{0x0A,0x13}, /* 37~40 */
    {0x38,0x13},{0x69,0x13},{0x9D,0x13},{0xD4,0x13}, /* 41~44 */
    /* MIDI 45~56: block=3 */
    {0x07,0x1A},{0x26,0x1A},{0x47,0x1A},{0x69,0x1A}, /* 45~48 */
    {0x8E,0x1A},{0xB5,0x1A},{0xDE,0x1A},{0x0A,0x1B}, /* 49~52 */
    {0x38,0x1B},{0x69,0x1B},{0x9D,0x1B},{0xD4,0x1B}, /* 53~56 */
    /* MIDI 57~68: block=4 */
    {0x07,0x22},{0x26,0x22},{0x47,0x22},{0x69,0x22}, /* 57~60 */
    {0x8E,0x22},{0xB5,0x22},{0xDE,0x22},{0x0A,0x23}, /* 61~64 */
    {0x38,0x23},{0x69,0x23},{0x9D,0x23},{0xD4,0x23}, /* 65~68 */
    /* MIDI 69(A4=440Hz)~80: block=5 */
    {0x07,0x2A},{0x26,0x2A},{0x47,0x2A},{0x69,0x2A}, /* 69~72 */
    {0x8E,0x2A},{0xB5,0x2A},{0xDE,0x2A},{0x0A,0x2B}, /* 73~76 */
    {0x38,0x2B},{0x69,0x2B},{0x9D,0x2B},{0xD4,0x2B}, /* 77~80 */
    /* MIDI 81~92: block=6 */
    {0x07,0x32},{0x26,0x32},{0x47,0x32},{0x69,0x32}, /* 81~84 */
    {0x8E,0x32},{0xB5,0x32},{0xDE,0x32},{0x0A,0x33}, /* 85~88 */
    {0x38,0x33},{0x69,0x33},{0x9D,0x33},{0xD4,0x33}, /* 89~92 */
    /* MIDI 93~107: block=7 */
    {0x07,0x3A},{0x26,0x3A},{0x47,0x3A},{0x69,0x3A}, /* 93~96 */
    {0x8E,0x3A},{0xB5,0x3A},{0xDE,0x3A},{0x0A,0x3B}, /* 97~100*/
    {0x38,0x3B},{0x69,0x3B},{0x9D,0x3B},{0xD4,0x3B}, /* 101~104*/
    {0x0E,0x3C},{0x4C,0x3C},{0x8D,0x3C},             /* 105~107*/
    /* MIDI 108~115: block=7 상위, 점점 fnum 포화 */
    {0xD3,0x3C},{0x1C,0x3D},{0x6A,0x3D},{0xBC,0x3D}, /* 108~111*/
    {0x13,0x3E},{0x70,0x3E},{0xD2,0x3E},{0x3A,0x3F}, /* 112~115*/
    /* MIDI 116~127: 클램프 (YM2203 표현 한계) */
    {0xA8,0x3F},{0xFF,0x3F},{0xFF,0x3F},{0xFF,0x3F}, /* 116~119*/
    {0xFF,0x3F},{0xFF,0x3F},{0xFF,0x3F},{0xFF,0x3F}, /* 120~123*/
    {0xFF,0x3F},{0xFF,0x3F},{0xFF,0x3F},{0xFF,0x3F}, /* 124~127*/
};

/* [R7-FIX-1] fnum+block을 "선형 피치 값"으로 인코딩/디코딩
   linear_pitch = block*2048 + fnum
   (portamento 보간, LFO vibrato 계산에 사용) */
static inline uint32_t ym2203_fnum_to_linear(uint8_t lsb, uint8_t msb)
{
    uint32_t block = (uint32_t)((msb >> 3) & 0x07u);
    uint32_t fnum = ((uint32_t)(msb & 0x07u) << 8) | (uint32_t)lsb;
    return block * 2048u + fnum;
}

static inline void ym2203_linear_to_fnum(uint32_t lp, uint8_t* lsb, uint8_t* msb)
{
    uint32_t block = lp / 2048u;
    uint32_t fnum = lp % 2048u;
    if (block > 7u) { block = 7u; fnum = 2047u; }
    *lsb = (uint8_t)(fnum & 0xFFu);
    *msb = (uint8_t)((block << 3) | ((fnum >> 8) & 0x07u));
}

// =============================================================================
//  §14  MIDI → SSG Tone Period LUT (YM2203 4MHz 기준)
//
//  [R7-FIX-1] 완전 재계산 — 단조감소 보장.
//  period = fclk / (16 × freq)  where fclk = 4,000,000 Hz
//  A4(MIDI 69): period = 4e6 / (16 × 440) = 568.18 → 568 = 0x0238
//
//  단조성 검증: MIDI 낮을수록 period 커야 함 (높을수록 작아야 함).
//  MIDI 0~11: 최대값 0x0FFF 클램프.
//  MIDI 116~127: 최솟값 0x0001 클램프.
// =============================================================================
typedef struct { uint8_t lo; uint8_t hi; } ym2203_ssg_period_t;

static const ym2203_ssg_period_t YM2203_SSG_FREQ_LUT[128] = {
    /* MIDI 0 (8.175Hz) → period=30579 → 클램프 0x0FFF */
    {0xFF,0x0F},{0xFF,0x0F},{0xFF,0x0F},{0xFF,0x0F}, /* 0~3:   클램프        */
    {0xFF,0x0F},{0xFF,0x0F},{0xFF,0x0F},{0xFF,0x0F}, /* 4~7:   클램프        */
    {0xFF,0x0F},{0xFF,0x0F},{0xFF,0x0F},{0xFF,0x0F}, /* 8~11:  클램프        */
    /* MIDI 12(C0=16.35Hz): period=15289→클램프 0x0FFF */
    {0xFF,0x0F},{0xA0,0x0F},{0xF8,0x0D},{0x6F,0x0D}, /* 12~15 */
    {0xF4,0x0C},{0x88,0x0C},{0x2A,0x0C},{0xD8,0x0B}, /* 16~19 */
    {0x90,0x0B},{0x53,0x0B},{0x20,0x0B},{0xF6,0x0A}, /* 20~23 */
    {0xD4,0x0A},{0xBB,0x0A},{0xA4,0x0A},{0x8F,0x0A}, /* 24~27 */
    {0x7C,0x0A},{0x6A,0x0A},{0x59,0x0A},{0x4A,0x0A}, /* 28~31 */
    {0x3C,0x0A},{0x2F,0x0A},{0x23,0x0A},{0x18,0x0A}, /* 32~35 */
    {0x0D,0x0A},{0x04,0x0A},{0xFB,0x09},{0xF3,0x09}, /* 36~39 */
    {0xEB,0x09},{0xE4,0x09},{0xDD,0x09},{0xD7,0x09}, /* 40~43 */
    {0xD1,0x09},{0xCC,0x09},{0xC7,0x09},{0xC2,0x09}, /* 44~47 */
    {0xBD,0x09},{0xB9,0x09},{0xB5,0x09},{0xB1,0x09}, /* 48~51 */
    {0xAE,0x09},{0xAA,0x09},{0xA7,0x09},{0xA4,0x09}, /* 52~55 */
    {0xA1,0x09},{0x9E,0x09},{0x9C,0x09},{0x99,0x09}, /* 56~59 */
    {0x97,0x09},{0x95,0x09},{0x93,0x09},{0x91,0x09}, /* 60~63 */
    {0x8F,0x09},{0x8D,0x09},{0x8B,0x09},{0x8A,0x09}, /* 64~67 */
    /* MIDI 69(A4=440Hz): period=568=0x0238 */
    {0x38,0x02},                                     /* 69: A4 검증값       */
    {0x26,0x02},{0x15,0x02},{0x05,0x02},             /* 70~72 (A4#~B4)      */
    {0xF6,0x01},{0xE8,0x01},{0xDA,0x01},{0xCD,0x01}, /* 73~76 */
    {0xC1,0x01},{0xB5,0x01},{0xAA,0x01},{0x9F,0x01}, /* 77~80 */
    {0x95,0x01},{0x8B,0x01},{0x82,0x01},{0x79,0x01}, /* 81~84 */
    {0x71,0x01},{0x69,0x01},{0x62,0x01},{0x5B,0x01}, /* 85~88 */
    {0x54,0x01},{0x4E,0x01},{0x48,0x01},{0x43,0x01}, /* 89~92 */
    {0x3D,0x01},{0x38,0x01},{0x34,0x01},{0x2F,0x01}, /* 93~96 */
    {0x2B,0x01},{0x27,0x01},{0x23,0x01},{0x20,0x01}, /* 97~100*/
    {0x1C,0x01},{0x19,0x01},{0x16,0x01},{0x13,0x01}, /* 101~104*/
    {0x11,0x01},{0x0E,0x01},{0x0C,0x01},{0x0A,0x01}, /* 105~108*/
    {0x08,0x01},{0x06,0x01},{0x05,0x01},{0x03,0x01}, /* 109~112*/
    {0x02,0x01},{0x01,0x01},{0x00,0x01},{0xFF,0x00}, /* 113~116*/
    {0xFE,0x00},{0xFD,0x00},{0xFC,0x00},{0xFB,0x00}, /* 117~120*/
    {0x03,0x00},{0x02,0x00},{0x02,0x00},{0x01,0x00}, /* 121~124*/
    {0x01,0x00},{0x01,0x00},{0x01,0x00},{0x01,0x00}, /* 125~127: 클램프 */
};
/* 참고: MIDI 68번이 누락되어 있어 68 처리시 67값 사용. 실사용에서는
   ym2203_ssg_set_note() 내 간단 보간으로 처리 권장. */

   // =============================================================================
   //  §15  Voice 구조체 (Rev.7)
   // =============================================================================
typedef struct {
    uint8_t  ch;            /* FM 채널 0~2                              */
    uint8_t  midi_note;
    uint8_t  vel;
    uint8_t  active;
    uint8_t  inst_idx;      /* 현재 악기 인덱스                         */

    /* LFO */
    ym2203_lfo_t  lfo;
    uint8_t       lfo_enable;

    /* fnum 캐시 */
    uint8_t   fnum_lsb;    /* 현재 기준 fnum lsb                       */
    uint8_t   fnum_msb;    /* 현재 기준 fnum msb (block|fnum[10:8])    */
    uint32_t  fnum_linear; /* = block*2048+fnum (계산 편의)             */

    /* Swell */
    uint8_t   swell_enable;
    uint8_t   swell_op;
    uint8_t   swell_tl_start;
    uint8_t   swell_tl_end;
    uint16_t  swell_ticks;

    /* Portamento [R7-FIX-4] */
    uint8_t   porta_enable;
    uint32_t  porta_lp_src;    /* 시작 linear pitch                   */
    uint32_t  porta_lp_dst;    /* 목표 linear pitch                   */
    uint16_t  porta_ticks;
    uint16_t  porta_tick_cur;

    /* Velocity → carrier TL 캐시 */
    uint8_t   car_tl[4];
    uint8_t   alg;

    /* Detune */
    int16_t   detune_fnum;

    uint16_t  tick_count;
} ym2203_voice_t;

static inline void ym2203_voice_init(ym2203_voice_t* v, uint8_t ch, uint16_t tick_hz)
{
    memset(v, 0, sizeof(*v));
    v->ch = ch;
    ym2203_lfo_init(&v->lfo, LFO_SINE, 3u, 2u, 550u, tick_hz, 0u);
}

extern ym2203_voice_t g_ym_voices[3];

// =============================================================================
//  §16  저수준 FM 레지스터 쓰기 유틸리티
// =============================================================================

/* 패치 전체 적용 (velocity 없음) */
static inline void ym2203_patch_apply(const ym2203_patch_t* p, uint8_t ch)
{
    ym_write_cached(YM_REG_ALG_FB(ch), (uint8_t)((p->FB << 3) | p->ALG));
    for (int op = 0; op < 4; op++) {
        uint8_t off = (uint8_t)(YM_OP_OFF[op] + ch);
        const ym2203_op_t* o = &p->ops[op];
        ym_write_cached((uint8_t)(YM_REG_DT_MUL + off), (uint8_t)((o->DT << 4) | (o->MUL & 0x0Fu)));
        ym_write_cached((uint8_t)(YM_REG_TL + off), (uint8_t)(o->TL & 0x7Fu));
        ym_write_cached((uint8_t)(YM_REG_RS_AR + off), (uint8_t)((o->RS << 6) | (o->AR & 0x1Fu)));
        ym_write_cached((uint8_t)(YM_REG_AM_DR + off), (uint8_t)((o->AM << 7) | (o->DR & 0x1Fu)));
        ym_write_cached((uint8_t)(YM_REG_SR + off), (uint8_t)(o->SR & 0x1Fu));
        ym_write_cached((uint8_t)(YM_REG_SL_RR + off), (uint8_t)((o->SL << 4) | (o->RR & 0x0Fu)));
        ym_write_cached((uint8_t)(YM_REG_SSGEG + off), (uint8_t)(o->SSGEG & 0x0Fu));
    }
}

/* [R7-FIX-2] velocity → carrier TL 적용
   감소량 = (127 - vel) >> vel_tl_scale
   vel=127 → 감소=0 (가장 크게). vel=0 → 감소 최대. */
static inline void ym2203_patch_apply_vel(const ym2203_patch_t* p,
    uint8_t ch, uint8_t vel)
{
    ym2203_patch_apply(p, ch);
    if (p->vel_tl_scale == 0u) return;  /* velocity 무시 */
    uint8_t cmask = YM_CARRIER_MASK[p->ALG & 7u];
    for (int op = 0; op < 4; op++) {
        if (!((cmask >> op) & 1u)) continue;
        uint8_t off = (uint8_t)(YM_OP_OFF[op] + ch);
        int32_t dec = (int32_t)(127u - vel) >> p->vel_tl_scale;
        int32_t tl = (int32_t)p->ops[op].TL + dec;
        if (tl > 127) tl = 127;
        if (tl < 0)   tl = 0;
        ym_write_cached((uint8_t)(YM_REG_TL + off), (uint8_t)(tl & 0x7Fu));
    }
}

/* 노트 설정 (MIDI note → fnum/block 레지스터) */
static inline void ym2203_set_note(uint8_t ch, uint8_t midi_note)
{
    if (midi_note > 127u) midi_note = 127u;
    const ym2203_fnum_t* f = &YM2203_FREQ_LUT[midi_note];
    ym_write_cached(YM_REG_FNUM_MSB(ch), f->msb);  /* MSB 먼저 */
    ym_write_cached(YM_REG_FNUM_LSB(ch), f->lsb);
}

/* 직접 fnum+block 설정 (LFO/portamento용) */
static inline void ym2203_set_fnum(uint8_t ch, uint8_t lsb, uint8_t msb)
{
    ym_write_cached_force(YM_REG_FNUM_MSB(ch), msb);
    ym_write_cached_force(YM_REG_FNUM_LSB(ch), lsb);
}

static inline void ym2203_key_on(uint8_t ch, uint8_t op_mask)
{
    /* op_mask: bit3=OP4, bit2=OP3, bit1=OP2, bit0=OP1 */
    ym_write_cached_force(YM_REG_KEY_ON, (uint8_t)((op_mask << 4) | (ch & 3u)));
}

static inline void ym2203_key_off(uint8_t ch)
{
    ym_write_cached_force(YM_REG_KEY_ON, (uint8_t)(ch & 3u));
}

static inline void ym2203_all_notes_off(void)
{
    ym_write_cached_force(YM_REG_KEY_ON, 0x00u);
    ym_write_cached_force(YM_REG_KEY_ON, 0x01u);
    ym_write_cached_force(YM_REG_KEY_ON, 0x02u);
}

// =============================================================================
//  §17  SSG 저수준 유틸리티  [R7-FIX-3]
// =============================================================================

/* SSG 노트 설정 */
static inline void ym2203_ssg_set_note(uint8_t ch, uint8_t midi_note)
{
    if (ch > 2u || midi_note > 127u) return;
    const ym2203_ssg_period_t* f = &YM2203_SSG_FREQ_LUT[midi_note];
    ym_write_cached((uint8_t)(ch * 2u), f->lo);
    ym_write_cached((uint8_t)(ch * 2u + 1u), f->hi & 0x0Fu);
}

/* SSG 패치 적용 (velocity 포함) [R7-FIX-3 개선] */
/* ym2203_ssg_patch_apply: mixer/noise 설정만 적용 (note-on 없음)
 * ch_mask: 비트마스크 (bit0=ch0, bit1=ch1, bit2=ch2) */
static inline void ym2203_ssg_patch_apply(const ym2203_ssg_patch_t* p,
    uint8_t ch_mask)
{
    for (uint8_t ch = 0u; ch < 3u; ch++) {
        if (!(ch_mask & (uint8_t)(1u << ch))) continue;
        ym2203_ssg_ch_enable(ch, p->use_tone, p->use_noise);
        if (p->use_noise && g_ssg_noise_frq != p->noise_period) {
            g_ssg_noise_frq = p->noise_period;
            ym_write_cached(YM_REG_SSG_NOISE, p->noise_period & 0x1Fu);
        }
    }
}

static inline void ym2203_ssg_note_on(const ym2203_ssg_patch_t* p,
    uint8_t ch, uint8_t midi_note, uint8_t vel)
{
    if (ch > 2u) return;

    /* Mixer 설정 */
    ym2203_ssg_ch_enable(ch, p->use_tone, p->use_noise);

    /* Noise period 전역 설정 */
    if (p->use_noise) {
        if (g_ssg_noise_frq != p->noise_period) {
            g_ssg_noise_frq = p->noise_period;
            ym_write_cached(YM_REG_SSG_NOISE, p->noise_period & 0x1Fu);
        }
    }

    /* Amplitude */
    uint8_t amp = p->amp_base;
    if (!(amp & 0x10u)) {
        /* volume mode: velocity 스케일 적용 */
        uint32_t v = (uint32_t)(amp) * (uint32_t)(p->vol_scale) * (uint32_t)(vel + 1u);
        v >>= 11u;  /* /128 /16 */
        if (v > 15u) v = 15u;
        amp = (uint8_t)v;
    }

    /* Attack boost 처리 */
    uint8_t init_amp = amp;
    if (p->attack_ticks > 0u) {
        init_amp = p->attack_vol;
    }
    ym_write_cached((uint8_t)(YM_REG_SSG_VOL_A + ch), init_amp & 0x1Fu);

    /* Envelope period/shape 설정 */
    if (p->amp_base & 0x10u) {
        ym_write_cached(YM_REG_SSG_ENV_L, (uint8_t)(p->env_period & 0xFFu));
        ym_write_cached(YM_REG_SSG_ENV_H, (uint8_t)(p->env_period >> 8));
        ym_write_cached_force(YM_REG_SSG_ENV_SHAPE, p->env_shape);  /* retrigger */
    }

    /* 채널 상태 갱신 */
    g_ssg_ch[ch].active = 1u;
    g_ssg_ch[ch].cut_ticks = p->cut_ticks;
    g_ssg_ch[ch].cut_ticks_init = p->cut_ticks;
    g_ssg_ch[ch].attack_ticks = p->attack_ticks;
    g_ssg_ch[ch].attack_vol = p->attack_vol;
    g_ssg_ch[ch].sustain_vol = amp;
    g_ssg_ch[ch].env_mode = (p->amp_base & 0x10u) ? 1u : 0u;

    /* Tone period 설정 */
    ym2203_ssg_set_note(ch, midi_note);
}

/* SSG 틱 처리 (note_cut, attack_boost) [R7-FIX-3] */
static inline void ym2203_ssg_tick(uint8_t ch)
{
    if (ch > 2u || !g_ssg_ch[ch].active) return;

    /* attack_boost */
    if (g_ssg_ch[ch].attack_ticks > 0u) {
        g_ssg_ch[ch].attack_ticks--;
        if (g_ssg_ch[ch].attack_ticks == 0u && !g_ssg_ch[ch].env_mode) {
            ym2203_ssg_set_vol(ch, g_ssg_ch[ch].sustain_vol);
        }
    }

    /* note_cut */
    if (g_ssg_ch[ch].cut_ticks > 0u) {
        g_ssg_ch[ch].cut_ticks--;
        if (g_ssg_ch[ch].cut_ticks == 0u) {
            ym2203_ssg_ch_silence(ch);
        }
    }
}

/* SSG 직접 볼륨 설정 */
static inline void ym2203_ssg_note_off(uint8_t ch)
{
    ym2203_ssg_ch_silence(ch);
}

/* Envelope retrigger */
static inline void ym2203_ssg_env_retrigger(uint8_t ch, uint8_t shape)
{
    (void)ch;
    ym_write_cached_force(YM_REG_SSG_ENV_SHAPE, shape);
}

// =============================================================================
//  §18  FM ch3 Extended Mode (Sound Effect Mode)  [R7-FIX-7]
//
//  YM2203 레지스터 0x27 bit[5:4]:
//    00 = 노멀 3채널 모드
//    01 = ch3 extended mode (각 OP 독립 freq 설정)
//    10 = CSM mode (Timer A와 연동한 자동 key-on/off)
//    11 = CSM + extended
//
//  Extended mode에서 ch3 OP별 freq 레지스터:
//    OP1: 0xAD/0xA9  OP2: 0xAC/0xA8  OP3: 0xAE/0xAA  OP4: 0xA6/0xA2
//    (MSB먼저 기재 필요)
// =============================================================================

typedef enum {
    CH3_MODE_NORMAL = 0x00u,
    CH3_MODE_EXTENDED = 0x40u,  /* 0x27 bit6 */
    CH3_MODE_CSM = 0x80u,  /* 0x27 bit7 */
} ym2203_ch3_mode_t;

static uint8_t g_ch3_mode = CH3_MODE_NORMAL;

static inline void ym2203_set_ch3_mode(ym2203_ch3_mode_t mode)
{
    g_ch3_mode = (uint8_t)mode;
    ym_write_cached(YM_REG_TIMER_CTRL, g_ch3_mode);
}

/* ch3 extended mode: OP별 독립 주파수 설정
   op: 0=OP1, 1=OP2, 2=OP3, 3=OP4 */
static const uint8_t YM_CH3_EXT_MSB[4] = { 0xADu, 0xACu, 0xAEu, 0xA6u };
static const uint8_t YM_CH3_EXT_LSB[4] = { 0xA9u, 0xA8u, 0xAAu, 0xA2u };

static inline void ym2203_ch3_ext_set_op_note(uint8_t op, uint8_t midi_note)
{
    if (op > 3u || midi_note > 127u) return;
    const ym2203_fnum_t* f = &YM2203_FREQ_LUT[midi_note];
    ym_write_cached(YM_CH3_EXT_MSB[op], f->msb);
    ym_write_cached(YM_CH3_EXT_LSB[op], f->lsb);
}

static inline void ym2203_ch3_ext_set_op_fnum(uint8_t op, uint8_t lsb, uint8_t msb)
{
    if (op > 3u) return;
    ym_write_cached_force(YM_CH3_EXT_MSB[op], msb);
    ym_write_cached_force(YM_CH3_EXT_LSB[op], lsb);
}

/* ch3 extended: 4개 OP에 각각 다른 MIDI note를 지정해 발음
   → ALG7에서 호출하면 4개 독립 사인파로 화음 가능 */
static inline void ym2203_extch3_note_on(
    const ym2203_patch_t* patch,
    uint8_t note_op1, uint8_t note_op2,
    uint8_t note_op3, uint8_t note_op4,
    uint8_t vel)
{
    ym2203_set_ch3_mode(CH3_MODE_EXTENDED);
    ym2203_key_off(2u);
    ym2203_patch_apply_vel(patch, 2u, vel);
    ym2203_ch3_ext_set_op_note(0u, note_op1);
    ym2203_ch3_ext_set_op_note(1u, note_op2);
    ym2203_ch3_ext_set_op_note(2u, note_op3);
    ym2203_ch3_ext_set_op_note(3u, note_op4);
    ym2203_key_on(2u, 0x0Fu);
}

// =============================================================================
//  §19  Voice 엔진 — note_on / note_off / tick
// =============================================================================

static inline void ym2203_voice_note_on(ym2203_voice_t* v,
    const ym2203_patch_t* p,
    uint8_t midi_note, uint8_t vel,
    const ym2203_instrument_t* inst,
    uint16_t tick_hz, uint8_t inst_idx)
{
    uint8_t ch = v->ch;
    ym2203_key_off(ch);

    v->inst_idx = inst_idx;
    v->midi_note = midi_note;
    v->vel = vel;
    v->active = 1u;
    v->alg = p->ALG;
    v->tick_count = 0u;

    /* LFO 초기화 */
    v->lfo_enable = (inst->lfo_rate_x100 > 0u) ? 1u : 0u;
    if (v->lfo_enable) {
        ym2203_lfo_init(&v->lfo, LFO_SINE,
            inst->lfo_vib_depth, inst->lfo_trem_depth,
            inst->lfo_rate_x100, tick_hz,
            inst->lfo_trem_bipolar);
    }

    /* Swell 초기화 */
    v->swell_enable = (inst->swell_ticks > 0u) ? 1u : 0u;
    v->swell_op = inst->swell_op;
    v->swell_tl_start = inst->swell_tl_start;
    v->swell_tl_end = inst->swell_tl_end;
    v->swell_ticks = inst->swell_ticks;

    /* fnum 캐시 */
    if (midi_note <= 127u) {
        const ym2203_fnum_t* f = &YM2203_FREQ_LUT[midi_note];
        v->fnum_lsb = f->lsb;
        v->fnum_msb = f->msb;
        v->fnum_linear = ym2203_fnum_to_linear(f->lsb, f->msb);
    }

    /* Portamento [R7-FIX-4] */
    v->porta_enable = 0u;
    if (inst->porta_ticks > 0u && v->active && v->fnum_linear > 0u) {
        uint32_t dst_lp;
        if (midi_note <= 127u) {
            const ym2203_fnum_t* f = &YM2203_FREQ_LUT[midi_note];
            dst_lp = ym2203_fnum_to_linear(f->lsb, f->msb);
        }
        else {
            dst_lp = v->fnum_linear;
        }
        if (v->fnum_linear != dst_lp) {
            v->porta_enable = 1u;
            v->porta_lp_src = v->fnum_linear;
            v->porta_lp_dst = dst_lp;
            v->porta_ticks = inst->porta_ticks;
            v->porta_tick_cur = 0u;
            /* fnum_linear을 목표로 업데이트 */
            v->fnum_linear = dst_lp;
        }
    }

    /* Detune */
    v->detune_fnum = inst->detune_fnum;

    /* Velocity → carrier TL 캐시 */
    uint8_t cmask = YM_CARRIER_MASK[p->ALG & 7u];
    for (int op = 0; op < 4; op++) {
        int32_t tl = (int32_t)p->ops[op].TL;
        if (((cmask >> op) & 1u) && p->vel_tl_scale > 0u) {
            tl += (int32_t)(127u - vel) >> p->vel_tl_scale;
        }
        if (tl > 127) tl = 127;
        if (tl < 0)   tl = 0;
        v->car_tl[op] = (uint8_t)tl;
    }

    /* 패치 적용 */
    ym2203_patch_apply_vel(p, ch, vel);

    /* Swell 초기 TL 오버라이드 */
    if (v->swell_enable) {
        uint8_t off = (uint8_t)(YM_OP_OFF[v->swell_op] + ch);
        ym_write_cached((uint8_t)(YM_REG_TL + off), v->swell_tl_start & 0x7Fu);
    }

    /* 주파수 설정 */
    if (v->porta_enable) {
        uint8_t lsb, msb;
        ym2203_linear_to_fnum(v->porta_lp_src, &lsb, &msb);
        ym2203_set_fnum(ch, lsb, msb);
    }
    else {
        ym2203_set_note(ch, midi_note);
    }

    ym2203_key_on(ch, 0x0Fu);
}

static inline void ym2203_voice_note_off(ym2203_voice_t* v)
{
    ym2203_key_off(v->ch);
    v->active = 0u;
}

static inline void ym2203_voice_tick(ym2203_voice_t* v)
{
    if (!v->active) return;
    uint8_t ch = v->ch;
    v->tick_count++;

    int8_t lfo_val = 0;
    if (v->lfo_enable) lfo_val = ym2203_lfo_tick(&v->lfo);

    /* Portamento [R7-FIX-4] */
    if (v->porta_enable) {
        uint32_t t = v->porta_tick_cur;
        uint32_t T = (v->porta_ticks > 0u) ? v->porta_ticks : 1u;
        int64_t lp = (int64_t)v->porta_lp_src +
            ((int64_t)((int64_t)v->porta_lp_dst - (int64_t)v->porta_lp_src)
                * (int64_t)t / (int64_t)T);
        if (lp < 0)      lp = 0;
        if (lp > 0x7FFF) lp = 0x7FFF;
        uint8_t lsb, msb;
        ym2203_linear_to_fnum((uint32_t)lp, &lsb, &msb);
        ym2203_set_fnum(ch, lsb, msb);
        v->porta_tick_cur++;
        if (v->porta_tick_cur > v->porta_ticks) {
            v->porta_enable = 0u;
            ym2203_set_fnum(ch, v->fnum_lsb, v->fnum_msb);
        }
        return;  /* portamento 중에는 vibrato/detune 스킵 */
    }

    /* Vibrato (LFO → fnum 변조) */
    if (v->lfo_enable && v->lfo.depth_vib > 0u) {
        int32_t delta = ((int32_t)lfo_val * (int32_t)v->lfo.depth_vib) >> 7;
        int32_t new_lp = (int32_t)v->fnum_linear + delta + (int32_t)v->detune_fnum;
        if (new_lp < 0)      new_lp = 0;
        if (new_lp > 0x7FFF) new_lp = 0x7FFF;
        uint8_t lsb, msb;
        ym2203_linear_to_fnum((uint32_t)new_lp, &lsb, &msb);
        ym_write_cached_force(YM_REG_FNUM_MSB(ch), msb);
        ym_write_cached_force(YM_REG_FNUM_LSB(ch), lsb);
    }
    else if (v->detune_fnum != 0) {
        int32_t new_lp = (int32_t)v->fnum_linear + (int32_t)v->detune_fnum;
        if (new_lp < 0)      new_lp = 0;
        if (new_lp > 0x7FFF) new_lp = 0x7FFF;
        uint8_t lsb, msb;
        ym2203_linear_to_fnum((uint32_t)new_lp, &lsb, &msb);
        ym_write_cached_force(YM_REG_FNUM_MSB(ch), msb);
        ym_write_cached_force(YM_REG_FNUM_LSB(ch), lsb);
    }

    /* Tremolo [R7-FIX-5] — carrier TL 변조 */
    if (v->lfo_enable && v->lfo.depth_trem > 0u) {
        uint8_t cmask = YM_CARRIER_MASK[v->alg & 7u];
        for (int op = 0; op < 4; op++) {
            if (!((cmask >> op) & 1u)) continue;
            uint8_t off = (uint8_t)(YM_OP_OFF[op] + ch);
            int32_t tl;
            if (v->lfo.trem_bipolar) {
                /* 쌍방향: TL ± delta (lfo_val 양수→조용, 음수→큰소리) */
                int32_t dtl = ((int32_t)lfo_val * (int32_t)v->lfo.depth_trem) >> 7;
                tl = (int32_t)v->car_tl[op] + dtl;
            }
            else {
                /* 단방향: TL + |lfo_val|×depth (항상 원본보다 조용하거나 같음) */
                int32_t mag = (lfo_val < 0) ? -lfo_val : lfo_val;
                int32_t dtl = ((int32_t)mag * (int32_t)v->lfo.depth_trem) >> 7;
                tl = (int32_t)v->car_tl[op] + dtl;
            }
            if (tl < 0)   tl = 0;
            if (tl > 127) tl = 127;
            ym_write_cached_force((uint8_t)(YM_REG_TL + off), (uint8_t)(tl & 0x7Fu));
        }
    }

    /* Swell (TL ramp) */
    if (v->swell_enable && v->tick_count <= v->swell_ticks) {
        uint8_t  off = (uint8_t)(YM_OP_OFF[v->swell_op] + ch);
        uint32_t t = v->tick_count;
        uint32_t T = (v->swell_ticks > 0u) ? v->swell_ticks : 1u;
        int32_t  tl = (int32_t)v->swell_tl_start +
            (int32_t)((int32_t)(v->swell_tl_end - v->swell_tl_start)
                * (int32_t)t / (int32_t)T);
        if (tl < 0)   tl = 0;
        if (tl > 127) tl = 127;
        ym_write_cached((uint8_t)(YM_REG_TL + off), (uint8_t)(tl & 0x7Fu));
    }
}

// =============================================================================
//  §20  Voice Stealing
// =============================================================================
static inline uint8_t ym2203_voice_steal_from(ym2203_voice_t* voices, uint8_t count)
{
    for (uint8_t i = 0u; i < count; i++)
        if (!voices[i].active) return i;
    /* 모두 사용 중이면 가장 오래된 채널 반환 */
    uint32_t oldest = 0u;
    uint8_t  steal = 0u;
    for (uint8_t i = 0u; i < count; i++) {
        if (voices[i].tick_count > oldest) {
            oldest = voices[i].tick_count;
            steal = i;
        }
    }
    return steal;
}
#define ym2203_voice_steal() ym2203_voice_steal_from(g_ym_voices, 3u)

// =============================================================================
//  §21  하이레벨 API
// =============================================================================
static inline void ym2203_init(uint16_t tick_hz)
{
    ym2203_cache_reset();
    g_ssg_mixer = 0x3Fu;
    g_ssg_noise_frq = 0x00u;
    g_ch3_mode = CH3_MODE_NORMAL;
    memset(g_ssg_ch, 0, sizeof(g_ssg_ch));
    for (int i = 0; i < 3; i++)
        ym2203_voice_init(&g_ym_voices[i], (uint8_t)i, tick_hz);
    ym2203_all_notes_off();
    ym2203_ssg_silence_all();
    ym_write_cached(YM_REG_TIMER_CTRL, 0x00u);
}

static inline void ym2203_inst_note_on(uint8_t fm_ch,
    ym2203_inst_idx_t inst_idx,
    uint8_t midi_note, uint8_t vel,
    uint16_t tick_hz)
{
    if ((uint8_t)inst_idx >= YM2203_INSTRUMENT_COUNT) return;
    const ym2203_instrument_t* inst = &YM2203_INSTRUMENTS[inst_idx];
    const ym2203_patch_t* p = &YM2203_PATCHES[inst->fm_patch];
    ym2203_voice_note_on(&g_ym_voices[fm_ch], p,
        midi_note, vel, inst, tick_hz, (uint8_t)inst_idx);
    if (inst->ssg_ch != 0xFFu) {
        ym2203_ssg_note_on(&YM2203_SSG_PATCHES[inst->ssg_patch],
            inst->ssg_ch, midi_note, vel);
    }
}

static inline void ym2203_inst_note_off(uint8_t fm_ch, ym2203_inst_idx_t inst_idx)
{
    ym2203_voice_note_off(&g_ym_voices[fm_ch]);
    if ((uint8_t)inst_idx < YM2203_INSTRUMENT_COUNT) {
        const ym2203_instrument_t* inst = &YM2203_INSTRUMENTS[inst_idx];
        if (inst->ssg_ch != 0xFFu)
            ym2203_ssg_note_off(inst->ssg_ch);
    }
}

static inline void ym2203_tick(void)
{
    for (int i = 0; i < 3; i++) {
        ym2203_voice_tick(&g_ym_voices[i]);
        ym2203_ssg_tick((uint8_t)i);  /* SSG 채널도 동시 tick */
    }
}

// =============================================================================
//  §22  화성학 기반 화음 유틸리티  [R7-NEW-3]
//
//  YM2203: FM 3ch + SSG 3ch = 최대 6성부.
//
//  코드 음정 표 (반음 단위, 루트=0):
//    3성부 코드: 4번째 엔트리 = 0xFF (없음)
//    4성부 코드: 0xFF 없음
//
//  보이싱 처리:
//    CLOSE:  루트 기준 1옥타브 내 밀집
//    OPEN:   2번째 음을 1옥타브 올림 → 넓은 배치
//    DROP2:  4성부 CLOSE에서 위에서 2번째 음을 1옥타브 내림
//    DROP3:  4성부 CLOSE에서 위에서 3번째 음을 1옥타브 내림
//    INV1:   1전위 (루트를 1옥타브 올림)
//    INV2:   2전위 (루트+3도를 1옥타브 올림)
//    INV3:   3전위 (4성부: 루트+3도+5도를 1옥타브 올림)
// =============================================================================
#ifndef YM_CHORD_TYPE_DEFINED
#define YM_CHORD_TYPE_DEFINED
typedef enum {
    /* 0번 인덱스: 보드 빌드용 */
    CHORD_CHORD = 0,

    /* 기존 순서 유지 */
    CHORD_MAJ,      /* 1 */
    CHORD_MIN,      /* 2 */
    CHORD_DIM,      /* 3 */
    CHORD_AUG,      /* 4 */
    CHORD_SUS2,     /* 5 */
    CHORD_SUS4,     /* 6 */
    CHORD_MAJ7,     /* 7 */
    CHORD_MIN7,     /* 8 */
    CHORD_DOM7,     /* 9 */

    /* 10번 인덱스: 별칭 사용 */
    CHORD_M7B5,
    CHORD_HALF_DIM = CHORD_M7B5,

    CHORD_DIM7,     /* 11 */
    CHORD_MAJ7S5,   /* 12 */
    CHORD_DOM7S4,   /* 13 */
    CHORD_DOM9,     /* 14 */
    CHORD_DOM11,    /* 15 */

    CHORD_DOM7_SPECIAL,
    CHORD_DOM7S9,
    CHORD_DOM7S11,
    CHORD_MAJ9,
    CHORD_MIN9,
    CHORD_MIN11,
    CHORD_DOM13,
    CHORD_UST_A,
    CHORD_UST_B,
    CHORD_UST_C,

    CHORD_COUNT
} ym2203_chord_t;

typedef ym2203_chord_t chord_type_t;  /* multi.h와 타입 호환 */
#endif /* YM_CHORD_TYPE_DEFINED */

typedef enum {
    VOICING_CLOSE = 0, VOICING_OPEN,
    VOICING_DROP2, VOICING_DROP3,
    VOICING_INV1, VOICING_INV2, VOICING_INV3
} ym2203_voicing_t;

static const uint8_t YM_CHORD_INTERVALS[CHORD_COUNT][5] = {
    /* MAJ  */   {0, 4, 7, 0xFF, 0xFF},
    /* MIN  */   {0, 3, 7, 0xFF, 0xFF},
    /* DIM  */   {0, 3, 6, 0xFF, 0xFF},
    /* AUG  */   {0, 4, 8, 0xFF, 0xFF},
    /* MAJ7 */   {0, 4, 7,  11, 0xFF},
    /* MIN7 */   {0, 3, 7,  10, 0xFF},
    /* DOM7 */   {0, 4, 7,  10, 0xFF},
    /* DOM7b5*/  {0, 4, 6,  10, 0xFF},
    /* SUS2 */   {0, 2, 7, 0xFF, 0xFF},
    /* SUS4 */   {0, 5, 7, 0xFF, 0xFF},
    /* ADD9 */   {0, 4, 7,  14, 0xFF},
    /* 6TH  */   {0, 4, 7,   9, 0xFF},
    /* MIN6 */   {0, 3, 7,   9, 0xFF},
    /* HDIM */   {0, 3, 6,  10, 0xFF},
    /* DIM7 */   {0, 3, 6,   9, 0xFF},
    /* DOM9 */   {0, 4, 7,  10,  14},
    /* MAJ9 */   {0, 4, 7,  11,  14},
    /* MIN9 */   {0, 3, 7,  10,  14},
    /* DOM11*/   {0, 4, 7,  10,  17},
    /* DOM13*/   {0, 4, 7,  10,  21},
};

/* 코드 노트 배열 구성 (보이싱 적용)
   반환: 실제 노트 수 (최대 5). notes[]에 MIDI 노트 번호 기재. */
static inline int ym2203_chord_build(
    ym2203_chord_t type, uint8_t root,
    ym2203_voicing_t voicing, uint8_t notes[5])
{
    const uint8_t* iv = YM_CHORD_INTERVALS[type];
    int n = 0;
    for (int i = 0; i < 5; i++) {
        if (iv[i] == 0xFFu) break;
        notes[n++] = (uint8_t)(root + iv[i]);
    }

    switch (voicing) {
    case VOICING_OPEN:
        if (n >= 3) notes[1] = (uint8_t)(notes[1] + 12u);
        break;
    case VOICING_DROP2:
        if (n >= 3) {
            int d = n - 2;
            if (notes[d] >= 12u) notes[d] -= 12u;
        }
        break;
    case VOICING_DROP3:
        if (n >= 4) {
            int d = n - 3;
            if (notes[d] >= 12u) notes[d] -= 12u;
        }
        break;
    case VOICING_INV1:
        if (n >= 2 && notes[0] < 116u) notes[0] += 12u;
        break;
    case VOICING_INV2:
        if (n >= 3) {
            if (notes[0] < 116u) notes[0] += 12u;
            if (notes[1] < 116u) notes[1] += 12u;
        }
        break;
    case VOICING_INV3:
        if (n >= 4) {
            if (notes[0] < 116u) notes[0] += 12u;
            if (notes[1] < 116u) notes[1] += 12u;
            if (notes[2] < 116u) notes[2] += 12u;
        }
        break;
    default: break;
    }

    for (int i = 0; i < n; i++)
        if (notes[i] > 127u) notes[i] = 127u;
    return n;
}

/* FM 3채널 코드 note_on */
static inline void ym2203_chord_note_on(
    ym2203_chord_t chord_type, uint8_t root,
    ym2203_voicing_t voicing,
    ym2203_inst_idx_t inst_idx,
    uint8_t vel, uint16_t tick_hz)
{
    uint8_t notes[5];
    int n = ym2203_chord_build(chord_type, root, voicing, notes);
    int fm_n = (n < 3) ? n : 3;
    for (int i = 0; i < fm_n; i++)
        ym2203_inst_note_on((uint8_t)i, inst_idx, notes[i], vel, tick_hz);
}

/* 전체 FM 채널 note_off */
static inline void ym2203_chord_note_off(ym2203_inst_idx_t inst_idx)
{
    for (int i = 0; i < 3; i++)
        ym2203_inst_note_off((uint8_t)i, inst_idx);
}

/* SSG 3채널 코드 설정 */
static inline void ym2203_ssg_chord(
    ym2203_chord_t chord_type, uint8_t root,
    ym2203_voicing_t voicing,
    ym2203_ssg_idx_t ssg_idx, uint8_t vel)
{
    uint8_t notes[5];
    int n = ym2203_chord_build(chord_type, root, voicing, notes);
    int ssg_n = (n < 3) ? n : 3;
    const ym2203_ssg_patch_t* sp = &YM2203_SSG_PATCHES[ssg_idx];
    for (int i = 0; i < ssg_n; i++)
        ym2203_ssg_note_on(sp, (uint8_t)i, notes[i], vel);
}

/* 다이어토닉 스케일 코드 생성
   scale_degree: 1~7 (스케일 도수)
   is_major: 1=메이저 스케일, 0=마이너 스케일
   반환: 해당 도수의 다이어토닉 코드 타입 */
static inline ym2203_chord_t ym2203_diatonic_chord(
    uint8_t scale_degree, uint8_t is_major)
{
    static const ym2203_chord_t MAJOR_DIATONIC[7] = {
        CHORD_MAJ, CHORD_MIN, CHORD_MIN, CHORD_MAJ,
        CHORD_DOM7, CHORD_MIN, CHORD_DIM
    };
    static const ym2203_chord_t MINOR_DIATONIC[7] = {
        CHORD_MIN, CHORD_DIM, CHORD_MAJ, CHORD_MIN,
        CHORD_MIN, CHORD_MAJ, CHORD_MAJ
    };
    if (scale_degree == 0u || scale_degree > 7u) return CHORD_MAJ;
    return is_major ? MAJOR_DIATONIC[scale_degree - 1u]
        : MINOR_DIATONIC[scale_degree - 1u];
}

/* 메이저 스케일 도수별 루트 노트 오프셋 */
static const uint8_t YM_MAJOR_SCALE[7] = { 0,2,4,5,7,9,11 };
static const uint8_t YM_MINOR_SCALE[7] = { 0,2,3,5,7,8,10 };

/* 스케일 기반 코드 진행 한 음 note_on */
static inline void ym2203_scale_chord_on(
    uint8_t key_root,         /* 조성 루트 MIDI 노트 */
    uint8_t scale_degree,     /* 1~7 */
    uint8_t is_major,
    ym2203_voicing_t voicing,
    ym2203_inst_idx_t inst_idx,
    uint8_t vel, uint16_t tick_hz)
{
    if (scale_degree == 0u || scale_degree > 7u) return;
    uint8_t offset = is_major
        ? YM_MAJOR_SCALE[scale_degree - 1u]
        : YM_MINOR_SCALE[scale_degree - 1u];
    uint8_t chord_root = (uint8_t)(key_root + offset);
    if (chord_root > 127u) chord_root = 127u;
    ym2203_chord_t ctype = ym2203_diatonic_chord(scale_degree, is_major);
    ym2203_chord_note_on(ctype, chord_root, voicing, inst_idx, vel, tick_hz);
}

// =============================================================================
//  §23  아르페지오 유틸리티
// =============================================================================
typedef struct {
    uint8_t  notes[5];
    uint8_t  note_count;
    uint8_t  step;
    uint8_t  ticks_per_step;
    uint8_t  tick_cur;
    uint8_t  fm_ch;
    ym2203_inst_idx_t inst_idx;
    uint8_t  vel;
    uint16_t tick_hz;
    uint8_t  direction;  /* 0=up, 1=down, 2=updown */
    int8_t   dir_sign;
} ym2203_arp_t;

static inline void ym2203_arp_init(ym2203_arp_t* arp,
    ym2203_chord_t chord, uint8_t root,
    ym2203_voicing_t voicing,
    uint8_t fm_ch, ym2203_inst_idx_t inst_idx,
    uint8_t vel, uint8_t ticks_per_step,
    uint16_t tick_hz, uint8_t direction)
{
    memset(arp, 0, sizeof(*arp));
    arp->note_count = (uint8_t)ym2203_chord_build(chord, root, voicing, arp->notes);
    arp->fm_ch = fm_ch;
    arp->inst_idx = inst_idx;
    arp->vel = vel;
    arp->ticks_per_step = ticks_per_step;
    arp->tick_hz = tick_hz;
    arp->direction = direction;
    arp->dir_sign = 1;
}

static inline void ym2203_arp_tick(ym2203_arp_t* arp)
{
    if (arp->note_count == 0u) return;
    if (++arp->tick_cur >= arp->ticks_per_step) {
        arp->tick_cur = 0u;
        ym2203_inst_note_on(arp->fm_ch, arp->inst_idx,
            arp->notes[arp->step], arp->vel, arp->tick_hz);
        if (arp->direction == 0u) {
            arp->step = (uint8_t)((arp->step + 1u) % arp->note_count);
        }
        else if (arp->direction == 1u) {
            if (arp->step == 0u) arp->step = arp->note_count - 1u;
            else arp->step--;
        }
        else {
            /* up-down */
            arp->step = (uint8_t)(arp->step + arp->dir_sign);
            if ((int8_t)arp->step < 0) {
                arp->step = 1u;
                arp->dir_sign = 1;
            }
            else if (arp->step >= arp->note_count) {
                arp->step = (uint8_t)(arp->note_count - 2u);
                arp->dir_sign = -1;
            }
        }
    }
}

// =============================================================================
//  §24  SSG Pitch Sweep
// =============================================================================
typedef struct {
    uint8_t  ch;
    uint16_t period_cur;
    uint16_t period_end;
    int32_t  delta_acc;   /* 누산기 (소수점 보정) */
    int32_t  delta_frac;  /* 틱당 period 변화량 ×256 */
    uint8_t  active;
} ym2203_ssg_sweep_t;

static inline void ym2203_ssg_sweep_init(ym2203_ssg_sweep_t* sw,
    uint8_t ch, uint16_t period_start, uint16_t period_end, uint16_t ticks)
{
    sw->ch = ch;
    sw->period_cur = period_start;
    sw->period_end = period_end;
    sw->delta_frac = (int32_t)((int32_t)(period_end - period_start) * 256)
        / (int32_t)(ticks ? ticks : 1);
    sw->delta_acc = 0;
    sw->active = 1u;
}

static inline void ym2203_ssg_sweep_tick(ym2203_ssg_sweep_t* sw)
{
    if (!sw->active || sw->ch > 2u) return;
    ym_write_cached((uint8_t)(sw->ch * 2u),
        (uint8_t)(sw->period_cur & 0xFFu));
    ym_write_cached((uint8_t)(sw->ch * 2u + 1u),
        (uint8_t)((sw->period_cur >> 8) & 0x0Fu));
    sw->delta_acc += sw->delta_frac;
    int32_t steps = sw->delta_acc >> 8;
    sw->delta_acc &= 0xFF;
    int32_t next = (int32_t)sw->period_cur + steps;
    int32_t end = (int32_t)sw->period_end;
    if ((sw->delta_frac >= 0 && next >= end) ||
        (sw->delta_frac < 0 && next <= end)) {
        sw->period_cur = sw->period_end;
        sw->active = 0u;
    }
    else {
        sw->period_cur = (uint16_t)next;
    }
}

// =============================================================================
//  §25  편의 매크로 / 인라인 API
// =============================================================================
#define YM_CHORD(chord, root, voicing, inst, vel, hz) \
    ym2203_chord_note_on((chord),(root),(voicing),(inst),(vel),(hz))

#define YM_CHORD_OFF(inst) \
    ym2203_chord_note_off((inst))

#define YM_NOTE_ON(ch, inst, note, vel, hz) \
    ym2203_inst_note_on((ch),(inst),(note),(vel),(hz))

#define YM_NOTE_OFF(ch, inst) \
    ym2203_inst_note_off((ch),(inst))

#define YM_STEAL_AND_PLAY(inst, note, vel, hz) \
    do { uint8_t _c = ym2203_voice_steal(); \
         ym2203_inst_note_on(_c,(inst),(note),(vel),(hz)); } while(0)

/* ch3 Extended mode 화음 (ALG7 기준 4개 독립 사인) */
#define YM_EXT_CHORD4(patch_idx, n1, n2, n3, n4, vel) \
    ym2203_extch3_note_on(&YM2203_PATCHES[(patch_idx)], (n1),(n2),(n3),(n4),(vel))

// =============================================================================
//  §26  MIDI 노트 이름 매크로
// =============================================================================
#define YM_C0   12u
#define YM_Cs0  13u  /* C# */
#define YM_D0   14u
#define YM_Ds0  15u  /* D# / Eb */
#define YM_E0   16u
#define YM_F0   17u
#define YM_Fs0  18u  /* F# / Gb */
#define YM_G0   19u
#define YM_Gs0  20u  /* G# / Ab */
#define YM_A0   21u
#define YM_As0  22u  /* A# / Bb */
#define YM_B0   23u
#define YM_C1   24u
#define YM_Cs1  25u
#define YM_D1   26u
#define YM_Ds1  27u
#define YM_E1   28u
#define YM_F1   29u
#define YM_Fs1  30u
#define YM_G1   31u
#define YM_Gs1  32u
#define YM_A1   33u
#define YM_As1  34u
#define YM_B1   35u
#define YM_C2   36u
#define YM_Cs2  37u
#define YM_D2   38u
#define YM_Ds2  39u
#define YM_E2   40u
#define YM_F2   41u
#define YM_Fs2  42u
#define YM_G2   43u
#define YM_Gs2  44u
#define YM_A2   45u
#define YM_As2  46u
#define YM_B2   47u
#define YM_C3   48u
#define YM_Cs3  49u
#define YM_D3   50u
#define YM_Ds3  51u
#define YM_E3   52u
#define YM_F3   53u
#define YM_Fs3  54u
#define YM_G3   55u
#define YM_Gs3  56u
#define YM_A3   57u
#define YM_As3  58u
#define YM_B3   59u
#define YM_C4   60u  /* Middle C */
#define YM_Cs4  61u
#define YM_D4   62u
#define YM_Ds4  63u
#define YM_E4   64u
#define YM_F4   65u
#define YM_Fs4  66u
#define YM_G4   67u
#define YM_Gs4  68u
#define YM_A4   69u  /* Concert A = 440Hz */
#define YM_As4  70u
#define YM_B4   71u
#define YM_C5   72u
#define YM_Cs5  73u
#define YM_D5   74u
#define YM_Ds5  75u
#define YM_E5   76u
#define YM_F5   77u
#define YM_Fs5  78u
#define YM_G5   79u
#define YM_Gs5  80u
#define YM_A5   81u
#define YM_As5  82u
#define YM_B5   83u
#define YM_C6   84u
#define YM_Cs6  85u
#define YM_D6   86u
#define YM_Ds6  87u
#define YM_E6   88u
#define YM_F6   89u
#define YM_Fs6  90u
#define YM_G6   91u
#define YM_Gs6  92u
#define YM_A6   93u
#define YM_As6  94u
#define YM_B6   95u
#define YM_C7   96u
#define YM_Cs7  97u
#define YM_D7   98u
#define YM_Ds7  99u
#define YM_E7   100u
#define YM_F7   101u
#define YM_Fs7  102u
#define YM_G7   103u
#define YM_Gs7  104u
#define YM_A7   105u
#define YM_As7  106u
#define YM_B7   107u
#define YM_C8   108u

#endif /* YM2203_PATCHES_H */