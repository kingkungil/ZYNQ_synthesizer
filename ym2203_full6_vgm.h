#ifndef YM2203_FULL6_VGM_PRESETS_H
#define YM2203_FULL6_VGM_PRESETS_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* =============================================================================
 * R9-CLEAN FULL6 보강
 * - 기존 VGM 실측 5종은 보존하고, CLEAN_LEAD/PAD/PLUCK/BRASS 4종 추가.
 * - SAFE_* FM 블록은 FB/TL/SSGEG를 보수화하여 발진/거친 소리 가능성을 줄임.
 * - full6_vgm_note_on()의 PSG noise hard 값을 6→8로 완화.
 * ============================================================================= */

 /* =============================================================================
  * ym2203_full6_vgm_presets.h  (UNIFIED: 실측추출 + 발진안전화 + 빌드가드)
  *
  * 이 헤더는 팀원 요청 4가지를 모두 충족하도록 통합한 최종본입니다.
  *   (1) VGM 실측 기반   : exp 폴더의 .vgm 10곡 37,290개 key-on에서 FM 음색을 실제 추출
  *                         (단순 참고가 아니라 레지스터 캡처 → 디코드 → 정제)
  *   (2) FM3 + PSG3 전부 : full_inst_t(fm[3]+ssg_p[3])로 6채널 동시 사용
  *                         PSG noise period는 VGM 실측값(3,5,4 주류) 반영
  *   (3) 발진/거친음 방지 : 실측 음색에 안전화 적용
  *                         (FB>=6 -> 3, modulator TL<=2 -> 8, carrier TL 10~24,
  *                          AR=0 -> 28, RR=0 -> 9). 발진위험 점수 평균 0.17/최대 1.
  *   (4) 빌드/통합 편의  : _GNU_SOURCE + GCC diagnostic 가드, note_on/off/all_off 헬퍼
  *
  * OP 슬롯 순서는 기존 YM_OP_OFF={0,8,4,12}에 맞춰 보정 완료.
  * 기존 ym2203_patches.h / ym2203_multi.h를 수정하지 않고 함께 사용.
  *
  * 권장 include 순서:
  *   #include "ym2203_patches.h"
  *   #include "ym2203_multi.h"
  *   #include "ym2203_full6_vgm_presets.h"
  * ============================================================================= */

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"
#endif

#include "ym2203_multi.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* 안전한 무음 FM 패치 (체인 초기화/뮤트용) */
    static const ym2203_patch_t VGM6_FM_SILENT = {
        "VGM6_SILENT", 7, 0, 0,
        { {0,1,127,0,0,0,0,0,0,0,0}, {0,1,127,0,0,0,0,0,0,0,0},
          {0,1,127,0,0,0,0,0,0,0,0}, {0,1,127,0,0,0,0,0,0,0,0} }
    };

    /* ── VGM 실측 추출 + 발진 안전화 FM 빌딩블록 30종 ──────────────────────── */
    /* VGM6_ORGAN_1: ALG7 FB3 (VGM hold 86s, 5956 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_ORGAN_1_P = {
        "VGM6_ORGAN_1", 7, 3, 0,
        {
            {  0,  8, 24,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  0, 24,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  1, 24,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  0, 10,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_BELL_1: ALG7 FB5 (VGM hold 133s, 379 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_BELL_1_P = {
        "VGM6_BELL_1", 7, 5, 4,
        {
            {  0,  4, 24,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  3, 23,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  2, 23,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  1, 23,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_BELL_2: ALG7 FB5 (VGM hold 75s, 657 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_BELL_2_P = {
        "VGM6_BELL_2", 7, 5, 4,
        {
            {  0,  2, 24,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  1, 24,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  1, 24,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  1, 23,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_ORGAN_2: ALG7 FB4 (VGM hold 72s, 675 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_ORGAN_2_P = {
        "VGM6_ORGAN_2", 7, 4, 0,
        {
            {  0,  2, 10,  0, 31,  0,  0,  8,  0, 15, 10},
            {  0,  0, 10,  0, 31,  0, 18, 20,  2, 15,  0},
            {  0,  2, 10,  0, 31,  0, 18,  8,  0, 15,  0},
            {  0,  0, 10,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_PLUCK_1: ALG0 FB3 (VGM hold 75s, 320 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PLUCK_1_P = {
        "VGM6_PLUCK_1", 0, 3, 5,
        {
            {  0, 14, 36,  0, 31,  0,  0, 11,  0, 15,  0},
            {  7,  2, 14,  0, 31,  0,  0, 12,  0, 15,  0},
            {  3,  2, 12,  0, 31,  0,  0, 12,  0, 15,  0},
            {  0,  7, 15,  0, 31,  0,  0, 10,  0, 15,  0}
        }
    };

    /* VGM6_PLUCK_2: ALG0 FB3 (VGM hold 84s, 49 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PLUCK_2_P = {
        "VGM6_PLUCK_2", 0, 3, 5,
        {
            {  0, 10, 34,  0,  9,  0,  0,  8,  0, 15,  0},
            {  5, 13, 31,  0,  9,  0,  0,  6,  0, 15,  0},
            {  7, 10, 29,  0, 24,  0,  0,  7,  0, 15,  0},
            {  1, 13, 14,  0, 21,  0, 10,  0,  2, 15,  0}
        }
    };

    /* VGM6_ORGAN_3: ALG7 FB4 (VGM hold 61s, 301 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_ORGAN_3_P = {
        "VGM6_ORGAN_3", 7, 4, 0,
        {
            {  0, 15, 10,  0, 31,  0, 23, 31,  5, 15, 10},
            {  0,  3, 10,  0, 31,  0, 23, 22,  2, 15,  0},
            {  0,  1, 10,  0, 31,  0, 25,  0, 11, 15,  0},
            {  0,  0, 10,  0, 31,  0, 13, 31,  2, 15,  0}
        }
    };

    /* VGM6_GUITAR_1: ALG3 FB4 (VGM hold 29s, 864 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_GUITAR_1_P = {
        "VGM6_GUITAR_1", 3, 4, 5,
        {
            {  0,  0, 13,  0, 31,  0, 24, 29,  2, 15, 10},
            {  0,  0, 14,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  0, 12,  0, 31,  0, 16, 25,  1, 15,  8},
            {  0,  0, 14,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_PLUCK_3: ALG0 FB3 (VGM hold 28s, 798 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PLUCK_3_P = {
        "VGM6_PLUCK_3", 0, 3, 5,
        {
            {  0,  0,  9,  0, 31,  0, 17,  0,  2,  9,  0},
            {  0,  0,  9,  0, 31,  0, 17,  0,  2, 10,  0},
            {  0,  0,  9,  0, 31,  0, 17,  0,  2, 10,  0},
            {  0,  0, 10,  0, 31,  0, 17,  0,  2,  9,  0}
        }
    };

    /* VGM6_ORGAN_4: ALG7 FB3 (VGM hold 34s, 545 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_ORGAN_4_P = {
        "VGM6_ORGAN_4", 7, 3, 0,
        {
            {  0,  6, 24,  0, 31,  0, 20,  0,  0, 15,  0},
            {  0,  0, 24,  0, 31,  0, 16,  0,  0, 15,  0},
            {  0,  0, 22,  0, 31,  0, 22,  0,  0, 15,  0},
            {  0,  1, 10,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_ORGAN_5: ALG7 FB3 (VGM hold 34s, 512 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_ORGAN_5_P = {
        "VGM6_ORGAN_5", 7, 3, 0,
        {
            {  0,  9, 24,  0, 31,  0, 20,  0,  0, 15,  0},
            {  0,  0, 24,  0, 31,  0, 18,  0,  6, 15,  0},
            {  0,  0, 21,  0, 31,  0, 23,  0,  0, 15,  0},
            {  0,  1, 10,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_ORGAN_6: ALG7 FB1 (VGM hold 40s, 380 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_ORGAN_6_P = {
        "VGM6_ORGAN_6", 7, 1, 0,
        {
            {  0, 11, 24,  0, 31,  0, 20, 10,  3,  9,  0},
            {  3,  1, 10,  0, 31,  0, 20, 13,  4,  9,  0},
            {  3,  2, 24,  0, 31,  0, 24,  0,  2,  9,  0},
            {  7,  1, 10,  0, 31,  0,  0,  0,  0, 10,  0}
        }
    };

    /* VGM6_PAD_1: ALG5 FB4 (VGM hold 40s, 120 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PAD_1_P = {
        "VGM6_PAD_1", 5, 4, 3,
        {
            {  0,  4, 26,  0, 31,  0,  0,  5,  0,  7,  0},
            {  0,  4, 14,  0, 31,  0,  8,  0,  6, 10,  0},
            {  4,  3, 26,  0, 31,  0,  0,  5,  0,  7,  0},
            {  2,  3, 14,  0, 31,  0,  8,  0,  6, 10,  0}
        }
    };

    /* VGM6_PAD_2: ALG5 FB4 (VGM hold 40s, 120 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PAD_2_P = {
        "VGM6_PAD_2", 5, 4, 3,
        {
            {  0,  4, 26,  0, 31,  0,  0,  5,  0,  7,  0},
            {  0,  4, 29,  0, 31,  0,  8,  0,  6, 10,  0},
            {  4,  3, 26,  0, 31,  0,  0,  5,  0,  7,  0},
            {  2,  3, 24,  0, 31,  0,  8,  0,  6, 10,  0}
        }
    };

    /* VGM6_BELL_3: ALG7 FB4 (VGM hold 36s, 123 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_BELL_3_P = {
        "VGM6_BELL_3", 7, 4, 4,
        {
            {  0, 12, 24,  0, 31,  0, 13, 10,  2,  7,  0},
            {  5,  4, 24,  0, 31,  0,  0,  0,  0, 10,  0},
            {  4,  9, 24,  0, 31,  0, 13, 10,  2,  7,  0},
            {  2,  3, 24,  0, 31,  0,  0,  0,  0, 10,  0}
        }
    };

    /* VGM6_PLUCK_4: ALG0 FB3 (VGM hold 8s, 660 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PLUCK_4_P = {
        "VGM6_PLUCK_4", 0, 3, 5,
        {
            {  0,  0, 18,  0, 31,  0, 17,  0,  2,  9,  0},
            {  0,  0, 18,  0, 31,  0, 17,  0,  2, 10,  0},
            {  0,  0, 18,  0, 31,  0, 17,  0,  2, 10,  0},
            {  0,  0, 18,  0, 31,  0, 17,  0,  2,  9,  0}
        }
    };

    /* VGM6_PAD_3: ALG5 FB4 (VGM hold 37s, 20 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PAD_3_P = {
        "VGM6_PAD_3", 5, 4, 3,
        {
            {  3,  6, 24,  0, 22,  0,  7,  0,  6,  6,  0},
            {  7,  6, 10,  0, 16,  0,  0,  6,  0,  7,  0},
            {  3,  5, 26,  0, 21,  0,  7,  0,  6,  6,  0},
            {  7,  5, 10,  0, 31,  0,  0,  6,  0,  7,  0}
        }
    };

    /* VGM6_PLUCK_5: ALG0 FB3 (VGM hold 30s, 119 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PLUCK_5_P = {
        "VGM6_PLUCK_5", 0, 3, 5,
        {
            {  0,  1,  8,  0, 31,  0, 23, 25,  8, 15,  8},
            {  0,  1,  8,  0, 31,  0, 22, 28,  6, 15,  8},
            {  0,  1,  8,  0, 31,  0, 23, 29,  8, 15,  8},
            {  0,  1, 10,  0, 31,  0, 23, 29,  6, 15,  8}
        }
    };

    /* VGM6_PLUCK_6: ALG0 FB4 (VGM hold 33s, 32 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PLUCK_6_P = {
        "VGM6_PLUCK_6", 0, 4, 5,
        {
            {  0,  5, 44,  0, 31,  1,  0,  0,  0,  9,  0},
            {  0,  5, 11,  0, 19,  0,  0,  0,  0, 10,  0},
            {  0,  4, 43,  0, 31,  1,  0,  0,  0,  9,  0},
            {  0,  4, 11,  0, 19,  0,  0,  0,  0, 10,  0}
        }
    };

    /* VGM6_LEAD_1: ALG0 FB5 (VGM hold 26s, 122 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_LEAD_1_P = {
        "VGM6_LEAD_1", 0, 5, 4,
        {
            {  0,  3, 34,  0, 31,  0, 19,  0, 11, 15,  0},
            {  0,  1,  9,  0, 31,  0, 12,  0,  3, 15,  0},
            {  0,  3, 12,  0, 31,  0, 12,  0,  3, 15,  0},
            {  0,  2, 11,  0, 31,  0, 12,  0,  3, 15,  0}
        }
    };

    /* VGM6_GUITAR_2: ALG3 FB4 (VGM hold 13s, 324 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_GUITAR_2_P = {
        "VGM6_GUITAR_2", 3, 4, 5,
        {
            {  0,  0, 13,  0, 31,  0, 24, 28,  2, 15, 10},
            {  0,  0, 14,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  0, 12,  0, 31,  0, 16, 23,  1, 15,  8},
            {  0,  0, 14,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_GUITAR_3: ALG3 FB2 (VGM hold 26s, 20 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_GUITAR_3_P = {
        "VGM6_GUITAR_3", 3, 2, 5,
        {
            {  0,  2, 13,  0, 31,  0, 21,  0,  9,  8,  0},
            {  0,  2, 16,  0, 20,  0,  0,  0,  0,  9,  0},
            {  0,  4, 35,  0, 31,  0, 23,  0,  2,  8,  0},
            {  0,  1, 10,  0, 18,  0,  0,  7,  0,  9,  0}
        }
    };

    /* VGM6_BELL_4: ALG7 FB5 (VGM hold 11s, 305 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_BELL_4_P = {
        "VGM6_BELL_4", 7, 5, 4,
        {
            {  0,  5, 24,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  4, 21,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  3, 21,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  2, 23,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_BELL_5: ALG7 FB3 (VGM hold 21s, 58 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_BELL_5_P = {
        "VGM6_BELL_5", 7, 3, 4,
        {
            {  7,  8, 24,  0, 31,  0,  0,  0,  0,  7,  0},
            {  5,  2, 24,  0, 31,  0,  0,  0,  0,  3,  0},
            {  2,  1, 24,  0, 31,  0,  0,  0,  0,  7,  0},
            {  3,  1, 24,  0, 31,  0,  0,  0,  0,  9,  0}
        }
    };

    /* VGM6_PAD_4: ALG5 FB4 (VGM hold 20s, 9 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PAD_4_P = {
        "VGM6_PAD_4", 5, 4, 3,
        {
            {  3,  4, 24,  0, 22,  0,  7,  0,  6,  6,  0},
            {  7,  4, 10,  0, 16,  0,  0,  6,  0,  7,  0},
            {  3,  3, 26,  0, 21,  0,  7,  0,  6,  6,  0},
            {  7,  3, 10,  0, 31,  0,  0,  6,  0,  7,  0}
        }
    };

    /* VGM6_GUITAR_4: ALG3 FB4 (VGM hold 7s, 214 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_GUITAR_4_P = {
        "VGM6_GUITAR_4", 3, 4, 5,
        {
            {  0,  0, 13,  0, 31,  0, 23, 28,  1, 15, 10},
            {  0,  0, 14,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  0, 12,  0, 31,  0, 17, 25,  2, 15,  8},
            {  0,  0, 14,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_GUITAR_5: ALG3 FB4 (VGM hold 7s, 190 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_GUITAR_5_P = {
        "VGM6_GUITAR_5", 3, 4, 5,
        {
            {  0,  0, 13,  0, 31,  0, 23, 29,  2, 15, 10},
            {  0,  0, 14,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  0, 12,  0, 31,  0, 14, 25,  1, 15,  8},
            {  0,  0, 14,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_PAD_5: ALG5 FB4 (VGM hold 15s, 3 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_PAD_5_P = {
        "VGM6_PAD_5", 5, 4, 3,
        {
            {  3,  4, 24,  0, 22,  0,  7,  0,  6,  6,  0},
            {  7,  4, 15,  0, 16,  0,  0,  6,  0,  7,  0},
            {  3,  3, 26,  0, 21,  0,  7,  0,  6,  6,  0},
            {  7,  3, 15,  0, 31,  0,  0,  6,  0,  7,  0}
        }
    };

    /* VGM6_BELL_6: ALG7 FB4 (VGM hold 10s, 112 hits, carrier=OP[1, 2, 3, 4]) */
    static const ym2203_patch_t VGM6_BELL_6_P = {
        "VGM6_BELL_6", 7, 4, 4,
        {
            {  0,  3, 24,  0, 22,  0, 24,  0,  5, 15,  0},
            {  0,  7, 15,  0, 31,  0,  0,  8,  0, 15,  0},
            {  0,  0, 24,  0, 31,  0,  0,  0,  0, 15,  0},
            {  0,  0, 15,  0, 31,  0,  0,  0,  0, 15,  0}
        }
    };

    /* VGM6_LEAD_2: ALG0 FB5 (VGM hold 10s, 98 hits, carrier=OP[4]) */
    static const ym2203_patch_t VGM6_LEAD_2_P = {
        "VGM6_LEAD_2", 0, 5, 4,
        {
            {  0,  7, 10,  0, 31,  0, 19,  0,  7, 15,  0},
            {  0,  4, 11,  0, 31,  0, 19, 20,  5, 15,  0},
            {  0,  3, 12,  0, 31,  0, 19, 20,  5, 15,  0},
            {  0,  5, 10,  0, 31,  0, 19, 20,  5, 15,  0}
        }
    };



    /* ── [R9-CLEAN] 청음용 안전화 FM 빌딩블록 6종 ────────────────────────────── */
    static const ym2203_patch_t VGM6_SAFE_LEAD_P = {
        "VGM6_SAFE_LEAD", 1, 2, 4,
        { {0,2,42,0,28,0,12,0,2,10,0}, {0,1,34,0,28,0,10,0,2,10,0},
          {0,1,50,0,27,0, 8,0,1,10,0}, {0,1,14,0,30,0, 7,0,0, 9,0} }
    };
    static const ym2203_patch_t VGM6_SAFE_PAD_P = {
        "VGM6_SAFE_PAD", 6, 1, 3,
        { {0,1,50,0,18,0, 7,0,3,10,0}, {1,1,23,0,20,0, 7,0,2,10,0},
          {5,1,25,0,19,0, 8,0,2,10,0}, {0,1,26,0,20,0, 7,0,2,10,0} }
    };
    static const ym2203_patch_t VGM6_SAFE_BRASS_P = {
        "VGM6_SAFE_BRASS", 4, 2, 4,
        { {0,2,38,0,23,0,10,0,2,10,0}, {0,1,17,0,25,0, 9,0,1,10,0},
          {0,2,44,0,22,0,12,0,2,10,0}, {0,1,18,0,25,0, 8,0,1,10,0} }
    };
    static const ym2203_patch_t VGM6_SAFE_ORGAN_P = {
        "VGM6_SAFE_ORGAN", 7, 0, 0,
        { {0,1,25,0,31,0,0,0,0,8,0}, {0,2,27,0,31,0,0,0,0,8,0},
          {0,4,30,0,31,0,0,0,0,8,0}, {0,1,20,0,31,0,0,0,0,8,0} }
    };
    static const ym2203_patch_t VGM6_SAFE_PLUCK_P = {
        "VGM6_SAFE_PLUCK", 0, 2, 5,
        { {0,2,46,0,31,0,16,0,5,8,0}, {0,3,38,0,31,0,14,0,5,8,0},
          {0,1,44,0,31,0,13,0,5,8,0}, {0,1,16,0,31,0,12,0,5,8,0} }
    };
    static const ym2203_patch_t VGM6_SAFE_BELL_P = {
        "VGM6_SAFE_BELL", 7, 2, 4,
        { {0,0,31,0,31,0,11,0,7,7,0}, {0,1,34,0,31,0,10,0,7,7,0},
          {0,3,39,0,31,0, 9,0,8,7,0}, {0,1,19,0,31,0, 9,0,7,7,0} }
    };

    /* ── FM3 + PSG3 동시 사용 full_inst_t 프리셋 5종 ───────────────────────── */
    /*   PSG 보강 레이어. noise 계열은 VGM 실측 period(3,5,4 주류) 기준 설계. */
    static const full_inst_t FULL6_VGM_ROUND_LEAD = {
        "FULL6_VGM_ROUND_LEAD", 3,
        { VGM6_LEAD_1_P, VGM6_PAD_1_P, VGM6_BELL_1_P },
        { 0, +4, -5 },
        { 0, 0, +12 },
        { 13, 10, 8 },
        0, 0, 0, 0,
        2, 1, 450,
        0x07,
        { SSG_SQUARE_SOFT, SSG_CHORD_LAYER, SSG_BUZZ },
        { 6, 4, 2 },
        { 0, 12, 7 }
    };

    static const full_inst_t FULL6_VGM_SOFT_BRASS = {
        "FULL6_VGM_SOFT_BRASS", 3,
        { VGM6_GUITAR_1_P, VGM6_ORGAN_1_P, VGM6_PAD_2_P },
        { 0, +3, -3 },
        { 0, 0, +7 },
        { 12, 10, 8 },
        0, 0, 0, 0,
        1, 1, 360,
        0x07,
        { SSG_SQUARE_SOFT, SSG_CHORD_LAYER, SSG_SQUARE_SOFT },
        { 5, 3, 2 },
        { 0, 7, 12 }
    };

    static const full_inst_t FULL6_VGM_WARM_PAD = {
        "FULL6_VGM_WARM_PAD", 3,
        { VGM6_PAD_1_P, VGM6_PAD_2_P, VGM6_BELL_2_P },
        { 0, +6, -6 },
        { -12, 0, +12 },
        { 9, 9, 6 },
        0, 0, 0, 0,
        4, 2, 520,
        0x07,
        { SSG_CHORD_LAYER, SSG_SQUARE_SOFT, SSG_CHORD_LAYER },
        { 3, 3, 2 },
        { -12, 0, 12 }
    };

    static const full_inst_t FULL6_VGM_CLEAN_ORGAN = {
        "FULL6_VGM_CLEAN_ORGAN", 3,
        { VGM6_ORGAN_1_P, VGM6_ORGAN_2_P, VGM6_ORGAN_3_P },
        { 0, +2, -2 },
        { 0, +12, -12 },
        { 11, 9, 8 },
        0, 0, 0, 0,
        0, 0, 0,
        0x07,
        { SSG_SQUARE_BRIGHT, SSG_SQUARE_SOFT, SSG_CHORD_LAYER },
        { 4, 3, 2 },
        { 0, 12, -12 }
    };

    static const full_inst_t FULL6_VGM_PLUCK_LAYER = {
        "FULL6_VGM_PLUCK_LAYER", 3,
        { VGM6_PLUCK_1_P, VGM6_PLUCK_2_P, VGM6_BELL_3_P },
        { 0, +5, -4 },
        { 0, +12, 0 },
        { 12, 7, 5 },
        0, 0, 0, 0,
        0, 0, 0,
        0x07,
        { SSG_ATTACK_PLUCK, SSG_ATTACK_MALLET, SSG_CHORD_LAYER },
        { 5, 4, 2 },
        { 0, 12, 7 }
    };



    static const full_inst_t FULL6_VGM_CLEAN_LEAD = {
        "FULL6_VGM_CLEAN_LEAD", 3,
        { VGM6_SAFE_LEAD_P, VGM6_SAFE_ORGAN_P, VGM6_SAFE_BELL_P },
        { 0, +4, -5 }, { 0, 0, +12 }, { 12, 8, 5 },
        0, 0, 0, 0, 1, 1, 420,
        0x07, { SSG_SQUARE_SOFT, SSG_CHORD_LAYER, SSG_SQUARE_SOFT }, { 4, 3, 1 }, { 0, 12, 7 }
    };

    static const full_inst_t FULL6_VGM_CLEAN_PAD = {
        "FULL6_VGM_CLEAN_PAD", 3,
        { VGM6_SAFE_PAD_P, VGM6_SAFE_ORGAN_P, VGM6_SAFE_BRASS_P },
        { 0, +6, -6 }, { -12, 0, +12 }, { 8, 7, 5 },
        0, 0, 0, 0, 3, 1, 500,
        0x07, { SSG_CHORD_LAYER, SSG_SQUARE_SOFT, SSG_CHORD_LAYER }, { 3, 2, 1 }, { -12, 0, 12 }
    };

    static const full_inst_t FULL6_VGM_CLEAN_PLUCK = {
        "FULL6_VGM_CLEAN_PLUCK", 3,
        { VGM6_SAFE_PLUCK_P, VGM6_SAFE_BELL_P, VGM6_SAFE_LEAD_P },
        { 0, +5, -4 }, { 0, +12, 0 }, { 11, 5, 4 },
        0, 0, 0, 0, 0, 0, 0,
        0x07, { SSG_ATTACK_PLUCK, SSG_ATTACK_MALLET, SSG_CHORD_LAYER }, { 4, 3, 1 }, { 0, 12, 7 }
    };

    static const full_inst_t FULL6_VGM_CLEAN_BRASS = {
        "FULL6_VGM_CLEAN_BRASS", 3,
        { VGM6_SAFE_BRASS_P, VGM6_SAFE_LEAD_P, VGM6_SAFE_ORGAN_P },
        { 0, +3, -3 }, { 0, 0, +7 }, { 10, 7, 5 },
        0, 0, 0, 0, 1, 1, 360,
        0x07, { SSG_SQUARE_SOFT, SSG_CHORD_LAYER, SSG_SQUARE_SOFT }, { 4, 2, 1 }, { 0, 7, 12 }
    };

    static const full_inst_t* const FULL6_VGM_CATALOG[] = {
        &FULL6_VGM_ROUND_LEAD,
        &FULL6_VGM_SOFT_BRASS,
        &FULL6_VGM_WARM_PAD,
        &FULL6_VGM_CLEAN_ORGAN,
        &FULL6_VGM_PLUCK_LAYER,
        &FULL6_VGM_CLEAN_LEAD,
        &FULL6_VGM_CLEAN_PAD,
        &FULL6_VGM_CLEAN_PLUCK,
        &FULL6_VGM_CLEAN_BRASS,
    };
#define FULL6_VGM_CATALOG_COUNT ((uint8_t)(sizeof(FULL6_VGM_CATALOG)/sizeof(FULL6_VGM_CATALOG[0])))

    /* 실측 빌딩블록 30종 풀 (단일 패치로 쓰거나 악기 재구성 시 사용) */
    static const ym2203_patch_t* const VGM6_FM_POOL[] = {
        &VGM6_ORGAN_1_P,
        &VGM6_BELL_1_P,
        &VGM6_BELL_2_P,
        &VGM6_ORGAN_2_P,
        &VGM6_PLUCK_1_P,
        &VGM6_PLUCK_2_P,
        &VGM6_ORGAN_3_P,
        &VGM6_GUITAR_1_P,
        &VGM6_PLUCK_3_P,
        &VGM6_ORGAN_4_P,
        &VGM6_ORGAN_5_P,
        &VGM6_ORGAN_6_P,
        &VGM6_PAD_1_P,
        &VGM6_PAD_2_P,
        &VGM6_BELL_3_P,
        &VGM6_PLUCK_4_P,
        &VGM6_PAD_3_P,
        &VGM6_PLUCK_5_P,
        &VGM6_PLUCK_6_P,
        &VGM6_LEAD_1_P,
        &VGM6_GUITAR_2_P,
        &VGM6_GUITAR_3_P,
        &VGM6_BELL_4_P,
        &VGM6_BELL_5_P,
        &VGM6_PAD_4_P,
        &VGM6_GUITAR_4_P,
        &VGM6_GUITAR_5_P,
        &VGM6_PAD_5_P,
        &VGM6_BELL_6_P,
        &VGM6_LEAD_2_P,
        &VGM6_SAFE_LEAD_P,
        &VGM6_SAFE_PAD_P,
        &VGM6_SAFE_BRASS_P,
        &VGM6_SAFE_ORGAN_P,
        &VGM6_SAFE_PLUCK_P,
        &VGM6_SAFE_BELL_P,
    };
#define VGM6_FM_POOL_COUNT ((uint8_t)(sizeof(VGM6_FM_POOL)/sizeof(VGM6_FM_POOL[0])))

    static inline const full_inst_t* full6_vgm_get(uint8_t idx) {
        if (idx >= FULL6_VGM_CATALOG_COUNT) idx = 0;
        return FULL6_VGM_CATALOG[idx];
    }
    static inline void full6_vgm_note_on(uint8_t idx, uint8_t note, uint8_t vel) {
        fi_note_on_v(full6_vgm_get(idx), note, vel, 24, 8);
    }
    static inline void full6_vgm_note_off(uint8_t idx) {
        fi_note_off(full6_vgm_get(idx));
    }
    static inline void full6_vgm_all_off(void) {
        ym2203_all_notes_off();
        ym2203_ssg_silence_all();
    }

#ifdef __cplusplus
}
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif /* YM2203_FULL6_VGM_PRESETS_H */