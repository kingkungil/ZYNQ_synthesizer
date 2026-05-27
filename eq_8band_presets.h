/* =============================================================================
 *  eq_8band_presets.h  —  Rev.2
 *
 *  8밴드 파라메트릭 EQ 프리셋 테이블
 *
 *  대상 HW : pre_eq_4band.v × 2 직렬 (PRE + POST 8밴드 구성)
 *  구성 방식: PRE EQ hw = 밴드 0~3 / POST EQ hw = 밴드 4~7
 *  사용 코드: effect_bd_uio.c  →  apply_eq8_preset_atomic()
 *
 *  Fs = 48000 Hz   Q포맷: Q2.22   RBJ Audio EQ Cookbook
 *
 *  밴드 주파수 배치 (모든 프리셋 공통)
 *  ┌──────┬────────┬──────┬─────┬──────┬──────┬───────┬───────┐
 *  │  B0  │   B1   │  B2  │  B3 │  B4  │  B5  │  B6   │  B7   │
 *  ├──────┼────────┼──────┼─────┼──────┼──────┼───────┼───────┤
 *  │ ~32  │  ~120  │ ~300 │~800 │~2kHz │~4kHz │~10kHz │~16kHz │
 *  │ Sub  │  Bass  │LoMid │ Mid │UpMid │ Pres │  Air  │HiShelf│
 *  └──────┴────────┴──────┴─────┴──────┴──────┴───────┴───────┘
 *  B0: Low Shelf / HPF   B7: High Shelf   B1~B6: Bell / Notch / LPF
 *
 *  레지스터 매핑
 *    pre_eq  base → B0~B3  ( EQ_REG_OFF(0~3, coef) )
 *    post_eq base → B4~B7  ( EQ_REG_OFF(0~3, coef) )
 *
 *  Rev.2 변경사항 (vs Rev.1)
 *  ──────────────────────────────────────────────────────────────
 *  [악기 추가 — 관악기]
 *    · TIN_WHISTLE / TIN_WHISTLE_WARM / TIN_WHISTLE_SESSION
 *    · FLUTE / RECORDER / OCARINA
 *    · SAXOPHONE_ALTO / TENOR / SOPRANO / BARITONE
 *    · CLARINET / OBOE / BASSOON
 *    · BAGPIPE / HARMONICA
 *    · TRUMPET / TROMBONE / FRENCH_HORN / TUBA
 *  [악기 추가 — 현악기/타악기]
 *    · VIOLA / DOUBLE_BASS / HARP
 *    · NYLON_GUITAR / GUITAR_CLEAN / GUITAR_CRUNCH / GUITAR_LEAD
 *    · SITAR / SANTUR
 *  [악기 추가 — 전자/합성]
 *    · SYNTH_PAD / SYNTH_LEAD / SYNTH_BASS
 *    · ELECTRIC_PIANO / ORGAN_FULL / ORGAN_DRAWBAR
 *  [음질 개선 특화]
 *    · AIR_RESTORATION   — 고역/에어 복원
 *    · MUD_CLEANER       — 저중역 혼탁 제거
 *    · HARSHNESS_REDUCER — 2~5kHz 거슬림 제거
 *    · PROXIMITY_FIX     — 근접 효과 보정
 *    · RECORDING_ENHANCE — 녹음 일반 강화
 *    · MASTERING_GLUE    — 마스터링 글루 커브
 *    · LOUDNESS_MAXIMIZER — 라우드니스 극대화
 *    · CLARITY_BOOST     — 명료도 부스트
 *    · WARMTH_RESTORE    — 따뜻함 복원
 *    · STEREO_ENHANCE    — 스테레오 이미지 강화
 *    · ROOM_CORRECTION   — 소형 룸 보정
 *    · LOW_END_TIGHTEN   — 저역 타이트닝
 *    · VOCAL_AIR         — 보컬 에어 실크
 *    · INSTRUMENTAL_BALANCE — 악기 밸런스 정리
 *  [총 프리셋: 95개]
 * =============================================================================
 */

#ifndef EQ_8BAND_PRESETS_H
#define EQ_8BAND_PRESETS_H

#include <stdint.h>
#include "eq_4band_presets.h"  /* BiquadCoef, EQ_REG_OFF, EQ_REG_CTRL */

#ifdef __cplusplus
extern "C" {
#endif


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 1.  상수 / 포인터 헬퍼
     * ─────────────────────────────────────────────────────────────── */
#define EQ8_BANDS       8u
#define EQ8_HALF_BANDS  4u

#define EQ8_PRE_PTR(p8)   ((const BiquadCoef*)((p8) + 0))
#define EQ8_POST_PTR(p8)  ((const BiquadCoef*)((p8) + 4))


     /* ─────────────────────────────────────────────────────────────────────────────
      * § 2.  apply_eq8_preset_atomic() — PRE + POST EQ HW 동시 원자적 기록
      *        coef_bypass 프로토콜: bypass=1 → 40 shadow writes → bypass=0
      * ─────────────────────────────────────────────────────────────── */
#define apply_eq8_preset_atomic(pre_base, post_base, p8)                      \
    do {                                                                       \
        volatile uint8_t* _pre  = (volatile uint8_t*)(pre_base);             \
        volatile uint8_t* _post = (volatile uint8_t*)(post_base);            \
        const BiquadCoef* _p    = (const BiquadCoef*)(p8);                   \
        *((volatile uint32_t*)(_pre  + EQ_REG_CTRL)) = 0x00000002u;          \
        *((volatile uint32_t*)(_post + EQ_REG_CTRL)) = 0x00000002u;          \
        for (int _b = 0; _b < 4; _b++) {                                     \
            *((volatile uint32_t*)(_pre +EQ_REG_OFF(_b,0)))=_p[_b].b0  &0xFFFFFFu; \
            *((volatile uint32_t*)(_pre +EQ_REG_OFF(_b,1)))=_p[_b].b1  &0xFFFFFFu; \
            *((volatile uint32_t*)(_pre +EQ_REG_OFF(_b,2)))=_p[_b].b2  &0xFFFFFFu; \
            *((volatile uint32_t*)(_pre +EQ_REG_OFF(_b,3)))=_p[_b].a1  &0xFFFFFFu; \
            *((volatile uint32_t*)(_pre +EQ_REG_OFF(_b,4)))=_p[_b].a2  &0xFFFFFFu; \
            *((volatile uint32_t*)(_post+EQ_REG_OFF(_b,0)))=_p[4+_b].b0&0xFFFFFFu; \
            *((volatile uint32_t*)(_post+EQ_REG_OFF(_b,1)))=_p[4+_b].b1&0xFFFFFFu; \
            *((volatile uint32_t*)(_post+EQ_REG_OFF(_b,2)))=_p[4+_b].b2&0xFFFFFFu; \
            *((volatile uint32_t*)(_post+EQ_REG_OFF(_b,3)))=_p[4+_b].a1&0xFFFFFFu; \
            *((volatile uint32_t*)(_post+EQ_REG_OFF(_b,4)))=_p[4+_b].a2&0xFFFFFFu; \
        }                                                                      \
        *((volatile uint32_t*)(_pre  + EQ_REG_CTRL)) = 0x00000000u;          \
        *((volatile uint32_t*)(_post + EQ_REG_CTRL)) = 0x00000000u;          \
    } while(0)

      /* ─────────────────────────────────────────────────────────────────────────────
       * § 3.  8밴드 프리셋 데이터  (95개, Fs=48kHz, Q2.22)
       * ─────────────────────────────────────────────────────────────── */

       /* ═══════════════════════════════════════════════════════════════
        *  [0+]  § A  기존 프리셋 — 악기 / 장르 / 음색 / 환경
        * ═══════════════════════════════════════════════════════════════ */

        /* (0) 바이패스 — All bands pass-through */
    static const BiquadCoef EQ8_BYPASS[8] = {
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u}
    };

    /* (1) 대금 — Low-cut, 800Hz~4kHz overtone boost, air */
    static const BiquadCoef EQ8_DAEGEUM[8] = {
        {0x3FF586u,0x80C0A5u,0x3F4ABAu,0x80C0C3u,0x3F405Eu},
        {0x3FDB49u,0x816901u,0x3EBFB5u,0x816901u,0x3E9AFFu},
        {0x402DA8u,0x81FD4Eu,0x3DEDEDu,0x81FD4Eu,0x3E1B95u},
        {0x407FC5u,0x848905u,0x3BA550u,0x848905u,0x3C2514u},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x429856u,0xA28211u,0x295C31u,0xA28211u,0x2BF486u},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x44DA06u,0x11C556u,0x0CA027u,0x16D2ABu,0x0C6CD9u}
    };

    /* (2) 피리 — Strong 800Hz~2kHz core, low/hi cut */
    static const BiquadCoef EQ8_PIRI[8] = {
        {0x3FF046u,0x80C637u,0x3F4A5Bu,0x80C664u,0x3F3ACEu},
        {0x3FC4CEu,0x8144D0u,0x3EFA63u,0x8144D0u,0x3EBF30u},
        {0x401866u,0x81A8D9u,0x3E57B6u,0x81A8D9u,0x3E701Bu},
        {0x414071u,0x84F53Bu,0x3A77D5u,0x84F53Bu,0x3BB846u},
        {0x4306D4u,0x8BDFD3u,0x35320Bu,0x8BDFD3u,0x3838DFu},
        {0x4304FEu,0xA558EAu,0x25A83Eu,0xA558EAu,0x28AD3Cu},
        {0x3DA4FCu,0xEA1554u,0x1708FEu,0xEA1554u,0x14ADFAu},
        {0x3A0EFCu,0x15A5ADu,0x0B64F2u,0x0F7548u,0x0BA453u}
    };

    /* (3) 해금 — Bowing overtone, warm 800Hz~4kHz + air */
    static const BiquadCoef EQ8_HAEGEUM[8] = {
        {0x3FF586u,0x80C0A5u,0x3F4ABAu,0x80C0C3u,0x3F405Eu},
        {0x3FE291u,0x81223Cu,0x3EFF35u,0x81223Cu,0x3EE1C6u},
        {0x401866u,0x81A8D9u,0x3E57B6u,0x81A8D9u,0x3E701Bu},
        {0x40C082u,0x8453AEu,0x3B9A35u,0x8453AEu,0x3C5AB7u},
        {0x4205B1u,0x8B0A6Fu,0x37101Au,0x8B0A6Fu,0x3915CBu},
        {0x433F43u,0x9EC82Au,0x2D02BCu,0x9EC82Au,0x3041FFu},
        {0x445FF6u,0xE79E05u,0x19D531u,0xE79E05u,0x1E3527u},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (4) 가야금 — Attack clarity, low resonance, hi sparkle */
    static const BiquadCoef EQ8_GAYAGEUM[8] = {
        {0x40053Du,0x80B0E0u,0x3F4AF4u,0x80B0D1u,0x3F5022u},
        {0x40189Au,0x80C20Cu,0x3F295Fu,0x80C20Cu,0x3F41F9u},
        {0x402460u,0x826D01u,0x3D876Du,0x826D01u,0x3DABCCu},
        {0x40484Cu,0x83AE61u,0x3CB8A1u,0x83AE61u,0x3D00EDu},
        {0x40E74Eu,0x8B1A4Eu,0x381E0Fu,0x8B1A4Eu,0x39055Cu},
        {0x410FCAu,0xA0381Fu,0x2D8954u,0xA0381Fu,0x2E991Eu},
        {0x43B428u,0xE90AB7u,0x14FFEDu,0xE90AB7u,0x18B416u},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* (5) 거문고 — Deep low, heavy low-mid, hi rolloff */
    static const BiquadCoef EQ8_GEOMUNGO[8] = {
        {0x400FBEu,0x80A719u,0x3F4A5Bu,0x80A6EDu,0x3F59ECu},
        {0x403B69u,0x80CF2Bu,0x3EF970u,0x80CF2Bu,0x3F34D9u},
        {0x4048F1u,0x824C41u,0x3D83A2u,0x824C41u,0x3DCC93u},
        {0x403FAAu,0x84C154u,0x3BACCCu,0x84C154u,0x3BEC77u},
        {0x3F2935u,0x934434u,0x316888u,0x934434u,0x3091BCu},
        {0x3D1DD6u,0xA96E7Du,0x26D814u,0xA96E7Du,0x23F5E9u},
        {0x3DA4FCu,0xEA1554u,0x1708FEu,0xEA1554u,0x14ADFAu},
        {0x3A0EFCu,0x15A5ADu,0x0B64F2u,0x0F7548u,0x0BA453u}
    };

    /* (6) 해금 솔로 — HPF + 400~4kHz core overtone boost */
    static const BiquadCoef EQ8_HAEGEUM_SOLO[8] = {
        {0x3F8905u,0x80EDF6u,0x3F8905u,0x80EF5Bu,0x3F136Eu},
        {0x3FDB4Au,0x816B3Fu,0x3EBFB7u,0x816B3Fu,0x3E9B01u},
        {0x4040F3u,0x8221E7u,0x3DC95Fu,0x8221E7u,0x3E0A51u},
        {0x40C082u,0x8453AEu,0x3B9A35u,0x8453AEu,0x3C5AB7u},
        {0x428C4Cu,0x8AAFADu,0x36E774u,0x8AAFADu,0x3973C0u},
        {0x446057u,0x9E1BC2u,0x2CA8BDu,0x9E1BC2u,0x310914u},
        {0x445FF6u,0xE79E05u,0x19D531u,0xE79E05u,0x1E3527u},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* (7) 남성 보컬 — HPF 80Hz, mud cut, 2~4kHz clarity */
    static const BiquadCoef EQ8_VOCAL_MALE[8] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x3FE291u,0x81223Cu,0x3EFF35u,0x81223Cu,0x3EE1C6u},
        {0x3FDBB5u,0x82B437u,0x3D88D3u,0x82B437u,0x3D6488u},
        {0x405FA6u,0x84A4CBu,0x3BA981u,0x84A4CBu,0x3C0927u},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x429856u,0xA28211u,0x295C31u,0xA28211u,0x2BF486u},
        {0x42720Fu,0xE93EEFu,0x157844u,0xE93EEFu,0x17EA53u},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (8) 여성 보컬 — HPF 100Hz, nasal cut, 2.5kHz clarity */
    static const BiquadCoef EQ8_VOCAL_FEMALE[8] = {
        {0x3F6907u,0x812DF2u,0x3F6907u,0x812F56u,0x3ED372u},
        {0x3FDB4Au,0x816B3Fu,0x3EBFB7u,0x816B3Fu,0x3E9B01u},
        {0x3FDCB3u,0x82AAF0u,0x3D9A11u,0x82AAF0u,0x3D76C4u},
        {0x40474Eu,0x856BCFu,0x3B27EBu,0x856BCFu,0x3B6F39u},
        {0x422EF4u,0x90D101u,0x333B3Au,0x90D101u,0x356A2Eu},
        {0x430E33u,0xAD2CBEu,0x2557EDu,0xAD2CBEu,0x286620u},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* (9) 피아노 — Full-range balance, mid clarity, attack */
    static const BiquadCoef EQ8_PIANO[8] = {
        {0x3FFAC3u,0x80BB3Cu,0x3F4AF4u,0x80BB4Bu,0x3F45C6u},
        {0x4009D5u,0x80A532u,0x3F54FFu,0x80A532u,0x3F5ED4u},
        {0x3FDBB5u,0x82B437u,0x3D88D3u,0x82B437u,0x3D6488u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x40E27Cu,0x8D6DA3u,0x35BA8Cu,0x8D6DA3u,0x369D07u},
        {0x410FCAu,0xA0381Fu,0x2D8954u,0xA0381Fu,0x2E991Eu},
        {0x43B428u,0xE90AB7u,0x14FFEDu,0xE90AB7u,0x18B416u},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (10) 어쿠스틱 기타 — HPF 80Hz, body, attack/pick clarity */
    static const BiquadCoef EQ8_GUITAR_ACOUSTIC[8] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x4013B4u,0x809C39u,0x3F5419u,0x809C39u,0x3F67CDu},
        {0x3FDBB5u,0x82B437u,0x3D88D3u,0x82B437u,0x3D6488u},
        {0x405FA6u,0x84A4CBu,0x3BA981u,0x84A4CBu,0x3C0927u},
        {0x412EE6u,0x8D307Cu,0x35AD70u,0x8D307Cu,0x36DC56u},
        {0x41EF81u,0xA2EF04u,0x298738u,0xA2EF04u,0x2B76B9u},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (11) 베이스 — Sub/low boost, hi rolloff */
    static const BiquadCoef EQ8_BASS[8] = {
        {0x400FBEu,0x80A719u,0x3F4A5Bu,0x80A6EDu,0x3F59ECu},
        {0x40318Fu,0x80AD7Du,0x3F24F9u,0x80AD7Du,0x3F5688u},
        {0x4048F1u,0x824C41u,0x3D83A2u,0x824C41u,0x3DCC93u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x3F2935u,0x934434u,0x316888u,0x934434u,0x3091BCu},
        {0x3D1DD6u,0xA96E7Du,0x26D814u,0xA96E7Du,0x23F5E9u},
        {0x392F04u,0xEAF3D6u,0x182340u,0xEAF3D6u,0x115244u},
        {0x34AA97u,0x170723u,0x0AD3A9u,0x0B31A6u,0x0B53BDu}
    };

    /* (12) 드럼 — Kick sub/punch, snare attack, cymbal air */
    static const BiquadCoef EQ8_DRUMS[8] = {
        {0x400A7Cu,0x80ABEAu,0x3F4ABAu,0x80ABCDu,0x3F5519u},
        {0x401DA5u,0x8093BFu,0x3F52A2u,0x8093BFu,0x3F7047u},
        {0x3FB761u,0x82DAE2u,0x3D8674u,0x82DAE2u,0x3D3DD6u},
        {0x3FA289u,0x876100u,0x39A68Eu,0x876100u,0x394917u},
        {0x412EE6u,0x8D307Cu,0x35AD70u,0x8D307Cu,0x36DC56u},
        {0x433F43u,0x9EC82Au,0x2D02BCu,0x9EC82Au,0x3041FFu},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x4A128Du,0x0F6D97u,0x0D4714u,0x19EE3Fu,0x0CD8F9u}
    };

    /* (13) 바이올린 — HPF, 2.5kHz overtone focus */
    static const BiquadCoef EQ8_VIOLIN[8] = {
        {0x3F8905u,0x80EDF6u,0x3F8905u,0x80EF5Bu,0x3F136Eu},
        {0x3FE7A3u,0x81CB29u,0x3E5848u,0x81CB29u,0x3E3FEBu},
        {0x4030A5u,0x82304Du,0x3DCB42u,0x82304Du,0x3DFBE6u},
        {0x408F22u,0x852D12u,0x3B1F44u,0x852D12u,0x3BAE66u},
        {0x427AA6u,0x8ED1F7u,0x350B36u,0x8ED1F7u,0x3785DCu},
        {0x43D9B6u,0xA942A8u,0x297B83u,0xA942A8u,0x2D5539u},
        {0x445FF6u,0xE79E05u,0x19D531u,0xE79E05u,0x1E3527u},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* (14) 첼로 — Deep low, rich mid, slightly warm */
    static const BiquadCoef EQ8_CELLO[8] = {
        {0x400A7Cu,0x80ABEAu,0x3F4ABAu,0x80ABCDu,0x3F5519u},
        {0x401EE0u,0x80987Bu,0x3F4B71u,0x80987Bu,0x3F6A51u},
        {0x403CF7u,0x81E833u,0x3DEC21u,0x81E833u,0x3E2918u},
        {0x407044u,0x83E902u,0x3C2C8Du,0x83E902u,0x3C9CD2u},
        {0x412EE6u,0x8D307Cu,0x35AD70u,0x8D307Cu,0x36DC56u},
        {0x414892u,0xA35E1Du,0x29ADDFu,0xA35E1Du,0x2AF671u},
        {0x400000u,0xE9A90Eu,0x16504Eu,0xE9A90Eu,0x16504Eu},
        {0x3CF503u,0x14BE91u,0x0BB60Au,0x11939Au,0x0BD605u}
    };

    /* (15) 오케스트라 — Full-range balance, hall air */
    static const BiquadCoef EQ8_ORCH[8] = {
        {0x40053Du,0x80B0E0u,0x3F4AF4u,0x80B0D1u,0x3F5022u},
        {0x4009D5u,0x80A532u,0x3F54FFu,0x80A532u,0x3F5ED4u},
        {0x3FDBB5u,0x82B437u,0x3D88D3u,0x82B437u,0x3D6488u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x40E27Cu,0x8D6DA3u,0x35BA8Cu,0x8D6DA3u,0x369D07u},
        {0x414892u,0xA35E1Du,0x29ADDFu,0xA35E1Du,0x2AF671u},
        {0x43B428u,0xE90AB7u,0x14FFEDu,0xE90AB7u,0x18B416u},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (16) 국악 일반 — 800Hz~2kHz overtone boost, clarity */
    static const BiquadCoef EQ8_GUGAK[8] = {
        {0x3FFAC3u,0x80BB3Cu,0x3F4AF4u,0x80BB4Bu,0x3F45C6u},
        {0x3FF14Du,0x81125Cu,0x3F005Au,0x81125Cu,0x3EF1A7u},
        {0x401E67u,0x820B3Au,0x3DEF40u,0x820B3Au,0x3E0DA7u},
        {0x407FC5u,0x848905u,0x3BA550u,0x848905u,0x3C2514u},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x42401Eu,0xA5D3BDu,0x25DF4Au,0xA5D3BDu,0x281F69u},
        {0x42720Fu,0xE93EEFu,0x157844u,0xE93EEFu,0x17EA53u},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (17) 국악 따뜻 — Low warmth boost, hi softened */
    static const BiquadCoef EQ8_GUGAK_WARM[8] = {
        {0x400A7Cu,0x80ABEAu,0x3F4ABAu,0x80ABCDu,0x3F5519u},
        {0x40375Fu,0x8112B5u,0x3EBC30u,0x8112B5u,0x3EF38Fu},
        {0x4050E0u,0x829CC3u,0x3D3E69u,0x829CC3u,0x3D8F4Au},
        {0x408F22u,0x852D12u,0x3B1F44u,0x852D12u,0x3BAE66u},
        {0x409690u,0x8DAC4Fu,0x35C595u,0x8DAC4Fu,0x365C25u},
        {0x3E8AF5u,0xA85B8Du,0x26A86Cu,0xA85B8Du,0x253361u},
        {0x3DA4FCu,0xEA1554u,0x1708FEu,0xEA1554u,0x14ADFAu},
        {0x3A0EFCu,0x15A5ADu,0x0B64F2u,0x0F7548u,0x0BA453u}
    };

    /* (18) 국악 밝음 — Low cut, 2kHz+ bright, air open */
    static const BiquadCoef EQ8_GUGAK_BRIGHT[8] = {
        {0x3FFAC3u,0x80BB3Cu,0x3F4AF4u,0x80BB4Bu,0x3F45C6u},
        {0x3FE291u,0x81223Cu,0x3EFF35u,0x81223Cu,0x3EE1C6u},
        {0x401E67u,0x820B3Au,0x3DEF40u,0x820B3Au,0x3E0DA7u},
        {0x407FC5u,0x848905u,0x3BA550u,0x848905u,0x3C2514u},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x43F016u,0xA1AE8Du,0x28F8AEu,0xA1AE8Du,0x2CE8C3u},
        {0x47A0FDu,0xE87190u,0x1362D5u,0xE87190u,0x1B03D3u},
        {0x4A128Du,0x0F6D97u,0x0D4714u,0x19EE3Fu,0x0CD8F9u}
    };

    /* (19) 스튜디오 모니터 — HPF 30Hz, flat/accurate reproduction */
    static const BiquadCoef EQ8_STUDIO[8] = {
        {0x3FD290u,0x805AE1u,0x3FD290u,0x805B01u,0x3FA540u},
        {0x3FF57Au,0x80C595u,0x3F48F6u,0x80C595u,0x3F3E70u},
        {0x3FF6D6u,0x8160B5u,0x3EC178u,0x8160B5u,0x3EB84Du},
        {0x400000u,0x83F16Du,0x3CBD83u,0x83F16Du,0x3CBD83u},
        {0x40396Cu,0x8BACBCu,0x383457u,0x8BACBCu,0x386DC4u},
        {0x40874Du,0xA09914u,0x2DA1DCu,0xA09914u,0x2E2929u},
        {0x41EB21u,0xE70286u,0x1EA2D4u,0xE70286u,0x208DF5u},
        {0x44DA06u,0x11C556u,0x0CA027u,0x16D2ABu,0x0C6CD9u}
    };

    /* (20) 방송 EBU — HPF, mid clarity, presence */
    static const BiquadCoef EQ8_BROADCAST[8] = {
        {0x3FA540u,0x80B581u,0x3FA540u,0x80B601u,0x3F4B00u},
        {0x3FE291u,0x81223Cu,0x3EFF35u,0x81223Cu,0x3EE1C6u},
        {0x3FDBB5u,0x82B437u,0x3D88D3u,0x82B437u,0x3D6488u},
        {0x403FAAu,0x84C154u,0x3BACCCu,0x84C154u,0x3BEC77u},
        {0x412EE6u,0x8D307Cu,0x35AD70u,0x8D307Cu,0x36DC56u},
        {0x41EF81u,0xA2EF04u,0x298738u,0xA2EF04u,0x2B76B9u},
        {0x42720Fu,0xE93EEFu,0x157844u,0xE93EEFu,0x17EA53u},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (21) 따뜻함 — Low warmth, hi softened */
    static const BiquadCoef EQ8_WARMTH[8] = {
        {0x400A7Cu,0x80ABEAu,0x3F4ABAu,0x80ABCDu,0x3F5519u},
        {0x402C5Fu,0x80DB20u,0x3EFC85u,0x80DB20u,0x3F28E4u},
        {0x4048F1u,0x824C41u,0x3D83A2u,0x824C41u,0x3DCC93u},
        {0x403FAAu,0x84C154u,0x3BACCCu,0x84C154u,0x3BEC77u},
        {0x3F2935u,0x934434u,0x316888u,0x934434u,0x3091BCu},
        {0x3D1DD6u,0xA96E7Du,0x26D814u,0xA96E7Du,0x23F5E9u},
        {0x3B5FB9u,0xEA83A1u,0x17A413u,0xEA83A1u,0x1303CBu},
        {0x374C16u,0x166792u,0x0B19BCu,0x0D5485u,0x0B78DEu}
    };

    /* (22) 프레즌스 — HPF, strong 2~4kHz presence */
    static const BiquadCoef EQ8_PRESENCE[8] = {
        {0x3FA540u,0x80B581u,0x3FA540u,0x80B601u,0x3F4B00u},
        {0x3FE291u,0x81223Cu,0x3EFF35u,0x81223Cu,0x3EE1C6u},
        {0x3FDBB5u,0x82B437u,0x3D88D3u,0x82B437u,0x3D6488u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x446057u,0x9E1BC2u,0x2CA8BDu,0x9E1BC2u,0x310914u},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* (23) V자형 — Low+hi boost, mid cut */
    static const BiquadCoef EQ8_VSHAPED[8] = {
        {0x401503u,0x80A26Cu,0x3F49D5u,0x80A230u,0x3F5E9Cu},
        {0x402C5Fu,0x80DB20u,0x3EFC85u,0x80DB20u,0x3F28E4u},
        {0x3FB761u,0x82DAE2u,0x3D8674u,0x82DAE2u,0x3D3DD6u},
        {0x3EE828u,0x882564u,0x399B76u,0x882564u,0x38839Eu},
        {0x3E54C1u,0x9409A1u,0x317098u,0x9409A1u,0x2FC559u},
        {0x4304FEu,0xA558EAu,0x25A83Eu,0xA558EAu,0x28AD3Cu},
        {0x47A0FDu,0xE87190u,0x1362D5u,0xE87190u,0x1B03D3u},
        {0x4DC5C9u,0x0D9A53u,0x0DC3C1u,0x1BFBC0u,0x0D281Du}
    };

    /* (24) 라우드니스 — Fletcher-Munson: low+hi boost */
    static const BiquadCoef EQ8_LOUDNESS[8] = {
        {0x401F9Du,0x809979u,0x3F4856u,0x80991Eu,0x3F6798u},
        {0x40375Fu,0x811075u,0x3EBC2Eu,0x811075u,0x3EF38Du},
        {0x402460u,0x826D01u,0x3D876Du,0x826D01u,0x3DABCCu},
        {0x400000u,0x83F16Du,0x3CBD83u,0x83F16Du,0x3CBD83u},
        {0x409690u,0x8DAC4Fu,0x35C595u,0x8DAC4Fu,0x365C25u},
        {0x429856u,0xA28211u,0x295C31u,0xA28211u,0x2BF486u},
        {0x47A0FDu,0xE87190u,0x1362D5u,0xE87190u,0x1B03D3u},
        {0x4DC5C9u,0x0D9A53u,0x0DC3C1u,0x1BFBC0u,0x0D281Du}
    };

    /* (25) 스쿱드 미드 — Metal: low+hi boost, deep mid cut */
    static const BiquadCoef EQ8_SCOOPED[8] = {
        {0x401503u,0x80A26Cu,0x3F49D5u,0x80A230u,0x3F5E9Cu},
        {0x403B69u,0x80CF2Bu,0x3EF970u,0x80CF2Bu,0x3F34D9u},
        {0x3F92E9u,0x8303BEu,0x3D8209u,0x8303BEu,0x3D14F2u},
        {0x3E3480u,0x8A6017u,0x381146u,0x8A6017u,0x3645C6u},
        {0x3CFC67u,0x984C6Eu,0x2E5FAAu,0x984C6Eu,0x2B5C11u},
        {0x4304FEu,0xA558EAu,0x25A83Eu,0xA558EAu,0x28AD3Cu},
        {0x47A0FDu,0xE87190u,0x1362D5u,0xE87190u,0x1B03D3u},
        {0x4DC5C9u,0x0D9A53u,0x0DC3C1u,0x1BFBC0u,0x0D281Du}
    };

    /* (26) 중역 부스트 — 300Hz~2kHz focused boost */
    static const BiquadCoef EQ8_MIDBOOST[8] = {
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x3FF14Du,0x81125Cu,0x3F005Au,0x81125Cu,0x3EF1A7u},
        {0x4048F1u,0x824C41u,0x3D83A2u,0x824C41u,0x3DCC93u},
        {0x410214u,0x842128u,0x3B8B70u,0x842128u,0x3C8D84u},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x42401Eu,0xA5D3BDu,0x25DF4Au,0xA5D3BDu,0x281F69u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u}
    };

    /* (27) 앰비언트 — Low spatial feel, hi-mid softened */
    static const BiquadCoef EQ8_AMBIENT[8] = {
        {0x400A7Cu,0x80ABEAu,0x3F4ABAu,0x80ABCDu,0x3F5519u},
        {0x4024CBu,0x812279u,0x3EBEFFu,0x812279u,0x3EE3CAu},
        {0x402853u,0x82C0FCu,0x3D42B1u,0x82C0FCu,0x3D6B05u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x3F6ACFu,0x8EB6D1u,0x35DD6Du,0x8EB6D1u,0x35483Cu},
        {0x3D1DD6u,0xA96E7Du,0x26D814u,0xA96E7Du,0x23F5E9u},
        {0x42720Fu,0xE93EEFu,0x157844u,0xE93EEFu,0x17EA53u},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (28) 에어 — HPF, strong 10kHz+ air */
    static const BiquadCoef EQ8_AIRY[8] = {
        {0x3FA540u,0x80B581u,0x3FA540u,0x80B601u,0x3F4B00u},
        {0x3FF14Du,0x81125Cu,0x3F005Au,0x81125Cu,0x3EF1A7u},
        {0x3FDBB5u,0x82B437u,0x3D88D3u,0x82B437u,0x3D6488u},
        {0x400000u,0x83F16Du,0x3CBD83u,0x83F16Du,0x3CBD83u},
        {0x409690u,0x8DAC4Fu,0x35C595u,0x8DAC4Fu,0x365C25u},
        {0x429856u,0xA28211u,0x295C31u,0xA28211u,0x2BF486u},
        {0x4A60FEu,0xE80E7Du,0x1221A0u,0xE80E7Du,0x1C829Du},
        {0x51A8C8u,0x0B8C51u,0x0E4CA5u,0x1E0506u,0x0D7CB9u}
    };

    /* (29) 전자음악 — Sub/kick boost, synth presence */
    static const BiquadCoef EQ8_ELECTRONIC[8] = {
        {0x401503u,0x80A26Cu,0x3F49D5u,0x80A230u,0x3F5E9Cu},
        {0x401EE0u,0x80987Bu,0x3F4B71u,0x80987Bu,0x3F6A51u},
        {0x3FC343u,0x825FDEu,0x3DEE1Au,0x825FDEu,0x3DB15Du},
        {0x3FADA5u,0x866DB4u,0x3A67C3u,0x866DB4u,0x3A1568u},
        {0x422EF4u,0x90D101u,0x333B3Au,0x90D101u,0x356A2Eu},
        {0x43D9B6u,0xA942A8u,0x297B83u,0xA942A8u,0x2D5539u},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x4A128Du,0x0F6D97u,0x0D4714u,0x19EE3Fu,0x0CD8F9u}
    };

    /* (30) 빈티지 테이프 — Low warmth, 5kHz+ rolloff */
    static const BiquadCoef EQ8_VINTAGE[8] = {
        {0x4014FEu,0x80DEE0u,0x3F0E41u,0x80DE91u,0x3F22F0u},
        {0x40189Au,0x80C44Du,0x3F2960u,0x80C44Du,0x3F41FAu},
        {0x403032u,0x8341D1u,0x3CB9D0u,0x8341D1u,0x3CEA03u},
        {0x401DD6u,0x84FEBAu,0x3BF329u,0x84FEBAu,0x3C10FFu},
        {0x3DAC6Cu,0x9EA502u,0x2BB408u,0x9EA502u,0x296074u},
        {0x3B08D7u,0xB56D84u,0x22F631u,0xB56D84u,0x1DFF07u},
        {0x3711C4u,0xEB65CEu,0x188829u,0xEB65CEu,0x0F99EEu},
        {0x3228DDu,0x178718u,0x0A920Du,0x090CFFu,0x0B3503u}
    };

    /* (31) 크리스피 — HPF, 5~10kHz attack/air boost */
    static const BiquadCoef EQ8_CRISPY[8] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x3FE291u,0x81223Cu,0x3EFF35u,0x81223Cu,0x3EE1C6u},
        {0x3FDBB5u,0x82B437u,0x3D88D3u,0x82B437u,0x3D6488u},
        {0x3FC095u,0x853B74u,0x3BB116u,0x853B74u,0x3B71AAu},
        {0x4171CCu,0x915BCCu,0x3365D0u,0x915BCCu,0x34D79Cu},
        {0x453236u,0xA88C04u,0x29093Au,0xA88C04u,0x2E3B70u},
        {0x4A60FEu,0xE80E7Du,0x1221A0u,0xE80E7Du,0x1C829Du},
        {0x51A8C8u,0x0B8C51u,0x0E4CA5u,0x1E0506u,0x0D7CB9u}
    };

    /* (32) 디에서 — 6kHz sibilance notch, hi softened */
    static const BiquadCoef EQ8_DEESS[8] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x3FE7A3u,0x81CB29u,0x3E5848u,0x81CB29u,0x3E3FEBu},
        {0x402851u,0x82D98Fu,0x3D42DFu,0x82D98Fu,0x3D6B30u},
        {0x403BBFu,0x84E33Fu,0x3BF0F7u,0x84E33Fu,0x3C2CB6u},
        {0x3ED3BCu,0x9DAC22u,0x2B9A19u,0x9DAC22u,0x2A6DD5u},
        {0x39D0C0u,0xB54C21u,0x2FD47Cu,0xB54C21u,0x29A53Du},
        {0x3AD3B2u,0xE80A38u,0x21BF69u,0xE80A38u,0x1C931Bu},
        {0x3A0EFCu,0x15A5ADu,0x0B64F2u,0x0F7548u,0x0BA453u}
    };

    /* (33) 딥 베이스 — Extreme sub, strong hi rolloff */
    static const BiquadCoef EQ8_DEEP_BASS[8] = {
        {0x401C35u,0x806095u,0x3F83ECu,0x80605Eu,0x3F9FEAu},
        {0x402996u,0x806CA8u,0x3F6B8Cu,0x806CA8u,0x3F9522u},
        {0x403B08u,0x81254Du,0x3EA6CBu,0x81254Du,0x3EE1D2u},
        {0x400000u,0x81D387u,0x3E58CDu,0x81D387u,0x3E58CDu},
        {0x3EA791u,0x8A3ADDu,0x3821BAu,0x8A3ADDu,0x36C94Bu},
        {0x3B6A41u,0xA0B326u,0x2BBCB5u,0xA0B326u,0x2726F6u},
        {0x35BF25u,0xD76D7Fu,0x1B65DDu,0xD76D7Fu,0x112502u},
        {0x2FC55Bu,0x17E9FFu,0x0A544Cu,0x06E6E6u,0x0B1CBFu}
    };

    /* (34) 서브 베이스 — 40Hz sub extreme boost, full hi cut */
    static const BiquadCoef EQ8_SUBBASS[8] = {
        {0x401C35u,0x806095u,0x3F83ECu,0x80605Eu,0x3F9FEAu},
        {0x402996u,0x806CA8u,0x3F6B8Cu,0x806CA8u,0x3F9522u},
        {0x403B08u,0x81254Du,0x3EA6CBu,0x81254Du,0x3EE1D2u},
        {0x400000u,0x81D387u,0x3E58CDu,0x81D387u,0x3E58CDu},
        {0x3EA791u,0x8A3ADDu,0x3821BAu,0x8A3ADDu,0x36C94Bu},
        {0x3B6A41u,0xA0B326u,0x2BBCB5u,0xA0B326u,0x2726F6u},
        {0x35BF25u,0xD76D7Fu,0x1B65DDu,0xD76D7Fu,0x112502u},
        {0x2FC55Bu,0x17E9FFu,0x0A544Cu,0x06E6E6u,0x0B1CBFu}
    };

    /* (35) 콘서트홀 — Hall low, string/wind range, hall air */
    static const BiquadCoef EQ8_CONCERT_HALL[8] = {
        {0x40053Du,0x80B0E0u,0x3F4AF4u,0x80B0D1u,0x3F5022u},
        {0x4009D5u,0x80A532u,0x3F54FFu,0x80A532u,0x3F5ED4u},
        {0x3FDBB5u,0x82B437u,0x3D88D3u,0x82B437u,0x3D6488u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x40E27Cu,0x8D6DA3u,0x35BA8Cu,0x8D6DA3u,0x369D07u},
        {0x414892u,0xA35E1Du,0x29ADDFu,0xA35E1Du,0x2AF671u},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* (36) 동굴 — Low resonance boost, extreme hi cut */
    static const BiquadCoef EQ8_CAVE[8] = {
        {0x402BDBu,0x810775u,0x3ED067u,0x8106A5u,0x3EFB72u},
        {0x4041F6u,0x80ECB4u,0x3EDC7Cu,0x80ECB4u,0x3F1E73u},
        {0x407824u,0x83E429u,0x3BE7E0u,0x83E429u,0x3C6003u},
        {0x400000u,0x851AF5u,0x3BF485u,0x851AF5u,0x3BF485u},
        {0x3CFC67u,0x984C6Eu,0x2E5FAAu,0x984C6Eu,0x2B5C11u},
        {0x37E9C5u,0xB7C4E7u,0x2321C0u,0xB7C4E7u,0x1B0B85u},
        {0x330DB1u,0xEC4E68u,0x190986u,0xEC4E68u,0x0C1737u},
        {0x2B5332u,0x186219u,0x09E23Eu,0x0297BDu,0x0AFFCEu}
    };

    /* (37) 수중 — Low boost, LPF 600Hz only */
    static const BiquadCoef EQ8_UNDERWATER[8] = {
        {0x404622u,0x821F29u,0x3DA89Du,0x821C98u,0x3DEC2Fu},
        {0x40782Bu,0x83CBDBu,0x3BE79Du,0x83CBDBu,0x3C5FC8u},
        {0x00176Au,0x002ED5u,0x00176Au,0x89AD93u,0x36B017u},
        {0x000A8Au,0x001513u,0x000A8Au,0x880634u,0x3823F3u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u}
    };

    /* (38) 라디오 — HPF 200Hz + LPF 4kHz, mid boost */
    static const BiquadCoef EQ8_RADIO[8] = {
        {0x3ED372u,0x82591Cu,0x3ED372u,0x825E9Fu,0x3DAC67u},
        {0x406497u,0x834D7Cu,0x3C926Bu,0x834D7Cu,0x3CF703u},
        {0x40EEB2u,0x8593A5u,0x3A8C18u,0x8593A5u,0x3B7ACBu},
        {0x412EE6u,0x8D307Cu,0x35AD70u,0x8D307Cu,0x36DC56u},
        {0x032AD0u,0x06559Fu,0x032AD0u,0xAE1B54u,0x1E8FEBu},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u}
    };

    /* (39) 메가폰 — HPF 400Hz + LPF 5kHz, mid boost */
    static const BiquadCoef EQ8_MEGAPHONE[8] = {
        {0x3E975Cu,0x82D149u,0x3E975Cu,0x82E742u,0x3D44B0u},
        {0x407FC5u,0x848905u,0x3BA550u,0x848905u,0x3C2514u},
        {0x4250EAu,0x884C01u,0x37BB6Fu,0x884C01u,0x3A0C59u},
        {0x439976u,0x9272A6u,0x3017AAu,0x9272A6u,0x33B121u},
        {0x04CA48u,0x09948Fu,0x04CA48u,0xB67060u,0x1CB8BEu},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u}
    };

    /* (40) 욕실 타일 — HPF, multi-band resonance boost */
    static const BiquadCoef EQ8_BATHROOM[8] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x401D7Bu,0x80FCCEu,0x3EFECDu,0x80FCCEu,0x3F1C48u},
        {0x4075FAu,0x81F72Du,0x3DF69Eu,0x81F72Du,0x3E6C98u},
        {0x40D753u,0x859A7Cu,0x3B14C5u,0x859A7Cu,0x3BEC18u},
        {0x411146u,0x8E1B4Eu,0x36AFE4u,0x8E1B4Eu,0x37C12Au},
        {0x3D8F11u,0xAD489Fu,0x2AB3EBu,0xAD489Fu,0x2842FCu},
        {0x3DA4FCu,0xEA1554u,0x1708FEu,0xEA1554u,0x14ADFAu},
        {0x3A0EFCu,0x15A5ADu,0x0B64F2u,0x0F7548u,0x0BA453u}
    };

    /* (41) 자연음 — HPF 40Hz, minimal EQ, transparent mid */
    static const BiquadCoef EQ8_NATURAL[8] = {
        {0x3FC371u,0x80791Du,0x3FC371u,0x807957u,0x3F871Cu},
        {0x3FF2E9u,0x81D3D8u,0x3E3F20u,0x81D434u,0x3E3264u},
        {0x3FEFD6u,0x826E14u,0x3DCE34u,0x826E14u,0x3DBE09u},
        {0x403BBFu,0x84E33Fu,0x3BF0F7u,0x84E33Fu,0x3C2CB6u},
        {0x40B7A5u,0x91ED58u,0x338643u,0x91ED58u,0x343DE8u},
        {0x4246F1u,0xAD9E4Au,0x25900Eu,0xAD9E4Au,0x27D6FFu},
        {0x42720Fu,0xE93EEFu,0x157844u,0xE93EEFu,0x17EA53u},
        {0x44DA06u,0x11C556u,0x0CA027u,0x16D2ABu,0x0C6CD9u}
    };

    /* (42) 보컬 BPF — HPF 250Hz + LPF 5kHz, vocal sharpening */
    static const BiquadCoef EQ8_BPF_VOICE[8] = {
        {0x3E892Cu,0x82EDA8u,0x3E892Cu,0x82F63Bu,0x3D1AEBu},
        {0x402851u,0x82D98Fu,0x3D42DFu,0x82D98Fu,0x3D6B30u},
        {0x409E5Cu,0x85D517u,0x3A9A6Cu,0x85D517u,0x3B38C8u},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x41B110u,0x95D0C0u,0x313DE0u,0x95D0C0u,0x32EEF0u},
        {0x049F60u,0x093EC1u,0x049F60u,0xB90339u,0x197A49u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u}
    };

    /* (43) 험 제거 — 50/60Hz notch + harmonics + HPF 30Hz */
    static const BiquadCoef EQ8_NOTCH_HUMS[8] = {
        {0x3FFE37u,0x800447u,0x3FFE37u,0x800447u,0x3FFC6Du},
        {0x3FFDDBu,0x80054Du,0x3FFDDBu,0x80054Du,0x3FFBB6u},
        {0x3FFAA4u,0x800D87u,0x3FFAA4u,0x800D87u,0x3FF548u},
        {0x3FF992u,0x8010E7u,0x3FF992u,0x8010E7u,0x3FF323u},
        {0x3FD290u,0x805AE1u,0x3FD290u,0x805B01u,0x3FA540u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u}
    };

    /* (44) 틸트 따뜻 — Warm tilt: full-spectrum low-leaning */
    static const BiquadCoef EQ8_TILT_WARM[8] = {
        {0x401A3Eu,0x8116B0u,0x3ED261u,0x811634u,0x3EEC23u},
        {0x40347Au,0x835DABu,0x3C897Au,0x835AD3u,0x3CBB1Eu},
        {0x402459u,0x82B6EDu,0x3D87E7u,0x82B6EDu,0x3DAC40u},
        {0x400000u,0x851AF5u,0x3BF485u,0x851AF5u,0x3BF485u},
        {0x3F6ACFu,0x8EB6D1u,0x35DD6Du,0x8EB6D1u,0x35483Cu},
        {0x34FD83u,0xBE3F25u,0x1848FBu,0xABBAD2u,0x1FCAD0u},
        {0x330B2Cu,0xE509AEu,0x0B4C35u,0xD29E07u,0x10C309u},
        {0x34AA97u,0x170723u,0x0AD3A9u,0x0B31A6u,0x0B53BDu}
    };

    /* (45) 틸트 밝음 — Bright tilt: full-spectrum hi-leaning */
    static const BiquadCoef EQ8_TILT_BRIGHT[8] = {
        {0x3FE5CDu,0x814A29u,0x3ED261u,0x814AA4u,0x3EB8A9u},
        {0x3FCBB1u,0x83C0B4u,0x3C897Au,0x83C389u,0x3C5800u},
        {0x3FDBBCu,0x82FDECu,0x3D894Du,0x82FDECu,0x3D6509u},
        {0x400000u,0x851AF5u,0x3BF485u,0x851AF5u,0x3BF485u},
        {0x409690u,0x8DAC4Fu,0x35C595u,0x8DAC4Fu,0x365C25u},
        {0x4D4C15u,0x9A3897u,0x2665CDu,0xB095CDu,0x1D54ACu},
        {0x503EBDu,0xC7190Cu,0x150439u,0xDE31ACu,0x0E2A56u},
        {0x4DC5C9u,0x0D9A53u,0x0DC3C1u,0x1BFBC0u,0x0D281Du}
    };

    /* (46) 피드백 억제 — 800/1600/3200Hz notch, HPF, hi cut */
    static const BiquadCoef EQ8_ANTI_FEEDBACK[8] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x3FF14Du,0x81125Cu,0x3F005Au,0x81125Cu,0x3EF1A7u},
        {0x3FDBB5u,0x82B437u,0x3D88D3u,0x82B437u,0x3D6488u},
        {0x3F95A8u,0x818707u,0x3F95A8u,0x818707u,0x3F2B51u},
        {0x3F2DD4u,0x846737u,0x3F2DD4u,0x846737u,0x3E5BA9u},
        {0x3E69D4u,0x8DF70Fu,0x3E69D4u,0x8DF70Fu,0x3CD3A7u},
        {0x3B5FB9u,0xEA83A1u,0x17A413u,0xEA83A1u,0x1303CBu},
        {0x374C16u,0x166792u,0x0B19BCu,0x0D5485u,0x0B78DEu}
    };

    /* (47) 틴 휘슬 — Irish tin whistle: HPF 200Hz, 1.2kHz 명도, 3kHz 특유 배음, air open */
    static const BiquadCoef EQ8_TIN_WHISTLE[8] = {
        {0x3F1261u,0x81DB3Du,0x3F1261u,0x81E0C6u,0x3E2A4Bu},
        {0x3FE7A4u,0x81D8F0u,0x3E5857u,0x81D8F0u,0x3E3FFBu},
        {0x403F79u,0x832815u,0x3D1F16u,0x832815u,0x3D5E8Fu},
        {0x40EE68u,0x860901u,0x3A8DCAu,0x860901u,0x3B7C32u},
        {0x4371A8u,0x949F97u,0x30C796u,0x949F97u,0x34393Eu},
        {0x437248u,0xB8506Bu,0x21EEC6u,0xB8506Bu,0x25610Eu},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [48+]  § B  관악기 — 목관
     * ═══════════════════════════════════════════════════════════════ */

     /* (48) 틴 휘슬 따뜻 — Tin whistle with warmer body, reduced harshness */
    static const BiquadCoef EQ8_TIN_WHISTLE_WARM[8] = {
        {0x3F2A14u,0x81ABD7u,0x3F2A14u,0x81B054u,0x3E58A6u},
        {0x401866u,0x81A8D9u,0x3E57B6u,0x81A8D9u,0x3E701Bu},
        {0x4054C3u,0x83156Fu,0x3D1C86u,0x83156Fu,0x3D7148u},
        {0x40C62Bu,0x862921u,0x3A9580u,0x862921u,0x3B5BABu},
        {0x41B110u,0x95D0C0u,0x313DE0u,0x95D0C0u,0x32EEF0u},
        {0x414168u,0xAAC6E3u,0x2A2A77u,0xAAC6E3u,0x2B6BDFu},
        {0x42720Fu,0xE93EEFu,0x157844u,0xE93EEFu,0x17EA53u},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (49) 틴 휘슬 세션 — Session/pub setting: cut mid mud, boost attack clarity */
    static const BiquadCoef EQ8_TIN_WHISTLE_SESSION[8] = {
        {0x3F141Fu,0x81D7C3u,0x3F141Fu,0x81DE75u,0x3E2EEFu},
        {0x3FB95Du,0x82D090u,0x3D97BDu,0x82D090u,0x3D511Au},
        {0x3FC095u,0x853B74u,0x3BB116u,0x853B74u,0x3B71AAu},
        {0x4123F2u,0x86A9A2u,0x3A436Cu,0x86A9A2u,0x3B675Eu},
        {0x43EC95u,0x993C1Fu,0x2EA851u,0x993C1Fu,0x3294E6u},
        {0x44C01Bu,0xC36B83u,0x1EC35Fu,0xC36B83u,0x23837Au},
        {0x43CB17u,0x000000u,0x13F5B4u,0x000000u,0x17C0CBu},
        {0x446436u,0x1C629Eu,0x0DD3FCu,0x20A788u,0x0DF348u}
    };

    /* (50) 플루트 — Concert flute: HPF 200Hz, 800Hz warmth, 2~4kHz shimmer, air */
    static const BiquadCoef EQ8_FLUTE[8] = {
        {0x3EF580u,0x821500u,0x3EF580u,0x821A86u,0x3DF087u},
        {0x3FE7A4u,0x81D8F0u,0x3E5857u,0x81D8F0u,0x3E3FFBu},
        {0x406B03u,0x83E990u,0x3C5A68u,0x83E990u,0x3CC56Bu},
        {0x408441u,0x87D5DDu,0x3A008Du,0x87D5DDu,0x3A84CEu},
        {0x421F93u,0x9581CBu,0x3124D4u,0x9581CBu,0x334467u},
        {0x429115u,0xB8BF32u,0x223350u,0xB8BF32u,0x24C465u},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [51+]  § B-2  목관 — 색소폰 패밀리
     * ═══════════════════════════════════════════════════════════════ */

     /* (51) 리코더 — Recorder: HPF 150Hz, woody 600Hz body, sweet 2kHz overtone */
    static const BiquadCoef EQ8_RECORDER[8] = {
        {0x3F37D4u,0x819058u,0x3F37D4u,0x819377u,0x3E72C7u},
        {0x401460u,0x815F51u,0x3E9DADu,0x815F51u,0x3EB20Du},
        {0x4079B6u,0x82B142u,0x3D3839u,0x82B142u,0x3DB1F0u},
        {0x408EE3u,0x85D585u,0x3B2170u,0x85D585u,0x3BB052u},
        {0x411460u,0x91A3B6u,0x33774Au,0x91A3B6u,0x348BAAu},
        {0x400000u,0xB225C6u,0x222180u,0xB225C6u,0x222180u},
        {0x400000u,0xE9A90Eu,0x16504Eu,0xE9A90Eu,0x16504Eu},
        {0x3C6B7Eu,0x020985u,0x0A60E9u,0xFDD794u,0x0AFE58u}
    };

    /* (52) 오카리나 — Ocarina: rounded low-mid, mellow 1~2kHz, no harsh highs */
    static const BiquadCoef EQ8_OCARINA[8] = {
        {0x3F1BDAu,0x81C84Du,0x3F1BDAu,0x81CB6Au,0x3E3AD1u},
        {0x4030E9u,0x8192C1u,0x3E554Fu,0x8192C1u,0x3E8638u},
        {0x408D85u,0x8334AFu,0x3CC462u,0x8334AFu,0x3D51E7u},
        {0x40B0B9u,0x87B071u,0x39FA3Cu,0x87B071u,0x3AAAF5u},
        {0x400000u,0x8FCF02u,0x367AA2u,0x8FCF02u,0x367AA2u},
        {0x3DED20u,0xA29CCDu,0x2DE888u,0xA29CCDu,0x2BD5A8u},
        {0x39A62Bu,0xD5BEE2u,0x1ADC11u,0xD5BEE2u,0x14823Cu},
        {0x32D646u,0x06D877u,0x08E2A3u,0xF761C7u,0x0B2F99u}
    };

    /* (53) 알토 색소폰 — Alto sax: Eb3~Ab5, woody 400Hz body, reedy 1.5~3kHz bite */
    static const BiquadCoef EQ8_SAXOPHONE_ALTO[8] = {
        {0x3F7A5Bu,0x810B4Bu,0x3F7A5Bu,0x810CAFu,0x3EF61Au},
        {0x3FEBA6u,0x81815Bu,0x3E9E18u,0x81815Bu,0x3E89BEu},
        {0x4061C7u,0x820648u,0x3DC433u,0x820648u,0x3E25FAu},
        {0x40510Fu,0x843934u,0x3C52E8u,0x843934u,0x3CA3F8u},
        {0x422DD8u,0x8AC830u,0x365EA5u,0x8AC830u,0x388C7Du},
        {0x42F008u,0x9CFFD8u,0x2B7255u,0x9CFFD8u,0x2E625Du},
        {0x424419u,0xD2922Au,0x189793u,0xD2922Au,0x1ADBACu},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (54) 테너 색소폰 — Tenor sax: Bb2~E5, deep body 250Hz, rich 1~2kHz, smooth top */
    static const BiquadCoef EQ8_SAXOPHONE_TENOR[8] = {
        {0x3F9505u,0x80D5F6u,0x3F9505u,0x80D6DAu,0x3F2AEFu},
        {0x400C47u,0x80CF7Eu,0x3F2A83u,0x80CF7Eu,0x3F36CAu},
        {0x405C15u,0x8150ABu,0x3E690Bu,0x8150ABu,0x3EC521u},
        {0x4054C3u,0x83156Fu,0x3D1C86u,0x83156Fu,0x3D7148u},
        {0x419A6Bu,0x88CBF8u,0x37EF75u,0x88CBF8u,0x3989E0u},
        {0x420FDBu,0x98753Au,0x2E02DFu,0x98753Au,0x3012BAu},
        {0x410E14u,0xC7C194u,0x1B55EBu,0xC7C194u,0x1C63FFu},
        {0x400000u,0x000000u,0x0AFB0Du,0x000000u,0x0AFB0Du}
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [55+]  § B-3  목관 — 기타
     * ═══════════════════════════════════════════════════════════════ */

     /* (55) 소프라노 색소폰 — Soprano sax: bright Bb3~G6, nasal 1~2kHz, edgy 3~4kHz */
    static const BiquadCoef EQ8_SAXOPHONE_SOPRANO[8] = {
        {0x3F4DA8u,0x8164B0u,0x3F4DA8u,0x8167D0u,0x3E9E70u},
        {0x3FD743u,0x819742u,0x3E9C93u,0x819742u,0x3E73D6u},
        {0x402064u,0x823F1Bu,0x3DCCAFu,0x823F1Bu,0x3DED13u},
        {0x40962Bu,0x849523u,0x3BE554u,0x849523u,0x3C7B7Fu},
        {0x4217A7u,0x8C81D9u,0x35797Bu,0x8C81D9u,0x379122u},
        {0x429856u,0xA28211u,0x295C31u,0xA28211u,0x2BF486u},
        {0x43972Bu,0xDD984Au,0x16507Eu,0xDD984Au,0x19E7A9u},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (56) 바리톤 색소폰 — Bari sax: Bb1~E4, massive 100~200Hz body, deep reedy texture */
    static const BiquadCoef EQ8_SAXOPHONE_BARITONE[8] = {
        {0x400A7Cu,0x80ABEAu,0x3F4ABAu,0x80ABCDu,0x3F5519u},
        {0x402504u,0x80B77Au,0x3F2787u,0x80B77Au,0x3F4C8Cu},
        {0x404882u,0x81056Au,0x3EBF90u,0x81056Au,0x3F0812u},
        {0x4048E3u,0x829646u,0x3D841Du,0x829646u,0x3DCD01u},
        {0x411C15u,0x86E3B9u,0x3982AAu,0x86E3B9u,0x3A9EC0u},
        {0x4151E9u,0x940DF0u,0x30ACE1u,0x940DF0u,0x31FECAu},
        {0x400000u,0xBD21BEu,0x1E90DFu,0xBD21BEu,0x1E90DFu},
        {0x3BE330u,0xEF8D89u,0x0B1350u,0xEA3856u,0x0C4BB3u}
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [57+]  § B-4  목관 — 리드 악기
     * ═══════════════════════════════════════════════════════════════ */

     /* (57) 클라리넷 — Clarinet: hollow 500Hz chalumeau, 1.5kHz throat, 3kHz clarity */
    static const BiquadCoef EQ8_CLARINET[8] = {
        {0x3F5FB8u,0x81408Fu,0x3F5FB8u,0x814290u,0x3EC171u},
        {0x3FEFAFu,0x813737u,0x3EE43Au,0x813737u,0x3ED3E9u},
        {0x4065C3u,0x823275u,0x3DACE2u,0x823275u,0x3E12A6u},
        {0x403BBFu,0x84E33Fu,0x3BF0F7u,0x84E33Fu,0x3C2CB6u},
        {0x417BDDu,0x8CF4D2u,0x359E3Eu,0x8CF4D2u,0x371A1Bu},
        {0x41EF81u,0xA2EF04u,0x298738u,0xA2EF04u,0x2B76B9u},
        {0x425F15u,0xDDE42Cu,0x16C249u,0xDDE42Cu,0x19215Du},
        {0x4331E0u,0x127438u,0x0C6D46u,0x15C7AAu,0x0C4BB3u}
    };

    /* (58) 오보에 — Oboe: nasal 800Hz core, reedy 1.5~3kHz bite, bright edge */
    static const BiquadCoef EQ8_OBOE[8] = {
        {0x3F4DA8u,0x8164B0u,0x3F4DA8u,0x8167D0u,0x3E9E70u},
        {0x3FEBA7u,0x818794u,0x3E9E1Eu,0x818794u,0x3E89C5u},
        {0x403696u,0x82A660u,0x3D8645u,0x82A660u,0x3DBCDAu},
        {0x40BEBAu,0x83DE81u,0x3C4093u,0x83DE81u,0x3CFF4Du},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x41EF81u,0xA2EF04u,0x298738u,0xA2EF04u,0x2B76B9u},
        {0x425F15u,0xDDE42Cu,0x16C249u,0xDDE42Cu,0x19215Du},
        {0x437E55u,0x0817FFu,0x0BC070u,0x0BF66Bu,0x0B605Au}
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [59+]  § B-5  금관악기
     * ═══════════════════════════════════════════════════════════════ */

     /* (59) 바순 — Bassoon: warm 100~300Hz body, reedy 600Hz, mellow top */
    static const BiquadCoef EQ8_BASSOON[8] = {
        {0x40053Du,0x80B0E0u,0x3F4AF4u,0x80B0D1u,0x3F5022u},
        {0x401ECCu,0x80BCAFu,0x3F288Au,0x80BCAFu,0x3F4756u},
        {0x40499Du,0x817DDEu,0x3E5182u,0x817DDEu,0x3E9B1Fu},
        {0x4054C3u,0x83156Fu,0x3D1C86u,0x83156Fu,0x3D7148u},
        {0x40ADDBu,0x8986C8u,0x381D8Cu,0x8986C8u,0x38CB67u},
        {0x400000u,0x9CBC95u,0x2B711Fu,0x9CBC95u,0x2B711Fu},
        {0x3DF0D8u,0xC921FBu,0x1C3044u,0xC921FBu,0x1A211Du},
        {0x390A41u,0x03D862u,0x09D515u,0xFBAF82u,0x0B0837u}
    };

    /* (60) 백파이프 — Bagpipe: drone+chanter, 200~500Hz warm, aggressive 1~3kHz */
    static const BiquadCoef EQ8_BAGPIPE[8] = {
        {0x400DFBu,0x80E545u,0x3F0EC0u,0x80E510u,0x3F1C87u},
        {0x403321u,0x813DBAu,0x3E9A45u,0x813DBAu,0x3ECD66u},
        {0x406DC9u,0x824C29u,0x3D7DF6u,0x824C29u,0x3DEBC0u},
        {0x4077E4u,0x84AE78u,0x3BEA0Eu,0x84AE78u,0x3C61F2u},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x41EF81u,0xA2EF04u,0x298738u,0xA2EF04u,0x2B76B9u},
        {0x424419u,0xD2922Au,0x189793u,0xD2922Au,0x1ADBACu},
        {0x437E55u,0x0817FFu,0x0BC070u,0x0BF66Bu,0x0B605Au}
    };

    /* (61) 하모니카 — Harmonica: reedy 600~1.5kHz, nasal mid, punchy attack */
    static const BiquadCoef EQ8_HARMONICA[8] = {
        {0x3F5FB8u,0x81408Fu,0x3F5FB8u,0x814290u,0x3EC171u},
        {0x3FEBA6u,0x81815Bu,0x3E9E18u,0x81815Bu,0x3E89BEu},
        {0x4050DBu,0x82B560u,0x3D3E97u,0x82B560u,0x3D8F72u},
        {0x40BEBAu,0x83DE81u,0x3C4093u,0x83DE81u,0x3CFF4Du},
        {0x419F57u,0x8B2F57u,0x368311u,0x8B2F57u,0x382269u},
        {0x41BE95u,0x9DCCD9u,0x2BBF35u,0x9DCCD9u,0x2D7DCAu},
        {0x424419u,0xD2922Au,0x189793u,0xD2922Au,0x1ADBACu},
        {0x437E55u,0x0817FFu,0x0BC070u,0x0BF66Bu,0x0B605Au}
    };

    /* (62) 트럼펫 — Trumpet: bright 1~2kHz attack, strong 3~5kHz presence, projecting */
    static const BiquadCoef EQ8_TRUMPET[8] = {
        {0x3F7143u,0x811D79u,0x3F7143u,0x811F7Au,0x3EE488u},
        {0x3FEBA6u,0x81815Bu,0x3E9E18u,0x81815Bu,0x3E89BEu},
        {0x402851u,0x82D98Fu,0x3D42DFu,0x82D98Fu,0x3D6B30u},
        {0x411C15u,0x86E3B9u,0x3982AAu,0x86E3B9u,0x3A9EC0u},
        {0x42EFA0u,0x904CB9u,0x330640u,0x904CB9u,0x35F5E0u},
        {0x43D81Cu,0xACBD3Cu,0x251A90u,0xACBD3Cu,0x28F2ACu},
        {0x43B428u,0xE90AB7u,0x14FFEDu,0xE90AB7u,0x18B416u},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [63+]  § C  현악기 / 발현악기
     * ═══════════════════════════════════════════════════════════════ */

     /* (63) 트롬본 — Trombone: warm 150~400Hz slide body, 1~2kHz brass shimmer */
    static const BiquadCoef EQ8_TROMBONE[8] = {
        {0x4006FCu,0x80EBDBu,0x3F0F0Cu,0x80EBC1u,0x3F15EEu},
        {0x402E0Fu,0x811D3Au,0x3EBDBCu,0x811D3Au,0x3EEBCBu},
        {0x4040F3u,0x8221E7u,0x3DC95Fu,0x8221E7u,0x3E0A51u},
        {0x40484Cu,0x83AE61u,0x3CB8A1u,0x83AE61u,0x3D00EDu},
        {0x4158F9u,0x8B64E7u,0x369259u,0x8B64E7u,0x37EB53u},
        {0x41BE95u,0x9DCCD9u,0x2BBF35u,0x9DCCD9u,0x2D7DCAu},
        {0x424419u,0xD2922Au,0x189793u,0xD2922Au,0x1ADBACu},
        {0x43CAD0u,0xFDB6D9u,0x0BA51Au,0x02286Cu,0x0AFE58u}
    };

    /* (64) 프렌치 호른 — French horn: mellow 150~500Hz, round 1kHz, dark top */
    static const BiquadCoef EQ8_FRENCH_HORN[8] = {
        {0x400DFBu,0x80E545u,0x3F0EC0u,0x80E510u,0x3F1C87u},
        {0x403D77u,0x81351Cu,0x3E988Eu,0x81351Cu,0x3ED605u},
        {0x406540u,0x82A405u,0x3D3B96u,0x82A405u,0x3DA0D7u},
        {0x4077E4u,0x84AE78u,0x3BEA0Eu,0x84AE78u,0x3C61F2u},
        {0x409690u,0x8DAC4Fu,0x35C595u,0x8DAC4Fu,0x365C25u},
        {0x3EBDE4u,0xA53054u,0x2A1E36u,0xA53054u,0x28DC1Au},
        {0x3BB227u,0xD4ED4Du,0x1A733Eu,0xD4ED4Du,0x162565u},
        {0x35D96Bu,0x05713Fu,0x09561Bu,0xF98820u,0x0B18A5u}
    };

    /* (65) 튜바 — Tuba: massive 60~150Hz body, warm 300Hz, minimal highs */
    static const BiquadCoef EQ8_TUBA[8] = {
        {0x401182u,0x808752u,0x3F680Du,0x808728u,0x3F7966u},
        {0x402956u,0x809024u,0x3F4951u,0x809024u,0x3F72A7u},
        {0x403D76u,0x813B59u,0x3E9894u,0x813B59u,0x3ED609u},
        {0x403696u,0x82A660u,0x3D8645u,0x82A660u,0x3DBCDAu},
        {0x40238Au,0x86343Bu,0x3B2CE4u,0x86343Bu,0x3B506Eu},
        {0x3E009Fu,0x9BB08Fu,0x2E929Bu,0x9BB08Fu,0x2C933Au},
        {0x38222Fu,0xCC07F6u,0x1D3C17u,0xCC07F6u,0x155E46u},
        {0x2FFE44u,0x08121Au,0x087977u,0xF53CCFu,0x0B4D07u}
    };

    /* (66) 비올라 — Viola: warm 200~500Hz body between violin/cello, dark timbre */
    static const BiquadCoef EQ8_VIOLA[8] = {
        {0x3F9505u,0x80D5F6u,0x3F9505u,0x80D6DAu,0x3F2AEFu},
        {0x400C47u,0x80CF7Eu,0x3F2A83u,0x80CF7Eu,0x3F36CAu},
        {0x404E76u,0x8198CAu,0x3E3528u,0x8198CAu,0x3E839Eu},
        {0x4054C3u,0x83156Fu,0x3D1C86u,0x83156Fu,0x3D7148u},
        {0x412289u,0x8A2779u,0x380F3Cu,0x8A2779u,0x3931C5u},
        {0x41BE95u,0x9DCCD9u,0x2BBF35u,0x9DCCD9u,0x2D7DCAu},
        {0x424419u,0xD2922Au,0x189793u,0xD2922Au,0x1ADBACu},
        {0x437E55u,0x0817FFu,0x0BC070u,0x0BF66Bu,0x0B605Au}
    };

    /* (67) 더블 베이스 — Double bass: deep 60~150Hz, woody 300Hz body, dark timbre */
    static const BiquadCoef EQ8_DOUBLE_BASS[8] = {
        {0x400D1Eu,0x808B3Au,0x3F687Du,0x808B1Bu,0x3F757Cu},
        {0x402417u,0x809440u,0x3F4A74u,0x809440u,0x3F6E8Bu},
        {0x40499Du,0x817DDEu,0x3E5182u,0x817DDEu,0x3E9B1Fu},
        {0x403F79u,0x832815u,0x3D1F16u,0x832815u,0x3D5E8Fu},
        {0x4073A2u,0x89B88Eu,0x382505u,0x89B88Eu,0x3898A7u},
        {0x3EFE8Cu,0x9AD4B2u,0x2E82A8u,0x9AD4B2u,0x2D8135u},
        {0x3BF223u,0xCA13FBu,0x1CA172u,0xCA13FBu,0x189396u},
        {0x35D96Bu,0x05713Fu,0x09561Bu,0xF98820u,0x0B18A5u}
    };

    /* (68) 하프 — Harp: 60Hz resonance, pluck attack 1~4kHz, sparkle hi */
    static const BiquadCoef EQ8_HARP[8] = {
        {0x400A7Cu,0x80ABEAu,0x3F4ABAu,0x80ABCDu,0x3F5519u},
        {0x400B16u,0x8079A2u,0x3F7F4Fu,0x8079A2u,0x3F8A65u},
        {0x402064u,0x823F1Bu,0x3DCCAFu,0x823F1Bu,0x3DED13u},
        {0x4059C2u,0x84C880u,0x3BEDEFu,0x84C880u,0x3C47B0u},
        {0x41B110u,0x95D0C0u,0x313DE0u,0x95D0C0u,0x32EEF0u},
        {0x437248u,0xB8506Bu,0x21EEC6u,0xB8506Bu,0x25610Eu},
        {0x451BB4u,0x000000u,0x136F4Eu,0x000000u,0x188B01u},
        {0x4819DFu,0x2615C9u,0x0FF55Au,0x2D61F9u,0x10C309u}
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [69+]  § C-2  기타 패밀리
     * ═══════════════════════════════════════════════════════════════ */

     /* (69) 나일론 기타 — Classical guitar: warm 200Hz body, pluck 1~3kHz, no harsh hi */
    static const BiquadCoef EQ8_NYLON_GUITAR[8] = {
        {0x3FA459u,0x80B74Eu,0x3FA459u,0x80B7CEu,0x3F4933u},
        {0x4028FBu,0x8100E3u,0x3EE147u,0x8100E3u,0x3F0A42u},
        {0x403C8Du,0x82C737u,0x3D4104u,0x82C737u,0x3D7D91u},
        {0x408EE3u,0x85D585u,0x3B2170u,0x85D585u,0x3BB052u},
        {0x414385u,0x96218Au,0x3153F9u,0x96218Au,0x32977Eu},
        {0x40BFF4u,0xAE878Eu,0x25F106u,0xAE878Eu,0x26B0FAu},
        {0x400000u,0xE9A90Eu,0x16504Eu,0xE9A90Eu,0x16504Eu},
        {0x3CAFF5u,0x0B57E8u,0x0AC99Bu,0x07ACC0u,0x0B24B8u}
    };

    /* (70) 일렉 기타 클린 — Electric clean: low cut, glassy 2~4kHz, sparkle top */
    static const BiquadCoef EQ8_GUITAR_CLEAN[8] = {
        {0x3F9505u,0x80D5F6u,0x3F9505u,0x80D6DAu,0x3F2AEFu},
        {0x3FF62Cu,0x80B8C0u,0x3F5519u,0x80B8C0u,0x3F4B45u},
        {0x3FF3D2u,0x81CC68u,0x3E58B4u,0x81CC68u,0x3E4C86u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x417BDDu,0x8CF4D2u,0x359E3Eu,0x8CF4D2u,0x371A1Bu},
        {0x433F43u,0x9EC82Au,0x2D02BCu,0x9EC82Au,0x3041FFu},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* (71) 일렉 기타 크런치 — Crunch guitar: tight low, aggressive 2~5kHz, bite */
    static const BiquadCoef EQ8_GUITAR_CRUNCH[8] = {
        {0x3F8905u,0x80EDF6u,0x3F8905u,0x80EF5Bu,0x3F136Eu},
        {0x3FEC52u,0x80C365u,0x3F544Eu,0x80C365u,0x3F40A0u},
        {0x3FF3D2u,0x81CC68u,0x3E58B4u,0x81CC68u,0x3E4C86u},
        {0x40484Cu,0x83AE61u,0x3CB8A1u,0x83AE61u,0x3D00EDu},
        {0x41C96Eu,0x8CBA9Fu,0x358CEEu,0x8CBA9Fu,0x37565Cu},
        {0x4484D8u,0xA8E665u,0x2944ABu,0xA8E665u,0x2DC984u},
        {0x43B428u,0xE90AB7u,0x14FFEDu,0xE90AB7u,0x18B416u},
        {0x44DA06u,0x11C556u,0x0CA027u,0x16D2ABu,0x0C6CD9u}
    };

    /* (72) 일렉 기타 리드 — Lead guitar: presence peak 2~4kHz, sustain, singing tone */
    static const BiquadCoef EQ8_GUITAR_LEAD[8] = {
        {0x3F9505u,0x80D5F6u,0x3F9505u,0x80D6DAu,0x3F2AEFu},
        {0x3FF62Cu,0x80B8C0u,0x3F5519u,0x80B8C0u,0x3F4B45u},
        {0x402064u,0x823F1Bu,0x3DCCAFu,0x823F1Bu,0x3DED13u},
        {0x406C44u,0x842197u,0x3C4F7Bu,0x842197u,0x3CBBBFu},
        {0x42EFA0u,0x904CB9u,0x330640u,0x904CB9u,0x35F5E0u},
        {0x43D81Cu,0xACBD3Cu,0x251A90u,0xACBD3Cu,0x28F2ACu},
        {0x43B428u,0xE90AB7u,0x14FFEDu,0xE90AB7u,0x18B416u},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [73+]  § C-3  월드 / 민속 현악기
     * ═══════════════════════════════════════════════════════════════ */

     /* (73) 시타르 — Sitar: jiwari buzz 1~2kHz, sympathetic string shimmer 3~8kHz */
    static const BiquadCoef EQ8_SITAR[8] = {
        {0x3F9505u,0x80D5F6u,0x3F9505u,0x80D6DAu,0x3F2AEFu},
        {0x401E98u,0x814FB5u,0x3E9CD1u,0x814FB5u,0x3EBB69u},
        {0x4050DBu,0x82B560u,0x3D3E97u,0x82B560u,0x3D8F72u},
        {0x414C6Cu,0x86BEB5u,0x3977CEu,0x86BEB5u,0x3AC43Au},
        {0x422EF4u,0x90D101u,0x333B3Au,0x90D101u,0x356A2Eu},
        {0x43D81Cu,0xACBD3Cu,0x251A90u,0xACBD3Cu,0x28F2ACu},
        {0x44D531u,0xDD4D48u,0x15D679u,0xDD4D48u,0x1AABA9u},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* (74) 산투르 — Santur/hammered dulcimer: metallic 800Hz~4kHz, bright sustain */
    static const BiquadCoef EQ8_SANTUR[8] = {
        {0x3F7A5Bu,0x810B4Bu,0x3F7A5Bu,0x810CAFu,0x3EF61Au},
        {0x401055u,0x8116DCu,0x3EE3F2u,0x8116DCu,0x3EF447u},
        {0x403C8Du,0x82C737u,0x3D4104u,0x82C737u,0x3D7D91u},
        {0x40962Bu,0x849523u,0x3BE554u,0x849523u,0x3C7B7Fu},
        {0x422EF4u,0x90D101u,0x333B3Au,0x90D101u,0x356A2Eu},
        {0x430E33u,0xAD2CBEu,0x2557EDu,0xAD2CBEu,0x286620u},
        {0x43B428u,0xE90AB7u,0x14FFEDu,0xE90AB7u,0x18B416u},
        {0x44DA06u,0x11C556u,0x0CA027u,0x16D2ABu,0x0C6CD9u}
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [75+]  § D  전자 / 합성음
     * ═══════════════════════════════════════════════════════════════ */

     /* (75) 신스 패드 — Synth pad: smooth low-mid, soft attack, no harsh transients */
    static const BiquadCoef EQ8_SYNTH_PAD[8] = {
        {0x400A7Cu,0x80ABEAu,0x3F4ABAu,0x80ABCDu,0x3F5519u},
        {0x401EB5u,0x80F374u,0x3EF41Cu,0x80F374u,0x3F12D1u},
        {0x4030A5u,0x82304Du,0x3DCB42u,0x82304Du,0x3DFBE6u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x3FC6C7u,0x8C14BEu,0x383B50u,0x8C14BEu,0x380217u},
        {0x3E7071u,0xA23176u,0x2DE129u,0xA23176u,0x2C519Au},
        {0x3DCFBEu,0xD41FDAu,0x19F08Eu,0xD41FDAu,0x17C04Bu},
        {0x3A0EFCu,0x15A5ADu,0x0B64F2u,0x0F7548u,0x0BA453u}
    };

    /* (76) 신스 리드 — Synth lead: tight low, aggressive mid, bright presence */
    static const BiquadCoef EQ8_SYNTH_LEAD[8] = {
        {0x3F9505u,0x80D5F6u,0x3F9505u,0x80D6DAu,0x3F2AEFu},
        {0x3FF62Cu,0x80B8C0u,0x3F5519u,0x80B8C0u,0x3F4B45u},
        {0x402064u,0x823F1Bu,0x3DCCAFu,0x823F1Bu,0x3DED13u},
        {0x4077E4u,0x84AE78u,0x3BEA0Eu,0x84AE78u,0x3C61F2u},
        {0x42FFCCu,0x94E93Eu,0x30E9BAu,0x94E93Eu,0x33E985u},
        {0x4456C7u,0xB7E37Du,0x21A455u,0xB7E37Du,0x25FB1Bu},
        {0x451BB4u,0x000000u,0x136F4Eu,0x000000u,0x188B01u},
        {0x4819DFu,0x2615C9u,0x0FF55Au,0x2D61F9u,0x10C309u}
    };

    /* (77) 신스 베이스 — Synth bass: sub punch, tight 100~200Hz, no mid flub */
    static const BiquadCoef EQ8_SYNTH_BASS[8] = {
        {0x401182u,0x808752u,0x3F680Du,0x808728u,0x3F7966u},
        {0x402956u,0x809024u,0x3F4951u,0x809024u,0x3F72A7u},
        {0x4020BCu,0x810801u,0x3EE267u,0x810801u,0x3F0323u},
        {0x3F9FF0u,0x840846u,0x3CB9EDu,0x840846u,0x3C59DCu},
        {0x3F1AD9u,0x8AFF23u,0x3830D3u,0x8AFF23u,0x374BACu},
        {0x3D05ABu,0x9C956Au,0x2E95D9u,0x9C956Au,0x2B9B84u},
        {0x38222Fu,0xCC07F6u,0x1D3C17u,0xCC07F6u,0x155E46u},
        {0x2FFE44u,0x08121Au,0x087977u,0xF53CCFu,0x0B4D07u}
    };

    /* (78) 일렉 피아노 — Rhodes/Wurlitzer: warm 200Hz, tine attack 1.5~3kHz, no harsh top */
    static const BiquadCoef EQ8_ELECTRIC_PIANO[8] = {
        {0x3FA459u,0x80B74Eu,0x3FA459u,0x80B7CEu,0x3F4933u},
        {0x4028FBu,0x8100E3u,0x3EE147u,0x8100E3u,0x3F0A42u},
        {0x403C8Du,0x82C737u,0x3D4104u,0x82C737u,0x3D7D91u},
        {0x403BBFu,0x84E33Fu,0x3BF0F7u,0x84E33Fu,0x3C2CB6u},
        {0x417BDDu,0x8CF4D2u,0x359E3Eu,0x8CF4D2u,0x371A1Bu},
        {0x410FCAu,0xA0381Fu,0x2D8954u,0xA0381Fu,0x2E991Eu},
        {0x3EE577u,0xD3BABEu,0x19A50Cu,0xD3BABEu,0x188A83u},
        {0x3C6B7Eu,0x020985u,0x0A60E9u,0xFDD794u,0x0AFE58u}
    };

    /* (79) 풀 오르간 — Full organ: strong 60~300Hz fundamental, even upper partials */
    static const BiquadCoef EQ8_ORGAN_FULL[8] = {
        {0x400FBEu,0x80A719u,0x3F4A5Bu,0x80A6EDu,0x3F59ECu},
        {0x4013B4u,0x809C39u,0x3F5419u,0x809C39u,0x3F67CDu},
        {0x4030E9u,0x8192C1u,0x3E554Fu,0x8192C1u,0x3E8638u},
        {0x40484Cu,0x83AE61u,0x3CB8A1u,0x83AE61u,0x3D00EDu},
        {0x40E27Cu,0x8D6DA3u,0x35BA8Cu,0x8D6DA3u,0x369D07u},
        {0x414892u,0xA35E1Du,0x29ADDFu,0xA35E1Du,0x2AF671u},
        {0x424419u,0xD2922Au,0x189793u,0xD2922Au,0x1ADBACu},
        {0x43CAD0u,0xFDB6D9u,0x0BA51Au,0x02286Cu,0x0AFE58u}
    };

    /* (80) 드로우바 오르간 — Hammond B3 drawbar: 800Hz~2kHz Leslie rotary, click attack */
    static const BiquadCoef EQ8_ORGAN_DRAWBAR[8] = {
        {0x3FA459u,0x80B74Eu,0x3FA459u,0x80B7CEu,0x3F4933u},
        {0x4009D5u,0x80A532u,0x3F54FFu,0x80A532u,0x3F5ED4u},
        {0x4024A2u,0x819DA5u,0x3E56AFu,0x819DA5u,0x3E7B51u},
        {0x40916Bu,0x8370A1u,0x3CAD9Au,0x8370A1u,0x3D3F04u},
        {0x41E648u,0x8AFB1Cu,0x3671D7u,0x8AFB1Cu,0x38581Fu},
        {0x41BE95u,0x9DCCD9u,0x2BBF35u,0x9DCCD9u,0x2D7DCAu},
        {0x424419u,0xD2922Au,0x189793u,0xD2922Au,0x1ADBACu},
        {0x43CAD0u,0xFDB6D9u,0x0BA51Au,0x02286Cu,0x0AFE58u}
    };

    /* ═══════════════════════════════════════════════════════════════
     *  [81+]  § E  음질 개선 / 보정
     * ═══════════════════════════════════════════════════════════════ */

     /* (81) 고역 복원 — Restore lost air/sparkle in compressed/lossy recordings */
    static const BiquadCoef EQ8_AIR_RESTORATION[8] = {
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x407317u,0x8B7AADu,0x382E81u,0x8B7AADu,0x38A197u},
        {0x4289B8u,0xAA00E6u,0x29DBB6u,0xAA00E6u,0x2C656Eu},
        {0x47DCD4u,0xE712A8u,0x1872CAu,0xE712A8u,0x204F9Eu},
        {0x4A60FEu,0x17F183u,0x1221A0u,0x17F183u,0x1C829Du},
        {0x4E12F1u,0x245511u,0x10679Du,0x310E73u,0x11C12Cu}
    };

    /* (82) 저역 정리 — Remove muddy buildup: 200~500Hz targeted cut */
    static const BiquadCoef EQ8_MUD_CLEANER[8] = {
        {0x3FD290u,0x805AE1u,0x3FD290u,0x805B01u,0x3FA540u},
        {0x3FF3BCu,0x80E59Eu,0x3F2AABu,0x80E59Eu,0x3F1E66u},
        {0x3FA6DFu,0x822ABBu,0x3E3FA8u,0x822ABBu,0x3DE687u},
        {0x3F8F20u,0x83BDE5u,0x3CEA40u,0x83BDE5u,0x3C7960u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x40B7A5u,0x91ED58u,0x338643u,0x91ED58u,0x343DE8u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u}
    };

    /* (83) 거슬림 제거 — Tame harsh 2~5kHz digititis/distortion artifacts */
    static const BiquadCoef EQ8_HARSHNESS_REDUCER[8] = {
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400F2Fu,0x824580u,0x3DF061u,0x824580u,0x3DFF8Fu},
        {0x401DD6u,0x84FEBAu,0x3BF329u,0x84FEBAu,0x3C10FFu},
        {0x3ED699u,0x8F45FFu,0x35DD68u,0x8F45FFu,0x34B401u},
        {0x3CE8E0u,0xA379E9u,0x2DED78u,0xA379E9u,0x2AD658u},
        {0x3E5EC3u,0xCEFCB6u,0x23A7D0u,0xCEFCB6u,0x220693u},
        {0x3E75C5u,0x143C1Au,0x0BE10Du,0x12A1C0u,0x0BF12Cu}
    };

    /* (84) 근접 효과 보정 — Fix proximity effect: cut bloated low, restore natural balance */
    static const BiquadCoef EQ8_PROXIMITY_FIX[8] = {
        {0x3FA540u,0x80B581u,0x3FA540u,0x80B601u,0x3F4B00u},
        {0x3FC4CEu,0x8144D0u,0x3EFA63u,0x8144D0u,0x3EBF30u},
        {0x3FA4BDu,0x82822Eu,0x3DEA4Bu,0x82822Eu,0x3D8F08u},
        {0x3FD7C8u,0x832834u,0x3D4497u,0x832834u,0x3D1C60u},
        {0x40ADDBu,0x8986C8u,0x381D8Cu,0x8986C8u,0x38CB67u},
        {0x414892u,0xA35E1Du,0x29ADDFu,0xA35E1Du,0x2AF671u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u}
    };

    /* (85) 녹음 강화 — General recording enhancement: clean low, clear mid, open top */
    static const BiquadCoef EQ8_RECORDING_ENHANCE[8] = {
        {0x3FC371u,0x80791Du,0x3FC371u,0x807957u,0x3F871Cu},
        {0x3FED97u,0x80EC28u,0x3F2A45u,0x80EC28u,0x3F17DCu},
        {0x3FE1A7u,0x8246F3u,0x3DF03Bu,0x8246F3u,0x3DD1E2u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x411460u,0x91A3B6u,0x33774Au,0x91A3B6u,0x348BAAu},
        {0x4246F1u,0xAD9E4Au,0x25900Eu,0xAD9E4Au,0x27D6FFu},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* (86) 마스터링 글루 — Gentle mastering curve: slight low/hi boost, transparent mid */
    static const BiquadCoef EQ8_MASTERING_GLUE[8] = {
        {0x40053Eu,0x80743Bu,0x3F8704u,0x807432u,0x3F8C38u},
        {0x4004EEu,0x805294u,0x3FAA49u,0x805294u,0x3FAF37u},
        {0x3FF9DEu,0x80E661u,0x3F2AE8u,0x80E661u,0x3F24C6u},
        {0x400000u,0x82E186u,0x3DCEE9u,0x82E186u,0x3DCEE9u},
        {0x405291u,0x93CD1Bu,0x34CA7Fu,0x93CD1Bu,0x351D11u},
        {0x41AC24u,0xCDB4D5u,0x22EA33u,0xCDB4D5u,0x249657u},
        {0x43CB17u,0x000000u,0x13F5B4u,0x000000u,0x17C0CBu},
        {0x454AA6u,0x26BF7Cu,0x0FC1D3u,0x2B8266u,0x104990u}
    };

    /* (87) 라우드니스 극대화 — Pre-limiter loudness: sub + hi boost, reduce mid masking */
    static const BiquadCoef EQ8_LOUDNESS_MAXIMIZER[8] = {
        {0x401182u,0x808752u,0x3F680Du,0x808728u,0x3F7966u},
        {0x401EE0u,0x80987Bu,0x3F4B71u,0x80987Bu,0x3F6A51u},
        {0x3FD278u,0x8256F3u,0x3DEF66u,0x8256F3u,0x3DC1DFu},
        {0x3FC095u,0x853B74u,0x3BB116u,0x853B74u,0x3B71AAu},
        {0x414385u,0x96218Au,0x3153F9u,0x96218Au,0x32977Eu},
        {0x4514FBu,0xCF3CBEu,0x1C7188u,0xCF3CBEu,0x218684u},
        {0x47A0FDu,0x178E70u,0x1362D5u,0x178E70u,0x1B03D3u},
        {0x4B06D2u,0x25487Bu,0x102C63u,0x2F3B60u,0x11404Eu}
    };

    /* (88) 명료도 부스트 — Cut mud+harshness, boost 2~5kHz speech/instrument clarity */
    static const BiquadCoef EQ8_CLARITY_BOOST[8] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x3FD743u,0x819742u,0x3E9C93u,0x819742u,0x3E73D6u},
        {0x3FC3A8u,0x8324C7u,0x3D436Eu,0x8324C7u,0x3D0716u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x417BDDu,0x8CF4D2u,0x359E3Eu,0x8CF4D2u,0x371A1Bu},
        {0x4224A7u,0x9F7C38u,0x2D4D70u,0x9F7C38u,0x2F7217u},
        {0x43B428u,0xE90AB7u,0x14FFEDu,0xE90AB7u,0x18B416u},
        {0x44DA06u,0x11C556u,0x0CA027u,0x16D2ABu,0x0C6CD9u}
    };

    /* (89) 따뜻함 복원 — Restore warmth in cold/digital recordings, gentle low-mid boost */
    static const BiquadCoef EQ8_WARMTH_RESTORE[8] = {
        {0x400DFBu,0x80E545u,0x3F0EC0u,0x80E510u,0x3F1C87u},
        {0x402E0Fu,0x811D3Au,0x3EBDBCu,0x811D3Au,0x3EEBCBu},
        {0x4040F3u,0x8221E7u,0x3DC95Fu,0x8221E7u,0x3E0A51u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x3FC6C7u,0x8C14BEu,0x383B50u,0x8C14BEu,0x380217u},
        {0x3E7071u,0xA23176u,0x2DE129u,0xA23176u,0x2C519Au},
        {0x3DA4FCu,0xEA1554u,0x1708FEu,0xEA1554u,0x14ADFAu},
        {0x3B7D7Fu,0x1536F6u,0x0B8CB5u,0x1084C4u,0x0BBC66u}
    };

    /* (90) 스테레오 강화 — Mono-compatible: slight hi boost + low mono-ify hint */
    static const BiquadCoef EQ8_STEREO_ENHANCE[8] = {
        {0x40029Fu,0x80B368u,0x3F4B02u,0x80B361u,0x3F4D99u},
        {0x3FFC50u,0x8087EDu,0x3F7FCAu,0x8087EDu,0x3F7C1Au},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x401029u,0x82D1F4u,0x3DCE68u,0x82D1F4u,0x3DDE91u},
        {0x40A59Bu,0x938A1Bu,0x34BFFBu,0x938A1Bu,0x356596u},
        {0x43641Au,0xCD18B7u,0x226A78u,0xCD18B7u,0x25CE92u},
        {0x47A0FDu,0x178E70u,0x1362D5u,0x178E70u,0x1B03D3u},
        {0x4819DFu,0x2615C9u,0x0FF55Au,0x2D61F9u,0x10C309u}
    };

    /* (91) 룸 보정 — Typical small room correction: 200Hz and 400Hz node cut, hi restore */
    static const BiquadCoef EQ8_ROOM_CORRECTION[8] = {
        {0x3FD290u,0x805AE1u,0x3FD290u,0x805B01u,0x3FA540u},
        {0x3FF62Cu,0x80B8C0u,0x3F5519u,0x80B8C0u,0x3F4B45u},
        {0x3FA609u,0x81F4F2u,0x3E7267u,0x81F4F2u,0x3E1870u},
        {0x3F813Eu,0x839441u,0x3D1AB4u,0x839441u,0x3C9BF2u},
        {0x401DD6u,0x84FEBAu,0x3BF329u,0x84FEBAu,0x3C10FFu},
        {0x41998Bu,0x9FD92Du,0x2D6D34u,0x9FD92Du,0x2F06C0u},
        {0x44FC8Du,0xE8D712u,0x147F11u,0xE8D712u,0x197B9Eu},
        {0x468CACu,0x110A40u,0x0CD552u,0x17DCCAu,0x0C8F74u}
    };

    /* (92) 저역 타이트닝 — Tighten flabby low: HPF, sub cut, focus 80~120Hz punch */
    static const BiquadCoef EQ8_LOW_END_TIGHTEN[8] = {
        {0x3FB457u,0x809753u,0x3FB457u,0x8097ACu,0x3F6907u},
        {0x401078u,0x80648Bu,0x3F8CC8u,0x80648Bu,0x3F9D40u},
        {0x3FC2C4u,0x81AE6Cu,0x3E99E6u,0x81AE6Cu,0x3E5CAAu},
        {0x3FC3A8u,0x8324C7u,0x3D436Eu,0x8324C7u,0x3D0716u},
        {0x40180Au,0x83DA76u,0x3CBC8Fu,0x83DA76u,0x3CD49Au},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u},
        {0x400000u,0x000000u,0x000000u,0x000000u,0x000000u}
    };

    /* (93) 보컬 에어 — Add silky air to vocals: 8~16kHz open, gentle presence */
    static const BiquadCoef EQ8_VOCAL_AIR[8] = {
        {0x3F871Cu,0x80F1C8u,0x3F871Cu,0x80F2ACu,0x3F0F1Cu},
        {0x3FED97u,0x80EC28u,0x3F2A45u,0x80EC28u,0x3F17DCu},
        {0x3FE1A7u,0x8246F3u,0x3DF03Bu,0x8246F3u,0x3DD1E2u},
        {0x403022u,0x83C41Eu,0x3CBAEFu,0x83C41Eu,0x3CEB11u},
        {0x411460u,0x91A3B6u,0x33774Au,0x91A3B6u,0x348BAAu},
        {0x46262Eu,0xCEE7D9u,0x1C0A21u,0xCEE7D9u,0x22304Fu},
        {0x4AA413u,0x000000u,0x10F926u,0x000000u,0x1B9D39u},
        {0x4B06D2u,0x25487Bu,0x102C63u,0x2F3B60u,0x11404Eu}
    };

    /* (94) 악기 밸런스 — Multi-instrument mix: cut build-up frequencies, even balance */
    static const BiquadCoef EQ8_INSTRUMENTAL_BALANCE[8] = {
        {0x3FC371u,0x80791Du,0x3FC371u,0x807957u,0x3F871Cu},
        {0x3FF62Cu,0x80B8C0u,0x3F5519u,0x80B8C0u,0x3F4B45u},
        {0x3FC344u,0x826766u,0x3DEE24u,0x826766u,0x3DB168u},
        {0x3FCFF9u,0x83D595u,0x3CBCBCu,0x83D595u,0x3C8CB5u},
        {0x4057FFu,0x87FC48u,0x3A05A3u,0x87FC48u,0x3A5DA2u},
        {0x41E4A6u,0xAA62EAu,0x2A053Bu,0xAA62EAu,0x2BE9E2u},
        {0x43B428u,0xE90AB7u,0x14FFEDu,0xE90AB7u,0x18B416u},
        {0x44DA06u,0x11C556u,0x0CA027u,0x16D2ABu,0x0C6CD9u}
    };


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 4.  프리셋 인덱스 enum
     * ─────────────────────────────────────────────────────────────── */
    typedef enum {
        EQ8_IDX_BYPASS = 0,
        EQ8_IDX_DAEGEUM = 1,
        EQ8_IDX_PIRI = 2,
        EQ8_IDX_HAEGEUM = 3,
        EQ8_IDX_GAYAGEUM = 4,
        EQ8_IDX_GEOMUNGO = 5,
        EQ8_IDX_HAEGEUM_SOLO = 6,
        EQ8_IDX_VOCAL_MALE = 7,
        EQ8_IDX_VOCAL_FEMALE = 8,
        EQ8_IDX_PIANO = 9,
        EQ8_IDX_GUITAR_ACOUSTIC = 10,
        EQ8_IDX_BASS = 11,
        EQ8_IDX_DRUMS = 12,
        EQ8_IDX_VIOLIN = 13,
        EQ8_IDX_CELLO = 14,
        EQ8_IDX_ORCH = 15,
        EQ8_IDX_GUGAK = 16,
        EQ8_IDX_GUGAK_WARM = 17,
        EQ8_IDX_GUGAK_BRIGHT = 18,
        EQ8_IDX_STUDIO = 19,
        EQ8_IDX_BROADCAST = 20,
        EQ8_IDX_WARMTH = 21,
        EQ8_IDX_PRESENCE = 22,
        EQ8_IDX_VSHAPED = 23,
        EQ8_IDX_LOUDNESS = 24,
        EQ8_IDX_SCOOPED = 25,
        EQ8_IDX_MIDBOOST = 26,
        EQ8_IDX_AMBIENT = 27,
        EQ8_IDX_AIRY = 28,
        EQ8_IDX_ELECTRONIC = 29,
        EQ8_IDX_VINTAGE = 30,
        EQ8_IDX_CRISPY = 31,
        EQ8_IDX_DEESS = 32,
        EQ8_IDX_DEEP_BASS = 33,
        EQ8_IDX_SUBBASS = 34,
        EQ8_IDX_CONCERT_HALL = 35,
        EQ8_IDX_CAVE = 36,
        EQ8_IDX_UNDERWATER = 37,
        EQ8_IDX_RADIO = 38,
        EQ8_IDX_MEGAPHONE = 39,
        EQ8_IDX_BATHROOM = 40,
        EQ8_IDX_NATURAL = 41,
        EQ8_IDX_BPF_VOICE = 42,
        EQ8_IDX_NOTCH_HUMS = 43,
        EQ8_IDX_TILT_WARM = 44,
        EQ8_IDX_TILT_BRIGHT = 45,
        EQ8_IDX_ANTI_FEEDBACK = 46,
        EQ8_IDX_TIN_WHISTLE = 47,
        EQ8_IDX_TIN_WHISTLE_WARM = 48,
        EQ8_IDX_TIN_WHISTLE_SESSION = 49,
        EQ8_IDX_FLUTE = 50,
        EQ8_IDX_RECORDER = 51,
        EQ8_IDX_OCARINA = 52,
        EQ8_IDX_SAXOPHONE_ALTO = 53,
        EQ8_IDX_SAXOPHONE_TENOR = 54,
        EQ8_IDX_SAXOPHONE_SOPRANO = 55,
        EQ8_IDX_SAXOPHONE_BARITONE = 56,
        EQ8_IDX_CLARINET = 57,
        EQ8_IDX_OBOE = 58,
        EQ8_IDX_BASSOON = 59,
        EQ8_IDX_BAGPIPE = 60,
        EQ8_IDX_HARMONICA = 61,
        EQ8_IDX_TRUMPET = 62,
        EQ8_IDX_TROMBONE = 63,
        EQ8_IDX_FRENCH_HORN = 64,
        EQ8_IDX_TUBA = 65,
        EQ8_IDX_VIOLA = 66,
        EQ8_IDX_DOUBLE_BASS = 67,
        EQ8_IDX_HARP = 68,
        EQ8_IDX_NYLON_GUITAR = 69,
        EQ8_IDX_GUITAR_CLEAN = 70,
        EQ8_IDX_GUITAR_CRUNCH = 71,
        EQ8_IDX_GUITAR_LEAD = 72,
        EQ8_IDX_SITAR = 73,
        EQ8_IDX_SANTUR = 74,
        EQ8_IDX_SYNTH_PAD = 75,
        EQ8_IDX_SYNTH_LEAD = 76,
        EQ8_IDX_SYNTH_BASS = 77,
        EQ8_IDX_ELECTRIC_PIANO = 78,
        EQ8_IDX_ORGAN_FULL = 79,
        EQ8_IDX_ORGAN_DRAWBAR = 80,
        EQ8_IDX_AIR_RESTORATION = 81,
        EQ8_IDX_MUD_CLEANER = 82,
        EQ8_IDX_HARSHNESS_REDUCER = 83,
        EQ8_IDX_PROXIMITY_FIX = 84,
        EQ8_IDX_RECORDING_ENHANCE = 85,
        EQ8_IDX_MASTERING_GLUE = 86,
        EQ8_IDX_LOUDNESS_MAXIMIZER = 87,
        EQ8_IDX_CLARITY_BOOST = 88,
        EQ8_IDX_WARMTH_RESTORE = 89,
        EQ8_IDX_STEREO_ENHANCE = 90,
        EQ8_IDX_ROOM_CORRECTION = 91,
        EQ8_IDX_LOW_END_TIGHTEN = 92,
        EQ8_IDX_VOCAL_AIR = 93,
        EQ8_IDX_INSTRUMENTAL_BALANCE = 94,
        EQ8_PRESET_COUNT = 95
    } eq8_preset_idx_t;

    static const char* const EQ8_PRESET_NAMES[95] = {
        "바이패스"                   ,  /*   0 */
        "대금"                     ,  /*   1 */
        "피리"                     ,  /*   2 */
        "해금"                     ,  /*   3 */
        "가야금"                    ,  /*   4 */
        "거문고"                    ,  /*   5 */
        "해금 솔로"                  ,  /*   6 */
        "남성 보컬"                  ,  /*   7 */
        "여성 보컬"                  ,  /*   8 */
        "피아노"                    ,  /*   9 */
        "어쿠스틱 기타"                ,  /*  10 */
        "베이스"                    ,  /*  11 */
        "드럼"                     ,  /*  12 */
        "바이올린"                   ,  /*  13 */
        "첼로"                     ,  /*  14 */
        "오케스트라"                  ,  /*  15 */
        "국악 일반"                  ,  /*  16 */
        "국악 따뜻"                  ,  /*  17 */
        "국악 밝음"                  ,  /*  18 */
        "스튜디오 모니터"               ,  /*  19 */
        "방송 EBU"                 ,  /*  20 */
        "따뜻함"                    ,  /*  21 */
        "프레즌스"                   ,  /*  22 */
        "V자형"                    ,  /*  23 */
        "라우드니스"                  ,  /*  24 */
        "스쿱드 미드"                 ,  /*  25 */
        "중역 부스트"                 ,  /*  26 */
        "앰비언트"                   ,  /*  27 */
        "에어"                     ,  /*  28 */
        "전자음악"                   ,  /*  29 */
        "빈티지 테이프"                ,  /*  30 */
        "크리스피"                   ,  /*  31 */
        "디에서"                    ,  /*  32 */
        "딥 베이스"                  ,  /*  33 */
        "서브 베이스"                 ,  /*  34 */
        "콘서트홀"                   ,  /*  35 */
        "동굴"                     ,  /*  36 */
        "수중"                     ,  /*  37 */
        "라디오"                    ,  /*  38 */
        "메가폰"                    ,  /*  39 */
        "욕실 타일"                  ,  /*  40 */
        "자연음"                    ,  /*  41 */
        "보컬 BPF"                 ,  /*  42 */
        "험 제거"                   ,  /*  43 */
        "틸트 따뜻"                  ,  /*  44 */
        "틸트 밝음"                  ,  /*  45 */
        "피드백 억제"                 ,  /*  46 */
        "틴 휘슬"                   ,  /*  47 */
        "틴 휘슬 따뜻"                ,  /*  48 */
        "틴 휘슬 세션"                ,  /*  49 */
        "플루트"                    ,  /*  50 */
        "리코더"                    ,  /*  51 */
        "오카리나"                   ,  /*  52 */
        "알토 색소폰"                 ,  /*  53 */
        "테너 색소폰"                 ,  /*  54 */
        "소프라노 색소폰"               ,  /*  55 */
        "바리톤 색소폰"                ,  /*  56 */
        "클라리넷"                   ,  /*  57 */
        "오보에"                    ,  /*  58 */
        "바순"                     ,  /*  59 */
        "백파이프"                   ,  /*  60 */
        "하모니카"                   ,  /*  61 */
        "트럼펫"                    ,  /*  62 */
        "트롬본"                    ,  /*  63 */
        "프렌치 호른"                 ,  /*  64 */
        "튜바"                     ,  /*  65 */
        "비올라"                    ,  /*  66 */
        "더블 베이스"                 ,  /*  67 */
        "하프"                     ,  /*  68 */
        "나일론 기타"                 ,  /*  69 */
        "일렉 기타 클린"               ,  /*  70 */
        "일렉 기타 크런치"              ,  /*  71 */
        "일렉 기타 리드"               ,  /*  72 */
        "시타르"                    ,  /*  73 */
        "산투르"                    ,  /*  74 */
        "신스 패드"                  ,  /*  75 */
        "신스 리드"                  ,  /*  76 */
        "신스 베이스"                 ,  /*  77 */
        "일렉 피아노"                 ,  /*  78 */
        "풀 오르간"                  ,  /*  79 */
        "드로우바 오르간"               ,  /*  80 */
        "고역 복원"                  ,  /*  81 */
        "저역 정리"                  ,  /*  82 */
        "거슬림 제거"                 ,  /*  83 */
        "근접 효과 보정"               ,  /*  84 */
        "녹음 강화"                  ,  /*  85 */
        "마스터링 글루"                ,  /*  86 */
        "라우드니스 극대화"              ,  /*  87 */
        "명료도 부스트"                ,  /*  88 */
        "따뜻함 복원"                 ,  /*  89 */
        "스테레오 강화"                ,  /*  90 */
        "룸 보정"                   ,  /*  91 */
        "저역 타이트닝"                ,  /*  92 */
        "보컬 에어"                  ,  /*  93 */
        "악기 밸런스"                   /*  94 */
    };

    static const BiquadCoef* const EQ8_PRESET_DATA[95] = {
        EQ8_BYPASS,
        EQ8_DAEGEUM,
        EQ8_PIRI,
        EQ8_HAEGEUM,
        EQ8_GAYAGEUM,
        EQ8_GEOMUNGO,
        EQ8_HAEGEUM_SOLO,
        EQ8_VOCAL_MALE,
        EQ8_VOCAL_FEMALE,
        EQ8_PIANO,
        EQ8_GUITAR_ACOUSTIC,
        EQ8_BASS,
        EQ8_DRUMS,
        EQ8_VIOLIN,
        EQ8_CELLO,
        EQ8_ORCH,
        EQ8_GUGAK,
        EQ8_GUGAK_WARM,
        EQ8_GUGAK_BRIGHT,
        EQ8_STUDIO,
        EQ8_BROADCAST,
        EQ8_WARMTH,
        EQ8_PRESENCE,
        EQ8_VSHAPED,
        EQ8_LOUDNESS,
        EQ8_SCOOPED,
        EQ8_MIDBOOST,
        EQ8_AMBIENT,
        EQ8_AIRY,
        EQ8_ELECTRONIC,
        EQ8_VINTAGE,
        EQ8_CRISPY,
        EQ8_DEESS,
        EQ8_DEEP_BASS,
        EQ8_SUBBASS,
        EQ8_CONCERT_HALL,
        EQ8_CAVE,
        EQ8_UNDERWATER,
        EQ8_RADIO,
        EQ8_MEGAPHONE,
        EQ8_BATHROOM,
        EQ8_NATURAL,
        EQ8_BPF_VOICE,
        EQ8_NOTCH_HUMS,
        EQ8_TILT_WARM,
        EQ8_TILT_BRIGHT,
        EQ8_ANTI_FEEDBACK,
        EQ8_TIN_WHISTLE,
        EQ8_TIN_WHISTLE_WARM,
        EQ8_TIN_WHISTLE_SESSION,
        EQ8_FLUTE,
        EQ8_RECORDER,
        EQ8_OCARINA,
        EQ8_SAXOPHONE_ALTO,
        EQ8_SAXOPHONE_TENOR,
        EQ8_SAXOPHONE_SOPRANO,
        EQ8_SAXOPHONE_BARITONE,
        EQ8_CLARINET,
        EQ8_OBOE,
        EQ8_BASSOON,
        EQ8_BAGPIPE,
        EQ8_HARMONICA,
        EQ8_TRUMPET,
        EQ8_TROMBONE,
        EQ8_FRENCH_HORN,
        EQ8_TUBA,
        EQ8_VIOLA,
        EQ8_DOUBLE_BASS,
        EQ8_HARP,
        EQ8_NYLON_GUITAR,
        EQ8_GUITAR_CLEAN,
        EQ8_GUITAR_CRUNCH,
        EQ8_GUITAR_LEAD,
        EQ8_SITAR,
        EQ8_SANTUR,
        EQ8_SYNTH_PAD,
        EQ8_SYNTH_LEAD,
        EQ8_SYNTH_BASS,
        EQ8_ELECTRIC_PIANO,
        EQ8_ORGAN_FULL,
        EQ8_ORGAN_DRAWBAR,
        EQ8_AIR_RESTORATION,
        EQ8_MUD_CLEANER,
        EQ8_HARSHNESS_REDUCER,
        EQ8_PROXIMITY_FIX,
        EQ8_RECORDING_ENHANCE,
        EQ8_MASTERING_GLUE,
        EQ8_LOUDNESS_MAXIMIZER,
        EQ8_CLARITY_BOOST,
        EQ8_WARMTH_RESTORE,
        EQ8_STEREO_ENHANCE,
        EQ8_ROOM_CORRECTION,
        EQ8_LOW_END_TIGHTEN,
        EQ8_VOCAL_AIR,
        EQ8_INSTRUMENTAL_BALANCE
    };


    /* ─────────────────────────────────────────────────────────────────────────────
     * § 5.  악기별 권장 8밴드 프리셋 매핑 매크로
     * ─────────────────────────────────────────────────────────────── */
#define EQ8_FOR_DAEGEUM                   EQ8_IDX_DAEGEUM
#define EQ8_FOR_PIRI                      EQ8_IDX_PIRI
#define EQ8_FOR_HAEGEUM                   EQ8_IDX_HAEGEUM
#define EQ8_FOR_HAEGEUM_SOLO              EQ8_IDX_HAEGEUM_SOLO
#define EQ8_FOR_GAYAGEUM                  EQ8_IDX_GAYAGEUM
#define EQ8_FOR_GEOMUNGO                  EQ8_IDX_GEOMUNGO
#define EQ8_FOR_TIN_WHISTLE               EQ8_IDX_TIN_WHISTLE
#define EQ8_FOR_FLUTE                     EQ8_IDX_FLUTE
#define EQ8_FOR_RECORDER                  EQ8_IDX_RECORDER
#define EQ8_FOR_OCARINA                   EQ8_IDX_OCARINA
#define EQ8_FOR_CLARINET                  EQ8_IDX_CLARINET
#define EQ8_FOR_OBOE                      EQ8_IDX_OBOE
#define EQ8_FOR_BASSOON                   EQ8_IDX_BASSOON
#define EQ8_FOR_HARMONICA                 EQ8_IDX_HARMONICA
#define EQ8_FOR_SAX_ALTO                  EQ8_IDX_SAXOPHONE_ALTO
#define EQ8_FOR_SAX_TENOR                 EQ8_IDX_SAXOPHONE_TENOR
#define EQ8_FOR_SAX_SOPRANO               EQ8_IDX_SAXOPHONE_SOPRANO
#define EQ8_FOR_SAX_BARI                  EQ8_IDX_SAXOPHONE_BARITONE
#define EQ8_FOR_TRUMPET                   EQ8_IDX_TRUMPET
#define EQ8_FOR_TROMBONE                  EQ8_IDX_TROMBONE
#define EQ8_FOR_FRENCH_HORN               EQ8_IDX_FRENCH_HORN
#define EQ8_FOR_TUBA                      EQ8_IDX_TUBA
#define EQ8_FOR_VIOLIN                    EQ8_IDX_VIOLIN
#define EQ8_FOR_VIOLA                     EQ8_IDX_VIOLA
#define EQ8_FOR_CELLO                     EQ8_IDX_CELLO
#define EQ8_FOR_DOUBLE_BASS               EQ8_IDX_DOUBLE_BASS
#define EQ8_FOR_HARP                      EQ8_IDX_HARP
#define EQ8_FOR_PIANO                     EQ8_IDX_PIANO
#define EQ8_FOR_GTR_ACOUSTIC              EQ8_IDX_GUITAR_ACOUSTIC
#define EQ8_FOR_GTR_NYLON                 EQ8_IDX_NYLON_GUITAR
#define EQ8_FOR_GTR_CLEAN                 EQ8_IDX_GUITAR_CLEAN
#define EQ8_FOR_GTR_CRUNCH                EQ8_IDX_GUITAR_CRUNCH
#define EQ8_FOR_GTR_LEAD                  EQ8_IDX_GUITAR_LEAD
#define EQ8_FOR_BASS                      EQ8_IDX_BASS
#define EQ8_FOR_DRUMS                     EQ8_IDX_DRUMS
#define EQ8_FOR_SITAR                     EQ8_IDX_SITAR
#define EQ8_FOR_SYNTH_PAD                 EQ8_IDX_SYNTH_PAD
#define EQ8_FOR_SYNTH_LEAD                EQ8_IDX_SYNTH_LEAD
#define EQ8_FOR_SYNTH_BASS                EQ8_IDX_SYNTH_BASS
#define EQ8_FOR_ELEC_PIANO                EQ8_IDX_ELECTRIC_PIANO
#define EQ8_FOR_ORGAN_FULL                EQ8_IDX_ORGAN_FULL
#define EQ8_FOR_VOCAL_M                   EQ8_IDX_VOCAL_MALE
#define EQ8_FOR_VOCAL_F                   EQ8_IDX_VOCAL_FEMALE
#define EQ8_FOR_STUDIO                    EQ8_IDX_STUDIO
#define EQ8_FOR_BROADCAST                 EQ8_IDX_BROADCAST
#define EQ8_FOR_BYPASS                    EQ8_IDX_BYPASS


     /* ─────────────────────────────────────────────────────────────────────────────
      * § 6.  apply_eq8_idx() 인라인 헬퍼
      * ─────────────────────────────────────────────────────────────── */
    static inline void apply_eq8_idx(volatile uint8_t* pre_base,
        volatile uint8_t* post_base,
        uint8_t idx)
    {
        if ((uint32_t)idx >= EQ8_PRESET_COUNT) idx = 0;
        apply_eq8_preset_atomic(pre_base, post_base, EQ8_PRESET_DATA[idx]);
    }


#ifdef __cplusplus
}
#endif

#endif /* EQ_8BAND_PRESETS_H */
