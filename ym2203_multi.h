// =============================================================================
//  ym2203_multi.h  ?  YM2203 가상 멀티OP / 화성학 통합 라이브러리  Rev.6
//
//  기반: ym2203_multi.h Rev.5 + ym2203_patches.h Rev.7 상호보완 통합
//
//  qq Rev.6 수정 내용 (patches.h Rev.7 상호보완) qqqqqqqqqqqqqqqqqqqqqqqqqqq
//
//  [COMPAT-1] 타입명 통일 ? ym2203_freq_t → ym2203_fnum_t
//      · patches.h Rev.7 기준 타입명으로 전체 교체.
//      · 구버전 헤더를 사용 중이라면 typedef ym2203_fnum_t ym2203_freq_t; 추가                                                                                                                                                                                                                                필요.
//
//  [COMPAT-2] vel_tl_scale 인식 ? ym_patch_vol(), ym_patch_vol_split()
//      · patches.h Rev.7의 ym2203_patch_t에 vel_tl_scale(0~8) 필드 추가됨.
//      · vel_tl_scale=0: velocity 무시 (오르간 등). LUT 감쇄 건너뜀.
//      · vel_tl_scale>0: LUT_attn × scale/8 적용. scale=4=표준, scale=8=최대.
//      · 이중 적용 방지: ym2203_patch_apply_vel()과 ym_patch_vol()을 혼용 금지                                                                                                                                                                                                                               .
//        multi.h API(fi_note_on 등)는 ym_patch_vol()만 사용합니다.
//
//  [COMPAT-3] SSG API 통합 ? ym2203_ssg_note_on() (patches.h Rev.7)
//      · fi_note_on(), fi_note_on_v()에서 구버전 ssg_patch_apply_ch() 제거.
//      · Rev.7 ym2203_ssg_note_on()이 mixer, noise_period, envelope,
//        attack_boost, cut_ticks를 단일 호출로 처리합니다.
//      · SSG 노이즈 프리셋 확장: SSG_NOISE_CRASH 취급 추가.
//
//  [COMPAT-4] 포르타멘토 linear pitch 업그레이드 ? portamento_t
//      · lp_src, lp_dst 필드 추가 (block×2048+fnum).
//      · pt_start(): Rev.7 ym2203_fnum_to_linear() 사전 계산.
//      · pt_tick(): Rev.7 ym2203_linear_to_fnum() 역변환 + ym_write_force().
//      · 옥타브 경계를 가로지르는 글라이드(예: E3→C5)가 연속적으로 동작합니다                                                                                                                                                                                                                               .
//
//  [COMPAT-5] 통합 틱 함수 ? vm_tick_full()
//      · vm_tick() + ym2203_tick() 동시 호출.
//      · ym2203_tick()은 FM LFO(vibrato/tremolo) + SSG cut/attack 틱 처리.
//      · RT 루프에서 vm_tick() 대신 vm_tick_full() 사용 권장.
//
//  [COMPAT-6] KD_INTERVAL_JI TT(증4도) 수정
//      · 기존 +35c → -49c (11:8 비율, 551.3c, 평균율 600c 대비 -49c).
//
//  [COMPAT-7] §5Z 상호보완 섹션 업데이트
//      · SSG_NOISE_BREATH 프리셋 부재 문제 및 patches.h 추가 방법 기술.
//      · vel_tl_scale, ym2203_fnum_to_linear() 활용 가이드 추가.
//
//  qq Rev.5 수정 내용 (이전 버전과 동일) qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq
//
//  [RT-1] PetaLinux 실시간 스케줄링 헬퍼 (§0C)
//  [JI-1] 실시간 조(Key) 판별 엔진 (§2B) ? key_detector_t
//  [VH-1] Note Hang 방지 강화 (§5G) ? vm_cc, vm_panic_off
//  [NK-1] PSG 노이즈 주파수 벨로시티 가변 제어 (§11)
//
//  qq Rev.4 수정 내용 (이전 버전과 동일) qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq
//
//  [FIX-1] Block 캐리 처리 ? ym_fnum_cents_blk() (§0B)
//  [FIX-2] 벨로시티 로그 LUT ? VEL_TO_TL_ATTN[128] (§0A)
//  [FIX-3] 농음 랜덤 워크 ? nlfo_tick_r() (§5B)
//  [FIX-4] 서스테인 Re-trigger ? vm_note_on() (§5G)
//  [FIX-5] mp_pitch_bend 데드 코드 제거 (§5H)
//
//  qq Rev.3 신규 추가 내용 (이전 버전과 동일) qqqqqqqqqqqqqqqqqqqqqqqqqq
//
//  [A] 순정률(Just Intonation) 실시간 FNUM 보정
//      · 코드 타입별 주파수 비 정수 근사 테이블 (4:5:6, 4:5:6:7 등)
//      · ym_ji_fnum_offset() ? 성부 인덱스별 cent 오프셋 반환
//      · fi_chord_on_ji() ? JI 보정 코드 발음 API
//
//  [B] MIDI 지능형 보이스 매니저 (voice_mgr_t)
//      · 최대 16노트 추적, 화성적 우선순위 기반 Voice Stealing
//      · 우선순위: 근음 > 가이드톤(3·7도) > 멜로디 상성 > 5도 > 텐션
//      · PSG 폴백: FM 채널 부족 시 단순 배음을 PSG로 자동 전환
//      · 서스테인 페달(CC#64) 로직 포함
//      · vm_note_on() / vm_note_off() / vm_cc() / vm_tick() API
//
//  [C] 대위법 엔진 (counterpoint_t)
//      · 채널 간 병행 5도/8도 실시간 감지 → 자동 전위 조정
//      · 경과음(Passing Tone) / 보조음(Neighboring Tone) 자동 삽입
//      · 계류음(Suspension): 4→3, 7→6 해결 로직
//      · cp_check_parallels() / cp_resolve_suspension() API
//
//  [D] 텐션 기반 FM 파라미터 모핑 (timbre_morph_t)
//      · 코드 불협화도 점수 계산 (dissonance_score)
//      · 긴장도에 따른 FB·캐리어 TL 실시간 변조
//      · tm_morph_apply() ? 매 틱 호출
//
//  [E] 포르타멘토 / 글리산도 엔진 (portamento_t)
//      · FNUM 선형/지수 보간 (국악 추성·퇴성·전성)
//      · pt_start() / pt_tick() ? 매 틱 FNUM 점진 업데이트
//
//  [F] 비선형 국악 농음 LFO (nonlinear_lfo_t)
//      · 비대칭 상승/하강 폭, 가속·감속 속도 개별 설정
//      · 율명별 농음 프리셋 (황/태/중/임/남)
//      · nlfo_tick() ? 매 틱 FNUM 오프셋 반환
//
//  [G] 링 버퍼 캐논 엔진 (canon_engine_t)
//      · 채널 0 연주를 캡처 → 채널 1에 N틱 지연 재생
//      · 화성 테이블 기반 자동 음정 보정 (Quantization)
//      · ce_capture() / ce_playback_tick() API
//
//  [H] MIDI 퍼포먼스 매핑
//      · Velocity → 캐리어/모듈레이터 TL 독립 비율 매핑
//      · Aftertouch → 농음 깊이 실시간 제어
//      · Pitch Bend → 순정률 오프셋 보정
//      · mod_wheel → FM Feedback 연속 변조
//      · midi_perf_apply() 단일 호출 API
//
//  [I] 화성 분석기 (chord_analyzer_t)
//      · 눌린 노트 집합 → 코드 타입/루트 자동 판별
//      · 불협화도 점수 0~100 출력
//      · ca_analyze() API
//

// ── Rev.7 CLEAN full_inst_t 보강 ───────────────────────────────────────────
//  [R7-CLEAN] FULL_CLEAN_ROUND_LEAD / WARM_PAD / MELLOW_PLUCK / SOFT_BRASS 추가.
//             기존 DUAL/TRIPLE 구조는 건드리지 않고 full_inst_t 기반만 추가하여
//             빌드 리스크를 줄이고, FM3+SSG3 전체 사용 청음 프리셋을 제공.
//
// =============================================================================

#ifndef YM2203_MULTI_H
#define YM2203_MULTI_H



#pragma once
#include "ym2203_patches.h"
#include <stdint.h>
#include <string.h>

// =============================================================================
//  §1  유틸리티: FNUM 디튠 / 반음 계산
// =============================================================================

// =============================================================================
//  §0A  벨로시티 → TL 변환 로그 LUT
//  왜 LUT인가:
//    TL 레지스터는 감쇄량(dB)이므로 이미 로그 스케일입니다.
//    선형 vel → 선형 TL 매핑은 실제로 "두 번 로그"가 되어
//    약타에서 소리가 거의 안 들리고 강타에서 갑자기 커집니다.
//    아래 LUT는 vel(0~127)을 TL 감쇄량(0~40)으로 역-로그 변환하여
//    인간 귀에 선형처럼 들리는 자연스러운 터치감을 만듭니다.
//    (감쇄량 0=최대음량, 40=매우 약함)
// =============================================================================
static const uint8_t VEL_TO_TL_ATTN[128] = {
    40,39,38,37,36,35,34,33, 32,31,30,30,29,29,28,28,
    27,27,26,26,25,25,24,24, 23,23,22,22,21,21,20,20,
    19,19,18,18,17,17,16,16, 15,15,14,14,13,13,12,12,
    11,11,10,10, 9, 9, 9, 8,  8, 8, 7, 7, 7, 6, 6, 6,
     5, 5, 5, 5, 4, 4, 4, 4,  3, 3, 3, 3, 3, 2, 2, 2,
     2, 2, 2, 1, 1, 1, 1, 1,  1, 1, 1, 1, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0
};

// =============================================================================
//  §0B  농음 랜덤 워크 ? 기계적 반복 방지
//  xorshift16 기반 초경량 PRNG (8바이트 상태, 나눗셈 없음)
//  nlfo_tick_r() 로 호출하면 매 주기마다 ±rand_range cent 안에서
//  depth와 속도에 미세 변동이 생겨 살아있는 느낌을 줍니다.
// =============================================================================
static uint16_t g_nlfo_rng = 0xACE1u;
static inline uint16_t nlfo_rand(void)
{
    g_nlfo_rng ^= (uint16_t)(g_nlfo_rng << 7u);
    g_nlfo_rng ^= (uint16_t)(g_nlfo_rng >> 9u);
    g_nlfo_rng ^= (uint16_t)(g_nlfo_rng << 8u);
    return g_nlfo_rng;
}

// =============================================================================
//  §0C  [Rev.5] PetaLinux 실시간 스케줄링 헬퍼
//
//  문제: PetaLinux(mainline Linux)는 실시간 OS가 아닙니다.
//    · CFS 스케줄러는 수 ms 단위 컨텍스트 스위칭 지연을 허용합니다.
//    · vm_tick()/seq_tick() 같은 10ms 주기 함수가 밀리면 박자 지터가
//      청취자에게 명확히 체감됩니다 (인간의 리듬 인지 한계 ? 2~3ms).
//    · 페이지 폴트(mmap 지연)도 수십 μs 추가 지연을 유발합니다.
//
//  해결 전략:
//    1) SCHED_FIFO + 높은 우선순위로 음악 스레드가 선점당하지 않도록
//    2) mlockall(MCL_CURRENT | MCL_FUTURE) 로 페이지 폴트 사전 차단
//    3) CPU 친화성(affinity) 고정으로 캐시 핫 상태 유지
//    4) 타이머: clock_nanosleep(CLOCK_MONOTONIC) 으로 절대 시간 기준
//       드리프트 없는 주기 확보 (sleep_until 패턴)
//
//  사용:
//    int main() {
//        ym_rt_setup(80, 1);   // priority=80, cpu=1
//        while (1) ym_rt_tick_loop(&g_vm, &g_seq, 10000000); // 10ms
//    }
// =============================================================================
#ifdef __linux__
#include <sched.h>
#include <sys/mman.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

/*
 * ym_rt_setup ? 호출 스레드를 SCHED_FIFO RT 스레드로 승격
 *
 * priority: 1~99 (99=최고). 음악 스레드 권장값: 70~85.
 *   · 너무 높으면(>90) 커널 워치독/IRQ 핸들러와 경쟁하여 시스템 불안정
 *   · 너무 낮으면(<60) 일반 프로세스에 선점당할 수 있음
 *   · 권장: 80 (IRQ는 보통 50~70 대역)
 *
 * cpu_affinity: 고정할 CPU 번호. -1이면 고정 안 함.
 *   · Zynq-7000 듀얼코어에서 CPU1을 전용으로 쓰면 Linux 커널 활동이
 *     주로 CPU0에서 일어나므로 음악 스레드 간섭이 크게 줄어듭니다.
 *
 * 반환: 0=성공, -1=실패 (root 권한 필요)
 */
static inline int ym_rt_setup(int priority, int cpu_affinity)
{
    int ret = 0;

    /* 1. 메모리 잠금: 페이지 폴트로 인한 지연 차단 */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) ret = -1;

    /* 2. SCHED_FIFO 승격 */
    struct sched_param sp;
    sp.sched_priority = priority;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) ret = -1;

    /* 3. CPU 친화성 고정 */
    if (cpu_affinity >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET((size_t)cpu_affinity, &cpuset);
        if (pthread_setaffinity_np(pthread_self(),
            sizeof(cpu_set_t), &cpuset) != 0) ret = -1;
    }

    return ret;
}

/*
 * ym_rt_tick_loop ? 드리프트 없는 정밀 주기 틱 루프
 *
 * 일반적인 nanosleep()은 매 호출마다 현재 시각 기준으로 잠들므로
 * 처리 시간이 누적되면 박자가 점점 늦어집니다 (드리프트).
 *
 * 이 함수는 clock_nanosleep(TIMER_ABSTIME)을 사용해 절대 시각 기준으로
 * 잠들므로 처리 시간이 길어져도 다음 틱의 절대 시각은 변하지 않습니다.
 *
 * tick_ns: 틱 주기 (나노초). 10ms = 10000000
 *
 * 사용 예:
 *   struct timespec next;
 *   clock_gettime(CLOCK_MONOTONIC, &next);
 *   while (running) {
 *       vm_tick(&g_vm);
 *       seq_tick(&g_seq);
 *       ym_rt_sleep_until(&next, 10000000);  // 다음 10ms 틱까지 대기
 *   }
 */
static inline void ym_rt_sleep_until(struct timespec* next_wake, long tick_ns)
{
    /* 다음 웨이크업 시각 계산 */
    next_wake->tv_nsec += tick_ns;
    while (next_wake->tv_nsec >= 1000000000L) {
        next_wake->tv_nsec -= 1000000000L;
        next_wake->tv_sec++;
    }
    /* 절대 시각 기반 sleep ? 처리 시간을 자동으로 보정 */
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
        next_wake, NULL) == EINTR) {
        /* EINTR: 시그널로 깨어남 → 재시도 */
    }
}

/*
 * ym_rt_measure_jitter ? 틱 지터 측정 유틸
 *
 * 실제 tick 간격과 목표 간격의 차이를 나노초 단위로 반환합니다.
 * 디버깅/튜닝 목적으로 사용. 정상 범위: < 100μs (100000 ns)
 * RT-Preempt 커널 적용 시: < 20μs
 *
 * 사용:
 *   static struct timespec prev = {0};
 *   long jitter = ym_rt_measure_jitter(&prev, 10000000);
 *   if (jitter > 1000000) fprintf(stderr, "jitter %ld us\n", jitter/1000);
 */
static inline long ym_rt_measure_jitter(struct timespec* prev_ts,
    long target_tick_ns)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (prev_ts->tv_sec == 0 && prev_ts->tv_nsec == 0) {
        *prev_ts = now;
        return 0;
    }
    long diff_ns = (long)(now.tv_sec - prev_ts->tv_sec) * 1000000000L
        + (long)(now.tv_nsec - prev_ts->tv_nsec);
    *prev_ts = now;
    return diff_ns - target_tick_ns;  /* 양수=늦음, 음수=빠름 */
}

#else  /* non-Linux stub */
static inline int  ym_rt_setup(int p, int c) { (void)p; (void)c; return 0; }
struct timespec { long tv_sec; long tv_nsec; };
static inline void ym_rt_sleep_until(struct timespec* n, long t) { (void)n; (void)t; }
static inline long ym_rt_measure_jitter(struct timespec* p, long t) { (void)p; (void)t; return 0; }
#endif /* __linux__ */


static inline uint16_t ym_fnum_cents(uint16_t base, int16_t cents)
{
    int32_t r = (int32_t)base + ((int32_t)base * (int32_t)cents) / 1731;
    if (r < 0)    r = 0;
    if (r > 2047) r = 2047;
    return (uint16_t)r;
}

/*
 * ym_fnum_cents_blk ? Block 캐리 처리 포함 FNUM+Block 계산
 *
 * 문제: 높은 옥타브(B4 등, blk=4, fnum?1945)에서 농음/포르타멘토로
 *       +50cent 이상 올라가면 fnum이 2047을 초과해 음정이 꺾입니다.
 *
 * 해결: fnum > 2047이면 블록을 +1하고 fnum을 절반으로 낮춥니다.
 *       반대로 fnum < 256이면 블록을 -1하고 fnum을 두 배로 올립니다.
 *       (YM2203 datasheet §5: FNUM의 유효 범위는 블록당 256~2047)
 *
 * 출력: *out_fnum, *out_blk 에 보정된 값을 씁니다.
 */
static inline void ym_fnum_cents_blk(uint16_t base_fnum, uint8_t base_blk,
    int16_t cents,
    uint16_t* out_fnum, uint8_t* out_blk)
{
    int32_t fnum = (int32_t)base_fnum
        + ((int32_t)base_fnum * (int32_t)cents) / 1731;
    int32_t blk = (int32_t)base_blk;

    /* 상향 캐리: fnum > 2047 → blk+1, fnum/2 (최대 blk=7) */
    while (fnum > 2047 && blk < 7) { fnum >>= 1; blk++; }
    /* 하향 캐리: fnum < 256 → blk-1, fnum*2 (최소 blk=0) */
    while (fnum < 256 && blk > 0) { fnum <<= 1; blk--; }

    if (fnum < 0)    fnum = 0;
    if (fnum > 2047) fnum = 2047;
    if (blk < 0)   blk = 0;
    if (blk > 7)   blk = 7;

    *out_fnum = (uint16_t)fnum;
    *out_blk = (uint8_t)blk;
}

/* 반음 오프셋 FNUM */
static inline uint16_t ym_fnum_semi(uint16_t base, int8_t semi)
{
    return ym_fnum_cents(base, (int16_t)semi * 100);
}

/* MIDI 노트 → FNUM+MSB 추출 */
static inline void ym_get_fnum(uint8_t note, uint16_t* fnum, uint8_t* msb)
{
    if (note > 127u) note = 127u;
    const ym2203_fnum_t* f = &YM2203_FREQ_LUT[note];
    *fnum = (uint16_t)(f->lsb | ((uint16_t)(f->msb & 0x07u) << 8));
    *msb = f->msb;
}

/* 채널에 디튠+반음오프셋 적용하여 음정 세팅 (Block 캐리 처리 포함) */
static inline void ym_set_note_ex(uint8_t ch, uint8_t midi_note,
    int16_t detune_cent, int8_t note_off)
{
    int16_t n = (int16_t)midi_note + (int16_t)note_off;
    if (n < 0)   n = 0;
    if (n > 127) n = 127;
    const ym2203_fnum_t* f = &YM2203_FREQ_LUT[(uint8_t)n];
    uint16_t base_fnum = (uint16_t)(f->lsb | ((uint16_t)(f->msb & 0x07u) << 8));
    uint8_t  base_blk = (uint8_t)((f->msb >> 3u) & 0x07u);
    uint16_t fnum; uint8_t blk;
    ym_fnum_cents_blk(base_fnum, base_blk, detune_cent, &fnum, &blk);
    ym_write((uint8_t)(0xA4u + ch), (uint8_t)((blk << 3u) | ((fnum >> 8u) & 0x07u)));
    ym_write((uint8_t)(0xA0u + ch), (uint8_t)(fnum & 0xFFu));
}

/* FNUM을 직접 채널에 기록 (포르타멘토용) */
static inline void ym_set_fnum_raw(uint8_t ch, uint16_t fnum, uint8_t blk)
{
    ym_write((uint8_t)(0xA4u + ch), (uint8_t)((blk << 3u) | ((fnum >> 8u) & 0x07u)));
    ym_write((uint8_t)(0xA0u + ch), (uint8_t)(fnum & 0xFFu));
}

/* 볼륨 스케일(0~16) + 벨로시티 적용 패치
 *
 * [Rev.4] 선형 → 로그 LUT 교체
 * [Rev.6] Rev.7 patches.h 호환: vel_tl_scale==0인 패치는 velocity 무시.
 *   patches.h Rev.7은 패치 자체에 vel_tl_scale(0~8)을 내장합니다.
 *   vel_tl_scale=0: 오르간처럼 velocity가 없는 악기 → TL 감쇄 건너뜀.
 *   vel_tl_scale>0: LUT 감쇄량을 scale에 비례하여 적용.
 *   수식: attn = VEL_TO_TL_ATTN[vel] * vel_tl_scale / 8
 *   이렇게 하면 patches.h의 의도(scale=4=표준, scale=8=최대감도)와 일치합니다.
 *
 * VEL_TO_TL_ATTN[127]=0 (최대 음량), VEL_TO_TL_ATTN[0]=40 (매우 약한 소리)
 */
static inline void ym_patch_vol(const ym2203_patch_t* p,
    uint8_t ch, uint8_t vel, uint8_t vol)
{
    ym2203_patch_apply(p, ch);
    uint8_t cm = YM_CARRIER_MASK[p->ALG & 7u];
    uint8_t vel_safe = (vel > 127u) ? 127u : vel;
    uint8_t lut_attn = VEL_TO_TL_ATTN[vel_safe];
    for (int op = 0; op < 4; op++) {
        uint8_t off = (uint8_t)(YM_OP_OFF[op] + ch);
        int32_t tl = (int32_t)p->ops[op].TL;
        if (((cm >> op) & 1u) && p->vel_tl_scale > 0u) {
            /* vel_tl_scale 비례 적용 (0=무시, 4=표준, 8=최대) */
            tl += (int32_t)lut_attn * (int32_t)p->vel_tl_scale / 8;
        }
        if (vol < 16u) tl += (int32_t)(16u - vol) * 5;
        if (tl < 0)   tl = 0;
        if (tl > 127) tl = 127;
        ym_write((uint8_t)(0x40u + off), (uint8_t)(tl & 0x7Fu));
    }
}

/* [Rev.6] Velocity → 캐리어/모듈레이터 TL 분리 매핑 (LUT 기반, vel_tl_scale 연                                                                                                                                                                                                                               동)
 * car_ratio: 캐리어 감쇄 비율 0~8
 * mod_ratio: 모듈레이터 감쇄 비율 0~8
 *
 * [Rev.7 호환] vel_tl_scale=0이면 ratio 무관하게 velocity 무시.
 * 수식: tl += VEL_TO_TL_ATTN[vel] * min(ratio, vel_tl_scale) / 8
 */
static inline void ym_patch_vol_split(const ym2203_patch_t* p,
    uint8_t ch, uint8_t vel, uint8_t vol,
    uint8_t car_ratio, uint8_t mod_ratio)
{
    ym2203_patch_apply(p, ch);
    uint8_t cm = YM_CARRIER_MASK[p->ALG & 7u];
    uint8_t vel_safe = (vel > 127u) ? 127u : vel;
    uint8_t lut_val = VEL_TO_TL_ATTN[vel_safe];
    for (int op = 0; op < 4; op++) {
        uint8_t off = (uint8_t)(YM_OP_OFF[op] + ch);
        int32_t tl = (int32_t)p->ops[op].TL;
        uint8_t is_car = (cm >> op) & 1u;
        if (p->vel_tl_scale > 0u) {
            uint8_t ratio = is_car ? car_ratio : mod_ratio;
            tl += (int32_t)lut_val * (int32_t)ratio / 8;
        }
        if (vol < 16u) tl += (int32_t)(16u - vol) * 5;
        if (tl < 0)   tl = 0;
        if (tl > 127) tl = 127;
        ym_write((uint8_t)(0x40u + off), (uint8_t)(tl & 0x7Fu));
    }
}

// =============================================================================
//  §2  화성학 상수 정의
// =============================================================================

/* qq 음계 인터벌 (반음 단위) qq */
#define INT_P1   0   /* 완전1도  */
#define INT_m2   1   /* 단2도    */
#define INT_M2   2   /* 장2도    */
#define INT_m3   3   /* 단3도    */
#define INT_M3   4   /* 장3도    */
#define INT_P4   5   /* 완전4도  */
#define INT_TT   6   /* 증4/감5  트라이톤 */
#define INT_P5   7   /* 완전5도  */
#define INT_A5   8   /* 증5도    (= INT_m6, 이명동음) */
#define INT_m6   8   /* 단6도    */
#define INT_M6   9   /* 장6도    */
#define INT_m7  10   /* 단7도    */
#define INT_M7  11   /* 장7도    */
#define INT_P8  12   /* 완전8도  */
#define INT_b9  13   /* 단9도    */
#define INT_N9  14   /* 장9도    */
#define INT_s9  15   /* 증9도    */
#define INT_P11 17   /* 완전11도 */
#define INT_s11 18   /* 증11도   */
#define INT_P13 21   /* 장13도   */
#define INT_b13 20   /* 단13도   */

/*
// qq 코드 타입 qq
typedef enum {
    // 3화음
    CHORD_MAJ = 0,
    CHORD_MIN = 1,
    CHORD_DIM = 2,
    CHORD_AUG = 3,
    CHORD_SUS2 = 4,
    CHORD_SUS4 = 5,
    // 7화음
    CHORD_MAJ7 = 6,
    CHORD_MIN7 = 7,
    CHORD_DOM7 = 8,
    CHORD_HALF_DIM = 9,
    CHORD_DIM7 = 10,
    CHORD_MAJ7S5 = 11,
    CHORD_DOM7S4 = 12,
    // 텐션
    CHORD_DOM9 = 13,
    CHORD_CHORD = 14,
    CHORD_DOM7S9 = 15,
    CHORD_DOM11 = 16,
    CHORD_MAJ9 = 17,
    CHORD_MIN9 = 18,
    CHORD_MIN11 = 19,
    CHORD_DOM13 = 20,
    // 어퍼 스트럭처
    CHORD_UST_A = 21,
    CHORD_UST_B = 22,
    CHORD_UST_C = 23,
    CHORD_COUNT
} chord_type_t;    */

typedef struct {
    const char* name;
    int8_t     tones[4];
    uint8_t     n_tones;
    /* [Rev.3] 불협화도 점수 0~100 (낮을수록 협화) */
    uint8_t     dissonance;
} chord_def_t;

static const chord_def_t CHORD_DEFS[] = {
    /* 3화음 */
        {"Maj",     {0, INT_M3, INT_P5,  0xFF}, 3,  5},
        {"min",     {0, INT_m3, INT_P5,  0xFF}, 3, 10},
        {"dim",     {0, INT_m3, INT_TT,  0xFF}, 3, 55},
        {"aug",     {0, INT_M3, INT_A5,  0xFF}, 3, 45}, // INT_m6 대신 증5도 권장
        {"sus2",    {0, INT_M2, INT_P5,  0xFF}, 3, 15},
        {"sus4",    {0, INT_P4, INT_P5,  0xFF}, 3, 20},

        /* 7화음 */
        {"Maj7",    {0, INT_M3, INT_P5,  INT_M7}, 4, 20},
        {"min7",    {0, INT_m3, INT_P5,  INT_m7}, 4, 25},
        {"dom7",    {0, INT_M3, INT_P5,  INT_m7}, 4, 35},
        {"m7b5",    {0, INT_m3, INT_TT,  INT_m7}, 4, 60}, // log에서 CHORD_HALF_DIM과 연결되는지 확인
        {"dim7",    {0, INT_m3, INT_TT,  INT_M6}, 4, 70},

        /* 텐션 - 중복 제거 및 이름 정리 */
        {"dom9",    {0, INT_M3, INT_m7,  INT_N9}, 4, 40},
        {"dom7b9",  {0, INT_M3, INT_m7,  INT_b9}, 4, 70}, // CHORD_DOM7 -> dom7b9으로 수정
        {"dom7s9",  {0, INT_M3, INT_m7,  INT_s9}, 4, 75}, // #9 -> s9 통일
        {"dom7s11", {0, INT_M3, INT_m7,  INT_s11},4, 65}, // 하나만 남김
        {"Maj9",    {0, INT_M3, INT_M7,  INT_N9}, 4, 22},
        {"min9",    {0, INT_m3, INT_m7,  INT_N9}, 4, 28},
        /* UST */
        {"UST-A",   {INT_M3, INT_s11, INT_P13, 0xFF}, 3, 60},
        {"UST-B",   {INT_b9, INT_M3,  INT_b13, 0xFF}, 3, 75},
        {"UST-C",   {INT_s9, INT_M3,  INT_s11, 0xFF}, 3, 80},
};

/* qq 조성 음계 qq */
static const int8_t SCALE_MAJOR[7] = { 0,2,4,5,7,9,11 };
static const int8_t SCALE_NAT_MIN[7] = { 0,2,3,5,7,8,10 };
static const int8_t SCALE_DORIAN[7] = { 0,2,3,5,7,9,10 };
static const int8_t SCALE_MIXO[7] = { 0,2,4,5,7,9,10 };
static const int8_t SCALE_LYDIAN[7] = { 0,2,4,6,7,9,11 };
static const int8_t SCALE_PHRYGIAN[7] = { 0,1,3,5,7,8,10 };
static const int8_t SCALE_LOCRIAN[7] = { 0,1,3,5,6,8,10 };
/* 한국 음계 */
static const int8_t SCALE_PYEONGJO[5] = { 0,2,5,7,9 };
static const int8_t SCALE_GYEMYEONG[5] = { 0,3,5,7,10 };

typedef enum {
    SCALE_T_MAJOR = 0, SCALE_T_NAT_MIN, SCALE_T_DORIAN,
    SCALE_T_MIXO, SCALE_T_LYDIAN, SCALE_T_PHRYGIAN, SCALE_T_LOCRIAN,
    SCALE_T_PYEONGJO, SCALE_T_GYEMYEONG,
    SCALE_T_COUNT
} scale_type_t;

/* qq 다이아토닉 코드 타입 테이블 qq */
static const chord_type_t DIATONIC_MAJ[7] = {
    CHORD_MAJ7, CHORD_MIN7, CHORD_MIN7, CHORD_MAJ7,
    CHORD_DOM7, CHORD_MIN7, CHORD_HALF_DIM
};
static const chord_type_t DIATONIC_MIN[7] = {
    CHORD_MIN7, CHORD_HALF_DIM, CHORD_MAJ7, CHORD_MIN7,
    CHORD_DOM7, CHORD_MAJ7,   CHORD_DOM7
};
static const chord_type_t DIATONIC_DOR[7] = {
    CHORD_MIN7, CHORD_MIN7,   CHORD_MAJ7, CHORD_DOM7,
    CHORD_MIN7, CHORD_HALF_DIM, CHORD_MAJ7
};

/* qq 케이던스 패턴 qq */
typedef struct {
    const char* name;
    uint8_t     len;
    int8_t      degree[8];
    chord_type_t ctype[8];
    uint8_t     beats[8];
} cadence_t;

#define CHORD_AUTO CHORD_COUNT

static const cadence_t CAD_AUTHENTIC = { "Authentic V7-I",   2, {5,1},    {CHORD_DOM7,    CHORD_MAJ7}, {2,4} };
static const cadence_t CAD_251 = { "II-V-I",           3, {2,5,1},  {CHORD_MIN7,    CHORD_DOM7,    CHORD_MAJ7}, {2,2,4} };
static const cadence_t CAD_251_B9 = { "II-V-I b9",        3, {2,5,1},  {CHORD_HALF_DIM, CHORD_DOM7,    CHORD_MAJ7}, {2,2,4} };
static const cadence_t CAD_TTS = { "TTS: II-bII7-I",   3, {2,-1,1}, {CHORD_MIN7,    CHORD_DOM11, CHORD_MAJ7}, {2,2,4} };
static const cadence_t CAD_PLAGAL = { "Plagal IV-I",      2, {4,1},    {CHORD_MAJ7,    CHORD_MAJ7}, {2,4} };
static const cadence_t CAD_DECEPTIVE = { "Deceptive V-VI",   2, {5,6},    {CHORD_DOM7,    CHORD_MIN7}, {2,4} };
static const cadence_t CAD_MODAL_BVII = { "Modal bVII-I",     2, {-7,1},   {CHORD_MAJ7,    CHORD_MAJ7}, {2,4} };
static const cadence_t CAD_DORIAN_IV = { "Dorian IV-I",      2, {4,1},    {CHORD_DOM7,    CHORD_MIN7}, {2,4} };
static const cadence_t CAD_PHRYGIAN = { "Phrygian bII-I",   2, {-1,1},   {CHORD_MAJ7,    CHORD_MIN7}, {2,4} };
static const cadence_t CAD_SEC_DOM = { "SecDom V/V-V-I",   3, {2,5,1},  {CHORD_DOM7,    CHORD_DOM7,    CHORD_MAJ7}, {2,2,4} };
static const cadence_t CAD_UST13 = { "UST13 V13-I",      2, {5,1},    {CHORD_DOM13,   CHORD_MAJ7}, {2,4} }; // 공백 제거 완료

/* qq 전위 (Inversion) qq */
typedef enum {
    INV_ROOT = 0,
    INV_1ST = 1,
    INV_2ND = 2,
    INV_3RD = 3,
} inversion_t;

/* 전위에 따른 3성부 보이싱 */
static inline void ym_chord_voiced(uint8_t root_note,
    chord_type_t ctype, inversion_t inv,
    int8_t out_notes[3])
{
    const chord_def_t* cd = &CHORD_DEFS[ctype];
    uint8_t n = (cd->n_tones > 3u) ? 3u : cd->n_tones;
    int16_t notes[4];
    for (int i = 0; i < (int)n; i++)
        notes[i] = (int16_t)root_note + (int16_t)cd->tones[i];
    for (int rot = 0; rot < (int)inv && rot < (int)n; rot++) {
        int16_t tmp = notes[0];
        for (int j = 0; j < (int)n - 1; j++) notes[j] = notes[j + 1];
        notes[n - 1] = (int16_t)(tmp + 12);
    }
    for (int i = 0; i < 3; i++)
        out_notes[i] = (int8_t)((i < (int)n) ? notes[i] : notes[0]);
}

/* [Rev.3] 최단 거리 전위 선택 ? 이전 성부와의 수직 이동거리 최소화 */
static inline inversion_t ym_nearest_inversion(
    uint8_t root, chord_type_t ctype,
    const int8_t prev_notes[3])
{
    inversion_t best_inv = INV_ROOT;
    int32_t best_dist = 0x7FFFFFFF;
    for (int inv = 0; inv < 4; inv++) {
        int8_t cand[3];
        ym_chord_voiced(root, ctype, (inversion_t)inv, cand);
        int32_t dist = 0;
        for (int v = 0; v < 3; v++) {
            int32_t d = (int32_t)cand[v] - (int32_t)prev_notes[v];
            dist += d * d;
        }
        if (dist < best_dist) { best_dist = dist; best_inv = (inversion_t)inv; }
    }
    return best_inv;
}

// =============================================================================
//  §2A  [Rev.3] 순정률 (Just Intonation) FNUM 보정
// =============================================================================
//
//  12평균율 대비 순정률 편차 (cent):
//    장3도(M3):  평균율 400c  →  순정률 386c  →  편차 -14c
//    단3도(m3):  평균율 300c  →  순정률 316c  →  편차 +16c
//    완전5도(P5):평균율 700c  →  순정률 702c  →  편차  +2c
//    단7도(m7):  평균율 1000c →  순정률 969c  →  편차 -31c
//    장7도(M7):  평균율 1100c →  순정률 1088c →  편차 -12c
//    장2도(M2):  편차  +4c
//    단9도(b9):  편차 -29c
//    장9도(N9):  편차  +4c
//    증11도(s11):편차 -49c
//    장13도(P13):편차  +2c
//
//  각 코드 성부(0=루트,1=3rd/sus,2=5th/텐션,3=7th)의 JI 보정값 (cent)
// =============================================================================

/* 성부 인덱스 최대 4 */
#define JI_MAX_TONES 4

/* 코드 타입별 JI 오프셋 테이블 (성부 순서 = CHORD_DEFS.tones 순서) */
static const int8_t JI_OFFSETS[CHORD_COUNT][JI_MAX_TONES] = {
    /*              root  tone1  tone2  tone3  */
    /* MAJ    */  {  0,   -14,   +2,    0   },  /* 1-M3-P5 */
    /* MIN    */  {  0,   +16,   +2,    0   },  /* 1-m3-P5 */
    /* DIM    */  {  0,   +16,   -29,   0   },  /* 1-m3-TT */
    /* AUG    */  {  0,   -14,   -14,   0   },  /* 1-M3-m6 */
    /* SUS2   */  {  0,   +4,    +2,    0   },  /* 1-M2-P5 */
    /* SUS4   */  {  0,   -2,    +2,    0   },  /* 1-P4-P5 */
    /* MAJ7   */  {  0,   -14,   +2,    -12 },  /* 1-M3-P5-M7 */
    /* MIN7   */  {  0,   +16,   +2,    -31 },  /* 1-m3-P5-m7 */
    /* DOM7   */  {  0,   -14,   +2,    -31 },  /* 1-M3-P5-m7 */
    /* M7B5   */  {  0,   +16,   -29,   -31 },
    /* DIM7   */  {  0,   +16,   -29,   +16 },
    /* MAJ7S5 */  {  0,   -14,   -14,   -12 },
    /* DOM7S4 */  {  0,   -14,   -49,   -31 },
    /* DOM9   */  {  0,   -14,   -31,   +4  },  /* 3성부: 1-M3-m7-N9 */
    /* CHORD_DOM7 */  {  0,   -14,   -31,   -29 },
    /* DOM7S9 */  {  0,   -14,   -31,   +20 },
    /* DOM7S11*/  {  0,   -14,   -31,   -49 },
    /* MAJ9   */  {  0,   -14,   -12,   +4  },
    /* MIN9   */  {  0,   +16,   -31,   +4  },
    /* MIN11  */  {  0,   +16,   -31,   -2  },
    /* DOM13  */  {  0,   -14,   -31,   +2  },
    /* UST_A  */  {  -14, -49,   +2,    0   },
    /* UST_B  */  {  -29, -14,   +16,   0   },
    /* UST_C  */  {  +20, -14,   -49,   0   },
};

/*
 * ym_ji_fnum_offset ? 성부 인덱스(voice_idx)에 대한 JI cent 오프셋 반환
 * chord_type, voice_idx(0~3) → cent 오프셋
 * Pitch Bend 값(pb: -8192~+8191)을 추가로 받아 JI와 합산
 */
static inline int16_t ym_ji_offset(chord_type_t ct, uint8_t vidx,
    int16_t pb_cent)
{
    if (ct >= CHORD_COUNT || vidx >= JI_MAX_TONES) return pb_cent;
    return (int16_t)JI_OFFSETS[ct][vidx] + pb_cent;
}

/*
 * fi_chord_on_ji ? 순정률 보정 코드 발음
 * fi_chord_on의 JI 버전. pb_cent: 피치벤드 cent 값 (-100~+100)
 */
 /* forward-declare full_inst_t for use in §2A */
typedef struct full_inst_s full_inst_t;

static inline void fi_chord_on_ji(const full_inst_t* fi,
    uint8_t root, chord_type_t ctype, inversion_t inv,
    uint8_t vel, int16_t pb_cent);  /* 구현: §11 이후 */

// =============================================================================
//  §2B  [Rev.5] 실시간 조(Key) 판별 엔진 ? key_detector_t
// =============================================================================
//
//  문제: 순정률(JI)은 연주 중인 '조(Key)'에 따라 루트음 기준이 달라야 합니다.
//    예) C장조에서 E를 연주 → JI 오프셋은 C 기준 장3도(-14c)
//        E장조에서 E를 연주 → E가 루트이므로 오프셋 0
//    고정 루트로 JI를 적용하면 조바꿈 시 오히려 불협화음이 됩니다.
//
//  해결: Krumhansl-Kessler 조성 프로파일 기반 실시간 조 판별
//    · 연주된 노트들의 pitch-class 가중치를 장/단조 프로파일과 비교
//    · 매 Note-On마다 누적(지수 감쇄 망각)하여 조 자동 갱신
//    · 판별 결과(root + mode)를 JI 오프셋 기준점으로 자동 연동
//
//  정밀도: 이 구현은 정수 근사로 매우 가볍습니다.
//    곱셈: 24회, 비교: 24회 → Note-On 당 약 1μs 이하 (Zynq A9 기준)
//
//  KK 프로파일 (정수 근사 ×100, 원래 float):
//    장조: {674,253,363,253,435,363,253,536,253,380,253,317}
//    단조: {651,268,481,628,267,463,253,628,348,253,477,253}
// =============================================================================

#define KD_DECAY_SHIFT  3   /* 망각 감쇄: 매 Note-On마다 acc >>= 3 (12.5% 잔류)                                                                                                                                                                                                                                */
#define KD_PC           12

typedef struct {
    uint16_t  acc[KD_PC];   /* pitch-class 누적 가중치 (0~65535)  */
    uint8_t   root;         /* 판별된 루트 (0~11, 피치 클래스)    */
    uint8_t   is_minor;     /* 0=장조, 1=단조                     */
    uint8_t   valid;        /* 1=유효한 판별 결과 존재             */
    uint8_t   min_notes;    /* 판별에 필요한 최소 노트 수 (기본 4) */
    uint16_t  note_count;   /* 누적 입력 노트 수                   */
} key_detector_t;

/* KK 프로파일 (×100 정수화) */
static const uint16_t KD_MAJ[KD_PC] = {
    674,253,363,253,435,363,253,536,253,380,253,317
};
static const uint16_t KD_MIN[KD_PC] = {
    651,268,481,628,267,463,253,628,348,253,477,253
};

static inline void kd_init(key_detector_t* kd)
{
    uint8_t i;
    for (i = 0u; i < KD_PC; i++) kd->acc[i] = 0u;
    kd->root = 0u;
    kd->is_minor = 0u;
    kd->valid = 0u;
    kd->min_notes = 4u;
    kd->note_count = 0u;
}

/*
 * kd_note_on ? Note-On 이벤트를 입력해 조성 누적값 갱신
 *
 * 지수 망각: 새 노트 입력 전 전체 acc를 KD_DECAY_SHIFT 만큼 우측 시프트.
 * 이렇게 하면 최근 노트에 가중치가 높아져 조바꿈에 빠르게 반응합니다.
 * 새 노트의 pitch-class에 vel 가중치를 추가합니다.
 */
static inline void kd_note_on(key_detector_t* kd, uint8_t note, uint8_t vel)
{
    uint8_t pc = note % 12u;
    uint8_t i;

    /* 망각 감쇄 ? 전체 acc를 서서히 줄여 오래된 노트의 영향 감소 */
    for (i = 0u; i < KD_PC; i++)
        kd->acc[i] = (uint16_t)(kd->acc[i] >> KD_DECAY_SHIFT);

    /* 현재 노트 가중치 추가 */
    uint16_t add = (uint16_t)(vel > 0u ? vel : 1u);
    kd->acc[pc] = (uint16_t)(kd->acc[pc] + add);
    if (kd->acc[pc] < add) kd->acc[pc] = 0xFFFFu; /* 오버플로 방지 */

    kd->note_count++;
    if (kd->note_count < (uint16_t)kd->min_notes) return;

    /* qq 조 판별 qq */
    uint8_t  best_root = 0u;
    uint8_t  best_minor = 0u;
    int32_t  best_score = -1;

    for (uint8_t r = 0u; r < KD_PC; r++) {
        /* 장조 상관값 계산 */
        int32_t maj_score = 0, min_score = 0;
        for (i = 0u; i < KD_PC; i++) {
            uint8_t idx = (uint8_t)((i + KD_PC - r) % KD_PC);
            maj_score += (int32_t)kd->acc[i] * (int32_t)KD_MAJ[idx];
            min_score += (int32_t)kd->acc[i] * (int32_t)KD_MIN[idx];
        }
        if (maj_score > best_score) {
            best_score = maj_score;
            best_root = r;
            best_minor = 0u;
        }
        if (min_score > best_score) {
            best_score = min_score;
            best_root = r;
            best_minor = 1u;
        }
    }

    kd->root = best_root;
    kd->is_minor = best_minor;
    kd->valid = 1u;
}

/*
 * kd_get_ji_offset ? 판별된 조를 기준으로 JI 오프셋 보정
 *
 * 기존 ym_ji_offset()은 chord_type 기반 고정 오프셋을 반환합니다.
 * 이 함수는 key_detector가 판별한 실시간 루트를 고려해 오프셋을 추가 보정합니다                                                                                                                                                                                                                               .
 *
 * 원리: 현재 연주 중인 note의 pitch-class와 판별된 조 루트의 관계(interval)를
 *       구하고, 해당 interval의 JI 편차를 추가로 더합니다.
 *
 * note:   현재 발음 중인 MIDI 노트
 * kd:     key_detector (kd->valid가 0이면 보정 없이 0 반환)
 * base_ji_cent: ym_ji_offset()으로 얻은 기존 코드 내 JI 오프셋
 */
static const int8_t KD_INTERVAL_JI[KD_PC] = {
    /*  P1   m2   M2   m3   M3   P4   TT   P5   m6   M6   m7   M7  */
    /*  JI 편차(cent): 12평균율 대비 순정률
        TT(증4도): 11:8 비율 → 551.3c, 평균율 600c → 편차 -49c가 아님.
        순정 증4도(11:8)는 551.3c이므로 평균율(600c) 대비 -49c.
        그러나 실용적 JI에서 TT는 7:5(582.5c, +45c편차 아님)도 사용.
        여기서는 harmonic series 기준 11:8(551c) → 편차 = 551-600 = -49c
        단, kd_get_ji_offset의 맥락은 '이 음정에 이 오프셋을 더해 JI에 근접'이므                                                                                                                                                                                                                               로
        TT를 순정화하면 -49c(551c 쪽)이 맞습니다.
        기존 +35는 오류(근거 불명)였으므로 -49로 수정합니다.           */
        0,  -29,  +4, +16, -14,  -2, -49,  +2,  -14, +6, -31, -12
};

static inline int16_t kd_get_ji_offset(const key_detector_t* kd,
    uint8_t note, int16_t base_ji_cent)
{
    if (!kd->valid) return base_ji_cent;
    uint8_t pc = note % 12u;
    uint8_t interval = (uint8_t)((pc + KD_PC - kd->root) % KD_PC);
    return (int16_t)(base_ji_cent + (int16_t)KD_INTERVAL_JI[interval]);
}

/*
 * kd_ji_note_ex ? 판별 조 기준 JI 보정 + Block 캐리를 적용해 채널에 발음
 *
 * 단독 멜로디 노트(fi_note_on 이후 세부 음정 조정)에 사용합니다.
 * ch: FM 채널, note: MIDI 노트, kd: key_detector, pb_cent: 피치벤드 cent
 */
static inline void kd_ji_note_ex(uint8_t ch, uint8_t note,
    const key_detector_t* kd, int16_t pb_cent)
{
    int16_t ji = kd_get_ji_offset(kd, note, 0);
    int16_t total_cent = ji + pb_cent;
    const ym2203_fnum_t* f = &YM2203_FREQ_LUT[note < 128u ? note : 127u];
    uint16_t base_fnum = (uint16_t)(f->lsb | ((uint16_t)(f->msb & 0x07u) << 8));
    uint8_t  base_blk = (uint8_t)((f->msb >> 3u) & 0x07u);
    uint16_t fnum; uint8_t blk;
    ym_fnum_cents_blk(base_fnum, base_blk, total_cent, &fnum, &blk);
    ym_set_fnum_raw(ch, fnum, blk);
}



// =============================================================================
//  §3  타이밍 시스템
// =============================================================================

static inline uint16_t bpm_to_ticks(uint8_t bpm)
{
    if (bpm == 0u) bpm = 120u;
    return (uint16_t)(6000u / (uint32_t)bpm);
}

typedef enum {
    DUR_WHOLE = 0,
    DUR_HALF = 1,
    DUR_QUARTER = 2,
    DUR_EIGHTH = 3,
    DUR_SIXTEENTH = 4,
    DUR_DOTTED_H = 5,
    DUR_DOTTED_Q = 6,
    DUR_DOTTED_E = 7,
    DUR_TRIPLET_Q = 8,
    DUR_TRIPLET_E = 9,
    DUR_COUNT
} dur_t;

static inline uint16_t dur_ticks(dur_t dur, uint16_t q_ticks)
{
    switch (dur) {
    case DUR_WHOLE:     return (uint16_t)(q_ticks * 4u);
    case DUR_HALF:      return (uint16_t)(q_ticks * 2u);
    case DUR_QUARTER:   return q_ticks;
    case DUR_EIGHTH:    return (uint16_t)(q_ticks / 2u);
    case DUR_SIXTEENTH: return (uint16_t)(q_ticks / 4u);
    case DUR_DOTTED_H:  return (uint16_t)(q_ticks * 3u);
    case DUR_DOTTED_Q:  return (uint16_t)(q_ticks * 3u / 2u);
    case DUR_DOTTED_E:  return (uint16_t)(q_ticks * 3u / 4u);
    case DUR_TRIPLET_Q: return (uint16_t)(q_ticks * 2u / 3u);
    case DUR_TRIPLET_E: return (uint16_t)(q_ticks / 3u);
    default:            return q_ticks;
    }
}

static inline void dur_swing_pair(uint16_t q_ticks, uint8_t swing_ratio,
    uint16_t* long_tick, uint16_t* short_tick)
{
    uint16_t total = q_ticks;
    *long_tick = (uint16_t)(total * (50u + swing_ratio) / 100u);
    *short_tick = (uint16_t)(total - *long_tick);
}

typedef enum {
    ART_LEGATO = 95,
    ART_NORMAL = 80,
    ART_STACCATO = 50,
    ART_TENUTO = 100,
} articulation_t;

// =============================================================================
//  §4  시퀀서 이벤트 / 화성 진행 레코드
// =============================================================================

typedef struct {
    uint8_t      root;
    chord_type_t ctype;
    inversion_t  inv;
    dur_t        dur;
    uint8_t      vel;
} chord_event_t;

typedef struct {
    uint8_t note;
    dur_t   dur;
    uint8_t vel;
} melody_event_t;

#define SEQ_MAX 32
typedef struct {
    uint8_t       bpm;
    uint8_t       swing;
    uint8_t       len;
    chord_event_t events[SEQ_MAX];
} chord_seq_t;

// =============================================================================
//  §5  가상 멀티OP 구조체
// =============================================================================

typedef enum {
    V8_ALG0 = 0, V8_ALG1, V8_ALG2, V8_ALG3, V8_ALG4, V8_ALG5, V8_ALG6, V8_ALG7,
    V8_ALG_COUNT
} ym2203_v8alg_t;
static const uint8_t V8_CHA[V8_ALG_COUNT] = { 0,0,4,1,5,3,2,7 };
static const uint8_t V8_CHB[V8_ALG_COUNT] = { 0,7,4,1,5,3,7,7 };

typedef enum {
    V12_ALG0 = 0, V12_ALG1, V12_ALG2, V12_ALG3, V12_ALG4, V12_ALG5,
    V12_ALG_COUNT
} ym2203_v12alg_t;
static const uint8_t V12_CH[V12_ALG_COUNT][3] = {
    {0,0,0},{1,4,7},{5,5,5},{1,1,3},{7,7,7},{4,5,1}
};

typedef struct {
    const char* name;
    ym2203_v8alg_t  alg;
    ym2203_patch_t  ch_a, ch_b;
    int16_t         det_a, det_b;
    uint8_t         vol_a, vol_b;
    uint8_t         lfo_vib, lfo_trem;
    uint16_t        lfo_hz100;
    uint8_t         sw_op;
    uint8_t         sw_tl0, sw_tl1;
    uint16_t        sw_ticks;
} dual_patch_t;

typedef struct {
    const char* name;
    ym2203_v12alg_t alg;
    ym2203_patch_t  ch[3];
    int16_t         det[3];
    int8_t          noff[3];
    uint8_t         vol[3];
    uint8_t         lfo_vib, lfo_trem;
    uint16_t        lfo_hz100;
} triple_patch_t;

struct full_inst_s {
    const char* name;
    uint8_t          fm_n;
    ym2203_patch_t   fm[3];
    int16_t          fm_det[3];
    int8_t           fm_noff[3];
    uint8_t          fm_vol[3];
    uint8_t          sw_op;
    uint8_t          sw_tl0, sw_tl1;
    uint16_t         sw_ticks;
    uint8_t          lfo_vib, lfo_trem;
    uint16_t         lfo_hz100;
    uint8_t          ssg_mask;
    ym2203_ssg_idx_t ssg_p[3];
    uint8_t          ssg_amp[3];
    int8_t           ssg_noff[3];
};

// =============================================================================
//  §5A  [Rev.3] 포르타멘토 / 글리산도 엔진
// =============================================================================
//
//  국악 주법 매핑:
//    추성(推聲): 목표음에서 위로 밀어 올림  → PT_MODE_UP
//    퇴성(退聲): 목표음에서 아래로 내림      → PT_MODE_DOWN
//    전성(轉聲): 아래→위 혹은 위→아래 훑기  → PT_MODE_SLIDE
//    꺾기(折聲): 음 끝에서 급격히 하강       → PT_MODE_BEND_OFF
// =============================================================================

typedef enum {
    PT_MODE_SLIDE = 0,  /* 선형 FNUM 보간 (일반 포르타멘토)  */
    PT_MODE_UP = 1,  /* 낮은 음에서 목표음으로 상행 (추성) */
    PT_MODE_DOWN = 2,  /* 높은 음에서 목표음으로 하행 (퇴성) */
    PT_MODE_BEND_OFF = 3,  /* Key-Off 후 하강 꺾기 (꺾성)        */
    PT_MODE_EXP = 4,  /* 지수 곡선 보간 (자연 감속)          */
} pt_mode_t;

typedef struct {
    uint16_t fnum_src;    /* 시작 FNUM (하위 호환)          */
    uint16_t fnum_dst;    /* 목표 FNUM (하위 호환)          */
    uint8_t  blk;         /* 블록(옥타브) 값                */
    uint8_t  ch;          /* 대상 FM 채널                   */
    uint16_t total_ticks; /* 총 보간 틱 수                  */
    uint16_t cur_tick;    /* 현재 틱                        */
    pt_mode_t mode;
    uint8_t  active;      /* 1=진행 중                      */
    /* [Rev.6] Rev.7 linear pitch 기반 크로스-블록 보간    */
    uint32_t lp_src;      /* block*2048+fnum 시작값         */
    uint32_t lp_dst;      /* block*2048+fnum 목표값         */
} portamento_t;

/*
 * pt_start ? 포르타멘토 초기화
 * from_note/to_note: MIDI 노트
 * ticks: 보간 총 틱 수 (BPM에 맞게 조절)
 *
 * [Rev.6] Rev.7의 ym2203_fnum_to_linear() 기반 linear pitch 보간 사용.
 *   block × 2048 + fnum의 "선형 피치 수"로 변환 후 보간하므로
 *   옥타브를 가로지르는 글라이드(예: E3→C5)도 자연스럽게 처리됩니다.
 *   기존 단일 FNUM 보간 방식은 블록이 달라지면 음정이 뛰었습니다.
 */
static inline void pt_start(portamento_t* pt,
    uint8_t from_note, uint8_t to_note,
    uint8_t ch, uint16_t ticks, pt_mode_t mode)
{
    if (from_note > 127u) from_note = 127u;
    if (to_note > 127u) to_note = 127u;
    const ym2203_fnum_t* fs = &YM2203_FREQ_LUT[from_note];
    const ym2203_fnum_t* fd = &YM2203_FREQ_LUT[to_note];
    /* [Rev.6] linear pitch 캐시 ? fnum_src/dst는 하위 호환용으로 유지 */
    pt->fnum_src = (uint16_t)(fs->lsb | ((uint16_t)(fs->msb & 7u) << 8));
    pt->fnum_dst = (uint16_t)(fd->lsb | ((uint16_t)(fd->msb & 7u) << 8));
    pt->blk = (uint8_t)((fd->msb >> 3u) & 0x07u);
    pt->ch = ch;
    pt->total_ticks = (ticks < 1u) ? 1u : ticks;
    pt->cur_tick = 0u;
    pt->mode = mode;
    pt->active = 1u;
    /* linear pitch 사전 계산 ? tick 내 나눗셈 반복 방지 */
    pt->lp_src = ym2203_fnum_to_linear(fs->lsb, fs->msb);
    pt->lp_dst = ym2203_fnum_to_linear(fd->lsb, fd->msb);
}

/*
 * pt_tick ? 매 10ms 틱 호출. 반환: 1=진행 중, 0=완료
 * [Rev.6] Rev.7 ym2203_linear_to_fnum() 활용 ? 옥타브 글라이드 정확 처리
 */

void ym_write_force(uint8_t reg, uint8_t val);

static inline int pt_tick(portamento_t* pt)
{
    if (!pt->active) return 0;
    if (pt->cur_tick >= pt->total_ticks) {
        ym_set_fnum_raw(pt->ch, pt->fnum_dst, pt->blk);
        pt->active = 0;
        return 0;
    }
    uint32_t t = pt->cur_tick;
    uint32_t T = (pt->total_ticks > 0u) ? pt->total_ticks : 1u;
    int64_t  s = (int64_t)pt->lp_src;
    int64_t  d = (int64_t)pt->lp_dst;
    int64_t  lp;

    switch (pt->mode) {
    case PT_MODE_EXP: {
        /* 지수 감속: lp = dst + (src-dst)*(1 - t/T)^2 */
        int64_t rem = (int64_t)(T - t);
        lp = d + (s - d) * rem * rem / ((int64_t)T * (int64_t)T);
        break;
    }
    default: /* 선형 */
        lp = s + (d - s) * (int64_t)t / (int64_t)T;
        break;
    }
    if (lp < 0)      lp = 0;
    if (lp > 0x7FFF) lp = 0x7FFF;
    uint8_t lsb, msb;
    ym2203_linear_to_fnum((uint32_t)lp, &lsb, &msb);
    ym_write_force((uint8_t)(0xA4u + pt->ch), msb);
    ym_write_force((uint8_t)(0xA0u + pt->ch), lsb);
    pt->cur_tick++;
    return 1;
}

// =============================================================================
//  §5B  [Rev.3] 비선형 국악 농음 LFO
// =============================================================================
//
//  서양 비브라토: 규칙적 사인파 ±일정폭
//  국악 농음/요성: 상승폭 ≠ 하강폭, 속도 변화, 비대칭 파형
//
//  NLFO 파형:
//    구간 0~rise_ticks : 0 → +depth_up   (상행, 선형)
//    구간 rise~period  : +depth_up → -depth_dn (하행, 선형)
//    이후 반복
// =============================================================================

typedef struct {
    int16_t  depth_up;    /* 상행 최대 cent (+)             */
    int16_t  depth_dn;    /* 하행 최대 cent (양수로 저장)   */
    uint16_t rise_ticks;  /* 상행 구간 틱 수                */
    uint16_t fall_ticks;  /* 하행 구간 틱 수 (period - rise)*/
    uint16_t phase;       /* 현재 위상 틱                   */
    uint8_t  active;
} nonlinear_lfo_t;

/* 율명별 농음 프리셋 */
typedef enum {
    NLFO_HWANG = 0,  /* 황(黃): 넓고 느린 요성        */
    NLFO_TAE = 1,  /* 태(太): 중간 속도 비대칭       */
    NLFO_JOONG = 2,  /* 중(仲): 빠르고 깊은 농음       */
    NLFO_IM = 3,  /* 임(林): 가볍고 짧은 흔들기     */
    NLFO_NAM = 4,  /* 남(南): 하행 강조 퇴성 스타일  */
    NLFO_PRESET_COUNT
} nlfo_preset_t;

/* [depth_up, depth_dn, rise_ticks, fall_ticks] */
static const int16_t NLFO_PRESETS[NLFO_PRESET_COUNT][4] = {
    /* HWANG */ { +60, -30,  18, 32 },
    /* TAE   */ { +45, -55,  12, 18 },
    /* JOONG */ { +80, -80,   8, 10 },
    /* IM    */ { +30, -25,   6,  8 },
    /* NAM   */ { +25, -90,  10, 20 },
};

static inline void nlfo_init(nonlinear_lfo_t* lfo, nlfo_preset_t p)
{
    lfo->depth_up = NLFO_PRESETS[p][0];
    lfo->depth_dn = NLFO_PRESETS[p][1];
    lfo->rise_ticks = (uint16_t)NLFO_PRESETS[p][2];
    lfo->fall_ticks = (uint16_t)NLFO_PRESETS[p][3];
    lfo->phase = 0u;
    lfo->active = 1u;
}

static inline void nlfo_init_custom(nonlinear_lfo_t* lfo,
    int16_t depth_up, int16_t depth_dn,
    uint16_t rise_t, uint16_t fall_t)
{
    lfo->depth_up = depth_up;
    lfo->depth_dn = depth_dn;
    lfo->rise_ticks = rise_t;
    lfo->fall_ticks = fall_t;
    lfo->phase = 0u;
    lfo->active = 1u;
}

/*
 * nlfo_tick ? 매 틱 호출. cent 오프셋 반환.
 * 반환값을 ym_fnum_cents_blk()에 전달하면 됩니다.
 */
static inline int16_t nlfo_tick(nonlinear_lfo_t* lfo)
{
    if (!lfo->active) return 0;
    uint16_t period = lfo->rise_ticks + lfo->fall_ticks;
    if (period == 0u) return 0;
    int16_t result;
    if (lfo->phase < lfo->rise_ticks) {
        /* 상행 구간 */
        result = (int16_t)((int32_t)lfo->depth_up
            * (int32_t)lfo->phase
            / (int32_t)lfo->rise_ticks);
    }
    else {
        /* 하행 구간 */
        uint16_t fp = lfo->phase - lfo->rise_ticks;
        result = lfo->depth_up
            + (int16_t)((int32_t)(lfo->depth_dn - lfo->depth_up)
                * (int32_t)fp
                / (int32_t)lfo->fall_ticks);
    }
    if (++lfo->phase >= period) lfo->phase = 0u;
    return result;
}

/*
 * nlfo_tick_r ? 랜덤 워크 포함 농음 틱 (권장)
 *
 * 문제: nlfo_tick()은 완벽히 규칙적인 삼각파를 생성하여 기계적으로 들립니다.
 *       실제 국악 연주자는 주기마다 속도·폭에 미세한 변동을 줍니다.
 *
 * 해결: 매 주기(phase==0 리셋 직후) xorshift 난수를 이용해
 *       ±rand_range cent 범위의 오프셋을 추가합니다.
 *       rand_range=5~10 정도가 자연스럽습니다.
 *
 * rand_range: 랜덤 진폭 변동 최대값 (cent), 0이면 완전히 규칙적
 */
static inline int16_t nlfo_tick_r(nonlinear_lfo_t* lfo, int16_t rand_range)
{
    if (!lfo->active) return 0;
    uint16_t period = lfo->rise_ticks + lfo->fall_ticks;
    if (period == 0u) return 0;
    int16_t result;
    if (lfo->phase < lfo->rise_ticks) {
        result = (int16_t)((int32_t)lfo->depth_up
            * (int32_t)lfo->phase
            / (int32_t)lfo->rise_ticks);
    }
    else {
        uint16_t fp = lfo->phase - lfo->rise_ticks;
        result = lfo->depth_up
            + (int16_t)((int32_t)(lfo->depth_dn - lfo->depth_up)
                * (int32_t)fp
                / (int32_t)lfo->fall_ticks);
    }
    /* 주기 마지막 틱에서 다음 주기 변동량 결정 */
    static int16_t s_rand_offset = 0;
    if (lfo->phase == 0u && rand_range > 0) {
        /* nlfo_rand() → -rand_range ~ +rand_range */
        int16_t r = (int16_t)(nlfo_rand() % (uint16_t)(2u * (uint16_t)rand_range + 1u));
        s_rand_offset = r - rand_range;
    }
    result += s_rand_offset;
    if (++lfo->phase >= period) lfo->phase = 0u;
    return result;
}

// =============================================================================
//  §5C  [Rev.3] 링 버퍼 캐논 엔진
// =============================================================================
//
//  채널 0의 Note On/Off 이벤트를 캡처하여 N틱 지연 후 채널 1에 재생.
//  화성 테이블(조성 음계)에 따라 음정 자동 보정(Quantization).
// =============================================================================

#define CANON_BUF_SIZE 64  /* 최대 캡처 이벤트 수 */

typedef struct {
    uint8_t  note;        /* MIDI note           */
    uint8_t  vel;         /* 0=Note Off          */
    uint16_t tick_abs;    /* 절대 틱 타임스탬프  */
} canon_event_t;

typedef struct {
    canon_event_t  buf[CANON_BUF_SIZE];
    uint8_t        wr;           /* 쓰기 인덱스       */
    uint8_t        rd;           /* 읽기 인덱스       */
    uint16_t       delay_ticks;  /* 지연 틱           */
    uint16_t       abs_tick;     /* 현재 절대 틱      */
    uint8_t        dst_ch;       /* 출력 FM 채널      */
    /* 조성 보정: key_root + scale */
    uint8_t        quantize;     /* 1=화성 보정 ON    */
    uint8_t        key_root;     /* 조성 루트 MIDI    */
    scale_type_t   scale_type;
    /* 음정 반전(Inversion)으로 캐논 변형 */
    uint8_t        mirror;       /* 1=상하 반전 대위  */
    uint8_t        mirror_root;  /* 반전 기준 MIDI 노트 */
} canon_engine_t;

static inline void ce_init(canon_engine_t* ce,
    uint16_t delay_ticks, uint8_t dst_ch,
    uint8_t quantize, uint8_t key_root, scale_type_t st)
{
    memset(ce, 0, sizeof(*ce));
    ce->delay_ticks = delay_ticks;
    ce->dst_ch = dst_ch;
    ce->quantize = quantize;
    ce->key_root = key_root;
    ce->scale_type = st;
}

/* 음정 보정: 가장 가까운 조성 음으로 양자화 */
static inline uint8_t ce_quantize_note(uint8_t note,
    uint8_t key_root, scale_type_t stype)
{
    static const int8_t* SCALE_PTRS[SCALE_T_COUNT] = {
        SCALE_MAJOR, SCALE_NAT_MIN, SCALE_DORIAN, SCALE_MIXO,
        SCALE_LYDIAN, SCALE_PHRYGIAN, SCALE_LOCRIAN,
        SCALE_PYEONGJO, SCALE_GYEMYEONG
    };
    static const uint8_t SCALE_LENS[SCALE_T_COUNT] = { 7,7,7,7,7,7,7,5,5 };
    const int8_t* sc = SCALE_PTRS[stype];
    uint8_t len = SCALE_LENS[stype];
    int16_t n12 = (int16_t)note - (int16_t)key_root;
    int16_t oct = n12 / 12;
    int16_t pc = n12 % 12;
    if (pc < 0) { pc += 12; oct--; }
    /* 가장 가까운 음 찾기 */
    int8_t  best_pc = sc[0];
    int16_t best_dist = 127;
    for (int i = 0; i < (int)len; i++) {
        int16_t d = pc - (int16_t)sc[i];
        if (d < 0) d = -d;
        if (d > 6) d = 12 - d;  /* 옥타브 경계 */
        if (d < best_dist) { best_dist = d; best_pc = sc[i]; }
    }
    int16_t result = (int16_t)key_root + oct * 12 + best_pc;
    if (result < 0)   result = 0;
    if (result > 127) result = 127;
    return (uint8_t)result;
}

/* 이벤트 캡처 (채널 0 Note On/Off 발생 시 호출) */
static inline void ce_capture(canon_engine_t* ce,
    uint8_t note, uint8_t vel)
{
    uint8_t next = (uint8_t)((ce->wr + 1u) & (CANON_BUF_SIZE - 1u));
    if (next == ce->rd) return;  /* 버퍼 가득 ? 드롭 */
    ce->buf[ce->wr].note = note;
    ce->buf[ce->wr].vel = vel;
    ce->buf[ce->wr].tick_abs = ce->abs_tick;
    ce->wr = next;
}

/* 매 틱 호출 ? 지연된 이벤트를 dst_ch에 재생 */
static inline void ce_tick(canon_engine_t* ce,
    const ym2203_patch_t* patch, uint8_t vol)
{
    ce->abs_tick++;
    while (ce->rd != ce->wr) {
        canon_event_t* ev = &ce->buf[ce->rd];
        uint16_t fire_tick = (uint16_t)(ev->tick_abs + ce->delay_ticks);
        if ((int16_t)(ce->abs_tick - fire_tick) < 0) break;
        uint8_t note = ev->note;
        /* 음정 변환: 반전 대위 */
        if (ce->mirror) {
            int16_t mn = 2 * (int16_t)ce->mirror_root - (int16_t)note;
            if (mn < 0)   mn = 0;
            if (mn > 127) mn = 127;
            note = (uint8_t)mn;
        }
        /* 조성 양자화 */
        if (ce->quantize)
            note = ce_quantize_note(note, ce->key_root, ce->scale_type);
        if (ev->vel > 0u) {
            ym2203_key_off(ce->dst_ch);
            ym_patch_vol(patch, ce->dst_ch, ev->vel, vol);
            ym_set_note_ex(ce->dst_ch, note, 0, 0);
            ym2203_key_on(ce->dst_ch, 0x0Fu);
        }
        else {
            ym2203_key_off(ce->dst_ch);
        }
        ce->rd = (uint8_t)((ce->rd + 1u) & (CANON_BUF_SIZE - 1u));
    }
}

// =============================================================================
//  §5D  [Rev.3] 텐션 기반 FM 파라미터 모핑 (Timbre-Harmonic Interaction)
// =============================================================================
//
//  불협화도(dissonance 0~100) → FM Feedback + 캐리어 TL 실시간 변조
//  긴장도가 높은 코드(dim7, CHORD_DOM7 등)에서 배음이 날카로워짐.
// =============================================================================

typedef struct {
    uint8_t ch;             /* 대상 FM 채널               */
    uint8_t base_fb;        /* 기본 FB 값 (0~7)           */
    uint8_t max_fb;         /* 최대 FB 값                 */
    uint8_t base_tl_delta;  /* 기본 TL 조정 (0)           */
    uint8_t max_tl_delta;   /* 최대 TL 조정 (낮을수록 큰소리)*/
    uint8_t active;
} timbre_morph_t;

static inline void tm_init(timbre_morph_t* tm,
    uint8_t ch, uint8_t base_fb, uint8_t max_fb,
    uint8_t max_tl_delta)
{
    tm->ch = ch;
    tm->base_fb = base_fb;
    tm->max_fb = max_fb;
    tm->base_tl_delta = 0u;
    tm->max_tl_delta = max_tl_delta;
    tm->active = 1u;
}

/*
 * tm_apply ? 불협화도 점수(0~100)를 받아 FB와 TL 즉시 변조
 * patch: 현재 패치 포인터 (TL 기준값 참조용)
 */
static inline void tm_apply(timbre_morph_t* tm,
    const ym2203_patch_t* patch, uint8_t dissonance)
{
    if (!tm->active) return;
    /* FB 변조 */
    uint8_t fb = tm->base_fb
        + (uint8_t)((uint32_t)(tm->max_fb - tm->base_fb)
            * dissonance / 100u);
    if (fb > 7u) fb = 7u;
    /* FB 레지스터: 0xB0 + ch, bits[5:3]=FB bits[2:0]=ALG */
    uint8_t alg_ch = patch->ALG & 0x07u;
    ym_write((uint8_t)(0xB0u + tm->ch), (uint8_t)((fb << 3u) | alg_ch));
    /* 캐리어 TL 변조: dissonance가 높으면 TL을 낮춰(볼륨↑) 날카롭게 */
    uint8_t cm = YM_CARRIER_MASK[alg_ch];
    uint8_t tl_delta = (uint8_t)((uint32_t)tm->max_tl_delta
        * dissonance / 100u);
    for (int op = 0; op < 4; op++) {
        if (!((cm >> op) & 1u)) continue;
        uint8_t off = (uint8_t)(YM_OP_OFF[op] + tm->ch);
        int32_t tl = (int32_t)patch->ops[op].TL - (int32_t)tl_delta;
        if (tl < 0)   tl = 0;
        if (tl > 127) tl = 127;
        ym_write((uint8_t)(0x40u + off), (uint8_t)(tl & 0x7Fu));
    }
}

// =============================================================================
//  §5E  [Rev.3] 대위법 엔진
// =============================================================================
//
//  병행 5도/8도: 두 성부가 직행(같은 방향)으로 완전5도·완전8도를 이루면 금지.
//  계류음: 화음 전환 시 이전 화음 음 중 하나를 한 박 지연 해결.
//  경과음: 두 음정 간격이 3도 이상일 때 중간 음 자동 삽입.
// =============================================================================

typedef struct {
    int8_t prev_notes[3];  /* 이전 화음 성부 노트            */
    int8_t curr_notes[3];  /* 현재 화음 성부 노트            */
    uint8_t susp_active;   /* 계류음 활성 (채널 비트마스크)  */
    int8_t  susp_note[3];  /* 계류음 노트                    */
    uint8_t susp_resolve[3]; /* 해결음 노트                  */
    uint16_t susp_tick[3];   /* 계류음 남은 틱               */
} counterpoint_t;

/*
 * cp_check_parallels ? 병행 5도/8도 감지
 * prev[3], next[3]: 이전/다음 성부 노트
 * 반환: 위반 채널 쌍 비트마스크 (bit 0=ch0-1, bit 1=ch0-2, bit 2=ch1-2)
 * 위반 발생 시 next[]를 자동으로 전위하여 수정.
 */
static inline uint8_t cp_check_parallels(
    const int8_t prev[3], int8_t next[3])
{
    uint8_t violations = 0u;
    static const uint8_t pairs[3][2] = { {0,1},{0,2},{1,2} };
    for (int p = 0; p < 3; p++) {
        uint8_t i = pairs[p][0], j = pairs[p][1];
        int8_t prev_ivl = (int8_t)(((int16_t)prev[j] - prev[i] + 12) % 12);
        int8_t next_ivl = (int8_t)(((int16_t)next[j] - next[i] + 12) % 12);
        int8_t prev_dir = (prev[j] > prev[i]) ? 1 : -1;
        int8_t next_dir = (next[j] > next[i]) ? 1 : -1;
        /* 직행 + 완전5도(7반음) 또는 완전8도(0반음) */
        if (prev_dir == next_dir
            && (next_ivl == 7 || next_ivl == 0)) {
            violations |= (uint8_t)(1u << p);
            /* 수정: 상성부를 한 옥타브 올려 반행으로 전환 */
            next[j] = (int8_t)(next[j] + 12);
            if (next[j] > 127) next[j] = (int8_t)(next[j] - 12);
        }
    }
    return violations;
}

/*
 * cp_prepare_suspension ? 화음 전환 시 계류음 준비
 * prev_notes → next_notes 전환 시 가장 큰 도약 성부를 계류음 처리
 * resolve_ticks: 해결까지의 틱 수
 */
static inline void cp_prepare_suspension(counterpoint_t* cp,
    const int8_t prev[3], const int8_t next[3],
    uint16_t resolve_ticks)
{
    /* 가장 큰 도약을 찾아 계류 */
    int16_t max_leap = 0;
    int     leap_v = -1;
    for (int v = 0; v < 3; v++) {
        int16_t d = (int16_t)next[v] - (int16_t)prev[v];
        if (d < 0) d = -d;
        if (d > max_leap && d >= 2) { max_leap = d; leap_v = v; }
    }
    if (leap_v < 0) return;
    /* 계류음: 이전 음 유지 → 해결음으로 하행 */
    int8_t rn = (int8_t)(prev[leap_v] - 1); /* 반음 하강 해결 */
    if (rn < 0) rn = 0;
    cp->susp_active |= (uint8_t)(1u << leap_v);
    cp->susp_note[leap_v] = prev[leap_v];
    cp->susp_resolve[leap_v] = rn;
    cp->susp_tick[leap_v] = resolve_ticks;
}

/*
 * cp_tick ? 계류음 해결 처리. 매 틱 호출.
 * 반환: 계류 완료된 채널 비트마스크
 */
static inline uint8_t cp_tick(counterpoint_t* cp)
{
    uint8_t done = 0u;
    for (int v = 0; v < 3; v++) {
        if (!(cp->susp_active & (1u << v))) continue;
        if (cp->susp_tick[v] > 0u) {
            cp->susp_tick[v]--;
        }
        else {
            /* 해결: 계류음 → 해결음 발음 */
            ym_set_note_ex((uint8_t)v, (uint8_t)cp->susp_resolve[v], 0, 0);
            cp->susp_active &= (uint8_t)(~(1u << v));
            done |= (uint8_t)(1u << v);
        }
    }
    return done;
}

// =============================================================================
//  §5F  [Rev.3] 화성 분석기 (Chord Analyzer)
// =============================================================================
//
//  눌린 노트 집합 → 코드 타입 + 루트 자동 판별
//  최대 6개 노트까지 동시 분석
// =============================================================================

#define CA_MAX_NOTES 6

typedef struct {
    chord_type_t  ctype;     /* 인식된 코드 타입   */
    uint8_t       root;      /* 인식된 루트 MIDI   */
    uint8_t       dissonance;/* 불협화도 0~100      */
    uint8_t       confidence;/* 인식 신뢰도 0~100  */
} chord_analysis_t;

/*
 * ca_analyze ? 노트 배열을 분석하여 코드 타입과 루트 반환
 * notes[]: MIDI 노트 배열 (정렬 불필요)
 * n_notes: 노트 수
 */
static inline chord_analysis_t ca_analyze(
    const uint8_t notes[], uint8_t n_notes)
{
    chord_analysis_t result = { CHORD_MAJ, 0, 0, 0 };
    if (n_notes < 2u) return result;

    /* 음정 클래스 집합 (피치 클래스 비트맵) */
    uint16_t pc_set = 0u;
    uint8_t  root = notes[0];
    for (int i = 0; i < (int)n_notes; i++) {
        pc_set |= (uint16_t)(1u << (notes[i] % 12u));
        if (notes[i] < root) root = notes[i];
    }
    root = root % 12u; /* 루트 피치 클래스 */

    /* 각 코드 타입의 구성음과 비교 */
    uint8_t best_match = 0u;
    chord_type_t best_ct = CHORD_MAJ;
    uint8_t best_root_pc = 0u;

    for (uint8_t r = 0; r < 12u; r++) {          /* 루트 후보 */
        for (int ct = 0; ct < (int)CHORD_COUNT; ct++) {
            const chord_def_t* cd = &CHORD_DEFS[ct];
            uint16_t chord_pc = 0u;
            uint8_t  match = 0u;
            for (int t = 0; t < (int)cd->n_tones; t++) {
                if (cd->tones[t] == 0xFFu) continue;
                uint8_t pc = (r + cd->tones[t]) % 12u;
                chord_pc |= (uint16_t)(1u << pc);
            }
            /* 일치 비트 카운트 */
            uint16_t common = pc_set & chord_pc;
            while (common) { match++; common &= (uint16_t)(common - 1u); }
            if (match > best_match) {
                best_match = match;
                best_ct = (chord_type_t)ct;
                best_root_pc = r;
            }
        }
    }
    /* 실제 MIDI 루트 추정 (가장 낮은 일치 노트) */
    uint8_t actual_root = 60u;
    for (int i = 0; i < (int)n_notes; i++) {
        if ((notes[i] % 12u) == best_root_pc) {
            actual_root = notes[i];
            break;
        }
    }
    result.ctype = best_ct;
    result.root = actual_root;
    result.dissonance = CHORD_DEFS[best_ct].dissonance;
    result.confidence = (uint8_t)(best_match * 100u
        / CHORD_DEFS[best_ct].n_tones);
    return result;
}

// =============================================================================
//  §5G  [Rev.3] MIDI 지능형 보이스 매니저
// =============================================================================
//
//  FM 3채널 + PSG 3채널의 보이스 스케줄러.
//  화성적 우선순위 기반 Voice Stealing.
//
//  우선순위 (높을수록 유지):
//    5 = 루트(1도)
//    4 = 가이드톤 3도/7도
//    3 = 최상성 멜로디
//    2 = 5도
//    1 = 텐션 (9/11/13)
//    0 = 미분류
// =============================================================================

#define VM_FM_CH   3
#define VM_PSG_CH  3
#define VM_TOTAL   (VM_FM_CH + VM_PSG_CH)

typedef struct {
    uint8_t  note;      /* MIDI note, 0=비어있음  */
    uint8_t  vel;
    uint8_t  priority;  /* 0~5                    */
    uint8_t  on;        /* 1=발음 중              */
    uint16_t age;       /* 발음 후 경과 틱        */
    uint8_t  sustain;   /* 서스테인 페달로 유지   */
} vm_voice_t;

typedef struct {
    vm_voice_t      voices[VM_TOTAL]; /* 0~2: FM, 3~5: PSG   */
    uint8_t         sustain_pedal;    /* CC#64 상태           */
    /* 현재 화성 컨텍스트 */
    uint8_t         ctx_root;
    chord_type_t    ctx_chord;
    uint8_t         ctx_valid;
    /* [Rev.5] 실시간 조 판별기 연동 */
    key_detector_t  key_det;          /* 자동 조 판별 상태    */
    uint8_t         use_key_det;      /* 1=조 판별 JI 보정 ON */
    /* 통계 */
    uint16_t        steal_count;
} voice_mgr_t;

/*
 * vm_voice_priority ? 화음 컨텍스트 기반 우선순위 계산
 */
static inline uint8_t vm_voice_priority(voice_mgr_t* vm, uint8_t note)
{
    if (!vm->ctx_valid) return 0u;
    uint8_t pc = note % 12u;
    uint8_t root_pc = vm->ctx_root % 12u;
    const chord_def_t* cd = &CHORD_DEFS[vm->ctx_chord];
    for (int t = 0; t < (int)cd->n_tones; t++) {
        if (cd->tones[t] == 0xFFu) continue;
        uint8_t tone_pc = (root_pc + cd->tones[t]) % 12u;
        if (pc != tone_pc) continue;
        /* 루트 */
        if (cd->tones[t] == INT_P1)  return 5u;
        /* 가이드톤 */
        if (cd->tones[t] == INT_M3 || cd->tones[t] == INT_m3
            || cd->tones[t] == INT_M7 || cd->tones[t] == INT_m7)
            return 4u;
        /* 5도 */
        if (cd->tones[t] == INT_P5)  return 2u;
        /* 텐션 */
        return 1u;
    }
    return 0u;
}

static inline void vm_init(voice_mgr_t* vm)
{
    memset(vm, 0, sizeof(*vm));
    kd_init(&vm->key_det);
}

/* [Rev.5] vm_init_ji ? 조 판별 JI 자동 보정 활성화하여 초기화 */
static inline void vm_init_ji(voice_mgr_t* vm)
{
    vm_init(vm);
    vm->use_key_det = 1u;
    vm->key_det.min_notes = 3u;  /* 3음 이상 입력 시 조 판별 시작 */
}

/* 화성 컨텍스트 업데이트 (코드 변경 시 호출) */
static inline void vm_set_chord(voice_mgr_t* vm,
    uint8_t root, chord_type_t ct)
{
    vm->ctx_root = root;
    vm->ctx_chord = ct;
    vm->ctx_valid = 1u;
    /* 기존 발음 중인 보이스 우선순위 재계산 */
    for (int i = 0; i < VM_TOTAL; i++) {
        if (vm->voices[i].on)
            vm->voices[i].priority
            = vm_voice_priority(vm, vm->voices[i].note);
    }
}

/* 빈 채널 또는 Steal할 채널 선택 */
static inline int vm_alloc_ch(voice_mgr_t* vm, uint8_t prefer_fm)
{
    /* 1순위: 빈 채널 */
    int start = prefer_fm ? 0 : VM_FM_CH;
    int end = prefer_fm ? VM_FM_CH : VM_TOTAL;
    for (int i = start; i < end; i++)
        if (!vm->voices[i].on) return i;
    /* 2순위: 서스테인 아닌, 우선순위 가장 낮고 가장 오래된 채널 */
    int   steal = -1;
    uint8_t min_pri = 255u;
    uint16_t max_age = 0u;
    for (int i = start; i < end; i++) {
        if (vm->voices[i].sustain) continue;
        if (vm->voices[i].priority < min_pri
            || (vm->voices[i].priority == min_pri
                && vm->voices[i].age > max_age)) {
            min_pri = vm->voices[i].priority;
            max_age = vm->voices[i].age;
            steal = i;
        }
    }
    if (steal >= 0) vm->steal_count++;
    return steal;
}

/*
 * vm_note_on ? 지능형 Note On
 * patch: FM 채널에 적용할 패치 (PSG는 현재 설정 유지)
 *
 * [Rev.4] Re-trigger 로직 추가:
 *   서스테인 페달이 눌린 상태에서 같은 음을 다시 치면,
 *   새 채널을 소모하지 않고 기존 채널을 재사용합니다.
 *   (채널 3개짜리 YM2203에서 서스테인+반복 연타로 인한 채널 고갈 방지)
 */
static inline void vm_note_on(voice_mgr_t* vm,
    uint8_t note, uint8_t vel,
    const ym2203_patch_t* fm_patch)
{
    /* Re-trigger: 서스테인 중 같은 음이 이미 발음 중이면 해당 채널 재사용 */
    if (vm->sustain_pedal) {
        for (int i = 0; i < VM_TOTAL; i++) {
            if (vm->voices[i].on && vm->voices[i].note == note) {
                vm->voices[i].sustain = 0u;
                vm->voices[i].vel = vel;
                vm->voices[i].age = 0u;
                if (i < VM_FM_CH) {
                    ym2203_key_off((uint8_t)i);
                    ym_patch_vol(fm_patch, (uint8_t)i, vel, 15u);
                    ym_set_note_ex((uint8_t)i, note, 0, 0);
                    ym2203_key_on((uint8_t)i, 0x0Fu);
                }
                else {
                    uint8_t ssg_ch = (uint8_t)(i - VM_FM_CH);
                    ym2203_ssg_set_note(ssg_ch, note);
                    ym2203_ssg_set_vol(ssg_ch, (uint8_t)(vel >> 3u));
                }
                return;
            }
        }
    }

    /* 우선 FM 할당 시도 */
    int ch = vm_alloc_ch(vm, 1);
    int is_fm = 1;
    if (ch < 0) {
        /* FM 불가 → PSG 폴백 */
        ch = vm_alloc_ch(vm, 0);
        is_fm = 0;
    }
    if (ch < 0) return;  /* 모든 채널 점유 + steal 불가 */

    vm_voice_t* v = &vm->voices[ch];
    /* 이전 음 Key Off */
    if (v->on) {
        if (is_fm) ym2203_key_off((uint8_t)ch);
        else       ym2203_ssg_ch_silence((uint8_t)(ch - VM_FM_CH));
    }
    v->note = note;
    v->vel = vel;
    v->priority = vm_voice_priority(vm, note);
    v->on = 1u;
    v->age = 0u;
    v->sustain = 0u;

    /* [Rev.5] 조 판별기 업데이트 ? 발음 전 Key 자동 갱신 */
    if (vm->use_key_det)
        kd_note_on(&vm->key_det, note, vel);

    if (is_fm) {
        ym_patch_vol(fm_patch, (uint8_t)ch, vel, 15u);
        ym_set_note_ex((uint8_t)ch, note, 0, 0);
        ym2203_key_on((uint8_t)ch, 0x0Fu);
    }
    else {
        uint8_t ssg_ch = (uint8_t)(ch - VM_FM_CH);
        ym2203_ssg_set_note(ssg_ch, note);
        ym2203_ssg_set_vol(ssg_ch, (uint8_t)(vel >> 3u));
    }
}

/*
 * vm_note_off ? Note Off (서스테인 페달 고려)
 */
static inline void vm_note_off(voice_mgr_t* vm, uint8_t note)
{
    for (int i = 0; i < VM_TOTAL; i++) {
        if (!vm->voices[i].on || vm->voices[i].note != note) continue;
        if (vm->sustain_pedal) {
            vm->voices[i].sustain = 1u;
        }
        else {
            if (i < VM_FM_CH) ym2203_key_off((uint8_t)i);
            else               ym2203_ssg_ch_silence((uint8_t)(i - VM_FM_CH));
            vm->voices[i].on = 0u;
        }
    }
}

/*
 * vm_cc ? CC 이벤트 처리
 * cc=64: 서스테인 페달
 * cc=120: All Sound Off (GM 규격) ? 즉각 강제 Key-Off
 * cc=123: All Notes Off (GM 규격) ? 서스테인 고려 후 종료
 *
 * [Rev.5] Note Hang 방지 강화:
 *   문제 1: 페달 해제 시 sustain=1이지만 on=0인 좀비 보이스가 남을 수 있음
 *           (vm_note_off가 sustain 중에 key_off를 안 하는 설계이므로
 *            on=0으로 이미 처리된 경우 sustain=1만 남는 엣지 케이스)
 *   해결:   페달 오프 시 on 여부와 무관하게 sustain=1인 모든 채널을 Key-Off
 *
 *   문제 2: 보이스 스틸링 시 steal된 채널의 on=1, note=이전값이 남아
 *           이후 Note-Off가 다른 채널의 같은 note를 끄는 오탐이 발생
 *   해결:   steal 시 vm_alloc_ch 내부에서 이전 노트 정보를 명시적으로 소거
 *           (vm_alloc_ch는 인라인이므로 여기서 vm_note_on 내 처리)
 */
static inline void vm_cc(voice_mgr_t* vm, uint8_t cc, uint8_t val)
{
    if (cc == 64u) {
        uint8_t pedal_on = (val >= 64u) ? 1u : 0u;
        vm->sustain_pedal = pedal_on;

        if (!pedal_on) {
            /* 페달 오프: sustain 플래그가 있는 채널 무조건 Key-Off
             * on=0인 경우도 포함 ? 좀비 sustain 보이스 완전 소거 */
            for (int i = 0; i < VM_TOTAL; i++) {
                if (!vm->voices[i].sustain) continue;
                if (i < VM_FM_CH) ym2203_key_off((uint8_t)i);
                else               ym2203_ssg_ch_silence((uint8_t)(i - VM_FM_CH));
                vm->voices[i].on = 0u;
                vm->voices[i].sustain = 0u;
                vm->voices[i].note = 0u;
            }
        }
    }
    else if (cc == 120u) {
        /* All Sound Off ? 즉각 강제 소거 (서스테인 무시) */
        for (int i = 0; i < VM_TOTAL; i++) {
            if (i < VM_FM_CH) ym2203_key_off((uint8_t)i);
            else               ym2203_ssg_ch_silence((uint8_t)(i - VM_FM_CH));
            vm->voices[i].on = 0u;
            vm->voices[i].sustain = 0u;
            vm->voices[i].note = 0u;
        }
        vm->sustain_pedal = 0u;
    }
    else if (cc == 123u) {
        /* All Notes Off ? 서스테인 중이면 sustain 플래그만 제거, 즉시 Key-Off */
        vm->sustain_pedal = 0u;
        for (int i = 0; i < VM_TOTAL; i++) {
            if (!vm->voices[i].on) continue;
            if (i < VM_FM_CH) ym2203_key_off((uint8_t)i);
            else               ym2203_ssg_ch_silence((uint8_t)(i - VM_FM_CH));
            vm->voices[i].on = 0u;
            vm->voices[i].sustain = 0u;
            vm->voices[i].note = 0u;
        }
    }
}

/*
 * vm_panic_off ? 비상 모든 채널 강제 소거
 *
 * MIDI 케이블 재연결, 모드 전환, 에러 복구 등 상태가 불확실할 때 호출.
 * 보이스 매니저 상태를 완전 리셋하고 하드웨어 Key-Off를 강제 실행합니다.
 *
 * [Rev.5] Note Hang의 최후 수단:
 *   · 3개 FM 채널 모두 Key-Off 레지스터에 직접 0x00 기록
 *   · 3개 SSG 채널 볼륨 레지스터 0으로 초기화
 *   · voice_mgr_t 상태 완전 zeroing
 */
static inline void vm_panic_off(voice_mgr_t* vm)
{
    /* FM: 채널 0~2 Key-Off (레지스터 0x28, bits[3:0]=채널 mask 0x0F) */
    for (int i = 0; i < VM_FM_CH; i++) {
        ym_write(0x28u, (uint8_t)i);          /* Key-Off: all ops */
    }
    /* SSG: 채널 0~2 볼륨 0 */
    for (int i = 0; i < VM_PSG_CH; i++) {
        ym2203_ssg_ch_silence((uint8_t)i);
    }
    /* 상태 완전 리셋 */
    memset(vm, 0, sizeof(*vm));
}

/* vm_tick ? 매 틱 age 카운터 증가 */
static inline void vm_tick(voice_mgr_t* vm)
{
    for (int i = 0; i < VM_TOTAL; i++)
        if (vm->voices[i].on && vm->voices[i].age < 0xFFFFu)
            vm->voices[i].age++;
}

/*
 * vm_tick_full ? [Rev.6] voice_mgr + patches.h 통합 틱
 *
 * Rev.7 patches.h의 ym2203_tick()은 다음을 처리합니다:
 *   · FM 채널 LFO vibrato/tremolo 업데이트 (ym2203_voice_tick)
 *   · SSG cut_ticks 자동 소거 (ym2203_ssg_tick)
 *   · SSG attack_boost 전환 처리
 *
 * multi.h의 vm_tick()은:
 *   · 보이스 age 카운터 증가 (voice stealing 기준)
 *
 * 이 함수는 두 가지를 한 번에 호출합니다.
 * RT 루프에서 vm_tick() 대신 vm_tick_full()을 사용하면 됩니다.
 *
 *   while (running) {
 *       vm_tick_full(&g_vm);          // 보이스 age + FM LFO + SSG 틱
 *       seq_tick(&g_seq);             // 시퀀서
 *       ym_rt_sleep_until(&next, 10000000LL);
 *   }
 */
static inline void vm_tick_full(voice_mgr_t* vm)
{
    vm_tick(vm);
    ym2203_tick();  /* patches.h Rev.7 FM LFO + SSG cut/attack 처리 */
}

// =============================================================================
//  §5H  [Rev.3] MIDI 퍼포먼스 매핑
// =============================================================================
//
//  단일 구조체로 Velocity·Aftertouch·PitchBend·ModWheel를 일괄 처리.
// =============================================================================

typedef struct {
    /* Velocity 분리 매핑 */
    uint8_t  vel_car_ratio;   /* 캐리어 TL 벨로시티 감도 0~8    */
    uint8_t  vel_mod_ratio;   /* 모듈레이터 TL 벨로시티 감도 0~8 */
    /* Aftertouch → 농음 깊이 */
    nonlinear_lfo_t* at_lfo;  /* NULL이면 비활성               */
    uint8_t  at_lfo_ch;       /* 적용 FM 채널                   */
    uint8_t  at_val;          /* 현재 Aftertouch 값             */
    /* Pitch Bend → JI 오프셋 병합 */
    int16_t  pb_cent;         /* 현재 피치벤드 cent (-200~+200) */
    uint8_t  pb_semitones;    /* 벤드 범위 반음 수 (기본 2)     */
    /* Mod Wheel → FM Feedback */
    uint8_t  mw_ch;           /* 대상 채널                      */
    uint8_t  mw_base_fb;      /* 기본 FB                        */
    uint8_t  mw_max_fb;       /* 최대 FB                        */
    uint8_t  mw_val;          /* 현재 ModWheel 값               */
} midi_perf_t;

static inline void mp_init(midi_perf_t* mp,
    uint8_t car_r, uint8_t mod_r,
    uint8_t pb_semi, uint8_t mw_ch,
    uint8_t base_fb, uint8_t max_fb)
{
    memset(mp, 0, sizeof(*mp));
    mp->vel_car_ratio = car_r;
    mp->vel_mod_ratio = mod_r;
    mp->pb_semitones = pb_semi;
    mp->mw_ch = mw_ch;
    mp->mw_base_fb = base_fb;
    mp->mw_max_fb = max_fb;
}

/* CC 이벤트 처리 */
static inline void mp_cc(midi_perf_t* mp,
    uint8_t cc, uint8_t val,
    const ym2203_patch_t* patch)
{
    if (cc == 1u) {
        /* Mod Wheel → FB 변조 */
        mp->mw_val = val;
        uint8_t fb = mp->mw_base_fb
            + (uint8_t)((uint32_t)(mp->mw_max_fb - mp->mw_base_fb)
                * val / 127u);
        if (fb > 7u) fb = 7u;
        ym_write((uint8_t)(0xB0u + mp->mw_ch),
            (uint8_t)((fb << 3u) | (patch->ALG & 7u)));
    }
}

/* Pitch Bend 처리 (-8192~+8191 → cent 변환) */
static inline void mp_pitch_bend(midi_perf_t* mp,
    int16_t pb_raw, uint8_t ch)
{
    mp->pb_cent = (int16_t)((int32_t)pb_raw
        * (int32_t)mp->pb_semitones
        * 100 / 8192);
    /* 실제 FNUM 업데이트는 호출자가 현재 노트로 ym_set_note_ex() 재호출해야 합                                                                                                                                                                                                                                니다.
       mp_tick()에서 at_lfo가 활성화된 경우 자동으로 pb_cent가 합산됩니다. */
    (void)ch;
}

/* Aftertouch → 농음 LFO 깊이 실시간 제어 */
static inline void mp_aftertouch(midi_perf_t* mp,
    uint8_t at_val, uint8_t ch)
{
    mp->at_val = at_val;
    mp->at_lfo_ch = ch;
    if (mp->at_lfo) {
        /* AT가 높을수록 농음 깊어짐: depth_up 스케일 */
        int16_t depth = (int16_t)((uint32_t)mp->at_lfo->depth_up
            * at_val / 127u);
        mp->at_lfo->depth_up = depth;
        mp->at_lfo->active = (at_val > 0u) ? 1u : 0u;
    }
}

/*
 * mp_tick ? 매 틱 호출: Aftertouch 농음 FNUM 적용
 * curr_note: 현재 채널의 MIDI 노트
 * [Rev.4] ym_fnum_cents_blk() 사용 → 고음역 AT 농음 꺾임 방지
 */
static inline void mp_tick(midi_perf_t* mp, uint8_t curr_note)
{
    if (!mp->at_lfo || !mp->at_lfo->active) return;
    int16_t lfo_cent = nlfo_tick(mp->at_lfo);
    lfo_cent += mp->pb_cent;
    const ym2203_fnum_t* f = &YM2203_FREQ_LUT[curr_note < 128u ? curr_note : 127u];
    uint16_t base_fnum = (uint16_t)(f->lsb | ((uint16_t)(f->msb & 0x07u) << 8));
    uint8_t  base_blk = (uint8_t)((f->msb >> 3u) & 0x07u);
    uint16_t fnum; uint8_t blk;
    ym_fnum_cents_blk(base_fnum, base_blk, lfo_cent, &fnum, &blk);
    ym_set_fnum_raw(mp->at_lfo_ch, fnum, blk);
}

// =============================================================================
//  §5Z  [Rev.6] FM 패치 / PSG 설정 검증 (Rev.7 patches.h 상호 보완)
// =============================================================================
//
//  [Rev.7 patches.h와의 인터페이스 변경점]
//
//  1. 타입명: ym2203_freq_t → ym2203_fnum_t (Rev.7 기준으로 통일됨)
//
//  2. ym2203_patch_t.vel_tl_scale 필드 추가 (Rev.7):
//     · 0 = velocity 무시 (오르간 등 고정 음량 악기)
//     · 3~4 = 표준 터치 감도
//     · 5~6 = 드럼/강한 어택 악기
//     · ym_patch_vol()이 이 값을 인식해 LUT 감쇄량을 비례 적용합니다.
//     · 기존 full_inst_t.fm[] 패치들은 vel_tl_scale=3(기본값)으로 동작합니다.
//
//  3. ym2203_ssg_note_on() (Rev.7):
//     · mixer, noise period, envelope, attack_boost, cut_ticks를 단일 호출로                                                                                                                                                                                                                                 처리.
//     · fi_note_on(), fi_note_on_v()에서 구버전 ssg_patch_apply_ch() 대신 사용                                                                                                                                                                                                                               .
//     · SSG 틱 처리는 ym2203_tick() 내 ym2203_ssg_tick()이 자동 처리.
//
//  4. SSG_NOISE_BREATH 프리셋 부재:
//     patches.h §5Z 권고사항: 취구악기(대금/피리/소금/단소)에서
//     SSG_NOISE_SNARE를 취구 노이즈로 전용하는 것은 음색적으로 부적합합니다.
//     스네어는 Tone+Noise 혼합이지만, 취구 노이즈는 Noise 단독 + 긴 유지가 필요                                                                                                                                                                                                                               합니다.
//
//     권장: ym2203_patches.h에 아래 프리셋을 추가하고 full_inst_t.ssg_p[]를 교                                                                                                                                                                                                                                체:
//
//       /* SSG_NOISE_BREATH: 취구 노이즈 (Noise 단독, 긴 지속) */
//       { "SSG_BREATH", 0, 1, 0x10,  8, 0x0000, 0x00, 128, 0, 0, 0 }
//       /* use_tone=0, use_noise=1, noise_period=0x10(중간), amp=8(중간 볼륨) *                                                                                                                                                                                                                               /
//
//     현재 FULL_DAEGEUM, FULL_PIRI, FULL_SOGEUM, FULL_DANSO의
//     ssg_p[0]=SSG_NOISE_SNARE → SSG_NOISE_BREATH로 교체 권장.
//
//  5. ym2203_fnum_to_linear() / ym2203_linear_to_fnum() (Rev.7):
//     · portamento_t.lp_src/lp_dst에서 크로스-블록 글라이드에 사용.
//     · pt_start(), pt_tick()에 완전 통합되었습니다.
//
//  qq DUAL/TRIPLE/국악기 패치 검증 내용은 Rev.4 §5Z와 동일 qqqqqqqqqqqqqq
//
//  qq DUAL 패치 이슈 qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq
//
//  DUAL_PIANO (ALG=5, ch_a/ch_b 모두 ALG=3):
//    ALG 불일치: dual_patch_t.alg=V8_ALG5이나 실제 ch_a.ALG=3, ch_b.ALG=3
//    → V8_ALG5는 시각적 설명용이며, 실제 레지스터에는 ch_a.ALG=3이 씌워집니다.
//    → 의도된 설계라면 문제 없음. 단, V8_ALG 필드와 실제 패치 ALG를 일치시키면
//      코드 가독성이 향상됩니다.
//    ch_b OP4의 DT2=1, MULT=6, D1R=30 → 빠른 decay와 고배음. 피아노 어택음에                                                                                                                                                                                                                                 적합.
//    ? 전체적으로 피아노 패치로 기능적으로 타당합니다.
//
//  DUAL_EPIANO (ch_b ALG=7):
//    ALG=7은 4OP 모두 캐리어(병렬 구조). TL이 20~26 수준으로 설정되어
//    볼륨이 다소 낮을 수 있습니다. EP 특유의 Bell-tone을 위한 의도된 설정으로                                                                                                                                                                                                                                 보임.
//    OP4 D1R=30, D2R=24: 빠른 감쇄 → 전기피아노 특성 ?
//
//  DUAL_STRINGS (ALG=5, LFO 활성):
//    lfo_hz100=550 → 5.5Hz 비브라토. 현악기 적정 범위(4~6Hz) ?
//    ch_a/ch_b KS=3: 고음역에서 빠른 decay. 현악기의 자연 감쇄 ?
//    det_a=+7, det_b=-7: ±7 cent 코러스 효과. 앙상블 질감 ?
//
//  DUAL_BRASS (ALG=4, ALG=4):
//    OP TL(캐리어) 모두 0: 항상 최대 음량. 벨로시티 LUT 적용 시
//    VEL_TO_TL_ATTN[vel]이 그대로 TL에 더해져 정상 동작.
//    AR=0, D1R=0 on some OPs: 즉각 최대값 → 브라스 특유의 강한 어택 ?
//    ? D1SL=15 on multiple OPs: Sustain Level 최저(가장 긴 D1 구간).
//      RR=15: 빠른 릴리즈. 의도된 설정이나 staccato 느낌이 강할 수 있음.
//
//  DUAL_ORGAN (ALG=7, FB=0):
//    FB=0: 피드백 없음. 순수한 사인파 오르간 ?
//    AR=0, D1R=0: 즉각 발음/지속. 오르간 특성 ?
//    RR=0: 릴리즈 없음 (Key-Off 즉시 소음). 이것이 오르간의 특성이나
//    실제로는 RR=1~2를 주면 클릭 노이즈를 방지할 수 있습니다.
//    ? OR_B의 MULT 값들이 8, 10, 12, 16으로 고배음 중심. 이 구성은
//      Hammond B3의 drawbar 상위 레지스터(5⅓', 2⅔', 2', 1')에 해당.
//      OR_A의 MULT 1, 2, 4, 6과 조합하면 풍성한 오르간 음색이 됩니다 ?
//
//  DUAL_OVERDRIVE (ALG=3, FB=7/5):
//    FB=7: 최대 피드백. 강렬한 디스토션 ?
//    ch_a OP1 DT1=2: 약간의 디튠 → 두꺼운 톤 ?
//    ch_b OP1 DT1=3, OP2 DT1=5: 더 강한 디튠 → 두 채널 합산 시 풍성한 오버드라                                                                                                                                                                                                                               이브 ?
//    det_b=-12: 한 옥타브 아래 → 서브 옥타브 추가로 두터운 기타 사운드 ?
//
//  DUAL_BASS (ALG=0):
//    ALG=0: 직렬 FM. TL(OP1~3 모두 낮음) → 강한 FM 변조로 저음 배음 풍부 ?
//    SR(D2R) = 0: 서스테인 레벨 유지 → 베이스 지속음 ?
//    ? BS_B의 AR 값들이 22~28로 BS_A(26~38)보다 느린 어택.
//      혼합 시 어택 불일치가 발생할 수 있습니다. 실용상 문제는 적음.
//
//  DUAL_PERC (ALG=6, ALG=7):
//    ALG=6/7 + SL=15, RR=8~15: 빠른 감쇄 → 타악기 특성 ?
//    DT1=0, MULT 다양: 벨 계열 배음 구성 ?
//    det_b=+200: 200cent = 2반음 위. 타악기 음정감 추가. 의도적 설정 ?
//    ? PC_B의 MULT=8: 8배음은 극단적. 아주 높은 배음 계열 → 심벌 질감에 가까움                                                                                                                                                                                                                               .
//
//  qq TRIPLE 패치 이슈 qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq
//
//  TRIPLE_DEEP_BASS: DB_C의 AR값들이 22~26으로 A/B에 비해 느림.
//    3채널 혼합 시 어택이 층층이 겹쳐 자연스러운 서브베이스 느낌을 줄 수 있음.
//    의도된 설계로 판단. ?
//
//  TRIPLE_DRUM_KIT: DRUM 3채널 모두 SL=15(최대 D1 구간), RR=8~15.
//    ? TOM의 SL=14: 다른 채널(15)과 약간 다름. 의도적이라면 OK,
//      실수라면 15로 통일 권장.
//
//  qq 국악기 full_inst_t 이슈 qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq
//
//  FULL_DAEGEUM:
//    DG_P (ALG=5, FB=3): 플루트계 배음. KS=0으로 전 음역 균일한 어택 ?
//    DG_J (ALG=1, FB=2): 저배음 변조. 대금 저음역의 두터운 질감 ?
//    ssg_mask=0x01, SSG_NOISE_SNARE + amp=5: 취구음(바람 소리) 표현. ?
//    ssg_noff[0]=+12: SSG를 한 옥타브 위에서 노이즈 → 고주파 취구 노이즈 ?
//    ? DG_P의 OP4 TL=26, DG_J OP4 TL=18로 FM1 패치에만 sw_op=0 적용됨.
//      swell 효과가 FM 채널 0(ch=0)에만 걸리는 구조 ? 의도된 설계.
//
//  FULL_SOGEUM:
//    ALG=5, FB=2: 소금의 청명한 고음 표현. DG_P보다 TL 높음(조용한 음색) ?
//    ssg_amp=4로 취구 노이즈 약하게 → 소금의 섬세함 ?
//    ? lfo_hz100=450 (4.5Hz): 소금의 빠른 농음 특성(5~7Hz)에는 약간 느립니다.
//      450 → 600 정도로 올리면 더 사실적입니다.
//
//  FULL_PIRI:
//    ALG=4, FB=6 (PI_A): 강한 피드백으로 피리의 독특한 배음 ?
//    PI_B ALG=1, FB=4: 하드모드 FM 변조 ? 피리의 거친 음색 ?
//    ssg_mask=0x03: SSG 채널 0, 1 동시 사용. amp 6+8 → 피리 노이즈 풍성 ?
//    ssg_noff[1]=+12: SSG ch1을 옥타브 위에서 → 배음 강조 ?
//
//  FULL_GEOMUNGO:
//    GM_A ALG=3, FB=3: 거문고 현 어택의 복잡한 배음 ?
//    GM_B ALG=3, FB=2: 서서히 감쇄하는 공명 ?
//    sw_op=0, sw_tl0=4, sw_tl1=16, sw_ticks=30: 어택 후 점진적 TL 상승.
//      발음 직후 밝았다가 어두워지는 거문고 음색 변화 ?
//    ssg_mask=0x01, SSG_NOISE_HIHAT: 현 튕기는 어택 노이즈 ?
//    ? KS=1(GM_A/B 공통): 고음역에서 어택 빨라짐. 거문고 특성상 OK.
//
//  FULL_AJAENG:
//    AJ_A ALG=1, FB=5: 강한 활 마찰음. KS=4→고음 빠른 감쇄 ?
//    ? KS=4(OP1, OP3): KS 최대값. 고음역에서 AR이 매우 빨라져
//      5~6 옥타브에서는 거의 즉발음이 됩니다. 아쟁의 특성상 중저음 악기이므로
//      사용 음역을 고려하면 문제 없습니다.
//    ssg_mask=0x03: SSG ch0+ch1 사용. amp 4+6 → 활 마찰 노이즈 ?
//    ssg_noff[1]=+7: 완전5도 위 노이즈 → 활 소리 배음 강조 ?
//
//  FULL_DANSO:
//    DS_A ALG=5, FB=1: 단순 배음. 단소의 청결한 음색 ?
//    OP1 TL=34(높음): 낮은 변조 깊이 → 순수음에 가까운 배음 ?
//    ssg_amp=3(낮음): 미세한 취구 노이즈 ?
//    ? lfo_hz100=450: 단소의 비교적 빠른 농음에는 다소 느릴 수 있음.
//
//  FULL_HUN (훈):
//    HN_A ALG=5, FB=0: 피드백 없음 → 훈의 부드럽고 둥근 음색 ?
//    ssg_mask=0x00: PSG 미사용. 취구 노이즈 없음 → 훈의 은은한 음색에 맞음 ?
//    lfo_hz100=400(4.0Hz): 훈의 느리고 깊은 요성 ?
//
//  FULL_GAYAGEUM:
//    GY_A/B: 거문고 패치와 유사하나 TL 전체적으로 약간 낮아 좀 더 밝음 ?
//    sw_op=0, sw_tl0=3, sw_tl1=14, sw_ticks=25: 가야금의 날카로운 어택
//      후 빠른 음색 변화 ?
//    ? GY_B의 AR 값들(KS1, AR2, D1R7~9): 느린 어택+빠른 D1R.
//      가야금 현 공명 시뮬레이션 의도로 보이나, 실제로는 채널 B의 소리가
//      채널 A 이후 늦게 나올 수 있습니다. 의도한 경우 ?.
//
//  FULL_HAEGEUM:
//    HG_A ALG=1, FB=6: 강한 피드백 FM. 해금의 거친 활 소리 ?
//    KS=3(OP1/3): 고음 빠른 어택 → 해금의 날카로운 고음 표현 ?
//    ssg_mask=0x03: SSG 2채널. amp 5+7 → 풍부한 활 노이즈 ?
//    ? lfo_hz100=480: 해금의 요성 속도로 적합하나, 해금은 비브라토 깊이가
//      매우 커야 합니다. NLFO_JOONG 프리셋(±80cent) 사용 권장.
//
//  qq PSG 설정 공통 이슈 qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq
//
//  SSG_NOISE_SNARE가 취구 노이즈로 사용되는 점:
//    실제 스네어 노이즈는 Tone+Noise 혼합, 취구 노이즈는 Noise 단독이 더 자연스                                                                                                                                                                                                                               럽습니다.
//    ym2203_patches.h에서 SSG_NOISE_BREATH 같은 별도 프리셋을 정의하여
//    Tone disable, Noise enable, 긴 AR/DR로 지속 노이즈를 만드는 것을 권장합니                                                                                                                                                                                                                                다.
//
//  SSG 벨로시티 스케일링:
//    vm_note_on()에서 `vel >> 3`으로 PSG 볼륨(0~15)을 설정하고 있습니다.
//    이것도 선형 매핑이므로 `VEL_TO_TL_ATTN[vel]을 이용해
//    ssg_vol = 15 - (VEL_TO_TL_ATTN[vel] * 15 / 40)` 방식으로
//    로그 스케일 적용을 권장합니다.
//
// =============================================================================

// =============================================================================
//  §6  가상 8OP 듀얼 패치 데이터
// =============================================================================

static const dual_patch_t DUAL_PIANO = {
    "DUAL_PIANO", V8_ALG5,
    {"P_A",3,5,3,{{0,8,2,2,31,0,18,0,8,12,0},{0,1,5,2,31,0,15,8,7,12,0},
                       {0,4,8,2,31,0,20,0,6,10,0},{0,2,4,2,31,0,14,6,6,10,0}}},
    {"P_B",3,4,3,{{0,2,18,1,31,0,22,10,5,10,0},{0,1,8,1,31,0,18,16,6,10,0},
                       {0,3,22,1,31,0,24,8,6,10,0},{0,1,6,1,31,0,20,14,6,10,0}}}                                                                                                                                                                                                                               ,
    0,+8, 16,14, 0,0,0, 0,2,26,20
};

static const dual_patch_t DUAL_EPIANO = {
    "DUAL_EPIANO", V8_ALG1,
    {"EP_A",0,0,3,{{0,14,8,1,31,0,22,0,8,12,0},{0,7,18,1,28,0,20,0,7,11,0},
                        {0,3,28,1,26,0,18,0,6,10,0},{0,1,4,1,31,0,16,16,5,9,0}}}                                                                                                                                                                                                                               ,
    {"EP_B",7,0,3,{{0,1,18,1,31,0,20,18,6,10,0},{0,2,22,1,31,0,22,20,7,10,0},
                        {0,3,26,1,31,0,24,22,8,10,0},{0,4,30,1,31,0,26,24,9,10,0                                                                                                                                                                                                                               }}},
    0,+3, 16,10, 0,0,0, 0,8,30,35
};

static const dual_patch_t DUAL_STRINGS = {
    "DUAL_STRINGS", V8_ALG5,
    {"ST_A",1,6,3,{{3,6,16,0,28,0,10,18,2,8,0},{5,2,10,0,28,0,12,16,3,8,0},
                        {3,1,22,0,26,0,14,14,4,8,0},{5,1,4,0,26,0,8,20,1,7,0}}},
    {"ST_B",1,5,3,{{2,6,18,0,27,0,11,17,2,8,0},{4,2,12,0,27,0,13,15,3,8,0},
                        {2,1,24,0,25,0,15,13,4,8,0},{4,1,6,0,25,0,9,19,1,7,0}}},
    +7,-7, 15,15, 4,2,550, 0,0,0,0
};

static const dual_patch_t DUAL_BRASS = {
    "DUAL_BRASS", V8_ALG2,
    {"BR_A",4,7,3,{{0,8,0,0,31,0,0,27,0,15,0},{0,6,4,0,31,0,0,22,0,15,0},
                        {0,4,0,0,31,0,0,24,0,15,0},{0,2,5,0,31,0,0,18,0,15,0}}},
    {"BR_B",4,6,3,{{0,4,6,0,29,0,0,25,0,14,0},{0,3,8,0,29,0,0,20,0,14,0},
                        {0,2,6,0,29,0,0,22,0,14,0},{0,1,10,0,29,0,0,16,0,14,0}}}                                                                                                                                                                                                                               ,
    0,+5, 16,13, 0,0,0, 0,0,0,0
};

static const dual_patch_t DUAL_ORGAN = {
    "DUAL_ORGAN", V8_ALG7,
    /* [Rev.4] RR 0→1: Key-Off 클릭 노이즈 방지 (오르간은 0이어도 되지만 실용상                                                                                                                                                                                                                                1 권장) */
    {"OR_A",7,0,3,{{0,1,10,0,31,0,0,0,0,15,1},{0,2,12,0,31,0,0,0,0,15,1},
                        {0,4,16,0,31,0,0,0,0,15,1},{0,6,20,0,31,0,0,0,0,15,1}}},
    {"OR_B",7,0,3,{{0,8,18,0,31,0,0,0,0,15,1},{0,10,22,0,31,0,0,0,0,15,1},
                        {0,12,26,0,31,0,0,0,0,15,1},{0,16,30,0,31,0,0,0,0,15,1}}                                                                                                                                                                                                                               },
    0,0, 16,14, 0,0,0, 0,0,0,0
};

static const dual_patch_t DUAL_CHOIR = {
    "DUAL_CHOIR", V8_ALG4,
    {"CH_A",5,3,3,{{0,3,28,0,24,0,12,4,1,10,0},{0,1,10,0,22,0,8,0,0,10,0},
                        {0,2,14,0,22,0,10,0,0,10,0},{0,4,18,0,22,0,12,0,0,10,0}}                                                                                                                                                                                                                               },
    {"CH_B",5,2,3,{{0,2,30,0,22,0,14,6,2,10,0},{0,1,12,0,20,0,10,0,0,10,0},
                        {0,2,16,0,20,0,12,0,0,10,0},{0,3,20,0,20,0,14,0,0,10,0}}                                                                                                                                                                                                                               },
    +5,-5, 15,14, 3,1,450, 0,0,0,0
};

static const dual_patch_t DUAL_OVERDRIVE = {
    "DUAL_OVERDRIVE", V8_ALG3,
    {"OD_A",1,7,3,{{0,15,12,0,31,0,12,8,2,8,0},{0,2,2,0,31,0,14,10,3,8,0},
                        {2,1,18,0,31,0,16,6,4,8,0},{0,1,6,0,31,0,0,0,0,8,0}}},
    {"OD_B",1,5,3,{{3,15,16,0,31,0,13,9,2,8,0},{5,2,4,0,31,0,15,11,3,8,0},
                        {1,1,20,0,31,0,17,7,4,8,0},{7,1,8,0,31,0,0,0,0,8,0}}},
    0,-12, 16,14, 2,1,350, 0,0,0,0
};

static const dual_patch_t DUAL_BASS = {
    "DUAL_BASS", V8_ALG0,
    {"BS_A",0,7,3,{{0,1,20,0,28,0,8,0,2,14,0},{0,2,30,0,31,0,6,0,1,14,0},
                        {0,1,36,0,31,0,4,0,0,14,0},{0,1,5,0,31,0,8,0,1,14,0}}},
    {"BS_B",0,6,3,{{0,1,24,0,22,0,10,0,3,12,0},{0,2,32,0,26,0,8,0,2,12,0},
                        {0,1,38,0,28,0,6,0,1,12,0},{0,1,8,0,28,0,10,0,2,12,0}}},
    0,-5, 16,12, 0,0,0, 0,0,0,0
};

static const dual_patch_t DUAL_PERC = {
    "DUAL_PERC", V8_ALG6,
    {"PC_A",2,0,3,{{0,1,6,0,31,0,20,0,15,12,8},{0,2,10,0,31,0,22,0,15,12,8},
                        {0,3,14,0,31,0,24,0,15,12,0},{0,1,4,0,31,0,18,0,15,12,8}                                                                                                                                                                                                                               }},
    {"PC_B",7,0,3,{{0,1,8,0,31,0,22,0,15,12,8},{0,2,10,0,31,0,24,0,15,12,8},
                        {0,4,14,0,31,0,26,0,15,12,8},{0,8,18,0,31,0,28,0,15,12,8                                                                                                                                                                                                                               }}},
    0,+200, 15,12, 0,0,0, 0,0,0,0
};

// =============================================================================
//  §7  가상 12OP 트리플 패치
// =============================================================================

static const triple_patch_t TRIPLE_ORCH_STRINGS = {
    "TRIPLE_ORCH_STR", V12_ALG3,
    {
        {"VLN",1,6,3,{{3,6,14,0,28,0,10,18,2,8,0},{5,2,8,0,28,0,12,16,3,8,0},
                           {3,1,20,0,26,0,14,14,4,8,0},{5,1,2,0,26,0,8,20,1,7,0}                                                                                                                                                                                                                               }},
        {"VLA",1,5,3,{{3,4,18,0,26,0,12,16,3,8,0},{5,1,12,0,26,0,14,14,4,8,0},
                           {3,1,24,0,24,0,16,12,5,8,0},{0,1,6,0,24,0,10,18,2,7,0                                                                                                                                                                                                                               }}},
        {"CEL",1,4,3,{{3,3,20,0,24,0,14,14,4,8,0},{5,1,14,0,24,0,16,12,5,8,0},
                           {3,1,26,0,22,0,18,10,6,8,0},{0,1,8,0,22,0,12,16,3,7,0                                                                                                                                                                                                                               }}},
    },
    {+12,0,-7}, {0,0,0}, {15,14,13}, 4,2,500
};

static const triple_patch_t TRIPLE_PIPE_ORGAN = {
    "TRIPLE_PIPE_ORG", V12_ALG2,
    {
        {"PO_A",5,0,3,{{0,1,8,0,31,0,0,0,0,15,0},{0,2,12,0,31,0,0,0,0,15,0},
                            {0,4,16,0,31,0,0,0,0,15,0},{0,8,20,0,31,0,0,0,0,15,0                                                                                                                                                                                                                               }}},
        {"PO_B",5,0,3,{{0,1,10,0,31,0,0,0,0,15,0},{0,6,16,0,31,0,0,0,0,15,0},
                            {0,10,20,0,31,0,0,0,0,15,0},{0,12,24,0,31,0,0,0,0,15                                                                                                                                                                                                                               ,0}}},
        {"PO_C",5,0,3,{{0,1,6,0,31,0,0,0,0,15,0},{0,3,14,0,31,0,0,0,0,15,0},
                            {0,5,18,0,31,0,0,0,0,15,0},{0,7,22,0,31,0,0,0,0,15,0                                                                                                                                                                                                                               }}},
    },
    {0,0,0}, {0,0,0}, {16,14,12}, 0,0,0
};

static const triple_patch_t TRIPLE_RICH_LEAD = {
    "TRIPLE_RICH_LEAD", V12_ALG1,
    {
        {"RL_A",1,7,3,{{0,11,20,0,31,0,18,10,3,9,0},{3,3,6,0,31,0,20,13,4,9,0},
                            {3,2,24,0,31,0,22,0,3,9,0},{7,1,8,0,31,0,0,0,0,10,0}                                                                                                                                                                                                                               }},
        {"RL_B",4,4,3,{{0,6,8,0,31,0,0,22,0,14,0},{0,4,12,0,31,0,0,18,0,14,0},
                            {0,3,8,0,31,0,0,20,0,14,0},{0,2,14,0,31,0,0,14,0,14,                                                                                                                                                                                                                               0}}},
        {"RL_C",7,0,3,{{0,1,20,0,31,0,0,0,0,15,0},{0,3,24,0,31,0,0,0,0,15,0},
                            {0,5,28,0,31,0,0,0,0,15,0},{0,7,30,0,31,0,0,0,0,15,0                                                                                                                                                                                                                               }}},
    },
    {0,+10,-8}, {0,0,0}, {16,12,8}, 3,1,500
};

static const triple_patch_t TRIPLE_DEEP_BASS = {
    "TRIPLE_DEEP_BASS", V12_ALG0,
    {
        {"DB_A",0,7,3,{{0,1,22,0,26,0,8,0,2,13,0},{0,2,30,0,28,0,6,0,1,13,0},
                            {0,1,36,0,30,0,4,0,0,13,0},{0,1,4,0,31,0,9,0,1,13,0}                                                                                                                                                                                                                               }},
        {"DB_B",0,7,3,{{0,1,24,0,24,0,10,0,3,12,0},{0,2,32,0,26,0,8,0,2,12,0},
                            {0,1,38,0,28,0,6,0,1,12,0},{0,1,6,0,28,0,11,0,2,12,0                                                                                                                                                                                                                               }}},
        {"DB_C",0,6,3,{{0,1,26,0,22,0,12,0,4,11,0},{0,2,34,0,24,0,10,0,3,11,0},
                            {0,1,40,0,26,0,8,0,2,11,0},{0,1,8,0,26,0,13,0,3,11,0                                                                                                                                                                                                                               }}},
    },
    {-5,0,+5}, {0,0,0}, {15,16,15}, 0,0,0
};

static const triple_patch_t TRIPLE_DRUM_KIT = {
    "TRIPLE_DRUM_KIT", V12_ALG4,
    {
        {"KIC",7,0,3,{{0,1,4,0,31,0,20,0,15,12,8},{0,1,8,0,31,0,22,0,15,12,8},
                           {0,2,16,0,31,0,18,0,15,12,0},{0,1,12,0,31,0,24,0,15,12,8}}},
        {"SNR",7,0,3,{{0,2,6,0,31,0,18,0,15,12,8},{0,3,10,0,31,0,20,0,15,12,8},
                           {0,1,12,0,31,0,22,0,15,12,0},{0,2,8,0,31,0,18,0,15,12,8}}},
        {"TOM",7,0,3,{{0,1,8,0,31,0,20,0,14,11,8},{0,2,12,0,31,0,22,0,14,11,8},
                           {0,3,16,0,31,0,24,0,14,11,0},{0,1,10,0,31,0,20,0,14,11,8}}},
    },
    {0,0,0}, {-24,-12,0}, {16,14,13}, 0,0,0
};

static const triple_patch_t TRIPLE_SYNTH_TEX = {
    "TRIPLE_SYNTH_TEX", V12_ALG5,
    {
        {"SB",4,6,3,{{0,6,0,0,31,0,0,26,0,14,0},{0,4,6,0,31,0,0,20,0,14,0},
                          {0,3,0,0,31,0,0,22,0,14,0},{0,2,8,0,31,0,0,16,0,14,0}}                                                                                                                                                                                                                               },
        {"SP",5,3,3,{{0,3,26,0,24,0,10,4,1,11,0},{0,1,8,0,24,0,8,0,0,11,0},
                          {0,2,14,0,22,0,9,2,1,11,0},{0,4,22,0,22,0,10,3,1,11,0}                                                                                                                                                                                                                               }},
        {"SL",1,7,3,{{0,11,22,0,31,0,18,10,3,9,0},{3,3,8,0,31,0,20,12,4,9,0},
                          {3,2,26,0,31,0,22,0,3,9,0},{7,1,8,0,31,0,0,0,0,10,0}}}                                                                                                                                                                                                                               ,
    },
    {0,+12,-12}, {0,0,0}, {14,12,16}, 5,2,600
};

// =============================================================================
//  §8  한국 전통악기
// =============================================================================

static const full_inst_t FULL_DAEGEUM = {
    "FULL_DAEGEUM", 2,
    {
        {"DG_P",5,3,3,{{0,3,34,0,23,0,9,3,1,11,0},{0,1,16,0,21,0,7,2,0,11,0},
                            {0,2,26,0,21,0,8,3,1,11,0},{0,4,11,0,20,0,9,4,1,10,                                                                                                                                                                                                                               0}}},
        {"DG_J",1,2,3,{{3,2,36,0,20,0,11,5,2,10,0},{5,1,22,0,19,0,13,9,3,10,0},
                            {0,1,30,0,18,0,15,7,4,10,0},{0,1,10,0,18,0,10,12,2,9,                                                                                                                                                                                                                               0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0,+5,0}, {0,-12,0}, {16,11,0},
    0,0,0,0,
    5,3,400,
    0x01,
    {SSG_NOISE_SNARE, SSG_SQUARE_SOFT, SSG_SQUARE_BRIGHT},
    {5, 0, 0},
    {12, 0, 0}
};

static const full_inst_t FULL_SOGEUM = {
    "FULL_SOGEUM", 1,
    {
        {"SG_A",5,2,3,{{0,2,36,0,25,0,9,2,0,12,0},{0,1,16,0,25,0,7,0,0,12,0},
                            {0,3,26,0,23,0,9,2,0,12,0},{0,5,10,0,21,0,11,3,0,11,                                                                                                                                                                                                                               0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0,0,0}, {0,0,0}, {16,0,0},
    0,0,0,0,
    /* [Rev.4] lfo_hz 450→580: 소금 농음 속도 현실화 (5~6Hz 대역) */
    3,1,580,
    0x01,
    {SSG_NOISE_SNARE, SSG_SQUARE_SOFT, SSG_SQUARE_BRIGHT},
    {4, 0, 0},
    {12, 0, 0}
};

static const full_inst_t FULL_PIRI = {
    "FULL_PIRI", 2,
    {
        {"PI_A",4,4,3,{{0,8,20,0,31,0,2,22,0,13,0},{0,6,18,0,31,0,0,18,0,13,0},
                            {0,4,20,0,31,0,0,20,0,13,0},{0,2,10,0,31,0,0,14,0,12,0                                                                                                                                                                                                                               }}},
        {"PI_B",1,4,3,{{0,4,32,0,27,0,10,7,2,10,0},{0,2,20,0,27,0,12,9,3,10,0},
                            {0,1,28,0,25,0,14,7,4,10,0},{0,1,10,0,25,0,8,12,2,9,0                                                                                                                                                                                                                               }}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0,+8,0}, {0,0,0}, {16,11,0},
    0,0,0,0,
    4,2,500,
    0x03,
    {SSG_NOISE_SNARE, SSG_SQUARE_SOFT, SSG_SQUARE_BRIGHT},
    {6, 8, 0},
    {0, 12, 0}
};

static const full_inst_t FULL_GEOMUNGO = {
    "FULL_GEOMUNGO", 2,
    {
        {"GM_A",3,3,3,{{0,6,3,1,31,0,22,0,9,13,8},{0,1,5,1,31,0,16,10,6,12,0},
                            {0,3,9,1,31,0,22,0,6,13,0},{0,1,7,1,31,0,18,8,5,12,0}}},
        {"GM_B",3,2,3,{{0,2,16,1,28,0,26,14,5,11,0},{0,1,8,1,28,0,22,20,6,11,0},
                            {0,4,20,1,28,0,28,8,5,11,0},{0,1,10,1,28,0,24,18,5,11,0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0,+3,0}, {0,0,0}, {16,14,0},
    0,4,16,30,
    0,0,0,
    0x01,
    {SSG_NOISE_HIHAT, SSG_SQUARE_BRIGHT, SSG_SQUARE_BRIGHT},
    {7, 0, 0},
    {0, 0, 0}
};

static const full_inst_t FULL_AJAENG = {
    "FULL_AJAENG", 1,
    {
        {"AJ_A",1,5,3,{{4,4,22,0,22,0,16,14,4,8,0},{6,2,14,0,22,0,18,12,5,8,0},
                            {4,1,28,0,20,0,20,10,6,8,0},{0,1,10,0,20,0,12,16,3,7                                                                                                                                                                                                                               ,0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0,0,0}, {0,0,0}, {16,0,0},
    0,0,0,0,
    5,2,480,
    0x03,
    {SSG_NOISE_SNARE, SSG_SQUARE_SOFT, SSG_SQUARE_BRIGHT},
    {4, 6, 0},
    {0, 7, 0}
};

static const full_inst_t FULL_DANSO = {
    "FULL_DANSO", 1,
    {
        {"DS_A",5,1,3,{{0,2,34,0,27,0,8,0,0,12,0},{0,1,6,0,27,0,6,0,0,12,0},
                            {0,3,22,0,25,0,8,0,0,12,0},{0,4,28,0,23,0,10,0,0,12,                                                                                                                                                                                                                               0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0,0,0}, {0,0,0}, {16,0,0},
    0,0,0,0,
    3,1,450,
    0x01,
    {SSG_NOISE_SNARE, SSG_SQUARE_BRIGHT, SSG_SQUARE_BRIGHT},
    {3, 0, 0},
    {12, 0, 0}
};

static const full_inst_t FULL_HUN = {
    "FULL_HUN", 1,
    {
        {"HN_A",5,0,3,{{0,1,28,0,22,0,10,4,1,11,0},{0,1,10,0,20,0,8,0,0,11,0},
                            {0,2,16,0,20,0,9,2,1,11,0},{0,3,22,0,18,0,10,3,1,11,                                                                                                                                                                                                                               0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0,0,0}, {0,0,0}, {16,0,0},
    0,0,0,0,
    2,1,400,
    0x00,
    {SSG_SQUARE_BRIGHT, SSG_SQUARE_BRIGHT, SSG_SQUARE_BRIGHT},
    {0, 0, 0}, {0, 0, 0}
};

static const full_inst_t FULL_GAYAGEUM = {
    "FULL_GAYAGEUM", 2,
    {
        {"GY_A",3,3,3,{{0,5,3,1,31,0,20,0,8,13,8},{0,1,5,1,31,0,16,10,6,12,0},
                            {0,3,8,1,31,0,22,0,6,13,0},{0,1,6,1,31,0,18,8,5,12,0                                                                                                                                                                                                                               }}},
        {"GY_B",3,2,3,{{0,2,16,1,29,0,24,12,5,11,0},{0,1,7,1,29,0,20,18,6,11,0},
                            {0,4,18,1,29,0,26,6,5,11,0},{0,1,9,1,29,0,22,16,5,11                                                                                                                                                                                                                               ,0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0,+5,0}, {0,0,0}, {16,13,0},
    0,3,14,25,
    0,0,0,
    0x01,
    {SSG_NOISE_HIHAT, SSG_SQUARE_BRIGHT, SSG_SQUARE_BRIGHT},
    {7, 0, 0}, {0, 0, 0}
};

static const full_inst_t FULL_HAEGEUM = {
    "FULL_HAEGEUM", 1,
    {
        {"HG_A",1,5,3,{{3,5,24,0,25,0,12,14,3,9,0},{5,2,18,0,25,0,14,12,4,9,0},
                            {3,1,30,0,23,0,16,10,5,9,0},{0,1,10,0,23,0,8,16,2,8,0                                                                                                                                                                                                                               }}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
        {"",7,0,3,{{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                        {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0,0,0}, {0,0,0}, {16,0,0},
    0,0,0,0,
    4,2,480,
    0x03,
    {SSG_NOISE_SNARE, SSG_SQUARE_SOFT, SSG_SQUARE_BRIGHT},
    {5, 7, 0}, {0, 0, 0}
};



// =============================================================================
//  §10A  [R9-CLEAN] 청음용 3FM + SSG 보강 full_inst_t 추가
//  - 기존 국악/화성학 구조는 보존하고, 원음이 거칠지 않은 기준 프리셋만 추가.
//  - FULL_CLEAN_* 는 FM3 전체를 사용하되 볼륨을 낮추고 SSG 보강도 약하게 둠.
// =============================================================================

static const full_inst_t FULL_CLEAN_ROUND_LEAD = {
    "FULL_CLEAN_ROUND_LEAD", 3,
    {
        {"CL_RL_A",1,2,4,{{0,2,42,0,28,0,12,0,2,10,0},{0,1,34,0,28,0,10,0,2,10,0},{0,1,50,0,27,0,8,0,1,10,0},{0,1,14,0,30,0,7,0,0,9,0}}},
        {"CL_RL_B",7,0,0,{{0,1,29,0,31,0,0,0,0,8,0},{0,2,31,0,31,0,0,0,0,8,0},{0,1,33,0,31,0,0,0,0,8,0},{0,1,24,0,31,0,0,0,0,8,0}}},
        {"CL_RL_C",5,1,3,{{0,1,54,0,22,0,7,0,0,10,0},{0,1,23,0,24,0,6,0,0,10,0},{0,2,58,0,21,0,7,0,0,10,0},{0,1,24,0,24,0,6,0,0,10,0}}},
    },
    {0,+4,-5}, {0,0,+12}, {13,9,7},
    0,0,0,0, 1,1,420,
    0x07, {SSG_SQUARE_SOFT, SSG_CHORD_LAYER, SSG_SQUARE_SOFT}, {4,3,2}, {0,12,7}
};

static const full_inst_t FULL_CLEAN_WARM_PAD = {
    "FULL_CLEAN_WARM_PAD", 3,
    {
        {"CL_PAD_A",6,1,3,{{0,1,50,0,18,0,7,0,3,10,0},{1,1,23,0,20,0,7,0,2,10,0},{5,1,25,0,19,0,8,0,2,10,0},{0,1,26,0,20,0,7,0,2,10,0}}},
        {"CL_PAD_B",4,1,3,{{0,1,52,0,18,0,7,0,4,10,0},{0,1,27,0,20,0,6,0,3,10,0},{0,2,56,0,18,0,7,0,4,10,0},{0,1,28,0,20,0,6,0,3,10,0}}},
        {"CL_PAD_C",7,0,2,{{0,1,33,0,20,0,7,0,2,10,0},{1,1,35,0,20,0,7,0,2,10,0},{5,1,37,0,20,0,7,0,2,10,0},{0,1,30,0,20,0,7,0,2,10,0}}},
    },
    {0,+6,-6}, {-12,0,+12}, {8,8,6},
    0,0,0,0, 3,1,500,
    0x07, {SSG_CHORD_LAYER, SSG_SQUARE_SOFT, SSG_CHORD_LAYER}, {3,2,2}, {-12,0,12}
};

static const full_inst_t FULL_CLEAN_MELLOW_PLUCK = {
    "FULL_CLEAN_MELLOW_PLUCK", 3,
    {
        {"CL_PLK_A",0,2,5,{{0,2,46,0,31,0,16,0,5,8,0},{0,3,38,0,31,0,14,0,5,8,0},{0,1,44,0,31,0,13,0,5,8,0},{0,1,16,0,31,0,12,0,5,8,0}}},
        {"CL_PLK_B",5,1,4,{{0,2,48,0,27,0,13,2,2,8,0},{0,1,23,0,28,0,10,4,1,8,0},{0,3,54,0,26,0,11,3,2,8,0},{0,1,18,0,30,0,9,5,1,8,0}}},
        {"CL_PLK_C",7,2,4,{{0,0,33,0,31,0,11,0,7,7,0},{0,1,36,0,31,0,10,0,7,7,0},{0,3,41,0,31,0,9,0,8,7,0},{0,1,21,0,31,0,9,0,7,7,0}}},
    },
    {0,+5,-4}, {0,+12,0}, {12,7,5},
    0,0,0,0, 0,0,0,
    0x07, {SSG_ATTACK_PLUCK, SSG_ATTACK_MALLET, SSG_CHORD_LAYER}, {4,3,1}, {0,12,7}
};

static const full_inst_t FULL_CLEAN_SOFT_BRASS = {
    "FULL_CLEAN_SOFT_BRASS", 3,
    {
        {"CL_BR_A",4,2,4,{{0,2,38,0,23,0,10,0,2,10,0},{0,1,17,0,25,0,9,0,1,10,0},{0,2,44,0,22,0,12,0,2,10,0},{0,1,18,0,25,0,8,0,1,10,0}}},
        {"CL_BR_B",1,2,4,{{0,2,46,0,24,0,12,0,2,10,0},{0,1,38,0,24,0,10,0,2,10,0},{0,1,52,0,23,0,9,0,1,10,0},{0,1,17,0,27,0,7,0,0,9,0}}},
        {"CL_BR_C",7,0,2,{{0,1,29,0,25,0,7,0,2,10,0},{0,1,31,0,25,0,7,0,2,10,0},{0,2,33,0,24,0,8,0,2,10,0},{0,1,25,0,24,0,8,0,2,10,0}}},
    },
    {0,+3,-3}, {0,0,+7}, {11,9,7},
    0,0,0,0, 1,1,360,
    0x07, {SSG_SQUARE_SOFT, SSG_CHORD_LAYER, SSG_SQUARE_SOFT}, {4,2,1}, {0,7,12}
};

static const full_inst_t* const YM2203_CLEAN_FULL_CATALOG[] = {
    &FULL_CLEAN_ROUND_LEAD,
    &FULL_CLEAN_WARM_PAD,
    &FULL_CLEAN_MELLOW_PLUCK,
    &FULL_CLEAN_SOFT_BRASS,
};
#define YM2203_CLEAN_FULL_CATALOG_COUNT ((uint8_t)(sizeof(YM2203_CLEAN_FULL_CATALOG)/sizeof(YM2203_CLEAN_FULL_CATALOG[0])))

// =============================================================================
//  §9  오케스트라 섹션 악기 (이하 Rev.2와 동일 ? 헤더 내용 포함)
//  §10 통합 악기 카탈로그 (full_inst_t* 배열)  ←  Rev.2 §10 그대로 유지
// =============================================================================
/* (§9·§10의 오케스트라 악기 정의 및 FULL_INST_CATALOG 배열은
    Rev.2 원본과 동일하므로 포함 생략. 실제 빌드 시 Rev.2 §9·§10 블록을
    이 위치에 삽입하거나 별도 ym2203_orch.h로 분리 include 권장)   */

    // =========================================================================                                                                                                                                                                                                                               ====
    //  §11  재생 API
    // =========================================================================                                                                                                                                                                                                                               ====

static inline void fi_note_on(const full_inst_t* fi,
    uint8_t midi_note, uint8_t vel)
{
    uint8_t n = fi->fm_n; if (n > 3u) n = 3u;
    for (int i = 0; i < (int)n; i++) {
        ym2203_key_off((uint8_t)i);
        ym_patch_vol(&fi->fm[i], (uint8_t)i, vel, fi->fm_vol[i]);
        if (i == 0 && fi->sw_ticks > 0u)
            ym_write((uint8_t)(0x40u + YM_OP_OFF[fi->sw_op]), fi->sw_tl0 & 0x7Fu);
        ym_set_note_ex((uint8_t)i, midi_note, fi->fm_det[i], fi->fm_noff[i]);
        ym2203_key_on((uint8_t)i, 0x0Fu);
    }
    for (int ch = 0; ch < 3; ch++) {
        if (!(fi->ssg_mask & (1u << ch))) continue;
        /* [Rev.6] Rev.7 patches.h 호환: ym2203_ssg_note_on()이
         * mixer, noise period, envelope, attack boost, cut_ticks를 모두 처리.
         * 구버전 ym2203_ssg_patch_apply_ch()는 더 이상 사용하지 않습니다. */
        int16_t ns = (int16_t)midi_note + (int16_t)fi->ssg_noff[ch];
        if (ns < 0)   ns = 0;
        if (ns > 127) ns = 127;
        ym2203_ssg_note_on(&YM2203_SSG_PATCHES[fi->ssg_p[ch]],
            (uint8_t)ch, (uint8_t)ns,
            (uint8_t)(fi->ssg_amp[ch] * 15u / 15u)); /* amp 그대로 vel로 전달 */
    }
}

/*
 * ym_ssg_noise_freq_vel ? 벨로시티에 따른 PSG 노이즈 주파수 가변 설정
 *
 * 문제: 기존 fi_note_on()은 ssg_amp만 고정값으로 쓰고 노이즈 주파수(NP)는
 *       패치에 고정되어 있습니다. 실제 대금/피리에서는:
 *         · 세게 불수록(강타): 취구 노이즈가 거칠고 고주파 성분이 강해집니다
 *         · 약하게 불수록(약타): 노이즈가 부드럽고 저주파 성분 위주입니다
 *
 * YM2203 SSG 노이즈 주파수 레지스터(0x06): 값이 작을수록 고주파 노이즈
 *   NP = 0~31, NP=0 → 최고주파 (거친 노이즈), NP=31 → 최저주파 (부드러운 노이                                                                                                                                                                                                                               즈)
 *
 * vel_to_np: 벨로시티 → NP 변환
 *   vel=127(최강) → np_high (거친 노이즈, 권장 2~8)
 *   vel=0  (최약) → np_low  (부드러운 노이즈, 권장 16~28)
 *
 * 이 함수는 fi_note_on() 이후에 바로 호출하여 노이즈 주파수를 조정합니다.
 */
static inline void ym_ssg_noise_freq_vel(uint8_t vel,
    uint8_t np_soft, uint8_t np_hard)
{
    uint8_t vel_s = (vel > 127u) ? 127u : vel;
    /* 선형 보간: vel=0→np_soft, vel=127→np_hard */
    int32_t np = (int32_t)np_soft
        + ((int32_t)np_hard - (int32_t)np_soft)
        * (int32_t)vel_s / 127;
    if (np < 0)  np = 0;
    if (np > 31) np = 31;
    ym_write(0x06u, (uint8_t)np);
}

/*
 * ym_ssg_vol_vel ? 벨로시티 로그 스케일 SSG 볼륨 설정
 *
 * vm_note_on()의 `vel >> 3` 선형 매핑 대신 사용.
 * SSG 볼륨은 0~15(4비트). VEL_TO_TL_ATTN LUT 활용해 로그 근사.
 *   ssg_vol = 15 - (VEL_TO_TL_ATTN[vel] * 15 / 40)
 */
static inline uint8_t ym_ssg_vol_from_vel(uint8_t vel)
{
    uint8_t vs = (vel > 127u) ? 127u : vel;
    int32_t vol = 15 - (int32_t)VEL_TO_TL_ATTN[vs] * 15 / 40;
    if (vol < 0)  vol = 0;
    if (vol > 15) vol = 15;
    return (uint8_t)vol;
}

/*
 * fi_note_on_v ? 벨로시티 완전 연동 발음 (PSG 노이즈 주파수 포함)
 *
 * fi_note_on()의 강화 버전입니다.
 * PSG 노이즈 주파수를 벨로시티에 따라 실시간으로 조정합니다.
 *
 * np_soft: 약타(vel=0) 시 노이즈 주파수 레지스터 값 (크면 부드러움, 0~31)
 * np_hard: 강타(vel=127) 시 노이즈 주파수 레지스터 값 (작으면 거칠음, 0~31)
 *
 * 악기별 권장 값:
 *   대금:  np_soft=22, np_hard=6   (취구: 강타=거친 숨소리)
 *   피리:  np_soft=18, np_hard=4   (서: 강타=날카로운 리드 노이즈)
 *   소금:  np_soft=20, np_hard=8
 *   아쟁:  np_soft=14, np_hard=3   (활: 강타=거친 마찰음)
 *   해금:  np_soft=16, np_hard=5
 */
static inline void fi_note_on_v(const full_inst_t* fi,
    uint8_t midi_note, uint8_t vel,
    uint8_t np_soft, uint8_t np_hard)
{
    uint8_t n = fi->fm_n; if (n > 3u) n = 3u;
    for (int i = 0; i < (int)n; i++) {
        ym2203_key_off((uint8_t)i);
        ym_patch_vol(&fi->fm[i], (uint8_t)i, vel, fi->fm_vol[i]);
        if (i == 0 && fi->sw_ticks > 0u)
            ym_write((uint8_t)(0x40u + YM_OP_OFF[fi->sw_op]), fi->sw_tl0 & 0x7Fu);
        ym_set_note_ex((uint8_t)i, midi_note, fi->fm_det[i], fi->fm_noff[i]);
        ym2203_key_on((uint8_t)i, 0x0Fu);
    }

    uint8_t has_noise = 0u;
    for (int ch = 0; ch < 3; ch++) {
        if (!(fi->ssg_mask & (1u << ch))) continue;
        int16_t ns = (int16_t)midi_note + (int16_t)fi->ssg_noff[ch];
        if (ns < 0)   ns = 0;
        if (ns > 127) ns = 127;
        /* [Rev.6] Rev.7 호환: ssg_note_on이 mixer+envelope+attack_boost+cut_tic                                                                                                                                                                                                                               ks 통합 처리.
         * vel 인자에 ssg_amp 비례 로그 볼륨을 넘겨 vol_scale 메커니즘을 활용합                                                                                                                                                                                                                                니다. */
        uint8_t ssg_vel = (uint8_t)((uint32_t)fi->ssg_amp[ch]
            * (uint32_t)ym_ssg_vol_from_vel(vel) / 15u);
        ym2203_ssg_note_on(&YM2203_SSG_PATCHES[fi->ssg_p[ch]],
            (uint8_t)ch, (uint8_t)ns, ssg_vel);
        /* 노이즈 채널 여부 추적 */
        if (fi->ssg_p[ch] == SSG_NOISE_SNARE ||
            fi->ssg_p[ch] == SSG_NOISE_HIHAT ||
            fi->ssg_p[ch] == SSG_NOISE_KICK ||
            fi->ssg_p[ch] == SSG_NOISE_CRASH) has_noise = 1u;
    }

    /* [Rev.5] PSG 노이즈 주파수 벨로시티 연동 */
    if (has_noise && np_soft != np_hard)
        ym_ssg_noise_freq_vel(vel, np_soft, np_hard);
}

static inline void fi_note_off(const full_inst_t* fi)
{
    uint8_t n = fi->fm_n; if (n > 3u) n = 3u;
    for (int i = 0; i < (int)n; i++) ym2203_key_off((uint8_t)i);
    for (int ch = 0; ch < 3; ch++)
        if (fi->ssg_mask & (1u << ch)) ym2203_ssg_ch_silence((uint8_t)ch);
}

static inline void fi_swell_tick(const full_inst_t* fi, uint16_t* tc)
{
    if (!fi->sw_ticks || *tc > fi->sw_ticks) return;
    int32_t tl = (int32_t)fi->sw_tl0 +
        (int32_t)((int32_t)(fi->sw_tl1 - fi->sw_tl0) * (int32_t)(*tc) /
            (int32_t)(fi->sw_ticks));
    if (tl < 0)   tl = 0;
    if (tl > 127) tl = 127;
    ym_write((uint8_t)(0x40u + YM_OP_OFF[fi->sw_op]), (uint8_t)(tl & 0x7Fu));
    (*tc)++;
}

/* [Rev.3] fi_chord_on_ji 구현 (§2A forward-declare 완성) */
static inline void fi_chord_on_ji(const full_inst_t* fi,
    uint8_t root, chord_type_t ctype, inversion_t inv,
    uint8_t vel, int16_t pb_cent)
{
    int8_t voices[3];
    ym_chord_voiced(root, ctype, inv, voices);
    uint8_t n = fi->fm_n; if (n > 3u) n = 3u;
    for (int i = 0; i < (int)n; i++) {
        ym2203_key_off((uint8_t)i);
        ym_patch_vol_split(&fi->fm[0], (uint8_t)i, vel, fi->fm_vol[0], 6u, 3u);
        int16_t note = (i < 3) ? (int16_t)voices[i] : (int16_t)root;
        if (note < 0)   note = 0;
        if (note > 127) note = 127;
        /* JI 보정 + 피치벤드 합산 */
        int16_t ji_cent = ym_ji_offset(ctype, (uint8_t)i, pb_cent);
        ym_set_note_ex((uint8_t)i, (uint8_t)note, fi->fm_det[i] + ji_cent, 0);
        ym2203_key_on((uint8_t)i, 0x0Fu);
    }
}

/* 화음 재생 (3채널 보이싱 ? 기본 평균율) */
static inline void fi_chord_on(const full_inst_t* fi,
    uint8_t root, chord_type_t ctype, inversion_t inv, uint8_t vel)
{
    fi_chord_on_ji(fi, root, ctype, inv, vel, 0);
}

/* [Rev.3] fi_chord_on_vl ? 최단 거리 전위 + JI 보정 코드 발음 */
static inline void fi_chord_on_vl(const full_inst_t* fi,
    uint8_t root, chord_type_t ctype,
    const int8_t prev_notes[3],  /* 이전 성부 노트 (보이스 리딩 참조용) */
    uint8_t vel, int16_t pb_cent)
{
    inversion_t best_inv = ym_nearest_inversion(root, ctype, prev_notes);
    fi_chord_on_ji(fi, root, ctype, best_inv, vel, pb_cent);
}

// =============================================================================
//  §12  화성 진행 시퀀서
// =============================================================================

typedef struct {
    const chord_seq_t* seq;
    const full_inst_t* inst;
    uint8_t            pos;
    uint16_t           tick;
    uint16_t           q_ticks;
    uint8_t            playing;
    uint8_t            loop;
    uint16_t           sw_tc;
    /* [Rev.3] 보이스 리딩 상태 */
    int8_t             last_notes[3];
    uint8_t            use_vl;       /* 1=보이스 리딩 ON    */
    uint8_t            use_ji;       /* 1=순정률 보정 ON    */
    /* [Rev.3] 대위법 엔진 연동 */
    counterpoint_t* cp;           /* NULL=미사용         */
    /* [Rev.3] 텐션 모핑 연동 */
    timbre_morph_t* tm;           /* NULL=미사용         */
} chord_seq_state_t;

static inline void seq_init(chord_seq_state_t* s,
    const chord_seq_t* seq, const full_inst_t* inst,
    uint8_t loop, uint8_t use_vl, uint8_t use_ji)
{
    memset(s, 0, sizeof(*s));
    s->seq = seq;
    s->inst = inst;
    s->q_ticks = bpm_to_ticks(seq->bpm);
    s->loop = loop;
    s->playing = 1u;
    s->use_vl = use_vl;
    s->use_ji = use_ji;
}

static inline int seq_tick(chord_seq_state_t* s)
{
    if (!s->playing || !s->seq) return 0;
    const chord_seq_t* sq = s->seq;
    if (s->pos >= sq->len) {
        if (s->loop) { s->pos = 0u; s->tick = 0u; }
        else { s->playing = 0u; fi_note_off(s->inst); return 0; }
    }
    const chord_event_t* ev = &sq->events[s->pos];
    uint16_t dur = dur_ticks(ev->dur, s->q_ticks);
    if (s->tick == 0u) {
        s->sw_tc = 0u;
        /* [Rev.3] 텐션 모핑 적용 */
        if (s->tm)
            tm_apply(s->tm, &s->inst->fm[0],
                CHORD_DEFS[ev->ctype].dissonance);
        /* [Rev.3] 보이스 리딩 or 기본 발음 */
        if (s->use_vl && (s->pos > 0u || s->last_notes[0] != 0)) {
            fi_chord_on_vl(s->inst, ev->root, ev->ctype,
                s->last_notes, ev->vel,
                s->use_ji ? 0 : 0);
        }
        else if (s->use_ji) {
            fi_chord_on_ji(s->inst, ev->root, ev->ctype,
                ev->inv, ev->vel, 0);
        }
        else {
            fi_chord_on(s->inst, ev->root, ev->ctype, ev->inv, ev->vel);
        }
        /* 현재 성부 저장 */
        ym_chord_voiced(ev->root, ev->ctype, ev->inv, s->last_notes);
        /* [Rev.3] 대위법: 병행 5도 보정 */
        if (s->cp && s->pos > 0u) {
            cp_check_parallels(s->cp->prev_notes, s->last_notes);
            cp_prepare_suspension(s->cp, s->cp->prev_notes,
                s->last_notes, dur / 4u);
        }
        if (s->cp) {
            memcpy(s->cp->prev_notes, s->last_notes,
                sizeof(s->last_notes));
        }
    }
    fi_swell_tick(s->inst, &s->sw_tc);
    /* [Rev.3] 계류음 해결 처리 */
    if (s->cp) cp_tick(s->cp);
    /* 아티큘레이션 */
    if (s->tick == (uint16_t)(dur * 85u / 100u))
        fi_note_off(s->inst);
    s->tick++;
    if (s->tick >= dur) { s->tick = 0u; s->pos++; }
    return 1;
}

/* --- 전방 선언 (Forward Declarations) --- */

// Dual Patches
static const dual_patch_t DUAL_PIANO;
static const dual_patch_t DUAL_EPIANO;
static const dual_patch_t DUAL_STRINGS;
static const dual_patch_t DUAL_BRASS;
static const dual_patch_t DUAL_ORGAN;
static const dual_patch_t DUAL_CHOIR;
static const dual_patch_t DUAL_OVERDRIVE;
static const dual_patch_t DUAL_BASS;
static const dual_patch_t DUAL_PERC;

// Triple Patches 
// (중요: 에러 목록에 맞춰 이름을 수정했습니다. 아래 이름들이 실제 데이터 정의와 일치해야 합니다)
static const triple_patch_t TRIPLE_ORCH_STRINGS;
static const triple_patch_t TRIPLE_PIPE_ORGAN;
static const triple_patch_t TRIPLE_RICH_LEAD;
static const triple_patch_t TRIPLE_DEEP_BASS;
static const triple_patch_t TRIPLE_DRUM_KIT;
static const triple_patch_t TRIPLE_SYNTH_TEX;
// =============================================================================
//  §13  예시 화성 진행
// =============================================================================

static const chord_seq_t SEQ_251_CMAJ = {
    120, 33, 3,
    {
        {62, CHORD_MIN7,  INV_ROOT, DUR_HALF, 90},
        {67, CHORD_DOM9,  INV_1ST,  DUR_HALF, 95},
        {60, CHORD_MAJ7,  INV_ROOT, DUR_WHOLE,85},
    }
};

static const chord_seq_t SEQ_TTS_CMAJ = {
    120, 33, 3,
    {
        {62, CHORD_MIN7,   INV_ROOT, DUR_HALF,  90},
        {61, CHORD_DOM11,INV_1ST,  DUR_HALF,  95},
        {60, CHORD_MAJ7,   INV_ROOT, DUR_WHOLE, 85},
    }
};

static const chord_seq_t SEQ_1625_CMAJ = {
    100, 0, 4,
    {
        {60, CHORD_MAJ7, INV_ROOT, DUR_HALF, 85},
        {57, CHORD_MIN7, INV_ROOT, DUR_HALF, 80},
        {62, CHORD_MIN7, INV_1ST,  DUR_HALF, 85},
        {67, CHORD_DOM7, INV_1ST,  DUR_HALF, 90},
    }
};

static const chord_seq_t SEQ_SEC_DOM_CMAJ = {
    110, 0, 7,
    {
        {60, CHORD_MAJ7, INV_ROOT, DUR_HALF,  85},
        {64, CHORD_DOM7, INV_1ST,  DUR_HALF,  90},
        {57, CHORD_MIN7, INV_ROOT, DUR_HALF,  85},
        {66, CHORD_DOM7, INV_1ST,  DUR_HALF,  90},
        {62, CHORD_MIN7, INV_ROOT, DUR_HALF,  85},
        {67, CHORD_DOM9, INV_1ST,  DUR_HALF,  90},
        {60, CHORD_MAJ7, INV_ROOT, DUR_WHOLE, 80},
    }
};

static const chord_seq_t SEQ_MODAL_IC = {
    100, 0, 4,
    {
        {60, CHORD_MAJ7, INV_ROOT, DUR_HALF,  85},
        {58, CHORD_MAJ7, INV_ROOT, DUR_HALF,  90},
        {56, CHORD_MAJ7, INV_1ST,  DUR_HALF,  85},
        {60, CHORD_MAJ7, INV_ROOT, DUR_WHOLE, 80},
    }
};

static const chord_seq_t SEQ_UST_PROG = {
    110, 33, 3,
    {
        {62, CHORD_MIN9,  INV_ROOT, DUR_HALF,  85},
        {67, CHORD_DOM13, INV_1ST,  DUR_HALF,  95},
        {60, CHORD_MAJ9,  INV_ROOT, DUR_WHOLE, 80},
    }
};

static const chord_seq_t SEQ_PYEONGJO = {
    80, 0, 4,
    {
        {60, CHORD_SUS2, INV_ROOT, DUR_HALF,  85},
        {65, CHORD_SUS2, INV_ROOT, DUR_HALF,  80},
        {67, CHORD_SUS4, INV_ROOT, DUR_HALF,  90},
        {60, CHORD_SUS2, INV_ROOT, DUR_WHOLE, 80},
    }
};

// =============================================================================
//  §14  듀얼/트리플 재생 API
// =============================================================================

static inline void dual_note_on(uint8_t start_ch,
    const dual_patch_t* dp, uint8_t note, uint8_t vel)
{
    uint8_t ca = start_ch, cb = (uint8_t)((start_ch + 1u) % 3u);
    ym2203_key_off(ca); ym2203_key_off(cb);
    ym_patch_vol(&dp->ch_a, ca, vel, dp->vol_a);
    if (dp->sw_ticks > 0u)
        ym_write((uint8_t)(0x40u + YM_OP_OFF[dp->sw_op] + ca), dp->sw_tl0 & 0x7Fu);
    ym_patch_vol(&dp->ch_b, cb, vel, dp->vol_b);
    ym_set_note_ex(ca, note, dp->det_a, 0);
    ym_set_note_ex(cb, note, dp->det_b, 0);
    ym2203_key_on(ca, 0x0Fu);
    ym2203_key_on(cb, 0x0Fu);
}

static inline void dual_note_off(uint8_t start_ch)
{
    ym2203_key_off(start_ch);
    ym2203_key_off((uint8_t)((start_ch + 1u) % 3u));
}

static inline void triple_note_on(const triple_patch_t* tp,
    uint8_t note, uint8_t vel)
{
    for (int i = 0; i < 3; i++) {
        ym2203_key_off((uint8_t)i);
        ym_patch_vol(&tp->ch[i], (uint8_t)i, vel, tp->vol[i]);
        ym_set_note_ex((uint8_t)i, note, tp->det[i], tp->noff[i]);
        ym2203_key_on((uint8_t)i, 0x0Fu);
    }
}

static inline void triple_note_off(void)
{
    ym2203_key_off(0); ym2203_key_off(1); ym2203_key_off(2);
}

// =============================================================================
//  §15  인덱스 테이블
// =============================================================================

typedef enum {
    DUAL_IDX_PIANO = 0, DUAL_IDX_EPIANO, DUAL_IDX_STRINGS, DUAL_IDX_BRASS,
    DUAL_IDX_ORGAN, DUAL_IDX_CHOIR, DUAL_IDX_OVERDRIVE, DUAL_IDX_BASS,
    DUAL_IDX_PERC, DUAL_IDX_COUNT
} dual_idx_t;

static const dual_patch_t* const YM_DUAL[DUAL_IDX_COUNT] = {
    &DUAL_PIANO, &DUAL_EPIANO, &DUAL_STRINGS, &DUAL_BRASS,
    &DUAL_ORGAN, &DUAL_CHOIR,  &DUAL_OVERDRIVE, &DUAL_BASS,
    &DUAL_PERC
};

typedef enum {
    TRIPLE_IDX_ORCH_STR = 0, TRIPLE_IDX_PIPE_ORG, TRIPLE_IDX_RICH_LD,
    TRIPLE_IDX_DEEP_BS, TRIPLE_IDX_DRUM_KT, TRIPLE_IDX_SYNTH_TX,
    TRIPLE_IDX_COUNT
} triple_idx_t;

static const triple_patch_t* const YM_TRIPLE[TRIPLE_IDX_COUNT] = {
    &TRIPLE_ORCH_STRINGS, &TRIPLE_PIPE_ORGAN, &TRIPLE_RICH_LEAD,
    &TRIPLE_DEEP_BASS,    &TRIPLE_DRUM_KIT,   &TRIPLE_SYNTH_TEX
};

// =============================================================================
//  §16  [Rev.5] 통합 사용 예시 ? 레퍼런스 코드
// =============================================================================
//
//  아래 예시는 #if 0 블록으로 컴파일에서 제외됩니다.
//  실제 적용 시 조건부 블록을 제거하고 사용하십시오.
//
//  lqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk
//  x  예시 A: JI + 보이스 리딩 코드 진행 (Rev.3)                       x
//  x  예시 B: MIDI 지능형 보이스 매니저 (Rev.3)                        x
//  x  예시 C: 국악 대금 + 비선형 농음 + 포르타멘토 (Rev.3)             x
//  x  예시 D: 캐논 엔진 (Rev.3)                                        x
//  x  예시 E: 화성 분석기 → 텐션 모핑 자동 적용 (Rev.3)               x
//  x  예시 F: [Rev.5] RT 스케줄링 + 조 판별 자동 JI MIDI 루프         x
//  x  예시 G: [Rev.5] 대금 벨로시티 연동 PSG 노이즈 + 랜덤 농음       x
//  mqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqj
//
// #if 0
//
//  /* qq 예시 F: RT 스케줄링 + 실시간 조 판별 JI 자동 연동 qq */
//  #include <pthread.h>
//  static volatile int g_running = 1;
//  static voice_mgr_t  g_vm_f;
//  static midi_perf_t  g_mp_f;
//
//  void example_f_init(void) {
//      vm_init_ji(&g_vm_f);            /* 조 판별 + JI 자동 연동 ON */
//      mp_init(&g_mp_f, 6, 3, 2, 0, 2, 7);
//      if (ym_rt_setup(80, 1) != 0)    /* Zynq CPU1, SCHED_FIFO 80 */
//          fprintf(stderr, "RT setup failed (need root)\n");
//  }
//
//  void example_f_midi(uint8_t st, uint8_t d1, uint8_t d2) {
//      if ((st & 0xF0u) == 0x90u && d2 > 0u) {
//          vm_note_on(&g_vm_f, d1, d2, &FULL_DAEGEUM.fm[0]);
//          /* vm_note_on이 kd_note_on() 자동 호출 → 조 실시간 갱신 */
//          if (g_vm_f.key_det.valid)
//              kd_ji_note_ex(0, d1, &g_vm_f.key_det, g_mp_f.pb_cent);
//      } else if ((st & 0xF0u) == 0x80u || ((st & 0xF0u) == 0x90u && d2 == 0))                                                                                                                                                                                                                                {
//          vm_note_off(&g_vm_f, d1);
//      } else if ((st & 0xF0u) == 0xB0u) {
//          vm_cc(&g_vm_f, d1, d2);    /* CC#120/123 Note Hang 완전 해소 */
//          mp_cc(&g_mp_f, d1, d2, &FULL_DAEGEUM.fm[0]);
//      } else if ((st & 0xF0u) == 0xE0u) {
//          int16_t pb = (int16_t)(((uint16_t)d2 << 7u) | d1) - 8192;
//          mp_pitch_bend(&g_mp_f, pb, 0);
//      }
//  }
//
//  void* example_f_rt_thread(void* arg) {
//      (void)arg;
//      struct timespec next_wake;
//      clock_gettime(CLOCK_MONOTONIC, &next_wake);
//      while (g_running) {
//          vm_tick_full(&g_vm_f);  /* [Rev.6] age + FM LFO + SSG cut/attack 통                                                                                                                                                                                                                                합 */
//          ym_rt_sleep_until(&next_wake, 10000000LL);  /* 10ms 절대 주기 */
//      }
//      return NULL;
//  }
//
//  /* qq 예시 G: 대금 벨로시티 연동 PSG 노이즈 + 랜덤 농음 qq */
//  static nonlinear_lfo_t g_lfo_g;
//  static uint8_t         g_curr_note_g = 60u;
//
//  void example_g_note(uint8_t note, uint8_t vel) {
//      g_curr_note_g = note;
//      fi_note_on_v(&FULL_DAEGEUM, note, vel,
//                   22,   /* np_soft: 약타 → NP=22, 부드러운 숨소리 */
//                   6);   /* np_hard: 강타 → NP=6, 거친 취구 노이즈 */
//      nlfo_init(&g_lfo_g, NLFO_HWANG);
//  }
//
//  void example_g_tick(void) {
//      int16_t lfo_cent = nlfo_tick_r(&g_lfo_g, 8); /* ±8 cent 랜덤 변동 */
//      /* 조 판별 JI + 농음 합산 후 발음 */
//      const ym2203_fnum_t* f = &YM2203_FREQ_LUT[g_curr_note_g];
//      uint16_t base_fnum = (uint16_t)(f->lsb | ((uint16_t)(f->msb & 7u) << 8))                                                                                                                                                                                                                               ;
//      uint8_t  base_blk  = (uint8_t)((f->msb >> 3u) & 7u);
//      uint16_t fnum; uint8_t blk;
//      ym_fnum_cents_blk(base_fnum, base_blk, lfo_cent, &fnum, &blk);
//      ym_set_fnum_raw(0, fnum, blk);
//  }
//
//  /* qq 예시 A: JI + 보이스 리딩 qq */
//  static chord_seq_state_t g_seq;
//  static counterpoint_t    g_cp;
//  static timbre_morph_t    g_tm;
//
//  void example_a_init(void) {
//      tm_init(&g_tm, 0, 3, 6, 8);
//      seq_init(&g_seq, &SEQ_251_CMAJ, &FULL_GAYAGEUM,
//               /*loop*/1, /*use_vl*/1, /*use_ji*/1);
//      g_seq.cp = &g_cp;
//      g_seq.tm = &g_tm;
//  }
//  void example_a_tick(void) { seq_tick(&g_seq); }  /* 10ms 타이머에서 호출 */
//
//  /* qq 예시 B: 인터랙티브 MIDI 보이스 매니저 qq */
//  static voice_mgr_t   g_vm;
//  static midi_perf_t   g_mp;
//  static nonlinear_lfo_t g_at_lfo;
//
//  void example_b_init(void) {
//      vm_init(&g_vm);
//      mp_init(&g_mp, 6, 3, 2, 0, 2, 7);
//      nlfo_init(&g_at_lfo, NLFO_HWANG);
//      g_mp.at_lfo = &g_at_lfo;
//  }
//  void example_b_midi(uint8_t status, uint8_t d1, uint8_t d2) {
//      if ((status & 0xF0u) == 0x90u) {
//          vm_set_chord(&g_vm, d1, CHORD_DOM7);  /* 실제는 ca_analyze() 사용 */
//          vm_note_on(&g_vm, d1, d2, &DUAL_PIANO.ch_a);
//          ce_capture(&g_ce, d1, d2);
//      } else if ((status & 0xF0u) == 0x80u) {
//          vm_note_off(&g_vm, d1);
//          ce_capture(&g_ce, d1, 0);
//      } else if ((status & 0xF0u) == 0xB0u) {
//          vm_cc(&g_vm, d1, d2);
//          mp_cc(&g_mp, d1, d2, &DUAL_PIANO.ch_a);
//      } else if ((status & 0xF0u) == 0xE0u) {
//          int16_t pb = (int16_t)(((uint16_t)d2 << 7u) | d1) - 8192;
//          mp_pitch_bend(&g_mp, pb, 0);
//      } else if ((status & 0xF0u) == 0xA0u) {
//          mp_aftertouch(&g_mp, d2, 0);
//      }
//  }
//  void example_b_tick(void) {
//      vm_tick(&g_vm);
//      mp_tick(&g_mp, 60u);  /* 현재 노트로 교체 */
//      ce_tick(&g_ce, &DUAL_PIANO.ch_a, 15u);
//  }
//
//  /* qq 예시 C: 대금 + 추성(포르타멘토) + 농음 qq */
//  static portamento_t g_pt;
//  static nonlinear_lfo_t g_lfo_daegeum;
//
//  void example_c_note(uint8_t from, uint8_t to) {
//      fi_note_on(&FULL_DAEGEUM, to, 90);
//      pt_start(&g_pt, from, to, 0, 8, PT_MODE_UP);  /* 추성: 8틱 상행 */
//      nlfo_init(&g_lfo_daegeum, NLFO_HWANG);
//  }
//  void example_c_tick(void) {
//      pt_tick(&g_pt);
//      /* 포르타멘토 완료 후 농음 적용 */
//      if (!g_pt.active) {
//          int16_t nlfo_cent = nlfo_tick(&g_lfo_daegeum);
//          uint16_t fnum; uint8_t msb;
//          ym_get_fnum(g_pt.fnum_dst, &fnum, &msb);  /* 목표 노트 */
//          fnum = ym_fnum_cents(fnum, nlfo_cent);
//          ym_set_fnum_raw(0, fnum, (uint8_t)((msb>>3)&7));
//      }
//  }
//
//  /* qq 예시 D: 캐논 엔진 qq */
//  static canon_engine_t g_ce;
//
//  void example_d_init(void) {
//      ce_init(&g_ce, 50u,      /* 50틱 = 0.5초 지연       */
//              1u,              /* 출력 채널 1             */
//              1u,              /* 조성 양자화 ON          */
//              60u,             /* 조성 루트 C4            */
//              SCALE_T_GYEMYEONG); /* 계면조 양자화        */
//      g_ce.mirror      = 1u;  /* 상하 반전 대위 ON       */
//      g_ce.mirror_root = 67u; /* G4 기준 반전            */
//  }
//
//  /* qq 예시 E: 화성 분석기 → 텐션 모핑 qq */
//  void example_e_analyze(uint8_t notes[], uint8_t n) {
//      chord_analysis_t ca = ca_analyze(notes, n);
//      timbre_morph_t tm;
//      tm_init(&tm, 0, 2, 7, 12);
//      tm_apply(&tm, &DUAL_PIANO.ch_a, ca.dissonance);
//  }
//
// #endif  /* 예시 블록 끝 */

#endif /* YM2203_MULTI_H */