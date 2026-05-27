// =============================================================================
//  effect_bd_uio_v14.c  ?  PetaLinux UIO 드라이버  Rev.14
// 
//
//  ┌─ 구조 개요 ──────────────────────────────────────────────────────────────┐
//
//  [섹션 A]  공통 인프라
//    A1. 헤더/상수/타입 정의
//    A2. UIO 유틸리티
//    A3. 레지스터 헬퍼
//    A4. 하드웨어 오프셋 상수
//    A5. 타이밍/신호 헬퍼
//
//  [섹션 B]  하드웨어 드라이버
//    B1. 전역 UIO 핸들 & 포인터
//    B2. YM2203 / ym 드라이버
//    B3. EQ 드라이버
//    B4. VCA 드라이버
//    B5. DELAY 드라이버
//    B6. LFO 드라이버
//
//  [섹션 C]  LCD / SPI 드라이버
//    C1. LCD 하드웨어
//    C2. LCD 그래픽 유틸리티
//
//  [섹션 D]  공통 상태 & 프리셋
//    D1. 전역 설정 구조체 (full_preset_t ? SQLite 직렬화 준비)
//    D2. 이펙터 프리셋 테이블
//    D3. YM2203 보이스 관리 (m3_full_* ? SYNTH/MIDI 공용)
//
//  [섹션 E]  모드 0: MENU SELECT
//  [섹션 F]  모드 1: SYNTH/MIDI
//  [섹션 G]  모드 2: VGM PLAYER
//  [섹션 H]  모드 3: USB MIDI KEYBOARD + YM2203
//  [섹션 I]  모드 4: INST SETUP  (mode4_instsetup_v2 전면 교체)
//    §P1  전역 설정 구조체 (이미 D1에 선언)
//    §P2  full_inst_t 카탈로그
//    §P3  기본값 초기화
//    §P4  화성학 런타임 초기화
//    §P5  HW 적용
//    §I-U  유틸리티
//    §I-D  LCD 드로우
//    §I-A  TOP 메뉴 APPLY
//    §I-F  FSM 업데이트
//    §I-E  공개 API
//
//  [섹션 J]  FSM 디스패처 & main()
//
//  └─ V14 변경사항 ───────────────────────────────────────────────────────────┘
//  [V14-D1]  global_preset_t → full_preset_t 로 완전 교체
//              fm_ch_cfg_t / psg_ch_cfg_t / harmony_cfg_t 추가
//              SQLite 직렬화 설계 주석 포함
//              g_preset → g_full_preset
//
//  [V14-D3]  ym_synth_* → m3_full_* 발음 경로 전면 교체
//              voice_mgr_t / midi_perf_t / nonlinear_lfo_t / counterpoint_t
//              런타임 전역 상태 추가
//              harmony_cfg.voice_mgr_on / ji_on 경로 분기
//
//  [V14-H]   mode_midivis_enter/update/cleanup 발음 함수 교체
//              preset_apply_hw() → full_preset_apply_hw()
//              ym_synth_note_on/off/all_off/tick → m3_full_*
//              m3_process_msg: m3_full_cc / m3_full_pitch_bend 통합
//
//  [V14-I]   모드 4 섹션 전체 교체 (mode4_instsetup_v2.c 기반)
//              [H-FULL]  화성학 엔진 서브메뉴 (M4_HARMONY)
//              [FM-DUAL] FM 편집 모드: full_inst_t ↔ raw patch
//              [PREVIEW] fi_note_on_v() 기반 프리뷰
//              [VOICE]   m3_full_note_on()에 voice_mgr_t 통합
//
//  [V14-MAIN] main() 초기화:
//              preset_default() → full_preset_default()
//              ym_voice_reset() → m3_full_voice_reset()
//              harmony_runtime_apply() 추가
//
//  [V14-COMPAT] FSM 테이블 시그니처 변경 없음
//               V13 섹션 A~C, E~G, J 구조 완전 보존
// =============================================================================

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define LIGHT_CHAIN_YM_PRE_EQ_VCA_POST_EQ_DELAY 1
#define ENABLE_HW_LFO0_DELAY 1

/* =============================================================================
 * [DIAG-CHAIN-ISOLATION]
 * 팀원 요청: Mode 4/Mode 3 문제 원인 특정용 단계별 체인 제한 파일.
 *
 * DIAG_CHAIN_STAGE:
 *   0 = YM2203 only: PRE EQ / VCA / POST EQ / DELAY / LFO0 모두 bypass/disabled
 *   1 = YM2203 + PRE EQ
 *   2 = YM2203 + PRE EQ + VCA
 *   3 = YM2203 + PRE EQ + VCA + POST EQ
 *   4 = YM2203 + PRE EQ + VCA + POST EQ + DELAY(+LFO0)
 * ============================================================================= */
#ifndef DIAG_CHAIN_STAGE
 /* [v2 변경] 팀원 요청 반영: VCA bypass 해제 + POST EQ를 PRE EQ와 동일 방식으로 운용.
  *   STAGE 3 = YM2203 + PRE EQ + VCA + POST EQ (DELAY는 미언급이므로 bypass 유지)
  *   - DIAG_USE_PRE_EQ=1, DIAG_USE_VCA=1, DIAG_USE_POST_EQ=1, DIAG_USE_DELAY=0
  *   - full_preset_apply_hw() / fx_init_sound_guaranteed() / diag_apply_chain_policy()
  *     모두 이 플래그를 따라 PRE EQ→VCA→POST EQ를 적용하고 DELAY만 bypass한다.
  *   - DELAY까지 켜려면 이 값을 4로 올리면 됨. */
#define DIAG_CHAIN_STAGE 4
#endif

#if (DIAG_CHAIN_STAGE < 0) || (DIAG_CHAIN_STAGE > 4)
#error "DIAG_CHAIN_STAGE must be 0..4"
#endif

#define DIAG_USE_PRE_EQ   (DIAG_CHAIN_STAGE >= 1)
#define DIAG_USE_VCA      (DIAG_CHAIN_STAGE >= 2)
#define DIAG_USE_POST_EQ  (DIAG_CHAIN_STAGE >= 3)
#define DIAG_USE_DELAY    (DIAG_CHAIN_STAGE >= 4)

  // ─────────────────────────────────────────────────────────────────────────────
  //  [섹션 A1]  헤더 / 기본 상수 / 타입
  // ─────────────────────────────────────────────────────────────────────────────
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <math.h>
#ifndef _MSC_VER
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/spi/spidev.h>
#include <linux/i2c-dev.h>
#endif /* !_MSC_VER */

/* -----------------------------------------------------------------------------
 * Build-warning guard
 * - 일반 gcc 빌드는 원래 통과하지만, 팀원 Makefile이 -Wall/-Wextra/-Werror를
 *   켠 경우 헤더 내부 경고와 기존 unused/misleading-indentation 경고가
 *   에러로 승격될 수 있어 해당 경고만 억제한다.
 * - 동작 로직 변경 없음.
 * ----------------------------------------------------------------------------- */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#ifdef _MSC_VER
 /* ==========================================================
    Visual Studio IntelliSense & Error Suppression Block
    (실제 빌드는 리눅스 환경의 GCC에서 수행)
    ========================================================== */

    // 1. 리눅스/GCC 전용 키워드 무시
#define __attribute__(x)
#define inline __inline
#define __restrict

// 2. POSIX/Linux IO 및 메모리 상수
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_SHARED 0x01
#define MAP_FAILED ((void*)-1)
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_NONBLOCK 04000
#define O_SYNC     04010000

// ssize_t (POSIX 부호 있는 size_t)
typedef long long ssize_t;

// I2C_SLAVE (linux/i2c-dev.h)
#define I2C_SLAVE  0x0703

// 3. 스레드 및 시간 관련 정의
#define CLOCK_MONOTONIC 1
#define SCHED_FIFO      1
typedef int pthread_t;
typedef int pthread_attr_t;
struct timespec { long tv_sec; long tv_nsec; };
struct sched_param { int sched_priority; };
struct sigaction { int sa_handler; int sa_mask; int sa_flags; };
#define SIGINT  2
#define SIGTERM 15

// 4. SPI (spidev.h) 핵심 구조체 정의
// 이 정의가 있어야 tr.tx_buf 등의 멤버 에러가 사라집니다.
struct spi_ioc_transfer {
    unsigned __int64 tx_buf;
    unsigned __int64 rx_buf;
    unsigned __int32 len;
    unsigned __int32 speed_hz;
    unsigned __int16 delay_usecs;
    unsigned __int8  bits_per_word;
    unsigned __int8  cs_change;
    unsigned __int32 pad;
};
#define SPI_IOC_MESSAGE(N) 0
#define SPI_MODE_0               0
#define SPI_IOC_WR_MODE          0
#define SPI_IOC_WR_BITS_PER_WORD 0
#define SPI_IOC_WR_MAX_SPEED_HZ  0

// 5. 악기 구조체 및 lvalue 에러 해결 (extern 선언)
// _MSC_VER 블록 안에서만 stub 정의 ? GCC 빌드는 ym2203_multi.h의 실제 정의를 사용
typedef struct {
    int pb_cent; // MSVC IntelliSense 전용 stub
    int dummy;
} full_inst_t;

extern const full_inst_t FULL_DAEGEUM;
extern const full_inst_t FULL_SOGEUM;
extern const full_inst_t FULL_PIRI;
extern const full_inst_t FULL_GEOMUNGO;
extern const full_inst_t FULL_AJAENG;
extern const full_inst_t FULL_DANSO;
extern const full_inst_t FULL_HUN;
extern const full_inst_t FULL_GAYAGEUM;
extern const full_inst_t FULL_HAEGEUM;
extern const full_inst_t FULL_VIOLIN;
extern const full_inst_t FULL_CELLO;

// 6. 기타 미디 관련 상수 및 구조체
#define POLLIN      0x0001
typedef struct { int dummy; } midi_perf_t;

// struct pollfd 완전 정의 (poll.h 대체)
struct pollfd {
    int   fd;
    short events;
    short revents;
};

// 7. 리눅스 시스템 함수 더미 선언
inline int clock_nanosleep(int clk_id, int flags, const struct timespec* req, struct timespec* rem) { return 0; }
inline int sched_setscheduler(int pid, int policy, const struct sched_param* param) { return 0; }
inline int open(const char* path, int flags, ...) { return 0; }
inline int close(int fd) { return 0; }
inline int ioctl(int fd, unsigned long request, ...) { return 0; }
inline void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off) { return (void*)0; }
inline void perror(const char* s) {}
inline pthread_t pthread_self(void) { return 0; }
inline int pthread_setschedparam(pthread_t t, int policy, const struct sched_param* p) { return 0; }

// 8. MSVC 경고 억제
#pragma warning(disable:4996) // 보안 경고 (scanf, 등)
#pragma warning(disable:4013) // 정의되지 않은 함수 호출
#pragma warning(disable:4047) // 포인터 간접 참조 수준 다름
#pragma warning(disable:4244) // 데이터 손실 가능성
#pragma warning(disable:4701) // 잠재적으로 초기화되지 않은 지역 변수 사용
#pragma warning(disable:4477) // 형식 지정자 비호환 (E0079 억제)

/* ── MSVC IntelliSense 전방 선언 (E0147 억제) ─────────────────────────── */
/* GCC 빌드에서는 불필요하지만 MSVC에서 함수 사용 전 선언이 필요함 */
typedef struct { int fd; void* map; size_t size; } uio_dev_t_fwd;
/* 실제 uio_dev_t는 나중에 정의되므로 여기서는 주요 함수만 전방 선언 */
static void hw_cleanup(void);
static void adly_adapter_init(void);
static void delay_mute(void);
static void apply_dly_custom_from_full_preset(void);
static void full_preset_apply_hw(void);
static void harmony_runtime_apply(void);
static void diag_dump_chain(const char* when);
#endif

#include "vgm_data_1.h"
#include "ym2203_patches.h"  /* chord_type enum 먼저 정의 (ym2203_multi.h가 참조) */
/* CHORD alias: enum 상수 CHORD_CHORD는 #ifndef로 감지되지 않으므로 재정의하지 않는다.
 * 구버전 코드가 CHORD_NONE을 참조할 경우에만 CHORD_CHORD로 alias 처리한다. */
#ifndef CHORD_NONE
#define CHORD_NONE CHORD_CHORD
#endif

 /* -----------------------------------------------------------------------------
  * [YM2203-SSG-COMPAT] Rev.12 ym2203_patches.h ↔ legacy multi/full6 headers
  * 현재 patches.h는 SSG_MELODY, SSG_PLUCK, SSG_DRUM, SSG_FX 계열 이름을 쓰고,
  * ym2203_multi.h / ym2203_full6_vgm.h 일부 preset은 구버전 별칭을 참조한다.
  * 실제 enum 값은 새 이름으로 존재하므로 include 순서상 여기서만 별칭을 제공한다.
  * ----------------------------------------------------------------------------- */
#ifndef SSG_SQUARE_BRIGHT
#define SSG_SQUARE_BRIGHT  SSG_MELODY_SQUARE_BRIGHT
#endif
#ifndef SSG_SQUARE_SOFT
#define SSG_SQUARE_SOFT    SSG_MELODY_SQUARE_WARM
#endif
#ifndef SSG_CHORD_LAYER
#define SSG_CHORD_LAYER    SSG_MELODY_ORGAN_PIPE
#endif
#ifndef SSG_ATTACK_PLUCK
#define SSG_ATTACK_PLUCK   SSG_PLUCK_HARP
#endif
#ifndef SSG_ATTACK_MALLET
#define SSG_ATTACK_MALLET  SSG_PLUCK_MARIMBA
#endif
#ifndef SSG_NOISE_KICK
#define SSG_NOISE_KICK     SSG_DRUM_KICK_DEEP
#endif
#ifndef SSG_NOISE_SNARE
#define SSG_NOISE_SNARE    SSG_DRUM_SNARE_CRISP
#endif
#ifndef SSG_NOISE_HIHAT
#define SSG_NOISE_HIHAT    SSG_DRUM_HIHAT_CLOSED
#endif
#ifndef SSG_NOISE_CRASH
#define SSG_NOISE_CRASH    SSG_DRUM_CYMBAL_CRASH
#endif
#ifndef SSG_BUZZ
#define SSG_BUZZ           SSG_FX_BUZZ_TONE
#endif

#include "ym2203_multi.h"   /* full_inst_t, voice_mgr_t, key_detector_t,
                               ym_patch_vol(), fi_note_on_v(), vm_note_on(),
                               vm_note_off(), vm_cc(), vm_tick_full(),
                               kd_ji_note_ex(), vm_init_ji(), vm_panic_off(),
                               midi_perf_t, nonlinear_lfo_t, counterpoint_t,
                               mp_init(), mp_cc(), mp_pitch_bend(),
                               nlfo_init(), nlfo_tick_r(), ym_fnum_cents_blk(),
                               ym_set_fnum_raw(), ym_set_note_ex()           */
#include "ym2203_full6_vgm.h"  /* FULL6_VGM_CATALOG[], FULL6_VGM_CATALOG_COUNT,
                                          VGM6_FM_POOL[], full6_vgm_note_on/off/all_off */

#define ADDR_DELAY    0x40000000u
#define ADDR_PRE_EQ   0x40003000u
#define ADDR_VCA      0x40004000u
#define ADDR_LFO_0    0x40001000u  /* delay_LFO  (nco_multi_lfo) */
#define ADDR_POST_EQ  0x40002000u  /* post_eq_4band_0 */
#define ADDR_DMA      0x40400000u
#define ADDR_DMA_BUF  0x1E000000u   /* reserved DDR3 오디오 캡처 버퍼 (8MB) */
#define DMA_BUF_SIZE  0x00800000u   /* 8MB */

                                          /* AXI DMA S2MM 레지스터 오프셋 (Xilinx PG021) */
#define DMA_S2MM_DMACR   0x30u   /* Control */
#define DMA_S2MM_DMASR   0x34u   /* Status  */
#define DMA_S2MM_DA      0x48u   /* Destination Address */
#define DMA_S2MM_LENGTH  0x58u   /* Transfer Length (bytes) */

#define DMA_CR_RUN       (1u << 0)
#define DMA_CR_RESET     (1u << 2)
#define DMA_CR_IOC_IRQEN (1u << 12)  /* Interrupt on Complete */
#define DMA_SR_IDLE      (1u << 1)
#define DMA_SR_IOC_IRQ   (1u << 12)

/* 녹음 샘플레이트: 로그 역산 확정 (57,147,392샘플 / 1190.6초 = 47998.8Hz ≈ 48kHz) */
#define REC_SAMPLE_RATE  48000u   /* [FIX-REC1] 실측 48kHz 확정 */
/* WAV SD 저장 경로: FAT32(p1) + ext4(p2 = rootfs) 양쪽에 저장 */
#define REC_WAV_DIR_FAT  "/run/media/BOOT-mmcblk0p1"  /* FAT32 첫 번째 파티션 */
#define REC_WAV_DIR_EXT4 "/home/root/recordings"       /* ext4 루트 파티션 내 디렉토리 */

/* ── 링버퍼 청크 설계 ──────────────────────────────────────────────────
 *  물리 버퍼 8MB를 DMA_CHUNK_COUNT개로 균등 분할.
 *  DMA는 청크 단위 순환, 청크 완료 시 SD에 append → 시간 제한 없음.
 *  DMA_CHUNK_SIZE: AXI DMA LENGTH 레지스터는 23비트(8MB-1)까지 지원.
 *  2MB(0x200000) × 4 = 8MB = DMA_BUF_SIZE.
 *  48000Hz stereo 16bit 기준: 2MB / (48000×4) ≈ 10.9초/청크.
 * ─────────────────────────────────────────────────────────────────── */
#define DMA_CHUNK_COUNT  4u
#define DMA_CHUNK_SIZE   (DMA_BUF_SIZE / DMA_CHUNK_COUNT)  /* 2MB */
#define ADDR_YM2203   0x41210000u
#define ADDR_LCD_CTL  0x41230000u
#define ADDR_FIFO_SG  0x41240000u
#define ADDR_CODE     0x41200000u
#define ADDR_rst      0x41220000u

 /* ── SPI ── */
#define SPI_DEV_LCD    "/dev/spidev0.0"
#define SPI_DEV_ESP32  "/dev/spidev1.0"
#define SPI_ESP32_HZ   1000000u
#define LCD_SPI_HZ     10000000u
#define LCD_W          320
#define LCD_H          240

/* ── LCD 색상 ── */
#define COLOR_BLACK   0x0000u
#define COLOR_WHITE   0xFFFFu
#define COLOR_GREEN   0x07E0u
#define COLOR_RED     0xF800u
#define COLOR_CYAN    0x07FFu
#define COLOR_GREY    0x7BEFu
#define COLOR_YELLOW  0xFFE0u
#define COLOR_DKGREY  0x39E7u
#define COLOR_MAGENTA 0xF81Fu
#define COLOR_DKGREEN 0x03E0u
#define COLOR_ORANGE  0xFD20u
#define COLOR_LTBLUE  0x867Fu

/* ── ADC / 조이스틱 ── */
#define ADC_MAX      4095u
#define ADC_CENTER   2048u
#define JOY_LOW      1000u
#define JOY_HIGH     3000u
#define JOY_DEAD_LO  1848u
#define JOY_DEAD_HI  2248u

/* ── 스위치 ── */
/* 물리 스위치 5개 (조이스틱 내장 포함):
 *   SW1 = VOL_UP   : 볼륨업 / EQ 배율 증가
 *   SW2 = VOL_DOWN : 볼륨다운 / EQ 배율 감소
 *   SW3 = SELECT   : 확인 / 서브메뉴 진입 / ON-OFF 토글
 *   SW4 = EXIT     : 뒤로가기 / 상위 복귀 (현재 설정 유지)
 *   SW5 = SAVE     : 저장 확정 전용
 *                    (구 SW_CANCEL 제거 ? EXIT가 뒤로가기+취소 역할 담당)
 *
 *   M4_FM_OP 에서:
 *     SW3 = 미리듣기    SW4 = 뒤로(FM_INST화면)
 *     SW5 = 저장확정    조이스틱 내장SW = OP 순환 이동 (JX 롱프레스: MUTE토글)
 */
#define SW_VOL_UP   1
#define SW_VOL_DOWN 2
#define SW_SELECT   3
#define SW_EXIT     4
#define SW_CANCEL   SW_EXIT  /* 호환성 alias: EXIT가 취소 역할 겸임, 별도 핀 없음 */
#define SW_SAVE     5        /* SW5: 저장/확정 */
 /* [FIX] m4_update_dly_edit 등에서 SW1/SW2로 참조 ? SW_VOL_UP/DOWN alias */
#define SW1  SW_VOL_UP
#define SW2  SW_VOL_DOWN

 /* ── FSM 모드 번호 ── */
#define ZYNQ_MODE_MENU       0
#define ZYNQ_MODE_SYNTH      1
#define ZYNQ_MODE_VGM        2
#define ZYNQ_MODE_MIDIVIS    3
#define ZYNQ_MODE_INSTSETUP  4
#define ZYNQ_MODE_COUNT      5

/* ── Q 포맷 ── */
#define Q15_ONE   0x00007FFFu
#define Q22_ONE   0x00400000u
#define Q24_1_00  0x01000000u
/* MS_TO_SAMPLES_Q8: ms → reg_delay_time Q16.8 변환
 * RTL 오디오 샘플레이트 = 48kHz (AXI-Stream fire_in 기준).
 * 1ms @ 48kHz = 48 samples → Q16.8 = 48×256 = 12288.
 * analog_delay.h ADLY_MS_TO_Q168()과 동일 공식.
 * ※ 구버전 "50kHz / ×12800" 은 오류였음 ? RTL은 48kHz 기준. */
#define MS_TO_SAMPLES_Q8(ms) ((uint32_t)((double)(ms)*12288.0))

 /* ── SPI 패킷 ── */
typedef struct {
    uint16_t adc_vol;
    uint16_t adc_pitch;
    uint16_t joy_x;
    uint16_t joy_y;
    uint8_t  sw_status;
    uint8_t  active_mode;
    uint8_t  sync_marker;
    uint8_t  midi_status;
    uint8_t  midi_note;
    uint8_t  midi_vel;
    uint8_t  padding[14];
} __attribute__((packed)) spi_packet_t;
typedef char _spi_sz_check[(sizeof(spi_packet_t) == 28) ? 1 : -1];

/* ── UIO 디바이스 ── */
typedef struct { int fd; void* map; size_t size; } uio_dev_t;

/* ── Biquad 계수 ── */
#ifndef BIQUAD_COEF_DEFINED
#define BIQUAD_COEF_DEFINED
typedef struct { uint32_t b0, b1, b2, a1, a2; } BiquadCoef;
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "eq_4band_presets.h"

/* [EQ8-FIX] eq_8band_presets.h uses EQ_REG_CTRL, ensure defined before include */
#ifndef EQ_REG_CTRL
#define EQ_REG_CTRL 0x100u
#endif
#include "eq_8band_presets.h"   /* PRE 4band + POST 4band = 8band unified preset */

/* =============================================================================
 * pre_eq_4band.v ctrl 레지스터 (@ 0x100)
 *   bit0 = bypass     : 1이면 AXI-Stream passthrough, ST_IDLE에서 FSM 진입 차단
 *   bit1 = coef_bypass: 1이면 ST_LOAD에서 shadow → 워킹 레지스터 래치 freeze
 *                       (shadow write 자체는 coef_bypass 무관하게 항상 허용)
 *
 * 계수 write 정상 순서:
 *   1) ctrl = PRE_EQ_CTRL_COEF_BYPASS (0x02): FSM 동작 유지, shadow 래치만 freeze
 *   2) 20개 계수 shadow write
 *   3) ctrl = PRE_EQ_CTRL_RUN        (0x00): 다음 ST_LOAD에서 새 계수 반영
 * ============================================================================= */
#define PRE_EQ_CTRL_BYPASS        0x00000001u
#define PRE_EQ_CTRL_COEF_BYPASS   0x00000002u
#define PRE_EQ_CTRL_RUN           0x00000000u

 /* =============================================================================
  * [FIX-EQ-COMPAT]
  * eq_4band_presets.h를 먼저 include한 뒤, Mode 4 EQ 상세 편집에 필요한
  * 확장 타입/상수/헬퍼가 없는 구버전 헤더에서만 보완한다.
  * ============================================================================= */
  /* =============================================================================
   * [EQ-COMPAT-Rev3]
   * eq_4band_presets.h Rev.3 include 시 EQ_GAIN_MIN_DB가 정의되어 이 블록 전체 스킵.
   * 헤더 없이 단독 컴파일 시에만 아래 정의가 활성화됨.
   * Rev.3: BELL/LO-SHF/HI-SHF/HPF/LPF/BPF/NOTCH/TILT/APF/BYPASS = 10종
   * ============================================================================= */
#ifndef EQ_GAIN_MIN_DB

typedef enum {
    EQ_FTYPE_BELL = 0,
    EQ_FTYPE_LOW_SHELF = 1,
    EQ_FTYPE_HIGH_SHELF = 2,
    EQ_FTYPE_HPF = 3,
    EQ_FTYPE_LPF = 4,
    EQ_FTYPE_BPF = 5,   /* 밴드패스 (fc + Q) */
    EQ_FTYPE_NOTCH = 6,   /* 노치/대역제거 (fc + Q) */
    EQ_FTYPE_TILT = 7,   /* 틸트/스펙트럼기울기 (fc + gain_dB + S) */
    EQ_FTYPE_APF = 8,   /* 전역통과/위상 (fc + Q) */
    EQ_FTYPE_BYPASS = 9,
    EQ_FTYPE_COUNT = 10
} eq_filter_type_t;

static const char* const EQ_FTYPE_NAMES[EQ_FTYPE_COUNT] = {
    "BELL",    /* 0 */
    "LO-SHF",  /* 1 */
    "HI-SHF",  /* 2 */
    "HPF",     /* 3 */
    "LPF",     /* 4 */
    "BPF",     /* 5 */
    "NOTCH",   /* 6 */
    "TILT",    /* 7 */
    "APF",     /* 8 */
    "BYPASS",  /* 9 */
};

/*
 *  파라미터 개수 (Mode 4 EQ 편집기가 표시할 행 수 결정)
 *    3 → Freq / Gain / Q or S   (BELL, LO-SHF, HI-SHF, TILT)
 *    2 → Freq / Q               (HPF, LPF, BPF, NOTCH, APF)
 *    0 → 없음                    (BYPASS)
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

#define EQ_GAIN_MIN_DB       (-18.0)
#define EQ_GAIN_MAX_DB       ( 18.0)

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
#define EQ_FC_MIN_BPF         40.0
#define EQ_FC_MAX_BPF      16000.0
#define EQ_FC_MIN_NOTCH       20.0
#define EQ_FC_MAX_NOTCH    18000.0
#define EQ_FC_MIN_TILT       100.0
#define EQ_FC_MAX_TILT      5000.0
#define EQ_FC_MIN_APF         20.0
#define EQ_FC_MAX_APF      20000.0

#define EQ_FC_DEFAULT_BAND0  120.0
#define EQ_FC_DEFAULT_BAND1  600.0
#define EQ_FC_DEFAULT_BAND2  2500.0
#define EQ_FC_DEFAULT_BAND3  8000.0

#define EQ_Q_DEFAULT_BELL    1.0
#define EQ_Q_DEFAULT_HPF     0.707
#define EQ_Q_DEFAULT_LPF     0.707
#define EQ_Q_DEFAULT_BPF     1.0
#define EQ_Q_DEFAULT_NOTCH   5.0
#define EQ_Q_DEFAULT_APF     0.707
#define EQ_Q_MIN             0.300
#define EQ_Q_MAX            10.000
#define EQ_Q_NOTCH_MAX      30.000
#define EQ_S_DEFAULT         0.707
#define EQ_S_MIN             0.300
#define EQ_S_MAX             1.500

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

static inline const char* eq_p2_name(eq_filter_type_t t)
{
    switch (t) {
    case EQ_FTYPE_LOW_SHELF:
    case EQ_FTYPE_HIGH_SHELF:
    case EQ_FTYPE_TILT:   return "S";
    default:              return "Q";
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

static inline void eq_clamp_params(eq_filter_type_t t, double* fc, double* gain_db, double* p2)
{
    double lo = 0.0, hi = 0.0;
    eq_fc_range(t, &lo, &hi);
    if (*fc < lo) *fc = lo;
    if (*fc > hi) *fc = hi;
    if (*gain_db < EQ_GAIN_MIN_DB) *gain_db = EQ_GAIN_MIN_DB;
    if (*gain_db > EQ_GAIN_MAX_DB) *gain_db = EQ_GAIN_MAX_DB;
    eq_p2_range(t, &lo, &hi);
    if (*p2 < lo) *p2 = lo;
    if (*p2 > hi) *p2 = hi;
}

static inline BiquadCoef eq_calc_band(eq_filter_type_t type, double fc, double gain_db, double p2)
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

#endif /* EQ_GAIN_MIN_DB */

/* [LIGHT-CHAIN FINAL]
 * WARM / SVF / REVERB / LFO1은 팀원 요청에 따라 코드 의존성에서 제거한다.
 * 따라서 warm_engine.h / sound_body.h / fdn_reverb.h는 include하지 않는다.
 * 기존 전체 FSM/LCD 구조는 유지하되, 해당 블록의 런타임 경로만 비활성화한다.
 */
#define ANALOG_DELAY_NO_DUMP   /* dump는 별도 delay_readback()에서 처리 */
#include "analog_delay.h"

 /* === analog_delay v6.0 호환 shim (자동 삽입) === */
 /* ============================================================
  * analog_delay v6.0 ↔ v7.0 호환 shim
  * (analog_delay.h v6.0 include 직후 삽입)
  * ============================================================ */

  /* --- 구조체 필드 alias (v7.0 코드가 .regs 등을 참조) --- */
  /* v6.0: base_delay / base_lfo / map_size_delay / map_size_lfo
   * v7.0: regs / uio_fd / mmap_size / phys_base
   * 해결: analog_delay_t 확장 래퍼 대신 매크로 alias 불가(struct 멤버).
   * → adly_adapter_init()에서 .base_delay에 대입하도록 .regs 참조를 패치.
   * 아래는 .uio_fd / .mmap_size / .phys_base 가짜 필드를 지역 변수로 무시 처리. */

   /* --- v7.0 전용 함수 shim (inline) --- */
static inline void analog_delay_bypass_on(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 1);
}
static inline void analog_delay_bypass_off(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 0);
}
static inline void analog_delay_feedback_off(analog_delay_t* d)
{
    analog_delay_set_feedback_q15(d, 0u);
}
static inline float analog_delay_get_feedback_pct(const analog_delay_t* d)
{
    return ADLY_Q15_TO_PCT(analog_delay_get_feedback_q15(d));
}
static inline float analog_delay_get_dry_wet_pct(const analog_delay_t* d)
{
    return ADLY_Q15_TO_PCT(analog_delay_get_dry_wet_q15(d));
}

/* --- tap / wow_depth ? v6.0 RTL에 없는 기능: 무작동 stub --- */
#ifndef ADLY_OFF_TAP_EN
#define ADLY_OFF_TAP_EN          0xFFu   /* dummy ? RTL 없음, 쓰기 무시됨 */
#define ADLY_OFF_TAP1_DT         0xFFu
#define ADLY_OFF_TAP2_DT         0xFFu
#define ADLY_OFF_TAP1_GAIN       0xFFu
#define ADLY_OFF_TAP2_GAIN       0xFFu
#define ADLY_OFF_TAP1_FB_GAIN    0xFFu
#define ADLY_OFF_TAP_FB_GAIN     0xFFu
#define ADLY_OFF_WOW_DEPTH       0xFFu
#define ADLY_OFF_DITHER_EN       0xFFu
#define ADLY_OFF_APF_FB_EN       0xFFu
#define ADLY_OFF_APF_FB_MIX      0xFFu
#define ADLY_FB_MAX_PCT          75.0f
#define ADLY_SLEW_DEFAULT        ADLY_SLEW_ULTRA_SLOW
#endif

static inline void analog_delay_set_wow_depth(analog_delay_t* d, uint8_t depth)
{
    (void)d; (void)depth; /* wow_depth: v6.0 RTL에 없음 */
}
static inline void analog_delay_tap1_enable(analog_delay_t* d, float ms, float gain)
{
    (void)d; (void)ms; (void)gain;
}
static inline void analog_delay_tap1_disable(analog_delay_t* d)
{
    (void)d;
}
static inline void analog_delay_tap2_enable(analog_delay_t* d, float ms, float gain)
{
    (void)d; (void)ms; (void)gain;
}
static inline void analog_delay_tap2_disable(analog_delay_t* d)
{
    (void)d;
}

/* =============================================================================
 * 딜레이 프리셋 ? analog_delay.h v6.0 RTL(Rev.9) 실제 파라미터 기반
 *
 * RTL 제약:
 *   damp_alpha/hf ≤ 0x7FFF,  dry_wet ≤ 0x7FFF,  feedback RTL상한 0x6000(75%)
 *   diffusion_start=0→APF ON, ≥1→APF OFF,  sat_thresh 최솟값 0x0100
 *   slew: INSTANT=0xFFFF / FAST=0x1400 / MEDIUM=0x0500 / SLOW=0x0100 / DRIFT=0x0040
 * ============================================================================= */

 /** TAPE ECHO: Echoplex/SpaceEcho 계열. 320ms, HF 롤오프, APF 확산, 느린 슬루 */
static inline void analog_delay_preset_tape_echo(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 0);

    /* slew INSTANT 먼저 → time 레지스터 목표값 기록.
     * RTL 슬루는 HW 샘플 클럭(48kHz=20μs/sample)으로 동작.
     * CPU 쓰기가 μs 단위이므로 INSTANT→time→SLOW 순서로만 쓰면
     * INSTANT가 겨우 몇 샘플만 적용되고 SLOW로 바뀐다.
     * 해결: INSTANT로 쓴 후 충분한 시간(50ms = 2400샘플) 대기 →
     *        이 동안 RTL이 256ms→320ms를 완전히 수렴 → 이후 SLOW 복원. */
    analog_delay_set_slew_rate(d, ADLY_SLEW_INSTANT);
    analog_delay_set_time_ms(d, 320.0f);
    /* [FIX] usleep 제거: 메인루프 블로킹 → SPI sync_fail → delay_mute() → bypass ON
     * INSTANT slew는 RTL 샘플클럭(48kHz)으로 자동 수렴 ? CPU 대기 불필요 */

     /* [TUNING v2] 돌림노래/타닥타닥 효과 극대화:
      *   feedback 75%: RTL 상한(0x6000) 풀 활용 → 에코 5~6회 뚜렷하게 반복
      *   dry/wet 70%: 에코가 원음과 거의 동등한 볼륨 → 명확한 겹침 효과
      *   time 380→320ms: 약간 짧게 → 박자감 있는 타닥타닥 리듬감
      *   diffusion 완전 OFF: APF 비활성 → 에코 경계 선명, 흐려짐 없음
      *   damp 완전 OFF: 피드백 루프 HF 유지 → 각 에코가 원음처럼 선명
      *   sat_thresh 높게: 클리핑 없이 깨끗한 에코 반복 */
    analog_delay_set_feedback_pct(d, 75.0f);   /* RTL 상한 풀 활용 */
    analog_delay_set_dry_wet_pct(d, 70.0f);    /* 에코 존재감 강화 */
    /* diffusion 완전 OFF: 에코가 흐려지지 않고 돌림노래처럼 선명하게 반복 */
    analog_delay_set_diffusion(d, 0, 0.0f, 0.0f);
    analog_delay_set_lfo_depth_apf(d, 0.0f);   /* LFO 변조 OFF → 피치 안정 */
    analog_delay_set_slew_rate(d, ADLY_SLEW_INSTANT); /* 즉각 수렴 */
    analog_delay_set_sat_thresh(d, 30000u);     /* 클리핑 거의 없음 */
    analog_delay_set_ms_width_q15(d, 0x2000u); /* M/S 확산 적당히 */
    /* damp 완전 OFF: 에코 반복마다 HF 유지 → 선명한 돌림노래/메아리 */
    analog_delay_set_damp_alpha_q15(d, 0x0000u);
    analog_delay_set_damp_hf_q15(d, 0x0000u);
}

/** ROOM AMB: 800ms, 넓은 공간감, APF 확산, SLOW 슬루 */
static inline void analog_delay_preset_room_ambience(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 0);
    analog_delay_set_time_ms(d, 800.0f);
    analog_delay_set_feedback_pct(d, 68.0f);
    analog_delay_set_dry_wet_pct(d, 40.0f);
    analog_delay_set_diffusion(d, 1, 0.40f, 0.48f);
    analog_delay_set_lfo_depth_apf(d, 0.006f);
    analog_delay_set_slew_rate(d, ADLY_SLEW_SLOW);
    analog_delay_set_sat_thresh(d, 22000u);
    analog_delay_set_ms_width_q15(d, 0x5000u);
    analog_delay_set_damp_alpha_q15(d, 0x4000u);
    analog_delay_set_damp_hf_q15(d, 0x1000u);
}

/** BBD THIK: 180ms, 강한 HF 롤오프+APF 확산으로 두께감 */
static inline void analog_delay_preset_bbd_short_thicken(analog_delay_t* d)
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

/** PINGPONG: BPM 기반 점8분 딜레이, APF OFF, M/S 최대 확산 */
static inline void analog_delay_preset_pingpong_dotted(analog_delay_t* d, float bpm)
{
    float qn_ms = (bpm > 20.0f && bpm < 300.0f) ? (60000.0f / bpm) : 500.0f;
    float dt_ms = qn_ms * 0.75f;
    if (dt_ms < ADLY_MIN_DELAY_MS) dt_ms = ADLY_MIN_DELAY_MS;
    if (dt_ms > ADLY_MAX_DELAY_MS) dt_ms = ADLY_MAX_DELAY_MS;
    analog_delay_set_bypass(d, 0);
    analog_delay_set_time_ms(d, dt_ms);
    analog_delay_set_feedback_pct(d, 55.0f);
    analog_delay_set_dry_wet_pct(d, 42.0f);
    analog_delay_set_diffusion(d, 0, 0.0f, 0.5f);
    analog_delay_set_lfo_depth_apf(d, 0.0f);
    analog_delay_set_slew_rate(d, ADLY_SLEW_FAST);
    analog_delay_set_sat_thresh(d, 0x7FFFu);
    analog_delay_set_ms_width_q15(d, 0x7000u);
    analog_delay_set_damp_alpha_q15(d, 0x1000u);
    analog_delay_set_damp_hf_q15(d, 0x0000u);
}

/** GHOST: 600ms, 낮은 wet, 강한 APF 확산, 은은한 공간감 */
static inline void analog_delay_preset_ghost_echo(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 0);
    analog_delay_set_time_ms(d, 600.0f);
    analog_delay_set_feedback_pct(d, 62.0f);
    analog_delay_set_dry_wet_pct(d, 28.0f);
    analog_delay_set_diffusion(d, 1, 0.65f, 0.58f);
    analog_delay_set_lfo_depth_apf(d, 0.010f);
    analog_delay_set_slew_rate(d, ADLY_SLEW_SLOW);
    analog_delay_set_sat_thresh(d, 23000u);
    analog_delay_set_ms_width_q15(d, 0x5800u);
    analog_delay_set_damp_alpha_q15(d, 0x5000u);
    analog_delay_set_damp_hf_q15(d, 0x1800u);
}

/** BITCRUSH: 125ms, 낮은 sat_thresh(0x0800)으로 강한 소프트클립, 8bit 질감 */
static inline void analog_delay_preset_bitcrush(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 0);
    analog_delay_set_time_ms(d, 125.0f);
    analog_delay_set_feedback_pct(d, 55.0f);
    analog_delay_set_dry_wet_pct(d, 50.0f);
    analog_delay_set_diffusion(d, 0, 0.0f, 0.5f);
    analog_delay_set_lfo_depth_apf(d, 0.0f);
    analog_delay_set_slew_rate(d, ADLY_SLEW_INSTANT);
    analog_delay_set_sat_thresh(d, 0x0800u);
    analog_delay_set_ms_width_q15(d, 0x2000u);
    analog_delay_set_damp_alpha_q15(d, 0x6000u);
    analog_delay_set_damp_hf_q15(d, 0x4000u);
}

/** REVERSE: ULTRA_SLOW 슬루로 딜레이가 천천히 이동하는 리버스 질감 */
static inline void analog_delay_preset_reverse_sim(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 0);
    analog_delay_set_time_ms(d, 700.0f);
    analog_delay_set_feedback_pct(d, 65.0f);
    analog_delay_set_dry_wet_pct(d, 45.0f);
    analog_delay_set_diffusion(d, 1, 0.70f, 0.60f);
    analog_delay_set_lfo_depth_apf(d, 0.015f);
    analog_delay_set_slew_rate(d, ADLY_SLEW_ULTRA_SLOW);
    analog_delay_set_sat_thresh(d, 19000u);
    analog_delay_set_ms_width_q15(d, 0x4800u);
    analog_delay_set_damp_alpha_q15(d, 0x5800u);
    analog_delay_set_damp_hf_q15(d, 0x2800u);
}

/** DAEGEUM: 대금. 500ms, HF 댐핑 OFF, APF 얕게 ? 청아한 맑은 톤 */
static inline void analog_delay_preset_daegeum(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 0);
    analog_delay_set_time_ms(d, 500.0f);
    analog_delay_set_feedback_pct(d, 52.0f);
    analog_delay_set_dry_wet_pct(d, 35.0f);
    analog_delay_set_diffusion(d, 1, 0.30f, 0.42f);
    analog_delay_set_lfo_depth_apf(d, 0.005f);
    analog_delay_set_slew_rate(d, ADLY_SLEW_SLOW);
    analog_delay_set_sat_thresh(d, 25000u);
    analog_delay_set_ms_width_q15(d, 0x3800u);
    analog_delay_set_damp_alpha_q15(d, 0x2000u);
    analog_delay_set_damp_hf_q15(d, 0x0000u);
}

/** GAYAGEUM: 가야금. 280ms, 높은 피드백, 맑고 긴 현악 공명 */
static inline void analog_delay_preset_gayageum(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 0);
    analog_delay_set_time_ms(d, 280.0f);
    analog_delay_set_feedback_pct(d, 68.0f);
    analog_delay_set_dry_wet_pct(d, 38.0f);
    analog_delay_set_diffusion(d, 1, 0.45f, 0.50f);
    analog_delay_set_lfo_depth_apf(d, 0.007f);
    analog_delay_set_slew_rate(d, ADLY_SLEW_MEDIUM);
    analog_delay_set_sat_thresh(d, 24000u);
    analog_delay_set_ms_width_q15(d, 0x4800u);
    analog_delay_set_damp_alpha_q15(d, 0x3000u);
    analog_delay_set_damp_hf_q15(d, 0x0800u);
}

/** HAEGEUM: 해금. 160ms, 강한 HF 댐핑, 활 마찰음 특성 */
static inline void analog_delay_preset_haegeum(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 0);
    analog_delay_set_time_ms(d, 160.0f);
    analog_delay_set_feedback_pct(d, 58.0f);
    analog_delay_set_dry_wet_pct(d, 40.0f);
    analog_delay_set_diffusion(d, 1, 0.50f, 0.52f);
    analog_delay_set_lfo_depth_apf(d, 0.009f);
    analog_delay_set_slew_rate(d, ADLY_SLEW_FAST);
    analog_delay_set_sat_thresh(d, 21000u);
    analog_delay_set_ms_width_q15(d, 0x3000u);
    analog_delay_set_damp_alpha_q15(d, 0x6000u);
    analog_delay_set_damp_hf_q15(d, 0x3800u);
}

/** PIRI: 피리. 420ms, 따뜻한 중역 두께감 */
static inline void analog_delay_preset_piri(analog_delay_t* d)
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

/** AJAENG: 아쟁. 650ms, sat_thresh 17000, 강한 저역 포화+긴 여운 */
static inline void analog_delay_preset_ajaeng(analog_delay_t* d)
{
    analog_delay_set_bypass(d, 0);
    analog_delay_set_time_ms(d, 650.0f);
    analog_delay_set_feedback_pct(d, 62.0f);
    analog_delay_set_dry_wet_pct(d, 44.0f);
    analog_delay_set_diffusion(d, 1, 0.60f, 0.55f);
    analog_delay_set_lfo_depth_apf(d, 0.008f);
    analog_delay_set_slew_rate(d, ADLY_SLEW_DRIFT);
    analog_delay_set_sat_thresh(d, 17000u);
    analog_delay_set_ms_width_q15(d, 0x4000u);
    analog_delay_set_damp_alpha_q15(d, 0x6800u);
    analog_delay_set_damp_hf_q15(d, 0x3500u);
}

/* --- g_warm_knob / g_svf_knob stub (LIGHT: warm/svf 제거됨) --- */
static struct { uint16_t sv; uint16_t sp; uint8_t armed; }
g_warm_knob = { 0 }, g_svf_knob = { 0 };

/* === shim 끝 === */

#include "vca_compressor.h"

 /* ── WARM/SVF/FDN 최소 상수 (하드웨어 제거됨, 구조체 필드 초기화용만 유지) ── */
#define SVF_BODY_DEFAULT   0u
#define FDN_SP_HALL        0u
#define FDN_SP_NUM         2u

// =============================================================================
//  [CHAIN DIAG]  신호 체인 단계별 진단 ? 매크로 & 프로토타입
//  (함수 정의는 B1 섹션 p_* 변수 선언 이후에 위치)
//
//  컴파일: -DCHAIN_DIAG_LEVEL=N  (0=비활성 1=부팅덤프 2=주기덤프 3=verbose)
// =============================================================================
#ifndef CHAIN_DIAG_LEVEL
#define CHAIN_DIAG_LEVEL 2
#endif

#define _CRED  "\033[1;31m"
#define _CGRN  "\033[1;32m"
#define _CYEL  "\033[1;33m"
#define _CBLU  "\033[1;34m"
#define _CCYN  "\033[1;36m"
#define _CRST  "\033[0m"

#define DIAG_OK(s,f,...)   fprintf(stderr,_CGRN"[DIAG OK ] [%s] "f _CRST"\n",s,##__VA_ARGS__)
#define DIAG_WARN(s,f,...) fprintf(stderr,_CYEL"[DIAG WRN] [%s] "f _CRST"\n",s,##__VA_ARGS__)
#define DIAG_ERR(s,f,...)  fprintf(stderr,_CRED"[DIAG ERR] [%s] "f _CRST"\n",s,##__VA_ARGS__)
#define DIAG_INFO(s,f,...) fprintf(stderr,_CCYN"[DIAG INF] [%s] "f _CRST"\n",s,##__VA_ARGS__)

/* 함수 프로토타입 (정의는 B1 이후) */
static int  diag_check_all_uio(void);
static void diag_dump_chain(const char* when);
static void diag_periodic_dump(void);
static inline void _diag_vca_makeup_guard(uint32_t val, const char* caller);
static inline void _diag_delay_dw_guard(uint32_t val, const char* caller);
static void diag_pt2258_gate_check(int mode);
// =============================================================================
//  [CHAIN DIAG END]
// =============================================================================

/* ── 전방 선언: 정의가 뒤에 있는 전역 변수 & 함수 ── */
/* NOTE: static 변수는 C에서 두 번 정의 불가 → 정의를 이 위치로 끌어올림 */
static volatile uint8_t* p_delay = NULL; /* → B1에서 대입 */
static int      fd_pt2258 = -1;
static int      g_pt2258_addr = -1;
static uint8_t  g_pt2258_att_db = 15;   /* ALL 채널 공통 감쇠 (legacy 경로용) */
static uint8_t  g_pt2258_att_l_db = 15; /* CH1(L) 감쇠 ? ADC_VOL  가변저항 */
static uint8_t  g_pt2258_att_r_db = 15; /* CH2(R) 감쇠 ? ADC_PITCH 가변저항 */
static uint8_t  g_pt2258_muted = 1;
/* pt2258_set_all_volume_db: 정의가 B4-PT 섹션에 있으므로 전방 선언 */
static void pt2258_set_all_volume_db(int att_db);
/* eq_band_flush / eq_band_flush_post: 정의가 Mode4 섹션에 있으므로 전방 선언.
 * vgm_start_single(섹션G) 및 mode_midivis_enter(섹션H)에서 사용. */
static void eq_band_flush(void);
static void eq_band_flush_post(void);
/* 호출하나 정의가 섹션I에 있으므로 여기서 전방 선언. */
/* rec_start / rec_stop_and_save: 정의가 REC 섹션에 있으므로 전방 선언 */
static void rec_start(void);
static void rec_stop_and_save(void);

/* ── analog_delay_t 어댑터 (analog_delay.h v7.0 §0 구조체 초기화) ──────── */
static analog_delay_t g_adly;   /* 전역 핸들 ? regs/uio_fd/mmap_size/phys_base */

/* [BUG1-FIX] p_lfo_0 실제 정의는 아래 UIO 포인터 섹션에 있으나,
 * adly_adapter_init()에서 base_lfo 초기화에 사용하므로 전방 선언 필요. */
static volatile uint8_t* p_lfo_0 = NULL;

static void adly_adapter_init(void)
{
    /* p_delay / p_lfo_0 확정 후 호출. */
    if (!p_delay) {
        fprintf(stderr, "[ADLY] adly_adapter_init: p_delay=NULL, 초기화 중단\n");
        return;
    }
    memset(&g_adly, 0, sizeof(g_adly));
    g_adly.base_delay = (volatile uint32_t*)p_delay;
    g_adly.base_lfo = (volatile uint32_t*)p_lfo_0;  /* NULL이면 LFO 미사용 */

    /* [시퀀스 §23] 올바른 초기화 순서:
     *  1. bypass ON  ? 초기화 중 잡음 출력 방지
     *  2. reset_defaults ? 모든 레지스터를 안전 기본값으로 기록
     *  3. lfo_depth_apf 설정
     *  4. lfo_set_for_delay ? CH0/CH4/CH5 APF 변조 파라미터 설정
     *  5. boot_wait ? 리셋 후 dt_slewed≠dt_target 수렴 대기
     *  6. bypass OFF + preset 적용은 apply_dly_custom_from_full_preset()에서 수행 */
    analog_delay_bypass_on(&g_adly);
    analog_delay_reset_defaults(&g_adly);

    if (g_adly.base_lfo) {
        /* [FIX] lfo_depth_apf=0: tape_echo preset이 APF OFF + LFO depth=0으로
         * 세팅하므로, 초기화 시에도 같은 값으로 맞춤.
         * 이전 0.008f는 APF OFF 상태에서도 g_mod에 잔류 변조가 남아
         * echo 신호에 간섭을 일으켜 돌림노래 선명도를 떨어뜨렸음. */
        analog_delay_set_lfo_depth_apf(&g_adly, 0.0f);
        lfo_set_for_delay(&g_adly, 0.04f, 0.50f, 0.30f, 0.01f);
    }

    /* 부팅 슬루 수렴 대기 (rate 0x1000 임시 적용 후 복원) */
    analog_delay_boot_wait(&g_adly, 0x1000u);

    printf("[ADLY] adly_adapter_init 완료: reset_defaults+LFO+boot_wait\n");
}
ym2203_voice_t g_ym_voices[3]; //5_ 5 문제 2. g_ym_voices 전역 배열 정의가 없음
// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 A2]  UIO 유틸리티
// ─────────────────────────────────────────────────────────────────────────────
#define UIO_MAX_DEVICES 32

static int uio_find_index(const char* ip_name)
{
    char path[128], buf[128];
    for (int i = 0; i < UIO_MAX_DEVICES; i++) {
        snprintf(path, sizeof(path), "/sys/class/uio/uio%d/name", i);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        int found = 0;
        if (fgets(buf, sizeof(buf), f)) {
            buf[strcspn(buf, "\r\n")] = '\0';
            found = (strcmp(buf, ip_name) == 0);
        }
        fclose(f);
        if (found) return i;
    }
    return -1;
}

static int uio_find_index_by_addr(uint32_t target)
{
    char path[128], buf[64];
    for (int i = 0; i < UIO_MAX_DEVICES; i++) {
        snprintf(path, sizeof(path),
            "/sys/class/uio/uio%d/maps/map0/addr", i);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        uint32_t addr = 0;
        if (fgets(buf, sizeof(buf), f)) addr = (uint32_t)strtoul(buf, NULL, 0);
        fclose(f);
        if (addr == target) return i;
    }
    fprintf(stderr, "[UIO] addr=0x%08X 탐색 실패\n", target);
    return -1;
}

static size_t uio_read_size(int idx)
{
    char path[128], buf[64];
    snprintf(path, sizeof(path),
        "/sys/class/uio/uio%d/maps/map0/size", idx);
    FILE* f = fopen(path, "r");
    if (!f) return 0x1000;
    size_t sz = 0x1000;
    if (fgets(buf, sizeof(buf), f)) {
        size_t v = (size_t)strtoull(buf, NULL, 0);
        if (v > 0) sz = v;  /* strtoull 실패/0 → 기본값 0x1000 유지 */
    }
    fclose(f);
    return sz;
}

static uio_dev_t uio_mmap_idx(int idx, const char* label, int prot)
{
    uio_dev_t dev = { .fd = -1, .map = NULL, .size = 0 };
    char devpath[64];
    snprintf(devpath, sizeof(devpath), "/dev/uio%d", idx);
    dev.fd = open(devpath, (prot & PROT_WRITE) ? O_RDWR : O_RDONLY);
    if (dev.fd < 0) {
        fprintf(stderr, "[UIO] open %s: %s\n", devpath, strerror(errno));
        return dev;
    }
    dev.size = uio_read_size(idx);
    dev.map = mmap(NULL, dev.size, prot, MAP_SHARED, dev.fd, 0);
    if (dev.map == MAP_FAILED) {
        fprintf(stderr, "[UIO] mmap %s: %s\n", devpath, strerror(errno));
        close(dev.fd); dev.fd = -1; dev.map = NULL;
    }
    else {
        printf("[UIO] %-26s -> %s (size=0x%zx)\n", label, devpath, dev.size);
    }
    return dev;
}

static uio_dev_t uio_open_name_or_addr(const char* name, uint32_t addr, int prot)
{
    int idx = uio_find_index(name);
    if (idx < 0) {
        printf("[UIO] '%s' 이름 탐색 실패 → addr 0x%08X 재탐색\n", name, addr);
        idx = uio_find_index_by_addr(addr);
    }
    if (idx < 0) { uio_dev_t d = { -1,NULL,0 }; return d; }
    return uio_mmap_idx(idx, name, prot);
}

static uio_dev_t uio_open_by_addr(const char* label, uint32_t addr, int prot)
{
    int idx = uio_find_index_by_addr(addr);
    if (idx < 0) { uio_dev_t d = { -1,NULL,0 }; return d; }
    return uio_mmap_idx(idx, label, prot);
}

static void uio_close(uio_dev_t* dev)
{
    if (dev->map && dev->map != MAP_FAILED) {
        munmap(dev->map, dev->size); dev->map = NULL;
    }
    if (dev->fd >= 0) { close(dev->fd); dev->fd = -1; }
}

static int load_uio_module(void)
{
    struct stat st;
    if (stat("/dev/uio0", &st) == 0) { printf("[UIO] 이미 로드됨\n"); return 0; }
    printf("[UIO] modprobe uio_pdrv_genirq ...\n");
    int ret = system("modprobe uio_pdrv_genirq");
    if (ret == -1 || (WIFEXITED(ret) && WEXITSTATUS(ret) != 0)) {
        fprintf(stderr, "[UIO] modprobe 실패\n"); return -1;
    }
    for (int i = 0; i < 30; i++) {
        usleep(100000);
        if (stat("/dev/uio0", &st) == 0) {
            printf("[UIO] /dev/uio0 확인 (%.1fs)\n", (i + 1) * 0.1f);
            return 0;
        }
    }
    fprintf(stderr, "[UIO] /dev/uio0 타임아웃\n"); return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 A3]  레지스터 헬퍼
// ─────────────────────────────────────────────────────────────────────────────
static inline void reg_wr(volatile uint8_t* base, uint32_t off, uint32_t val)
{
    *((volatile uint32_t*)(base + off)) = val;
}

static inline uint32_t reg_rd(volatile uint8_t* base, uint32_t off)
{
    return *((volatile uint32_t*)(base + off));
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 A4]  하드웨어 오프셋 상수
// ─────────────────────────────────────────────────────────────────────────────

/* YM2203 GPIO */
#define YM_GPIO1_OFF   0x00u
#define YM_GPIO1_TRI   0x04u
#define YM_GPIO2_OFF   0x08u
#define YM_GPIO2_TRI   0x0Cu
#define YM_WR_BIT      (1u << 16)
#define YM_SSG_VOL_CH0 0x08u
#define YM_SSG_VOL_CH1 0x09u
#define YM_SSG_VOL_CH2 0x0Au
#define YM_SSG_MIXER   0x07u

/* FIFO */
#define FIFO_STATUS_FULL  (1u << 0)
#define FIFO_STATUS_AF    (1u << 1)
#define FIFO_STATUS_BUSY  (1u << 2)

/* LCD GPIO */
#define LCD_GPIO1_OFF  0x00u
#define LCD_GPIO1_TRI  0x04u
#define LCD_DC         (1u << 1)
#define LCD_RST        (1u << 2)
#define LCD_BL         (1u << 3)
#define LCD_OUT_MASK   0x0Fu

/* DELAY */
/* ── analog_delay_stereo_top Rev.15 레지스터 오프셋
 * analog_delay.h ADLY_OFF_* 가 정규 정의. 아래는 하위 호환 alias. */
#define DLY_TIME_OFF          ADLY_OFF_DELAY_TIME
#define DLY_BYPASS_OFF        ADLY_OFF_BYPASS
#define DLY_FB_OFF            ADLY_OFF_FEEDBACK
#define DLY_DRYWET_OFF        ADLY_OFF_DRY_WET
#define DLY_DIFF_START_OFF    ADLY_OFF_DIFFUSION_START
#define DLY_DIFF_MIX_OFF      ADLY_OFF_DIFFUSION_MIX
#define DLY_DIFF_G_OFF        ADLY_OFF_DIFFUSION_G
#define DLY_LFO_DEPTH_APF_OFF ADLY_OFF_LFO_DEPTH_APF
#define DLY_SLEW_OFF          ADLY_OFF_SLEW_RATE
#define DLY_SAT_THRESH_OFF    ADLY_OFF_SAT_THRESH
#define DLY_BYPASS2_OFF       ADLY_OFF_BYPASS_ALIAS
#define DLY_MS_WIDTH_OFF      ADLY_OFF_MS_WIDTH
#define DLY_OUTHPF_BYP_OFF    ADLY_OFF_OUTHPF_BYPASS
#define DLY_DAMP_ALPHA_OFF    ADLY_OFF_DAMP_ALPHA
#define DLY_DAMP_HF_OFF       ADLY_OFF_DAMP_HF
#define DLY_TAP_EN_OFF        ADLY_OFF_TAP_EN
#define DLY_TAP1_DT_OFF       ADLY_OFF_TAP1_DT
#define DLY_TAP2_DT_OFF       ADLY_OFF_TAP2_DT
#define DLY_TAP1_GAIN_OFF     ADLY_OFF_TAP1_GAIN
#define DLY_TAP2_GAIN_OFF     ADLY_OFF_TAP2_GAIN
#define DLY_WOW_DEPTH_OFF     ADLY_OFF_WOW_DEPTH
#define DLY_DITHER_EN_OFF     ADLY_OFF_DITHER_EN
#define DLY_APF_FB_EN_OFF     ADLY_OFF_APF_FB_EN
#define DLY_APF_FB_MIX_OFF    ADLY_OFF_APF_FB_MIX
#define DLY_TAP_FB_GAIN_OFF   ADLY_OFF_TAP_FB_GAIN
#define DLY_TAP1_FB_GAIN_OFF  ADLY_OFF_TAP1_FB_GAIN
#define DLY_FB_THRESH_MAX     ADLY_FB_MAX_Q15

 /* EQ */
#ifndef EQ_REG_OFF
#define EQ_REG_OFF(b,c) (((uint32_t)((b)*5+(c)))*4u)
#endif

/* ── vca_compressor Rev.5 레지스터 맵 (wr_addr[5:2]×4) ──────────────────────
 * 아래 오프셋은 RTL case(wr_addr[5:2]) 에서 직접 추출. */
#define VCA_THRESH_OFF   0x00u  /* Q5.8  [12:0]  압축 시작 레벨  기본 0x0D00 */
#define VCA_CS_OFF       0x04u  /* Q1.15 [15:0]  압축 비율(slope) 기본 0x6000(4:1) */
#define VCA_MAKEUP_OFF   0x08u  /* Q2.14 [15:0]  makeup gain 0x4000=1.0      */
#define VCA_ATK_OFF      0x0Cu  /* [4:0]  attack  shift 기본 4               */
#define VCA_REL_OFF      0x10u  /* [4:0]  release shift 기본 6               */
#define VCA_ENV_MODE_OFF 0x14u  /* [0]    0=Peak, 1=RMS                      */
#define VCA_KNEE_OFF     0x18u  /* Q5.8  [12:0]  soft knee 폭 기본 0x0200   */
#define VCA_DITHER_OFF   0x1Cu  /* [0]    TPDF dither enable                 */
#define VCA_KCOEFF_OFF   VCA_CS_OFF  /* 구버전 코드 호환 alias */
#define VCA_MAKEUP_ONE   0x4000u     /* makeup 1.0 (Q2.14) */
#define VCA_CS_RATIO_1_1 0x0000u     /* 1:1 (bypass)       */
#define VCA_CS_RATIO_2_1 16384u      /* 2:1                */
#define VCA_CS_RATIO_4_1 24576u      /* 4:1  (RTL reset 기본값) */
#define VCA_CS_RATIO_8_1 28672u      /* 8:1                */

 /* LFO */
#ifndef LFO_OFF
#define LFO_OFF(ch,reg)  (((uint32_t)(ch)*16u+(uint32_t)(reg)*4u))
#endif
#define LFO_FCW_0_1HZ    0x0000000Au
#define LFO_FCW_10HZ     0x000005B0u
#define LFO_FCW_OFF      0x00000000u

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 A5]  타이밍 / 신호 헬퍼
// ─────────────────────────────────────────────────────────────────────────────
#define MEM_BARRIER()  __sync_synchronize()

static inline void msleep(long ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

static inline uint16_t adc_smooth(uint16_t prev, uint16_t nv)
{
    return (uint16_t)(((uint32_t)prev * 7u + (uint32_t)nv) / 8u);
}

static inline uint32_t adc_to_lfo_fcw(uint16_t adc)
{
    return LFO_FCW_0_1HZ + (uint32_t)((uint64_t)adc * (LFO_FCW_10HZ - LFO_FCW_0_1HZ) / ADC_MAX);
}

static inline uint8_t adc_to_ssg_vol(uint16_t adc)
{
    return (uint8_t)((uint32_t)adc * 15u / ADC_MAX);
}

static volatile sig_atomic_t g_exit = 0;
static void sig_handler(int sig) { (void)sig; g_exit = 1; }

// =============================================================================
//  [섹션 B]  하드웨어 드라이버
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 B1]  전역 UIO 핸들 & 포인터
// ─────────────────────────────────────────────────────────────────────────────
/* ── UIO 핸들 (BD 인스턴스 1:1 대응) ── */
static uio_dev_t uio_delay;
static uio_dev_t uio_pre_eq;
static uio_dev_t uio_vca;
static uio_dev_t uio_lfo_0;
static uio_dev_t uio_post_eq;
static uio_dev_t uio_DMA;
static uio_dev_t uio_DMA_BUF;   /* 예약 DDR3 캡처 버퍼 (audio_dma_buffer) */
static uio_dev_t uio_ym2203;

/* ── 녹음 상태 ──────────────────────────────────────────────────────────── */
typedef enum { REC_IDLE, REC_RUNNING, REC_STOPPING } rec_state_t;
static volatile rec_state_t  g_rec_state = REC_IDLE;
static volatile uint32_t     g_rec_bytes = 0;       /* 현재까지 수신된 총 바이트 */
volatile uint8_t             g_m3_lsw_reset = 0;   /* [FIX-REC2b] 모드 재진입 시 lsw 초기화 플래그 (모드3) */
volatile uint8_t             g_m1_lsw_reset = 0;   /* [FIX-REC2b] 모드 재진입 시 lsw 초기화 플래그 (모드1) */
static struct timespec       g_rec_saved_ts = { 0, 0 };  /* 저장 완료 시각 (모드1/3 공유, 0=없음) */
static volatile uint32_t     g_rec_wrap = 0;       /* 8MB 경계 넘은 횟수 (링버퍼) */
static pthread_t             g_rec_thread;
static volatile int          g_rec_thread_exit = 0;
/* 링버퍼 추가 상태 */
static FILE* g_rec_file = NULL;  /* 녹음 중 오픈된 WAV 파일 (FAT32) */
static FILE* g_rec_file_ext4 = NULL;  /* 녹음 중 오픈된 WAV 파일 (ext4)  */
static uint32_t g_rec_idx = 0;        /* 파일 번호 카운터 */
static volatile uint32_t     g_rec_chunk_idx = 0;   /* 현재 쓸 청크 인덱스 (0~3 순환) */
static uio_dev_t uio_lcd_ctrl;
static uio_dev_t uio_fifo_sg;
static uio_dev_t uio_CODE;
static uio_dev_t uio_rst_gpio;


/* ── 레지스터 포인터 ── */
static volatile uint8_t* p_pre_eq = NULL;
static volatile uint8_t* p_vca = NULL;
/* p_lfo_0: 전방 선언이 adly_adapter_init 앞에 위치 (BUG1-FIX 참조) */
/* p_lfo: delay_LFO(ch0) 의 alias ? B6 LFO 드라이버가 참조하는 단일 포인터 */
#define p_lfo p_lfo_0
static volatile uint8_t* p_post_eq = NULL;
static volatile uint8_t* p_ym = NULL;
static volatile uint8_t* p_fifosg = NULL;
static volatile const uint8_t* p_efin = NULL;
static volatile uint8_t* p_lcd = NULL;
static volatile uint8_t* p_code = NULL;  /* CODE GPIO (PT2258 주소핀) */
static volatile uint8_t* p_rst_gpio = NULL;

static int fd_lcd = -1;
static int fd_esp = -1;

// =============================================================================
//  [CHAIN DIAG IMPL]  신호 체인 진단 함수 구현체
//  (p_* 변수 선언 이후에 위치 ? forward-reference 경고 없음)
// =============================================================================
static inline int _diag_uio_one(const char* label,
    volatile const uint8_t* ptr, int optional)
{
#if CHAIN_DIAG_LEVEL >= 1
    if (!ptr) {
        if (optional) DIAG_WARN("UIO", "%-22s → NULL (optional)", label);
        else          DIAG_ERR("UIO", "%-22s → NULL [FATAL]", label);
        return optional ? 0 : 1;
    }
    DIAG_OK("UIO", "%-22s → %p", label, (const void*)ptr);
#else
    (void)label; (void)ptr; (void)optional;
#endif
    return 0;
}

static int diag_check_all_uio(void)
{
    int fatal = 0;
    fprintf(stderr, _CBLU"\n━━━ [CHAIN DIAG] UIO 매핑 검증 ━━━"_CRST"\n");
    fatal += _diag_uio_one("YM2203(ym_gpio)", p_ym, 0);
    fatal += _diag_uio_one("pre_eq_4band", p_pre_eq, 0);
    fatal += _diag_uio_one("vca_compressor", p_vca, 0);
    fatal += _diag_uio_one("post_eq_4band", p_post_eq, 0);
    fatal += _diag_uio_one("analog_delay", p_delay, 0);

#if ENABLE_HW_LFO0_DELAY
    fatal += _diag_uio_one("delay_LFO(lfo0)", p_lfo_0, 1);
    fatal += _diag_uio_one("CODE_GPIO(pt2258)", p_code, 1);
#endif // <--- 반드시 이 줄을 추가하여 #if 블록을 닫아야 합니다.

    if (!p_vca)
        DIAG_ERR("UIO", "vca 매핑 실패 ? BD 이름 'vca_top'/'vca_compressor' UIO 등록 확인");

    if (fatal)
        fprintf(stderr, _CRED"[CHAIN DIAG] UIO FATAL=%d\n"_CRST, fatal);
    else
        DIAG_OK("UIO", "전체 필수 UIO 매핑 정상");

    fprintf(stderr, _CBLU"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"_CRST"\n\n");
    return fatal;
}

static void _diag_dump_ym(void)
{
#if CHAIN_DIAG_LEVEL >= 1
    if (!p_ym) { DIAG_WARN("YM2203", "p_ym=NULL"); return; }
    uint32_t tri1 = *(volatile uint32_t*)(p_ym + 0x04u);
    uint32_t tri2 = *(volatile uint32_t*)(p_ym + 0x0Cu);
    uint32_t g1 = *(volatile uint32_t*)(p_ym + 0x00u);
    uint32_t g2 = *(volatile uint32_t*)(p_ym + 0x08u);
    /* NOTE: C_ALL_OUTPUTS=1 AXI GPIO는 TRI 레지스터가 HW 고정(0xFFFFFFFF).
     *       SW readback이 항상 0xFFFF...이므로 TRI 값으로 판단 불가.
     *       대신 GPIO1 출력값이 초기화 완료 여부를 나타냄 (0이면 미초기화). */
    fprintf(stderr, _CCYN"[DIAG] YM2203: GPIO1=0x%08X TRI1=0x%08X(HW-fixed) | GPIO2=0x%08X TRI2=0x%08X(HW-fixed)"_CRST"\n", g1, tri1, g2, tri2);
    if (g1 == 0 && g2 == 0) DIAG_WARN("YM2203", "GPIO1/2 출력값=0 ? ym_gpio_init() 미완료 가능성");
    else                     DIAG_OK("YM2203", "GPIO 출력 정상 (TRI는 C_ALL_OUTPUTS=1로 HW 고정, 무시)");
#endif
}

static void _diag_dump_pre_eq(void)
{
#if CHAIN_DIAG_LEVEL >= 1
    if (!p_pre_eq) { DIAG_WARN("pre_EQ", "p_pre_eq=NULL"); return; }
    uint32_t ctrl = *(volatile uint32_t*)(p_pre_eq + 0x100u);
    uint32_t b0 = *(volatile uint32_t*)(p_pre_eq + 0x00u) & 0xFFFFFF;
    fprintf(stderr, _CCYN"[DIAG] pre_EQ: ctrl=0x%08X band0_b0=0x%06X"_CRST"\n", ctrl, b0);
    if (ctrl & 0x01u) DIAG_WARN("pre_EQ", "bypass=1 ? eq_apply() 미호출 또는 coef_bypass 해제 누락");
    if (ctrl & 0x02u) DIAG_WARN("pre_EQ", "coef_bypass=1 잔류");
    if (b0 == 0)      DIAG_WARN("pre_EQ", "band0.b0=0 ? EQ 계수 미적용 가능성");
#endif
}

static void _diag_dump_vca(void)
{
#if CHAIN_DIAG_LEVEL >= 1
    if (!p_vca) { DIAG_WARN("VCA", "p_vca=NULL"); return; }
    uint32_t thresh = *(volatile uint32_t*)(p_vca + 0x00u) & 0x1FFF;
    uint32_t cs = *(volatile uint32_t*)(p_vca + 0x04u) & 0xFFFF;
    uint32_t makeup = *(volatile uint32_t*)(p_vca + 0x08u) & 0xFFFF;
    uint32_t atk = *(volatile uint32_t*)(p_vca + 0x0Cu) & 0x1F;
    uint32_t rel = *(volatile uint32_t*)(p_vca + 0x10u) & 0x1F;
    uint32_t env = *(volatile uint32_t*)(p_vca + 0x14u) & 0x01;
    uint32_t knee = *(volatile uint32_t*)(p_vca + 0x18u) & 0x1FFF;
    fprintf(stderr, _CCYN"[DIAG] VCA: thresh=0x%04X cs=0x%04X makeup=0x%04X atk=%u rel=%u env=%s knee=0x%04X"_CRST"\n",
        thresh, cs, makeup, atk, rel, env ? "RMS" : "PEAK", knee);
    if (makeup == 0)      DIAG_ERR("VCA", "makeup=0 ? 신호 완전 소거! vca_drain() 후 복원 누락 의심");
    if (makeup == 0xFFFF) DIAG_WARN("VCA", "makeup=0xFFFF ? 파워온 잔류값, 초기화 미실시 가능성");
    if (cs == 0 && thresh == 0x1FFF) DIAG_INFO("VCA", "바이패스 모드 (thresh 최대, cs=0)");
#endif
}



static void _diag_dump_post_eq(void)
{
#if CHAIN_DIAG_LEVEL >= 1
    if (!p_post_eq) { DIAG_WARN("post_EQ", "p_post_eq=NULL"); return; }
    uint32_t ctrl = *(volatile uint32_t*)(p_post_eq + 0x100u);
    fprintf(stderr, _CCYN"[DIAG] post_EQ: ctrl=0x%08X"_CRST"\n", ctrl);
    if (ctrl & 0x01u) DIAG_WARN("post_EQ", "bypass=1 ? post EQ 바이패스");
#endif
}

static void _diag_dump_delay(void)
{
#if CHAIN_DIAG_LEVEL >= 1
    if (!p_delay) { DIAG_WARN("DELAY", "p_delay=NULL"); return; }
    uint32_t tq = *(volatile uint32_t*)(p_delay + 0x00u);
    uint32_t dw = *(volatile uint32_t*)(p_delay + 0x14u) & 0xFFFF;
    uint32_t fb = *(volatile uint32_t*)(p_delay + 0x08u) & 0xFFFF;
    /* ADLY_OFF_BYPASS=0x04 (6'h01) ? RTL에서 유효한 유일한 상태 레지스터.
     * 이전 코드의 0x28(6'h0A)은 RTL 미맵 주소 → default:DEADBEEF → &0xFF=0xEF 오경보. */
    uint32_t bypass = *(volatile uint32_t*)(p_delay + 0x04u) & 0x1;
    fprintf(stderr, _CCYN"[DIAG] DELAY: time_q=0x%08X(%.1fms) dw=0x%04X fb=0x%04X bypass=%u"_CRST"\n",
        tq, (double)tq / 12288.0, dw, fb, bypass);
    if (dw == 0)      DIAG_ERR("DELAY", "drywet=0 ? 완전 무음! bypass_on→bypass_off 누락 의심");
    if (dw == 0x3000) DIAG_INFO("DELAY", "drywet=0x3000 ? bypass_pass(dry only) 상태");
    if (bypass)       DIAG_WARN("DELAY", "bypass=1 ? 딜레이 신호 차단 중. analog_delay_bypass_off() 확인");
#endif
}


static void _diag_dump_pt2258(void)
{
#if CHAIN_DIAG_LEVEL >= 1
    if (fd_pt2258 < 0) {
        if (g_pt2258_addr < 0)
            /* open은 됐지만 ACK 없어서 close된 상태 ? 하드웨어(배선/pull-up) 문제 */
            DIAG_ERR("PT2258", "I2C open OK but no ACK ? 0x40~0x43 배선/pull-up 확인");
        else
            DIAG_ERR("PT2258", "fd<0 ? /dev/i2c-0 open 실패. I2C 드라이버 로드 확인");
        return;
    }
    if (g_pt2258_addr < 0) {
        DIAG_ERR("PT2258", "addr<0 ? ACK 없음. CODE GPIO 배선 및 7bit 주소(0x40~0x43) 확인"); return;
    }
    fprintf(stderr, _CCYN"[DIAG] PT2258: fd=%d addr=0x%02X att=%udB muted=%u"_CRST"\n",
        fd_pt2258, (uint8_t)g_pt2258_addr, g_pt2258_att_db, g_pt2258_muted);
    if (g_pt2258_muted)    DIAG_WARN("PT2258", "MUTE=1 ? pt2258_audio_gate_for_mode() 시점 확인");
    if (g_pt2258_att_db >= 79) DIAG_WARN("PT2258", "att=79dB ? 초기값(15dB) 미적용 가능성");
#endif
}

static void _diag_dump_lfo(const char* name, volatile uint8_t* p, int chs)
{
#if CHAIN_DIAG_LEVEL >= 2
    if (!p) { DIAG_INFO(name, "optional ? 미연결"); return; }
    fprintf(stderr, _CCYN"[DIAG] %s:"_CRST, name);
    for (int i = 0; i < chs; i++) {
        uint32_t fcw = *(volatile uint32_t*)(p + (uint32_t)(i * 16));
        uint32_t dep = *(volatile uint32_t*)(p + (uint32_t)(i * 16 + 4)) & 0xFFFF;
        fprintf(stderr, " ch%d(fcw=0x%X dep=0x%X)", i, fcw, dep);
    }
    fprintf(stderr, "\n");
#else
    (void)name; (void)p; (void)chs;
#endif
}

static void diag_dump_chain(const char* when)
{
#if CHAIN_DIAG_LEVEL >= 1
    time_t t = time(NULL); struct tm* tm_p = localtime(&t);
    char ts[32]; strftime(ts, sizeof(ts), "%H:%M:%S", tm_p);
    fprintf(stderr, _CBLU"\n━━━ [CHAIN DIAG] %s @ %s ━━━"_CRST"\n", when, ts);
    _diag_dump_ym();
    _diag_dump_pre_eq();
    _diag_dump_vca();
    _diag_dump_pt2258();
    fprintf(stderr, _CBLU"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"_CRST"\n\n");
#else
    (void)when;
#endif
}

static void diag_periodic_dump(void)
{
#if CHAIN_DIAG_LEVEL >= 2
    static time_t last = 0;
    time_t now = time(NULL);
    if (now - last < 10) return;
    last = now;
    diag_dump_chain("주기 상태 덤프");
#endif
}

static inline void _diag_vca_makeup_guard(uint32_t val, const char* caller)
{
#if CHAIN_DIAG_LEVEL >= 2
    if ((val & 0xFFFF) == 0)
        DIAG_ERR("VCA", "%s: makeup=0 쓰기 ? 신호 완전 소거!", caller);
#else
    (void)val; (void)caller;
#endif
}

static inline void _diag_delay_dw_guard(uint32_t val, const char* caller)
{
#if CHAIN_DIAG_LEVEL >= 2
    if ((val & 0xFFFF) == 0)
        DIAG_ERR("DELAY", "%s: drywet=0 ? 완전 무음!", caller);
#else
    (void)val; (void)caller;
#endif
}

static void diag_pt2258_gate_check(int mode)
{
#if CHAIN_DIAG_LEVEL >= 1
    /* [REQ-M3-GATE]
     * Mode 3(MIDIVIS)은 팀원 요청에 맞춰 "Note-On 전까지 PT2258 mute"가 정상이다.
     * 따라서 모드 진입 직후 mute 상태를 오류로 보지 않고, 실제 Note-On에서만 unmute한다.
     * Mode 1(SYNTH)은 기존처럼 진입 즉시 출력 가능 상태를 기대한다.
     */
    if (mode == ZYNQ_MODE_SYNTH && g_pt2258_muted)
        DIAG_ERR("PT2258", "SYNTH인데 MUTE=1 ? pt2258_audio_gate_for_mode() 미호출");
    if (mode == ZYNQ_MODE_MIDIVIS && g_pt2258_muted)
        DIAG_INFO("PT2258", "MIDIVIS 진입 직후 MUTE=1 정상: Note-On 수신 시 unmute");
    if (mode != ZYNQ_MODE_SYNTH && mode != ZYNQ_MODE_MIDIVIS &&
        mode != ZYNQ_MODE_VGM && mode != ZYNQ_MODE_INSTSETUP && !g_pt2258_muted)
        DIAG_WARN("PT2258", "모드%d(비오디오)인데 MUTE=0 ? 잡음 출력 가능", mode);
#else
    (void)mode;
#endif
}
// =============================================================================
//  [CHAIN DIAG IMPL END]
// =============================================================================


// ─────────────────────────────────────────────────────────────────────────────
static inline uint32_t fifo_status(void)
{
    return p_fifosg ? (reg_rd(p_fifosg, 0x00u) & 0x7u) : 0;
}

static inline int fifo_is_full(void) { return (fifo_status() & FIFO_STATUS_FULL) != 0; }
static inline int fifo_needs_wait(void) { return (fifo_status() & (FIFO_STATUS_AF | FIFO_STATUS_BUSY)) != 0; }

void ym_write(uint8_t reg, uint8_t data)
{
    if (!p_ym) return;
    if (p_fifosg) {
        uint32_t t = 100000u;
        while (fifo_is_full() && --t) sched_yield();
        if (!t) {
            fprintf(stderr, "[ym] WARN: fifo_full timeout reg=0x%02X\n", reg);
            return;
        }
    }
    uint32_t packet = ((uint32_t)reg << 8) | (uint32_t)data;
    reg_wr(p_ym, YM_GPIO1_OFF, packet);
    reg_wr(p_ym, YM_GPIO1_OFF, packet | YM_WR_BIT);
    reg_wr(p_ym, YM_GPIO1_OFF, packet);
    if (p_fifosg) {
        uint32_t t = 50000u;
        while (fifo_needs_wait() && --t) sched_yield();
    }
    else {
        usleep(1);
    }
}

static void ym_reset(void)
{
    if (!p_ym) return;
    reg_wr(p_ym, YM_GPIO2_OFF, 0x00000001u); usleep(10000);
    reg_wr(p_ym, YM_GPIO2_OFF, 0x00000000u); usleep(10000);
    printf("[ym] HW Reset 완료\n");
}

static void ym_silence(void)
{
    if (!p_ym) return;
    ym_write(YM_SSG_VOL_CH0, 0);
    ym_write(YM_SSG_VOL_CH1, 0);
    ym_write(YM_SSG_VOL_CH2, 0);
    ym_write(YM_SSG_MIXER, 0x3F);
    ym_write(0x28, 0x00);
    ym_write(0x28, 0x01);
    ym_write(0x28, 0x02);
    usleep(1000);
    printf("[ym] Silenced\n");
}

static void ym_gpio_init(void)
{
    if (!p_ym) return;
    reg_wr(p_ym, YM_GPIO1_TRI, 0u);
    reg_wr(p_ym, YM_GPIO2_TRI, 0u);
    reg_wr(p_ym, YM_GPIO1_OFF, 0u);
    reg_wr(p_ym, YM_GPIO2_OFF, 0u);
    printf("[GPIO] axi_ym2203 전체 출력\n");
}

static void ym_set_ssg_volume_checked(uint16_t adc_vol)
{
    static uint16_t last = 0xFFFFu;
    if (!p_ym) return;
    uint16_t diff = (adc_vol > last) ? (adc_vol - last) : (last - adc_vol);
    if (diff < 100u) return;
    last = adc_vol;
    uint8_t vol = adc_to_ssg_vol(adc_vol);
    ym_write(YM_SSG_VOL_CH0, vol);
    ym_write(YM_SSG_VOL_CH1, vol);
    ym_write(YM_SSG_VOL_CH2, vol);
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 B3]  EQ 드라이버
// ─────────────────────────────────────────────────────────────────────────────
static void eq_apply(const BiquadCoef p[4], const char* name)
{
    if (!p_pre_eq) return;

    /* [PRE-EQ ctrl 시퀀스] RTL pre_eq_4band.v 실제 동작 기준:
     *   - shadow write는 coef_bypass 무관하게 항상 허용 (현재 RTL 수정 완료)
     *   - coef_bypass=1(bit1): ST_LOAD에서 r_bX/r_aX 래치 freeze → 반쪽 계수 방지
     *   - bypass=1(bit0)     : AXI-Stream passthrough + ST_IDLE에서 FSM 진입 차단
     *                          → bypass=1 상태에서 write하면 ctrl=0x00 해제 후 1샘플
     *                             딜레이로 계수 반영되지만 FSM이 돌지 않아 glitch 없음
     *                          → 그러나 ST_IDLE !bypass 가드로 인해 EQ가 연속 동작 안 함
     *
     * 정상 순서 (ym0.c 검증 완료):
     *   1) ctrl=0x02 (coef_bypass=1, bypass=0): FSM 계속 동작, shadow 래치만 freeze
     *   2) 20개 계수 shadow write
     *   3) ctrl=0x00 (coef_bypass=0): 다음 ST_LOAD에서 새 계수 적용
     */
    reg_wr(p_pre_eq, EQ_REG_CTRL, PRE_EQ_CTRL_COEF_BYPASS);
    for (int b = 0; b < 4; b++) {
        reg_wr(p_pre_eq, EQ_REG_OFF(b, 0), p[b].b0 & 0x00FFFFFFu);
        reg_wr(p_pre_eq, EQ_REG_OFF(b, 1), p[b].b1 & 0x00FFFFFFu);
        reg_wr(p_pre_eq, EQ_REG_OFF(b, 2), p[b].b2 & 0x00FFFFFFu);
        reg_wr(p_pre_eq, EQ_REG_OFF(b, 3), p[b].a1 & 0x00FFFFFFu);
        reg_wr(p_pre_eq, EQ_REG_OFF(b, 4), p[b].a2 & 0x00FFFFFFu);
    }
    reg_wr(p_pre_eq, EQ_REG_CTRL, PRE_EQ_CTRL_RUN);
    printf("[PRE_EQ] %s applied\n", name);
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 B4]  VCA 드라이버
// ─────────────────────────────────────────────────────────────────────────────
static void vca_write(uint16_t thresh, uint16_t knee, uint16_t cs,
    uint16_t makeup, uint8_t atk, uint8_t rel)
{
    if (!p_vca) return;
    reg_wr(p_vca, VCA_THRESH_OFF, thresh & 0x1FFFu);
    reg_wr(p_vca, VCA_CS_OFF, cs);
    _diag_vca_makeup_guard(makeup, __func__);   /* [DIAG] makeup=0 라이브 감지 */
    reg_wr(p_vca, VCA_MAKEUP_OFF, makeup);
    reg_wr(p_vca, VCA_ATK_OFF, (uint32_t)(atk & 0x1Fu));
    reg_wr(p_vca, VCA_REL_OFF, (uint32_t)(rel & 0x1Fu));
    reg_wr(p_vca, VCA_ENV_MODE_OFF, 0u);          /* 기본 peak */
    reg_wr(p_vca, VCA_KNEE_OFF, knee & 0x1FFFu);
    reg_wr(p_vca, VCA_DITHER_OFF, 0u);
}

static void vca_set_makeup_checked(uint16_t new_mk)
{
    static uint16_t last = 0xFFFFu;
    if (!p_vca) return;
    uint16_t diff = (new_mk > last) ? (new_mk - last) : (last - new_mk);
    if (diff < 16u) return;
    last = new_mk;
    reg_wr(p_vca, VCA_MAKEUP_OFF, new_mk);
}

/* Rev.5 VCA 기준: makeup Q2.14, 0x4000=1.0 */
static void vca_bypass(void)
{
    vca_write(0x1FFFu, 0x0000u, VCA_CS_RATIO_1_1, VCA_MAKEUP_ONE, 4, 8);
    printf("[VCA] BYPASS\n");
}

static void vca_soft(void)
{
    vca_write(0x0D00u, 0x0200u, VCA_CS_RATIO_2_1, VCA_MAKEUP_ONE, 4, 6);
    reg_wr(p_vca, VCA_ENV_MODE_OFF, 1u);          /* RMS */
    printf("[VCA] SOFT 2:1 RMS\n");
}

static void vca_final(void)
{
    vca_write(0x0D00u, 0x0200u, VCA_CS_RATIO_4_1, 0x5A82u, 4, 6); /* +3dB */
    reg_wr(p_vca, VCA_ENV_MODE_OFF, 1u);          /* RMS */
    printf("[VCA] FINAL 4:1 +3dB RMS\n");
}

/* vca_drain: makeup=0으로 신호 차단 후 30ms 대기.
 * 반드시 호출 후 vca_write() 등으로 makeup 복원할 것.
 * 복원 없이 방치 시 완전 무음 상태가 지속된다. */
static void vca_drain(void)
{
    if (!p_vca) return;
    reg_wr(p_vca, VCA_MAKEUP_OFF, 0x0000u);
    reg_wr(p_vca, VCA_ATK_OFF, 1u);
    reg_wr(p_vca, VCA_REL_OFF, 1u);
    usleep(30000);
    printf("[VCA] drained\n");
}

static void vca_readback(void)
{
    if (!p_vca) return;
    printf("[VCA] thresh=0x%04X cs=0x%04X makeup=0x%04X atk=%u rel=%u env=%u knee=0x%04X dither=%u\n",
        reg_rd(p_vca, VCA_THRESH_OFF) & 0x1FFF,
        reg_rd(p_vca, VCA_CS_OFF) & 0xFFFF,
        reg_rd(p_vca, VCA_MAKEUP_OFF) & 0xFFFF,
        reg_rd(p_vca, VCA_ATK_OFF) & 0x1F,
        reg_rd(p_vca, VCA_REL_OFF) & 0x1F,
        reg_rd(p_vca, VCA_ENV_MODE_OFF) & 0x01,
        reg_rd(p_vca, VCA_KNEE_OFF) & 0x1FFF,
        reg_rd(p_vca, VCA_DITHER_OFF) & 0x01);
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 B4-PT] PT2258 Linux I2C 드라이버
// ─────────────────────────────────────────────────────────────────────────────
//  CODE GPIO (BD: /CODE  0x41240000, axi_gpio 2bit ALL_OUTPUT)
//    gpio_io_o[1:0] → 외부핀 CODE2(bit1), CODE1(bit0)
//    CODE GPIO는 PS7 EMIO I2C0 SCL/SDA 와 무관한 별도 핀이며,
//    PT2258 하드웨어 주소핀 A1:A0에 직접 연결된다.
//
//  PT2258 7bit I2C 주소 = 0b1000_A1A0  → 0x40 ~ 0x43  [FIX: 이전 0x20은 오기]
//    CODE[1:0] = 00 → 0x20   CODE[1:0] = 01 → 0x21
//    CODE[1:0] = 10 → 0x22   CODE[1:0] = 11 → 0x23
//
//  PS7 I2C0 는 EMIO 경유 → PetaLinux에서 /dev/i2c-0 으로 노출
//  (PCW_I2C0_I2C0_IO = EMIO, PCW_EN_EMIO_I2C0 = 1)
// ─────────────────────────────────────────────────────────────────────────────

/* ── CODE GPIO 레지스터 오프셋 (AXI GPIO) ── */
#define CODE_GPIO_DATA_OFF  0x00u   /* GPIO_DATA  ? 쓰기: bit[1:0] */
#define CODE_GPIO_TRI_OFF   0x04u   /* GPIO_TRI   ? 0=output */

/* ── CODE 핀 조합 → PT2258 7bit 주소 ── */
/* PT2258 데이터시트: 7bit addr = 0b1000_A1A0 → 0x40~0x43           */
/* [BUG FIX] 이전 0x20 (0b0100_A1A0) 은 잘못된 주소 ? I2C 스캐너 확인 결과 0x40 */
#define PT2258_ADDR_BASE    0x40u   /* A1=0,A0=0 */
/* code_bits = (CODE2<<1)|CODE1 ; addr = PT2258_ADDR_BASE | code_bits */

/* ── I2C 디바이스 경로 ── */
#ifndef PT2258_I2C_DEV_DEFAULT
#define PT2258_I2C_DEV_DEFAULT  "/dev/i2c-0"   /* PS7 I2C0 EMIO */
#endif

/* ── PT2258 커맨드 바이트 ── */
#define PT2258_RESET_CMD    0xC0u
#define PT2258_MUTE_ON      0xF9u
#define PT2258_MUTE_OFF     0xF8u
/* 채널별 감쇠 (attenuation):  reg = base | digit */
#define PT2258_CH1_10DB     0x80u   /* CH1 십의 자리 */
#define PT2258_CH1_1DB      0x90u   /* CH1 일의 자리 */
#define PT2258_CH2_10DB     0x40u   /* CH2 십의 자리 */
#define PT2258_CH2_1DB      0x50u   /* CH2 일의 자리 */
#define PT2258_CH3_10DB     0x20u
#define PT2258_CH3_1DB      0x30u
#define PT2258_CH4_10DB     0x00u
#define PT2258_CH4_1DB      0x10u
#define PT2258_CH5_10DB     0x60u
#define PT2258_CH5_1DB      0x70u
#define PT2258_CH6_10DB     0xA0u
#define PT2258_CH6_1DB      0xB0u
#define PT2258_ALL_10DB     0xD0u   /* ALL CH 십의 자리 */
#define PT2258_ALL_1DB      0xE0u   /* ALL CH 일의 자리 */
#define PT2258_VOL_MIN_DB   0       /* 0 dB 감쇠 = 최대 음량 */
#define PT2258_VOL_MAX_DB   79      /* 79 dB 감쇠 = 최소 음량 */

/* fd_pt2258 / g_pt2258_* : 상단 전방선언 블록에서 정의 */

/* CODE GPIO에 code_bits(0~3)를 출력 → PT2258 A1:A0 핀 구동 */
static void code_gpio_set(uint8_t code_bits)
{
    if (!p_code) return;
    /* AXI GPIO TRI=0 (출력) 확인 후 DATA 쓰기 */
    reg_wr(p_code, CODE_GPIO_TRI_OFF, 0x00u);
    reg_wr(p_code, CODE_GPIO_DATA_OFF, (uint32_t)(code_bits & 0x03u));
}

/* 현재 CODE GPIO 값에서 PT2258 7bit 주소를 계산 */
static int code_gpio_read_addr(void)
{
    if (!p_code) return PT2258_ADDR_BASE;  /* fallback */
    uint32_t val = reg_rd(p_code, CODE_GPIO_DATA_OFF) & 0x03u;
    return (int)(PT2258_ADDR_BASE | val);
}

static int pt2258_select_addr(int addr)
{
    if (fd_pt2258 < 0) return -1;
    if (ioctl(fd_pt2258, I2C_SLAVE, addr) < 0) {
        fprintf(stderr, "[PT2258] ioctl(I2C_SLAVE,0x%02X): %s\n", addr, strerror(errno));
        return -1;
    }
    return 0;
}

static int pt2258_write_bytes(const uint8_t* data, size_t len, const char* tag)
{
    if (fd_pt2258 < 0 || g_pt2258_addr < 0) return -1;
    if (pt2258_select_addr(g_pt2258_addr) != 0) return -1;
    ssize_t wr = write(fd_pt2258, data, len);
    if (wr == (ssize_t)len) return 0;
    fprintf(stderr, "[PT2258] %s fail addr=0x%02X len=%zu wr=%zd: %s\n",
        tag ? tag : "TX", g_pt2258_addr, len, wr, strerror(errno));
    return -1;
}

static int pt2258_probe_addr(int addr)
{
    /* [FIX⑧] 빈 write(len=0)로 ACK만 확인 ? RESET 전 커맨드 전송 방지
     * write(fd, buf, 0)은 I2C START+ADDR+STOP만 발생시켜 ACK 여부만 반환 */
    uint8_t dummy = 0;
    if (pt2258_select_addr(addr) != 0) return -1;
    return (write(fd_pt2258, &dummy, 0) == 0) ? 0 : -1;
}

/*
 * pt2258_init_linux()
 *  1. CODE GPIO A1:A0 조합(0~3) 순회 → 각 주소 probe
 *     (베어메탈 pt2258_probe()의 CODE 핀 순회 로직과 동등)
 *  2. 우선 현재 CODE 핀값으로 시도, 실패 시 0x40~0x43 전체 스캔
 *  3. RESET → usleep(20ms) → 초기 볼륨 적용
 */
static int pt2258_init_linux(void)
{
    const char* dev = getenv("PT2258_I2C_DEV");
    if (!dev || !dev[0]) dev = PT2258_I2C_DEV_DEFAULT;

    fd_pt2258 = open(dev, O_RDWR);
    if (fd_pt2258 < 0) {
        fprintf(stderr, "[PT2258] %s open 실패: %s ? PT2258 없이 계속\n", dev, strerror(errno));
        return -1;
    }

    /* ── Step 1: 현재 CODE GPIO에서 기대 주소 우선 시도 ── */
    int expected = code_gpio_read_addr();
    printf("[PT2258] CODE GPIO A1:A0=%d → 기대 7bit=0x%02X\n",
        expected - PT2258_ADDR_BASE, expected);

    if (pt2258_probe_addr(expected) == 0) {
        g_pt2258_addr = expected;
    }
    else {
        /* ── Step 2: CODE 핀 0~3 순회하며 probe (베어메탈 pt2258_probe 동등) ── */
        fprintf(stderr, "[PT2258] 기대 주소 0x%02X ACK 없음 ? CODE 핀 순회 스캔\n", expected);
        for (uint8_t bits = 0; bits < 4 && g_pt2258_addr < 0; bits++) {
            int candidate = (int)(PT2258_ADDR_BASE | bits);
            if (candidate == expected) continue;  /* 이미 시도 */
            code_gpio_set(bits);
            usleep(10000);
            printf("[PT2258] CODE[1:0]=%u → 0x%02X probe\n", bits, candidate);
            if (pt2258_probe_addr(candidate) == 0) {
                g_pt2258_addr = candidate;
                fprintf(stderr, "[PT2258] WARNING: CODE[1:0]=%u(0x%02X) 에서 ACK"
                    " ? CODE GPIO 배선/초기값 확인 필요\n", bits, candidate);
            }
        }
        /* CODE 핀 복원 (검색 후 원래 값으로) */
        code_gpio_set((uint8_t)(expected - PT2258_ADDR_BASE));
    }

    if (g_pt2258_addr < 0) {
        fprintf(stderr, "[PT2258] 0x40~0x43 전체 ACK 없음 ? I2C 배선/pull-up 확인\n");
        close(fd_pt2258); fd_pt2258 = -1;
        return -1;
    }

    printf("[PT2258] 연결: dev=%s 7bit=0x%02X\n", dev, g_pt2258_addr);
    uint8_t rst = PT2258_RESET_CMD;
    pt2258_write_bytes(&rst, 1, "RESET");
    usleep(20000);
    pt2258_set_all_volume_db(g_pt2258_att_db);
    /* 채널별 감쇠 상태를 초기값과 동기화 */
    g_pt2258_att_l_db = g_pt2258_att_db;
    g_pt2258_att_r_db = g_pt2258_att_db;
    /* [DIAG] PT2258 초기화 직후 상태 확인 */
    fprintf(stderr, _CCYN"[DIAG] PT2258 init: dev=%s addr=0x%02X att=%udB muted=%u"_CRST"\n",
        dev, (uint8_t)g_pt2258_addr, g_pt2258_att_db, g_pt2258_muted);
    if (g_pt2258_att_db >= 79) DIAG_WARN("PT2258", "init 후 att=79dB ? 초기값 미적용 가능성");
    return 0;
}

static void pt2258_set_mute(int mute)
{
    g_pt2258_muted = mute ? 1u : 0u;
    uint8_t cmd = mute ? PT2258_MUTE_ON : PT2258_MUTE_OFF;
    (void)pt2258_write_bytes(&cmd, 1, mute ? "MUTE ON" : "MUTE OFF");
}

static void pt2258_set_all_volume_db(int att_db)
{
    if (att_db < PT2258_VOL_MIN_DB) att_db = PT2258_VOL_MIN_DB;
    if (att_db > PT2258_VOL_MAX_DB) att_db = PT2258_VOL_MAX_DB;
    g_pt2258_att_db = (uint8_t)att_db;
    uint8_t buf[2] = {
        (uint8_t)(PT2258_ALL_10DB | (uint8_t)(att_db / 10)),
        (uint8_t)(PT2258_ALL_1DB | (uint8_t)(att_db % 10))
    };
    (void)pt2258_write_bytes(buf, sizeof(buf), "ALL VOL");
}

static void pt2258_set_stereo_volume_db(int l_att_db, int r_att_db)
{
    if (l_att_db < 0) l_att_db = 0; if (l_att_db > 79) l_att_db = 79;
    if (r_att_db < 0) r_att_db = 0; if (r_att_db > 79) r_att_db = 79;
    uint8_t lbuf[2] = { (uint8_t)(PT2258_CH1_10DB | (l_att_db / 10)), (uint8_t)(PT2258_CH1_1DB | (l_att_db % 10)) };
    uint8_t rbuf[2] = { (uint8_t)(PT2258_CH2_10DB | (r_att_db / 10)), (uint8_t)(PT2258_CH2_1DB | (r_att_db % 10)) };
    (void)pt2258_write_bytes(lbuf, sizeof(lbuf), "CH1 VOL");
    (void)pt2258_write_bytes(rbuf, sizeof(rbuf), "CH2 VOL");
}

static void pt2258_update_from_adc(uint16_t adc)
{
    static int last = -1;
    /* ADC 높음=큰 소리 → attenuation 낮음 */
    int att = 79 - (int)((uint32_t)adc * 79u / ADC_MAX);
    if (last >= 0 && abs(att - last) < 2) return;
    last = att;
    pt2258_set_all_volume_db(att);
}

/* 메뉴 화면 전용 스테레오 ADC 볼륨.
 * ADC_VOL(가변저항0) → CH1(L),  ADC_PITCH(가변저항1) → CH2(R).
 * ADC 최대(4095) = 0dB 감쇠(최대음량), ADC 최소(0) = 79dB 감쇠.
 * 2dB 미만 변화는 무시해 불필요한 I2C 트래픽 억제. */
static void pt2258_update_stereo_from_adc(uint16_t adc_l, uint16_t adc_r)
{
    int att_l = 79 - (int)((uint32_t)adc_l * 79u / ADC_MAX);
    int att_r = 79 - (int)((uint32_t)adc_r * 79u / ADC_MAX);
    if (abs(att_l - (int)g_pt2258_att_l_db) >= 2) {
        g_pt2258_att_l_db = (uint8_t)att_l;
        uint8_t buf[2] = {
            (uint8_t)(PT2258_CH1_10DB | (att_l / 10)),
            (uint8_t)(PT2258_CH1_1DB | (att_l % 10))
        };
        (void)pt2258_write_bytes(buf, sizeof(buf), "CH1(L) VOL");
    }
    if (abs(att_r - (int)g_pt2258_att_r_db) >= 2) {
        g_pt2258_att_r_db = (uint8_t)att_r;
        uint8_t buf[2] = {
            (uint8_t)(PT2258_CH2_10DB | (att_r / 10)),
            (uint8_t)(PT2258_CH2_1DB | (att_r % 10))
        };
        (void)pt2258_write_bytes(buf, sizeof(buf), "CH2(R) VOL");
    }
}

static void pt2258_audio_gate_for_mode(int mode)
{
    /* SYNTH/MIDIVIS: 진입 즉시 unmute.
     * VGM(2)/INSTSETUP(4): 신호 쏘기 직전에만 unmute ? 여기서는 mute 유지.
     * MENU/기타: mute. */
    int audio_on = (mode == ZYNQ_MODE_SYNTH || mode == ZYNQ_MODE_MIDIVIS);
    pt2258_set_mute(!audio_on);
}

static void pt2258_cleanup(void)
{
    if (fd_pt2258 >= 0) {
        pt2258_set_mute(1);
        close(fd_pt2258);
        fd_pt2258 = -1;
    }
    g_pt2258_addr = -1;
    g_pt2258_muted = 1;   /* [FIX] 상태 변수 초기화 */
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 B5]  DELAY 드라이버
/* ── 딜레이 직접 레지스터 쓰기 헬퍼 (RTL Rev.15 기준) ───────────────────── */

/* bypass bit 단독 제어 (0=active, 1=bypass) */
/* analog_delay.h analog_delay_bypass_on/off()의 p_delay 기반 래퍼 */
static inline void delay_set_bypass(int on)
{
    if (!p_delay) return;
    if (on) analog_delay_bypass_on(&g_adly);
    else    analog_delay_bypass_off(&g_adly);
}

/* 딜레이 전체 파라미터 설정 (RTL 실제 맵 기준)
 *  time_q  : Q16.8 샘플 수, MS_TO_SAMPLES_Q8(ms) 사용
 *  fb      : feedback  [15:0] Q0.16  (상한 0x6000)
 *  dw      : dry/wet   [15:0] Q0.16  (0x4000=50%, 0x8000=100%wet)
 *  diff_s  : diffusion_start  [7:0]  (에코 인덱스 기준, 기본 3)
 *  diff_mix: diffusion_mix    [15:0] Q0.16
 *  diff_g  : diffusion_g (APF g 계수) [15:0] Q0.15
 *  lfo_d   : lfo_depth_apf    [15:0] Q0.16
 *  slew    : slew_rate        [15:0] Q16.8 스텝 (기본 0x0010)
 *  sat     : sat_thresh       [15:0] raw (기본 22000)
 *  ms_w    : ms_width         [15:0] Q0.16 (0x4000=normal, 0x8000=full-wide)
 *  d_alpha : damp_alpha       [15:0] Q0.16 (0=off, 0x7FFF=max)
 *  d_hf    : damp_hf          [15:0] Q0.16 (0=off)
 *  tap_en  : [1:0] tap enable  bit0=tap1, bit1=tap2
 *  tap1_dt : tap1 delay time  Q16.8
 *  tap2_dt : tap2 delay time  Q16.8
 *  tap1_g  : tap1_gain        [7:0] Q0.8
 *  tap2_g  : tap2_gain        [7:0] Q0.8
 *  tap1_fg : tap1_fb_gain     [7:0] Q0.8
 *  tap_fg  : tap_fb_gain      [7:0] Q0.8 (main fb)
 *  wow_d   : wow_depth        [3:0] (right-shift 양)
 */
 /* delay_write_all ? analog_delay.h v7.0 API 경유 전체 레지스터 기록.
  * 시그니처는 기존과 동일하므로 delay_slapback/delay_full 등 호출부 변경 불필요. */
static void delay_write_all(
    uint32_t time_q,
    uint16_t fb, uint16_t dw,
    uint8_t  diff_s, uint16_t diff_mix, uint16_t diff_g,
    uint16_t lfo_d, uint16_t slew, uint16_t sat,
    uint16_t ms_w, uint16_t d_alpha, uint16_t d_hf,
    uint8_t  tap_en,
    uint32_t tap1_dt, uint32_t tap2_dt,
    uint8_t  tap1_g, uint8_t  tap2_g,
    uint8_t  tap1_fg, uint8_t  tap_fg,
    uint8_t  wow_d)
{
    if (!p_delay) return;
    _diag_delay_dw_guard(dw, __func__);
    analog_delay_set_time_q168(&g_adly, time_q);
    analog_delay_set_feedback_q15(&g_adly, fb);
    analog_delay_set_dry_wet_q15(&g_adly, dw);
    /* diff_s: raw diffusion_start 값 그대로 전달 */
    analog_delay_set_diffusion(&g_adly, diff_s,
        (float)diff_mix / 32767.0f, (float)(diff_g & 0x7FFFu) / 32767.0f);
    adly_wr(&g_adly, ADLY_OFF_LFO_DEPTH_APF, (uint32_t)lfo_d);
    analog_delay_set_slew_rate(&g_adly, slew);
    analog_delay_set_sat_thresh(&g_adly, sat);
    analog_delay_set_ms_width_q15(&g_adly, ms_w);
    analog_delay_set_damp_alpha_q15(&g_adly, d_alpha);
    analog_delay_set_damp_hf_q15(&g_adly, d_hf);
    adly_wr(&g_adly, ADLY_OFF_TAP_EN, (uint32_t)tap_en);
    adly_wr(&g_adly, ADLY_OFF_TAP1_DT, tap1_dt & 0xFFFFFFu);
    adly_wr(&g_adly, ADLY_OFF_TAP2_DT, tap2_dt & 0xFFFFFFu);
    adly_wr(&g_adly, ADLY_OFF_TAP1_GAIN, (uint32_t)tap1_g);
    adly_wr(&g_adly, ADLY_OFF_TAP2_GAIN, (uint32_t)tap2_g);
    adly_wr(&g_adly, ADLY_OFF_TAP1_FB_GAIN, (uint32_t)tap1_fg);
    adly_wr(&g_adly, ADLY_OFF_TAP_FB_GAIN, (uint32_t)tap_fg);
    analog_delay_set_wow_depth(&g_adly, wow_d);
}

/* bypass-pass: 딜레이 비활성, dry 신호 직결 통과
 * bypass bit=1(hw bypass)이므로 dry_wet 값과 무관하게 in_hold 직결 출력.
 * 단, 이전 상태에 따른 팝 방지를 위해 fb=0 먼저 설정. */
static void delay_bypass_pass(void)
{
    if (!p_delay) return;
    reg_wr(p_delay, DLY_FB_OFF, 0u);        /* 피드백 먼저 제거 */
    reg_wr(p_delay, DLY_TAP_EN_OFF, 0u);    /* 멀티탭 비활성 */
    delay_set_bypass(1);
    printf("[DELAY] BYPASS(hw bypass on)\n");
}

/* slapback: 80ms 단발 에코, 피드백 없음 */
static void delay_slapback(void)
{
    delay_set_bypass(0);
    delay_write_all(
        MS_TO_SAMPLES_Q8(80),
        0x0800u, 0x3000u,      /* fb, dw */
        3, 0x5000u, 0x5000u,   /* diff_s, diff_mix, diff_g */
        0x0180u, 0x0010u, 22000,/* lfo_d, slew, sat */
        0x4000u, 0x5000u, 0x0000u, /* ms_w, d_alpha, d_hf */
        0x00u,                 /* tap_en=0 */
        MS_TO_SAMPLES_Q8(80), MS_TO_SAMPLES_Q8(160),
        0x60u, 0x40u,          /* tap1_g, tap2_g */
        0xC0u, 0xFFu,          /* tap1_fb_gain, tap_fb_gain */
        6u);                   /* wow_depth */
    printf("[DELAY] SLAPBACK\n");
}

/* full delay: 200ms 피드백 딜레이 */
static void delay_full(void)
{
    delay_set_bypass(0);
    delay_write_all(
        MS_TO_SAMPLES_Q8(200),
        0x3333u, 0x2CCCu,      /* fb, dw */
        3, 0x6000u, 0x5000u,   /* diff_s, diff_mix, diff_g */
        0x0180u, 0x0010u, 22000,/* lfo_d, slew, sat */
        0x4000u, 0x5800u, 0x0000u, /* ms_w, d_alpha, d_hf */
        0x00u,                 /* tap_en=0 */
        MS_TO_SAMPLES_Q8(200), MS_TO_SAMPLES_Q8(400),
        0x60u, 0x40u,
        0xC0u, 0xFFu,
        6u);
    printf("[DELAY] FULL\n");
}

static void delay_mute(void)
{
    if (!p_delay) return;
    /* 어댑터 경유: feedback=0, bypass on → 피드백 꼬리 즉시 차단 */
    analog_delay_feedback_off(&g_adly);
    analog_delay_bypass_on(&g_adly);
    usleep(500000);
}

static void delay_readback(void)
{
    if (!p_delay) return;
    printf("[DELAY] time=%.1fms fb=%.0f%% wet=%.0f%%\n",
        analog_delay_get_time_ms(&g_adly),
        analog_delay_get_feedback_pct(&g_adly),
        analog_delay_get_dry_wet_pct(&g_adly));
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 B5b]  REVERB 드라이버  (fdn_reverb.h API 경유)
// ─────────────────────────────────────────────────────────────────────────────
/* fdn_apply_preset() 이 enable = 0 → 레지스터 갱신 → enable = 1 시퀀스를
 * 내부에서 처리하므로 pop-free. */

static void lfo_stop_ch(int ch)
{
    if (!p_lfo || ch < 0 || ch > 7) return;
    reg_wr(p_lfo, LFO_OFF(ch, 0), LFO_FCW_OFF);
    reg_wr(p_lfo, LFO_OFF(ch, 1), 0u);
}

static void lfo_stop_all(void)
{
    for (int i = 0; i < 8; i++) lfo_stop_ch(i);
}

static void lfo_init_default(void)
{
    /* RTL: CH0=APF주변조, CH4=L/R공통offset(÷8), CH5=R전용offset(÷4)
     * CH1~3,CH6~7 은 delay_top 배선 없음(DEAD).
     * 모든 채널을 0으로 초기화해 이전 모드 잔류값 제거. */
    lfo_stop_all();
}

/**
 * @brief LFO 채널 한 번에 설정 (p_lfo 기반 ? analog_delay.h §20 대응 구현)
 *
 * @param ch    채널 번호 0~7
 * @param fcw   주파수 제어 워드 (LFO_HZ_TO_FCW 매크로 사용)
 * @param depth 이득 Q0.16 (0x8000=1.0, LFO_DEPTH_MAX)
 * @param poff  위상 오프셋 32b (LFO_POFF_* 상수 사용)
 * @param wave  파형 (LFO_WAVE_SINE/TRIANGLE/SQUARE)
 */
static void lfo_set_ch(int ch,
    uint32_t fcw, uint16_t depth, uint32_t poff, uint16_t wave)
{
    if (!p_lfo || ch < 0 || ch > 7) return;
    if (depth > LFO_DEPTH_MAX) depth = LFO_DEPTH_MAX;
    reg_wr(p_lfo, LFO_OFF(ch, 0), fcw);
    reg_wr(p_lfo, LFO_OFF(ch, 1), (uint32_t)depth);
    reg_wr(p_lfo, LFO_OFF(ch, 2), poff);
    reg_wr(p_lfo, LFO_OFF(ch, 3), (uint32_t)(wave & 0x3u));
}

static void lfo_set_fcw_checked(int ch, uint32_t new_fcw)
{
    static uint32_t last[8] = { 0 };
    if (!p_lfo || ch < 0 || ch > 7) return;
    uint32_t diff = (new_fcw > last[ch]) ? (new_fcw - last[ch]) : (last[ch] - new_fcw);
    if (diff < 4u) return;
    last[ch] = new_fcw;
    reg_wr(p_lfo, LFO_OFF(ch, 0), new_fcw);
}

// =============================================================================
//  [섹션 C]  LCD / SPI 드라이버
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 C1]  LCD 하드웨어
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t lcd_shadow = 0;

static void gpio_lcd_set(uint32_t mask, int val)
{
    if (!p_lcd) return;
    if (val) lcd_shadow |= mask; else lcd_shadow &= ~mask;
    reg_wr(p_lcd, LCD_GPIO1_OFF, lcd_shadow);
}

static void spi_xfer(int fd, const uint8_t* tx, uint8_t* rx,
    int len, uint32_t speed)
{
    static uint8_t dummy_rx[LCD_W * 2];
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)(rx ? rx : dummy_rx),
        .len = (uint32_t)len, .speed_hz = speed,
        .bits_per_word = 8, .delay_usecs = 0,
    };
    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) perror("spi_xfer");
}

static void lcd_write_cmd(uint8_t c)
{
    gpio_lcd_set(LCD_DC, 0); spi_xfer(fd_lcd, &c, NULL, 1, LCD_SPI_HZ);
}

static void lcd_write_data8(uint8_t d)
{
    gpio_lcd_set(LCD_DC, 1); spi_xfer(fd_lcd, &d, NULL, 1, LCD_SPI_HZ);
}

static void lcd_init(void)
{
    printf("[LCD] ILI9341 초기화\n");
    gpio_lcd_set(LCD_RST, 1); msleep(10);
    gpio_lcd_set(LCD_RST, 0); msleep(50);
    gpio_lcd_set(LCD_RST, 1); msleep(150);
    lcd_write_cmd(0x01); msleep(150);
    lcd_write_cmd(0xCB);
    lcd_write_data8(0x39); lcd_write_data8(0x2C); lcd_write_data8(0x00);
    lcd_write_data8(0x34); lcd_write_data8(0x02);
    lcd_write_cmd(0xCF);
    lcd_write_data8(0x00); lcd_write_data8(0xC1); lcd_write_data8(0x30);
    lcd_write_cmd(0xE8);
    lcd_write_data8(0x85); lcd_write_data8(0x00); lcd_write_data8(0x78);
    lcd_write_cmd(0xEA); lcd_write_data8(0x00); lcd_write_data8(0x00);
    lcd_write_cmd(0xED);
    lcd_write_data8(0x64); lcd_write_data8(0x03);
    lcd_write_data8(0x12); lcd_write_data8(0x81);
    lcd_write_cmd(0xF7); lcd_write_data8(0x20);
    lcd_write_cmd(0xC0); lcd_write_data8(0x23);
    lcd_write_cmd(0xC1); lcd_write_data8(0x10);
    lcd_write_cmd(0xC5); lcd_write_data8(0x3E); lcd_write_data8(0x28);
    lcd_write_cmd(0xC7); lcd_write_data8(0x86);
    lcd_write_cmd(0x36); lcd_write_data8(0x28);
    lcd_write_cmd(0x3A); lcd_write_data8(0x55);
    lcd_write_cmd(0xB1); lcd_write_data8(0x00); lcd_write_data8(0x18);
    lcd_write_cmd(0xB6);
    lcd_write_data8(0x08); lcd_write_data8(0x82); lcd_write_data8(0x27);
    lcd_write_cmd(0x11); msleep(150);
    lcd_write_cmd(0x29); msleep(50);
    gpio_lcd_set(LCD_BL, 1);
    printf("[LCD] 완료\n");
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 C2]  LCD 그래픽 유틸리티
// ─────────────────────────────────────────────────────────────────────────────
static const uint8_t font5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
    {0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},
    {0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},
    {0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},
    {0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},
    {0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},
    {0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},
    {0x10,0x08,0x08,0x10,0x08},{0x00,0x00,0x00,0x00,0x00}
};

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_write_cmd(0x2A);
    lcd_write_data8(x0 >> 8); lcd_write_data8(x0 & 0xFF);
    lcd_write_data8(x1 >> 8); lcd_write_data8(x1 & 0xFF);
    lcd_write_cmd(0x2B);
    lcd_write_data8(y0 >> 8); lcd_write_data8(y0 & 0xFF);
    lcd_write_data8(y1 >> 8); lcd_write_data8(y1 & 0xFF);
    lcd_write_cmd(0x2C);
}

static void lcd_clear(uint16_t color)
{
    uint8_t line[LCD_W * 2]; memset(line, 0, sizeof(line));
    for (int x = 0; x < LCD_W; x++) {
        line[x * 2] = (uint8_t)(color >> 8);
        line[x * 2 + 1] = (uint8_t)(color & 0xFF);
    }
    lcd_set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_lcd_set(LCD_DC, 1);
    for (int y = 0; y < LCD_H; y++)
        spi_xfer(fd_lcd, line, NULL, LCD_W * 2, LCD_SPI_HZ);
}

static void lcd_fill_rect(uint16_t x, uint16_t y,
    uint16_t w, uint16_t h, uint16_t color)
{
    uint8_t line[LCD_W * 2]; memset(line, 0, sizeof(line));
    int px = (w > LCD_W) ? LCD_W : (int)w;
    for (int i = 0; i < px; i++) {
        line[i * 2] = (uint8_t)(color >> 8);
        line[i * 2 + 1] = (uint8_t)(color & 0xFF);
    }
    lcd_set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    gpio_lcd_set(LCD_DC, 1);
    for (uint16_t r = 0; r < h; r++)
        spi_xfer(fd_lcd, line, NULL, px * 2, LCD_SPI_HZ);
}

static void lcd_draw_char(uint16_t x, uint16_t y, char c,
    uint16_t fg, uint16_t bg)
{
    uint8_t idx = (c < 32) ? 0 : (uint8_t)(c - 32);
    if (idx >= 96) idx = 0;
    uint8_t buf[80]; int p = 0; memset(buf, 0, sizeof(buf));
    lcd_set_window(x, y, x + 4, y + 7);
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 5; i++) {
            uint16_t px = (font5x7[idx][i] & (1 << j)) ? fg : bg;
            buf[p++] = (uint8_t)(px >> 8);
            buf[p++] = (uint8_t)(px & 0xFF);
        }
    gpio_lcd_set(LCD_DC, 1);
    spi_xfer(fd_lcd, buf, NULL, 80, LCD_SPI_HZ);
}

static void lcd_string(uint16_t x, uint16_t y, const char* s,
    uint16_t fg, uint16_t bg)
{
    while (*s) { lcd_draw_char(x, y, *s++, fg, bg); x += 6; }
}

static void lcd_draw_header(uint16_t bar_col, const char* title, uint16_t txt_col)
{
    lcd_fill_rect(0, 0, LCD_W, 18, bar_col); lcd_string(4, 5, title, txt_col, bar_col);
}

static void __attribute__((unused)) lcd_row(uint16_t y, const char* s, uint16_t fg, uint16_t bg)
{
    lcd_fill_rect(0, y, LCD_W, 12, bg); lcd_string(4, y + 2, s, fg, bg);
}

static uint32_t sync_ok_cnt = 0, sync_fail_cnt = 0;
static void lcd_draw_statusbar(int spi_ok, uint16_t adc_vol, int mode)
{
    char buf[54];
    snprintf(buf, sizeof(buf), "SPI:%-3s VOL:%-4d M:%d OK:%u",
        spi_ok ? "OK " : "ERR", adc_vol, mode, sync_ok_cnt);
    lcd_string(2, 228, buf, spi_ok ? COLOR_DKGREEN : COLOR_RED, COLOR_BLACK);
}

// =============================================================================
//  [섹션 D]  공통 상태 & 프리셋
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 D1]  전역 설정 구조체  (SQLite 직렬화 준비)
//
//  향후 SQLite 스키마 (참고):
//  CREATE TABLE presets (
//    id           INTEGER PRIMARY KEY AUTOINCREMENT,
//    name         TEXT NOT NULL,
//    created_at   INTEGER,          -- Unix timestamp
//    fm0_inst     INTEGER, fm0_edited INTEGER, fm0_patch BLOB(44),
//    fm1_inst     INTEGER, fm1_edited INTEGER, fm1_patch BLOB(44),
//    fm2_inst     INTEGER, fm2_edited INTEGER, fm2_patch BLOB(44),
//    psg0_en      INTEGER, psg0_patch INTEGER, psg0_amp INTEGER, psg0_noff INTEGER,
//    psg1_en      INTEGER, psg1_patch INTEGER, psg1_amp INTEGER, psg1_noff INTEGER,
//    psg2_en      INTEGER, psg2_patch INTEGER, psg2_amp INTEGER, psg2_noff INTEGER,
//    pre_eq_preset    INTEGER, vca_preset INTEGER,
//    dly_preset   INTEGER, lfo_preset INTEGER,
//    ch_mode      INTEGER, unison_inst INTEGER,
//    vm_on        INTEGER, ji_on  INTEGER,
//    cp_on        INTEGER, kd_on  INTEGER,
//    kd_min_notes INTEGER, pb_semi INTEGER,
//    swing_ratio  INTEGER, scale_type INTEGER, key_root INTEGER
//  );
// ─────────────────────────────────────────────────────────────────────────────

/* FM 채널별 설정 */
/* ???????????????????????????????????????????????????????????????????????????
   [V16 수정 1/6] fm_ch_cfg_t 구조체 - Dual/Triple 플래그 추가

   - use_dual 필드 추가: Dual 패치(8OP) 사용 여부
   - use_triple 필드 추가: Triple 패치(12OP) 사용 여부

   ??????????????????????????????????????????????????????????????????????????? */
typedef struct {
    uint8_t inst_idx;         /* full_inst / raw_patch / dual / triple 인덱스 */
    uint8_t use_full : 1;   /* 1=full_inst_t, 0=raw patch */
    uint8_t use_dual : 1;   /* [V16 추가] 1=dual_patch_t (8OP) */
    uint8_t use_triple : 1;   /* [V16 추가] 1=triple_patch_t (12OP) */
    uint8_t edited : 1;   /* 1=patch_override 사용 */
    uint8_t op_count : 3;  /* [FIX] 이 채널에 할당할 OP 수 (1~4, 0=기본=fm_n 따름) */
    uint8_t enable : 1;  /* 1=채널 발음 ON (기본), 0=소거/스킵 */
    uint8_t vol;              /* 0~15 */
    ym2203_patch_t patch_override;  /* 편집된 패치 */

    /* [V16 추가] Dual/Triple 패치 포인터 - 모드4 UI가 설정 */
    const dual_patch_t* dual_patch_ptr;    /* DUAL 모드용 */
    const triple_patch_t* triple_patch_ptr;  /* TRIPLE 모드용 */
} fm_ch_cfg_t;

/* PSG 채널별 설정 */
typedef struct {
    uint8_t          enable;
    ym2203_ssg_idx_t patch_idx;
    uint8_t          amp;           /* 0~15                                  */
    int8_t           note_offset;   /* 반음 오프셋 (-12 ~ +12)               */
    /* SQLite: psg{N}_en, psg{N}_patch, psg{N}_amp, psg{N}_noff */
} psg_ch_cfg_t;

/* 화성학 엔진 설정 */
typedef struct {
    /* ─ voice_mgr / 보이스 스케줄러 ─ */
    uint8_t  voice_mgr_on;     /* 1=vm_note_on() 경로 사용 (FM+PSG 6ch 관리) */
    uint8_t  vm_chord_auto;    /* 1=ca_analyze() 로 코드 자동 판별           */

    /* ─ 순정률(JI) 보정 ─ */
    uint8_t  ji_on;            /* 1=kd_ji_note_ex() 음정 보정 적용           */
    uint8_t  pb_semitones;     /* 피치벤드 범위 반음 수 (기본 2)             */

    /* ─ 조 판별 ─ */
    uint8_t  key_detect_on;    /* 1=kd_note_on() 실시간 조 판별 ON           */
    uint8_t  kd_min_notes;     /* 조 판별 시작 최소 노트 수 (기본 4)         */
    uint8_t  key_root;         /* 고정 루트 (0~11, key_detect=OFF 시 사용)   */
    uint8_t  scale_type;       /* scale_type_t (0=Major~8=계면조)            */

    /* ─ 대위법 ─ */
    uint8_t  counterpoint_on;  /* 1=cp_check_parallels() 병행5도 감지         */
    uint8_t  suspension_on;    /* 1=cp_prepare_suspension() 계류음 처리       */

    /* ─ 퍼포먼스 매핑 ─ */
    uint8_t  vel_car_ratio;    /* 캐리어 TL 벨로시티 감도 0~8 (기본 6)       */
    uint8_t  vel_mod_ratio;    /* 모듈레이터 TL 벨로시티 감도 0~8 (기본 2)   */

    /* ─ 스윙 / 아티큘레이션 ─ */
    uint8_t  swing_ratio;      /* 0=straight, 1~49=swing% (seq_tick용)       */

    /* ─ 농음(비브라토) ─ */
    uint8_t  nlfo_preset;      /* nlfo_preset_t: 0=HWANG~4=NAM, 0xFF=OFF    */
    uint8_t  nlfo_rand_range;  /* 랜덤 워크 폭 cent (0~20, 기본 8)           */

    /* ─ 포르타멘토 ─ */
    uint8_t  porta_mode;       /* pt_mode_t: 0=SLIDE 1=UP 2=DOWN 4=EXP      */
    uint8_t  porta_ticks;      /* 보간 틱 수 (0=OFF)                         */

    /* SQLite: vm_on, ji_on, cp_on, kd_on, kd_min_notes,
               pb_semi, swing_ratio, scale_type, key_root 등 각 컬럼 */
} harmony_cfg_t;


/* ???????????????????????????????????????????????????????????????????????????
   [V16 수정 2/6] 채널 모드 enum 추가

   3가지 FM 채널 운용 모드:

   1. CH_MODE_INDEPENDENT (기본값)
      - 3개 채널 각각 독립: FM0(4OP) + FM1(4OP) + FM2(4OP)

   2. CH_MODE_DUAL
      - FM0+FM1 통합(8OP) + FM2 독립(4OP)

   3. CH_MODE_TRIPLE
      - FM0+FM1+FM2 전체 통합(12OP)
   ??????????????????????????????????????????????????????????????????????????? */
typedef enum {
    CH_MODE_INDEPENDENT = 0,  /* 3개 채널 독립: 3 x 4OP (폴리) */
    CH_MODE_DUAL = 1,         /* FM 0+1 합성: 8OP + FM 2 보조 (모노) */
    CH_MODE_TRIPLE = 2,       /* FM 0+1+2 합성: 12OP (모노) */
    CH_MODE_VGM6 = 3        /* VGM 실측 6ch 프리셋 (full_inst_t: fm[3]+ssg[3]) */
} channel_mode_t;

/* 완전 설정 ? 전역 단일 진실 소스 */
typedef struct {
    char          preset_name[32];  /* SQLite name 컬럼 */

    channel_mode_t ch_mode;         /* [V16 추가] 채널 운용 모드 */
    uint8_t       vgm6_inst_idx;   /* CH_MODE_VGM6: FULL6_VGM_CATALOG 인덱스 (0..FULL6_VGM_CATALOG_COUNT-1) */
    /* SQLite: ch_mode INTEGER, vgm6_inst INTEGER, */

    fm_ch_cfg_t   fm[3];
    psg_ch_cfg_t  psg[3];
    harmony_cfg_t harmony;

    uint8_t       pre_eq_preset;
    uint8_t       pre_eq_custom_valid;   /* 1=Mode4 직접 편집 band 파라미터를 Mode3에서 재적용 */
    uint8_t       post_eq_custom_valid;  /* 1=POST EQ 직접 편집 band 파라미터를 Mode3에서 재적용 */
    uint8_t       eq8_enabled;           /* 1=PRE 4band+POST 4band as single 8band preset */
    uint8_t       eq8_preset;            /* index into EQ8_PRESET_* (eq_8band_presets.h) */
    uint8_t       vca_preset;
    uint8_t       dly_preset;
    uint8_t       lfo_preset;

    /* [FIX-M4-DLY-SAVE]
     * Mode 4 DELAY 세부 편집값.
     * 기존 구조는 dly_preset 번호만 저장해서 SW5로 time/fb/wet 등을 바꿔도
     * APPLY 또는 모드 재진입 시 apply_dly_preset()이 세부값을 다시 덮어썼다.
     */
    uint8_t       dly_custom_valid;
    float         dly_time_ms;
    float         dly_fb_pct;
    float         dly_dw_pct;
    float         dly_damp_a_pct;
    float         dly_damp_hf_pct;
    uint16_t      dly_sat_thresh;
    uint8_t       dly_wow_depth;
    float         dly_diff_mix_pct;
    float         dly_diff_g_pct;
    float         dly_ms_pct;
    uint16_t      dly_slew_raw;
    float         dly_tap1_ms;
    float         dly_tap1_gain;
    float         dly_tap2_ms;
    float         dly_tap2_gain;

    uint8_t       rev_preset;    /* fdn_reverb space preset (FDN_SP_*) 0..23   */
    uint8_t       warm_preset;   /* warm_engine 프리셋 인덱스 (WE_PRESET_TABLE) */
    uint8_t       svf_preset;    /* character_svf 프리셋 (svf_body_id_t)        */
    uint8_t       post_eq_preset;/* post_eq_4band 프리셋 (eq_4band_presets.h 공용) */

    /* [V15r] reverb 세부 파라미터 ? fdn_preset_defaults[] 기준값을 오프셋/덮어쓰기 */
    /* 0 = "preset 기본값 그대로 사용"을 나타내는 sentinel 로 초기화.
     * 편집 시 아래 값들이 fdn_apply_preset() 이후 덮어써진다. */
    uint32_t      rev_wet_q24;       /* wet_mix   Q8.24 (0=preset default)   */
    uint32_t      rev_dry_q24;       /* dry_mix   Q8.24 (0=preset default)   */
    uint16_t      rev_rt60_ms;       /* RT60 ms   (0=preset default)          */
    uint32_t      rev_decay_q24;     /* decay_gain Q8.24 (0=preset default)   */
    uint32_t      rev_width_q24;     /* stereo_width Q8.24 (0=preset default) */
    uint32_t      rev_er_q24;        /* er_level  Q8.24 (0=preset default)    */
    uint32_t      rev_shimmer_q24;   /* shimmer_mix Q8.24 (0=no shimmer)      */
    uint8_t       rev_custom_valid;  /* 1=세부값 유효, 0=preset default 사용  */

    uint32_t      created_at;       /* SQLite timestamp */
} full_preset_t;


/* -----------------------------------------------------------------------------
 * [YM2203-MODE-HELPER] Mode 3/4 공용 채널 모드 유틸리티
 * - Mode 4에서 선택한 INDEPENDENT / DUAL / TRIPLE / VGM6 상태를
 *   Mode 3 발음 경로와 HW APPLY 경로가 동일하게 사용한다.
 * - DUAL/TRIPLE 포인터가 비어 있으면 ym2203_multi.h의 대표 패치를 자동 연결한다.
 * ----------------------------------------------------------------------------- */
static inline const char* ch_mode_name(channel_mode_t mode)
{
    switch (mode) {
    case CH_MODE_INDEPENDENT: return "INDEPENDENT";
    case CH_MODE_DUAL:        return "DUAL-8OP";
    case CH_MODE_TRIPLE:      return "TRIPLE-12OP";
    case CH_MODE_VGM6:        return "VGM6-FULL";
    default:                  return "UNKNOWN";
    }
}

static inline void full_preset_sync_combo_patch(full_preset_t* p)
{
    if (!p) return;

    if (p->ch_mode == CH_MODE_DUAL) {
        p->fm[0].use_dual = 1u;
        p->fm[0].use_triple = 0u;
        if (p->fm[0].dual_patch_ptr == NULL)
            p->fm[0].dual_patch_ptr = YM_DUAL[DUAL_IDX_PIANO];
    }
    else if (p->ch_mode == CH_MODE_TRIPLE) {
        p->fm[0].use_dual = 0u;
        p->fm[0].use_triple = 1u;
        if (p->fm[0].triple_patch_ptr == NULL)
            p->fm[0].triple_patch_ptr = YM_TRIPLE[TRIPLE_IDX_ORCH_STR];
    }
    else {
        p->fm[0].use_dual = 0u;
        p->fm[0].use_triple = 0u;
    }
}

static full_preset_t g_full_preset;


/* 화성학 엔진 런타임 상태 ? g_full_preset.harmony 설정을 참조 */
static voice_mgr_t     g_voice_mgr;
static midi_perf_t     g_midi_perf;
static nonlinear_lfo_t g_nlfo;
static uint8_t         g_nlfo_note = 0;  /* 현재 발음 중인 노트 (농음용)     */
static counterpoint_t  g_cp;

/* ADC 버퍼 */
static uint16_t adc_buf[4] = { ADC_MAX / 2, ADC_MAX / 2, ADC_CENTER, ADC_CENTER };
#define ADC_IDX_VOL   0
#define ADC_IDX_PITCH 1
#define ADC_IDX_JOYX  2
#define ADC_IDX_JOYY  3

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 D2]  이펙터 프리셋 테이블
// ─────────────────────────────────────────────────────────────────────────────
/* EQ preset table은 eq_4band_presets.h(30개 프리셋, 4밴드)를 사용합니다. */
/* pre_eq / post_eq 모두 동일 헤더(eq_4band_presets.h) 프리셋 공용 */
#define pre_eq_preset_COUNT  EQ_PRESET_COUNT
#define pre_eq_preset_NAMES  EQ_PRESET_NAMES
#define pre_eq_preset_DATA   EQ_PRESET_DATA
#define post_eq_preset_COUNT EQ_PRESET_COUNT
#define post_eq_preset_NAMES EQ_PRESET_NAMES
#define post_eq_preset_DATA  EQ_PRESET_DATA

/* ============================================================
 * VCA 프리셋 (vca_compressor.h VCA_Commit 기반)
 *
 * 프리셋별 체감 특성:
 *   0  BYPASS      : 1:1 투명 통과 (압축 없음)
 *   1  SOFT 2:1    : 부드러운 버스 글루 (기존)
 *   2  FINAL +3dB  : 4:1 + makeup +3dB (기존)
 *   3  PUNCH       : 어택 강조, 킥/스네어 느낌 (체감 강함)
 *   4  SQUASH      : 강한 압축 8:1, 과감한 빈티지 느낌
 *   5  SUSTAIN     : 긴 릴리즈로 음 꼬리 연장 (FM 잔향 극대화)
 *   6  LIMITER     : 브릭월 리미터 (피크 제한)
 *   7  WARM GLUE   : 느린 RMS 글루, 두껍고 따뜻한 질감
 *   8  BRIGHT ATK  : 빠른 어택/릴리즈, 선명한 어택 부각
 *   9  SIDECHAIN   : 사이드체인 느낌, 강한 펌핑 효과
 * ============================================================ */
#define VCA_PRESET_COUNT 10

static const char* const VCA_PRESET_NAMES[VCA_PRESET_COUNT] = {
    "BYPASS   ",   /* 0 */
    "SOFT 2:1 ",   /* 1 */
    "FINAL+3dB",   /* 2 */
    "PUNCH    ",   /* 3 */
    "SQUASH   ",   /* 4 */
    "SUSTAIN  ",   /* 5 */
    "LIMITER  ",   /* 6 */
    "WARM GLUE",   /* 7 */
    "BRIGHT   ",   /* 8 */
    "SIDECHAIN",   /* 9 */
};

/* --- 프리셋 함수 (모두 VCA_Commit 경유) --------------------------------- */
typedef void (*VoidFn)(void);

static void vca_bypass_wrap(void) { vca_bypass(); }
static void vca_soft_wrap(void) { vca_soft(); }
static void vca_final_wrap(void) { vca_final(); }

/* 3: PUNCH ? 어택 강조 (-18dBFS / 4:1 / Peak / A=8ms / R=60ms / +3dB) */
static void vca_preset_punch(void) {
    if (!p_vca) return;
    vca_state_t s = { -18.0f, 4.0f, 2.0f, 8.0f, 60.0f, 3.0f, VCA_DETECTOR_PEAK };
    VCA_Commit((uintptr_t)p_vca, &s);
    printf("[VCA] PUNCH: T=-18dBFS R=4:1 A=8ms D=60ms M=+3dB PEAK\n");
}

/* 4: SQUASH ? 강한 압축 (-24dBFS / 8:1 / Peak / A=2ms / R=50ms / +5dB) */
static void vca_preset_squash(void) {
    if (!p_vca) return;
    vca_state_t s = { -24.0f, 8.0f, 1.0f, 2.0f, 50.0f, 5.0f, VCA_DETECTOR_PEAK };
    VCA_Commit((uintptr_t)p_vca, &s);
    printf("[VCA] SQUASH: T=-24dBFS R=8:1 A=2ms D=50ms M=+5dB PEAK\n");
}

/* 5: SUSTAIN ? FM 음 꼬리 연장 (-20dBFS / 3:1 / RMS / A=5ms / R=800ms / +4dB) */
static void vca_preset_sustain(void) {
    if (!p_vca) return;
    vca_state_t s = { -20.0f, 3.0f, 5.0f, 5.0f, 800.0f, 4.0f, VCA_DETECTOR_RMS };
    VCA_Commit((uintptr_t)p_vca, &s);
    printf("[VCA] SUSTAIN: T=-20dBFS R=3:1 A=5ms D=800ms M=+4dB RMS\n");
}

/* 6: LIMITER ? 브릭월 리미터 (-3dBFS / INF / Peak / A=0.1ms / R=50ms) */
static void vca_preset_limiter(void) {
    if (!p_vca) return;
    vca_state_t s = { -3.0f, VCA_RATIO_INFINITY, 0.5f, 0.1f, 50.0f, 0.0f, VCA_DETECTOR_PEAK };
    VCA_Commit((uintptr_t)p_vca, &s);
    printf("[VCA] LIMITER: T=-3dBFS R=INF A=0.1ms D=50ms PEAK\n");
}

/* 7: WARM GLUE ? 두꺼운 버스 질감 (-14dBFS / 2:1 / RMS / A=40ms / R=300ms / +2dB) */
static void vca_preset_warm_glue(void) {
    if (!p_vca) return;
    vca_state_t s = { -14.0f, 2.0f, 8.0f, 40.0f, 300.0f, 2.0f, VCA_DETECTOR_RMS };
    VCA_Commit((uintptr_t)p_vca, &s);
    printf("[VCA] WARM GLUE: T=-14dBFS R=2:1 K=8dB A=40ms D=300ms M=+2dB RMS\n");
}

/* 8: BRIGHT ATK ? 선명한 어택 부각 (-12dBFS / 3:1 / Peak / A=1ms / R=40ms / +2dB) */
static void vca_preset_bright(void) {
    if (!p_vca) return;
    vca_state_t s = { -12.0f, 3.0f, 0.5f, 1.0f, 40.0f, 2.0f, VCA_DETECTOR_PEAK };
    VCA_Commit((uintptr_t)p_vca, &s);
    printf("[VCA] BRIGHT: T=-12dBFS R=3:1 A=1ms D=40ms M=+2dB PEAK\n");
}

/* 9: SIDECHAIN ? 강한 펌핑 효과 (-30dBFS / 10:1 / Peak / A=0.5ms / R=120ms / +6dB) */
static void vca_preset_sidechain(void) {
    if (!p_vca) return;
    vca_state_t s = { -30.0f, 10.0f, 1.0f, 0.5f, 120.0f, 6.0f, VCA_DETECTOR_PEAK };
    VCA_Commit((uintptr_t)p_vca, &s);
    printf("[VCA] SIDECHAIN: T=-30dBFS R=10:1 A=0.5ms D=120ms M=+6dB PEAK\n");
}

static const VoidFn VCA_PRESET_FN[VCA_PRESET_COUNT] = {
    vca_bypass_wrap,       /* 0 BYPASS    */
    vca_soft_wrap,         /* 1 SOFT 2:1  */
    vca_final_wrap,        /* 2 FINAL+3dB */
    vca_preset_punch,      /* 3 PUNCH     */
    vca_preset_squash,     /* 4 SQUASH    */
    vca_preset_sustain,    /* 5 SUSTAIN   */
    vca_preset_limiter,    /* 6 LIMITER   */
    vca_preset_warm_glue,  /* 7 WARM GLUE */
    vca_preset_bright,     /* 8 BRIGHT    */
    vca_preset_sidechain,  /* 9 SIDECHAIN */
};

/* ── 딜레이 프리셋 (analog_delay.h Rev.15 API 기반) ── */
#define DLY_PRESET_COUNT 14
static const char* const DLY_PRESET_NAMES[DLY_PRESET_COUNT] = {
    /* 0 */ "BYPASS   ",
    /* 1 */ "TAPE ECHO",   /* Echoplex/SpaceEcho 따뜻한 테이프 에코 */
    /* 2 */ "SLAPBACK ",   /* 로커빌리 단발 슬랩백 */
    /* 3 */ "ROOM AMB ",   /* 짧은 APF 룸 앰비언스 */
    /* 4 */ "BBD THIK ",   /* BBD 코러스 경계 두께감 */
    /* 5 */ "PINGPONG ",   /* 핑퐁 (120BPM 점8분+8분) */
    /* 6 */ "GHOST    ",   /* 은은한 공간감 */
    /* 7 */ "BITCRUSH ",   /* 8bit 느낌 극단 클리핑 */
    /* 8 */ "REVERSE  ",   /* APF FB + 느린 슬루 리버스 느낌 */
    /* 9 */ "DAEGEUM  ",   /* 대금: 청아한 낮은 댐핑 에코 */
    /*10 */ "GAYAGEUM ",   /* 가야금: 맑은 공명, 홀 반사 */
    /*11 */ "HAEGEUM  ",   /* 해금: 짧고 집중된 현악 에코 */
    /*12 */ "PIRI     ",   /* 피리: 따뜻한 중역 두께감 */
    /*13 */ "AJAENG   ",   /* 아쟁: 깊고 무거운 저역 여운 */
};

/* 각 프리셋 적용 함수 ? g_adly 어댑터 핸들 사용 */
static void _dly_p0(void) {
    /* HW bypass ON: RTL이 in_hold 직결 출력.
     * DRY_WET는 건드리지 않는다 ? 0을 쓰면 bypass ON 상태에서도
     * 출력 스케일이 0이 되어 완전 무음이 된다. (ADLY RTL 실측 버그) */
    analog_delay_bypass_on(&g_adly);
    analog_delay_feedback_off(&g_adly);
    adly_wr(&g_adly, ADLY_OFF_TAP_EN, 0u);
    printf("[DELAY] BYPASS\n");
}
static void _dly_p1(void) { analog_delay_preset_tape_echo(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p2(void) { analog_delay_preset_slapback(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p3(void) { analog_delay_preset_room_ambience(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p4(void) { analog_delay_preset_bbd_short_thicken(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p5(void) { analog_delay_preset_pingpong_dotted(&g_adly, 120.0f); analog_delay_bypass_off(&g_adly); }
static void _dly_p6(void) { analog_delay_preset_ghost_echo(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p7(void) { analog_delay_preset_bitcrush(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p8(void) { analog_delay_preset_reverse_sim(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p9(void) { analog_delay_preset_daegeum(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p10(void) { analog_delay_preset_gayageum(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p11(void) { analog_delay_preset_haegeum(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p12(void) { analog_delay_preset_piri(&g_adly); analog_delay_bypass_off(&g_adly); }
static void _dly_p13(void) { analog_delay_preset_ajaeng(&g_adly); analog_delay_bypass_off(&g_adly); }

/* VoidFn: 위 VCA 섹션에서 typedef 완료 */
static const VoidFn DLY_PRESET_FN[DLY_PRESET_COUNT] = {
    _dly_p0,  _dly_p1,  _dly_p2,  _dly_p3,
    _dly_p4,  _dly_p5,  _dly_p6,  _dly_p7,
    _dly_p8,  _dly_p9,  _dly_p10, _dly_p11,
    _dly_p12, _dly_p13
};

/* ── LFO0 프리셋 (nco_multi_lfo ch0 → delay APF 변조) ── */
/* LFO FCW 계산: FCW = freq_hz × 2^32 / 50MHz
 *   0.1Hz → 0x0000000A  0.5Hz → 0x00000032  1Hz → 0x00000064
 *   2Hz   → 0x000000C8  5Hz  → 0x000001F4  10Hz → 0x000003E8  */
#define LFO_FCW_0_5HZ   0x00000032u
#define LFO_FCW_1HZ     0x00000064u
#define LFO_FCW_2HZ     0x000000C8u
#define LFO_FCW_5HZ     0x000001F4u

#define LFO0_PRESET_COUNT 6
static const char* const LFO0_PRESET_NAMES[LFO0_PRESET_COUNT] = {
    "OFF      ", "SLOW.1Hz ", "CHORUS1Hz", "VIBR 2Hz ",
    "FAST 5Hz ", "MAX 10Hz ",
};
/* depth / wave: 0=sine,1=tri,2=saw,3=rnd */
static const uint32_t LFO0_FCW[LFO0_PRESET_COUNT] = {
    LFO_FCW_OFF, LFO_FCW_0_1HZ, LFO_FCW_1HZ, LFO_FCW_2HZ,
    LFO_FCW_5HZ, LFO_FCW_10HZ
};
static const uint16_t LFO0_DEPTH[LFO0_PRESET_COUNT] = {
    0x0000u, 0x0080u, 0x0180u, 0x0200u, 0x0180u, 0x0180u
};
static const uint8_t  LFO0_WAVE[LFO0_PRESET_COUNT] = { 0, 0, 0, 0, 0, 0 };

#define LFO_PRESET_COUNT LFO0_PRESET_COUNT  /* 하위 호환 alias */
static const char* const* LFO_PRESET_NAMES = LFO0_PRESET_NAMES;

static void apply_pre_eq_preset(uint8_t idx)
{
    if (idx >= pre_eq_preset_COUNT) idx = 0;
    eq_apply(pre_eq_preset_DATA[idx], pre_eq_preset_NAMES[idx]);
}
static void apply_vca_preset(uint8_t idx)
{
    if (idx >= VCA_PRESET_COUNT) idx = 0;  /* 범위 초과 → BYPASS */
    VCA_PRESET_FN[idx]();
}
static void apply_dly_preset(uint8_t idx)
{
    if (idx >= DLY_PRESET_COUNT) idx = 1;
    DLY_PRESET_FN[idx]();
}

/* [FIX-M4-DLY-SAVE]
 * g_full_preset에 저장된 DELAY 세부값을 HW로 다시 반영한다.
 * apply_dly_preset() 직후 호출해야 preset 기본값 위에 사용자가 저장한
 * time/fb/wet/tap 값을 덮어쓸 수 있다.
 */
static void apply_dly_custom_from_full_preset(void)
{
    /* ── 순서: bypass ON 상태에서 모든 레지스터 확정 → bypass OFF ─────────
     * bypass OFF 순간부터 BRAM 출력이 믹스에 합류하므로,
     * 모든 파라미터(lfo_depth_apf 포함)가 그 이전에 안정돼야 한다.
     * apply_lfo_then_delay() → apply_lfo_preset() 이 이미 LFO를 설정했고
     * 이 함수는 그 다음에 호출됨. */

     /* 1. 프리셋 기본값 적용. 내부에서 bypass_off가 일어날 수 있으므로
      *    직후 bypass_on으로 재확보. */
    apply_dly_preset(g_full_preset.dly_preset);

    if (!p_delay) return;
    if (g_full_preset.dly_preset == 0) return;

    /* preset 함수 내부 bypass_off를 되돌림: custom 값 기록 완료 후 한 번만 OFF */
    analog_delay_bypass_on(&g_adly);

    if (!g_full_preset.dly_custom_valid) {
        /* custom 없음: 프리셋 기본값으로 그대로 bypass 해제 */
        printf("[DLY] preset=%u 적용 (custom 없음)\n", g_full_preset.dly_preset);
        analog_delay_bypass_off(&g_adly);
        return;
    }

    /* custom 값을 bypass ON 상태에서 전부 기록 */
    analog_delay_set_slew_rate(&g_adly, ADLY_SLEW_INSTANT);
    if (g_full_preset.dly_time_ms > 0.0f)
        analog_delay_set_time_ms(&g_adly, g_full_preset.dly_time_ms);
    analog_delay_set_feedback_pct(&g_adly, g_full_preset.dly_fb_pct);
    analog_delay_set_dry_wet_pct(&g_adly, g_full_preset.dly_dw_pct);

    analog_delay_set_damp_alpha_pct(&g_adly, g_full_preset.dly_damp_a_pct);
    analog_delay_set_damp_hf_pct(&g_adly, g_full_preset.dly_damp_hf_pct);

    {
        uint8_t apf_start = (g_full_preset.dly_diff_mix_pct > 0.5f)
            ? 0x00u : ADLY_DIFFUSION_OFF;
        adly_wr(&g_adly, ADLY_OFF_DIFFUSION_START, (uint32_t)apf_start);
        adly_wr(&g_adly, ADLY_OFF_DIFFUSION_MIX,
            (uint32_t)ADLY_PCT_TO_Q15(g_full_preset.dly_diff_mix_pct));
        adly_wr(&g_adly, ADLY_OFF_DIFFUSION_G,
            (uint32_t)ADLY_PCT_TO_Q15(g_full_preset.dly_diff_g_pct));
    }

    if (g_full_preset.dly_sat_thresh >= ADLY_SAT_THRESH_MIN)
        analog_delay_set_sat_thresh(&g_adly, g_full_preset.dly_sat_thresh);

    if (g_full_preset.dly_slew_raw >= ADLY_SLEW_RATE_MIN)
        analog_delay_set_slew_rate(&g_adly, g_full_preset.dly_slew_raw);

    /* 모든 레지스터 확정 후 bypass OFF */
    analog_delay_bypass_off(&g_adly);

    printf("[DLY] custom 적용: preset=%u time=%.1fms fb=%.1f%% wet=%.1f%%"
        " damp=%.1f%% diff=%.1f%% slew=0x%04X\n",
        g_full_preset.dly_preset, g_full_preset.dly_time_ms,
        g_full_preset.dly_fb_pct, g_full_preset.dly_dw_pct,
        g_full_preset.dly_damp_a_pct, g_full_preset.dly_diff_mix_pct,
        g_full_preset.dly_slew_raw);
}

static void apply_lfo_preset(uint8_t idx)
{
    /* RTL analog_delay_top.v 배선:
     *   lfo_mux   = lfo_0                       (CH0: APF g 주 변조, 무감쇠)
     *   lfo_mix   = lfo_0 + (lfo_4 >>> 3)       (CH4: L/R 공통 offset, ÷8)
     *   lfo_apf_R = lfo_mix + (lfo_5 >>> 2)     (CH5: R 전용 offset,  ÷4)
     *   CH1~3, CH6~7 → delay_top 배선 없음(DEAD)
     * OFF 시 CH0/4/5 모두 정지해야 APF g 변조가 완전히 0이 됨. */
    if (idx == 0 || idx >= LFO0_PRESET_COUNT) {
        lfo_stop_ch(0);
        lfo_stop_ch(4);
        lfo_stop_ch(5);
        printf("[LFO0] OFF (CH0/4/5 정지)\n");
        return;
    }
    /* CH0: 주 변조 */
    lfo_set_ch(0,
        LFO0_FCW[idx],
        LFO0_DEPTH[idx],
        0u,
        (uint16_t)LFO0_WAVE[idx]);
    /* CH4: L/R 공통 offset. RTL이 ÷8 하므로 레지스터값 = 실효값×8.
     * depth = CH0 depth × (8/3), FCW = CH0 FCW × 0.375 (beating 효과). */
    {
        uint32_t d4 = ((uint32_t)LFO0_DEPTH[idx] * 8u) / 3u;
        if (d4 > LFO_DEPTH_MAX) d4 = LFO_DEPTH_MAX;
        uint32_t fcw4 = (uint32_t)(((uint64_t)LFO0_FCW[idx] * 3u) / 8u);
        lfo_set_ch(4, fcw4, (uint16_t)d4, 0u, (uint16_t)LFO0_WAVE[idx]);
    }
    /* CH5: R 전용 offset. RTL이 ÷4 하므로 레지스터값 = 실효값×4.
     * depth = CH0 depth × (4/3), +45° 위상차로 스테레오 확산. */
    {
        uint32_t d5 = ((uint32_t)LFO0_DEPTH[idx] * 4u) / 3u;
        if (d5 > LFO_DEPTH_MAX) d5 = LFO_DEPTH_MAX;
        uint32_t fcw5 = (uint32_t)(((uint64_t)LFO0_FCW[idx] * 3u) / 8u);
        lfo_set_ch(5, fcw5, (uint16_t)d5, LFO_POFF_45DEG, (uint16_t)LFO0_WAVE[idx]);
    }
    printf("[LFO0] preset[%u]=%s CH0:fcw=0x%08X depth=0x%04X  CH4/5 비례설정\n",
        idx, LFO0_PRESET_NAMES[idx], LFO0_FCW[idx], LFO0_DEPTH[idx]);
}

static void diag_bypass_delay(const char* reason);  /* forward decl */

/* ── apply_lfo_then_delay(): 반드시 LFO 먼저, 딜레이 bypass 해제 나중 ────────
 * RTL: bypass OFF 순간 BRAM 출력이 믹스에 합류.
 * 그 시점에 LFO(APF g 변조)가 안정돼 있어야 자글자글 잡음 없음.
 * 모드4 preview / APPLY / 모드2 재생 등 모든 경로에서 이 함수를 통해 호출. */
static void apply_lfo_then_delay(void)
{
    if (!DIAG_USE_DELAY) {
        diag_bypass_delay("apply_lfo_then_delay");   /* CH0/4/5 정지 포함 */
        return;
    }
#if ENABLE_HW_LFO0_DELAY
    apply_lfo_preset(g_full_preset.lfo_preset);      /* ① LFO CH0/4/5 먼저 */
#endif
    apply_dly_custom_from_full_preset();             /* ② bypass OFF 포함 */
}

/* ─── post_eq 적용 (pre_eq와 동일 계수 테이블, 다른 HW 포인터) ─── */
static void eq_apply_post(const BiquadCoef p[4], const char* name)
{
    if (!p_post_eq) return;

    /* post EQ도 pre_eq_4band.v 동일 RTL 사용 → 동일한 coef_bypass 시퀀스 적용 */
    reg_wr(p_post_eq, EQ_REG_CTRL, PRE_EQ_CTRL_COEF_BYPASS);
    for (int b = 0; b < 4; b++) {
        reg_wr(p_post_eq, EQ_REG_OFF(b, 0), p[b].b0 & 0x00FFFFFFu);
        reg_wr(p_post_eq, EQ_REG_OFF(b, 1), p[b].b1 & 0x00FFFFFFu);
        reg_wr(p_post_eq, EQ_REG_OFF(b, 2), p[b].b2 & 0x00FFFFFFu);
        reg_wr(p_post_eq, EQ_REG_OFF(b, 3), p[b].a1 & 0x00FFFFFFu);
        reg_wr(p_post_eq, EQ_REG_OFF(b, 4), p[b].a2 & 0x00FFFFFFu);
    }
    reg_wr(p_post_eq, EQ_REG_CTRL, PRE_EQ_CTRL_RUN);
    printf("[POST_EQ] %s applied\n", name);
}

static void apply_post_eq_preset(uint8_t idx)
{
    if (idx >= post_eq_preset_COUNT) idx = 0;
    eq_apply_post(post_eq_preset_DATA[idx], post_eq_preset_NAMES[idx]);
}


/* =============================================================================
 * [DIAG-CHAIN-BYPASS-HELPERS]
 * 비활성 단계의 이펙터는 Mode 4에서 조작해도 즉시 bypass/pass-through로 되돌린다.
 * 목적: YM2203만 → PRE EQ 추가 → VCA 추가 → POST EQ 추가 → DELAY 추가 순서로
 *       어느 블록에서 문제가 재현되는지 분리 확인.
 * ============================================================================= */
static const char* diag_chain_stage_name(void)
{
    switch (DIAG_CHAIN_STAGE) {
    case 0: return "STAGE0_YM_ONLY";
    case 1: return "STAGE1_YM_PRE_EQ";
    case 2: return "STAGE2_YM_PRE_EQ_VCA";
    case 3: return "STAGE3_YM_PRE_EQ_VCA_POST_EQ";
    case 4: return "STAGE4_YM_PRE_EQ_VCA_POST_EQ_DELAY";
    default: return "STAGE_UNKNOWN";
    }
}

static void diag_bypass_pre_eq(const char* reason)
{
    if (p_pre_eq) {
        reg_wr(p_pre_eq, EQ_REG_CTRL, PRE_EQ_CTRL_BYPASS);  /* bit0=bypass, bit1=coef_bypass */
    }
    printf("[DIAG-%s] PRE_EQ BYPASS (%s)\n", diag_chain_stage_name(), reason ? reason : "-");
}

static void diag_bypass_post_eq(const char* reason)
{
    if (p_post_eq) {
        reg_wr(p_post_eq, EQ_REG_CTRL, PRE_EQ_CTRL_BYPASS); /* bit0=bypass, bit1=coef_bypass */
    }
    printf("[DIAG-%s] POST_EQ BYPASS (%s)\n", diag_chain_stage_name(), reason ? reason : "-");
}

static void diag_bypass_vca(const char* reason)
{
    /* VCA는 통과용 설정: threshold max + ratio 1:1 + makeup 1.0 */
    vca_bypass();
    printf("[DIAG-%s] VCA BYPASS (%s)\n", diag_chain_stage_name(), reason ? reason : "-");
}

static void diag_bypass_delay(const char* reason)
{
    if (p_delay) {
        analog_delay_set_feedback_q15(&g_adly, 0u);
        analog_delay_set_dry_wet_q15(&g_adly, 0u); /* dry only */
        analog_delay_bypass_on(&g_adly);           /* HW bypass: input pass-through */
    }
#if ENABLE_HW_LFO0_DELAY
    lfo_stop_ch(0);
    lfo_stop_ch(4);   /* CH4: L/R 공통 offset */
    lfo_stop_ch(5);   /* CH5: R 전용 offset   */
#endif
    printf("[DIAG-%s] DELAY/LFO0 BYPASS (%s)\n", diag_chain_stage_name(), reason ? reason : "-");
}

static void diag_apply_chain_policy(const char* reason)
{
    if (!DIAG_USE_PRE_EQ)  diag_bypass_pre_eq(reason);
    if (!DIAG_USE_VCA)     diag_bypass_vca(reason);
    if (!DIAG_USE_POST_EQ) diag_bypass_post_eq(reason);
    if (!DIAG_USE_DELAY)   diag_bypass_delay(reason);
}

static int diag_top_item_enabled(int item)
{
    /* 현재 TOP index: YM=0, HARMONY=1, PRE_EQ=2, VCA=3, POST_EQ=4, EXIT=5.
     * DELAY/LFO는 Stage3에서 숨김 상수로만 유지한다. */
    if (item == 2)   return DIAG_USE_PRE_EQ;
    if (item == 3)   return DIAG_USE_VCA;
    if (item == 4)   return DIAG_USE_POST_EQ;
    if (item == 5)   return DIAG_USE_DELAY;  /* M4_T_DELAY */
    if (item == 107) return DIAG_USE_DELAY; /* M4_T_LFO */
    return 1;
}

/* ─── SVF 적용 ─── */

/* 이펙터 전체 초기화 ? LIGHT 체인 소리 보장 (모든 모드 진입 시 공통) */
static void fx_init_sound_guaranteed(void)
{
    /* [DIAG] 단계별 체인 제한:
     * 0 YM only / 1 +PRE EQ / 2 +VCA / 3 +POST EQ / 4 +DELAY */
    if (p_pre_eq)  reg_wr(p_pre_eq, EQ_REG_CTRL, DIAG_USE_PRE_EQ ? PRE_EQ_CTRL_RUN : PRE_EQ_CTRL_BYPASS);
    if (p_post_eq) reg_wr(p_post_eq, EQ_REG_CTRL, DIAG_USE_POST_EQ ? PRE_EQ_CTRL_RUN : PRE_EQ_CTRL_BYPASS);

    if (DIAG_USE_PRE_EQ)  apply_pre_eq_preset(g_full_preset.pre_eq_preset);
    else                  diag_bypass_pre_eq("fx_init");

    if (DIAG_USE_VCA)     apply_vca_preset(g_full_preset.vca_preset);
    else                  diag_bypass_vca("fx_init");

    if (DIAG_USE_POST_EQ) apply_post_eq_preset(g_full_preset.post_eq_preset);
    else                  diag_bypass_post_eq("fx_init");

    /* [EQ8] 8band unified preset: PRE/POST individual -> overwrite with EQ8 */
    if (g_full_preset.eq8_enabled && DIAG_USE_PRE_EQ && DIAG_USE_POST_EQ && p_pre_eq && p_post_eq) {
        apply_eq8_idx(p_pre_eq, p_post_eq, g_full_preset.eq8_preset);
        printf("[EQ8] fx_init apply idx=%u %s\n",
            (unsigned)g_full_preset.eq8_preset,
            EQ8_PRESET_NAMES[g_full_preset.eq8_preset]);
    }

    if (DIAG_USE_DELAY)   apply_dly_custom_from_full_preset();
    else                  diag_bypass_delay("fx_init");

#if ENABLE_HW_LFO0_DELAY
    if (DIAG_USE_DELAY)   apply_lfo_preset(g_full_preset.lfo_preset);
    else                  lfo_stop_ch(0);
#endif

    diag_apply_chain_policy("fx_init_sound_guaranteed");
    vca_readback();
    delay_readback();
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 D3]  YM2203 보이스 관리  (m3_full_* ? SYNTH/MIDI 공용)
//
//  [V14-D3] v13의 ym_synth_* + global_preset_t 기반 발음 로직을
//           full_preset_t + harmony_cfg_t 기반 m3_full_* 로 전면 교체.
//           voice_mgr_on / ji_on / nlfo_preset 경로 분기 포함.
// ─────────────────────────────────────────────────────────────────────────────

/* §P2  full_inst_t 카탈로그 */

/* ──────────────────────────────────────────────────────────────────────
   §9  오케스트라 악기 ? ym2203_patches.h Rev.8 VIOLIN/CELLO 패치 기반
   full_inst_t 형식으로 래핑. fm_n=1 (단일 FM 채널).
   LFO 비브라토(4~5Hz), SSG 없음 (현악기 특성).
   ────────────────────────────────────────────────────────────────────── */

   /* FULL_VIOLIN ? ALG1 FB6, 운궁 특성 (AR=22, SR=0 풀서스테인)
      § ym2203_patches.h Rev.8 [28] VIOLIN 패치 그대로 사용
      lfo_hz100=450(4.5Hz): 현악기 비브라토 적정 범위(4~6Hz)
      SSG 미사용: 현악기는 취구 노이즈 불필요 */
static const full_inst_t FULL_VIOLIN = {
    "FULL_VIOLIN", 1,
    {
        {"VIOLIN", 1, 6, 4, {
            { 3,  6, 20, 0, 22, 0,  8,  0,  2,  8,  0},  /* OP1 mod: AR=22, SR=0 */
            { 5,  2, 14, 0, 22, 0, 10,  0,  3,  8,  0},  /* OP2 mod: MUL=2 2배음 */
            { 3,  1, 26, 0, 20, 0, 12,  0,  4,  8,  0},  /* OP3 mod */
            { 5,  1,  7, 0, 20, 0,  6,  0,  1,  7,  0}}},/* OP4 car: SR=0 풀서스테인 */
        {"", 7, 0, 3, {{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                       {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
        {"", 7, 0, 3, {{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                       {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0, 0, 0}, {0, 0, 0}, {15, 0, 0},
    0, 0, 0, 0,         /* sw_op, sw_tl0, sw_tl1, sw_ticks */
    4, 1, 450,          /* lfo_vib=4, lfo_trem=1, lfo_hz100=450 */
    0x00,               /* ssg_mask: SSG 미사용 */
    {SSG_SQUARE_BRIGHT, SSG_SQUARE_BRIGHT, SSG_SQUARE_BRIGHT},
    {0, 0, 0}, {0, 0, 0}
};

/* FULL_CELLO ? ALG1 FB5, 바이올린보다 어두운 음색, 더 느린 어택
   § ym2203_patches.h Rev.8 [29] CELLO 패치
   lfo_hz100=380(3.8Hz): 첼로 비브라토(3~5Hz, 바이올린보다 느림)
   SSG 미사용 */
static const full_inst_t FULL_CELLO = {
    "FULL_CELLO", 1,
    {
        {"CELLO", 1, 5, 4, {
            { 3,  3, 24, 0, 20, 0,  8,  0,  2,  8,  0},  /* OP1: AR=20 느린 어택 */
            { 5,  1, 18, 0, 18, 0, 10,  0,  3,  8,  0},  /* OP2: AR=18 */
            { 3,  1, 30, 0, 18, 0, 12,  0,  4,  8,  0},  /* OP3 */
            { 0,  1,  7, 0, 20, 0,  6,  0,  1,  7,  0}}},/* OP4 car: SR=0 */
        {"", 7, 0, 3, {{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                       {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
        {"", 7, 0, 3, {{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                       {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}},
    },
    {0, 0, 0}, {0, 0, 0}, {14, 0, 0},
    0, 0, 0, 0,
    3, 1, 380,          /* lfo_vib=3, lfo_trem=1, lfo_hz100=380 */
    0x00,
    {SSG_SQUARE_BRIGHT, SSG_SQUARE_BRIGHT, SSG_SQUARE_BRIGHT},
    {0, 0, 0}, {0, 0, 0}
};

/* ── §9b  raw 패치 → full_inst_t 1채널 래핑 (모든 YM2203_PATCHES 항목) ──
 * fm_n=1, ssg_mask=0, LFO=0: 순수 FM 단일채널 음색
 * M4_YM 화면에서 use_full=1 경로로 인덱스 접근 가능 */
#define _FM1(pname, alg, fb, ar_op4)  { (pname), (alg), (fb), (ar_op4), \
    { {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}, \
      {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0} } }

static const full_inst_t FRAW_VGM_LEAD_A = { "VGM-LEAD-A",   1, {YM2203_PATCHES[PATCH_VGM_LEAD_A], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_LEAD_B = { "VGM-LEAD-B",   1, {YM2203_PATCHES[PATCH_VGM_LEAD_B], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_LEAD_C = { "VGM-LEAD-C",   1, {YM2203_PATCHES[PATCH_VGM_LEAD_C], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_LEAD_D = { "VGM-LEAD-D",   1, {YM2203_PATCHES[PATCH_VGM_LEAD_D], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_LEAD_E = { "VGM-LEAD-E",   1, {YM2203_PATCHES[PATCH_VGM_LEAD_E], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_BRASS = { "VGM-BRASS",    1, {YM2203_PATCHES[PATCH_VGM_BRASS],  _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_PAD_A = { "VGM-PAD-A",    1, {YM2203_PATCHES[PATCH_VGM_PAD_A],  _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_PAD_B = { "VGM-PAD-B",    1, {YM2203_PATCHES[PATCH_VGM_PAD_B],  _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_PAD_C = { "VGM-PAD-C",    1, {YM2203_PATCHES[PATCH_VGM_PAD_C],  _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_ORGAN_A = { "VGM-ORGAN-A",  1, {YM2203_PATCHES[PATCH_VGM_ORGAN_A],_FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_ORGAN_B = { "VGM-ORGAN-B",  1, {YM2203_PATCHES[PATCH_VGM_ORGAN_B],_FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_ORGAN_C = { "VGM-ORGAN-C",  1, {YM2203_PATCHES[PATCH_VGM_ORGAN_C],_FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_DUAL_A = { "VGM-DUAL-A",   1, {YM2203_PATCHES[PATCH_VGM_DUAL_A], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_DUAL_B = { "VGM-DUAL-B",   1, {YM2203_PATCHES[PATCH_VGM_DUAL_B], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_DRUM_A = { "VGM-DRUM-A",   1, {YM2203_PATCHES[PATCH_VGM_DRUM_A], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_DRUM_B = { "VGM-DRUM-B",   1, {YM2203_PATCHES[PATCH_VGM_DRUM_B], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_DRUM_C = { "VGM-DRUM-C",   1, {YM2203_PATCHES[PATCH_VGM_DRUM_C], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_BASS = { "VGM-BASS",     1, {YM2203_PATCHES[PATCH_VGM_BASS],   _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_PIANO = { "PIANO",        1, {YM2203_PATCHES[PATCH_PIANO],        _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_EPIANO = { "EPIANO",       1, {YM2203_PATCHES[PATCH_EPIANO],       _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_MUSICBOX = { "MUSICBOX",     1, {YM2203_PATCHES[PATCH_MUSICBOX],     _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_HARP = { "HARP",         1, {YM2203_PATCHES[PATCH_HARP],         _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_HARPSICHORD = { "HARPSICHORD",  1, {YM2203_PATCHES[PATCH_HARPSICHORD],  _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_CELESTA = { "CELESTA",      1, {YM2203_PATCHES[PATCH_CELESTA],      _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_XYLOPHONE = { "XYLOPHONE",    1, {YM2203_PATCHES[PATCH_XYLOPHONE],    _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_MARIMBA = { "MARIMBA",      1, {YM2203_PATCHES[PATCH_MARIMBA],      _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_BELL = { "BELL",         1, {YM2203_PATCHES[PATCH_BELL],         _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VIBRAPHONE = { "VIBRAPHONE",   1, {YM2203_PATCHES[PATCH_VIBRAPHONE],   _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VIOLIN = { "VIOLIN-R",     1, {YM2203_PATCHES[PATCH_VIOLIN],       _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,450,0,{0},{0},{0} };
static const full_inst_t FRAW_CELLO = { "CELLO-R",      1, {YM2203_PATCHES[PATCH_CELLO],        _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,380,0,{0},{0},{0} };
static const full_inst_t FRAW_STRINGS_ENS = { "STRINGS",      1, {YM2203_PATCHES[PATCH_STRINGS_ENS],  _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,420,0,{0},{0},{0} };
static const full_inst_t FRAW_AGITAR = { "ACOU-GTR",     1, {YM2203_PATCHES[PATCH_AGITAR],       _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_EGITAR_CLEAN = { "EGTR-CLEAN",   1, {YM2203_PATCHES[PATCH_EGITAR_CLEAN], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_EGITAR_DRIVE = { "EGTR-DRIVE",   1, {YM2203_PATCHES[PATCH_EGITAR_DRIVE], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_SHAMISEN = { "SHAMISEN",     1, {YM2203_PATCHES[PATCH_SHAMISEN],     _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_BANJO = { "BANJO",        1, {YM2203_PATCHES[PATCH_BANJO],        _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_KOTO = { "KOTO",         1, {YM2203_PATCHES[PATCH_KOTO],         _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_FLUTE = { "FLUTE",        1, {YM2203_PATCHES[PATCH_FLUTE],        _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,3,1,480,0,{0},{0},{0} };
static const full_inst_t FRAW_DAEGEUM = { "DAEGEUM-R",    1, {YM2203_PATCHES[PATCH_DAEGEUM],      _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,3,1,500,0,{0},{0},{0} };
static const full_inst_t FRAW_SOGEUM = { "SOGEUM-R",     1, {YM2203_PATCHES[PATCH_SOGEUM],       _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,3,1,480,0,{0},{0},{0} };
static const full_inst_t FRAW_OBOE = { "OBOE",         1, {YM2203_PATCHES[PATCH_OBOE],         _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,450,0,{0},{0},{0} };
static const full_inst_t FRAW_TRUMPET = { "TRUMPET",      1, {YM2203_PATCHES[PATCH_TRUMPET],      _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_SAXOPHONE = { "SAXOPHONE",    1, {YM2203_PATCHES[PATCH_SAXOPHONE],    _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_TROMBONE = { "TROMBONE",     1, {YM2203_PATCHES[PATCH_TROMBONE],     _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_BASS_ACOUSTIC = { "BASS-ACOU",    1, {YM2203_PATCHES[PATCH_BASS_ACOUSTIC],_FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_BASS_FRETLESS = { "BASS-FRET",    1, {YM2203_PATCHES[PATCH_BASS_FRETLESS],_FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_VGM_BASS2 = { "VGM-BASS2",    1, {YM2203_PATCHES[PATCH_VGM_BASS2],   _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_CHOIR = { "CHOIR",        1, {YM2203_PATCHES[PATCH_CHOIR],        _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,400,0,{0},{0},{0} };
static const full_inst_t FRAW_SYNTH_LEAD = { "SYNTH-LEAD",   1, {YM2203_PATCHES[PATCH_SYNTH_LEAD],   _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_SYNTH_PAD = { "SYNTH-PAD",    1, {YM2203_PATCHES[PATCH_SYNTH_PAD],    _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,300,0,{0},{0},{0} };
static const full_inst_t FRAW_SYNTH_BRASS = { "SYNTH-BRASS",  1, {YM2203_PATCHES[PATCH_SYNTH_BRASS],  _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_DRUM_KICK = { "DRUM-KICK",    1, {YM2203_PATCHES[PATCH_DRUM_KICK],    _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_DRUM_SNARE = { "DRUM-SNARE",   1, {YM2203_PATCHES[PATCH_DRUM_SNARE],   _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_DRUM_HI_TOM = { "DRUM-HI-TOM", 1, {YM2203_PATCHES[PATCH_DRUM_HI_TOM],  _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_DRUM_LO_TOM = { "DRUM-LO-TOM", 1, {YM2203_PATCHES[PATCH_DRUM_LO_TOM],  _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
/* ── [추가] 헤더 신규 패치 36종 래핑 (CLEAN/UNIQUE/HIFI/PLEASANT/PRESTIGE) ── */
static const full_inst_t FRAW_CLEAN_ROUND_LEAD = { "CLN-LEAD", 1, {YM2203_PATCHES[PATCH_CLEAN_ROUND_LEAD], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_SOFT_BRASS = { "CLN-BRASS", 1, {YM2203_PATCHES[PATCH_CLEAN_SOFT_BRASS], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_EP_WARM = { "CLN-EP", 1, {YM2203_PATCHES[PATCH_CLEAN_EP_WARM], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_FLUTE_AIR = { "CLN-FLUTE", 1, {YM2203_PATCHES[PATCH_CLEAN_FLUTE_AIR], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,3,1,480,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_STRINGS_PAD = { "CLN-STR", 1, {YM2203_PATCHES[PATCH_CLEAN_STRINGS_PAD], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,420,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_ORGAN_ROUND = { "CLN-ORGAN", 1, {YM2203_PATCHES[PATCH_CLEAN_ORGAN_ROUND], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_PLUCK_MELLOW = { "CLN-PLUCK", 1, {YM2203_PATCHES[PATCH_CLEAN_PLUCK_MELLOW], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_BASS_ROUND = { "CLN-BASS", 1, {YM2203_PATCHES[PATCH_CLEAN_BASS_ROUND], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_BELL_SOFT = { "CLN-BELL", 1, {YM2203_PATCHES[PATCH_CLEAN_BELL_SOFT], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_SYNTH_PAD = { "CLN-SYNPAD", 1, {YM2203_PATCHES[PATCH_CLEAN_SYNTH_PAD], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,300,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_GUITAR_SOFT = { "CLN-GTR", 1, {YM2203_PATCHES[PATCH_CLEAN_GUITAR_SOFT], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_CLEAN_CHOIR_SOFT = { "CLN-CHOIR", 1, {YM2203_PATCHES[PATCH_CLEAN_CHOIR_SOFT], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,400,0,{0},{0},{0} };
static const full_inst_t FRAW_UNIQUE_METAL_IMPACT = { "UNQ-METAL", 1, {YM2203_PATCHES[PATCH_UNIQUE_METAL_IMPACT], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_UNIQUE_ALIEN_LEAD = { "UNQ-ALIEN", 1, {YM2203_PATCHES[PATCH_UNIQUE_ALIEN_LEAD], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_UNIQUE_WOBBLE_BASS = { "UNQ-WOBBLE", 1, {YM2203_PATCHES[PATCH_UNIQUE_WOBBLE_BASS], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_UNIQUE_CRYSTAL_BELL = { "UNQ-CRYSTAL", 1, {YM2203_PATCHES[PATCH_UNIQUE_CRYSTAL_BELL], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_UNIQUE_GLASS_HARM = { "UNQ-GLASS", 1, {YM2203_PATCHES[PATCH_UNIQUE_GLASS_HARM], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_UNIQUE_CYBER_ORGAN = { "UNQ-CYBER", 1, {YM2203_PATCHES[PATCH_UNIQUE_CYBER_ORGAN], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_HIFI_PIANO_BRIGHT = { "HIFI-PIANO", 1, {YM2203_PATCHES[PATCH_HIFI_PIANO_BRIGHT], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_HIFI_STRINGS_WARM = { "HIFI-STR", 1, {YM2203_PATCHES[PATCH_HIFI_STRINGS_WARM], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,420,0,{0},{0},{0} };
static const full_inst_t FRAW_HIFI_FLUTE_PURE = { "HIFI-FLUTE", 1, {YM2203_PATCHES[PATCH_HIFI_FLUTE_PURE], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,3,1,480,0,{0},{0},{0} };
static const full_inst_t FRAW_HIFI_VIBES_MELLOW = { "HIFI-VIBES", 1, {YM2203_PATCHES[PATCH_HIFI_VIBES_MELLOW], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_HIFI_BRASS_FULL = { "HIFI-BRASS", 1, {YM2203_PATCHES[PATCH_HIFI_BRASS_FULL], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_HIFI_CELLO_DEEP = { "HIFI-CELLO", 1, {YM2203_PATCHES[PATCH_HIFI_CELLO_DEEP], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,380,0,{0},{0},{0} };
static const full_inst_t FRAW_PLEASANT_WARM_PAD = { "PLS-PAD", 1, {YM2203_PATCHES[PATCH_PLEASANT_WARM_PAD], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,350,0,{0},{0},{0} };
static const full_inst_t FRAW_PLEASANT_GUITAR_NYLON = { "PLS-GTR", 1, {YM2203_PATCHES[PATCH_PLEASANT_GUITAR_NYLON], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_PLEASANT_MARIMBA_SOFT = { "PLS-MARIMBA", 1, {YM2203_PATCHES[PATCH_PLEASANT_MARIMBA_SOFT], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_PLEASANT_CHOIR_LUSH = { "PLS-CHOIR", 1, {YM2203_PATCHES[PATCH_PLEASANT_CHOIR_LUSH], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,400,0,{0},{0},{0} };
static const full_inst_t FRAW_PLEASANT_EP_CLEAN = { "PLS-EP", 1, {YM2203_PATCHES[PATCH_PLEASANT_EP_CLEAN], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_PLEASANT_HARP_RICH = { "PLS-HARP", 1, {YM2203_PATCHES[PATCH_PLEASANT_HARP_RICH], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_PRESTIGE_GRAND_PIANO = { "PRS-PIANO", 1, {YM2203_PATCHES[PATCH_PRESTIGE_GRAND_PIANO], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_PRESTIGE_ORCH_STRINGS = { "PRS-STR", 1, {YM2203_PATCHES[PATCH_PRESTIGE_ORCH_STRINGS], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,450,0,{0},{0},{0} };
static const full_inst_t FRAW_PRESTIGE_FRENCH_HORN = { "PRS-HORN", 1, {YM2203_PATCHES[PATCH_PRESTIGE_FRENCH_HORN], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,400,0,{0},{0},{0} };
static const full_inst_t FRAW_PRESTIGE_KOTO_NOBLE = { "PRS-KOTO", 1, {YM2203_PATCHES[PATCH_PRESTIGE_KOTO_NOBLE], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
static const full_inst_t FRAW_PRESTIGE_CRYSTAL_PAD = { "PRS-CRYSTAL", 1, {YM2203_PATCHES[PATCH_PRESTIGE_CRYSTAL_PAD], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,2,1,350,0,{0},{0},{0} };
static const full_inst_t FRAW_PRESTIGE_HARPSICHORD = { "PRS-HARPSI", 1, {YM2203_PATCHES[PATCH_PRESTIGE_HARPSICHORD], _FM1("",7,0,3), _FM1("",7,0,3)}, {0},{0},{15},0,0,0,0,0,0,0,0,{0},{0},{0} };
#undef _FM1

static const full_inst_t* const FULL_INST_CATALOG[] = {
    /* ── 국악기 §8 ── */
    &FULL_DAEGEUM,          /*  0 */
    &FULL_SOGEUM,           /*  1 */
    &FULL_PIRI,             /*  2 */
    &FULL_GEOMUNGO,         /*  3 */
    &FULL_AJAENG,           /*  4 */
    &FULL_DANSO,            /*  5 */
    &FULL_HUN,              /*  6 */
    &FULL_GAYAGEUM,         /*  7 */
    &FULL_HAEGEUM,          /*  8 */
    /* ── §9 오케스트라 ── */
    &FULL_VIOLIN,           /*  9 */
    &FULL_CELLO,            /* 10 */
    /* ── §9b raw 패치 래핑 (VGM 계열) ── */
    &FRAW_VGM_LEAD_A,       /* 11 */
    &FRAW_VGM_LEAD_B,       /* 12 */
    &FRAW_VGM_LEAD_C,       /* 13 */
    &FRAW_VGM_LEAD_D,       /* 14 */
    &FRAW_VGM_LEAD_E,       /* 15 */
    &FRAW_VGM_BRASS,        /* 16 */
    &FRAW_VGM_PAD_A,        /* 17 */
    &FRAW_VGM_PAD_B,        /* 18 */
    &FRAW_VGM_PAD_C,        /* 19 */
    &FRAW_VGM_ORGAN_A,      /* 20 */
    &FRAW_VGM_ORGAN_B,      /* 21 */
    &FRAW_VGM_ORGAN_C,      /* 22 */
    &FRAW_VGM_DUAL_A,       /* 23 */
    &FRAW_VGM_DUAL_B,       /* 24 */
    &FRAW_VGM_DRUM_A,       /* 25 */
    &FRAW_VGM_DRUM_B,       /* 26 */
    &FRAW_VGM_DRUM_C,       /* 27 */
    &FRAW_VGM_BASS,         /* 28 */
    /* ── 건반 ── */
    &FRAW_PIANO,            /* 29 */
    &FRAW_EPIANO,           /* 30 */
    &FRAW_MUSICBOX,         /* 31 */
    &FRAW_HARP,             /* 32 */
    &FRAW_HARPSICHORD,      /* 33 */
    &FRAW_CELESTA,          /* 34 */
    &FRAW_XYLOPHONE,        /* 35 */
    &FRAW_MARIMBA,          /* 36 */
    &FRAW_BELL,             /* 37 */
    &FRAW_VIBRAPHONE,       /* 38 */
    /* ── 현악기 ── */
    &FRAW_VIOLIN,           /* 39 */
    &FRAW_CELLO,            /* 40 */
    &FRAW_STRINGS_ENS,      /* 41 */
    &FRAW_AGITAR,           /* 42 */
    &FRAW_EGITAR_CLEAN,     /* 43 */
    &FRAW_EGITAR_DRIVE,     /* 44 */
    &FRAW_SHAMISEN,         /* 45 */
    &FRAW_BANJO,            /* 46 */
    &FRAW_KOTO,             /* 47 */
    /* ── 관악기 ── */
    &FRAW_FLUTE,            /* 48 */
    &FRAW_DAEGEUM,          /* 49 */
    &FRAW_SOGEUM,           /* 50 */
    &FRAW_OBOE,             /* 51 */
    &FRAW_TRUMPET,          /* 52 */
    &FRAW_SAXOPHONE,        /* 53 */
    &FRAW_TROMBONE,         /* 54 */
    /* ── 베이스 ── */
    &FRAW_BASS_ACOUSTIC,    /* 55 */
    &FRAW_BASS_FRETLESS,    /* 56 */
    &FRAW_VGM_BASS2,        /* 57 */
    /* ── 보컬 / 패드 ── */
    &FRAW_CHOIR,            /* 58 */
    &FRAW_SYNTH_LEAD,       /* 59 */
    &FRAW_SYNTH_PAD,        /* 60 */
    &FRAW_SYNTH_BRASS,      /* 61 */
    /* ── 드럼 FM ── */
    &FRAW_DRUM_KICK,        /* 62 */
    &FRAW_DRUM_SNARE,       /* 63 */
    &FRAW_DRUM_HI_TOM,      /* 64 */
    &FRAW_DRUM_LO_TOM,      /* 65 */
    /* ── [추가] 헤더 신규 패치 36종 (66~101) ── */
    &FRAW_CLEAN_ROUND_LEAD,
    &FRAW_CLEAN_SOFT_BRASS,
    &FRAW_CLEAN_EP_WARM,
    &FRAW_CLEAN_FLUTE_AIR,
    &FRAW_CLEAN_STRINGS_PAD,
    &FRAW_CLEAN_ORGAN_ROUND,
    &FRAW_CLEAN_PLUCK_MELLOW,
    &FRAW_CLEAN_BASS_ROUND,
    &FRAW_CLEAN_BELL_SOFT,
    &FRAW_CLEAN_SYNTH_PAD,
    &FRAW_CLEAN_GUITAR_SOFT,
    &FRAW_CLEAN_CHOIR_SOFT,
    &FRAW_UNIQUE_METAL_IMPACT,
    &FRAW_UNIQUE_ALIEN_LEAD,
    &FRAW_UNIQUE_WOBBLE_BASS,
    &FRAW_UNIQUE_CRYSTAL_BELL,
    &FRAW_UNIQUE_GLASS_HARM,
    &FRAW_UNIQUE_CYBER_ORGAN,
    &FRAW_HIFI_PIANO_BRIGHT,
    &FRAW_HIFI_STRINGS_WARM,
    &FRAW_HIFI_FLUTE_PURE,
    &FRAW_HIFI_VIBES_MELLOW,
    &FRAW_HIFI_BRASS_FULL,
    &FRAW_HIFI_CELLO_DEEP,
    &FRAW_PLEASANT_WARM_PAD,
    &FRAW_PLEASANT_GUITAR_NYLON,
    &FRAW_PLEASANT_MARIMBA_SOFT,
    &FRAW_PLEASANT_CHOIR_LUSH,
    &FRAW_PLEASANT_EP_CLEAN,
    &FRAW_PLEASANT_HARP_RICH,
    &FRAW_PRESTIGE_GRAND_PIANO,
    &FRAW_PRESTIGE_ORCH_STRINGS,
    &FRAW_PRESTIGE_FRENCH_HORN,
    &FRAW_PRESTIGE_KOTO_NOBLE,
    &FRAW_PRESTIGE_CRYSTAL_PAD,
    &FRAW_PRESTIGE_HARPSICHORD,
};
#define FULL_INST_COUNT  ((uint8_t)(sizeof(FULL_INST_CATALOG)/sizeof(FULL_INST_CATALOG[0])))
#define RAW_PATCH_COUNT  ((uint8_t)(YM2203_PATCH_COUNT))



static void harmony_runtime_apply(void)
{
    harmony_cfg_t* h = &g_full_preset.harmony;
    if (h->ji_on || h->key_detect_on) vm_init_ji(&g_voice_mgr);
    else vm_init(&g_voice_mgr);
    g_voice_mgr.use_key_det = (uint8_t)((h->ji_on || h->key_detect_on) ? 1u : 0u);
    g_voice_mgr.key_det.min_notes = h->kd_min_notes ? h->kd_min_notes : 3u;

    mp_init(&g_midi_perf,
        h->vel_car_ratio ? h->vel_car_ratio : 6u,
        h->vel_mod_ratio ? h->vel_mod_ratio : 2u,
        h->pb_semitones ? h->pb_semitones : 2u,
        0u, 2u, 7u);

    if (h->nlfo_preset < 5u) nlfo_init(&g_nlfo, (nlfo_preset_t)h->nlfo_preset);
    memset(&g_cp, 0, sizeof(g_cp));
}

static void full_preset_default(void)
{
    memset(&g_full_preset, 0, sizeof(g_full_preset));
    snprintf(g_full_preset.preset_name, sizeof(g_full_preset.preset_name), "DEFAULT");
    g_full_preset.ch_mode = CH_MODE_INDEPENDENT;

    for (uint8_t ch = 0; ch < 3; ch++) {
        fm_ch_cfg_t* fc = &g_full_preset.fm[ch];
        /* [FIX-DEFAULT] pot.c 방식: 채널별로 다른 악기 (대금/소금/피리) */
        fc->inst_idx = (ch < FULL_INST_COUNT) ? ch : 0;
        fc->use_full = 1;
        fc->use_dual = 0;
        fc->use_triple = 0;
        fc->edited = 0;
        fc->enable = 1;       /* 기본: 채널 ON */
        fc->dual_patch_ptr = NULL;
        fc->triple_patch_ptr = NULL;
        uint8_t idx = fc->inst_idx;
        if (idx < FULL_INST_COUNT && FULL_INST_CATALOG[idx]->fm_n > 0) {
            /* [FIX-DEFAULT-VOL] fc->vol = fi->fm_vol[0] (instrument native volume).
             * Previous fc->vol=15 fixed caused TL+5 attenuation on all instruments. */
            fc->vol = FULL_INST_CATALOG[idx]->fm_vol[0];
            fc->patch_override = FULL_INST_CATALOG[idx]->fm[0];
        }
        else if (FULL_INST_COUNT > 0 && FULL_INST_CATALOG[0]->fm_n > 0) {
            fc->vol = FULL_INST_CATALOG[0]->fm_vol[0];
            fc->patch_override = FULL_INST_CATALOG[0]->fm[0];
        }
        else {
            fc->vol = 15;
        }
    }

    for (uint8_t ch = 0; ch < 3; ch++) {
        g_full_preset.psg[ch].enable = 0;
        g_full_preset.psg[ch].patch_idx = 0;
        g_full_preset.psg[ch].amp = 8;
        g_full_preset.psg[ch].note_offset = 0;
    }

    g_full_preset.harmony.voice_mgr_on = 0;
    g_full_preset.harmony.vm_chord_auto = 0;
    g_full_preset.harmony.ji_on = 0;
    g_full_preset.harmony.pb_semitones = 2;
    g_full_preset.harmony.key_detect_on = 0;
    g_full_preset.harmony.kd_min_notes = 3;
    g_full_preset.harmony.key_root = 0;
    g_full_preset.harmony.scale_type = 0;
    g_full_preset.harmony.counterpoint_on = 0;
    g_full_preset.harmony.suspension_on = 0;
    g_full_preset.harmony.vel_car_ratio = 6;
    g_full_preset.harmony.vel_mod_ratio = 2;
    g_full_preset.harmony.swing_ratio = 0;
    g_full_preset.harmony.nlfo_preset = 5; /* OFF */
    g_full_preset.harmony.nlfo_rand_range = 8;
    g_full_preset.harmony.porta_mode = 0;
    g_full_preset.harmony.porta_ticks = 0;

    g_full_preset.pre_eq_preset = 0;   /* PRE EQ 인덱스 0 = flat 계수, HW bypass 아님 */
    g_full_preset.pre_eq_custom_valid = 0u;
    g_full_preset.eq8_enabled = 0u;       /* default: PRE/POST 4band individual mode */
    g_full_preset.eq8_preset = 0u;         /* EQ8_BYPASS */
    g_full_preset.post_eq_custom_valid = 0u;
    g_full_preset.vca_preset = 1;      /* VCA 인덱스 1 = SOFT 2:1, bypass 해제 운용 */
    g_full_preset.dly_preset = 1;
    g_full_preset.lfo_preset = 0;
    /* DELAY 세부값 초기화 ? 0 = preset 기본값 사용 */
    g_full_preset.dly_custom_valid = 0u;
    g_full_preset.dly_time_ms = 0.0f;
    g_full_preset.dly_fb_pct = 0.0f;
    g_full_preset.dly_dw_pct = 0.0f;
    g_full_preset.dly_damp_a_pct = 0.0f;
    g_full_preset.dly_damp_hf_pct = 0.0f;
    g_full_preset.dly_sat_thresh = 0u;
    g_full_preset.dly_wow_depth = 0u;
    g_full_preset.dly_diff_mix_pct = 0.0f;
    g_full_preset.dly_diff_g_pct = 0.0f;
    g_full_preset.dly_ms_pct = 0.0f;
    g_full_preset.dly_slew_raw = 0u;
    g_full_preset.dly_tap1_ms = 0.0f;
    g_full_preset.dly_tap1_gain = 0.0f;
    g_full_preset.dly_tap2_ms = 0.0f;
    g_full_preset.dly_tap2_gain = 0.0f;
    g_full_preset.rev_preset = FDN_SP_HALL; /* 리버브 기본값 = Hall */
    /* reverb 세부값 초기화 ? 0 = "preset 기본값 사용" sentinel */
    g_full_preset.rev_wet_q24 = 0u;
    g_full_preset.rev_dry_q24 = 0u;
    g_full_preset.rev_rt60_ms = 0u;
    g_full_preset.rev_decay_q24 = 0u;
    g_full_preset.rev_width_q24 = 0u;
    g_full_preset.rev_er_q24 = 0u;
    g_full_preset.rev_shimmer_q24 = 0u;
    g_full_preset.rev_custom_valid = 0u;
    g_full_preset.warm_preset = 0;   /* WARM 인덱스 0 = MONITOR (asym=0, 투명) */
    g_full_preset.svf_preset = SVF_BODY_DEFAULT; /* SVF  = 기본 나무 관악기    */
    g_full_preset.post_eq_preset = 0; /* POST_EQ = flat 계수, Stage3에서는 HW bypass 해제 후 운용 */
    g_full_preset.created_at = (uint32_t)time(NULL);
}

/* ???????????????????????????????????????????????????????????????????????????
   [V16 수정 3/6] multi.h 타입 사용 준비

   dual_patch_t, triple_patch_t 타입이 이제 사용 가능

   모드 4 UI에서 원하는 dual/triple 패치를 선택하면,
   full_preset_apply_hw()와 m3_full_note_on()이 자동으로 적용합니다.

   // 모드 4 UI에서
   g_full_preset.fm[0].dual_patch_ptr = &DUAL_PIANO;  // 사용자 선택
   g_full_preset.fm[0].use_dual = 1;
   full_preset_apply_hw();  // APPLY NOW

   // 자동으로 FM 0+1에 DUAL_PIANO 적용됨

    사용 가능한 악기 목록 (ym2203_multi.h 참조):

   [Dual 패치 - 8OP]
   - DUAL_PIANO, DUAL_EPIANO, DUAL_STRINGS
   - DUAL_BRASS, DUAL_ORGAN, DUAL_CHOIR
   - DUAL_OVERDRIVE, DUAL_BASS, DUAL_PERC

   [Triple 패치 - 12OP]
   - TRIPLE_ORCH_STRINGS, TRIPLE_PIPE_ORGAN
   - TRIPLE_RICH_LEAD, TRIPLE_DEEP_BASS
   - TRIPLE_DRUM_KIT, TRIPLE_SYNTH_TEX
   ??????????????????????????????????????????????????????????????????????????? */

   /* ???????????????????????????????????????????????????????????????????????????
   full_preset_apply_hw() 함수 - 채널 모드별 HW 적용

   - 기존: 무조건 3채널 독립 적용
   - 변경: g_full_preset.ch_mode에 따라 분기

   1. CH_MODE_INDEPENDENT (기존 로직 유지)
      - FM 0, 1, 2 각각 독립적으로 패치 적용
      - 3개 채널 모두 다른 악기 가능

   2. CH_MODE_DUAL (새로 추가)
      - fc[0].use_dual==1이면: fc[0].dual_patch_ptr에서 8OP 패치 적용
      - FM 0에 dual_patch->ch_a 적용
      - FM 1에 dual_patch->ch_b 적용
      - FM 2는 독립 (보조 채널)

   3. CH_MODE_TRIPLE (새로 추가)
      - fc[0].use_triple==1이면: fc[0].triple_patch_ptr에서 12OP 패치 적용
      - FM 0에 triple_patch->ch_a 적용
      - FM 1에 triple_patch->ch_b 적용
      - FM 2에 triple_patch->ch_c 적용

   - 모드 4에서 APPLY NOW 버튼 클릭 시
   - 모드 전환 시 (enter 함수에서)
   ??????????????????????????????????????????????????????????????????????????? */
static void full_preset_apply_hw(void)
{
    if (!p_ym) return;
    full_preset_sync_combo_patch(&g_full_preset);
    ym2203_cache_reset();

    printf("[V16] Applying ch_mode=%d (%s)\n",
        g_full_preset.ch_mode, ch_mode_name(g_full_preset.ch_mode));

    switch (g_full_preset.ch_mode) {

        /* ─────────────────────────────────────────────────────────────────────
           CH_MODE_INDEPENDENT: 각 채널 독립 (3 x 4OP) - 기존 로직
           ───────────────────────────────────────────────────────────────────── */
    case CH_MODE_INDEPENDENT:
        for (uint8_t ch = 0; ch < 3; ch++) {
            fm_ch_cfg_t* fc = &g_full_preset.fm[ch];
            ym2203_key_off(ch);

            /* enable=0: 채널 소거 (TL=127 무음 패치 적용 후 skip) */
            if (!fc->enable) {
                static const ym2203_patch_t fm_silent = {
                    "MUTE",7,0,0,
                    {{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                     {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}
                };
                ym2203_patch_apply(&fm_silent, ch);
                continue;
            }

            const ym2203_patch_t* p = NULL;

            if (fc->edited) {
                /* 편집된 패치 사용 */
                p = &fc->patch_override;
            }
            else if (fc->use_full && fc->inst_idx < FULL_INST_COUNT) {
                /* full_inst_t 사용 */
                const full_inst_t* fi = FULL_INST_CATALOG[fc->inst_idx];
                if (fi->fm_n > 0) p = &fi->fm[0];
            }
            else if (!fc->use_full && fc->inst_idx < YM2203_PATCH_COUNT) {
                /* raw patch 사용 */
                p = &YM2203_PATCHES[fc->inst_idx];
            }

            if (p) ym2203_patch_apply(p, ch);
        }
        break;

        /* ─────────────────────────────────────────────────────────────────────
           CH_MODE_DUAL: FM 0+1 = 8OP, FM 2 보조
           ───────────────────────────────────────────────────────────────────── */
    case CH_MODE_DUAL:
    {
        fm_ch_cfg_t* fc0 = &g_full_preset.fm[0];

        /* FM 0, 1에 Dual 패치 적용 */
        if (fc0->use_dual && fc0->dual_patch_ptr != NULL) {
            const dual_patch_t* dp = fc0->dual_patch_ptr;

            ym2203_key_off(0);
            ym2203_key_off(1);

            ym2203_patch_apply(&dp->ch_a, 0);  /* FM 0에 ch_a (4OP) */
            ym2203_patch_apply(&dp->ch_b, 1);  /* FM 1에 ch_b (4OP) */

            printf("[V16-DUAL] Applied: %s\n", dp->name);
        }
        else {
            /* Dual 플래그가 없으면 일반 패치 */
            for (uint8_t ch = 0; ch < 2; ch++) {
                fm_ch_cfg_t* fc = &g_full_preset.fm[ch];
                const ym2203_patch_t* p = fc->edited ? &fc->patch_override :
                    (fc->use_full && fc->inst_idx < FULL_INST_COUNT ?
                        &FULL_INST_CATALOG[fc->inst_idx]->fm[0] :
                        &YM2203_PATCHES[fc->inst_idx]);
                ym2203_key_off(ch);
                if (p) ym2203_patch_apply(p, ch);
            }
        }

        /* FM 2는 보조 (독립) */
        {
            fm_ch_cfg_t* fc2 = &g_full_preset.fm[2];
            const ym2203_patch_t* p = fc2->edited ? &fc2->patch_override :
                (fc2->use_full && fc2->inst_idx < FULL_INST_COUNT ?
                    &FULL_INST_CATALOG[fc2->inst_idx]->fm[0] :
                    &YM2203_PATCHES[fc2->inst_idx]);
            ym2203_key_off(2);
            if (p) ym2203_patch_apply(p, 2);
        }
        break;
    }

    /* ─────────────────────────────────────────────────────────────────────
       CH_MODE_TRIPLE: FM 0+1+2 = 12OP
       ───────────────────────────────────────────────────────────────────── */
    case CH_MODE_TRIPLE:
    {
        fm_ch_cfg_t* fc0 = &g_full_preset.fm[0];

        if (fc0->use_triple && fc0->triple_patch_ptr != NULL) {
            const triple_patch_t* tp = fc0->triple_patch_ptr;

            ym2203_key_off(0);
            ym2203_key_off(1);
            ym2203_key_off(2);

            ym2203_patch_apply(&tp->ch[0], 0);  /* FM 0에 ch_a (4OP) */
            ym2203_patch_apply(&tp->ch[1], 1);  /* FM 1에 ch_b (4OP) */
            ym2203_patch_apply(&tp->ch[2], 2);  /* FM 2에 ch_c (4OP) */

            printf("[V16-TRIPLE] Applied: %s\n", tp->name);
        }
        else {
            /* Triple 플래그가 없으면 일반 패치 */
            for (uint8_t ch = 0; ch < 3; ch++) {
                fm_ch_cfg_t* fc = &g_full_preset.fm[ch];
                const ym2203_patch_t* p = fc->edited ? &fc->patch_override :
                    (fc->use_full && fc->inst_idx < FULL_INST_COUNT ?
                        &FULL_INST_CATALOG[fc->inst_idx]->fm[0] :
                        &YM2203_PATCHES[fc->inst_idx]);
                ym2203_key_off(ch);
                if (p) ym2203_patch_apply(p, ch);
            }
        }
        break;
    }

    /* ── CH_MODE_VGM6: FULL6_VGM_CATALOG 6ch 프리셋 ────────────────────────
     * fi_note_on_v() 내부에서 FM+SSG 동시 처리하므로 apply 단계에서는
     * FM 패치만 로드하고 SSG mixer는 ssg_mask 기반으로 직접 설정한다.
     * PSG 공통 블록(아래)은 VGM6에서 건너뜀(goto skip_psg). */
    case CH_MODE_VGM6:
    {
        if (g_full_preset.vgm6_inst_idx >= FULL6_VGM_CATALOG_COUNT)
            g_full_preset.vgm6_inst_idx = 0;
        const full_inst_t* fi = FULL6_VGM_CATALOG[g_full_preset.vgm6_inst_idx];
        uint8_t n = fi->fm_n; if (n > 3u) n = 3u;

        for (uint8_t ch = 0; ch < n; ch++) {
            ym2203_key_off(ch);
            ym2203_patch_apply(&fi->fm[ch], ch);
        }

        /* SSG mixer: ssg_mask 비트 기반으로 직접 설정 */
        {
            uint8_t mixer = 0x3Fu;  /* 전체 닫기 */
            for (int ch = 0; ch < 3; ch++) {
                if (!(fi->ssg_mask & (uint8_t)(1u << ch))) continue;
                mixer &= (uint8_t)~(1u << ch);
                mixer &= (uint8_t)~(1u << (ch + 3));
            }
            ym_write(YM_SSG_MIXER, mixer);
        }

        printf("[VGM6] Applied preset #%u: %s (fm=%u ssg_mask=0x%02X)\n",
            g_full_preset.vgm6_inst_idx, fi->name, fi->fm_n, fi->ssg_mask);
        goto skip_psg;  /* PSG 공통 블록 건너뜀 */
    }

    }  /* end switch(ch_mode) */

    /* PSG 적용 (모든 모드 공통) */
    {
        uint8_t mixer = 0x3Fu;  /* 기본: 모든 SSG 채널 닫기 (tone+noise off) */
        for (uint8_t ch = 0; ch < 3; ch++) {
            psg_ch_cfg_t* pc = &g_full_preset.psg[ch];
            if (!pc->enable) {
                ym2203_ssg_set_vol(ch, 0);
                /* [FIX-PSG] 비활성 채널은 mixer 비트 유지 (close) */
                continue;
            }
            if (pc->patch_idx < SSG_COUNT) {
                ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx],
                    (uint8_t)(1u << ch));
                ym2203_ssg_set_vol(ch, pc->amp);
                /* [FIX-PSG] 활성 채널: tone open (bit 0~2 = ch0~2 tone disable) */
                mixer &= (uint8_t)~(1u << ch);        /* tone enable */
                mixer &= (uint8_t)~(1u << (ch + 3u)); /* noise enable */
            }
        }
        /* [FIX-PSG] SSG Mixer 레지스터 실제 기록 ? 볼륨 0만으론 tone 게이트 안 닫힘 */
        if (p_ym) ym_write(YM_SSG_MIXER, mixer);
    }

skip_psg:
    harmony_runtime_apply();

    /* [LIGHT-CHAIN]
     * 요청 체인: YM2203 -> PRE EQ -> VCA -> POST EQ -> DELAY
     * WARM/SVF/REVERB/LFO1은 제외하고, 실제 남은 블록만 순서대로 적용한다.
     */
    if (DIAG_USE_PRE_EQ)  apply_pre_eq_preset(g_full_preset.pre_eq_preset);
    else                  diag_bypass_pre_eq("full_preset_apply_hw");

    if (DIAG_USE_VCA)     apply_vca_preset(g_full_preset.vca_preset);
    else                  diag_bypass_vca("full_preset_apply_hw");

    if (DIAG_USE_POST_EQ) apply_post_eq_preset(g_full_preset.post_eq_preset);
    else                  diag_bypass_post_eq("full_preset_apply_hw");

    /* Mode4에서 직접 편집한 PRE/POST EQ band 파라미터가 있으면
     * preset apply 이후에 다시 flush하여 Mode3에서도 동일 값으로 동작시킨다. */
    if (DIAG_USE_PRE_EQ && g_full_preset.pre_eq_custom_valid) {
        eq_band_flush();
        printf("[PRE_EQ] custom band parameters restored in full_preset_apply_hw\n");
    }
    if (DIAG_USE_POST_EQ && g_full_preset.post_eq_custom_valid) {
        eq_band_flush_post();
        printf("[POST_EQ] custom band parameters restored in full_preset_apply_hw\n");
    }

    /* [EQ8] 8band unified preset overrides individual PRE/POST */
    if (g_full_preset.eq8_enabled && DIAG_USE_PRE_EQ && DIAG_USE_POST_EQ && p_pre_eq && p_post_eq) {
        apply_eq8_idx(p_pre_eq, p_post_eq, g_full_preset.eq8_preset);
        printf("[EQ8] full_preset_apply_hw idx=%u %s\n",
            (unsigned)g_full_preset.eq8_preset,
            EQ8_PRESET_NAMES[g_full_preset.eq8_preset]);
    }

    /* ── LFO 먼저 → 딜레이 bypass 해제: apply_lfo_then_delay() ─────────
     * RTL bypass OFF 순간 BRAM 출력이 믹스에 합류.
     * LFO(APF g 변조)가 그 시점에 안정돼 있어야 자글자글 잡음 없음. */
    apply_lfo_then_delay();

    diag_apply_chain_policy("full_preset_apply_hw_end");
    printf("[DIAG-%s] HW apply complete\n", diag_chain_stage_name());
}



/* ── 보이스 상태 ── */
static uint8_t s_m3_rr = 0;
static uint8_t s_m3_note_ch[128];
static uint8_t s_m3_ch_note[3];

static void m3_full_voice_reset(void)
{
    /* [FIX-VR1] 보이스 채널 매핑 완전 초기화 (0xFF = 미할당) */
    memset(s_m3_note_ch, 0xFF, sizeof(s_m3_note_ch));
    memset(s_m3_ch_note, 0xFF, sizeof(s_m3_ch_note));
    s_m3_rr = 0;
    g_nlfo_note = 0xFF;

    /* [FIX-VR2] YM 채널 전체 Key-Off + SSG Mixer 닫기 ? 잔류음 방지 */
    if (p_ym) {
        for (int ch = 0; ch < 3; ch++) ym2203_key_off((uint8_t)ch);
        for (int ch = 0; ch < 3; ch++) ym2203_ssg_set_vol((uint8_t)ch, 0);
        ym_write(YM_SSG_MIXER, 0x3Fu);  /* tone/noise 모두 close */
    }

    if (g_full_preset.harmony.voice_mgr_on)
        vm_panic_off(&g_voice_mgr);
}

/* ── Note-On 발음 경로 선택 ── */
/* ???????????????????????????????????????????????????????????????????????????
   [V16 수정 5/6] m3_full_note_on() 함수 - 채널 모드별 발음

   - 기존: 라운드로빈으로 1개 채널에만 발음
   - 변경: ch_mode에 따라 발음 채널 수 결정

   1. CH_MODE_INDEPENDENT
      - 3개 채널 모두에 동시 발음 (폴리포닉)
      - 각 채널이 독립적으로 울림

   2. CH_MODE_DUAL
      - FM 0+1에만 동시 발음 (8OP 유니즌)
      - FM 2는 발음 안 함 (보조 채널)

   3. CH_MODE_TRIPLE
      - FM 0+1+2 모두에 동시 발음 (12OP 유니즌)

   Voice Manager 모드:
   - harmony.voice_mgr_on==1이면 기존 로직 유지 (ch_mode 무시)
   - voice_mgr가 자동으로 채널 할당

   호출 시점:
   - 모드 3 (MIDI KEYBOARD)에서 MIDI Note On 수신 시
   - 모드 1 (SYNTH)에서 건반 입력 시
   ??????????????????????????????????????????????????????????????????????????? */
static void m3_full_note_on(uint8_t note, uint8_t vel)
{
    if (note > 127) return;

    full_preset_sync_combo_patch(&g_full_preset);
    harmony_cfg_t* h = &g_full_preset.harmony;

    /* 농음 추적 */
    g_nlfo_note = note;
    if (h->nlfo_preset < NLFO_PRESET_COUNT)
        nlfo_init(&g_nlfo, (nlfo_preset_t)h->nlfo_preset);

    /* ─────────────────────────────────────────────────────────────────────
       Voice Manager 모드 (기존 로직 유지)
       ─────────────────────────────────────────────────────────────────── */
    if (h->voice_mgr_on) {
        fm_ch_cfg_t* fc = &g_full_preset.fm[0];
        const ym2203_patch_t* p = fc->edited ? &fc->patch_override :
            (fc->use_full && fc->inst_idx < FULL_INST_COUNT ?
                &FULL_INST_CATALOG[fc->inst_idx]->fm[0] :
                &YM2203_PATCHES[fc->inst_idx]);
        vm_note_on(&g_voice_mgr, note, vel, p);

        if (h->ji_on && g_voice_mgr.key_det.valid) {
            for (int i = 0; i < VM_FM_CH; i++) {
                if (g_voice_mgr.voices[i].on && g_voice_mgr.voices[i].note == note) {
                    kd_ji_note_ex((uint8_t)i, note, &g_voice_mgr.key_det, g_midi_perf.pb_cent);
                    break;
                }
            }
        }
        s_m3_note_ch[note] = 0;
        return;
    }

    /* ── pot.c 방식 채널 모드별 발음 ─────────────────────────────────────── */
    switch (g_full_preset.ch_mode) {

        /* ── INDEPENDENT: pot.c 경로 B 완전 동일 ─────────────────────────────
         *  3채널 동시 유니즌. key_off→patch_apply→vol→set_note→key_on 순서 고정.
         *  ym_write_force_cached(0x27, 0x00): LFO 채널 모드 OFF (음정 안정).
         *  PSG는 ch==0 에서만 ssg_mask 처리 (pot.c 동일). */
    case CH_MODE_INDEPENDENT:
    {
        ym_write_force_cached(0x27, 0x00);
        for (uint8_t ch = 0; ch < 3; ch++) {
            fm_ch_cfg_t* fc = &g_full_preset.fm[ch];

            /* enable=0: 이 채널 발음 skip */
            if (!fc->enable) continue;

            /* [FIX-M3-PATCH] full_preset_apply_hw()와 동일한 패치 선택 로직.
             * use_full=0(raw patch) 채널이 무조건 FULL_INST_CATALOG로 빠지던 버그 수정.
             * fm_n=0(SSG 전용) 악기에 garbage FM 패치 적용되던 버그 수정. */
            const ym2203_patch_t* p = NULL;
            if (fc->edited) {
                p = &fc->patch_override;
            }
            else if (fc->use_full && fc->inst_idx < FULL_INST_COUNT) {
                const full_inst_t* fi_p = FULL_INST_CATALOG[fc->inst_idx];
                if (fi_p->fm_n > 0) p = &fi_p->fm[0];
            }
            else if (!fc->use_full && fc->inst_idx < YM2203_PATCH_COUNT) {
                p = &YM2203_PATCHES[fc->inst_idx];
            }
            if (!p) continue;  /* fm_n=0 SSG 전용 악기: FM key_on 스킵 */

            ym2203_key_off(ch);
            ym2203_patch_apply(p, ch);

            if (h->vel_car_ratio > 0 || h->vel_mod_ratio > 0)
                ym_patch_vol_split(p, ch, vel, fc->vol,
                    h->vel_car_ratio, h->vel_mod_ratio);
            else
                ym_patch_vol(p, ch, vel, fc->vol);

            /* [FIX-M3-NOTE] use_full: fm_det/fm_noff/swell TL applied via ym_set_note_ex */
            if (fc->use_full && !fc->edited && fc->inst_idx < FULL_INST_COUNT) {
                const full_inst_t* fi_n = FULL_INST_CATALOG[fc->inst_idx];
                uint8_t ci = (ch < fi_n->fm_n) ? ch : 0;
                if (ch == 0 && fi_n->sw_ticks > 0u)
                    ym_write((uint8_t)(0x40u + YM_OP_OFF[fi_n->sw_op]), fi_n->sw_tl0 & 0x7Fu);
                ym_set_note_ex(ch, note, fi_n->fm_det[ci], fi_n->fm_noff[ci]);
            }
            else {
                ym2203_set_note(ch, note);
            }
            ym2203_key_on(ch, 0x0Fu);

            /* [FIX-M3-SSG] use_full: ym2203_ssg_note_on() for mixer+envelope+attack_boost.
             * Old ssg_set_vol-only path left mixer gate closed -> no SSG sound. */
            if (ch == 0 && fc->use_full && !fc->edited && fc->inst_idx < FULL_INST_COUNT) {
                const full_inst_t* fi_s = FULL_INST_CATALOG[fc->inst_idx];
                uint8_t has_noise = 0u;
                for (int pch = 0; pch < 3; pch++) {
                    if (!(fi_s->ssg_mask & (1u << pch))) continue;
                    psg_ch_cfg_t* pc = &g_full_preset.psg[pch];
                    if (!pc->enable) { ym2203_ssg_set_vol((uint8_t)pch, 0); continue; }
                    int16_t ns = (int16_t)note + (int16_t)fi_s->ssg_noff[pch];
                    if (ns < 0) ns = 0; if (ns > 127) ns = 127;
                    uint8_t ssg_vel = (uint8_t)((uint32_t)fi_s->ssg_amp[pch]
                        * (uint32_t)ym_ssg_vol_from_vel(vel) / 15u);
                    ym2203_ssg_note_on(&YM2203_SSG_PATCHES[fi_s->ssg_p[pch]],
                        (uint8_t)pch, (uint8_t)ns, ssg_vel);
                    if (fi_s->ssg_p[pch] == SSG_NOISE_SNARE ||
                        fi_s->ssg_p[pch] == SSG_NOISE_HIHAT ||
                        fi_s->ssg_p[pch] == SSG_NOISE_KICK ||
                        fi_s->ssg_p[pch] == SSG_NOISE_CRASH) has_noise = 1u;
                }
                if (has_noise) ym_ssg_noise_freq_vel(vel, 20u, 6u);
            }

            s_m3_ch_note[ch] = note;

            if (h->ji_on && h->key_detect_on) {
                if (ch == 0) kd_note_on(&g_voice_mgr.key_det, note, vel);
                if (g_voice_mgr.key_det.valid)
                    kd_ji_note_ex(ch, note, &g_voice_mgr.key_det, g_midi_perf.pb_cent);
            }
        }
        s_m3_note_ch[note] = 0;
        break;
    }

    /* ── DUAL: FM 0+1 동시 (8OP 유니즌) ──────────────────────────────── */
    case CH_MODE_DUAL:
    {
        ym_write_force_cached(0x27, 0x00);
        for (uint8_t ch = 0; ch < 2; ch++) {
            fm_ch_cfg_t* fc = &g_full_preset.fm[ch];
            /* [FIX-M3-PATCH-DUAL] fm_n=0 SSG 전용 악기 가드 */
            const ym2203_patch_t* p = NULL;
            if (fc->edited) {
                p = &fc->patch_override;
            }
            else if (fc->use_full && fc->inst_idx < FULL_INST_COUNT) {
                const full_inst_t* fi_p = FULL_INST_CATALOG[fc->inst_idx];
                if (fi_p->fm_n > 0) p = &fi_p->fm[0];
            }
            else if (!fc->use_full && fc->inst_idx < YM2203_PATCH_COUNT) {
                p = &YM2203_PATCHES[fc->inst_idx];
            }
            if (!p) continue;
            ym2203_key_off(ch);
            ym2203_patch_apply(p, ch);
            if (h->vel_car_ratio > 0 || h->vel_mod_ratio > 0)
                ym_patch_vol_split(p, ch, vel, fc->vol, h->vel_car_ratio, h->vel_mod_ratio);
            else
                ym_patch_vol(p, ch, vel, fc->vol);
            ym2203_set_note(ch, note);
            ym2203_key_on(ch, 0x0Fu);
            s_m3_ch_note[ch] = note;
        }
        s_m3_note_ch[note] = 0;
        break;
    }

    /* ── TRIPLE: FM 0+1+2 동시 (12OP 유니즌) ─────────────────────────── */
    case CH_MODE_TRIPLE:
    {
        ym_write_force_cached(0x27, 0x00);
        for (uint8_t ch = 0; ch < 3; ch++) {
            fm_ch_cfg_t* fc = &g_full_preset.fm[ch];
            /* [FIX-M3-PATCH-TRIPLE] fm_n=0 SSG 전용 악기 가드 */
            const ym2203_patch_t* p = NULL;
            if (fc->edited) {
                p = &fc->patch_override;
            }
            else if (fc->use_full && fc->inst_idx < FULL_INST_COUNT) {
                const full_inst_t* fi_p = FULL_INST_CATALOG[fc->inst_idx];
                if (fi_p->fm_n > 0) p = &fi_p->fm[0];
            }
            else if (!fc->use_full && fc->inst_idx < YM2203_PATCH_COUNT) {
                p = &YM2203_PATCHES[fc->inst_idx];
            }
            if (!p) continue;
            ym2203_key_off(ch);
            ym2203_patch_apply(p, ch);
            if (h->vel_car_ratio > 0 || h->vel_mod_ratio > 0)
                ym_patch_vol_split(p, ch, vel, fc->vol, h->vel_car_ratio, h->vel_mod_ratio);
            else
                ym_patch_vol(p, ch, vel, fc->vol);
            ym2203_set_note(ch, note);
            ym2203_key_on(ch, 0x0Fu);
            s_m3_ch_note[ch] = note;
        }
        s_m3_note_ch[note] = 0;
        break;
    }

    /* ── VGM6: FULL6_VGM_CATALOG 6ch 프리셋 발음 ─────────────────────────
     * fi_note_on_v()가 FM(fm_n채널)+SSG(ssg_mask채널) 일괄 처리.
     * np_soft=22, np_hard=6은 full6_vgm_note_on()과 동일한 기본값. */
    case CH_MODE_VGM6:
    {
        if (g_full_preset.vgm6_inst_idx >= FULL6_VGM_CATALOG_COUNT)
            g_full_preset.vgm6_inst_idx = 0;
        const full_inst_t* fi = FULL6_VGM_CATALOG[g_full_preset.vgm6_inst_idx];
        uint8_t n = fi->fm_n; if (n > 3u) n = 3u;

        ym_write_force_cached(0x27, 0x00);

        /* fi_note_on_v: key_off→patch+vol→set_note→key_on + SSG 통합 */
        fi_note_on_v(fi, note, vel, 22u, 6u);

        /* note 추적: FM 채널 0..n-1 */
        for (uint8_t ch = 0; ch < n; ch++)
            s_m3_ch_note[ch] = note;
        s_m3_note_ch[note] = 0;
        break;
    }

    }  /* end switch(ch_mode) in m3_full_note_on */
}


static void m3_full_note_off(uint8_t note)
{
    if (note > 127) return;
    harmony_cfg_t* h = &g_full_preset.harmony;

    if (h->voice_mgr_on) {
        vm_note_off(&g_voice_mgr, note);
        return;
    }

    /* pot.c 방식: 3채널 전부 순회해서 해당 note를 끔 (DUAL/TRIPLE/INDEPENDENT 공통) */
    for (uint8_t ch = 0; ch < 3; ch++) {
        if (s_m3_ch_note[ch] == note) {
            ym2203_key_off(ch);
            s_m3_ch_note[ch] = 0xFF;

            if (g_full_preset.ch_mode == CH_MODE_VGM6) {
                /* VGM6: ssg_mask 기반으로 SSG 채널 소거 */
                if (g_full_preset.vgm6_inst_idx < FULL6_VGM_CATALOG_COUNT) {
                    const full_inst_t* fi = FULL6_VGM_CATALOG[g_full_preset.vgm6_inst_idx];
                    for (int pch = 0; pch < 3; pch++)
                        if (fi->ssg_mask & (1u << pch))
                            ym2203_ssg_set_vol((uint8_t)pch, 0);
                }
            }
            else if (g_full_preset.fm[ch].use_full && !g_full_preset.fm[ch].edited) {
                uint8_t idx = g_full_preset.fm[ch].inst_idx;
                if (idx < FULL_INST_COUNT) {
                    const full_inst_t* fi = FULL_INST_CATALOG[idx];
                    for (int pch = 0; pch < 3; pch++)
                        if (fi->ssg_mask & (1u << pch))
                            ym2203_ssg_set_vol((uint8_t)pch, 0);
                }
            }
        }
    }
    s_m3_note_ch[note] = 0xFF;
}

/* CC 처리 ? voice_mgr / midi_perf 양쪽 전달 */
static void m3_full_cc(uint8_t cc, uint8_t val)
{
    harmony_cfg_t* h = &g_full_preset.harmony;
    if (h->voice_mgr_on) vm_cc(&g_voice_mgr, cc, val);

    fm_ch_cfg_t* fc = &g_full_preset.fm[0];
    const ym2203_patch_t* p0 = (fc->edited || !fc->use_full)
        ? &fc->patch_override
        : &FULL_INST_CATALOG[(fc->inst_idx < FULL_INST_COUNT) ? fc->inst_idx : 0]->fm[0];
    mp_cc(&g_midi_perf, cc, val, p0);
}

/* 피치벤드 */
static void m3_full_pitch_bend(int16_t pb_raw)
{
    mp_pitch_bend(&g_midi_perf, pb_raw, 0);
    for (int ch = 0; ch < 3; ch++) {
        if (s_m3_ch_note[ch] == 0xFF) continue;
        uint8_t note = s_m3_ch_note[ch];
        if (g_full_preset.harmony.ji_on && g_voice_mgr.key_det.valid)
            kd_ji_note_ex((uint8_t)ch, note,
                &g_voice_mgr.key_det, g_midi_perf.pb_cent);
        else
            ym_set_note_ex((uint8_t)ch, note,
                g_midi_perf.pb_cent, 0);
    }
}

/* 매 10ms 틱 ? 농음 LFO + voice_mgr age + FM LFO */
static void m3_full_tick(void)
{
    harmony_cfg_t* h = &g_full_preset.harmony;

    if (h->voice_mgr_on)
        vm_tick_full(&g_voice_mgr);  /* vm_tick_full() 내부에서 ym2203_tick()까지 처리 */
    else
        ym2203_tick();              /* Rev.12 FM/SSG LFO, SSG cut/attack/vibrato/portamento tick */

    if (g_nlfo.active && h->nlfo_preset < NLFO_PRESET_COUNT) {
        int16_t cent = nlfo_tick_r(&g_nlfo, h->nlfo_rand_range);
        /* [FIX-NLFO] 모든 활성 채널에 농음 LFO 적용 (이전: g_nlfo_note 단일 채널만) */
        for (uint8_t ch = 0; ch < 3; ch++) {
            uint8_t note = s_m3_ch_note[ch];
            if (note == 0xFF) continue;
            const ym2203_fnum_t* f = &YM2203_FREQ_LUT[note];
            uint16_t base = (uint16_t)(f->lsb | ((uint16_t)(f->msb & 7u) << 8));
            uint8_t  blk = (uint8_t)((f->msb >> 3u) & 7u);
            uint16_t fnum; uint8_t oblk;
            ym_fnum_cents_blk(base, blk,
                (int16_t)(cent + g_midi_perf.pb_cent),
                &fnum, &oblk);
            ym_set_fnum_raw(ch, fnum, oblk);
        }
    }
}

static void m3_full_all_off(void)
{
    if (g_full_preset.harmony.voice_mgr_on)
        vm_panic_off(&g_voice_mgr);
    else {
        for (int i = 0; i < 3; i++) ym2203_key_off((uint8_t)i);
    }
    for (int i = 0; i < 3; i++) ym2203_ssg_set_vol((uint8_t)i, 0);
    pt2258_set_mute(1);
    m3_full_voice_reset();
}

// =============================================================================
//  [섹션 E]  모드 0: MENU SELECT
// =============================================================================
#define MENU_COUNT 4
static const char* const MENU_NAMES[MENU_COUNT] = { "1.SYNTH/MIDI","2.VGM PLAYER","3.MIDI KEYBOARD","4.INST SETUP" };
static const uint16_t       MENU_COLORS[MENU_COUNT] = { COLOR_CYAN,COLOR_MAGENTA,COLOR_ORANGE,0x4A10u };
static const uint8_t        MENU_TO_MODE[MENU_COUNT] = { ZYNQ_MODE_SYNTH,ZYNQ_MODE_VGM,ZYNQ_MODE_MIDIVIS,ZYNQ_MODE_INSTSETUP };
static int     menu_idx = 0;
static uint8_t menu_joy_moved = 0;   /* [FIX] 모드 복귀 시 리셋 가능하도록 모듈 수준으로 승격 */
static uint8_t menu_last_sw = 0;

#define MENU_ROW_Y(i) ((uint16_t)(22 + (i)*28))

static void mode_menu_draw(void)
{
    lcd_clear(COLOR_BLACK);
    lcd_draw_header(COLOR_DKGREY, "  EFFECT PROCESSOR v14", COLOR_WHITE);
    for (int i = 0; i < MENU_COUNT; i++) {
        lcd_string(10, MENU_ROW_Y(i), (i == menu_idx) ? "->" : "  ", COLOR_WHITE, COLOR_BLACK);
        lcd_string(28, MENU_ROW_Y(i), MENU_NAMES[i],
            (i == menu_idx) ? MENU_COLORS[i] : COLOR_GREY, COLOR_BLACK);
    }
    /* PT2258 L/R dB 표시 */
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "L:-%2udB  R:-%2udB",
        (unsigned)g_pt2258_att_l_db, (unsigned)g_pt2258_att_r_db);
    lcd_string(4, 212, vbuf, COLOR_CYAN, COLOR_BLACK);
    lcd_string(4, 228, "JOY-Y:이동  SW3:선택", COLOR_DKGREY, COLOR_BLACK);
}

static void mode_menu_enter(void)
{
    menu_joy_moved = 0;   /* [FIX] 진입 시 항상 리셋 → 이전 세션 잔류 방지 */
    pt2258_audio_gate_for_mode(ZYNQ_MODE_MENU);
    /* [FIX-P6] 어떤 모드에서 복귀해도 EQ bypass 잔류 방지 */
    if (p_pre_eq)  reg_wr(p_pre_eq, EQ_REG_CTRL, PRE_EQ_CTRL_RUN);
    if (p_post_eq) reg_wr(p_post_eq, EQ_REG_CTRL, PRE_EQ_CTRL_RUN);
    mode_menu_draw();
}

static int mode_menu_update(spi_packet_t* rx, spi_packet_t* tx)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    int prev = menu_idx;

    if (jy < JOY_LOW && !menu_joy_moved && menu_idx > 0) { menu_idx--; menu_joy_moved = 1; }
    else if (jy > JOY_HIGH && !menu_joy_moved && menu_idx < MENU_COUNT - 1) { menu_idx++; menu_joy_moved = 1; }
    /* [FIX] dead zone 리셋은 else-if 체인 밖에서 독립 평가:
       jy < JOY_LOW (상단) 와 dead zone은 동시에 참이 될 수 없으므로
       else-if로 묶으면 조이스틱이 dead zone을 통과해도 리셋이 누락됨 */
    if (jy >= JOY_DEAD_LO && jy <= JOY_DEAD_HI) { menu_joy_moved = 0; }

    if (prev != menu_idx) {
        lcd_string(10, MENU_ROW_Y(prev), "  ", COLOR_WHITE, COLOR_BLACK);
        lcd_string(28, MENU_ROW_Y(prev), MENU_NAMES[prev], COLOR_GREY, COLOR_BLACK);
        lcd_string(10, MENU_ROW_Y(menu_idx), "->", COLOR_WHITE, COLOR_BLACK);
        lcd_string(28, MENU_ROW_Y(menu_idx), MENU_NAMES[menu_idx], MENU_COLORS[menu_idx], COLOR_BLACK);
    }

    tx->active_mode = ZYNQ_MODE_MENU;
    int next = ZYNQ_MODE_MENU;
    if (rx->sw_status == SW_SELECT && menu_last_sw != SW_SELECT)
        next = (int)MENU_TO_MODE[menu_idx];
    menu_last_sw = rx->sw_status;

    /* 메뉴 화면에서 가변저항으로 스테레오 볼륨 독립 제어.
     * ADC_IDX_VOL → CH1(L),  ADC_IDX_PITCH → CH2(R).
     * PT2258는 뮤트 중이어도 볼륨 레지스터 write는 유효 ? unmute 시 즉시 반영됨. */
    {
        uint8_t prev_l = g_pt2258_att_l_db, prev_r = g_pt2258_att_r_db;
        pt2258_update_stereo_from_adc(adc_buf[ADC_IDX_VOL], adc_buf[ADC_IDX_PITCH]);
        if (prev_l != g_pt2258_att_l_db || prev_r != g_pt2258_att_r_db) {
            char vbuf[32];
            snprintf(vbuf, sizeof(vbuf), "L:-%2udB  R:-%2udB",
                (unsigned)g_pt2258_att_l_db, (unsigned)g_pt2258_att_r_db);
            lcd_string(4, 212, vbuf, COLOR_CYAN, COLOR_BLACK);
        }
    }

    return next;
}

static void mode_menu_cleanup(void) { /* 메뉴 상태 유지 */ }

// =============================================================================
/* ── 섹션 F forward declarations ───────────────────────────────────────────
 * 섹션 H(모드 3)에 정의된 함수들을 섹션 F(모드 1)에서 사용하기 위한 전방 선언.
 * 모드 1/3은 동일 헬퍼를 공유한다. */

 /* M3 건반 시각화 상수 (섹션 F/H 공유) */
#define M3_KEY_MIN     36
#define M3_KEY_MAX     96
#define M3_OCT_COUNT   5
#define M3_ROW_Y_START 52
#define M3_ROW_H       32
#define M3_KEY_AREA_X  18
#define M3_KEY_AREA_W  (LCD_W - M3_KEY_AREA_X - 2)
#define M3_KEY_W       (M3_KEY_AREA_W / 12)
#define M3_KEY_H       (M3_ROW_H - 2)
static const uint8_t  M3_IS_BLACK[12] = { 0,1,0,1,0,0,1,0,1,0,1,0 };
static const char* M3_NOTE_NAMES[12] = { "C ","C#","D ","D#","E ","F ","F#","G ","G#","A ","A#","B " };

/* M3 상태 구조체 (모드 1/3 공유, 동시 실행 없음) */
typedef struct {
    uint8_t note_on[128];
    uint8_t sustain;
    uint8_t pending_off[128];
    uint8_t poly_count;
    uint8_t last_note;
    uint8_t last_vel;
} m3_state_t;
static m3_state_t m3;

static void m3_state_reset(void);
static void m3_draw_info(void);
static void m3_draw_octave_row(int r);
static void m3_recount_poly(void);
static void m3_handle_note_on(uint8_t note, uint8_t vel);
static void m3_handle_note_off(uint8_t note);
static void m3_handle_sustain(uint8_t val);
static void m3_idle_sound_guard(void);
static void midi_cc_eq8_preset(uint8_t val, int apply_commit);
static void midi_cc_eq8_enable(uint8_t val);
static void midi_cc_eq8_preview_trigger(uint8_t val);
static void rec_start(void);
static void rec_stop_and_save(void);

//  [섹션 F]  모드 1: SYNTH / SPI-MIDI
//  [V14] 모드 3(USB MIDI)과 동일한 구조. 차이점은 음정 입력 경로뿐:
//         모드 1 = ESP32 SPI 패킷(rx->midi_status/note/vel)
//         모드 3 = USB MIDI 장치 링버퍼
//  나머지 모든 로직 (full_preset_apply_hw, PT2258, 녹음, idle guard,
//  LFO, VCA, 건반 시각화, EQ8 CC, 피치벤드, m3_full_* 발음) 동일.
// =============================================================================

/* ── 모드 1 전용: 직전 SPI MIDI 패킷 중복 감지 ── */
static uint8_t s_m1_last_st = 0, s_m1_last_nt = 0, s_m1_last_vl = 0;

/* ── 모드 1 LCD REC 상태 표시 (모드 3의 m3_rec_draw_status와 동일 로직) ── */
static void m1_rec_draw_status(void)
{
    lcd_fill_rect(0, 210, LCD_W, 8, COLOR_BLACK);
    uint16_t rec_col = (g_rec_state == REC_RUNNING) ? COLOR_RED : COLOR_DKGREY;
    lcd_string(2, 210, "REC", rec_col, COLOR_BLACK);

    if (g_rec_state == REC_IDLE) {
        if (g_rec_saved_ts.tv_sec != 0) {
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (now.tv_sec - g_rec_saved_ts.tv_sec) * 1000L
                + (now.tv_nsec - g_rec_saved_ts.tv_nsec) / 1000000L;
            if (elapsed_ms < 1000L) {
                lcd_string(26, 210, "SAVED!  SW1:시작", COLOR_GREEN, COLOR_BLACK);
                return;
            }
            g_rec_saved_ts.tv_sec = 0;
        }
        lcd_string(26, 210, "READY   SW1:시작", COLOR_DKGREY, COLOR_BLACK);
    }
    else if (g_rec_state == REC_RUNNING) {
        lcd_string(26, 210, "*** RECORDING ***  SW2:종료", COLOR_RED, COLOR_BLACK);
    }
    else {
        lcd_string(26, 210, "SAVING...", COLOR_YELLOW, COLOR_BLACK);
    }
}

static void mode_synth_enter(void)
{
    printf("[M1] SPI-SYNTH 진입\n");

    /* ① 이전 세션 상태 완전 초기화 (note_on/sustain/pending_off/채널 매핑) */
    m3_state_reset();
    s_m1_last_st = 0; s_m1_last_nt = 0; s_m1_last_vl = 0;

    /* ② g_full_preset 전체 HW 반영 (YM2203 + PRE EQ + VCA + POST EQ + DELAY + LFO) */
    full_preset_apply_hw();
    diag_apply_chain_policy("mode1_enter");

    /* 딜레이 bypass 강제 OFF 보장 */
    if (DIAG_USE_DELAY && p_delay && g_full_preset.dly_preset != 0)
        analog_delay_bypass_off(&g_adly);

    printf("[M1] delay readback after enter: ");
    delay_readback();

    /* ③ PT2258: 진입 직후 unmute */
    pt2258_set_mute(0);

    /* HW 상태 readback */
    vca_readback();
    delay_readback();

    lcd_clear(COLOR_BLACK);
    lcd_draw_header(COLOR_CYAN, "  SPI SYNTH (mode=1)", COLOR_WHITE);

    /* 악기명 표시 — g_full_preset.fm[] 기반 (모드 3과 동일) */
    char buf[54];
    {
        fm_ch_cfg_t* f0 = &g_full_preset.fm[0];
        fm_ch_cfg_t* f1 = &g_full_preset.fm[1];
        fm_ch_cfg_t* f2 = &g_full_preset.fm[2];
        const char* n0 = (f0->use_full && !f0->edited && f0->inst_idx < FULL_INST_COUNT)
            ? FULL_INST_CATALOG[f0->inst_idx]->name
            : (f0->inst_idx < YM2203_PATCH_COUNT ? YM2203_PATCHES[f0->inst_idx].name : "???");
        const char* n1 = (f1->use_full && !f1->edited && f1->inst_idx < FULL_INST_COUNT)
            ? FULL_INST_CATALOG[f1->inst_idx]->name
            : (f1->inst_idx < YM2203_PATCH_COUNT ? YM2203_PATCHES[f1->inst_idx].name : "???");
        const char* n2 = (f2->use_full && !f2->edited && f2->inst_idx < FULL_INST_COUNT)
            ? FULL_INST_CATALOG[f2->inst_idx]->name
            : (f2->inst_idx < YM2203_PATCH_COUNT ? YM2203_PATCHES[f2->inst_idx].name : "???");
        snprintf(buf, sizeof(buf), "0:%-8s 1:%-8s 2:%-8s", n0, n1, n2);
    }
    lcd_string(2, 19, buf, COLOR_CYAN, COLOR_BLACK);

    m3_draw_info();
    lcd_fill_rect(0, 50, LCD_W, 2, COLOR_DKGREY);
    for (int r = 0; r < M3_OCT_COUNT; r++) m3_draw_octave_row(r);
    lcd_string(2, 219, "SW1:REC  SW2:STOP+SAVE  SW4:MENU", COLOR_DKGREY, COLOR_BLACK);
    lcd_string(2, 228, "long SW4: --  VOL:음량", COLOR_DKGREY, COLOR_BLACK);
    m1_rec_draw_status();

    /* [FIX-REC2b] 재진입 시 lsw 초기화 플래그 */
    g_m1_lsw_reset = 1;
}

static int mode_synth_update(spi_packet_t* rx, spi_packet_t* tx)
{
    static uint8_t lsw = 0;
    static uint32_t sw4_hold = 0;
    static rec_state_t lrec = (rec_state_t)99;
    static uint32_t toast_refresh = 0;

    /* [FIX-REC2b] 모드 재진입 시 lsw 초기화 */
    if (g_m1_lsw_reset) {
        lsw = rx->sw_status;
        sw4_hold = 0;
        g_m1_lsw_reset = 0;
    }

    tx->active_mode = ZYNQ_MODE_SYNTH;

    /* ── SW4 (EXIT): 짧게 → MENU (녹음 중이면 차단) ── */
    if (rx->sw_status == SW_EXIT) {
        sw4_hold++;
    }
    else {
        if (lsw == SW_EXIT && sw4_hold < 50u) {
            if (g_rec_state == REC_RUNNING) {
                lcd_fill_rect(0, 210, LCD_W, 8, COLOR_BLACK);
                lcd_string(2, 210, "REC", COLOR_RED, COLOR_BLACK);
                lcd_string(26, 210, "중지 후 이동  SW2:종료", COLOR_YELLOW, COLOR_BLACK);
            }
            else {
                sw4_hold = 0; lsw = rx->sw_status; return ZYNQ_MODE_MENU;
            }
        }
        sw4_hold = 0;
    }

    /* ── SW1: 녹음 시작 (엣지 감지) ── */
    if (rx->sw_status == SW_VOL_UP && lsw != SW_VOL_UP) {
        if (g_rec_state == REC_IDLE) {
            rec_start();
            m1_rec_draw_status();
        }
    }

    /* ── SW2: 녹음 종료+저장 (엣지 감지) ── */
    if (rx->sw_status == SW_VOL_DOWN && lsw != SW_VOL_DOWN) {
        if (g_rec_state == REC_RUNNING) {
            lcd_fill_rect(0, 210, LCD_W, 8, COLOR_BLACK);
            lcd_string(2, 210, "REC", COLOR_DKGREY, COLOR_BLACK);
            lcd_string(26, 210, "SAVING...", COLOR_YELLOW, COLOR_BLACK);
            rec_stop_and_save();
            clock_gettime(CLOCK_MONOTONIC, &g_rec_saved_ts);
            toast_refresh = 0;
            m1_rec_draw_status();
        }
    }

    /* ── REC 상태 변화 시 LCD 갱신 ── */
    if (g_rec_state != lrec) {
        lrec = g_rec_state;
        m1_rec_draw_status();
        toast_refresh = 0;
    }

    /* ── 토스트 만료 갱신 ── */
    if (g_rec_saved_ts.tv_sec != 0 && g_rec_state == REC_IDLE) {
        if (++toast_refresh >= 20u) {
            toast_refresh = 0;
            m1_rec_draw_status();
        }
    }

    lsw = rx->sw_status;

    /* ── ADC 가변저항 스테레오 볼륨 (모드 3과 동일: VOL→L, PITCH→R) ── */
    {
        uint8_t prev_l = g_pt2258_att_l_db, prev_r = g_pt2258_att_r_db;
        pt2258_update_stereo_from_adc(adc_buf[ADC_IDX_VOL], adc_buf[ADC_IDX_PITCH]);
        if (prev_l != g_pt2258_att_l_db || prev_r != g_pt2258_att_r_db) {
            char vbuf[32];
            snprintf(vbuf, sizeof(vbuf), "L:-%2udB  R:-%2udB",
                (unsigned)g_pt2258_att_l_db, (unsigned)g_pt2258_att_r_db);
            lcd_string(4, 219, vbuf, COLOR_CYAN, COLOR_BLACK);
        }
    }

    lfo_set_fcw_checked(0, adc_to_lfo_fcw(adc_buf[ADC_IDX_PITCH]));
    m3_full_tick();

    /* ══════════════════════════════════════════════════════════════════════
     * SPI MIDI 입력 처리 — 모드 3의 USB MIDI 큐 처리와 대칭적으로 동작.
     * rx->midi_status/note/vel 이 0이 아닌 새 값일 때만 처리 (중복 제거).
     * 처리 후 m3_handle_note_on/off, m3_handle_sustain, m3_full_cc 등
     * 모드 3과 동일한 핸들러 호출.
     * ══════════════════════════════════════════════════════════════════════ */
    if (rx->midi_status != 0) {
        uint8_t st = rx->midi_status, nt = rx->midi_note, vl = rx->midi_vel;
        /* 중복 패킷 무시 */
        if (!(st == s_m1_last_st && nt == s_m1_last_nt && vl == s_m1_last_vl)) {
            s_m1_last_st = st; s_m1_last_nt = nt; s_m1_last_vl = vl;
            uint8_t mt = st & 0xF0u;

            if (mt == 0x90u) {
                if (vl > 0u) m3_handle_note_on(nt, vl);
                else         m3_handle_note_off(nt);
            }
            else if (mt == 0x80u) {
                m3_handle_note_off(nt);
            }
            else if (mt == 0xB0u) {
                if (nt == 64u) {
                    m3_handle_sustain(vl);
                }
                else if (nt == 123u || nt == 120u) {
                    /* all notes off */
                    m3_full_all_off();
                    memset(m3.note_on, 0, sizeof(m3.note_on));
                    memset(m3.pending_off, 0, sizeof(m3.pending_off));
                    m3.sustain = 0; m3_recount_poly();
                    for (int r = 0; r < M3_OCT_COUNT; r++) m3_draw_octave_row(r);
                    m3_draw_info();
                }
                else {
                    m3_full_cc(nt, vl);
                    if (nt == 1u) {
                        uint16_t dep = (uint16_t)((uint32_t)vl * 0x01FFu / 127u);
                        lfo_set_ch(0, adc_to_lfo_fcw(adc_buf[ADC_IDX_PITCH]), dep, 0u, 0u);
                    }
                    else if (nt == 7u) {
                        vca_set_makeup_checked((uint16_t)((uint32_t)vl * Q15_ONE / 127u));
                    }
                    /* EQ8 MIDI CC 제어 (모드 3과 동일) */
                    else if (nt == 0x50u) { midi_cc_eq8_preset(vl, 0); }
                    else if (nt == 0x51u) { midi_cc_eq8_enable(vl); }
                    else if (nt == 0x52u) { midi_cc_eq8_preview_trigger(vl); }
                }
            }
            else if (mt == 0xE0u) {
                int16_t pb_raw = (int16_t)(((uint32_t)nt | ((uint32_t)vl << 7)) - 8192);
                m3_full_pitch_bend(pb_raw);
            }
        }
    }
    else {
        /* midi_status == 0: 새 입력 없음 → 중복 감지 상태 초기화 */
        s_m1_last_st = 0; s_m1_last_nt = 0; s_m1_last_vl = 0;
    }

    m3_idle_sound_guard();

    return ZYNQ_MODE_SYNTH;
}

static void mode_synth_cleanup(void)
{
    /* 녹음 중 모드 전환 시 강제 중단 + 저장 */
    if (g_rec_state != REC_IDLE) {
        printf("[M1] cleanup: 녹음 강제 종료 → WAV 저장\n");
        rec_stop_and_save();
    }

    /* PT2258 즉시 mute — 팝/잔류음 방지 */
    pt2258_set_mute(1);
    m3_full_all_off();
    ym_silence();
    /* SSG Mixer 닫기 */
    if (p_ym) ym_write(YM_SSG_MIXER, 0x3Fu);
    lfo_stop_all();
    vca_drain();
    vca_bypass();
    /* 상태 완전 초기화: 다음 진입 시 clean slate */
    m3_state_reset();
    s_m1_last_st = 0; s_m1_last_nt = 0; s_m1_last_vl = 0;
}

// =============================================================================
//  [섹션 G]  모드 2: VGM PLAYER  (V13 그대로 보존)
// =============================================================================
static volatile int      g_vgm_play = 0;
static volatile int      g_vgm_exit = 0;
static volatile uint32_t g_vgm_pos = 0;

/* [V16] 모드 2: 단일 재생 경로 ? ROUND 구조 제거
 *  모드4 APPLY 여부에 따라 두 경로만 존재:
 *   · g_m4_preset_valid == 0 : YM 원음 통과 (이펙터 바이패스)
 *   · g_m4_preset_valid == 1 : full_preset_apply_hw() 전체 적용
 *  진단용 ROUND1(vgm_start_single_diag) 완전 제거. */
static int g_m4_preset_valid = 0;   /* 모드4 APPLY 시 1로 설정 */

static int vgm_done = 0;   /* 재생 완료 플래그 (단발 모드) */

/* ─────────────────────────────────────────────────────────────────────────────
 *  vgm_start_single_diag()  ?  VGM_ROUND_NO_REV 전용 이펙터 직접 검증 시퀀스
 *  [V16] 완전 제거됨 ? full_preset_apply_hw() 단일 경로로 통합. */

static void* vgm_rt_thread(void* arg)
{
    (void)arg;
    struct sched_param sp = { .sched_priority = 80 };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    printf("[VGM-RT] 시작\n");
    while (!g_vgm_exit) {
        MEM_BARRIER();
        if (!g_vgm_play) { usleep(1000); continue; }
        printf("[VGM-RT] 재생 시작 (size=%u)\n", vgm_size);
        uint32_t i = 0;
        while (i < vgm_size) {
            g_vgm_pos = i; MEM_BARRIER();
            if (!g_vgm_play || g_vgm_exit) break;
            uint8_t cmd = vgm_music[i];
            if (cmd == 0x55) { if (i + 2 >= vgm_size) { i++; continue; } ym_write(vgm_music[i + 1], vgm_music[i + 2]); i += 3; }
            else if (cmd == 0x61) { if (i + 2 >= vgm_size) { i++; continue; } uint16_t n = (uint16_t)vgm_music[i + 1] | ((uint16_t)vgm_music[i + 2] << 8); usleep((uint32_t)((double)n * 22.675736)); i += 3; }
            else if (cmd == 0x62) { usleep(16667u); i++; }
            else if (cmd == 0x63) { usleep(20000u); i++; }
            else if ((cmd & 0xF0) == 0x70) { usleep((uint32_t)(((cmd & 0x0Fu) + 1u) * 22.675736)); i++; }
            else if (cmd == 0x66) { break; }
            else { i++; }
        }
        g_vgm_pos = 0; MEM_BARRIER(); g_vgm_play = 0; MEM_BARRIER();
        printf("[VGM-RT] 재생 완료\n");
    }
    printf("[VGM-RT] 종료\n"); return NULL;
}

static void vgm_draw_progress(void)
{
    if (vgm_size == 0) return;
    MEM_BARRIER(); uint32_t pos = g_vgm_pos;
    int filled = (int)((uint64_t)pos * 312u / vgm_size);
    if (filled > 312) filled = 312;
    if (filled > 0) lcd_fill_rect(4, 74, (uint16_t)filled, 6, COLOR_GREEN);
    if (filled < 312) lcd_fill_rect((uint16_t)(4 + filled), 74, (uint16_t)(312 - filled), 6, COLOR_DKGREY);
}

/* vgm_start_single: 모드2 항상 전 이펙터 바이패스 → YM 원음 통과 */
static void vgm_start_single(void)
{
    /* 모드2: 전 이펙터 바이패스 → YM 원음 통과
     * 베어메탈 delay_init 시퀀스 동일하게 적용:
     *   bypass=1 + dry_wet=0 먼저 → 파라미터 전기록 → 200ms BRAM flush → bypass 유지 */
    printf("\n[VGM] ★ ALL BYPASS: YM 원음 통과 ★\n");

    /* ── PRE/POST EQ: HW bypass ── */
    if (p_pre_eq)  reg_wr(p_pre_eq, EQ_REG_CTRL, PRE_EQ_CTRL_BYPASS);
    if (p_post_eq) reg_wr(p_post_eq, EQ_REG_CTRL, PRE_EQ_CTRL_BYPASS);

    /* ── VCA: 1:1 통과 (thresh=max, cs=0, makeup=1.0) ── */
    vca_bypass();

    /* ── LFO 전채널 정지 (CH0/4/5 포함) ── */
    lfo_stop_all();

    /* ── DELAY: 베어메탈 delay_init Step1~9 이식 ──
     * Step1: bypass=1, dry_wet=0 (이중 보호) */
    if (p_delay) {
        adly_wr(&g_adly, ADLY_OFF_BYPASS, 1u);
        adly_wr(&g_adly, ADLY_OFF_DRY_WET, 0u);

        /* Step2: OUTHPF 활성 */
        adly_wr(&g_adly, ADLY_OFF_OUTHPF_BYPASS, 0u);

        /* Step3~5: 발진 방지 안전값 전기록 (bypass=1 상태에서) */
        adly_wr(&g_adly, ADLY_OFF_DELAY_TIME, 0x200000u);   /* ~8192샘플/170ms */
        adly_wr(&g_adly, ADLY_OFF_FEEDBACK, 0u);          /* FB=0 (발진 방지) */
        adly_wr(&g_adly, ADLY_OFF_DIFFUSION_START, 0xFFu);     /* APF 완전 OFF */
        adly_wr(&g_adly, ADLY_OFF_DIFFUSION_MIX, 0u);
        adly_wr(&g_adly, ADLY_OFF_DIFFUSION_G, 0u);
        adly_wr(&g_adly, ADLY_OFF_LFO_DEPTH_APF, 0u);        /* LFO 변조 OFF */
        adly_wr(&g_adly, ADLY_OFF_SLEW_RATE, 0xFFFFu);   /* INSTANT */
        adly_wr(&g_adly, ADLY_OFF_SAT_THRESH, 30000u);
        adly_wr(&g_adly, ADLY_OFF_MS_WIDTH, 0u);        /* M/S OFF */
        adly_wr(&g_adly, ADLY_OFF_DAMP_ALPHA, 0u);        /* 댐핑 OFF */
        adly_wr(&g_adly, ADLY_OFF_DAMP_HF, 0u);
        adly_wr(&g_adly, ADLY_OFF_TAP_EN, 0u);        /* TAP OFF */

        /* Step6: BRAM flush 대기 (bypass=1 상태에서 200ms) */
        printf("[VGM] BRAM flush 대기 200ms...\n");
        usleep(200000u);

        /* Step7: bypass 유지 ? 원음 통과 목적이므로 해제하지 않음 */
        /* dry_wet도 0 유지 ? bypass=1이면 RTL이 in_hold 직결 출력 */
    }
    ym_gpio_init();
    ym_reset();
    vgm_done = 0; g_vgm_pos = 0; MEM_BARRIER();
    pt2258_set_mute(0);
    g_vgm_play = 1; MEM_BARRIER();
}


/* 2. 독립된 두 번째 함수: VGM 플레이어 모드 진입 및 UI 렌더링 */
static void mode_vgm_enter(void)
{
    pt2258_set_mute(1);
    if (!p_ym) { fprintf(stderr, "[VGM] p_ym=NULL\n"); return; }

    /* 진입 시 delay bypass=1 선점 + LFO 정지.
     * 실제 파라미터 초기화는 vgm_start_single()에서 베어메탈 시퀀스로 수행. */
    if (p_delay) adly_wr(&g_adly, ADLY_OFF_BYPASS, 1u);
    lfo_stop_all();
    ym_gpio_init(); ym_reset();
    lcd_clear(COLOR_BLACK);
    lcd_draw_header(COLOR_MAGENTA, "  VGM PLAYER (mode=2)", COLOR_WHITE);
    lcd_string(4, 228, "SW3:재생  SW4:종료  VOL:음량", COLOR_RED, COLOR_BLACK);

    /* 모드2: 항상 ALL BYPASS */
    lcd_string(4, 26, "ALL BYPASS: EQ/VCA/DLY 전부 통과", COLOR_CYAN, COLOR_BLACK);
    lcd_string(4, 42, "Playing...", COLOR_GREEN, COLOR_BLACK);

    vgm_start_single();
}

static int mode_vgm_update(spi_packet_t* rx, spi_packet_t* tx)
{
    static uint8_t lsw = 0;

    /* SW4: 메뉴 복귀 */
    if (rx->sw_status == SW_EXIT && lsw != SW_EXIT) {
        lsw = rx->sw_status; tx->active_mode = ZYNQ_MODE_VGM;
        return ZYNQ_MODE_MENU;
    }

    /* [V16] SW3: 재생 재시작 (라운드 개념 제거) */
    if (rx->sw_status == SW_SELECT && lsw != SW_SELECT) {
        printf("[VGM] SW3: 재생 재시작\n");
        g_vgm_play = 0; MEM_BARRIER();
        ym_silence();
        pt2258_set_mute(1);
        usleep(50000);
        lcd_fill_rect(0, 58, LCD_W, 20, COLOR_BLACK);
        lcd_string(4, 58, "Playing...", COLOR_GREEN, COLOR_BLACK);
        vgm_done = 0;
        vgm_start_single();
    }

    lsw = rx->sw_status; tx->active_mode = ZYNQ_MODE_VGM;

    static uint32_t ui_cnt = 0;
    if (++ui_cnt % 10 == 0) vgm_draw_progress();
    ym_set_ssg_volume_checked(adc_buf[ADC_IDX_VOL]);

    /* [V15] PT2258 스테레오 볼륨 */
    {
        uint8_t prev_l = g_pt2258_att_l_db, prev_r = g_pt2258_att_r_db;
        pt2258_update_stereo_from_adc(adc_buf[ADC_IDX_VOL], adc_buf[ADC_IDX_PITCH]);
        if (prev_l != g_pt2258_att_l_db || prev_r != g_pt2258_att_r_db) {
            char vbuf[24];
            snprintf(vbuf, sizeof(vbuf), "L:-%2udB R:-%2udB",
                (unsigned)g_pt2258_att_l_db, (unsigned)g_pt2258_att_r_db);
            lcd_string(4, 212, vbuf, COLOR_CYAN, COLOR_BLACK);
        }
    }
    MEM_BARRIER();

    /* 재생 완료 감지 (단발) */
    if (!g_vgm_play && !vgm_done) {
        vgm_done = 1;
        ym_silence();
        pt2258_set_mute(1);
        lcd_fill_rect(4, 74, 312, 6, COLOR_GREEN);
        lcd_fill_rect(0, 58, LCD_W, 14, COLOR_BLACK);
        lcd_string(4, 58, "완료. SW3:재생반복  SW4:메뉴", COLOR_DKGREY, COLOR_BLACK);
        printf("[VGM] 재생 완료\n");
    }
    return ZYNQ_MODE_VGM;
}
static void mode_vgm_cleanup(void)
{
    g_vgm_play = 0; MEM_BARRIER();
    pt2258_set_mute(1);          /* [PT2258] VGM 종료 → mute */
    ym_silence(); delay_mute(); vca_drain();

    /* VGM 종료 후 이펙터 복원: bypass 상태 해제 */
    if (p_pre_eq)  reg_wr(p_pre_eq, EQ_REG_CTRL, PRE_EQ_CTRL_RUN);
    if (p_post_eq) reg_wr(p_post_eq, EQ_REG_CTRL, PRE_EQ_CTRL_RUN);
    /* LIGHT-CHAIN: WARM/SVF/REVERB 제외 */
    /* [FIX-VCA] vca_drain() 후 makeup 복원: bypass 상태로 */
    vca_bypass();
    vgm_done = 0;  /* 다음 진입을 위해 초기화 */
}

// =============================================================================
// =============================================================================
//  [섹션 H-REC]  DMA 녹음 엔진  (모드 3 전용)
//
//  하드웨어 경로:
//    analog_delay_top.m_axis_dma → audio_to_axis → axi_dma_0/S_AXIS_S2MM
//    → DDR3 예약 버퍼 0x1E000000 (8MB, shared-dma-pool)
//
//  포맷: {R[31:16], L[15:0]} 32bit/sample → WAV stereo L,R interleave
//  샘플레이트: 48000 Hz (실측 확정: 57,147,392샘플 / 1190.6초)
//  더블버퍼링: IOC 수신 즉시 다음 슬롯 kick → fwrite 중 DMA 연속 동작
//
//  SW1: 녹음 시작   SW2: 녹음 종료 → WAV 저장
// =============================================================================

/* ── DMA 레지스터 헬퍼 ─────────────────────────────────────────────────── */
static inline void dma_reg_wr(uint32_t off, uint32_t val)
{
    if (!uio_DMA.map) return;
    volatile uint32_t* p = (volatile uint32_t*)((uint8_t*)uio_DMA.map + off);
    *p = val;
    __sync_synchronize();
}
static inline uint32_t dma_reg_rd(uint32_t off)
{
    if (!uio_DMA.map) return 0;
    volatile uint32_t* p = (volatile uint32_t*)((uint8_t*)uio_DMA.map + off);
    __sync_synchronize();
    return *p;
}

/* ── S2MM 단일 전송 구동 ──────────────────────────────────────────────── */
/* RS=1(Run)은 rec_start()에서 이미 세팅됨.
 * [폴링방식] UIO unmask write 제거 — IOC_IRQEN 미사용이므로 불필요. */
static void dma_s2mm_start_transfer(uint32_t dest_phys, uint32_t len_bytes)
{
    /* 1. IOC 플래그 클리어 (W1C) — 다음 폴링 기준점 초기화 */
    uint32_t sr = dma_reg_rd(DMA_S2MM_DMASR);
    if (sr & DMA_SR_IOC_IRQ) dma_reg_wr(DMA_S2MM_DMASR, DMA_SR_IOC_IRQ);

    /* 2. 목적지 주소 */
    dma_reg_wr(DMA_S2MM_DA, dest_phys);

    /* 3. 길이 기록 → 전송 시작 (LENGTH 쓰기가 트리거) */
    dma_reg_wr(DMA_S2MM_LENGTH, len_bytes);
    /* UIO unmask write 제거 — 폴링 방식에서 불필요 */
}

/* ── S2MM 소프트 리셋 ─────────────────────────────────────────────────── */
static void dma_s2mm_reset(void)
{
    dma_reg_wr(DMA_S2MM_DMACR, DMA_CR_RESET);
    for (int i = 0; i < 1000; i++) {
        if (!(dma_reg_rd(DMA_S2MM_DMACR) & DMA_CR_RESET)) break;
        usleep(100);
    }
}

/* ── WAV 헤더 기록 (스트리밍용: 먼저 placeholder 기록 후 finalize로 수정) ── */
static void rec_wav_write_header(FILE* f, uint32_t data_size)
{
    uint32_t sample_rate = REC_SAMPLE_RATE;
    uint16_t num_ch = 2;
    uint16_t bits = 16;
    uint16_t block_align = (uint16_t)(num_ch * bits / 8);   /* 4 */
    uint32_t byte_rate = sample_rate * block_align;
    uint32_t riff_size = 36u + data_size;

    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16; uint16_t audio_fmt = 1;
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_fmt, 2, 1, f);
    fwrite(&num_ch, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
}

/* 녹음 완료 후 RIFF/data 크기 필드만 덮어쓰기 */
static void rec_wav_finalize(FILE* f, uint32_t data_bytes)
{
    uint32_t riff_size = 36u + data_bytes;
    fseek(f, 4, SEEK_SET); fwrite(&riff_size, 4, 1, f);
    fseek(f, 40, SEEK_SET); fwrite(&data_bytes, 4, 1, f);
    fflush(f);
}

/* ── 녹음 스레드 (더블버퍼링: 다음 청크 DMA kick → 현재 청크 fwrite) ─── */
static void* rec_dma_thread(void* arg)
{
    (void)arg;
    struct sched_param sp = { .sched_priority = 70 };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    printf("[REC] 스레드 시작 (폴링모드, 청크=%uMB×%u)\n",
        DMA_CHUNK_SIZE >> 20, DMA_CHUNK_COUNT);

    /*
     * [더블버퍼링 설계 — 폴링 방식]
     *
     * UIO 인터럽트(IOC_IRQEN) 대신 DMASR.IOC 비트를 1ms 간격으로 직접 폴링.
     * UIO 카운터 누적으로 인한 poll() 즉시 리턴 버그 원천 차단.
     *
     * Cortex-A9 부하: usleep(1ms) × ~10,900회/청크 → 실질 부하 무시 가능.
     * SCHED_FIFO RT 스레드이므로 슬립 중 MIDI/main 스레드에 CPU 양보.
     *
     * 흐름:
     *   kick(slot 0) → poll DMASR.IOC
     *   IOC+Idle → kick(slot 1) → fwrite(slot 0)
     *   IOC+Idle → kick(slot 2) → fwrite(slot 1)
     *   ...
     */

    uint32_t cur = 0;   /* 지금 DMA가 채우고 있는 슬롯 */

    /* 첫 청크 kick */
    dma_s2mm_start_transfer(ADDR_DMA_BUF + cur * DMA_CHUNK_SIZE, DMA_CHUNK_SIZE);

    while (!g_rec_thread_exit) {
        MEM_BARRIER();
        if (g_rec_state != REC_RUNNING) { usleep(1000); continue; }

        /* ── DMASR.IOC 폴링 (1ms 간격) ── */
        int got_ioc = 0;
        while (!g_rec_thread_exit && g_rec_state == REC_RUNNING) {
            usleep(1000);
            uint32_t sr = dma_reg_rd(DMA_S2MM_DMASR);
            if (!(sr & DMA_SR_IOC_IRQ)) continue;   /* 아직 전송 중 */

            dma_reg_wr(DMA_S2MM_DMASR, DMA_SR_IOC_IRQ);  /* W1C 클리어 */

            if (!(sr & DMA_SR_IDLE)) {
                /* IOC 왔지만 Idle 아님 → 에러/경쟁상태, 재폴링 */
                printf("[REC] WARN: IOC+!Idle sr=0x%08X\n", sr);
                continue;
            }
            got_ioc = 1;
            break;
        }

        /* ── 중단 요청: 현재 청크 부분 저장 ── */
        if (g_rec_thread_exit || g_rec_state == REC_STOPPING) {
            /* S2MM_LENGTH는 write-only(BTT). 잔여량 읽기 불가.
             * Idle=1이면 완료 → 전체 저장. Idle=0이면 진행 중 → 폐기. */
            uint32_t sr = dma_reg_rd(DMA_S2MM_DMASR);
            if (sr & DMA_SR_IDLE) {
                uint8_t* src = (uint8_t*)uio_DMA_BUF.map + cur * DMA_CHUNK_SIZE;
                if (g_rec_file)      fwrite(src, 1, DMA_CHUNK_SIZE, g_rec_file);
                if (g_rec_file_ext4) fwrite(src, 1, DMA_CHUNK_SIZE, g_rec_file_ext4);
                g_rec_bytes += DMA_CHUNK_SIZE;
                printf("[REC] 마지막 청크 완료분 저장: %u bytes\n", DMA_CHUNK_SIZE);
            }
            else {
                printf("[REC] 마지막 청크 진행 중 → 부분 저장 생략\n");
            }
            break;
        }

        if (!got_ioc) continue;

        /* ── [핵심] 다음 슬롯 kick → 현재 슬롯 fwrite ── */
        /* kick을 fwrite 전에 먼저 실행해야 DMA 공백 없이 연속 동작 */
        uint32_t next = (cur + 1u) % DMA_CHUNK_COUNT;
        dma_s2mm_start_transfer(ADDR_DMA_BUF + next * DMA_CHUNK_SIZE, DMA_CHUNK_SIZE);

        uint8_t* src = (uint8_t*)uio_DMA_BUF.map + cur * DMA_CHUNK_SIZE;
        if (g_rec_file)      fwrite(src, 1, DMA_CHUNK_SIZE, g_rec_file);
        if (g_rec_file_ext4) fwrite(src, 1, DMA_CHUNK_SIZE, g_rec_file_ext4);
        g_rec_bytes += DMA_CHUNK_SIZE;
        g_rec_wrap++;
        MEM_BARRIER();
        printf("[REC] 청크 #%u (slot %u) 완료 → 누적 %.1f초\n",
            g_rec_wrap, cur,
            (float)g_rec_bytes / (float)(REC_SAMPLE_RATE * 4u));

        cur = next;
        g_rec_chunk_idx = cur;
        MEM_BARRIER();
    }

    dma_reg_wr(DMA_S2MM_DMACR, 0u);  /* RS=0 halt */
    printf("[REC] 스레드 종료\n");
    return NULL;
}

/* ── 공개 API ────────────────────────────────────────────────────────── */
static void rec_start(void)
{
    if (g_rec_state != REC_IDLE) return;
    if (!uio_DMA.map || !uio_DMA_BUF.map) {
        printf("[REC] DMA 미연결 → 녹음 불가\n"); return;
    }

    /* ── WAV 파일 오픈 (FAT32 + ext4 양쪽) ── */
    char path_fat[128], path_ext4[128];
    snprintf(path_fat, sizeof(path_fat), "%s/rec_%04u.wav", REC_WAV_DIR_FAT, g_rec_idx);
    snprintf(path_ext4, sizeof(path_ext4), "%s/rec_%04u.wav", REC_WAV_DIR_EXT4, g_rec_idx);
    g_rec_idx++;

    /* ext4 디렉토리 자동 생성 (최초 1회) */
    mkdir(REC_WAV_DIR_EXT4, 0755);

    g_rec_file = fopen(path_fat, "wb");
    g_rec_file_ext4 = fopen(path_ext4, "wb");

    if (!g_rec_file && !g_rec_file_ext4) {
        printf("[REC] WAV 파일 열기 실패 (FAT32:%s, ext4:%s)\n", path_fat, path_ext4);
        g_rec_idx--;  /* 실패 시 번호 롤백 */
        return;
    }
    if (!g_rec_file)      printf("[WARN] FAT32 저장 불가: %s\n", path_fat);
    if (!g_rec_file_ext4) printf("[WARN] ext4  저장 불가: %s\n", path_ext4);

    /* placeholder 헤더 기록 (finalize 시 덮어씀) */
    if (g_rec_file)      rec_wav_write_header(g_rec_file, 0u);
    if (g_rec_file_ext4) rec_wav_write_header(g_rec_file_ext4, 0u);

    printf("[REC] 녹음 시작 → FAT32:%s  ext4:%s\n", path_fat, path_ext4);

    g_rec_bytes = 0;
    g_rec_wrap = 0;
    g_rec_thread_exit = 0;

    /* DMA 소프트 리셋 후 RS=1 (Run) 세팅.
     * [폴링방식] IOC_IRQEN 제거 — UIO 인터럽트 카운터 누적 원천 차단.
     * DMASR을 리셋 직후 명시적으로 클리어해 잔류 IOC 플래그 제거. */
    dma_s2mm_reset();
    dma_reg_wr(DMA_S2MM_DMASR, DMA_SR_IOC_IRQ);   /* 잔류 IOC W1C 클리어 */
    dma_reg_wr(DMA_S2MM_DMACR, DMA_CR_RUN);        /* IOC_IRQEN 의도적 제거 */

    MEM_BARRIER();
    g_rec_state = REC_RUNNING;
    pthread_create(&g_rec_thread, NULL, rec_dma_thread, NULL);
}

static void rec_stop_and_save(void)
{
    if (g_rec_state == REC_IDLE) return;

    g_rec_state = REC_STOPPING;
    g_rec_thread_exit = 1;
    MEM_BARRIER();
    pthread_join(g_rec_thread, NULL);
    dma_s2mm_reset();

    uint32_t total_bytes = g_rec_bytes;
    printf("[REC] 녹음 종료: 총 %u bytes (%.1f초)\n",
        total_bytes, (float)total_bytes / (float)(REC_SAMPLE_RATE * 4u));

    /* 스트리밍 WAV: 헤더의 크기 필드만 덮어씀 */
    if (g_rec_file) {
        rec_wav_finalize(g_rec_file, total_bytes);
        fclose(g_rec_file);
        g_rec_file = NULL;
        printf("[REC] FAT32 저장 완료\n");
    }
    if (g_rec_file_ext4) {
        rec_wav_finalize(g_rec_file_ext4, total_bytes);
        fclose(g_rec_file_ext4);
        g_rec_file_ext4 = NULL;
        printf("[REC] ext4  저장 완료\n");
    }

    g_rec_state = REC_IDLE;
    g_rec_bytes = 0;
}

//  [섹션 H]  모드 3: USB MIDI KEYBOARD + YM2203
//    - preset_apply_hw() → full_preset_apply_hw()
//    - ym_synth_note_on/off/all_off/tick → m3_full_note_on/off/all_off/tick
//    - CC/피치벤드: m3_full_cc() / m3_full_pitch_bend() 사용
//    - m3_process_msg: sustain CC64 등 m3_full_cc 내부에서도 처리되므로
//      sustain 페달 시각화는 별도 유지
// =============================================================================

/* MIDI 수신 락프리 링버퍼 */
#define MIDI_QUEUE_SIZE 128
typedef struct { uint8_t st, d1, d2; } midi_msg_t;
static midi_msg_t        s_midi_queue[MIDI_QUEUE_SIZE];
static volatile uint32_t s_midi_wptr = 0;
static volatile uint32_t s_midi_rptr = 0;
static volatile int      g_midi_thread_exit = 0;
static int               m3_fd_midi = -1;
static char              m3_dev_label[32] = "NOT FOUND";

static inline int midi_queue_push(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint32_t next = (s_midi_wptr + 1u) % MIDI_QUEUE_SIZE;
    if (next == s_midi_rptr) return 0;
    s_midi_queue[s_midi_wptr].st = st;
    s_midi_queue[s_midi_wptr].d1 = d1;
    s_midi_queue[s_midi_wptr].d2 = d2;
    MEM_BARRIER(); s_midi_wptr = next; return 1;
}

static inline int midi_queue_pop(midi_msg_t* out)
{
    MEM_BARRIER();
    if (s_midi_rptr == s_midi_wptr) return 0;
    *out = s_midi_queue[s_midi_rptr];
    s_midi_rptr = (s_midi_rptr + 1u) % MIDI_QUEUE_SIZE; return 1;
}

/* [LIGHT-M3-FIX] 이전 Mode/MIDI 입력이 남아 뒤늦게 발음되는 것을 방지 */
static inline void m3_midi_queue_clear(void)
{
    s_midi_wptr = 0;
    s_midi_rptr = 0;
    MEM_BARRIER();
}

static void m3_flush_midi_input(void)
{
    if (m3_fd_midi < 0) return;
    uint8_t dump[64];
    for (int i = 0; i < 16; i++) {
        ssize_t r = read(m3_fd_midi, dump, sizeof(dump));
        if (r <= 0) break;
    }
}

static void* midi_rx_thread(void* arg)
{
    (void)arg;
    struct sched_param sp = { .sched_priority = 85 };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    printf("[MIDI-RX] 스레드 시작 (SCHED_FIFO prio=85)\n");
    uint8_t buf[3] = { 0 }; int idx = 0; uint8_t rstate = 0;
    while (!g_midi_thread_exit) {
        MEM_BARRIER();
        int fd = m3_fd_midi;
        if (fd < 0) { usleep(5000); continue; }
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        if (poll(&pfd, 1, 5) <= 0) continue;
        uint8_t byte;
        if (read(fd, &byte, 1) <= 0) { usleep(1000); continue; }
        if (byte & 0x80u) {
            if (byte == 0xF8u || byte == 0xFEu || byte == 0xFAu || byte == 0xFBu || byte == 0xFCu) continue;
            rstate = byte; buf[0] = byte; idx = 1;
        }
        else {
            if (idx == 0 && rstate) { buf[0] = rstate; idx = 1; }
            if (idx > 0 && idx < 3) buf[idx++] = byte;
            if (idx == 3) { midi_queue_push(buf[0], buf[1], buf[2]); idx = 1; }
        }
    }
    printf("[MIDI-RX] 종료\n"); return NULL;
}

/* MIDI 건반 시각화 상수: 섹션 F 전방 선언 블록으로 이동 */
/* M3_IS_BLACK / M3_NOTE_NAMES: 건반 색상/이름 상수 (섹션 F 앞으로 이동한 M3_* 상수와 세트) */

static const char* const M3_MIDI_PATHS[] = {
    "/dev/snd/midiC0D0","/dev/snd/midiC1D0","/dev/snd/midiC2D0",
    "/dev/midi0","/dev/midi1", NULL
};

static int m3_open_midi(void)
{
    for (int i = 0; M3_MIDI_PATHS[i]; i++) {
        int fd = open(M3_MIDI_PATHS[i], O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            strncpy(m3_dev_label, M3_MIDI_PATHS[i], sizeof(m3_dev_label) - 1);
            m3_dev_label[sizeof(m3_dev_label) - 1] = '\0';
            printf("[M3] MIDI: %s\n", m3_dev_label);
            return fd;
        }
    }
    strncpy(m3_dev_label, "NOT FOUND", sizeof(m3_dev_label));
    return -1;
}

static void m3_draw_key(int oct_row, int key_in_oct, int pressed)
{
    uint16_t x = (uint16_t)(M3_KEY_AREA_X + key_in_oct * M3_KEY_W);
    uint16_t y = (uint16_t)(M3_ROW_Y_START + oct_row * M3_ROW_H + 1);
    uint16_t color = pressed
        ? (M3_IS_BLACK[key_in_oct] ? COLOR_YELLOW : COLOR_CYAN)
        : (M3_IS_BLACK[key_in_oct] ? 0x2104u : COLOR_DKGREY);
    lcd_fill_rect(x, y, (uint16_t)(M3_KEY_W - 1), (uint16_t)M3_KEY_H, color);
}

static void m3_draw_octave_row(int r)
{
    uint16_t y = (uint16_t)(M3_ROW_Y_START + r * M3_ROW_H);
    lcd_fill_rect(0, y, LCD_W, (uint16_t)M3_ROW_H, COLOR_BLACK);
    char lbl[4]; snprintf(lbl, sizeof(lbl), "C%d", r + 2);
    lcd_string(1, (uint16_t)(y + (M3_ROW_H - 8) / 2), lbl, COLOR_GREY, COLOR_BLACK);
    int base = M3_KEY_MIN + r * 12;
    for (int k = 0; k < 12; k++) {
        int note = base + k;
        m3_draw_key(r, k, (note <= 127) ? (int)m3.note_on[note] : 0);
    }
    lcd_fill_rect(0, (uint16_t)(y + M3_ROW_H - 1), LCD_W, 1, COLOR_DKGREY);
}

static void m3_draw_info(void)
{
    char buf[54], nstr[8] = "---";
    snprintf(buf, sizeof(buf), "%-28s", m3_dev_label);
    lcd_string(2, 32, buf, (m3_fd_midi >= 0) ? COLOR_DKGREEN : COLOR_RED, COLOR_BLACK);
    if (m3.last_note <= 127) {
        int oct = (int)(m3.last_note / 12) - 1, pc = (int)(m3.last_note % 12);
        snprintf(nstr, sizeof(nstr), "%s%d", M3_NOTE_NAMES[pc], oct);
    }
    snprintf(buf, sizeof(buf), "SUS:%s POLY:%02d LAST:%-4s VEL:%3d",
        m3.sustain ? "ON " : "OFF", m3.poly_count, nstr, m3.last_vel);
    lcd_string(2, 40, buf, m3.sustain ? COLOR_ORANGE : COLOR_GREY, COLOR_BLACK);
}

static void m3_recount_poly(void)
{
    int c = 0; for (int i = 0; i < 128; i++) if (m3.note_on[i]) c++; m3.poly_count = (uint8_t)c;
}

/* [LIGHT-M3-FIX] forward declaration: note_off path calls this before the definition below. */
static void m3_idle_sound_guard(void);

static void m3_handle_note_on(uint8_t note, uint8_t vel)
{
    if (note > 127) return;
    m3.pending_off[note] = 0;
    if (!m3.note_on[note]) {
        m3.note_on[note] = 1;
        if (note >= M3_KEY_MIN && note <= M3_KEY_MAX) {
            int r = (note - M3_KEY_MIN) / 12, k = (note - M3_KEY_MIN) % 12;
            if (r < M3_OCT_COUNT) m3_draw_key(r, k, 1);
        }
    }
    m3.last_note = note; m3.last_vel = vel;
    m3_recount_poly(); m3_draw_info();

    /* LIGHT-CHAIN: SVF 제외. 실제 MIDI Note-On이 들어온 시점에만 PT2258을 연다. */
    pt2258_set_mute(0);
    m3_full_note_on(note, vel);     /* [V14] ym_synth_note_on → m3_full_note_on */
}

static void m3_handle_note_off(uint8_t note)
{
    if (note > 127) return;
    if (m3.sustain) { m3.pending_off[note] = 1; return; }
    if (m3.note_on[note]) {
        m3.note_on[note] = 0;
        if (note >= M3_KEY_MIN && note <= M3_KEY_MAX) {
            int r = (note - M3_KEY_MIN) / 12, k = (note - M3_KEY_MIN) % 12;
            if (r < M3_OCT_COUNT) m3_draw_key(r, k, 0);
        }
    }
    m3_recount_poly(); m3_draw_info();
    m3_full_note_off(note);         /* [V14] ym_synth_note_off → m3_full_note_off */
    if (m3.poly_count == 0 && !m3.sustain) {
        /* 팀원 요청: 건반 입력이 없을 때는 PT2258 gate를 닫아 잔류음 방지 */
        m3_idle_sound_guard();
    }
}

static void m3_handle_sustain(uint8_t val)
{
    uint8_t ns = (val >= 64) ? 1 : 0;
    if (ns == m3.sustain) return;
    m3.sustain = ns;
    if (!m3.sustain) {
        for (int i = 0; i < 128; i++) {
            if (!m3.pending_off[i]) continue;
            m3.pending_off[i] = 0;
            if (m3.note_on[i]) {
                m3.note_on[i] = 0;
                if (i >= M3_KEY_MIN && i <= M3_KEY_MAX) {
                    int r = (i - M3_KEY_MIN) / 12, k = (i - M3_KEY_MIN) % 12;
                    if (r < M3_OCT_COUNT) m3_draw_key(r, k, 0);
                }
            }
            m3_full_note_off((uint8_t)i);
        }
        m3_recount_poly();
        /* [FIX-M3-GATE] sustain 해제 시에도 mute 금지 (note_off와 동일한 이유) */
        // if (m3.poly_count == 0) pt2258_set_mute(1);
    }
    m3_draw_info();
}

/* [MIDI-EQ8] forward declarations */
static void midi_cc_eq8_preset(uint8_t val, int apply_commit);
static void midi_cc_eq8_enable(uint8_t val);
static void midi_cc_eq8_preview_trigger(uint8_t val);

static void m3_process_msg(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t mt = st & 0xF0u;
    if (mt == 0x90u) { if (d2 > 0) m3_handle_note_on(d1, d2); else m3_handle_note_off(d1); }
    else if (mt == 0x80u) { m3_handle_note_off(d1); }
    else if (mt == 0xB0u) {
        if (d1 == 64u) {
            m3_handle_sustain(d2);  /* sustain 시각화는 별도 유지 */
        }
        else if (d1 == 123u || d1 == 120u) {
            /* all notes off */
            m3_full_all_off();      /* [V14] ym_synth_all_off → m3_full_all_off */
            memset(m3.note_on, 0, sizeof(m3.note_on));
            memset(m3.pending_off, 0, sizeof(m3.pending_off));
            m3.sustain = 0; m3_recount_poly();
            for (int r = 0; r < M3_OCT_COUNT; r++) m3_draw_octave_row(r);
            m3_draw_info();
        }
        else {
            /* 나머지 CC는 m3_full_cc 로 전달 (modwheel, volume 등) */
            m3_full_cc(d1, d2);
            /* LFO / VCA 직접 제어도 유지 */
            if (d1 == 1u) {
                uint16_t dep = (uint16_t)((uint32_t)d2 * 0x01FFu / 127u);
                lfo_set_ch(0, adc_to_lfo_fcw(adc_buf[ADC_IDX_PITCH]), dep, 0u, 0u);
            }
            else if (d1 == 7u) {
                vca_set_makeup_checked((uint16_t)((uint32_t)d2 * Q15_ONE / 127u));
            }
            /* ── [MIDI-EQ8] EQ 8밴드 프리셋 MIDI CC 제어 ──────────────────────
             *  CC 80 (0x50): EQ8 프리셋 인덱스 선택 + HW 즉시 적용 (임시)
             *                val 0~127 → EQ8_PRESET_COUNT 범위로 선형 스케일.
             *                이후 CC81(val>=64)로 commit.
             *  CC 81 (0x51): EQ8 enable/disable 토글
             *                val 0~63 = disable(bypass), val 64~127 = enable(현재 idx 재적용)
             *  CC 82 (0x52): 현재 EQ8 상태로 YM2203 즉시 프리뷰 트리거
             *                val > 0 = 프리뷰 시작, val = 0 = 프리뷰 중단
             * ------------------------------------------------------------------ */
            else if (d1 == 0x50u) {   /* CC 80: EQ8 프리셋 선택 (임시 적용) */
                midi_cc_eq8_preset(d2, 0);
            }
            else if (d1 == 0x51u) {   /* CC 81: EQ8 enable/disable */
                midi_cc_eq8_enable(d2);
            }
            else if (d1 == 0x52u) {   /* CC 82: EQ8 + YM 즉시 프리뷰 트리거 */
                midi_cc_eq8_preview_trigger(d2);
            }
        }
    }
    else if (mt == 0xE0u) {
        /* 피치벤드 */
        int16_t pb_raw = (int16_t)(((uint32_t)d1 | ((uint32_t)d2 << 7)) - 8192);
        m3_full_pitch_bend(pb_raw); /* [V14] 신규 */
    }
}

/* [FIX-ISG] 모듈 수준 armed 변수: m3_state_reset() 보다 먼저 선언해야 컴파일 에러 없음.
 * m3_state_reset() 안에서 s_idle_guard_armed = 1u 로 재활성하므로
 * 선언이 반드시 함수 정의보다 앞에 위치해야 한다. */
static uint8_t s_idle_guard_armed = 1u;

static void m3_state_reset(void)
{
    memset(&m3, 0, sizeof(m3));
    m3.last_note = 0xFF;
    m3.sustain = 0;
    memset(m3.note_on, 0, sizeof(m3.note_on));
    memset(m3.pending_off, 0, sizeof(m3.pending_off));
    m3.poly_count = 0;
    m3_midi_queue_clear();
    s_idle_guard_armed = 1u;        /* [FIX-ISG] 재진입 시 idle guard 재활성 */
    m3_full_voice_reset();          /* [V14] ym_voice_reset → m3_full_voice_reset */
}

/* [LIGHT-M3-FIX] 무입력 잔류음 방지: YM/SSG 닫기.
 * PT2258 없는 환경에서도 안전: pt2258_write_bytes()는 fd<0이면 no-op. */
static void m3_force_silent_idle(const char* reason)
{
    (void)reason;
    pt2258_set_mute(1);     /* PT2258 없으면 no-op (fd_pt2258<0 → write_bytes 반환 -1) */
    for (int ch = 0; ch < 3; ch++) {
        ym2203_key_off((uint8_t)ch);
        ym2203_ssg_set_vol((uint8_t)ch, 0);
    }
    ym_write(YM_SSG_MIXER, 0x3Fu);  /* SSG tone/noise 모두 close */
    m3_state_reset();
}

/* note가 하나도 없을 때 출력 gate가 열린 상태로 방치되지 않도록 정리.
 * [FIX] static 지역변수 armed → 모듈 수준 s_idle_guard_armed 로 변경.
 * 이전 코드는 최초 한 번 작동 후 armed=0으로 굳어져 재진입 시 잔류음 방치. */
static void m3_idle_sound_guard(void)
{
    if (m3.poly_count == 0 && !m3.sustain) {
        if (s_idle_guard_armed) {
            for (int ch = 0; ch < 3; ch++) {
                ym2203_key_off((uint8_t)ch);
                ym2203_ssg_set_vol((uint8_t)ch, 0);
            }
            ym_write(YM_SSG_MIXER, 0x3Fu);
            pt2258_set_mute(1);
            s_idle_guard_armed = 0u;
        }
    }
    else {
        s_idle_guard_armed = 1u;
    }
}


/* ── Mode4 이펙터 세부 편집 forward declarations ─────────────────────
 * pre/post EQ 직접 편집, WARM/SVF raw parameter 편집 결과를
 * Mode3/Mode4 재적용 흐름에서도 유지하기 위한 전방 선언.
 * NOTE: eq_band_flush / eq_band_flush_post 는 파일 상단(B4-PT 전방 선언 블록)으로
 *       이동했으므로 여기서는 중복 선언하지 않음.
 */
static void m3_rec_draw_status(void);  /* forward decl: 정의는 mode_midivis_update 직전 */

static void mode_midivis_enter(void)
{
    printf("[M3] MIDI+SOUND 진입\n");

    /* ① 이전 세션 상태 완전 초기화 (note_on / sustain / pending_off / 채널 매핑) */
    m3_state_reset();

    /* ② g_full_preset 전체 HW 반영 (YM2203 + PRE EQ + VCA + POST EQ + DELAY + LFO) */
    full_preset_apply_hw();
    diag_apply_chain_policy("mode3_enter");

    /* [FIX] 딜레이 bypass 강제 OFF: full_preset_apply_hw가 bypass_off를 했지만
     * diag_apply_chain_policy 이후에도 명시적으로 보장 */
    if (DIAG_USE_DELAY && p_delay && g_full_preset.dly_preset != 0)
        analog_delay_bypass_off(&g_adly);

    printf("[M3] delay readback after enter: ");
    delay_readback();

    /* ③ PT2258: 진입 직후 unmute (pot.c에는 없었으나 현재 환경에 PT2258 있음)
     *    Note-On 기다리지 않고 바로 열어야 소리가 제때 나온다. */
    pt2258_set_mute(0);

    /* HW 상태 readback */
    vca_readback();
    delay_readback();

    if (m3_fd_midi >= 0) { close(m3_fd_midi); m3_fd_midi = -1; }
    m3_fd_midi = m3_open_midi();
    m3_flush_midi_input();

    lcd_clear(COLOR_BLACK);
    lcd_draw_header(COLOR_ORANGE, "  MIDI KBD+SOUND (mode=3)", COLOR_BLACK);

    /* 악기명 표시 ? g_full_preset.fm[] 기반 */
    char buf[54];
    {
        fm_ch_cfg_t* f0 = &g_full_preset.fm[0];
        fm_ch_cfg_t* f1 = &g_full_preset.fm[1];
        fm_ch_cfg_t* f2 = &g_full_preset.fm[2];
        const char* n0, * n1, * n2;
        n0 = (f0->use_full && !f0->edited && f0->inst_idx < FULL_INST_COUNT)
            ? FULL_INST_CATALOG[f0->inst_idx]->name
            : (f0->inst_idx < YM2203_PATCH_COUNT ? YM2203_PATCHES[f0->inst_idx].name : "???");
        n1 = (f1->use_full && !f1->edited && f1->inst_idx < FULL_INST_COUNT)
            ? FULL_INST_CATALOG[f1->inst_idx]->name
            : (f1->inst_idx < YM2203_PATCH_COUNT ? YM2203_PATCHES[f1->inst_idx].name : "???");
        n2 = (f2->use_full && !f2->edited && f2->inst_idx < FULL_INST_COUNT)
            ? FULL_INST_CATALOG[f2->inst_idx]->name
            : (f2->inst_idx < YM2203_PATCH_COUNT ? YM2203_PATCHES[f2->inst_idx].name : "???");
        snprintf(buf, sizeof(buf), "0:%-8s 1:%-8s 2:%-8s", n0, n1, n2);
    }
    lcd_string(2, 19, buf, COLOR_CYAN, COLOR_BLACK);

    m3_draw_info();
    lcd_fill_rect(0, 50, LCD_W, 2, COLOR_DKGREY);
    for (int r = 0; r < M3_OCT_COUNT; r++) m3_draw_octave_row(r);
    lcd_string(2, 219, "SW1:REC  SW2:STOP+SAVE  SW4:MENU", COLOR_DKGREY, COLOR_BLACK);
    lcd_string(2, 228, "long SW4:RESCAN  VOL:음량", COLOR_DKGREY, COLOR_BLACK);
    m3_rec_draw_status();
    if (m3_fd_midi < 0) lcd_string(2, 32, "NO MIDI - connect!", COLOR_RED, COLOR_BLACK);

    /* [FIX-REC2b] 재진입 시 lsw 초기화 플래그 세팅
     * static lsw가 이전 모드 스위치 값을 유지 → 첫 틱에서 SW1/SW2 엣지 오감지 방지 */
    g_m3_lsw_reset = 1;
}

/* ── REC 상태 LCD 표시 (y=210) ─────────────────────────────────────── */
/* REC 레이블(좌측 고정) + 상태 텍스트(우측) 분리 렌더.
 * REC 레이블: 녹음 중 → COLOR_RED, 대기 중 → COLOR_DKGREY
 * saved_toast: 저장 완료 1초 토스트 (호출자가 g_rec_saved_ts 설정) */
 /* g_rec_saved_ts 는 전역 선언으로 이동 (모드1/3 공유) */

static void m3_rec_draw_status(void)
{
    lcd_fill_rect(0, 210, LCD_W, 8, COLOR_BLACK);

    /* 좌측: REC 레이블 — 녹음 중이면 빨간색, 아니면 어두운 회색 */
    uint16_t rec_col = (g_rec_state == REC_RUNNING) ? COLOR_RED : COLOR_DKGREY;
    lcd_string(2, 210, "REC", rec_col, COLOR_BLACK);

    /* 우측: 상태 문구 */
    if (g_rec_state == REC_IDLE) {
        /* 저장 완료 토스트: 완료 시각으로부터 1초 이내면 표시 */
        if (g_rec_saved_ts.tv_sec != 0) {
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (now.tv_sec - g_rec_saved_ts.tv_sec) * 1000L
                + (now.tv_nsec - g_rec_saved_ts.tv_nsec) / 1000000L;
            if (elapsed_ms < 1000L) {
                lcd_string(26, 210, "SAVED!  SW1:시작", COLOR_GREEN, COLOR_BLACK);
                return;
            }
            g_rec_saved_ts.tv_sec = 0;  /* 토스트 만료 */
        }
        lcd_string(26, 210, "READY   SW1:시작", COLOR_DKGREY, COLOR_BLACK);
    }
    else if (g_rec_state == REC_RUNNING) {
        lcd_string(26, 210, "*** RECORDING ***  SW2:종료", COLOR_RED, COLOR_BLACK);
    }
    else {
        lcd_string(26, 210, "SAVING...", COLOR_YELLOW, COLOR_BLACK);
    }
}

static int mode_midivis_update(spi_packet_t* rx, spi_packet_t* tx)
{
    static uint8_t lsw = 0;
    static uint32_t sw4_hold = 0;
    static rec_state_t lrec = (rec_state_t)99;
    static uint32_t toast_refresh = 0;  /* 토스트 만료 폴링용 틱 카운터 */

    /* [FIX-REC2b] 모드 재진입 시 lsw를 현재 sw 값으로 초기화
     * → 진입 직후 첫 틱에서 SW1/SW2 엣지 오감지로 rec_start() 자동 호출 방지 */
    if (g_m3_lsw_reset) {
        lsw = rx->sw_status;
        sw4_hold = 0;
        g_m3_lsw_reset = 0;
    }

    tx->active_mode = ZYNQ_MODE_MIDIVIS;

    /* ── SW4 (EXIT): 짧게 → MENU, 길게(50tick) → MIDI 재스캔 ── */
    if (rx->sw_status == SW_EXIT) {
        sw4_hold++;
        if (sw4_hold == 50u) {
            m3_force_silent_idle("manual rescan");
            if (m3_fd_midi >= 0) { close(m3_fd_midi); m3_fd_midi = -1; }
            m3_fd_midi = m3_open_midi();
            m3_flush_midi_input();
            for (int r = 0; r < M3_OCT_COUNT; r++) m3_draw_octave_row(r);
            m3_draw_info();
        }
    }
    else {
        if (lsw == SW_EXIT && sw4_hold < 50u) {
            if (g_rec_state == REC_RUNNING) {
                /* 녹음 중 메뉴 복귀 차단 */
                lcd_fill_rect(0, 210, LCD_W, 8, COLOR_BLACK);
                lcd_string(2, 210, "REC", COLOR_RED, COLOR_BLACK);
                lcd_string(26, 210, "중지 후 이동  SW2:종료", COLOR_YELLOW, COLOR_BLACK);
            }
            else {
                sw4_hold = 0; lsw = rx->sw_status; return ZYNQ_MODE_MENU;
            }
        }
        sw4_hold = 0;
    }

    /* ── SW1: 녹음 시작 전용 (엣지 감지, 볼륨 역할 없음) ── */
    if (rx->sw_status == SW_VOL_UP && lsw != SW_VOL_UP) {
        if (g_rec_state == REC_IDLE) {
            rec_start();
            m3_rec_draw_status();
        }
    }

    /* ── SW2: 녹음 종료+저장 전용 (엣지 감지, 볼륨 역할 없음) ── */
    if (rx->sw_status == SW_VOL_DOWN && lsw != SW_VOL_DOWN) {
        if (g_rec_state == REC_RUNNING) {
            /* SAVING 즉시 표시 후 블로킹 저장 */
            lcd_fill_rect(0, 210, LCD_W, 8, COLOR_BLACK);
            lcd_string(2, 210, "REC", COLOR_DKGREY, COLOR_BLACK);
            lcd_string(26, 210, "SAVING...", COLOR_YELLOW, COLOR_BLACK);
            rec_stop_and_save();
            /* 저장 완료 시각 기록 → 토스트 표시 트리거 */
            clock_gettime(CLOCK_MONOTONIC, &g_rec_saved_ts);
            toast_refresh = 0;
            m3_rec_draw_status();
        }
    }

    /* ── REC 상태 변화 시 LCD 갱신 ── */
    if (g_rec_state != lrec) {
        lrec = g_rec_state;
        m3_rec_draw_status();
        toast_refresh = 0;
    }

    /* ── 토스트 만료 갱신 (1초 후 SAVED! → READY 로 자동 전환) ── */
    if (g_rec_saved_ts.tv_sec != 0 && g_rec_state == REC_IDLE) {
        if (++toast_refresh >= 20u) {  /* ~20틱마다 체크 */
            toast_refresh = 0;
            m3_rec_draw_status();      /* 만료됐으면 내부에서 ts 초기화 후 READY 표시 */
        }
    }

    lsw = rx->sw_status;

    /* ── ADC 가변저항 스테레오 볼륨 (모드 2와 동일: VOL→L, PITCH→R) ── */
    {
        uint8_t prev_l = g_pt2258_att_l_db, prev_r = g_pt2258_att_r_db;
        pt2258_update_stereo_from_adc(adc_buf[ADC_IDX_VOL], adc_buf[ADC_IDX_PITCH]);
        if (prev_l != g_pt2258_att_l_db || prev_r != g_pt2258_att_r_db) {
            char vbuf[32];
            snprintf(vbuf, sizeof(vbuf), "L:-%2udB  R:-%2udB",
                (unsigned)g_pt2258_att_l_db, (unsigned)g_pt2258_att_r_db);
            lcd_string(4, 219, vbuf, COLOR_CYAN, COLOR_BLACK);
        }
    }

    lfo_set_fcw_checked(0, adc_to_lfo_fcw(adc_buf[ADC_IDX_PITCH]));
    m3_full_tick();

    /* ── MIDI 메시지 처리 ── */
    midi_msg_t msg;
    while (midi_queue_pop(&msg)) m3_process_msg(msg.st, msg.d1, msg.d2);
    m3_idle_sound_guard();

    /* ── MIDI 장치 자동 재연결 ── */
    if (m3_fd_midi < 0) {
        static uint32_t rs = 0;
        if (++rs >= 100u) {
            rs = 0; m3_fd_midi = m3_open_midi();
            if (m3_fd_midi >= 0) {
                m3_state_reset();
                m3_flush_midi_input();
                fx_init_sound_guaranteed();
                full_preset_apply_hw();
                pt2258_set_mute(0);
                for (int r = 0; r < M3_OCT_COUNT; r++) m3_draw_octave_row(r);
                m3_draw_info();
            }
        }
    }
    return ZYNQ_MODE_MIDIVIS;
}

static void mode_midivis_cleanup(void)
{
    /* [REC 안전 종료] 녹음 중 모드 전환 시 강제 중단 + 저장 */
    if (g_rec_state != REC_IDLE) {
        printf("[M3] cleanup: 녹음 강제 종료 → WAV 저장\n");
        rec_stop_and_save();
    }

    /* [FIX-M3C1] PT2258 즉시 mute — 오디오 게이트를 먼저 닫아 팝/잔류음 방지 */
    pt2258_set_mute(1);
    m3_full_all_off();
    ym_silence();
    /* [FIX-M3C2] SSG Mixer 닫기: ym_silence()만으로는 tone/noise 게이트가 남음 */
    if (p_ym) ym_write(YM_SSG_MIXER, 0x3Fu);
    lfo_stop_all();
    vca_drain();
    vca_bypass();
    /* [FIX-M3C3] 상태 완전 초기화: 다음 모드3 진입 시 clean slate 보장 */
    m3_state_reset();
}

// =============================================================================
//  [섹션 I]  모드 4: INST SETUP  (mode4_instsetup_v2 전면 교체)
//
//  FSM 계층:
//    M4_TOP        메인 메뉴 8항목
//     ├─ M4_YM      YM2203 채널 선택 (FM 0~2 / PSG 0~2)
//     │   ├─ M4_FM_INST  악기 선택 (full_inst_t or raw patch)
//     │   │   └─ M4_FM_OP    OP 세밀편집
//     │   └─ M4_PSG_CH  PSG 채널 편집
//     └─ M4_HARMONY 화성학 엔진 설정 서브메뉴
//
//  조이스틱 규칙:
//    JOY-Y  : 커서 상하 / 악기 ±1
//    JOY-X  : 값 ±1(또는 ±5), 그룹 전환, 롱프레스=FM 편집 모드 전환
//    SW3(3) : 확정 / 서브메뉴 진입 / ON-OFF 토글
//    SW4(4) : 나가기 / 상위 복귀
//    SW5(5) : 취소 / 스냅샷 복원
// =============================================================================

/* ── 2. 모드 4 상태(State) 확장 및 업데이트 스위치 추가
모드 4 구조체에 VCA와 EQ를 세부 설정할 수 있는 상태를 추가합니다.
[수정 위치 3] m4_state_t 열거형 수정 ── */
/* ── FSM 상태 ── */
typedef enum {
    M4_TOP = 0,
    M4_YM_MODE_SEL,    /* YM 입력 모드 선택: 1ch 독립 vs 6ch VGM 프리셋 */
    M4_YM,
    M4_FM_INST,
    M4_FM_OP,
    M4_PSG_CH,
    M4_HARMONY,
    M4_VCA_EDIT,           /* VCA 세부 설정 화면 */
    M4_PRE_EQ_MODE_SEL,   /* PRE EQ 모드 선택: SW1=프리셋 / SW2=직접편집 */
    M4_PRE_EQ_PRESET,      /* PRE EQ 프리셋 선택 화면  (→M4_PRE_EQ_EDIT) */
    M4_PRE_EQ_EDIT,        /* PRE EQ 밴드별 파라미터 편집 화면 */
    M4_POST_EQ_MODE_SEL,  /* POST EQ 모드 선택: SW1=프리셋 / SW2=직접편집 */
    M4_POST_EQ_PRESET,     /* POST EQ 프리셋 선택 화면 (→M4_POST_EQ_EDIT) */
    M4_POST_EQ_EDIT,       /* POST EQ 밴드별 파라미터 편집 화면 */
    M4_EQ8_PRESET,         /* PRE 4밴드 + POST 4밴드 동시 적용 8밴드 통합 프리셋 */
    M4_DELAY_EDIT,         /* analog_delay 세부 설정 화면 */
} m4_state_t;

/* ── TOP 메뉴 인덱스 ──
 * [LIGHT-CHAIN] 앱/Mode4 메뉴도 남은 체인만 노출한다.
 * YM2203 -> PRE EQ -> VCA -> POST EQ -> DELAY -> APPLY -> EXIT
 */
#define M4_T_YM        0
#define M4_T_HARMONY   1   /* YM2203 발음/화성학 설정: 별도 이펙터 IP가 아니므로 유지 */
#define M4_T_EQ        2   /* pre_eq_4band */
#define M4_T_VCA       3   /* vca_compressor */
#define M4_T_POST_EQ   4   /* post_eq_4band */
 /* [M4-DELAY] DELAY를 TOP 메뉴에 정식 포함
  * 신호 경로: YM2203 → HARMONY → PRE EQ → VCA → POST EQ → DELAY
  * Stage4(DIAG_CHAIN_STAGE=4) 이상에서 활성화. */
#define M4_T_DELAY     5   /* analog_delay ? POST EQ 다음 단계 */
#define M4_T_EXIT      6   /* 최종 저장 후 메뉴 복귀 */
#define M4_T_COUNT     7
  /* 제외/미사용 상수 유지 (호환성 및 기존 switch case 보존) */
#define M4_T_APPLY     106 /* Quick APPLY 제거: 직접 선택 불가 */
#define M4_T_LFO       107
#define M4_T_PT2258    108

static const char* const M4_TOP_LABELS[M4_T_COUNT] = {
    "YM2203 SET ","HARMONY    ","PRE EQ     ","VCA        ",
    "POST EQ    ","DELAY      ","EXIT       "
};
static const uint16_t M4_TOP_COLORS[M4_T_COUNT] = {
    0xFD20u, 0x03E0u, 0xF81Fu, 0x4A10u,
    0xFFE0u, 0x07FFu,  /* DELAY: cyan */
    0xF800u
};

/* ── HARMONY 서브메뉴 항목 ── */
#define M4_H_VM_ON      0
#define M4_H_VM_CHORD   1
#define M4_H_JI_ON      2
#define M4_H_PB_SEMI    3
#define M4_H_KD_ON      4
#define M4_H_KD_MINNOTE 5
#define M4_H_KEY_ROOT   6
#define M4_H_SCALE      7
#define M4_H_CP_ON      8
#define M4_H_SUSP_ON    9
#define M4_H_VEL_CAR   10
#define M4_H_VEL_MOD   11
#define M4_H_SWING     12
#define M4_H_NLFO      13
#define M4_H_NLFO_RAND 14
#define M4_H_PORTA_MODE 15
#define M4_H_PORTA_TICK 16
#define M4_H_COUNT     17

static const char* const M4_H_LABELS[M4_H_COUNT] = {
    "VOICE MGR  ","VM AUTO CRD","JI(순정률) ","PB반음범위 ",
    "KEY DETECT ","KD MIN NOTE","키 루트    ","음계       ",
    "대위법     ","계류음     ","VEL 캐리어 ","VEL 모듈  ",
    "스윙비율   ","농음 LFO   ","농음 랜덤  ","포르타모드 ","포르타틱  "
};
static const uint16_t M4_H_COLORS[M4_H_COUNT] = {
    0x867Fu,0x867Fu,0x07FFu,0x07FFu,
    0xFD20u,0xFD20u,0xFD20u,0xFD20u,
    0xF81Fu,0xF81Fu,0x4FE0u,0x4FE0u,
    0x39E7u,0x4A10u,0x4A10u,0xFFE0u,0xFFE0u
};
static const uint8_t M4_H_MAX[M4_H_COUNT] = {
    1,1,1,12, 1,8,11,8, 1,1,8,8, 49,5,20, 4,30
};

static const char* const NOTE_NAMES[12] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};
static const char* const SCALE_NAMES[9] = {
    "Major","NatMin","Dorian","Mixo","Lydian","Phryg","Locrian","평조","계면"
};
static const char* const NLFO_NAMES[6] = {
    "황(HWANG)","태(TAE)","중(JOONG)","임(IM)","남(NAM)","OFF"
};
static const char* const PORTA_MODE_NAMES[5] = {
    "SLIDE","UP추성","DOWN퇴성","BEND_OFF꺾기","EXP지수"
};

/* ── OP 파라미터 ── */
#define M4_OP_P_COUNT  13
static const char* const M4_OP_P_NAMES[M4_OP_P_COUNT] = {
    "ALG","FB ","DT ","MUL","TL ","RS ","AR ","AM ","DR ","SR ","SL ","RR ","SSG"
};
static const uint8_t M4_OP_P_MAX[M4_OP_P_COUNT] = {
    7,7,7,15,127,3,31,1,31,31,15,15,15
};

/* ── 모드 4 컨텍스트 ── */
typedef struct {
    m4_state_t      state;

    full_preset_t   edit;
    full_preset_t   snap;

    int             top_cur;
    uint8_t         apply_flash;
    uint32_t        flash_cnt;

    int             ym_cur;         /* 0~2=FM, 3~5=PSG */

    int             fm_ch;
    uint8_t         fm_inst_cur;    /* 카탈로그 인덱스 (use_full=1) */
    uint8_t         fm_raw_cur;     /* raw patch 인덱스 (use_full=0) */
    uint8_t         fm_edit_mode;   /* 0=full_inst_t  1=raw patch */
    uint32_t        jx_hold_cnt;    /* JOY-X 롱프레스 카운터 */

    uint8_t         op_sel;
    int             op_param_cur;
    ym2203_patch_t  op_backup;

    int             psg_ch;

    int             hm_cur;

    /* Mode4 effect-detail editors */
    uint8_t         eq_target;       /* 0=pre_eq, 1=post_eq */
    uint8_t         vca_param_cur;   /* vca raw parameter cursor */
    uint8_t         dly_param_cur;   /* analog_delay parameter cursor */

    struct timespec preview_ts;
    int             preview_active;
    uint8_t         preview_ch;
    int             preview_is_psg;

    /* ADC 홀드 감지 (화면 전환 후 재동기화용) */
    uint16_t        last_adc_vol;
    uint16_t        last_adc_pitch;
    uint8_t         adc_armed;      /* 1 = 현재 화면 ADC 동기화 완료 */

    uint8_t         prev_sw;
    uint8_t         joy_y_mv;
    uint8_t         joy_x_mv;
} m4_ctx_t;

static m4_ctx_t M4;

// ─────────────────────────────────────────────────────────────────────────────
//  §I-U  유틸리티
// ─────────────────────────────────────────────────────────────────────────────

static inline const char* m4_inst_name(uint8_t idx)
{
    return (idx < FULL_INST_COUNT) ? FULL_INST_CATALOG[idx]->name : "???";
}

static inline const char* m4_ssg_name(uint8_t idx)
{
    return (idx < SSG_COUNT) ? YM2203_SSG_PATCHES[idx].name : "???";
}

static inline const char* m4_raw_patch_name(uint8_t idx)
{
    return (idx < YM2203_PATCH_COUNT) ? YM2203_PATCHES[idx].name : "???";
}

static void m4_deadzone(void)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    if (jy >= JOY_DEAD_LO && jy <= JOY_DEAD_HI) M4.joy_y_mv = 0;
    if (jx >= JOY_DEAD_LO && jx <= JOY_DEAD_HI) { M4.joy_x_mv = 0; M4.jx_hold_cnt = 0; }
}

/* 프리뷰 타이머 ? 350ms 후 자동 Key-Off */
static void m4_preview_tick(void)
{
    if (!M4.preview_active) return;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    long ms = (now.tv_sec - M4.preview_ts.tv_sec) * 1000L
        + (now.tv_nsec - M4.preview_ts.tv_nsec) / 1000000L;
    if (ms >= 350L) {
        if (M4.preview_is_psg) ym2203_ssg_set_vol(M4.preview_ch, 0);
        else {
            ym2203_key_off(M4.preview_ch);
            for (int p = 0; p < 3; p++) ym2203_ssg_set_vol((uint8_t)p, 0);
        }
        pt2258_set_mute(1);      /* [PT2258] preview 종료 → mute */
        M4.preview_active = 0;
    }
}

static void m4_preview_stop(void)
{
    if (!M4.preview_active) return;
    if (M4.preview_is_psg) ym2203_ssg_set_vol(M4.preview_ch, 0);
    else {
        ym2203_key_off(M4.preview_ch);
        for (int p = 0; p < 3; p++) ym2203_ssg_set_vol((uint8_t)p, 0);
    }
    /* [FIX-M4-PREVIEW] stop 시 PT2258 mute 제거.
     * 조이스틱으로 악기를 빠르게 스크롤할 때 stop→start 연속 호출 시
     * mute가 걸린 채 다음 preview_fm_full이 실행되면 소리가 안 남.
     * mute는 mode_instsetup_cleanup()에서만 수행. */
    M4.preview_active = 0;
}

/* fi_note_on_v() 기반 프리뷰 ? full_inst_t 전체(FM+SSG+LFO)를 한 번에 적용.
 * fi_note_on_v() 내부에서 ym2203_cache_reset()을 수행하므로
 * 악기를 바꿀 때마다 이전 패치 레지스터가 완전히 초기화됨.
 * ch 고정(내부 ch=0)은 프리뷰 목적에 문제없음. [RESTORE-fi_note_on_v] */
static void m4_preview_fm_full(uint8_t ch, uint8_t inst_idx)
{
    if (!p_ym || inst_idx >= FULL_INST_COUNT) return;
    m4_preview_stop();
    const full_inst_t* fi = FULL_INST_CATALOG[inst_idx];
    /* [FIX-M4-CHAIN] preview 전 체인 활성화 보장:
     * mode_instsetup_enter()에서 delay bypass=on, vca_bypass가 남아있을 수 있음.
     * VCA preset이 0(bypass)이어도 일단 preset을 재적용해 레지스터 안정화,
     * DELAY는 apply_dly_custom_from_full_preset()으로 bypass 해제. */
    if (DIAG_USE_VCA) apply_vca_preset(M4.edit.vca_preset);
    apply_lfo_then_delay();   /* LFO 먼저 → bypass OFF */
    diag_apply_chain_policy("m4_preview_full");
    pt2258_set_mute(0);
    /* [FIX-M4-PREVIEW-PATCH] fi_note_on_v() sets TL only, no patch_apply.
     * Without this, changing instruments sounds identical (FM OP params unchanged). */
    {
        uint8_t _n = fi->fm_n; if (_n > 3u) _n = 3u;
        for (uint8_t _i = 0; _i < _n; _i++)
            ym2203_patch_apply(&fi->fm[_i], _i);
    }
    fi_note_on_v(fi, 60, 100, 20, 6);
    M4.preview_ch = ch; M4.preview_is_psg = 0;
    clock_gettime(CLOCK_MONOTONIC, &M4.preview_ts);
    M4.preview_active = 1;
}

static void m4_preview_fm_raw(uint8_t ch, uint8_t raw_idx)
{
    if (!p_ym || raw_idx >= YM2203_PATCH_COUNT) return;
    m4_preview_stop();
    ym2203_key_off(ch);
    /* [FIX-M4-CHAIN] chain 활성화 */
    if (DIAG_USE_VCA) apply_vca_preset(M4.edit.vca_preset);
    apply_lfo_then_delay();   /* LFO 먼저 → bypass OFF */
    diag_apply_chain_policy("m4_preview_raw");
    pt2258_set_mute(0);          /* [PT2258] YM 신호 직전 unmute */
    ym2203_patch_apply_vel(&YM2203_PATCHES[raw_idx], ch, 100);
    ym2203_set_note(ch, 60);
    ym2203_key_on(ch, 0x0F);
    M4.preview_ch = ch; M4.preview_is_psg = 0;
    clock_gettime(CLOCK_MONOTONIC, &M4.preview_ts);
    M4.preview_active = 1;
}

static void m4_preview_fm_op(uint8_t ch)
{
    if (!p_ym) return;
    m4_preview_stop();
    ym2203_key_off(ch);
    /* [FIX-M4-CHAIN] chain 활성화 */
    if (DIAG_USE_VCA) apply_vca_preset(M4.edit.vca_preset);
    apply_lfo_then_delay();   /* LFO 먼저 → bypass OFF */
    diag_apply_chain_policy("m4_preview_op");
    pt2258_set_mute(0);          /* [PT2258] YM 신호 직전 unmute */
    ym2203_patch_apply_vel(&M4.edit.fm[ch].patch_override, ch, 100);
    ym2203_set_note(ch, 60);
    ym2203_key_on(ch, 0x0F);
    M4.preview_ch = ch; M4.preview_is_psg = 0;
    clock_gettime(CLOCK_MONOTONIC, &M4.preview_ts);
    M4.preview_active = 1;
}

static void m4_preview_psg(uint8_t ch, uint8_t pidx)
{
    if (!p_ym || pidx >= SSG_COUNT) return;
    m4_preview_stop();
    /* [FIX-M4-CHAIN] chain 활성화 */
    if (DIAG_USE_VCA) apply_vca_preset(M4.edit.vca_preset);
    apply_lfo_then_delay();   /* LFO 먼저 → bypass OFF */
    diag_apply_chain_policy("m4_preview_psg");
    pt2258_set_mute(0);          /* [PT2258] YM 신호 직전 unmute */
    ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pidx], (uint8_t)(1u << ch));
    ym2203_ssg_set_note(ch, 60);
    M4.preview_ch = ch; M4.preview_is_psg = 1;
    clock_gettime(CLOCK_MONOTONIC, &M4.preview_ts);
    M4.preview_active = 1;
}

/* =============================================================================
 * [MIDI-EQ8-PREVIEW]  EQ 8밴드 프리셋 MIDI CC 제어 + 프리뷰
 *
 *  ▸ m4_preview_vgm6_current()  : Mode4 VGM6 악기 기준 즉시 프리뷰
 *  ▸ m4_preview_current_ym()    : 현재 M4 악기 기준 EQ 변화 프리뷰
 *  ▸ midi_cc_eq8_preset()       : MIDI CC로 EQ8 프리셋 선택 + 즉시 HW 적용
 *  ▸ midi_note_eq8_preview()    : MIDI Note-On 시 EQ8 적용 상태로 프리뷰
 *
 *  CC 매핑 (m3_process_msg → else 분기에 추가됨):
 *    CC 80 (0x50)  EQ8 프리셋 인덱스 선택  (0-127 → 0..EQ8_PRESET_COUNT-1 스케일)
 *    CC 81 (0x51)  EQ8 enable toggle        (0~63=off, 64~127=on)
 *    CC 82 (0x52)  EQ8 프리뷰 트리거        (velocity 0이면 preview_stop)
 *
 *  Note-On 시 동작:
 *    Mode 3(MIDI KBD) 에서 CC80/81로 EQ8을 설정한 뒤 건반을 치면
 *    apply_eq8_idx()가 이미 HW에 적용된 상태로 YM2203이 발음한다.
 *    별도 Mode 4 진입 없이 실시간으로 EQ 변화를 청취할 수 있다.
 * ============================================================================= */

 /* [MIDI-EQ8] m4_preview_vgm6_current: pot_v8.c와 동일 경로 (midi.c 누락분 보완) */
static void m4_preview_vgm6_current(void)
{
    if (!p_ym || FULL6_VGM_CATALOG_COUNT == 0u) return;
    if (M4.edit.vgm6_inst_idx >= FULL6_VGM_CATALOG_COUNT)
        M4.edit.vgm6_inst_idx = 0u;

    m4_preview_stop();
    ym2203_all_notes_off();
    ym2203_ssg_silence_all();
    ym2203_cache_reset();

    if (DIAG_USE_VCA) apply_vca_preset(M4.edit.vca_preset);
    if (DIAG_USE_DELAY) apply_dly_custom_from_full_preset();
    diag_apply_chain_policy("m4_preview_vgm6");

    pt2258_set_mute(0);
    full6_vgm_note_on(M4.edit.vgm6_inst_idx, 60, 100);
    M4.preview_ch = 0;
    M4.preview_is_psg = 0;
    clock_gettime(CLOCK_MONOTONIC, &M4.preview_ts);
    M4.preview_active = 1;
}

/* [MIDI-EQ8] m4_preview_current_ym: EQ/프리셋 변경 시 현재 YM 악기로 즉시 프리뷰
 * CH_MODE_VGM6이면 VGM6 경로, 그 외에는 FM full/raw 경로를 사용한다. */
static void m4_preview_current_ym(void)
{
    if (M4.edit.ch_mode == CH_MODE_VGM6) {
        m4_preview_vgm6_current();
        return;
    }
    fm_ch_cfg_t* fc = &M4.edit.fm[0];
    if (fc->use_full && fc->inst_idx < FULL_INST_COUNT)
        m4_preview_fm_full(0, fc->inst_idx);
    else if (fc->inst_idx < YM2203_PATCH_COUNT)
        m4_preview_fm_raw(0, fc->inst_idx);
    else
        m4_preview_fm_full(0, 0);
}

/* [MIDI-EQ8] MIDI CC80 : EQ8 프리셋 인덱스 선택 + HW 즉시 적용
 *   val 0~127 → 0..EQ8_PRESET_COUNT-1 로 선형 스케일.
 *   apply_commit=1 이면 g_full_preset 확정, 0이면 임시(프리뷰용).
 */
static void midi_cc_eq8_preset(uint8_t val, int apply_commit)
{
    if (!p_pre_eq || !p_post_eq) return;
    uint8_t idx = (uint8_t)((uint32_t)val * (EQ8_PRESET_COUNT - 1u) / 127u);
    if (idx >= EQ8_PRESET_COUNT) idx = (uint8_t)(EQ8_PRESET_COUNT - 1u);

    /* HW 즉시 적용 */
    apply_eq8_idx(p_pre_eq, p_post_eq, idx);

    if (apply_commit) {
        g_full_preset.eq8_preset = idx;
        g_full_preset.eq8_enabled = 1u;
        M4.edit.eq8_preset = idx;
        M4.edit.eq8_enabled = 1u;
        M4.snap.eq8_preset = idx;
        printf("[MIDI-EQ8] CC80 commit: idx=%u %s\n",
            (unsigned)idx, EQ8_PRESET_NAMES[idx]);
    }
    else {
        printf("[MIDI-EQ8] CC80 preview: idx=%u %s\n",
            (unsigned)idx, EQ8_PRESET_NAMES[idx]);
    }
}

/* [MIDI-EQ8] MIDI CC81 : EQ8 enable/disable HW 토글
 *   val 0~63 = disable (bypass), 64~127 = enable (현재 idx 재적용)
 */
static void midi_cc_eq8_enable(uint8_t val)
{
    uint8_t en = (val >= 64u) ? 1u : 0u;
    g_full_preset.eq8_enabled = en;
    M4.edit.eq8_enabled = en;
    if (en) {
        uint8_t idx = g_full_preset.eq8_preset;
        if (idx >= EQ8_PRESET_COUNT) idx = 0u;
        if (p_pre_eq && p_post_eq) apply_eq8_idx(p_pre_eq, p_post_eq, idx);
        printf("[MIDI-EQ8] CC81 EQ8 ENABLED idx=%u %s\n",
            (unsigned)idx, EQ8_PRESET_NAMES[idx]);
    }
    else {
        /* disable → bypass: 인덱스 0이 EQ8_BYPASS */
        if (p_pre_eq && p_post_eq) apply_eq8_idx(p_pre_eq, p_post_eq, 0u);
        printf("[MIDI-EQ8] CC81 EQ8 DISABLED (bypass)\n");
    }
}

/* [MIDI-EQ8] MIDI CC82 : EQ8 적용 상태로 즉시 프리뷰 트리거
 *   val > 0 → m4_preview_current_ym() 호출 (현재 M4 악기 + 현재 EQ8 상태)
 *   val = 0 → m4_preview_stop()
 *   Mode 3(MIDI KBD)에서 사용하면 건반 입력 없이도 EQ 변화를 청취할 수 있다.
 */
static void midi_cc_eq8_preview_trigger(uint8_t val)
{
    if (val == 0u) {
        m4_preview_stop();
        return;
    }
    /* EQ8 적용 상태 보장: 현재 preset을 HW에 재적용 */
    if (g_full_preset.eq8_enabled && p_pre_eq && p_post_eq) {
        uint8_t idx = g_full_preset.eq8_preset;
        if (idx >= EQ8_PRESET_COUNT) idx = 0u;
        apply_eq8_idx(p_pre_eq, p_post_eq, idx);
    }
    m4_preview_current_ym();
}

/* OP 파라미터 포인터 */
static uint8_t* m4_op_ptr(int p)
{
    ym2203_patch_t* ep = &M4.edit.fm[M4.fm_ch].patch_override;
    ym2203_op_t* op = &ep->ops[M4.op_sel];
    switch (p) {
    case 0: return &ep->ALG; case 1: return &ep->FB;
    case 2: return &op->DT;  case 3: return &op->MUL;
    case 4: return &op->TL;  case 5: return &op->RS;
    case 6: return &op->AR;  case 7: return &op->AM;
    case 8: return &op->DR;  case 9: return &op->SR;
    case 10:return &op->SL;  case 11:return &op->RR;
    case 12:return &op->SSGEG;
    default:return &op->AR;
    }
}

/* HARMONY 파라미터 포인터 */
static uint8_t* m4_hm_ptr(int p)
{
    harmony_cfg_t* h = &M4.edit.harmony;
    switch (p) {
    case M4_H_VM_ON:      return &h->voice_mgr_on;
    case M4_H_VM_CHORD:   return &h->vm_chord_auto;
    case M4_H_JI_ON:      return &h->ji_on;
    case M4_H_PB_SEMI:    return &h->pb_semitones;
    case M4_H_KD_ON:      return &h->key_detect_on;
    case M4_H_KD_MINNOTE: return &h->kd_min_notes;
    case M4_H_KEY_ROOT:   return &h->key_root;
    case M4_H_SCALE:      return &h->scale_type;
    case M4_H_CP_ON:      return &h->counterpoint_on;
    case M4_H_SUSP_ON:    return &h->suspension_on;
    case M4_H_VEL_CAR:    return &h->vel_car_ratio;
    case M4_H_VEL_MOD:    return &h->vel_mod_ratio;
    case M4_H_SWING:      return &h->swing_ratio;
    case M4_H_NLFO:       return &h->nlfo_preset;
    case M4_H_NLFO_RAND:  return &h->nlfo_rand_range;
    case M4_H_PORTA_MODE: return &h->porta_mode;
    case M4_H_PORTA_TICK: return &h->porta_ticks;
    default:              return &h->voice_mgr_on;
    }
}

/* HARMONY 값 문자열 */
static void m4_hm_val_str(int p, char* out, int sz)
{
    uint8_t v = *m4_hm_ptr(p);
    switch (p) {
    case M4_H_VM_ON:
    case M4_H_VM_CHORD:
    case M4_H_JI_ON:
    case M4_H_KD_ON:
    case M4_H_CP_ON:
    case M4_H_SUSP_ON:
        snprintf(out, sz, "%s", v ? "ON " : "OFF"); break;
    case M4_H_KEY_ROOT:
        snprintf(out, sz, "%s(%d)", NOTE_NAMES[v % 12], v); break;
    case M4_H_SCALE:
        snprintf(out, sz, "%s", (v < 9) ? SCALE_NAMES[v] : "???"); break;
    case M4_H_NLFO:
        snprintf(out, sz, "%s", (v < 5) ? NLFO_NAMES[v] : NLFO_NAMES[5]); break;
    case M4_H_PORTA_MODE:
        snprintf(out, sz, "%s", (v < 5) ? PORTA_MODE_NAMES[v] : "???"); break;
    case M4_H_PORTA_TICK:
        if (v == 0) snprintf(out, sz, "OFF");
        else snprintf(out, sz, "%d틱", v);
        break;
    case M4_H_PB_SEMI:
        snprintf(out, sz, "+/-%d반음", v); break;
    case M4_H_VEL_CAR: case M4_H_VEL_MOD:
        snprintf(out, sz, "%d/8", v); break;
    case M4_H_SWING:
        if (v == 0) snprintf(out, sz, "STRAIGHT");
        else snprintf(out, sz, "%d%%", v);
        break;
    default:
        snprintf(out, sz, "%d", v); break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  §I-D  LCD 드로우
// ─────────────────────────────────────────────────────────────────────────────

/* ── TOP ── */
static void m4_draw_top_row(int idx, int sel)
{
    /* [FIX-M4-TOP-VISIBLE]
     * LCD 아래로 밀어냈다. 18px compact row로 11개 항목을 전부 표시한다.
     */
    const int row_h = 18;
    int y = 20 + idx * row_h;
    if (y > (LCD_H - row_h - 2)) return;
    uint16_t bg = sel ? 0x2104u : 0x0000u;
    lcd_fill_rect(0, (uint16_t)y, LCD_W, row_h, bg);
    lcd_string(2, (uint16_t)(y + 5), sel ? "->" : "  ", COLOR_WHITE, bg);
    lcd_string(18, (uint16_t)(y + 5), M4_TOP_LABELS[idx], M4_TOP_COLORS[idx], bg);

    char val[48] = "";
    switch (idx) {
    case M4_T_YM: {
        char s[3][10]; memset(s, 0, sizeof(s));
        for (int c = 0; c < 3; c++) {
            fm_ch_cfg_t* fc = &M4.edit.fm[c];
            if (fc->edited) snprintf(s[c], 10, "[E]");
            else if (!fc->use_full) snprintf(s[c], 10, "r%02d", fc->inst_idx);
            else snprintf(s[c], 10, "%.6s", m4_inst_name(fc->inst_idx));
        }
        snprintf(val, sizeof(val), "%s/%s/%s", s[0], s[1], s[2]);
        break;
    }
    case M4_T_HARMONY: {
        harmony_cfg_t* h = &M4.edit.harmony;
        snprintf(val, sizeof(val), "VM:%s JI:%s CP:%s",
            h->voice_mgr_on ? "ON" : "--",
            h->ji_on ? "ON" : "--",
            h->counterpoint_on ? "ON" : "--");
        break;
    }
    case M4_T_EQ:    snprintf(val, sizeof(val), "%-10s", pre_eq_preset_NAMES[M4.edit.pre_eq_preset]);   break;
    case M4_T_VCA:   snprintf(val, sizeof(val), "%-10s", VCA_PRESET_NAMES[M4.edit.vca_preset]); break;
    case M4_T_POST_EQ: snprintf(val, sizeof(val), "%-10s", post_eq_preset_NAMES[M4.edit.post_eq_preset]); break;
    case M4_T_DELAY: {
        const char* dn = (M4.edit.dly_preset < DLY_PRESET_COUNT)
            ? DLY_PRESET_NAMES[M4.edit.dly_preset] : "???";
        snprintf(val, sizeof(val), "[%02u]%s%-6s", M4.edit.dly_preset,
            M4.edit.dly_custom_valid ? "*" : " ", dn);
        break;
    }
    case M4_T_LFO:   snprintf(val, sizeof(val), "%-10s", LFO_PRESET_NAMES[M4.edit.lfo_preset]); break;
    case M4_T_PT2258:
        snprintf(val, sizeof(val), "L:-%2udB R:-%2udB [MUTE]",
            (unsigned)g_pt2258_att_l_db, (unsigned)g_pt2258_att_r_db);
        break;
    case M4_T_APPLY: snprintf(val, sizeof(val), "%s", M4.apply_flash ? "** SAVED! **" : "[SW3:저장]"); break;
    case M4_T_EXIT:  snprintf(val, sizeof(val), "-> MENU"); break;
    }
    if (val[0]) lcd_string(120, (uint16_t)(y + 5), val, sel ? COLOR_YELLOW : COLOR_WHITE, bg);
}

static void m4_draw_top_all(void)
{
    lcd_clear(COLOR_BLACK);
    lcd_draw_header(0x4A10u, "  INST SETUP LIGHT", COLOR_WHITE);
    for (int i = 0; i < M4_T_COUNT; i++) m4_draw_top_row(i, (i == M4.top_cur));
    lcd_string(2, 220, "JY:이동  JX:프리셋+/-  SW3:진입  SW4:메뉴복귀",
        COLOR_DKGREY, COLOR_BLACK);
    lcd_string(2, 230, "STAGE3: YM2203 -> PRE EQ -> VCA -> POST EQ  SW5:각 단계 저장",
        0x5AEBu, COLOR_BLACK);
}

/* ── YM 선택 ── */
/* ── YM 입력 모드 선택 화면 ─────────────────────────────────────────────────
 * M4_TOP → YM 항목 선택 시 가장 먼저 표시.
 * 현재 ch_mode를 하이라이트하고, JY로 커서 이동, SW3으로 확정.
 *   커서 0 → CH_MODE_INDEPENDENT → M4_YM (채널별 편집)
 *   커서 1 → CH_MODE_VGM6        → M4_YM (VGM6 프리셋 선택)
 * SW4(EXIT): M4_TOP 복귀.
 * ────────────────────────────────────────────────────────────────────────── */
 /* forward declaration: m4_update_ym_mode_sel → m4_draw_ym_all 참조 */
static void m4_draw_ym_all(void);

static void m4_draw_ym_mode_sel(void)
{
    lcd_clear(COLOR_BLACK);
    lcd_draw_header(0xFD20u, "  YM2203 입력 모드 선택", COLOR_BLACK);

    const char* desc[4] = {
        "FM0/FM1/FM2 독립 + PSG0/1/2 수동",
        "FM0+FM1 8OP 결합 + FM2 보조",
        "FM0+FM1+FM2 12OP 결합",
        "VGM 실측 FM3 + SSG3 6채널 프리셋"
    };
    const uint16_t accent[4] = { COLOR_CYAN, COLOR_YELLOW, COLOR_GREEN, COLOR_MAGENTA };

    for (int i = 0; i < 4; i++) {
        int sel = ((int)M4.edit.ch_mode == i);
        uint16_t y = (uint16_t)(28 + i * 46);
        uint16_t bg = sel ? 0x2104u : 0x0000u;
        lcd_fill_rect(0, y, LCD_W, 42, bg);
        if (sel) lcd_fill_rect(0, y, 3, 42, accent[i]);

        char line[64];
        snprintf(line, sizeof(line), "%s %d. %s", sel ? "->" : "  ", i + 1,
            ch_mode_name((channel_mode_t)i));
        lcd_string(8, (uint16_t)(y + 6), line, sel ? accent[i] : COLOR_GREY, bg);
        lcd_string(8, (uint16_t)(y + 24), desc[i],
            sel ? COLOR_WHITE : 0x4A10u, bg);
    }

    if (M4.edit.ch_mode == CH_MODE_VGM6 && M4.edit.vgm6_inst_idx < FULL6_VGM_CATALOG_COUNT) {
        char ibuf[64];
        snprintf(ibuf, sizeof(ibuf), "VGM6 현재: #%02u %s",
            (unsigned)M4.edit.vgm6_inst_idx,
            FULL6_VGM_CATALOG[M4.edit.vgm6_inst_idx]->name);
        lcd_string(4, 214, ibuf, 0x9E9Fu, COLOR_BLACK);
    }

    lcd_string(2, 228,
        "JY:모드선택  SW3:결정→편집  SW4:저장후메뉴",
        COLOR_DKGREY, COLOR_BLACK);
}

static int m4_update_ym_mode_sel(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];

    int mode = (int)M4.edit.ch_mode;
    if (mode < 0 || mode >(int)CH_MODE_VGM6) mode = 0;

    if (jy < JOY_LOW && !M4.joy_y_mv) {
        if (mode > 0) mode--;
        M4.edit.ch_mode = (channel_mode_t)mode;
        full_preset_sync_combo_patch(&M4.edit);
        M4.joy_y_mv = 1;
        m4_draw_ym_mode_sel();
    }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) {
        if (mode < (int)CH_MODE_VGM6) mode++;
        M4.edit.ch_mode = (channel_mode_t)mode;
        full_preset_sync_combo_patch(&M4.edit);
        M4.joy_y_mv = 1;
        m4_draw_ym_mode_sel();
    }

    /* SW3(SELECT): 선택 확정 → 해당 편집 화면으로 진입 */
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        full_preset_sync_combo_patch(&M4.edit);
        M4.state = M4_YM;
        M4.ym_cur = 0;
        m4_draw_ym_all();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* SW4(EXIT): 현재 Mode4 YM 설정을 저장하고 MENU 복귀 */
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        full_preset_sync_combo_patch(&M4.edit);
        memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
        memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
        g_m4_preset_valid = 1;
        full_preset_apply_hw();
        analog_delay_set_bypass(&g_adly, 1);
        pt2258_set_mute(1);
        m3_full_voice_reset();
        diag_apply_chain_policy("m4_ym_mode_sel_sw4_exit");
        return ZYNQ_MODE_MENU;
    }

    return ZYNQ_MODE_INSTSETUP;
}

static void m4_draw_ym_all(void)
{
    lcd_clear(COLOR_BLACK);

    /* ── VGM6 모드: 6ch 프리셋 선택 화면 ─────────────────────────────── */
    if (M4.edit.ch_mode == CH_MODE_VGM6) {
        lcd_draw_header(0xFD20u, "  YM6 VGM 프리셋  JY:이동 SW3:적용", COLOR_BLACK);
        uint8_t idx = M4.edit.vgm6_inst_idx;
        if (idx >= FULL6_VGM_CATALOG_COUNT) idx = 0;
        const full_inst_t* fi = FULL6_VGM_CATALOG[idx];

        char buf[60];
        snprintf(buf, sizeof(buf), "#%02u / %02u  %s",
            (unsigned)idx, (unsigned)(FULL6_VGM_CATALOG_COUNT - 1u), fi->name);
        lcd_string(4, 24, buf, COLOR_YELLOW, COLOR_BLACK);

        snprintf(buf, sizeof(buf), "FM:%u ch   SSG mask:0x%02X",
            (unsigned)fi->fm_n, (unsigned)fi->ssg_mask);
        lcd_string(4, 52, buf, COLOR_CYAN, COLOR_BLACK);

        for (uint8_t ch = 0; ch < fi->fm_n && ch < 3u; ch++) {
            snprintf(buf, sizeof(buf), "  FM%u  ALG:%u FB:%u  det=%+d noff=%+d vol=%u",
                (unsigned)ch,
                (unsigned)(fi->fm[ch].ALG & 7u),
                (unsigned)(fi->fm[ch].FB & 7u),
                (int)fi->fm_det[ch],
                (int)fi->fm_noff[ch],
                (unsigned)fi->fm_vol[ch]);
            lcd_string(4, (uint16_t)(76u + ch * 20u), buf, COLOR_GREY, COLOR_BLACK);
        }
        for (int ch = 0; ch < 3; ch++) {
            if (!(fi->ssg_mask & (uint8_t)(1u << ch))) continue;
            snprintf(buf, sizeof(buf), "  SSG%d  patch=%u amp=%u noff=%+d",
                ch, (unsigned)fi->ssg_p[ch],
                (unsigned)fi->ssg_amp[ch], (int)fi->ssg_noff[ch]);
            lcd_string(4, (uint16_t)(142u + (uint16_t)ch * 18u),
                buf, 0xF81Fu, COLOR_BLACK);
        }
        lcd_string(2, 210, "ch_mode: VGM6", COLOR_GREEN, COLOR_BLACK);
        lcd_string(2, 228,
            "JY:프리셋  SW3:즉시적용  SW4:모드선택",
            COLOR_DKGREY, COLOR_BLACK);
        return;
    }

    /* ── INDEPENDENT / DUAL / TRIPLE: 채널별 표시 ─────────────────────── */
    char mode_hdr[64];
    snprintf(mode_hdr, sizeof(mode_hdr), "  YM2203 %s  SW3:편집",
        ch_mode_name(M4.edit.ch_mode));
    lcd_draw_header(0xFD20u, mode_hdr, COLOR_BLACK);

    if (M4.edit.ch_mode == CH_MODE_DUAL && M4.edit.fm[0].dual_patch_ptr) {
        char b[64];
        snprintf(b, sizeof(b), "ACTIVE DUAL: %s", M4.edit.fm[0].dual_patch_ptr->name);
        lcd_string(4, 18, b, COLOR_YELLOW, COLOR_BLACK);
        lcd_string(4, 34, "─── FM 채널 ───", COLOR_CYAN, COLOR_BLACK);
    }
    else if (M4.edit.ch_mode == CH_MODE_TRIPLE && M4.edit.fm[0].triple_patch_ptr) {
        char b[64];
        snprintf(b, sizeof(b), "ACTIVE TRIPLE: %s", M4.edit.fm[0].triple_patch_ptr->name);
        lcd_string(4, 18, b, COLOR_YELLOW, COLOR_BLACK);
        lcd_string(4, 34, "─── FM 채널 ───", COLOR_CYAN, COLOR_BLACK);
    }
    else {
        lcd_string(4, 20, "─── FM 채널 ───", COLOR_CYAN, COLOR_BLACK);
    }
    uint16_t fm_y0 = (M4.edit.ch_mode == CH_MODE_INDEPENDENT) ? 30u : 46u;
    for (int ch = 0; ch < 3; ch++) {
        uint16_t y = (uint16_t)(fm_y0 + ch * 26);
        int sel = (M4.ym_cur == ch);
        uint16_t bg = sel ? 0x2104u : 0x0000u;
        lcd_fill_rect(0, y, LCD_W, 26, bg);
        if (sel) lcd_fill_rect(0, y, 3, 26, COLOR_CYAN);
        fm_ch_cfg_t* fc = &M4.edit.fm[ch];
        char tag[8] = "      ";
        if (!fc->enable)         strncpy(tag, "[OFF] ", 8);
        else if (fc->edited)     strncpy(tag, "[E]   ", 8);
        else if (!fc->use_full)  strncpy(tag, "[R]   ", 8);
        /* 악기 이름 + ALG/FB 요약 */
        char name_line[52];
        const full_inst_t* fi_ref = (fc->use_full && fc->inst_idx < FULL_INST_COUNT)
            ? FULL_INST_CATALOG[fc->inst_idx] : NULL;
        uint8_t eff_op = (fc->op_count > 0 && fc->op_count <= 4) ? fc->op_count
            : (fi_ref ? fi_ref->fm_n : 4u);
        snprintf(name_line, sizeof(name_line), "%sFM%d %s#%02d %-14s [OP:%d]",
            sel ? "->" : "  ", ch, tag, fc->inst_idx,
            fc->use_full ? m4_inst_name(fc->inst_idx) : m4_raw_patch_name(fc->inst_idx),
            eff_op);
        uint16_t fcol = fc->enable ? (sel ? COLOR_CYAN : COLOR_GREY) : (sel ? 0x8410u : 0x4208u);
        lcd_string(4, (uint16_t)(y + 4), name_line, fcol, bg);
        /* 패치 상세 (ALG/FB + 캐리어 TL) */
        const ym2203_patch_t* pp = &fc->patch_override;
        static const uint8_t car_m[8] = { 0x08,0x0C,0x0A,0x0E,0x0F,0x0F,0x0F,0x0F };
        uint8_t car = car_m[pp->ALG & 7];
        uint8_t tl0 = (car & 1) ? pp->ops[0].TL : 0;
        uint8_t tl1 = (car & 2) ? pp->ops[1].TL : 0;
        uint8_t tl2 = (car & 4) ? pp->ops[2].TL : 0;
        uint8_t tl3 = (car & 8) ? pp->ops[3].TL : 0;
        char det[52];
        snprintf(det, sizeof(det), "      ALG:%d FB:%d  CarTL:%3d/%3d/%3d/%3d",
            pp->ALG, pp->FB, tl0, tl1, tl2, tl3);
        lcd_string(4, (uint16_t)(y + 15), det,
            sel ? COLOR_YELLOW : 0x4A10u, bg);
    }
    uint16_t psg_y0 = (M4.edit.ch_mode == CH_MODE_INDEPENDENT) ? 120u : 136u;
    lcd_string(4, (uint16_t)(psg_y0 - 10u), "─── PSG 채널 ───", COLOR_MAGENTA, COLOR_BLACK);
    for (int ch = 0; ch < 3; ch++) {
        uint16_t y = (uint16_t)(psg_y0 + ch * 22);
        int sel = (M4.ym_cur == ch + 3);
        uint16_t bg = sel ? 0x2104u : 0x0000u;
        lcd_fill_rect(0, y, LCD_W, 22, bg);
        if (sel) lcd_fill_rect(0, y, 3, 22, COLOR_MAGENTA);
        psg_ch_cfg_t* pc = &M4.edit.psg[ch];
        char buf[60];
        snprintf(buf, sizeof(buf), "%sPSG%d %s #%-2d %-14s A=%2d noff=%+d",
            sel ? "->" : "  ", ch, pc->enable ? "ON " : "OFF",
            (int)pc->patch_idx, m4_ssg_name((uint8_t)pc->patch_idx),
            pc->amp, pc->note_offset);
        lcd_string(4, (uint16_t)(y + 6), buf,
            sel ? COLOR_MAGENTA : COLOR_DKGREY, bg);
    }
    lcd_string(2, 228,
        "JY:이동  JX:FM<>PSG  SW3:편집  SW4:모드선택  SW5:저장",
        COLOR_DKGREY, COLOR_BLACK);
}

/* ── FM 악기 선택 ── */
static void m4_draw_fm_inst(void)
{
    lcd_clear(COLOR_BLACK);
    char hdr[52];
    snprintf(hdr, sizeof(hdr), "  FM CH%d 악기 [%s]  ADC:노브탐색",
        M4.fm_ch, M4.fm_edit_mode ? "RAW패치" : "풀악기");
    lcd_draw_header(0x07FFu, hdr, COLOR_BLACK);

    fm_ch_cfg_t* fc = &M4.edit.fm[M4.fm_ch];
    char buf[60];

    if (!M4.fm_edit_mode) {
        /* ── 현재 선택 악기 이름 + 인덱스 바 ── */
        uint8_t idx = M4.fm_inst_cur;
        snprintf(buf, sizeof(buf), "#%02d %-18s%s",
            idx, m4_inst_name(idx), fc->edited ? "[편집됨]" : "");
        lcd_string(4, 22, buf, COLOR_YELLOW, COLOR_BLACK);

        /* 인덱스 프로그레스 바 */
        if (FULL_INST_COUNT > 1) {
            uint16_t bw = (uint16_t)((uint32_t)idx * (LCD_W - 8) / (FULL_INST_COUNT - 1));
            lcd_fill_rect(4, 34, bw, 5, COLOR_CYAN);
            if (bw < LCD_W - 8)
                lcd_fill_rect((uint16_t)(4 + bw), 34, (uint16_t)(LCD_W - 8 - bw), 5, 0x1082u);
        }

        /* full_inst 상세 파라미터 */
        if (idx < FULL_INST_COUNT) {
            const full_inst_t* fi = FULL_INST_CATALOG[idx];
            uint8_t eff_op = (fc->op_count > 0) ? fc->op_count : fi->fm_n;
            snprintf(buf, sizeof(buf), "FM:%d(할당OP:%d) SSG:0x%02X LFO:%3dHz  PB:%+dcent",
                fi->fm_n, eff_op, fi->ssg_mask, fi->lfo_hz100 / 100, g_midi_perf.pb_cent);
            lcd_string(4, 42, buf, COLOR_GREEN, COLOR_BLACK);
            if (fi->fm_n > 0) {
                const ym2203_patch_t* p = &fi->fm[0];
                snprintf(buf, sizeof(buf), "ALG:%d FB:%d", p->ALG, p->FB);
                lcd_string(4, 56, buf, COLOR_WHITE, COLOR_BLACK);
                for (int op = 0; op < 4; op++) {
                    /* 캐리어 여부 ALG별 표시 */
                    static const uint8_t car_m[8] = { 0x08,0x0C,0x0A,0x0E,0x0F,0x0F,0x0F,0x0F };
                    int is_car = (car_m[p->ALG & 7] >> op) & 1;
                    snprintf(buf, sizeof(buf), "%sOP%d TL:%3d AR:%2d DR:%2d SR:%2d RR:%2d MUL:%2d",
                        is_car ? "*" : " ",
                        op, p->ops[op].TL, p->ops[op].AR,
                        p->ops[op].DR, p->ops[op].SR, p->ops[op].RR, p->ops[op].MUL);
                    lcd_string(4, (uint16_t)(68 + op * 13), buf,
                        is_car ? COLOR_CYAN : COLOR_GREY, COLOR_BLACK);
                }
            }
        }

        /* 위아래 이웃 악기 스크롤 뷰 */
        for (int d = -2; d <= 2; d++) {
            int ni = (int)M4.fm_inst_cur + d;
            if (ni < 0 || ni >= FULL_INST_COUNT) continue;
            uint16_t y2 = (uint16_t)(128 + d * 14 + 28);
            char lb[36]; snprintf(lb, sizeof(lb), " #%02d %-18s", ni, m4_inst_name((uint8_t)ni));
            lcd_string(6, y2, lb, d == 0 ? COLOR_YELLOW : COLOR_DKGREY, COLOR_BLACK);
        }

        /* ADC 노브 현재 위치 표시 */
        {
            uint8_t adc_idx = (FULL_INST_COUNT > 1)
                ? (uint8_t)((uint32_t)adc_buf[ADC_IDX_VOL] * (FULL_INST_COUNT - 1) / ADC_MAX)
                : 0u;
            uint8_t pitch_note = 60u + (uint8_t)((uint32_t)adc_buf[ADC_IDX_PITCH] * 24u / ADC_MAX);
            snprintf(buf, sizeof(buf), "VOL노브:#%02d%s  PITCH노브:Note%3d",
                adc_idx, M4.adc_armed ? "" : "(미동기)", pitch_note);
            lcd_string(4, 210, buf, 0x4A10u, COLOR_BLACK);
        }

        lcd_string(2, 228,
            "JY:±1  JX:±5  SW1/2:OP개수  SW3:OP편집  SW4:뒤로  SW5:저장확정  [JX롱:RAW]",
            COLOR_DKGREY, COLOR_BLACK);
    }
    else {
        /* ── RAW 패치 모드 ── */
        uint8_t idx = M4.fm_raw_cur;
        snprintf(buf, sizeof(buf), "[RAW] #%02d %-16s%s",
            idx, m4_raw_patch_name(idx), fc->edited ? "[편집됨]" : "");
        lcd_string(4, 22, buf, 0xFFE0u, COLOR_BLACK);

        if (YM2203_PATCH_COUNT > 1) {
            uint16_t bw = (uint16_t)((uint32_t)idx * (LCD_W - 8) / (YM2203_PATCH_COUNT - 1));
            lcd_fill_rect(4, 34, bw, 5, 0xFFE0u);
            if (bw < LCD_W - 8)
                lcd_fill_rect((uint16_t)(4 + bw), 34, (uint16_t)(LCD_W - 8 - bw), 5, 0x1082u);
        }
        if (idx < YM2203_PATCH_COUNT) {
            const ym2203_patch_t* p = &YM2203_PATCHES[idx];
            snprintf(buf, sizeof(buf), "ALG:%d FB:%d", p->ALG, p->FB);
            lcd_string(4, 44, buf, COLOR_GREEN, COLOR_BLACK);
            for (int op = 0; op < 4; op++) {
                static const uint8_t car_m[8] = { 0x08,0x0C,0x0A,0x0E,0x0F,0x0F,0x0F,0x0F };
                int is_car = (car_m[p->ALG & 7] >> op) & 1;
                snprintf(buf, sizeof(buf), "%sOP%d TL:%3d AR:%2d DR:%2d SR:%2d RR:%2d MUL:%2d",
                    is_car ? "*" : " ",
                    op, p->ops[op].TL, p->ops[op].AR,
                    p->ops[op].DR, p->ops[op].SR, p->ops[op].RR, p->ops[op].MUL);
                lcd_string(4, (uint16_t)(56 + op * 13), buf,
                    is_car ? 0xFFE0u : COLOR_GREY, COLOR_BLACK);
            }
        }
        for (int d = -2; d <= 2; d++) {
            int ni = (int)M4.fm_raw_cur + d;
            if (ni < 0 || ni >= YM2203_PATCH_COUNT) continue;
            uint16_t y2 = (uint16_t)(122 + d * 13 + 26);
            char lb[36]; snprintf(lb, sizeof(lb), " #%02d %-18s", ni, m4_raw_patch_name((uint8_t)ni));
            lcd_string(6, y2, lb, d == 0 ? 0xFFE0u : COLOR_DKGREY, COLOR_BLACK);
        }
        {
            uint8_t adc_idx = (YM2203_PATCH_COUNT > 1)
                ? (uint8_t)((uint32_t)adc_buf[ADC_IDX_VOL] * (YM2203_PATCH_COUNT - 1) / ADC_MAX)
                : 0u;
            snprintf(buf, sizeof(buf), "VOL노브:#%02d%s  PITCH노브:미리듣기음정",
                adc_idx, M4.adc_armed ? "" : "(미동기)");
            lcd_string(4, 210, buf, 0x4A10u, COLOR_BLACK);
        }
        lcd_string(2, 228,
            "JY:±1  JX:±5  SW1/2:OP개수  SW3:OP편집  SW4:뒤로  SW5:저장확정  [JX롱:풀악기]",
            COLOR_DKGREY, COLOR_BLACK);
    }
}

/* ── FM OP 편집 ── */
static void m4_draw_fm_op(void)
{
    lcd_clear(COLOR_BLACK);
    /* 헤더: 어떤 악기의 OP를 편집 중인지 출처 표시 */
    {
        fm_ch_cfg_t* fc = &M4.edit.fm[M4.fm_ch];
        const char* base_name = "?";
        if (fc->use_full && fc->inst_idx < FULL_INST_COUNT)
            base_name = FULL_INST_CATALOG[fc->inst_idx]->name;
        else if (!fc->use_full && fc->inst_idx < YM2203_PATCH_COUNT)
            base_name = YM2203_PATCHES[fc->inst_idx].name;
        char hdr[52];
        snprintf(hdr, sizeof(hdr), " CH%d OP%d [%s]%s",
            M4.fm_ch, M4.op_sel, base_name,
            fc->edited ? "*" : "");
        lcd_draw_header(0x4A10u, hdr, COLOR_WHITE);
    }

    /* OP 탭 4개 */
    ym2203_patch_t* ep = &M4.edit.fm[M4.fm_ch].patch_override;
    for (int op = 0; op < 4; op++) {
        uint16_t x = (uint16_t)(op * 78 + 2);
        int is_mute = (ep->ops[op].TL >= 127);
        char lb[12];
        snprintf(lb, sizeof(lb), "OP%d%s", op, is_mute ? "[M]" : "   ");
        uint16_t bg = (op == M4.op_sel) ? COLOR_YELLOW
            : is_mute ? COLOR_DKGREY : 0x2104u;
        uint16_t fg = (op == M4.op_sel) ? COLOR_BLACK
            : is_mute ? COLOR_GREY : COLOR_WHITE;
        lcd_fill_rect(x, 18, 76, 12, bg);
        lcd_string((uint16_t)(x + 4), 19, lb, fg, bg);
    }
    lcd_fill_rect(0, 30, LCD_W, 2, COLOR_DKGREY);

    ym2203_op_t* opp = &ep->ops[M4.op_sel];
    int is_mute = (opp->TL >= 127);
    lcd_string(224, 20, is_mute ? "[MUTED]" : "       ",
        is_mute ? COLOR_RED : COLOR_DKGREY, COLOR_BLACK);

    for (int i = 0; i < M4_OP_P_COUNT; i++) {
        uint16_t y = (uint16_t)(32 + i * 15);
        if (y > 210) break;
        int sel = (i == M4.op_param_cur);
        uint16_t bg = sel ? 0x2104u : 0x0000u;
        lcd_fill_rect(0, y, LCD_W, 15, bg);
        if (sel) lcd_fill_rect(0, y, 3, 15, COLOR_CYAN);
        lcd_string(6, (uint16_t)(y + 4), M4_OP_P_NAMES[i],
            sel ? COLOR_CYAN : COLOR_GREY, bg);

        uint8_t val = *m4_op_ptr(i);
        uint8_t maxv = M4_OP_P_MAX[i];
        char vb[12]; snprintf(vb, sizeof(vb), "%3d", val);
        lcd_string(42, (uint16_t)(y + 4), vb, sel ? COLOR_WHITE : COLOR_GREY, bg);

        if (maxv > 0) {
            uint16_t bw = (uint16_t)((uint32_t)val * 150u / maxv);
            lcd_fill_rect(70, (uint16_t)(y + 3), bw, 9,
                sel ? COLOR_CYAN : 0x2945u);
            if (bw < 150)
                lcd_fill_rect((uint16_t)(70 + bw), (uint16_t)(y + 3),
                    (uint16_t)(150 - bw), 9, 0x1082u);
        }
        /* ALG/FB는 채널 전체 파라미터임을 표시 */
        if (i < 2) lcd_string(224, (uint16_t)(y + 4), "*CH",
            sel ? 0xFD20u : 0x4A10u, bg);
    }

    /* ADC_VOL 노브: 현재 파라미터 직접 매핑 상태 표시 */
    {
        uint8_t mx = M4_OP_P_MAX[M4.op_param_cur];
        uint8_t adc_val = (mx > 0)
            ? (uint8_t)((uint32_t)adc_buf[ADC_IDX_VOL] * mx / ADC_MAX) : 0u;
        char abuf[48];
        snprintf(abuf, sizeof(abuf), "VOL→%s=%3d%s  PITCH→캐리어TL오프셋",
            M4_OP_P_NAMES[M4.op_param_cur], adc_val,
            M4.adc_armed ? "" : "(미동기)");
        lcd_string(4, 214, abuf, 0x4A10u, COLOR_BLACK);
    }

    lcd_string(2, 228,
        "JY:파라미터이동  JX:값±1(즉시반영)  SW1:OP+  SW2:OP-  SW3:미리듣기  ADC:직접매핑  SW4:뒤로  SW5:저장",
        COLOR_DKGREY, COLOR_BLACK);
}

/* ── PSG 편집 ── */
static void m4_draw_psg(void)
{
    lcd_clear(COLOR_BLACK);
    char hdr[52]; snprintf(hdr, sizeof(hdr), "  PSG CH%d 편집  (ADC_VOL=볼륨)", M4.psg_ch);
    lcd_draw_header(0xF81Fu, hdr, COLOR_BLACK);
    psg_ch_cfg_t* pc = &M4.edit.psg[M4.psg_ch];
    char buf[56];

    /* ON/OFF 상태 */
    snprintf(buf, sizeof(buf), "상태: %s", pc->enable ? "■ ON " : "□ OFF");
    lcd_fill_rect(0, 22, LCD_W, 14, COLOR_BLACK);
    lcd_string(4, 24, buf, pc->enable ? COLOR_GREEN : COLOR_DKGREY, COLOR_BLACK);
    lcd_string(120, 24, "(SW3:토글)", COLOR_GREY, COLOR_BLACK);

    /* 패치 이름 + 프로그레스 바 */
    snprintf(buf, sizeof(buf), "패치: #%-2d %-16s",
        (int)pc->patch_idx, m4_ssg_name((uint8_t)pc->patch_idx));
    lcd_fill_rect(0, 38, LCD_W, 14, COLOR_BLACK);
    lcd_string(4, 40, buf, COLOR_YELLOW, COLOR_BLACK);

    if (SSG_COUNT > 1) {
        uint16_t bw = (uint16_t)((uint32_t)pc->patch_idx * (LCD_W - 8) / (SSG_COUNT - 1));
        lcd_fill_rect(4, 53, bw, 4, COLOR_MAGENTA);
        if (bw < LCD_W - 8)
            lcd_fill_rect((uint16_t)(4 + bw), 53, (uint16_t)(LCD_W - 8 - bw), 4, 0x1082u);
    }

    /* 이웃 패치 스크롤 뷰 */
    for (int d = -2; d <= 2; d++) {
        int ni = (int)pc->patch_idx + d;
        if (ni < 0 || ni >= SSG_COUNT) continue;
        uint16_t y2 = (uint16_t)(60 + d * 12 + 24);
        char lb[34]; snprintf(lb, sizeof(lb), " #%-2d %-16s", ni, m4_ssg_name((uint8_t)ni));
        lcd_string(6, y2, lb, d == 0 ? COLOR_YELLOW : COLOR_DKGREY, COLOR_BLACK);
    }

    /* 볼륨 바 */
    {
        uint16_t bw = (uint16_t)((uint32_t)pc->amp * (LCD_W - 8) / 15u);
        lcd_fill_rect(4, 130, bw, 10, COLOR_MAGENTA);
        if (bw < LCD_W - 8)
            lcd_fill_rect((uint16_t)(4 + bw), 130, (uint16_t)(LCD_W - 8 - bw), 10, 0x1082u);
        snprintf(buf, sizeof(buf), "볼륨: %2d/15", pc->amp);
        lcd_string(4, 143, buf, COLOR_CYAN, COLOR_BLACK);
    }

    /* 음정 오프셋 (JX) */
    {
        snprintf(buf, sizeof(buf), "음정 오프셋: %+d반음  (JX:변경  SW1/2:±1)",
            pc->note_offset);
        lcd_string(4, 158, buf, COLOR_WHITE, COLOR_BLACK);
    }

    /* ADC_VOL 노브 현재 값 표시 */
    {
        uint8_t adc_amp = (uint8_t)((uint32_t)adc_buf[ADC_IDX_VOL] * 15u / ADC_MAX);
        snprintf(buf, sizeof(buf), "VOL노브: %2d%s  PITCH노브: 미리듣기음정",
            adc_amp, M4.adc_armed ? "" : "(미동기)");
        lcd_string(4, 174, buf, 0x4A10u, COLOR_BLACK);
    }

    /* 3채널 전체 요약 */
    lcd_string(4, 192, "전체 PSG:", COLOR_WHITE, COLOR_BLACK);
    for (int ch = 0; ch < 3; ch++) {
        psg_ch_cfg_t* p2 = &M4.edit.psg[ch];
        char s[32];
        snprintf(s, sizeof(s), "CH%d:%s A=%2d noff=%+d",
            ch, p2->enable ? "ON " : "OFF", p2->amp, p2->note_offset);
        uint16_t col = (ch == M4.psg_ch) ? COLOR_MAGENTA : COLOR_DKGREY;
        lcd_string(4, (uint16_t)(204 + ch * 8), s, col, COLOR_BLACK);
    }

    lcd_string(2, 228,
        "JY:패치±1  JX:음정±1  SW1/2:볼륨±1  SW3:ON/OFF  SW4:뒤로  SW5:저장확정",
        COLOR_DKGREY, COLOR_BLACK);
}

/* ── HARMONY 서브메뉴 ── */
static void m4_draw_hm_row(int idx, int sel)
{
    int screen_top = (M4.hm_cur / 9) * 9;
    int si = idx - screen_top;
    if (si < 0 || si > 9) return;
    uint16_t y = (uint16_t)(20 + si * 21);
    uint16_t bg = sel ? 0x2104u : 0x0000u;
    lcd_fill_rect(0, y, LCD_W, 21, bg);
    lcd_string(2, (uint16_t)(y + 6), sel ? "->" : "  ", COLOR_WHITE, bg);
    lcd_string(18, (uint16_t)(y + 6), M4_H_LABELS[idx], M4_H_COLORS[idx], bg);
    char val[24]; m4_hm_val_str(idx, val, sizeof(val));
    lcd_string(178, (uint16_t)(y + 6), val, sel ? COLOR_YELLOW : COLOR_WHITE, bg);
    uint8_t v = *m4_hm_ptr(idx), mx = M4_H_MAX[idx];
    if (mx > 1) {
        uint16_t bw = (uint16_t)((uint32_t)v * 80u / mx);
        lcd_fill_rect(240, y, bw, 19, COLOR_DKGREEN);
        if (bw < 80) lcd_fill_rect((uint16_t)(240 + bw), y, (uint16_t)(80 - bw), 19, 0x1082u);
    }
}

static void m4_draw_hm_all(void)
{
    lcd_clear(COLOR_BLACK);
    lcd_draw_header(0x867Fu, "  HARMONY 설정", COLOR_WHITE);
    int screen_top = (M4.hm_cur / 9) * 9;
    for (int i = screen_top; i < screen_top + 9 && i < M4_H_COUNT; i++)
        m4_draw_hm_row(i, (i == M4.hm_cur));
    char pg[24];
    snprintf(pg, sizeof(pg), "%d/%d", M4.hm_cur + 1, M4_H_COUNT);
    lcd_string(290, 228, pg, COLOR_DKGREY, COLOR_BLACK);

    /* ADC_VOL 노브 표시 (수치형 항목일 때만) */
    uint8_t mx_cur = M4_H_MAX[M4.hm_cur];
    if (mx_cur > 1) {
        uint8_t adc_mapped = (uint8_t)((uint32_t)adc_buf[ADC_IDX_VOL] * mx_cur / ADC_MAX);
        char abuf[44];
        snprintf(abuf, sizeof(abuf), "VOL노브→%3d%s (현재:%3d)",
            adc_mapped, M4.adc_armed ? "" : "(미동기)", *m4_hm_ptr(M4.hm_cur));
        lcd_string(4, 220, abuf, 0x4A10u, COLOR_BLACK);
    }

    lcd_string(2, 228, "JY:항목  JX/SW1/2:값  SW3:ON/OFF토글  SW4:나가기  SW5:취소",
        COLOR_DKGREY, COLOR_BLACK);
}

// ─────────────────────────────────────────────────────────────────────────────
//  §I-A  TOP 메뉴 APPLY
// [FIX] WARM/SVF 상태 변수를 m4_top_jx_change 사용 이전으로 이동
// ─────────────────────────────────────────────────────────────────────────────

static void m4_top_jx_change(int dir)
{
    int c = 0;

    if (!diag_top_item_enabled(M4.top_cur)) {
        printf("[DIAG-%s] TOP item disabled -> forced bypass item=%d\n",
            diag_chain_stage_name(), M4.top_cur);
        diag_apply_chain_policy("m4_top_jx_change_disabled");
        m4_draw_top_row(M4.top_cur, 1);
        return;
    }
    switch (M4.top_cur) {
    case M4_T_EQ:
        if (DIAG_USE_PRE_EQ) {
            if (dir < 0 && M4.edit.pre_eq_preset>0) { M4.edit.pre_eq_preset--; M4.edit.pre_eq_custom_valid = 0u; apply_pre_eq_preset(M4.edit.pre_eq_preset);  c = 1; }
            if (dir > 0 && M4.edit.pre_eq_preset < pre_eq_preset_COUNT - 1) { M4.edit.pre_eq_preset++; M4.edit.pre_eq_custom_valid = 0u; apply_pre_eq_preset(M4.edit.pre_eq_preset);  c = 1; }
        }
        else {
            if (dir < 0 && M4.edit.pre_eq_preset>0) { M4.edit.pre_eq_preset--;  c = 1; }
            if (dir > 0 && M4.edit.pre_eq_preset < pre_eq_preset_COUNT - 1) { M4.edit.pre_eq_preset++;  c = 1; }
        }
        break;
    case M4_T_VCA:
        if (DIAG_USE_VCA) {
            if (dir < 0 && M4.edit.vca_preset>0) { M4.edit.vca_preset--; apply_vca_preset(M4.edit.vca_preset); c = 1; }
            if (dir > 0 && M4.edit.vca_preset < VCA_PRESET_COUNT - 1) { M4.edit.vca_preset++; apply_vca_preset(M4.edit.vca_preset); c = 1; }
        }
        else {
            if (dir < 0 && M4.edit.vca_preset>0) { M4.edit.vca_preset--; c = 1; }
            if (dir > 0 && M4.edit.vca_preset < VCA_PRESET_COUNT - 1) { M4.edit.vca_preset++; c = 1; }
        }
        break;
    case M4_T_POST_EQ:
        if (DIAG_USE_POST_EQ) {
            if (dir < 0 && M4.edit.post_eq_preset > 0) { M4.edit.post_eq_preset--; M4.edit.post_eq_custom_valid = 0u; apply_post_eq_preset(M4.edit.post_eq_preset); c = 1; }
            if (dir > 0 && M4.edit.post_eq_preset < post_eq_preset_COUNT - 1) { M4.edit.post_eq_preset++; M4.edit.post_eq_custom_valid = 0u; apply_post_eq_preset(M4.edit.post_eq_preset); c = 1; }
        }
        else {
            if (dir < 0 && M4.edit.post_eq_preset > 0) { M4.edit.post_eq_preset--; c = 1; }
            if (dir > 0 && M4.edit.post_eq_preset < post_eq_preset_COUNT - 1) { M4.edit.post_eq_preset++; c = 1; }
        }
        break;
    case M4_T_DELAY:
        if (DIAG_USE_DELAY) {
            if (dir < 0 && M4.edit.dly_preset > 0)
            {
                M4.edit.dly_preset--; M4.edit.dly_custom_valid = 0u; apply_dly_preset(M4.edit.dly_preset); c = 1;
            }
            if (dir > 0 && M4.edit.dly_preset < DLY_PRESET_COUNT - 1)
            {
                M4.edit.dly_preset++; M4.edit.dly_custom_valid = 0u; apply_dly_preset(M4.edit.dly_preset); c = 1;
            }
        }
        else {
            if (dir < 0 && M4.edit.dly_preset > 0) { M4.edit.dly_preset--; M4.edit.dly_custom_valid = 0u; c = 1; }
            if (dir > 0 && M4.edit.dly_preset < DLY_PRESET_COUNT - 1) { M4.edit.dly_preset++; M4.edit.dly_custom_valid = 0u; c = 1; }
        }
        break;
    case M4_T_LFO:
        if (dir < 0 && M4.edit.lfo_preset>0) { M4.edit.lfo_preset--; apply_lfo_preset(M4.edit.lfo_preset); c = 1; }
        if (dir > 0 && M4.edit.lfo_preset < LFO_PRESET_COUNT - 1) { M4.edit.lfo_preset++; apply_lfo_preset(M4.edit.lfo_preset); c = 1; }
        break;
    case M4_T_PT2258:
        /* mute 유지, 볼륨 레지스터만 write (unmute 시 즉시 반영됨) */
        if (dir < 0 && g_pt2258_att_db > PT2258_VOL_MIN_DB) {
            pt2258_set_all_volume_db((int)g_pt2258_att_db - 1);
            g_pt2258_att_l_db = g_pt2258_att_db;
            g_pt2258_att_r_db = g_pt2258_att_db;
            c = 1;
        }
        if (dir > 0 && g_pt2258_att_db < PT2258_VOL_MAX_DB) {
            pt2258_set_all_volume_db((int)g_pt2258_att_db + 1);
            g_pt2258_att_l_db = g_pt2258_att_db;
            g_pt2258_att_r_db = g_pt2258_att_db;
            c = 1;
        }
        /* mute 상태 재확인 (set_all_volume_db는 mute에 영향 없음) */
        if (c) pt2258_set_mute(1);
        break;
    default: break;
    }
    if (c) {
        m4_draw_top_row(M4.top_cur, 1);
        diag_apply_chain_policy("m4_top_jx_change");
        /* [DIAG] 활성화된 체인 단계에서만 preview */
        if ((M4.top_cur == M4_T_EQ && DIAG_USE_PRE_EQ) ||
            (M4.top_cur == M4_T_VCA && DIAG_USE_VCA) ||
            (M4.top_cur == M4_T_POST_EQ && DIAG_USE_POST_EQ) ||
            (M4.top_cur == M4_T_DELAY && DIAG_USE_DELAY)) {
            m4_preview_fm_full((uint8_t)M4.fm_ch, M4.fm_inst_cur);
        }
    }
}

static void m4_top_do_apply(void)
{
    memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
    full_preset_apply_hw();
    /* 직접 편집한 EQ 값이 있을 때만 프리셋 적용 위에 다시 flush한다.
     * 유효 플래그 없이 무조건 flush하면 초기 flat/custom 버퍼가 프리셋을 덮을 수 있다. */
    if (g_full_preset.pre_eq_custom_valid)  eq_band_flush();
    if (g_full_preset.post_eq_custom_valid) eq_band_flush_post();
    /* LIGHT-CHAIN: warm custom excluded */
    /* LIGHT-CHAIN: svf custom excluded */
    /* [V16] APPLY 완료 → g_m4_preset_valid=1 (모드2에서 full_preset_apply_hw 경로 활성) */
    g_m4_preset_valid = 1;
    /* APPLY 후에도 mute 유지 ? preview 시에만 unmute */
    pt2258_set_mute(1);
    m3_full_voice_reset();
    memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
    M4.apply_flash = 1; M4.flash_cnt = 0;
    if (M4.state == M4_TOP) m4_draw_top_all();

    // 1. 첫 번째 printf: 서식 문자에 맞춰서 M4.edit 변수들을 매핑하고 괄호와 세미콜론으로 닫습니다.
    printf("[M4] APPLY: FM0=#%d(%s) FM1=#%d FM2=#%d EQ=%d VCA=%d WARM=%d(%s) DLY=%d SVF=%d(%s) POST_EQ=%d REV=%d(%s)\n",
        M4.edit.fm[0].inst_idx,
        M4.edit.fm[0].use_full ? m4_inst_name(M4.edit.fm[0].inst_idx) : "RAW",
        M4.edit.fm[1].inst_idx,
        M4.edit.fm[2].inst_idx,
        M4.edit.pre_eq_preset,
        M4.edit.vca_preset,
        M4.edit.warm_preset,  // %d (WARM)
        "WARM_STR",           // %s (WARM 문자열 - 적절한 텍스트나 변수로 대체 가능)
        M4.edit.dly_preset,   // %d (DLY)
        M4.edit.svf_preset,   // %d (SVF)
        "SVF_STR",            // %s (SVF 문자열)
        M4.edit.post_eq_preset, // %d (POST_EQ)
        M4.edit.rev_preset,   // %d (REV)
        "REV_STR");           // %s (REV 문자열) -> 만약 문자열 인자들이 필요 없다면 서식 문자(%s)들을 포맷에서 지우셔야 합니다.

    // 2. 완전히 분리된 두 번째 printf
    printf("[M4] HARMONY: vm=%d ji=%d kd=%d cp=%d nlfo=%d\n",
        M4.edit.harmony.voice_mgr_on,
        M4.edit.harmony.ji_on,
        M4.edit.harmony.key_detect_on,
        M4.edit.harmony.counterpoint_on,
        (int)M4.edit.harmony.nlfo_preset);
}
// ─────────────────────────────────────────────────────────────────────────────
//  §I-F  FSM 업데이트 함수
// ─────────────────────────────────────────────────────────────────────────────



/* ── Mode4 상세 편집 forward declarations ───────────────────────────── */
#ifndef M4_VCA_PCOUNT
#define M4_VCA_PCOUNT 9
#endif
static inline uint16_t m4_sat_u16_add(uint16_t v, int d, uint16_t lo, uint16_t hi);
static void m4_draw_vca_detail_values(void);

/* [v13-FWD] VCA→WARM→SVF→DELAY 체인 진입에 필요한 전방 선언 */
static void m4_draw_dly_edit(void);
static int  m4_update_dly_edit(uint8_t sw);
/* [V15r-FWD] DELAY → REV_EDIT 전진에 필요한 전방 선언 */
static void rev_edit_load_from_preset(uint8_t idx);
static void rev_edit_save_to_full_preset(void);
static void dly_knob_snap(void);
static void dly_edit_read_from_hw(void);
static void dly_edit_apply_to_hw(void);

/* ── EQ 세부 편집 화면 ── */

/* 밴드별 실시간 파라미터 ? Mode 4 편집 전용
 *  filter_type : EQ_FTYPE_* (eq_4band_presets.h §5b)
 *  fc          : 중심/컷오프/쉘프 주파수 [Hz]
 *  gain_db     : 게인 [dB]  (HPF/LPF/BYPASS 는 무시)
 *  p2          : Q (Bell/HPF/LPF) 또는 S (Shelf 기울기)
 */
typedef struct {
    eq_filter_type_t filter_type;
    double fc;
    double gain_db;
    double p2;      /* Q 또는 S ? 타입에 따라 의미가 다름 */
} EqBandParam;

static EqBandParam g_pre_eq_band[4] = {
    { EQ_FTYPE_LOW_SHELF,  EQ_FC_DEFAULT_BAND0, 0.0, EQ_S_DEFAULT      },  /* band0: 저역  */
    { EQ_FTYPE_BELL,       EQ_FC_DEFAULT_BAND1, 0.0, EQ_Q_DEFAULT_BELL },  /* band1: 저중역 */
    { EQ_FTYPE_BELL,       EQ_FC_DEFAULT_BAND2, 0.0, EQ_Q_DEFAULT_BELL },  /* band2: 고중역 */
    { EQ_FTYPE_HIGH_SHELF, EQ_FC_DEFAULT_BAND3, 0.0, EQ_S_DEFAULT      },  /* band3: 고역  */
};
static EqBandParam g_post_eq_band[4] = {
    { EQ_FTYPE_LOW_SHELF,  EQ_FC_DEFAULT_BAND0, 0.0, EQ_S_DEFAULT      },
    { EQ_FTYPE_BELL,       EQ_FC_DEFAULT_BAND1, 0.0, EQ_Q_DEFAULT_BELL },
    { EQ_FTYPE_BELL,       EQ_FC_DEFAULT_BAND2, 0.0, EQ_Q_DEFAULT_BELL },
    { EQ_FTYPE_HIGH_SHELF, EQ_FC_DEFAULT_BAND3, 0.0, EQ_S_DEFAULT      },
};

static inline EqBandParam* m4_eq_active_band_table(void)
{
    return M4.eq_target ? g_post_eq_band : g_pre_eq_band;
}

static inline volatile uint8_t* m4_eq_active_hw(void)
{
    return M4.eq_target ? p_post_eq : p_pre_eq;
}

static inline const char* m4_eq_active_name(void)
{
    return M4.eq_target ? "POST EQ" : "PRE EQ";
}

/* 편집 서브상태:
 *   0        = 밴드 선택 (band0~3 아이콘 한눈에)
 *   1..4     = band0~3 파라미터 편집
 *     sub-state 안에서:
 *       g_eq_type_sel == 1 → 필터 타입 선택 중 (JX 로 변경, SW3 로 확정)
 *       g_eq_type_sel == 0 → 파라미터(fc/gain/p2) 편집 중 (JY 선택, 노브 조절)
 */
static uint8_t g_eq_edit_sub = 0;   /* 0=밴드 선택, 1~4=band0~3 편집 */
static uint8_t g_eq_band_param = 0;  /* 0=fc, 1=gain_dB, 2=p2 */
static uint8_t g_eq_type_sel = 1;  /* 1=필터종류 선택 모드, 0=파라미터 편집 모드 */
static uint8_t g_eq_type_tmp = 0;  /* 타입 선택 중 임시값 */

/* 노브 배율 (SW_VOL_UP/DOWN 으로 변경):
 *   ADC_VOL   세밀 단위 × g_eq_fine_mult
 *   ADC_PITCH 큰   단위 × g_eq_coarse_mult
 *   기본값: fine=1, coarse=10
 *   SW_VOL_UP   → 배율 ×10 (최대 1000)
 *   SW_VOL_DOWN → 배율 ÷10 (최소 1)
 */
static uint16_t g_eq_fine_mult = 1;    /* fine   노브 배율 */
static uint16_t g_eq_coarse_mult = 10;   /* coarse 노브 배율 */
#define EQ_MULT_MAX  1000u
#define EQ_MULT_MIN  1u

/* ??????????????????????????????????????????????????????????????????????????????
 * [EQ 편집 엔진 v2]  입력 매핑 (4채널):
 *
 *   ADC_VOL   (노브 L)  → 현재 파라미터 값 ? fine 조절
 *                          절대값 매핑: 노브 위치 = 파라미터 값
 *   ADC_PITCH (노브 R)  → 현재 파라미터 값 ? coarse 조절 (같은 파라미터, 큰 스텝)
 *   JOY_Y               → 밴드 선택 (상=밴드↑, 하=밴드↓) / 타입 모드에서는 타입 변경
 *   JOY_X               → 파라미터 선택 (좌=이전, 우=다음)
 *
 *   SW1(VOL_UP)         → SW3와 동일 (Apply 확정)
 *   SW2(VOL_DOWN)       → SW5와 동일 (Revert)
 *   SW3(SELECT)         → Apply: working → HW 반영
 *   SW4(EXIT)           → 나가기 (변경사항 있으면 경고)
 *   SW5(SAVE)           → Revert: working을 applied로 복원
 *
 * 노브 절대값 매핑 원칙:
 *   - 진입 시 현재 파라미터 값을 노브 위치로부터 snap_offset에 기록
 *   - 파라미터 전환 시마다 새 snap_offset 계산
 *   - 실제 값 = snap_offset + (노브 - snap_pos) × scale
 *   → 노브 점프 없음, deadzone 불필요
 * ?????????????????????????????????????????????????????????????????????????????? */

typedef struct {
    uint8_t     band;           /* 현재 밴드 0-3          */
    uint8_t     param;          /* 0=fc 1=gain 2=Q/S 3=type */
    EqBandParam working[4];     /* 편집 중 버퍼            */
    EqBandParam applied[4];     /* 마지막 Apply 버퍼       */
    bool        changed;        /* working ≠ applied       */
    /* 노브 snap 상태 */
    uint16_t    snap_vol;       /* snap 시점 ADC_VOL 값    */
    uint16_t    snap_pitch;     /* snap 시점 ADC_PITCH 값  */
    double      snap_val;       /* snap 시점 파라미터 값   */
    /* 조이스틱 이전 상태 */
    uint8_t     joy_x_moved;
    uint8_t     joy_y_moved;
} eq2_state_t;

static eq2_state_t g_eq2 = { 0 };

/* ─── 파라미터별 노브 스케일 (ADC 0-4095 → 파라미터 변화량) ─── */
static double eq2_fine_scale(int band, int param)
{
    /* ADC_VOL: fine ? 전체 노브 범위에서 fine 범위를 커버 */
    switch (param) {
    case 0: /* Freq: band별 log-like 스텝. 전체 노브=±200Hz (fine) */
        return 200.0 / 2048.0;
    case 1: /* Gain: 전체 노브=±6 dB fine */
        return 6.0 / 2048.0;
    case 2: /* Q/S: 전체 노브=±1.0 fine */
        return 1.0 / 2048.0;
    default: return 0.0;
    }
    (void)band;
}

static double eq2_coarse_scale(int band, int param)
{
    /* ADC_PITCH: coarse ? fine의 ×20 */
    switch (param) {
    case 0: return 4000.0 / 2048.0;  /* ±4kHz */
    case 1: return 18.0 / 2048.0;    /* ±18 dB (전체 범위) */
    case 2: return 9.7 / 2048.0;     /* ±Q범위 절반 */
    default: return 0.0;
    }
    (void)band;
}

/* ─── 파라미터 범위 clamp 래퍼 ─── */
static double eq2_clamp_val(int band, int param, double v,
    eq_filter_type_t ftype)
{
    if (param == 0) {
        double lo, hi;
        eq_fc_range(ftype, &lo, &hi);
        return v < lo ? lo : (v > hi ? hi : v);
    }
    if (param == 1) {
        double lo = EQ_GAIN_MIN_DB, hi = EQ_GAIN_MAX_DB;
        return v < lo ? lo : (v > hi ? hi : v);
    }
    if (param == 2) {
        double lo, hi;
        eq_p2_range(ftype, &lo, &hi);
        return v < lo ? lo : (v > hi ? hi : v);
    }
    return v;
    (void)band;
}

/* ─── snap 설정: 파라미터/밴드 전환 시 호출 ─── */
static void eq2_snap(void)
{
    int b = g_eq2.band, p = g_eq2.param;
    EqBandParam* bp = &g_eq2.working[b];
    uint8_t np = EQ_FTYPE_PARAM_COUNT[bp->filter_type];
    g_eq2.snap_vol = adc_buf[ADC_IDX_VOL];
    g_eq2.snap_pitch = adc_buf[ADC_IDX_PITCH];
    if (np == 2) {
        /* BPF/NOTCH/APF/HPF/LPF: p0=fc, p1=Q(p2) */
        switch (p) {
        case 0: g_eq2.snap_val = bp->fc;  break;
        case 1: g_eq2.snap_val = bp->p2;  break;
        default: g_eq2.snap_val = 0.0;    break;
        }
    }
    else {
        /* BELL/SHELF/TILT: p0=fc, p1=gain_db, p2=Q/S */
        switch (p) {
        case 0: g_eq2.snap_val = bp->fc;      break;
        case 1: g_eq2.snap_val = bp->gain_db; break;
        case 2: g_eq2.snap_val = bp->p2;      break;
        default: g_eq2.snap_val = 0.0;        break;
        }
    }
}

/* ─── 버퍼 초기화 ─── */
static void eq_band_flush_to_hw(volatile uint8_t* base, EqBandParam band[4]);

static void eq_edit_init(bool is_post)
{
    EqBandParam* src = is_post ? g_post_eq_band : g_pre_eq_band;
    memcpy(g_eq2.applied, src, 4 * sizeof(EqBandParam));
    memcpy(g_eq2.working, src, 4 * sizeof(EqBandParam));
    g_eq2.band = 0;
    g_eq2.param = 1;  /* 기본: Gain 선택 */
    g_eq2.changed = false;
    g_eq2.joy_x_moved = 0;
    g_eq2.joy_y_moved = 0;
    eq2_snap();
}

static void eq_apply_changes(bool is_post)
{
    memcpy(g_eq2.applied, g_eq2.working, 4 * sizeof(EqBandParam));
    volatile uint8_t* hw = is_post ? p_post_eq : p_pre_eq;
    eq_band_flush_to_hw(hw, g_eq2.applied);
    EqBandParam* glob = is_post ? g_post_eq_band : g_pre_eq_band;
    memcpy(glob, g_eq2.applied, 4 * sizeof(EqBandParam));
    if (is_post) M4.edit.post_eq_custom_valid = 1u;
    else         M4.edit.pre_eq_custom_valid = 1u;
    g_eq2.changed = false;
}

static void eq_revert_changes(void)
{
    memcpy(g_eq2.working, g_eq2.applied, 4 * sizeof(EqBandParam));
    g_eq2.changed = false;
    eq2_snap();
}

/* ─── 화면 그리기 ─── */
static void m4_draw_eq_edit_improved(bool is_post)
{
    lcd_clear(COLOR_BLACK);
    int b = g_eq2.band, p = g_eq2.param;
    EqBandParam* bp = &g_eq2.working[b];
    uint8_t np = EQ_FTYPE_PARAM_COUNT[bp->filter_type];

    /* 헤더: PRE=녹, POST=노랑 */
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "%s EQ  B%d/4  [%s]%s",
        is_post ? "POST" : "PRE ", b + 1,
        EQ_FTYPE_NAMES[bp->filter_type],
        g_eq2.changed ? " *" : "");
    lcd_draw_header(is_post ? 0xFFE0u : 0x07E0u, hdr, COLOR_BLACK);

    /* 4개 밴드 요약 행 */
    {
        char row[48];
        for (int i = 0; i < 4; i++) {
            EqBandParam* bi = &g_eq2.working[i];
            uint8_t ni = EQ_FTYPE_PARAM_COUNT[bi->filter_type];
            if (ni >= 2)
                snprintf(row, sizeof(row), "B%d %-4s %5.0fHz %+.1fdB", i + 1,
                    EQ_FTYPE_NAMES[bi->filter_type], bi->fc, bi->gain_db);
            else if (ni == 1)
                snprintf(row, sizeof(row), "B%d %-4s %5.0fHz      ", i + 1,
                    EQ_FTYPE_NAMES[bi->filter_type], bi->fc);
            else
                snprintf(row, sizeof(row), "B%d BYPS              ", i + 1);
            uint16_t col = (i == b) ? COLOR_CYAN : COLOR_DKGREY;
            lcd_string(4, (uint16_t)(26 + i * 14), row, col, COLOR_BLACK);
        }
    }

    /* 현재 밴드 상세 편집 영역 */
    int y = 90;
    lcd_fill_rect(0, (uint16_t)y, 320, 100, 0x0841u); /* 어두운 배경 */

    if (np == 0) {
        lcd_string(40, (uint16_t)(y + 30), "BYPASS ? 이 밴드는 출력에 영향 없음",
            COLOR_DKGREY, 0x0841u);
    }
    else {
        /* Freq */
        {
            bool sel = (p == 0);
            char ln[40];
            snprintf(ln, sizeof(ln), "%-6s  %7.1f Hz", "Freq", bp->fc);
            lcd_string(8, (uint16_t)(y + 8), ln,
                sel ? COLOR_YELLOW : COLOR_WHITE, 0x0841u);
            if (sel) lcd_string(4, (uint16_t)(y + 8), ">", COLOR_YELLOW, 0x0841u);
        }
        /* Gain (Bell/Shelf/Tilt만 표시, BPF/NOTCH/APF/HPF/LPF는 gain 없음) */
        if (np == 3) {
            bool sel = (p == 1);
            char ln[40];
            snprintf(ln, sizeof(ln), "%-6s  %+7.2f dB", "Gain", bp->gain_db);
            lcd_string(8, (uint16_t)(y + 30), ln,
                sel ? COLOR_YELLOW : COLOR_WHITE, 0x0841u);
            if (sel) lcd_string(4, (uint16_t)(y + 30), ">", COLOR_YELLOW, 0x0841u);
        }
        /* Q/S (np==3: 3번째 파라미터 / np==2: 2번째 파라미터) */
        if (np >= 2) {
            bool sel = (np == 3) ? (p == 2) : (p == 1);
            char ln[40];
            snprintf(ln, sizeof(ln), "%-6s  %7.3f", eq_p2_name(bp->filter_type), bp->p2);
            int ypos = (np == 3) ? (y + 52) : (y + 30);
            lcd_string(8, (uint16_t)ypos, ln,
                sel ? COLOR_YELLOW : COLOR_WHITE, 0x0841u);
            if (sel) lcd_string(4, (uint16_t)ypos, ">", COLOR_YELLOW, 0x0841u);
        }
        /* Type */
        {
            bool sel = (p == 3);
            char ln[40];
            snprintf(ln, sizeof(ln), "%-6s  %s", "Type", EQ_FTYPE_NAMES[bp->filter_type]);
            lcd_string(8, (uint16_t)(y + 74), ln,
                sel ? COLOR_YELLOW : 0x5AEBu, 0x0841u);
            if (sel) lcd_string(4, (uint16_t)(y + 74), ">", COLOR_YELLOW, 0x0841u);
        }
    }

    /* 노브 바 그래픽 (VOL=fine, PITCH=coarse) */
    {
        uint16_t bx = 200;
        /* fine bar */
        uint16_t fw = (uint16_t)((uint32_t)adc_buf[ADC_IDX_VOL] * 116u / ADC_MAX);
        lcd_fill_rect(bx, (uint16_t)(y + 8), 116, 8, 0x2104u);
        lcd_fill_rect(bx, (uint16_t)(y + 8), fw, 8, 0x07E0u);
        lcd_string(bx, (uint16_t)(y + 18), "fine(VOL)", 0x5AEBu, COLOR_BLACK);
        /* coarse bar */
        uint16_t cw = (uint16_t)((uint32_t)adc_buf[ADC_IDX_PITCH] * 116u / ADC_MAX);
        lcd_fill_rect(bx, (uint16_t)(y + 32), 116, 8, 0x2104u);
        lcd_fill_rect(bx, (uint16_t)(y + 32), cw, 8, 0xFD20u);
        lcd_string(bx, (uint16_t)(y + 42), "crse(PITCH)", 0x5AEBu, COLOR_BLACK);
    }

    /* 하단 힌트 */
    lcd_string(4, 208, "JX:파라미터  JY:밴드/타입  VOL:fine  PITCH:coarse",
        COLOR_DKGREY, COLOR_BLACK);
    lcd_string(4, 222, is_post
        ? "Type행:SW1/2 종류  SW3:Apply  SW4:취소  SW5:저장→DELAY"
        : "Type행:SW1/2 종류  SW3:Apply  SW4:취소  SW5:저장→VCA",
        COLOR_CYAN, COLOR_BLACK);
}

static void m4_draw_eq_value_only_improved(void)
{
    /* 노브 바 + 현재 선택 파라미터 값만 갱신 */
    int b = g_eq2.band, p = g_eq2.param;
    EqBandParam* bp = &g_eq2.working[b];
    int y = 90;

    /* 파라미터 값 갱신 */
    char ln[40];
    int py;
    switch (p) {
    case 0: snprintf(ln, sizeof(ln), "%-6s  %7.1f Hz ", "Freq", bp->fc);      py = y + 8;  break;
    case 1: snprintf(ln, sizeof(ln), "%-6s  %+7.2f dB", "Gain", bp->gain_db); py = y + 30; break;
    case 2: snprintf(ln, sizeof(ln), "%-6s  %7.3f    ", eq_p2_name(bp->filter_type), bp->p2); py = y + 52; break;
    default: return;
    }
    lcd_fill_rect(8, (uint16_t)py, 190, 14, 0x0841u);
    lcd_string(8, (uint16_t)py, ln, COLOR_YELLOW, 0x0841u);

    /* 노브 바 */
    uint16_t bx = 200;
    uint16_t fw = (uint16_t)((uint32_t)adc_buf[ADC_IDX_VOL] * 116u / ADC_MAX);
    uint16_t cw = (uint16_t)((uint32_t)adc_buf[ADC_IDX_PITCH] * 116u / ADC_MAX);
    lcd_fill_rect(bx, (uint16_t)(y + 8), 116, 8, 0x2104u);
    lcd_fill_rect(bx, (uint16_t)(y + 8), fw, 8, 0x07E0u);
    lcd_fill_rect(bx, (uint16_t)(y + 32), 116, 8, 0x2104u);
    lcd_fill_rect(bx, (uint16_t)(y + 32), cw, 8, 0xFD20u);

    /* 헤더 * 표시 갱신 */
    lcd_fill_rect(290, 0, 30, 18, g_eq2.changed ? 0xF800u : (int)(/* is_post 없음?헤더 색 근사 */ 0));
}

static void m4_flash_applied_message(void) {
    lcd_fill_rect(0, 220, 320, 20, COLOR_GREEN);
    lcd_string(90, 222, "APPLIED!", COLOR_WHITE, COLOR_GREEN);
    usleep(200000);
}

/* ─── 메인 업데이트 ─── */
static uint8_t g_eq_edit_last_sw = 0;  /* [FIX] mode4_eq_enter에서 리셋 가능하도록 */

static int m4_update_eq_edit_improved(spi_packet_t* rx, bool is_post)
{
    /* [FIX] static local -> 파일 수준 g_eq_edit_last_sw: mode4_eq_enter 리셋 가능 */
#define last_sw g_eq_edit_last_sw
    uint8_t sw = rx->sw_status;

    bool full_redraw = false;
    bool value_redraw = false;

    /* ── JOY_X: 파라미터 선택 ── */
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    int b = g_eq2.band;
    EqBandParam* bp = &g_eq2.working[b];
    uint8_t np = EQ_FTYPE_PARAM_COUNT[bp->filter_type];
    uint8_t max_param = (np == 0) ? 3u : (uint8_t)(np + 1u); /* type 항목 포함 */

    if (jx < JOY_LOW && !g_eq2.joy_x_moved) {
        g_eq2.param = (g_eq2.param == 0) ? max_param - 1 : g_eq2.param - 1;
        eq2_snap(); g_eq2.joy_x_moved = 1; full_redraw = true;
    }
    else if (jx > JOY_HIGH && !g_eq2.joy_x_moved) {
        g_eq2.param = (g_eq2.param + 1) % max_param;
        eq2_snap(); g_eq2.joy_x_moved = 1; full_redraw = true;
    }
    if (jx >= JOY_DEAD_LO && jx <= JOY_DEAD_HI) g_eq2.joy_x_moved = 0;

    /* ── JOY_Y: 밴드 선택 or 타입 변경 ── */
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    int p = g_eq2.param;
    if (p == 3) {
        /* 타입 모드: JY로 타입 순환 */
        if (jy < JOY_LOW && !g_eq2.joy_y_moved) {
            bp->filter_type = (bp->filter_type == 0)
                ? (uint8_t)(EQ_FTYPE_COUNT - 1) : bp->filter_type - 1;
            bp->p2 = (float)eq_p2_default(bp->filter_type);
            eq_clamp_params(bp->filter_type, &bp->fc, &bp->gain_db, &bp->p2);
            g_eq2.changed = true; g_eq2.joy_y_moved = 1; full_redraw = true;
        }
        else if (jy > JOY_HIGH && !g_eq2.joy_y_moved) {
            bp->filter_type = (bp->filter_type + 1) % EQ_FTYPE_COUNT;
            bp->p2 = (float)eq_p2_default(bp->filter_type);
            eq_clamp_params(bp->filter_type, &bp->fc, &bp->gain_db, &bp->p2);
            g_eq2.changed = true; g_eq2.joy_y_moved = 1; full_redraw = true;
        }
    }
    else {
        /* 일반 파라미터 모드: JY로 밴드 이동 */
        if (jy < JOY_LOW && !g_eq2.joy_y_moved && g_eq2.band > 0) {
            g_eq2.band--; eq2_snap();
            g_eq2.joy_y_moved = 1; full_redraw = true;
        }
        else if (jy > JOY_HIGH && !g_eq2.joy_y_moved && g_eq2.band < 3) {
            g_eq2.band++; eq2_snap();
            g_eq2.joy_y_moved = 1; full_redraw = true;
        }
    }
    if (jy >= JOY_DEAD_LO && jy <= JOY_DEAD_HI) g_eq2.joy_y_moved = 0;

    /* ── 노브 절대값 매핑 (파라미터 0-2만) ── */
    p = g_eq2.param;  /* 밴드/파라미터 변경 후 재취득 */
    b = g_eq2.band;
    bp = &g_eq2.working[b];
    if (p <= 2) {
        uint16_t vol = adc_buf[ADC_IDX_VOL];
        uint16_t pitch = adc_buf[ADC_IDX_PITCH];
        double fine_s = eq2_fine_scale(b, p);
        double coarse_s = eq2_coarse_scale(b, p);
        int dvol = (int)vol - (int)g_eq2.snap_vol;
        int dpitch = (int)pitch - (int)g_eq2.snap_pitch;
        double new_val = g_eq2.snap_val
            + (double)dvol * fine_s
            + (double)dpitch * coarse_s;

        /* BPF/NOTCH/APF/HPF/LPF (np==2): param0=fc, param1=Q(p2)
         * BELL/SHELF/TILT        (np==3): param0=fc, param1=gain, param2=Q(p2) */
        uint8_t np_cur = EQ_FTYPE_PARAM_COUNT[bp->filter_type];
        double* tgt;
        int clamp_param; /* eq2_clamp_val에 넘길 논리적 파라미터 인덱스 */
        if (np_cur == 2) {
            /* param0=fc, param1=p2(Q) */
            tgt = (p == 0) ? &bp->fc : &bp->p2;
            clamp_param = (p == 0) ? 0 : 2;
        }
        else {
            /* param0=fc, param1=gain_db, param2=p2(Q/S) */
            tgt = (p == 0) ? &bp->fc : (p == 1) ? &bp->gain_db : &bp->p2;
            clamp_param = p;
        }
        double clamped = eq2_clamp_val(b, clamp_param, new_val,
            (eq_filter_type_t)bp->filter_type);
        if (clamped != *tgt) {
            *tgt = clamped;
            g_eq2.changed = true;
            value_redraw = true;
        }
    }

    /* ── 버튼 처리 ── */
    if (g_eq2.param == 3 && sw == SW_VOL_UP && last_sw != sw) {
        /* [REQ-M4-EQ-ADC] 필터 종류를 조이스틱 ADC에만 의존하지 않도록
         * Type 행에서는 SW1/SW2로 확정적인 이전/다음 타입 선택을 지원한다. */
        bp->filter_type = (uint8_t)((bp->filter_type + 1u) % EQ_FTYPE_COUNT);
        bp->p2 = (float)eq_p2_default((eq_filter_type_t)bp->filter_type);
        eq_clamp_params((eq_filter_type_t)bp->filter_type, &bp->fc, &bp->gain_db, &bp->p2);
        g_eq2.changed = true;
        full_redraw = true;
    }
    else if (g_eq2.param == 3 && sw == SW_VOL_DOWN && last_sw != sw) {
        bp->filter_type = (uint8_t)((bp->filter_type == 0u) ? (EQ_FTYPE_COUNT - 1u) : (bp->filter_type - 1u));
        bp->p2 = (float)eq_p2_default((eq_filter_type_t)bp->filter_type);
        eq_clamp_params((eq_filter_type_t)bp->filter_type, &bp->fc, &bp->gain_db, &bp->p2);
        g_eq2.changed = true;
        full_redraw = true;
    }
    else if ((sw == SW_SELECT || sw == SW_VOL_UP) && last_sw != sw) {
        eq_apply_changes(is_post);
        m4_preview_fm_full((uint8_t)M4.fm_ch, M4.fm_inst_cur); /* [FIX] EQ 변화 preview */
        m4_flash_applied_message();
        full_redraw = true;
    }
    else if (sw == SW_SAVE && last_sw != sw) {
        if (is_post) {
            /* POST_EQ SW5: 저장 확정 후 Stage3 체인 최종 적용 */
            eq_apply_changes(true);
            g_eq_edit_sub = 0;
            last_sw = sw;
            return -2;   /* 호출자(mode_instsetup_update)가 최종 저장/메뉴 복귀 처리 */
        }
        /* PRE_EQ SW5: 저장 확정 후 VCA 편집으로 전진
         * 기존에는 SW5가 복원으로 동작해 '저장' 요구와 UI 안내가 불일치했다. */
        eq_apply_changes(false);
        g_eq_edit_sub = 0;
        last_sw = sw;
        return -3;       /* 호출자(mode_instsetup_update)가 VCA_EDIT으로 전환 */
    }
    else if (sw == SW_VOL_DOWN && last_sw != sw) {
        eq_revert_changes();
        full_redraw = true;
    }
    else if (sw == SW_EXIT && last_sw != SW_EXIT) {
        if (g_eq2.changed) {
            /* 미적용 경고 1초 표시 후 그냥 나감 */
            lcd_fill_rect(0, 208, 320, 32, 0x8000u);
            lcd_string(10, 212, "미적용 변경사항 있음! SW3으로 적용 후 나가세요.", COLOR_WHITE, 0x8000u);
            lcd_string(10, 224, "한 번 더 SW4 → 폐기하고 나가기", COLOR_YELLOW, 0x8000u);
            usleep(800000);
            if (rx->sw_status == SW_EXIT) {   /* 아직 눌린 상태면 진행 */
                eq_revert_changes();
                last_sw = sw;
                return -1;
            }
            full_redraw = true;
        }
        else {
            last_sw = sw;
            return -1;
        }
    }

    last_sw = sw;

    if (full_redraw)        m4_draw_eq_edit_improved(is_post);
    else if (value_redraw)  m4_draw_eq_value_only_improved();

    return ZYNQ_MODE_INSTSETUP;
#undef last_sw
}

/* ???????????????????????????????????????????????????????????????????????????
 * [Rev.4] EQ 모드 선택 화면  (선형 시퀀스 삽입 포인트)
 *
 *  선형 시퀀스:
 *    HARMONY → [PRE EQ 모드선택] → PRE EQ → VCA → [POST EQ 모드선택] → POST EQ → DELAY
 *
 *  SW1(VOL_UP)  : 프리셋 선택 모드 → M4_PRE/POST_EQ_PRESET
 *  SW2(VOL_DOWN): 직접 편집 모드   → M4_PRE/POST_EQ_EDIT  (현재 band 파라미터 유지)
 *  SW3(SELECT)  : [PRE EQ 전용] 8밴드 통합 프리셋 → M4_EQ8_PRESET
 *                 저장 시 PRE/POST EQ HW에 8밴드 적용 후 POST EQ 설정 화면을 건너뛰고 DELAY로 이동
 *  SW5(SAVE)    : 스킵 (현재 EQ 그대로 다음 단계 진행)
 *  SW4(EXIT)    : 이전 화면으로 복귀 (PRE→HARMONY, POST→VCA)
 * ??????????????????????????????????????????????????????????????????????????? */

static uint8_t g_eq_msel_last_sw = 0;

static void m4_draw_eq_mode_sel(bool is_post)
{
    lcd_clear(COLOR_BLACK);
    uint16_t hcol = is_post ? 0xFFE0u : 0x07E0u;
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "%s EQ  설정 방법 선택", is_post ? "POST" : "PRE ");
    lcd_draw_header(hcol, hdr, COLOR_BLACK);

    /* 현재 적용 중인 프리셋 표시 */
    uint8_t cur = is_post ? M4.edit.post_eq_preset : M4.edit.pre_eq_preset;
    char cur_line[48];
    snprintf(cur_line, sizeof(cur_line), "현재: [%02u] %s",
        (unsigned)cur, EQ_PRESET_NAMES[cur]);
    lcd_string(8, 26, cur_line, 0x5AEBu, COLOR_BLACK);

    if (!is_post) {
        /* PRE EQ 화면은 팀원 요청에 맞춰 SW1/SW2 목록에 SW3 = 8BAND PRESET 분기를 추가한다. */
        lcd_fill_rect(8, 52, 304, 44, 0x0820u);
        lcd_string(14, 58, "SW1  PRE EQ 4밴드 프리셋", 0x07E0u, 0x0820u);
        lcd_string(14, 74, "기존 4밴드 프리셋 선택 후 필요 시 밴드 편집", COLOR_WHITE, 0x0820u);

        lcd_fill_rect(8, 100, 304, 44, 0x1808u);
        lcd_string(14, 106, "SW2  PRE EQ 직접 편집", 0xFD20u, 0x1808u);
        lcd_string(14, 122, "4밴드 파라미터(Freq/Gain/Q/Type) 직접 조절", COLOR_WHITE, 0x1808u);

        lcd_fill_rect(8, 148, 304, 48, 0x0018u);
        lcd_string(14, 154, "SW3  8밴드 통합 프리셋", 0x07FFu, 0x0018u);
        lcd_string(14, 170, "PRE Band0~3 + POST Band4~7 동시 적용", COLOR_WHITE, 0x0018u);
        lcd_string(14, 184, "저장 후 POST EQ 설정 화면 생략 → DELAY 이동", COLOR_YELLOW, 0x0018u);

        lcd_string(8, 208, "SW5  현재 EQ 그대로 VCA 단계로 스킵", 0x8410u, COLOR_BLACK);
        lcd_string(8, 224, "SW4  뒤로 (HARMONY)", 0x8410u, COLOR_BLACK);
    }
    else {
        /* POST EQ 화면은 기존 2가지 선택지만 유지한다. EQ8은 PRE EQ에서만 진입한다. */
        lcd_fill_rect(8, 60, 304, 56, 0x0820u);
        lcd_string(14, 66, "SW1  POST EQ 4밴드 프리셋", 0x07E0u, 0x0820u);
        lcd_string(14, 82, "56개 프리셋에서 고르고 필요 시 밴드 미세조정", COLOR_WHITE, 0x0820u);
        lcd_string(14, 98, "[PST] 권장 프리셋이 상단에 표시됩니다", COLOR_DKGREY, 0x0820u);

        lcd_fill_rect(8, 124, 304, 56, 0x1808u);
        lcd_string(14, 130, "SW2  POST EQ 직접 편집", 0xFD20u, 0x1808u);
        lcd_string(14, 146, "4밴드 파라미터(Freq/Gain/Q/Type) 직접 조절", COLOR_WHITE, 0x1808u);
        lcd_string(14, 162, "현재 band 파라미터를 그대로 이어서 편집합니다", COLOR_DKGREY, 0x1808u);

        lcd_string(8, 192, "SW5  현재 POST EQ 그대로 DELAY 단계로 스킵", 0x8410u, COLOR_BLACK);
        lcd_string(8, 208, "SW4  뒤로 (VCA)", 0x8410u, COLOR_BLACK);
    }
}

/* 반환값:
 *   -1  = SW1 → 프리셋 선택으로
 *   -2  = SW2 → 직접 편집으로
 *   -3  = SW5 → 스킵(다음 단계)
 *   -4  = SW4 → 이전으로
 *   -5  = SW3 → [PRE EQ 전용] 8밴드 통합 프리셋 화면으로
 *   ZYNQ_MODE_INSTSETUP = 대기
 */
static int m4_update_eq_mode_sel(uint8_t sw, bool is_post)
{
    if (sw == SW_VOL_UP && g_eq_msel_last_sw != SW_VOL_UP) { g_eq_msel_last_sw = sw; return -1; }
    if (sw == SW_VOL_DOWN && g_eq_msel_last_sw != SW_VOL_DOWN) { g_eq_msel_last_sw = sw; return -2; }
    /* 팀원 요청: PRE EQ 선택 화면의 세 번째 선택지(SW3)로 8밴드 통합 프리셋 화면 진입 */
    if (!is_post && sw == SW_SELECT && g_eq_msel_last_sw != SW_SELECT) { g_eq_msel_last_sw = sw; return -5; }
    if (sw == SW_SAVE && g_eq_msel_last_sw != SW_SAVE) { g_eq_msel_last_sw = sw; return -3; }
    if (sw == SW_EXIT && g_eq_msel_last_sw != SW_EXIT) { g_eq_msel_last_sw = sw; return -4; }
    g_eq_msel_last_sw = sw;
    return ZYNQ_MODE_INSTSETUP;
}

static void mode4_eq_mode_sel_enter(bool is_post, uint8_t trigger_sw)
{
    g_eq_msel_last_sw = trigger_sw;
    m4_draw_eq_mode_sel(is_post);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * [EQ8] 8밴드 통합 프리셋 화면
 *   PRE EQ HW = EQ8 밴드 0~3, POST EQ HW = EQ8 밴드 4~7.
 *   SW_SELECT(SW3): 선택 스위치로 PRE EQ 프리셋 화면에서 진입/확정.
 *   JY: 프리셋 이동 + 즉시 HW 임시 적용, SW_SAVE(SW5)도 저장 확정.
 * ─────────────────────────────────────────────────────────────────────────── */
static uint8_t g_eq8_cur = 0;
static uint8_t g_eq8_last_sw = 0xFF;
static uint8_t g_eq8_joy_mv = 0;
/* [EQ8-100%-FIX]
 * EQ8 화면은 팀원 요청에 따라 PRE EQ 선택 화면의 SW3에서만 진입한다.
 * - g_eq8_from_post는 기존 호환용 경로 정보로 유지하되, 정상 UI 흐름에서는 0(PRE)만 사용한다.
 * - g_eq8_entry_snap: EQ8 preview 중 JY 이동으로 HW에 임시 적용한 값을 SW4에서 취소/복원하기 위한 진입 시점 스냅샷
 */
static uint8_t      g_eq8_from_post = 0;
static full_preset_t g_eq8_entry_snap;
static uint8_t      g_eq8_entry_valid = 0;

static const char* eq8_name_safe(uint8_t idx)
{
    if ((uint32_t)idx >= EQ8_PRESET_COUNT) idx = 0;
    return EQ8_PRESET_NAMES[idx];
}

static void m4_draw_eq8_preset(void)
{
    lcd_clear(COLOR_BLACK);
    lcd_draw_header(0x07FFu, "8BAND EQ PRE+POST", COLOR_BLACK);

    char line[80];
    uint8_t idx = (g_eq8_cur < EQ8_PRESET_COUNT) ? g_eq8_cur : 0;
    snprintf(line, sizeof(line), "[%02u/%02u] %s",
        (unsigned)(idx + 1u), (unsigned)EQ8_PRESET_COUNT, eq8_name_safe(idx));
    lcd_string(8, 30, line, COLOR_WHITE, COLOR_BLACK);

    lcd_string(8, 54, "PRE EQ : Band 0~3", 0x07E0u, COLOR_BLACK);
    lcd_string(8, 70, "POST EQ: Band 4~7", 0xFFE0u, COLOR_BLACK);

    if (idx > 0) {
        snprintf(line, sizeof(line), "  %02u %s", (unsigned)(idx - 1u), eq8_name_safe((uint8_t)(idx - 1u)));
        lcd_string(8, 104, line, COLOR_DKGREY, COLOR_BLACK);
    }
    snprintf(line, sizeof(line), "> %02u %s", (unsigned)idx, eq8_name_safe(idx));
    lcd_string(8, 120, line, 0x07FFu, COLOR_BLACK);
    if ((uint32_t)idx + 1u < EQ8_PRESET_COUNT) {
        snprintf(line, sizeof(line), "  %02u %s", (unsigned)(idx + 1u), eq8_name_safe((uint8_t)(idx + 1u)));
        lcd_string(8, 136, line, COLOR_DKGREY, COLOR_BLACK);
    }

    lcd_string(8, 188, "JY: preset move + preview", 0x5AEBu, COLOR_BLACK);
    lcd_string(8, 206, "SW3/SW5: save -> DELAY  SW4: back", 0x8410u, COLOR_BLACK);
}

/* [EQ8-100%-FIX]
 * EQ8 preview는 즉시 HW에 임시 적용되지만, SW4로 나갈 경우 저장되어서는 안 된다.
 * 따라서 SW4 취소 시 진입 당시 M4.edit 상태로 PRE/POST EQ HW를 복원한다.
 */
static void m4_eq8_restore_entry_state(void)
{
    if (!g_eq8_entry_valid) return;

    memcpy(&M4.edit, &g_eq8_entry_snap, sizeof(full_preset_t));

    if (M4.edit.eq8_enabled && M4.edit.eq8_preset < EQ8_PRESET_COUNT && p_pre_eq && p_post_eq) {
        apply_eq8_idx(p_pre_eq, p_post_eq, M4.edit.eq8_preset);
    }
    else {
        if (DIAG_USE_PRE_EQ && p_pre_eq) {
            if (M4.edit.pre_eq_custom_valid) eq_band_flush();
            else apply_pre_eq_preset(M4.edit.pre_eq_preset);
        }
        else {
            diag_bypass_pre_eq("eq8_cancel_restore");
        }

        if (DIAG_USE_POST_EQ && p_post_eq) {
            if (M4.edit.post_eq_custom_valid) eq_band_flush_post();
            else apply_post_eq_preset(M4.edit.post_eq_preset);
        }
        else {
            diag_bypass_post_eq("eq8_cancel_restore");
        }
    }

    diag_apply_chain_policy("eq8_cancel_restore");
    m4_preview_current_ym();
    printf("[EQ8] CANCEL/RESTORE idx=%u enabled=%u\n",
        (unsigned)M4.edit.eq8_preset, (unsigned)M4.edit.eq8_enabled);
}

static void m4_eq8_apply(uint8_t idx, int commit)
{
    if ((uint32_t)idx >= EQ8_PRESET_COUNT) idx = 0;
    g_eq8_cur = idx;

    if (p_pre_eq && p_post_eq) {
        apply_eq8_idx(p_pre_eq, p_post_eq, idx);
    }

    M4.edit.eq8_enabled = 1u;
    M4.edit.eq8_preset = idx;
    M4.edit.pre_eq_custom_valid = 0u;
    M4.edit.post_eq_custom_valid = 0u;

    if (commit) {
        g_full_preset.eq8_enabled = 1u;
        g_full_preset.eq8_preset = idx;
        g_full_preset.pre_eq_custom_valid = 0u;
        g_full_preset.post_eq_custom_valid = 0u;
        M4.snap.eq8_enabled = 1u;
        M4.snap.eq8_preset = idx;
    }

    diag_apply_chain_policy("eq8_apply");
    m4_preview_current_ym();
    printf("[EQ8] %s idx=%u %s\n", commit ? "COMMIT" : "PREVIEW", (unsigned)idx, eq8_name_safe(idx));
}

static int m4_update_eq8_preset(uint8_t sw)
{
    int jy = adc_buf[ADC_IDX_PITCH];

    if (jy < JOY_LOW && !g_eq8_joy_mv) {
        if (g_eq8_cur > 0) g_eq8_cur--;
        g_eq8_joy_mv = 1;
        m4_eq8_apply(g_eq8_cur, 0);
        m4_draw_eq8_preset();
    }
    else if (jy > JOY_HIGH && !g_eq8_joy_mv) {
        if ((uint32_t)g_eq8_cur + 1u < EQ8_PRESET_COUNT) g_eq8_cur++;
        g_eq8_joy_mv = 1;
        m4_eq8_apply(g_eq8_cur, 0);
        m4_draw_eq8_preset();
    }
    else if (jy >= JOY_LOW && jy <= JOY_HIGH) {
        g_eq8_joy_mv = 0;
    }

    if ((sw == SW_SELECT || sw == SW_SAVE) && g_eq8_last_sw != sw) {
        g_eq8_last_sw = sw;
        m4_eq8_apply(g_eq8_cur, 1);
        return -3;
    }
    if (sw == SW_EXIT && g_eq8_last_sw != SW_EXIT) {
        g_eq8_last_sw = sw;
        return -4;
    }

    g_eq8_last_sw = sw;
    return ZYNQ_MODE_INSTSETUP;
}

static void mode4_eq8_preset_enter(bool from_post, uint8_t trigger_sw)
{
    g_eq8_from_post = from_post ? 1u : 0u;
    memcpy(&g_eq8_entry_snap, &M4.edit, sizeof(full_preset_t));
    g_eq8_entry_valid = 1u;
    g_eq8_cur = (M4.edit.eq8_enabled && M4.edit.eq8_preset < EQ8_PRESET_COUNT) ? M4.edit.eq8_preset : 0u;
    g_eq8_last_sw = trigger_sw;
    g_eq8_joy_mv = 1;
    m4_draw_eq8_preset();
}

/* ???????????????????????????????????????????????????????????????????????????
 * [Rev.3] EQ 프리셋 선택 화면
 *
 *  흐름: [EQ 모드선택] → SW1 → M4_PRE/POST_EQ_PRESET (이 화면)
 *    JY ↑↓  : 프리셋 커서 이동 (1칸)
 *    JX ←→  : 프리셋 커서 이동 (5칸 빠른 이동)
 *    SW3     : 프리셋 HW 적용 → M4_PRE/POST_EQ_EDIT (밴드 편집)
 *    SW5     : 프리셋만 적용+저장 (PRE:-3→VCA, POST:-2→DELAY)
 *    SW4     : 뒤로 (EQ 모드선택으로)
 * ??????????????????????????????????????????????????????????????????????????? */

static uint8_t g_eq_preset_cur = 0;
static uint8_t g_eq_preset_last_sw = 0;
static uint8_t g_eq_preset_joy_y = 0;
static uint8_t g_eq_preset_joy_x = 0;

#define EQ_PRESET_ROWS   7

/*
 *  eq_preset_apply_to_band_table()
 *  정적 프리셋 계수를 HW에 기록하고 EqBandParam 테이블을 초기화.
 *  - HW: eq_write_preset_atomic (4밴드 원자 기록)
 *  - EqBandParam: 기본 flat 초기값 (0dB, default fc/Q)으로 초기화.
 *    → 이후 밴드 편집기 진입 시 "flat 상태에서 추가 미세조정" 가능.
 *    → HW는 이미 프리셋 계수가 적용된 상태이므로 소리는 프리셋대로 남.
 *  - pre/post_eq_custom_valid = 0 (프리셋 기반 표시)
 */
static void eq_preset_apply_to_band_table(bool is_post, uint8_t idx)
{
    if (idx >= EQ_PRESET_COUNT) idx = 0;

    volatile uint8_t* hw = is_post ? p_post_eq : p_pre_eq;
    if (hw) {
        eq_write_preset_atomic(hw, EQ_PRESET_DATA[idx]);
    }

    /* EqBandParam → flat 초기화 (HW는 프리셋, band 테이블은 편집 시작점) */
    EqBandParam* tbl = is_post ? g_post_eq_band : g_pre_eq_band;
    static const double def_fc[4] = {
        EQ_FC_DEFAULT_BAND0, EQ_FC_DEFAULT_BAND1,
        EQ_FC_DEFAULT_BAND2, EQ_FC_DEFAULT_BAND3
    };
    static const eq_filter_type_t def_type[4] = {
        EQ_FTYPE_LOW_SHELF, EQ_FTYPE_BELL, EQ_FTYPE_BELL, EQ_FTYPE_HIGH_SHELF
    };
    for (int b = 0; b < 4; b++) {
        tbl[b].filter_type = def_type[b];
        tbl[b].fc = def_fc[b];
        tbl[b].gain_db = 0.0;
        tbl[b].p2 = eq_p2_default(def_type[b]);
    }

    if (is_post) {
        M4.edit.post_eq_preset = idx;
        M4.edit.post_eq_custom_valid = 0u;
    }
    else {
        M4.edit.pre_eq_preset = idx;
        M4.edit.pre_eq_custom_valid = 0u;
    }
}

/* ── 프리셋 선택 화면 그리기 ── */
static void m4_draw_eq_preset_sel(bool is_post)
{
    lcd_clear(COLOR_BLACK);
    uint8_t idx = g_eq_preset_cur;
    uint8_t cur_stored = is_post ? M4.edit.post_eq_preset : M4.edit.pre_eq_preset;

    char hdr[48];
    snprintf(hdr, sizeof(hdr), "%s EQ PRESET  [%u/%u]",
        is_post ? "POST" : "PRE ", (unsigned)(idx + 1u), (unsigned)EQ_PRESET_COUNT);
    lcd_draw_header(is_post ? 0xFFE0u : 0x07E0u, hdr, COLOR_BLACK);

    /* 현재 적용 프리셋 */
    char cur[40];
    snprintf(cur, sizeof(cur), "APPLY: [%02u] %-14s", (unsigned)cur_stored,
        EQ_PRESET_NAMES[cur_stored]);
    lcd_string(4, 20, cur, 0x5AEBu, COLOR_BLACK);

    /* 목록 (EQ_PRESET_ROWS줄 스크롤) */
    int half = EQ_PRESET_ROWS / 2;
    int start = (int)idx - half;
    if (start < 0) start = 0;
    if (start + EQ_PRESET_ROWS > (int)EQ_PRESET_COUNT)
        start = (int)EQ_PRESET_COUNT - EQ_PRESET_ROWS;
    if (start < 0) start = 0;

    for (int r = 0; r < EQ_PRESET_ROWS; r++) {
        int pi = start + r;
        if (pi >= (int)EQ_PRESET_COUNT) break;
        bool is_cur = (pi == (int)idx);
        bool is_appl = (pi == (int)cur_stored);
        uint16_t col = is_cur ? COLOR_YELLOW : (is_appl ? 0x07FFu : COLOR_WHITE);
        char line[48];
        snprintf(line, sizeof(line), "%s[%02d] %-14s%s",
            is_cur ? ">" : " ", pi,
            EQ_PRESET_NAMES[pi],
            is_appl ? " *" : "");
        lcd_string(4, (uint16_t)(36 + r * 18), line, col, COLOR_BLACK);
    }

    lcd_string(4, 208, "JY:1칸  JX:5칸  SW3:적용+밴드편집", COLOR_DKGREY, COLOR_BLACK);
    lcd_string(4, 222, "SW5:프리셋저장만  SW4:취소", COLOR_CYAN, COLOR_BLACK);
}

/* ── 프리셋 선택 화면 업데이트 ── */
static int m4_update_eq_preset_sel(spi_packet_t* rx, bool is_post)
{
#define prs_last g_eq_preset_last_sw
    uint8_t  sw = rx->sw_status;
    bool     redraw = false;
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];

    /* JY: 1칸 이동 */
    if (jy < JOY_LOW && !g_eq_preset_joy_y) {
        if (g_eq_preset_cur > 0) { g_eq_preset_cur--; redraw = true; }
        g_eq_preset_joy_y = 1;
    }
    else if (jy > JOY_HIGH && !g_eq_preset_joy_y) {
        if (g_eq_preset_cur < EQ_PRESET_COUNT - 1u) { g_eq_preset_cur++; redraw = true; }
        g_eq_preset_joy_y = 1;
    }
    if (jy >= JOY_DEAD_LO && jy <= JOY_DEAD_HI) g_eq_preset_joy_y = 0;

    /* JX: 5칸 이동 */
    if (jx < JOY_LOW && !g_eq_preset_joy_x) {
        g_eq_preset_cur = (g_eq_preset_cur >= 5u) ? g_eq_preset_cur - 5u : 0u;
        g_eq_preset_joy_x = 1; redraw = true;
    }
    else if (jx > JOY_HIGH && !g_eq_preset_joy_x) {
        uint8_t nxt = g_eq_preset_cur + 5u;
        g_eq_preset_cur = (nxt < EQ_PRESET_COUNT) ? nxt : (uint8_t)(EQ_PRESET_COUNT - 1u);
        g_eq_preset_joy_x = 1; redraw = true;
    }
    if (jx >= JOY_DEAD_LO && jx <= JOY_DEAD_HI) g_eq_preset_joy_x = 0;

    /* SW3: 프리셋 적용 → 밴드 편집 진입 */
    if (sw == SW_SELECT && prs_last != SW_SELECT) {
        eq_preset_apply_to_band_table(is_post, g_eq_preset_cur);
        lcd_fill_rect(0, 208, 320, 32, 0x0010u);
        char msg[48];
        snprintf(msg, sizeof(msg), "PRESET [%02u] %s  HW적용됨",
            (unsigned)g_eq_preset_cur, EQ_PRESET_NAMES[g_eq_preset_cur]);
        lcd_string(4, 212, msg, COLOR_WHITE, 0x0010u);
        lcd_string(4, 224, "밴드 편집 화면으로...", COLOR_CYAN, 0x0010u);
        usleep(350000);
        prs_last = sw;
        return -10;  /* 호출자: 밴드 편집 화면 전환 */
    }

    /* SW5: 프리셋만 저장하고 다음 단계 */
    if (sw == SW_SAVE && prs_last != SW_SAVE) {
        eq_preset_apply_to_band_table(is_post, g_eq_preset_cur);
        lcd_fill_rect(0, 208, 320, 32, COLOR_GREEN);
        char msg[48];
        snprintf(msg, sizeof(msg), "PRESET [%02u] %s  저장완료",
            (unsigned)g_eq_preset_cur, EQ_PRESET_NAMES[g_eq_preset_cur]);
        lcd_string(4, 212, msg, COLOR_WHITE, COLOR_GREEN);
        usleep(350000);
        prs_last = sw;
        return is_post ? -2 : -3;
    }

    /* SW4: 취소 */
    if (sw == SW_EXIT && prs_last != SW_EXIT) {
        prs_last = sw;
        return -1;
    }

    prs_last = sw;
    if (redraw) m4_draw_eq_preset_sel(is_post);
    return ZYNQ_MODE_INSTSETUP;
#undef prs_last
}

/* ── 프리셋 선택 화면 진입 ── */
static void mode4_eq_preset_enter(bool is_post, uint8_t trigger_sw)
{
    g_eq_preset_cur = is_post ? M4.edit.post_eq_preset : M4.edit.pre_eq_preset;
    g_eq_preset_last_sw = trigger_sw;
    g_eq_preset_joy_y = 0;
    g_eq_preset_joy_x = 0;
    m4_draw_eq_preset_sel(is_post);
}

/* ─────────────────────────────────────────────────────────────── */
/* [9] 진입 함수 */
static void mode4_eq_enter(bool is_post, uint8_t trigger_sw) {
    /* [FIX] g_eq_edit_last_sw를 현재 루프의 sw(트리거 버튼)로 prime.
     * 이전에 M4.prev_sw를 썼으나 M4.prev_sw는 이 시점에서 아직 이전 루프 값이라
     * 다음 루프에서 sw==trigger_sw && last_sw!=trigger_sw 가 즉시 성립해
     * EQ 화면을 한 프레임도 보여주지 않고 탈출하는 버그가 있었다.
     * trigger_sw로 prime하면 next loop에서 last_sw==sw → 엣지 조건 불통과. */
    g_eq_edit_last_sw = trigger_sw;
    eq_edit_init(is_post);
    m4_draw_eq_edit_improved(is_post);
}

/* ──────────────────────────────────────────────────────────────
 * [모드4 개선 코드 끝]
 * ────────────────────────────────────────────────────────────── */

static void eq_band_flush_to_hw(volatile uint8_t* base, EqBandParam band[4])
{
    /* coef_bypass=1(0x02) → 계수 계산+write → coef_bypass=0(0x00)
     * base가 가리키는 EQ HW의 ctrl 오프셋은 0x100 (EQ_REG_CTRL과 동일) */
    if (!base) return;
    reg_wr(base, 0x100u, PRE_EQ_CTRL_COEF_BYPASS);
    for (int b = 0; b < 4; b++) {
        BiquadCoef c = eq_calc_band(
            band[b].filter_type,
            band[b].fc,
            band[b].gain_db,
            band[b].p2);
        eq_write_band(base, b, &c);
    }
    reg_wr(base, 0x100u, PRE_EQ_CTRL_RUN);
}

static void eq_band_flush(void)
{
    eq_band_flush_to_hw(p_pre_eq, g_pre_eq_band);
}

static void eq_band_flush_post(void)
{
    eq_band_flush_to_hw(p_post_eq, g_post_eq_band);
}

static void eq_band_flush_active(void)
{
    eq_band_flush_to_hw(m4_eq_active_hw(), m4_eq_active_band_table());
}

static void m4_draw_eq_edit(void)
{
    lcd_clear(COLOR_BLACK);

    if (g_eq_edit_sub == 0) {
        /* ── 밴드 선택 화면 ── */
        lcd_draw_header(M4.eq_target ? 0xFFE0u : 0xF81Fu, M4.eq_target ? "  POST EQ 4-BAND EDIT" : "  PRE EQ 4-BAND EDIT", COLOR_WHITE);
        lcd_string(4, 22, "밴드 선택:  JY:이동  SW3:진입  SW4:나가기", COLOR_DKGREY, COLOR_BLACK);
        for (int b = 0; b < 4; b++) {
            EqBandParam* bp = &m4_eq_active_band_table()[b];
            char row[48];
            uint16_t col = (b == (int)(g_eq_band_param)) ? COLOR_YELLOW : COLOR_WHITE;
            uint8_t np = EQ_FTYPE_PARAM_COUNT[bp->filter_type];
            if (np == 0) {
                snprintf(row, sizeof(row), "B%d: %-8s  BYPASS", b, EQ_FTYPE_NAMES[bp->filter_type]);
            }
            else if (np == 2) {
                snprintf(row, sizeof(row), "B%d: %-8s  fc=%.0f  %s=%.2f",
                    b, EQ_FTYPE_NAMES[bp->filter_type], bp->fc,
                    eq_p2_name(bp->filter_type), bp->p2);
            }
            else {
                snprintf(row, sizeof(row), "B%d: %-8s  fc=%.0f  g=%.1f  %s=%.2f",
                    b, EQ_FTYPE_NAMES[bp->filter_type], bp->fc, bp->gain_db,
                    eq_p2_name(bp->filter_type), bp->p2);
            }
            lcd_string(4, (uint16_t)(44 + b * 18), row, col, COLOR_BLACK);
        }
        /* 배율 표시 */
        char mstr[32];
        snprintf(mstr, sizeof(mstr), "Fine×%u  Coarse×%u",
            (unsigned)g_eq_fine_mult, (unsigned)g_eq_coarse_mult);
        lcd_string(4, 120, mstr, COLOR_DKGREY, COLOR_BLACK);
        lcd_string(4, 228, "JY:밴드  SW3:진입  SW4:나가기  V+/V-:배율", COLOR_DKGREY, COLOR_BLACK);
        return;
    }

    /* ── 밴드 파라미터 편집 화면 ── */
    int b = (int)g_eq_edit_sub - 1;
    EqBandParam* bp = &m4_eq_active_band_table()[b];

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "  %s BAND%d [%s]", m4_eq_active_name(), b, EQ_FTYPE_NAMES[bp->filter_type]);
    lcd_draw_header(0x07E0u, hdr, COLOR_WHITE);

    if (g_eq_type_sel) {
        /* ── 필터 타입 선택 ── */
        lcd_string(4, 24, "필터 종류 선택 (JX:변경  SW3:확정)", COLOR_CYAN, COLOR_BLACK);
        for (int t = 0; t < EQ_FTYPE_COUNT; t++) {
            uint16_t col = (t == (int)g_eq_type_tmp) ? COLOR_YELLOW : COLOR_GREY;
            char tbuf[24];
            snprintf(tbuf, sizeof(tbuf), "%s%s",
                (t == (int)g_eq_type_tmp) ? "-> " : "   ",
                EQ_FTYPE_NAMES[t]);
            lcd_string(4, (uint16_t)(46 + t * 18), tbuf, col, COLOR_BLACK);
        }
        lcd_string(4, 228, "JX:종류  SW3:확정  SW5:다음밴드  SW4:나가기", COLOR_DKGREY, COLOR_BLACK);
    }
    else {
        /* ── 파라미터 편집 ── */
        uint8_t np = EQ_FTYPE_PARAM_COUNT[bp->filter_type];
        const char* pnames[3];
        double pvals[3];
        pnames[0] = "fc(Hz)";
        pnames[1] = (np >= 2) ? (np == 2 ? eq_p2_name(bp->filter_type) : "gain(dB)") : "---";
        pnames[2] = (np == 3) ? eq_p2_name(bp->filter_type) : "---";

        if (np == 2) {
            /* HPF/LPF: p0=fc, p1=Q */
            pvals[0] = bp->fc;
            pvals[1] = bp->p2;
            pvals[2] = 0.0;
        }
        else {
            pvals[0] = bp->fc;
            pvals[1] = bp->gain_db;
            pvals[2] = bp->p2;
        }

        for (int p = 0; p < (int)np; p++) {
            uint16_t col = (p == (int)g_eq_band_param) ? COLOR_YELLOW : COLOR_WHITE;
            char pbuf[40];
            snprintf(pbuf, sizeof(pbuf), "%s: %.2f", pnames[p], pvals[p]);
            lcd_string(4, (uint16_t)(32 + p * 22), pbuf, col, COLOR_BLACK);
        }

        /* 노브 현재값 표시 */
        char nbuf[40];
        snprintf(nbuf, sizeof(nbuf), "Fine×%u(VOL)  Coarse×%u(PITCH)",
            (unsigned)g_eq_fine_mult, (unsigned)g_eq_coarse_mult);
        lcd_string(4, 110, nbuf, COLOR_DKGREY, COLOR_BLACK);

        /* 현재 파라미터 범위 표시 */
        double lo = 0.0, hi = 0.0;
        if (g_eq_band_param == 0) {
            eq_fc_range(bp->filter_type, &lo, &hi);
        }
        else if (np == 3 && g_eq_band_param == 1) {
            lo = EQ_GAIN_MIN_DB; hi = EQ_GAIN_MAX_DB;
        }
        else {
            eq_p2_range(bp->filter_type, &lo, &hi);
        }
        char rbuf[32];
        snprintf(rbuf, sizeof(rbuf), "범위: %.1f ~ %.1f", lo, hi);
        lcd_string(4, 128, rbuf, 0x39E7u, COLOR_BLACK);

        lcd_string(4, 228, "JY:파라미터  노브:값  SW3:타입변경  SW5:다음밴드  SW4:나가기",
            COLOR_DKGREY, COLOR_BLACK);
    }
}

static int m4_update_pre_eq_edit(uint8_t sw)
{
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    uint16_t jy = adc_buf[ADC_IDX_JOYY];

    /* ── SW4: 나가기 (EQ 결과는 HW에 이미 반영됨) ── */
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        g_eq_edit_sub = 0;
        g_eq_type_sel = 1;
        g_eq_band_param = 0;
        M4.state = M4_TOP;
        m4_draw_top_all();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── SW_VOL_UP / SW_VOL_DOWN: 노브 배율 조절 ── */
    if (sw == SW_VOL_UP && M4.prev_sw != SW_VOL_UP) {
        if (g_eq_fine_mult < EQ_MULT_MAX) g_eq_fine_mult *= 10u;
        if (g_eq_coarse_mult < EQ_MULT_MAX) g_eq_coarse_mult *= 10u;
        m4_draw_eq_edit();
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_VOL_DOWN && M4.prev_sw != SW_VOL_DOWN) {
        if (g_eq_fine_mult > EQ_MULT_MIN) g_eq_fine_mult /= 10u;
        if (g_eq_coarse_mult > EQ_MULT_MIN) g_eq_coarse_mult /= 10u;
        m4_draw_eq_edit();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── 밴드 선택 화면 (sub=0) ── */
    if (g_eq_edit_sub == 0) {
        /* JY: 밴드 커서 이동 */
        if (jy < JOY_LOW && !M4.joy_y_mv) {
            if (g_eq_band_param > 0) { g_eq_band_param--; m4_draw_eq_edit(); }
            M4.joy_y_mv = 1;
        }
        else if (jy > JOY_HIGH && !M4.joy_y_mv) {
            if (g_eq_band_param < 3) { g_eq_band_param++; m4_draw_eq_edit(); }
            M4.joy_y_mv = 1;
        }
        /* SW3: 선택한 밴드로 진입 */
        if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
            g_eq_edit_sub = (uint8_t)(g_eq_band_param + 1);
            g_eq_type_tmp = (uint8_t)m4_eq_active_band_table()[g_eq_band_param].filter_type;
            g_eq_type_sel = 1;   /* 필터 타입 선택부터 시작 */
            g_eq_band_param = 0;
            m4_draw_eq_edit();
        }
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── 밴드 편집 화면 (sub=1~4) ── */
    int b = (int)g_eq_edit_sub - 1;
    EqBandParam* bp = &m4_eq_active_band_table()[b];

    /* SW5: 다음 밴드로 순환 (band0→1→2→3→선택화면) */
    if (sw == SW_CANCEL && M4.prev_sw != SW_CANCEL) {
        if (g_eq_edit_sub < 4) {
            g_eq_edit_sub++;
            b = (int)g_eq_edit_sub - 1;
            bp = &m4_eq_active_band_table()[b];
            g_eq_type_tmp = (uint8_t)bp->filter_type;
            g_eq_type_sel = 1;
            g_eq_band_param = 0;
        }
        else {
            g_eq_edit_sub = 0;
            g_eq_band_param = 0;
            g_eq_type_sel = 1;
        }
        m4_draw_eq_edit();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── 타입 선택 모드 ── */
    if (g_eq_type_sel) {
        /* JX: 타입 변경 */
        if (jx < JOY_LOW && !M4.joy_x_mv) {
            if (g_eq_type_tmp > 0) { g_eq_type_tmp--; m4_draw_eq_edit(); }
            M4.joy_x_mv = 1;
        }
        else if (jx > JOY_HIGH && !M4.joy_x_mv) {
            if (g_eq_type_tmp < EQ_FTYPE_COUNT - 1) { g_eq_type_tmp++; m4_draw_eq_edit(); }
            M4.joy_x_mv = 1;
        }
        /* SW3: 타입 확정 → 파라미터 편집 모드 전환 */
        if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
            eq_filter_type_t new_type = (eq_filter_type_t)g_eq_type_tmp;
            if (new_type != bp->filter_type) {
                /* 타입이 바뀌면 p2 기본값으로 리셋, fc는 유지하되 범위 재클램프 */
                bp->filter_type = new_type;
                bp->p2 = eq_p2_default(new_type);
                eq_clamp_params(new_type, &bp->fc, &bp->gain_db, &bp->p2);
                eq_band_flush_active();
            }
            g_eq_type_sel = 0;
            g_eq_band_param = 0;
            m4_draw_eq_edit();
        }
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── 파라미터 편집 모드 ── */
    uint8_t np = EQ_FTYPE_PARAM_COUNT[bp->filter_type];

    /* SW3: 타입 선택 모드로 돌아가기 */
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        g_eq_type_tmp = (uint8_t)bp->filter_type;
        g_eq_type_sel = 1;
        m4_draw_eq_edit();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* JY: 파라미터 선택 (위/아래 순환) */
    if (np > 0) {
        if (jy < JOY_LOW && !M4.joy_y_mv) {
            if (g_eq_band_param > 0) g_eq_band_param--;
            else g_eq_band_param = (uint8_t)(np - 1);
            m4_draw_eq_edit(); M4.joy_y_mv = 1;
        }
        else if (jy > JOY_HIGH && !M4.joy_y_mv) {
            g_eq_band_param = (uint8_t)((g_eq_band_param + 1) % np);
            m4_draw_eq_edit(); M4.joy_y_mv = 1;
        }
    }

    /* ── 노브 → 파라미터 값 변경 ──
     *   ADC_VOL   (0~4095, 세밀) + ADC_PITCH (0~4095, 큰범위)
     *   두 노브를 합산하여 파라미터에 더할 델타를 계산.
     *   중심(2048)을 기준으로 양수/음수 방향.
     *   g_eq_fine_mult / g_eq_coarse_mult 로 배율 조정.
     *
     *   델타 = (adc - 2048) / 2048.0 × step × mult
     *   step: fc=1Hz, gain=0.1dB, Q/S=0.01
     *   이전 루프 값과 비교해 실제 변화가 있을 때만 write.
     */
    {
        static uint16_t prev_fine = 2048, prev_coarse = 2048;
        uint16_t cur_fine = adc_buf[ADC_IDX_VOL];
        uint16_t cur_coarse = adc_buf[ADC_IDX_PITCH];

        int fine_delta = (int)cur_fine - (int)prev_fine;
        int coarse_delta = (int)cur_coarse - (int)prev_coarse;

        /* 데드존: ±30 LSB 미만 무시 */
        if (fine_delta > -30 && fine_delta < 30) fine_delta = 0;
        if (coarse_delta > -30 && coarse_delta < 30) coarse_delta = 0;

        if (fine_delta != 0 || coarse_delta != 0) {
            double step_base;
            if (g_eq_band_param == 0) {
                step_base = 1.0;  /* fc: 1Hz 단위 */
            }
            else if (np == 3 && g_eq_band_param == 1) {
                step_base = 0.1;  /* gain: 0.1dB 단위 */
            }
            else {
                step_base = 0.01; /* Q/S: 0.01 단위 */
            }

            double delta =
                (double)fine_delta / 2048.0 * step_base * (double)g_eq_fine_mult +
                (double)coarse_delta / 2048.0 * step_base * (double)g_eq_coarse_mult;

            /* 파라미터에 적용 */
            if (g_eq_band_param == 0) {
                bp->fc += delta;
            }
            else if (np == 3 && g_eq_band_param == 1) {
                bp->gain_db += delta;
            }
            else {
                /* HPF/LPF: p1=Q(p2),  Bell/Shelf: p2=Q or S */
                bp->p2 += delta;
            }
            eq_clamp_params(bp->filter_type, &bp->fc, &bp->gain_db, &bp->p2);
            eq_band_flush_active();
            m4_draw_eq_edit();

            prev_fine = cur_fine;
            prev_coarse = cur_coarse;
        }
    }

    return ZYNQ_MODE_INSTSETUP;
}


/* ── WARM / SVF / VCA 세부 편집 상태 (변수 정의는 VCA forward decl 블록으로 이동) ── */


/* =============================================================================
 * DELAY 편집기 v2  (analog_delay_stereo_top Rev.9 / analog_delay.h v6.0 기준)
 *
 * ── 파라미터 9개 (RTL 실제 동작 파라미터만) ──────────────────────────────
 *   0  PRESET   프리셋 선택             JX/SW1/SW2
 *   1  TIME ms  딜레이 시간 (0.1~1365) VOL=±1ms   PITCH=±10ms
 *   2  FEEDBACK 피드백 (0~75%)         VOL=±0.5%  PITCH=±5%  ← 돌림노래 핵심
 *   3  DRY/WET  드라이/웻 (0~99%)      VOL=±0.5%  PITCH=±5%
 *   4  DAMP-FB  FB루프 댐핑 (0~99%)    VOL=±1%    PITCH=±5%  0=클린 50=테이프
 *   5  DAMP-HF  출력 HF 감쇠 (0~99%)  VOL=±1%    PITCH=±5%  보통 0 유지
 *   6  DIFF MIX APF 확산 (0~99%)      VOL=±1%    PITCH=±5%  0=선명 30=퍼짐
 *   7  SLEW     슬루레이트             SW1/SW2 단계 전환, JX x2/÷2
 *   8  SAT THR  클립 임계 (256~32767) VOL=±100   PITCH=±1000
 *
 * ── 조작 ──────────────────────────────────────────────────────────────────
 *   SW3(SELECT) : 즉시 HW 적용 + 미리듣기 (저장 아님, 화면 유지)
 *   SW4(EXIT)   : 취소 → snap 복원 → TOP 복귀
 *   SW5(SAVE)   : 저장 확정 → g_full_preset 동기화 → MENU 복귀
 *                 저장 후 Mode 3에서 MIDI 연주 시 즉시 딜레이 적용됨
 *
 * ── 딜레이 체감 팁 ────────────────────────────────────────────────────────
 *   FEEDBACK  55~70% : 반복 에코가 명확하게 들림
 *   DRY/WET   40~55% : 원음+에코 균형
 *   DAMP-FB   0~20%  : 0에 가까울수록 각 에코가 선명하게 반복
 *   TIME ms   200~400ms : 음악적으로 자연스러운 에코 간격
 * ============================================================================= */

#define M4_DLY_PCOUNT  9

 /* 슬루 프리셋 이름 (SLEW 항목 표시용) */
static const char* dly_slew_name(uint16_t s)
{
    if (s >= ADLY_SLEW_INSTANT) return "INSTANT";
    if (s >= ADLY_SLEW_FAST)    return "FAST   ";
    if (s >= ADLY_SLEW_MEDIUM)  return "MEDIUM ";
    if (s >= ADLY_SLEW_SLOW)    return "SLOW   ";
    if (s >= ADLY_SLEW_DRIFT)   return "DRIFT  ";
    return "ULTRA  ";
}

static const char* const M4_DLY_PNAME[M4_DLY_PCOUNT] = {
    "PRESET  ", "TIME ms ", "FEEDBACK", "DRY/WET ",
    "DAMP-FB ", "DAMP-HF ", "DIFF MIX", "SLEW    ", "SAT THR "
};

/* 딜레이 편집 캐시 구조체 (HW 실제값 기준) */
typedef struct {
    uint8_t  preset;       /* 0: 프리셋 인덱스 */
    float    time_ms;      /* 1: 딜레이 시간 ms */
    float    fb_pct;       /* 2: 피드백 % (0~75) */
    float    dw_pct;       /* 3: dry/wet % (0~99) */
    float    damp_a_pct;   /* 4: FB루프 댐핑 % (0=off) */
    float    damp_hf_pct;  /* 5: 출력 HF 댐핑 % (0=off) */
    float    diff_mix_pct; /* 6: APF 확산량 % (0=APF OFF) */
    uint16_t slew_raw;     /* 7: 슬루레이트 raw */
    uint16_t sat_thresh;   /* 8: 포화 임계 raw (256~32767) */
} dly_edit_t;

static dly_edit_t g_dly_edit = { 0 };
static dly_edit_t g_dly_snap = { 0 };  /* SW4 취소용 snap */

static uint8_t g_dly_snap_preset = 0;

/* 노브 snap 상태 */
static struct { uint16_t sv; uint16_t sp; uint8_t armed; } g_dly_knob = { 0 };

/* ── HW → 캐시 읽기 ──────────────────────────────────────────────────────── */
static void dly_edit_read_from_hw(void)
{
    if (!p_delay) return;
    g_dly_edit.preset = M4.edit.dly_preset;
    g_dly_edit.time_ms = analog_delay_get_time_ms(&g_adly);
    g_dly_edit.fb_pct = analog_delay_get_feedback_pct(&g_adly);
    g_dly_edit.dw_pct = analog_delay_get_dry_wet_pct(&g_adly);
    g_dly_edit.damp_a_pct = ADLY_Q15_TO_PCT(adly_rd(&g_adly, ADLY_OFF_DAMP_ALPHA));
    g_dly_edit.damp_hf_pct = ADLY_Q15_TO_PCT(adly_rd(&g_adly, ADLY_OFF_DAMP_HF));
    /* diff_mix: 0%이면 APF OFF로 간주 */
    g_dly_edit.diff_mix_pct = ADLY_Q15_TO_PCT(adly_rd(&g_adly, ADLY_OFF_DIFFUSION_MIX) & 0xFFFFu);
    g_dly_edit.slew_raw = (uint16_t)(adly_rd(&g_adly, ADLY_OFF_SLEW_RATE) & 0xFFFFu);
    g_dly_edit.sat_thresh = (uint16_t)(adly_rd(&g_adly, ADLY_OFF_SAT_THRESH) & 0x7FFFu);
}

/* ── 캐시 → HW 쓰기 ──────────────────────────────────────────────────────── */
static void dly_edit_apply_to_hw(void)
{
    if (!p_delay || !DIAG_USE_DELAY) return;

    analog_delay_set_bypass(&g_adly, 0);

    /* INSTANT slew + time 목표 기록 → usleep 60ms 대기로 RTL 수렴 보장
     * (INSTANT만 쓰고 바로 다음 파라미터로 넘어가면 RTL이 수렴 전에
     *  slew가 바뀌어 타임 수렴이 안 됨) */
    analog_delay_set_slew_rate(&g_adly, ADLY_SLEW_INSTANT);
    analog_delay_set_time_ms(&g_adly, g_dly_edit.time_ms);
    usleep(60000);

    analog_delay_set_feedback_pct(&g_adly, g_dly_edit.fb_pct);
    analog_delay_set_dry_wet_pct(&g_adly, g_dly_edit.dw_pct);

    /* damp: 0.0f(=OFF)도 유효값이므로 조건 없이 항상 적용 */
    analog_delay_set_damp_alpha_pct(&g_adly, g_dly_edit.damp_a_pct);
    analog_delay_set_damp_hf_pct(&g_adly, g_dly_edit.damp_hf_pct);

    /* APF: diff_mix>0.5%이면 APF ON (diffusion_start=0), 아니면 OFF (0xFF) */
    {
        uint8_t apf_start = (g_dly_edit.diff_mix_pct > 0.5f)
            ? 0x00u : ADLY_DIFFUSION_OFF;
        adly_wr(&g_adly, ADLY_OFF_DIFFUSION_START, (uint32_t)apf_start);
        adly_wr(&g_adly, ADLY_OFF_DIFFUSION_MIX,
            (uint32_t)ADLY_PCT_TO_Q15(g_dly_edit.diff_mix_pct));
    }

    /* sat_thresh: raw값 직접 쓰기 */
    analog_delay_set_sat_thresh(&g_adly, g_dly_edit.sat_thresh);

    /* 마지막에 사용자 설정 slew 복원 */
    analog_delay_set_slew_rate(&g_adly, g_dly_edit.slew_raw);
}

/* ── 캐시 → full_preset_t 저장 ──────────────────────────────────────────── */
static void dly_edit_save_to_preset(full_preset_t* dst)
{
    if (!dst) return;
    dst->dly_preset = g_dly_edit.preset;
    dst->dly_custom_valid = 1u;           /* 항상 custom 경로 활성화 */
    dst->dly_time_ms = g_dly_edit.time_ms;
    dst->dly_fb_pct = g_dly_edit.fb_pct;
    dst->dly_dw_pct = g_dly_edit.dw_pct;
    dst->dly_damp_a_pct = g_dly_edit.damp_a_pct;
    dst->dly_damp_hf_pct = g_dly_edit.damp_hf_pct;
    dst->dly_diff_mix_pct = g_dly_edit.diff_mix_pct;
    dst->dly_diff_g_pct = 50.0f;       /* g계수 기본 50% 고정 */
    dst->dly_sat_thresh = g_dly_edit.sat_thresh;
    dst->dly_slew_raw = g_dly_edit.slew_raw;
    /* 미구현 파라미터 초기화 */
    dst->dly_wow_depth = 0u;
    dst->dly_ms_pct = 50.0f;       /* M/S 50% 고정 */
    dst->dly_tap1_ms = 0.0f;
    dst->dly_tap1_gain = 0.0f;
    dst->dly_tap2_ms = 0.0f;
    dst->dly_tap2_gain = 0.0f;
}

/* ── 노브 snap ───────────────────────────────────────────────────────────── */
static void dly_knob_snap(void)
{
    g_dly_knob.sv = adc_buf[ADC_IDX_VOL];
    g_dly_knob.sp = adc_buf[ADC_IDX_PITCH];
    g_dly_knob.armed = 1;
}

/* ── 파라미터 현재값 → 문자열 ─────────────────────────────────────────────── */
static void dly_param_str(int p, char* buf, int sz)
{
    switch (p) {
    case 0:
        snprintf(buf, sz, "[%02u] %s", g_dly_edit.preset,
            (g_dly_edit.preset < DLY_PRESET_COUNT)
            ? DLY_PRESET_NAMES[g_dly_edit.preset] : "???");
        break;
    case 1: snprintf(buf, sz, "%6.1f ms", g_dly_edit.time_ms);  break;
    case 2: snprintf(buf, sz, "%5.1f %%", g_dly_edit.fb_pct);   break;
    case 3: snprintf(buf, sz, "%5.1f %%", g_dly_edit.dw_pct);   break;
    case 4: snprintf(buf, sz, "%5.1f %% %s", g_dly_edit.damp_a_pct,
        g_dly_edit.damp_a_pct < 1.0f ? "(CLEAN)" :
        g_dly_edit.damp_a_pct < 30.0f ? "(BRIGHT)" :
        g_dly_edit.damp_a_pct < 60.0f ? "(WARM)" : "(DARK)");
        break;
    case 5: snprintf(buf, sz, "%5.1f %% %s", g_dly_edit.damp_hf_pct,
        g_dly_edit.damp_hf_pct < 1.0f ? "(OFF)" : "(ON)");
        break;
    case 6: snprintf(buf, sz, "%5.1f %% %s", g_dly_edit.diff_mix_pct,
        g_dly_edit.diff_mix_pct < 1.0f ? "(APF OFF)" : "(APF ON)");
        break;
    case 7: snprintf(buf, sz, "0x%04X %s",
        g_dly_edit.slew_raw, dly_slew_name(g_dly_edit.slew_raw));
        break;
    case 8: snprintf(buf, sz, "%5u", g_dly_edit.sat_thresh); break;
    default: snprintf(buf, sz, "---"); break;
    }
}

/* ── 진행도 바 그리기 ─────────────────────────────────────────────────────── */
static void dly_draw_bar(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
    float val, float vmin, float vmax, uint16_t col)
{
    if (vmax <= vmin) return;
    float ratio = (val - vmin) / (vmax - vmin);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    uint16_t filled = (uint16_t)((float)w * ratio);
    if (filled > 0)
        lcd_fill_rect(x, y, filled, h, col);
    if (filled < w)
        lcd_fill_rect((uint16_t)(x + filled), y, (uint16_t)(w - filled), h, 0x1082u);
}

/* ── 화면 그리기 ─────────────────────────────────────────────────────────── */
static void m4_draw_dly_edit(void)
{
    lcd_clear(COLOR_BLACK);

    /* 헤더: preset 이름 + 적용 상태 */
    {
        char hdr[52];
        const char* pn = (g_dly_edit.preset < DLY_PRESET_COUNT)
            ? DLY_PRESET_NAMES[g_dly_edit.preset] : "???";
        snprintf(hdr, sizeof(hdr), " DELAY [%02u] %s", g_dly_edit.preset, pn);
        lcd_draw_header(0x5D1Cu, hdr, COLOR_WHITE);
    }

    int p = (int)M4.dly_param_cur;
    char val_buf[40];
    char row_buf[64];

    /* 파라미터 행 (전체 9개, 스크롤 없음) */
    for (int i = 0; i < M4_DLY_PCOUNT; i++) {
        uint16_t cy = (uint16_t)(20 + i * 22);
        int      is_sel = (i == p);
        uint16_t fg = is_sel ? COLOR_WHITE : 0x8C71u;
        uint16_t bg = is_sel ? 0x2945u : COLOR_BLACK;

        lcd_fill_rect(0, cy, 320, 22, bg);

        /* 선택 마커 */
        if (is_sel) lcd_fill_rect(0, cy, 3, 22, 0x07E0u);

        dly_param_str(i, val_buf, sizeof(val_buf));
        snprintf(row_buf, sizeof(row_buf), "%c %-8s %s",
            is_sel ? '>' : ' ', M4_DLY_PNAME[i], val_buf);
        lcd_string(5, (uint16_t)(cy + 7), row_buf, fg, bg);

        /* 선택된 항목에 진행도 바 */
        if (is_sel) {
            float lo = 0.0f, hi = 100.0f;
            uint16_t bar_col = 0x07E0u;
            float cur_val = 0.0f;
            switch (i) {
            case 1: lo = 0.0f;   hi = 1365.0f; cur_val = g_dly_edit.time_ms;  bar_col = 0x07FFu; break;
            case 2: lo = 0.0f;   hi = 75.0f;   cur_val = g_dly_edit.fb_pct;   bar_col = 0xFD20u; break;
            case 3: lo = 0.0f;   hi = 100.0f;  cur_val = g_dly_edit.dw_pct;   bar_col = 0x07E0u; break;
            case 4: lo = 0.0f;   hi = 100.0f;  cur_val = g_dly_edit.damp_a_pct; bar_col = 0xF800u; break;
            case 5: lo = 0.0f;   hi = 100.0f;  cur_val = g_dly_edit.damp_hf_pct; bar_col = 0xF810u; break;
            case 6: lo = 0.0f;   hi = 100.0f;  cur_val = g_dly_edit.diff_mix_pct; bar_col = 0x867Fu; break;
            default: lo = hi = cur_val = 0.0f; break;
            }
            if (lo < hi)
                dly_draw_bar(220, (uint16_t)(cy + 7), 94, 8, cur_val, lo, hi, bar_col);
        }
    }

    /* 하단 힌트 */
    lcd_string(2, 218, "JY:항목 VOL:fine PITCH:coarse JX/SW1/2:프리셋/슬루",
        0x6B4Du, COLOR_BLACK);
    lcd_string(2, 228, "SW3:즉시적용  SW4:취소->TOP  SW5:저장->MENU",
        0x07E0u, COLOR_BLACK);
}

/* ── 파라미터 delta 적용 ─────────────────────────────────────────────────── */
static void dly_param_delta(int p, int dvol, int dpitch)
{
    float fv = (float)dvol, fp = (float)dpitch;
    switch (p) {
    case 0: break;  /* PRESET: JX/SW 처리 */
    case 1:
        g_dly_edit.time_ms += fv * 1.0f + fp * 10.0f;
        if (g_dly_edit.time_ms < ADLY_MIN_DELAY_MS) g_dly_edit.time_ms = ADLY_MIN_DELAY_MS;
        if (g_dly_edit.time_ms > ADLY_MAX_DELAY_MS) g_dly_edit.time_ms = ADLY_MAX_DELAY_MS;
        break;
    case 2:
        g_dly_edit.fb_pct += fv * 0.5f + fp * 5.0f;
        if (g_dly_edit.fb_pct < 0.0f)          g_dly_edit.fb_pct = 0.0f;
        if (g_dly_edit.fb_pct > ADLY_FB_MAX_PCT) g_dly_edit.fb_pct = ADLY_FB_MAX_PCT;
        break;
    case 3:
        g_dly_edit.dw_pct += fv * 0.5f + fp * 5.0f;
        if (g_dly_edit.dw_pct < 0.0f)   g_dly_edit.dw_pct = 0.0f;
        if (g_dly_edit.dw_pct > 99.0f)  g_dly_edit.dw_pct = 99.0f;
        break;
    case 4:
        g_dly_edit.damp_a_pct += fv * 1.0f + fp * 5.0f;
        if (g_dly_edit.damp_a_pct < 0.0f)   g_dly_edit.damp_a_pct = 0.0f;
        if (g_dly_edit.damp_a_pct > 99.0f)  g_dly_edit.damp_a_pct = 99.0f;
        break;
    case 5:
        g_dly_edit.damp_hf_pct += fv * 1.0f + fp * 5.0f;
        if (g_dly_edit.damp_hf_pct < 0.0f)  g_dly_edit.damp_hf_pct = 0.0f;
        if (g_dly_edit.damp_hf_pct > 99.0f) g_dly_edit.damp_hf_pct = 99.0f;
        break;
    case 6:
        g_dly_edit.diff_mix_pct += fv * 1.0f + fp * 5.0f;
        if (g_dly_edit.diff_mix_pct < 0.0f)  g_dly_edit.diff_mix_pct = 0.0f;
        if (g_dly_edit.diff_mix_pct > 99.0f) g_dly_edit.diff_mix_pct = 99.0f;
        break;
    case 7: break;  /* SLEW: SW/JX 처리 */
    case 8: {
        int32_t v = (int32_t)g_dly_edit.sat_thresh + (int32_t)(fv * 100.0f) + (int32_t)(fp * 1000.0f);
        if (v < (int32_t)ADLY_SAT_THRESH_MIN) v = (int32_t)ADLY_SAT_THRESH_MIN;
        if (v > (int32_t)ADLY_SAT_THRESH_MAX) v = (int32_t)ADLY_SAT_THRESH_MAX;
        g_dly_edit.sat_thresh = (uint16_t)v;
        break;
    }
    }
}

/* ── 슬루 preset 단계 (SW1=빠르게, SW2=느리게) ───────────────────────────── */
static void dly_slew_step_up(void)   /* 더 빠르게 */
{
    if (g_dly_edit.slew_raw < ADLY_SLEW_DRIFT)   g_dly_edit.slew_raw = ADLY_SLEW_DRIFT;
    else if (g_dly_edit.slew_raw < ADLY_SLEW_SLOW)    g_dly_edit.slew_raw = ADLY_SLEW_SLOW;
    else if (g_dly_edit.slew_raw < ADLY_SLEW_MEDIUM)  g_dly_edit.slew_raw = ADLY_SLEW_MEDIUM;
    else if (g_dly_edit.slew_raw < ADLY_SLEW_FAST)    g_dly_edit.slew_raw = ADLY_SLEW_FAST;
    else                                                g_dly_edit.slew_raw = ADLY_SLEW_INSTANT;
}
static void dly_slew_step_down(void)  /* 더 느리게 */
{
    if (g_dly_edit.slew_raw > ADLY_SLEW_FAST)    g_dly_edit.slew_raw = ADLY_SLEW_FAST;
    else if (g_dly_edit.slew_raw > ADLY_SLEW_MEDIUM)  g_dly_edit.slew_raw = ADLY_SLEW_MEDIUM;
    else if (g_dly_edit.slew_raw > ADLY_SLEW_SLOW)    g_dly_edit.slew_raw = ADLY_SLEW_SLOW;
    else if (g_dly_edit.slew_raw > ADLY_SLEW_DRIFT)   g_dly_edit.slew_raw = ADLY_SLEW_DRIFT;
    else                                                g_dly_edit.slew_raw = ADLY_SLEW_ULTRA_SLOW;
}

/* ── 메인 업데이트 함수 ──────────────────────────────────────────────────── */
static int m4_update_dly_edit(uint8_t sw)
{
    if (!p_delay) {
        /* DELAY HW 없음 → 저장 완료 후 MENU 복귀 */
        g_m4_preset_valid = 1;
        full_preset_apply_hw();
        m3_full_voice_reset();
        pt2258_set_mute(1);
        lcd_fill_rect(0, 208, 320, 28, COLOR_GREEN);
        lcd_string(2, 214, "SAVED: DELAY 없음 - 전체 저장 완료", COLOR_WHITE, COLOR_GREEN);
        usleep(600000);
        return ZYNQ_MODE_MENU;
    }

    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    uint16_t vol = adc_buf[ADC_IDX_VOL];
    uint16_t pitch = adc_buf[ADC_IDX_PITCH];
    int p = (int)M4.dly_param_cur;
    int need_draw = 0;

    /* ── SW4(EXIT): 취소 → snap 복원 → POST EQ 모드선택으로 뒤로 ── */
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        g_dly_edit = g_dly_snap;
        M4.edit.dly_preset = g_dly_snap_preset;
        M4.edit.dly_custom_valid = 0u;
        apply_dly_preset(g_dly_snap_preset);
        dly_edit_read_from_hw();
        M4.eq_target = 1;
        M4.state = M4_POST_EQ_MODE_SEL;
        mode4_eq_mode_sel_enter(true, sw);
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── SW5(SAVE): 저장 확정 → g_full_preset 동기화 → MENU 복귀 ── */
    if (sw == SW_SAVE && M4.prev_sw != SW_SAVE) {
        /* 1. HW 최종 적용 */
        dly_edit_apply_to_hw();

        /* 2. M4.edit에 저장 */
        dly_edit_save_to_preset(&M4.edit);

        /* 3. g_full_preset 완전 동기화 (YM+EQ+VCA+DELAY 전체) */
        memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
        memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));

        /* 4. mode3/VGM 이펙터 체인 활성화 */
        g_m4_preset_valid = 1;

        printf("[DLY-SAVE] preset=%u time=%.1fms fb=%.1f%% wet=%.1f%%"
            " damp=%.1f%% diff=%.1f%% slew=0x%04X sat=%u\n",
            g_dly_edit.preset, g_dly_edit.time_ms,
            g_dly_edit.fb_pct, g_dly_edit.dw_pct,
            g_dly_edit.damp_a_pct, g_dly_edit.diff_mix_pct,
            g_dly_edit.slew_raw, g_dly_edit.sat_thresh);
        printf("[M4-SAVE] FULL CHAIN: YM->EQ->VCA->POST_EQ->DELAY "
            "FM0=#%d VCA=%d DLY=%d\n",
            g_full_preset.fm[0].inst_idx,
            g_full_preset.vca_preset,
            g_full_preset.dly_preset);

        /* 5. bypass ON (M4 편집 중 잔류음 차단) */
        analog_delay_set_bypass(&g_adly, 1);
        pt2258_set_mute(1);

        /* 6. LCD 피드백 */
        lcd_fill_rect(0, 196, 320, 44, 0x0320u);
        lcd_string(4, 198, "SAVED! Mode3 MIDI 연주시 딜레이 즉시 적용", COLOR_WHITE, 0x0320u);
        {
            char sbuf[52];
            snprintf(sbuf, sizeof(sbuf),
                "FB:%.0f%% WET:%.0f%% DAMP:%.0f%% TIME:%.0fms",
                g_dly_edit.fb_pct, g_dly_edit.dw_pct,
                g_dly_edit.damp_a_pct, g_dly_edit.time_ms);
            lcd_string(4, 212, sbuf, COLOR_YELLOW, 0x0320u);
        }
        lcd_string(4, 226, "MENU->Mode3 선택->MIDI 연주 확인", COLOR_CYAN, 0x0320u);
        usleep(1200000);

        m3_full_voice_reset();
        return ZYNQ_MODE_MENU;
    }

    /* ── SW3(SELECT): 즉시 HW 적용 + 미리듣기 ── */
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        dly_edit_apply_to_hw();
        dly_edit_save_to_preset(&M4.edit);
        memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
        pt2258_set_mute(0);
        m4_preview_fm_full((uint8_t)M4.fm_ch, M4.fm_inst_cur);
        need_draw = 1;
    }

    /* ── JOY_Y: 항목 이동 ── */
    if (jy < JOY_LOW && !M4.joy_y_mv) {
        if (p > 0) { M4.dly_param_cur--; p--; dly_knob_snap(); }
        M4.joy_y_mv = 1; need_draw = 1;
    }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) {
        if (p < M4_DLY_PCOUNT - 1) { M4.dly_param_cur++; p++; dly_knob_snap(); }
        M4.joy_y_mv = 1; need_draw = 1;
    }
    if (jy >= JOY_DEAD_LO && jy <= JOY_DEAD_HI) M4.joy_y_mv = 0;

    /* ── JOY_X / SW1/SW2: PRESET(p=0) 또는 SLEW(p=7) 특수 처리 ── */
    if (p == 0) {
        /* PRESET: JX 좌우, SW1/SW2 */
        if (jx < JOY_LOW && !M4.joy_x_mv) {
            if (g_dly_edit.preset > 0) {
                g_dly_edit.preset--;
                M4.edit.dly_preset = g_dly_edit.preset;
                apply_dly_preset(g_dly_edit.preset);
                dly_edit_read_from_hw();
                dly_knob_snap();
            }
            M4.joy_x_mv = 1; need_draw = 1;
        }
        else if (jx > JOY_HIGH && !M4.joy_x_mv) {
            if (g_dly_edit.preset < DLY_PRESET_COUNT - 1) {
                g_dly_edit.preset++;
                M4.edit.dly_preset = g_dly_edit.preset;
                apply_dly_preset(g_dly_edit.preset);
                dly_edit_read_from_hw();
                dly_knob_snap();
            }
            M4.joy_x_mv = 1; need_draw = 1;
        }
        if (sw == SW1 && M4.prev_sw != SW1 && g_dly_edit.preset > 0) {
            g_dly_edit.preset--;
            M4.edit.dly_preset = g_dly_edit.preset;
            apply_dly_preset(g_dly_edit.preset);
            dly_edit_read_from_hw(); dly_knob_snap(); need_draw = 1;
        }
        if (sw == SW2 && M4.prev_sw != SW2 && g_dly_edit.preset < DLY_PRESET_COUNT - 1) {
            g_dly_edit.preset++;
            M4.edit.dly_preset = g_dly_edit.preset;
            apply_dly_preset(g_dly_edit.preset);
            dly_edit_read_from_hw(); dly_knob_snap(); need_draw = 1;
        }
    }
    else if (p == 7) {
        /* SLEW: JX x2/÷2, SW1=빠르게, SW2=느리게 */
        if (jx < JOY_LOW && !M4.joy_x_mv) {
            if (g_dly_edit.slew_raw > 1) g_dly_edit.slew_raw >>= 1;
            M4.joy_x_mv = 1; need_draw = 1;
        }
        else if (jx > JOY_HIGH && !M4.joy_x_mv) {
            uint32_t nv = (uint32_t)g_dly_edit.slew_raw << 1;
            g_dly_edit.slew_raw = (nv > 0xFFFFu) ? 0xFFFFu : (uint16_t)nv;
            M4.joy_x_mv = 1; need_draw = 1;
        }
        if (sw == SW1 && M4.prev_sw != SW1) { dly_slew_step_up();   need_draw = 1; }
        if (sw == SW2 && M4.prev_sw != SW2) { dly_slew_step_down(); need_draw = 1; }
    }
    if (jx >= JOY_DEAD_LO && jx <= JOY_DEAD_HI) M4.joy_x_mv = 0;

    /* ── VOL/PITCH 노브: delta 방식 (p=1~6, 8) ── */
    if (p >= 1 && p != 7) {
        if (!g_dly_knob.armed) {
            int dv = (int)vol - (int)g_dly_knob.sv;
            int dp = (int)pitch - (int)g_dly_knob.sp;
            /* ±150 이내면 snap 완료 */
            if (dv > -150 && dv < 150 && dp > -150 && dp < 150)
                g_dly_knob.armed = 1;
        }
        else {
            int dvol = (int)vol - (int)g_dly_knob.sv;
            int dpitch = (int)pitch - (int)g_dly_knob.sp;
            int dv_abs = dvol < 0 ? -dvol : dvol;
            int dp_abs = dpitch < 0 ? -dpitch : dpitch;
            if (dv_abs >= 8 || dp_abs >= 8) {
                int step_v = dvol / 8;
                int step_p = dpitch / 8;
                if (step_v != 0 || step_p != 0) {
                    dly_param_delta(p, step_v, step_p);
                    /* 노브 움직임마다 즉시 HW 반영 (청각 피드백) */
                    dly_edit_apply_to_hw();
                    g_dly_knob.sv = vol;
                    g_dly_knob.sp = pitch;
                    need_draw = 1;
                }
            }
        }
    }

    if (need_draw) m4_draw_dly_edit();
    return ZYNQ_MODE_INSTSETUP;
}


/* ??????????????????????????????????????????????????????????????????????????????
 * VCA 편집 입력 매핑:
 *   JOY_Y              → 파라미터 선택 (상=이전, 하=다음)
 *   ADC_VOL  (노브 L)  → 현재 파라미터 값 fine 조절 (절대값 snap 방식)
 *   ADC_PITCH(노브 R)  → 현재 파라미터 값 coarse 조절
 *   JOY_X              → PRESET 선택 (param=0일 때), 또는 토글 파라미터
 *   SW1(VOL_UP)        → param=PRESET일 때 프리셋↑, 토글항목 전환
 *   SW2(VOL_DOWN)      → param=PRESET일 때 프리셋↓, 토글항목 전환
 *   SW3(SELECT)        → Apply (현재 레지스터 값 저장)
 *   SW4(EXIT)          → WARM으로 이동
 *   SW5(SAVE)          → Apply 후 WARM으로 이동
 *
 * 파라미터 목록 (vca_compressor.h 스케일 기준):
 *   0  PRESET   ? 프리셋 선택 (JX/SW1/SW2)
 *   1  THRESH   ? -60 ~ 0 dBFS   (노브 절대값)
 *   2  RATIO    ? 1.0 ~ ∞ (limiter)  (노브, 1:1~20:1 + INF)
 *   3  MAKEUP   ? 0 ~ 24 dB          (노브 절대값)
 *   4  ATTACK   ? 0.1 ~ 200 ms       (노브 절대값, log scale)

 *   5  RELEASE  ? 5 ~ 3000 ms        (노브 절대값, log scale)
 *   6  KNEE     ? 0 ~ 12 dB          (노브 절대값)
 *   7  ENV MODE ? PEAK / RMS         (JX 또는 SW1/2 토글)
 *   8  DITHER   ? ON / OFF           (JX 또는 SW1/2 토글)
 * ?????????????????????????????????????????????????????????????????????????????? */

 /* VCA 편집 상태 */
typedef struct {
    uint16_t snap_vol;
    uint16_t snap_pitch;
    float    snap_val;      /* snap 시점 파라미터 값 (실수 단위) */
    uint8_t  joy_x_moved;
} vca_edit_t;
static vca_edit_t g_vca_edit = { 0 };

/* 파라미터 정보 */
static const char* const M4_VCA_PNAME[M4_VCA_PCOUNT] = {
    "PRESET", "THRESH", "RATIO", "MAKEUP", "ATTACK", "RELEASE", "KNEE", "ENV", "DITHER"
};

/* VCA 노브→파라미터 값 읽기/쓰기 (실수 단위) */
static float vca_param_get(int p)
{
    if (!p_vca) return 0.0f;
    switch (p) {
    case 1: return VCA_ThresholdReg_To_dBFS(VCA_ReadThresholdRaw((uintptr_t)p_vca));
    case 2: return VCA_RatioReg_To_Float(VCA_ReadCSRaw((uintptr_t)p_vca));
    case 3: return VCA_MakeupReg_To_dB(VCA_ReadMakeupRaw((uintptr_t)p_vca));
    case 4: return vca_shift2ms_(VCA_ReadAttackRaw((uintptr_t)p_vca), VCA_SAMPLE_RATE);
    case 5: return vca_shift2ms_(VCA_ReadReleaseRaw((uintptr_t)p_vca), VCA_SAMPLE_RATE);
    case 6: return VCA_KneeReg_To_dB(VCA_ReadKneeRaw((uintptr_t)p_vca));
    default: return 0.0f;
    }
}

static void vca_param_set(int p, float v)
{
    if (!p_vca || !DIAG_USE_VCA) return;
    switch (p) {
    case 1: VCA_SetThresholdDb((uintptr_t)p_vca, v); break;
    case 2: VCA_SetRatio((uintptr_t)p_vca, v); break;
    case 3: VCA_SetMakeupDb((uintptr_t)p_vca, v); break;
    case 4: VCA_SetAttackMs((uintptr_t)p_vca, v); break;
    case 5: VCA_SetReleaseMs((uintptr_t)p_vca, v); break;
    case 6: VCA_SetKneeDb((uintptr_t)p_vca, v); break;
    }
}

/* 파라미터 범위 [lo, hi] 반환 */
static void vca_param_range(int p, float* lo, float* hi)
{
    switch (p) {
    case 1: *lo = -60.0f; *hi = 0.0f; break;
    case 2: *lo = 1.0f;  *hi = 20.0f; break; /* INF는 별도 처리 */
    case 3: *lo = 0.0f;  *hi = 24.0f; break;
    case 4: *lo = 0.1f;  *hi = 200.0f; break;
    case 5: *lo = 5.0f;  *hi = 3000.0f; break;
    case 6: *lo = 0.0f;  *hi = 12.0f; break;
    default:*lo = 0.0f;  *hi = 1.0f; break;
    }
}

/* 노브 스케일: 전체 ADC 범위(0-4095) → 파라미터 변화량 */
static float vca_fine_scale(int p)
{
    float lo, hi; vca_param_range(p, &lo, &hi);
    return (hi - lo) / 4095.0f * 0.2f;   /* 전체 노브 = 범위의 20% */
}
static float vca_coarse_scale(int p)
{
    float lo, hi; vca_param_range(p, &lo, &hi);
    return (hi - lo) / 4095.0f;           /* 전체 노브 = 전체 범위 */
}

static void vca_snap(int p)
{
    g_vca_edit.snap_vol = adc_buf[ADC_IDX_VOL];
    g_vca_edit.snap_pitch = adc_buf[ADC_IDX_PITCH];
    g_vca_edit.snap_val = (p >= 1 && p <= 6) ? vca_param_get(p) : 0.0f;
}

static void m4_draw_vca_detail_values(void)
{
    char buf[64];
    /* PRESET */
    {
        uint16_t col = (M4.vca_param_cur == 0) ? COLOR_YELLOW : COLOR_WHITE;
        snprintf(buf, sizeof(buf), "%-8s : %s", M4_VCA_PNAME[0],
            VCA_PRESET_NAMES[M4.edit.vca_preset]);
        lcd_string(4, 42, buf, col, COLOR_BLACK);
    }
    /* 연속 파라미터 1-6: 실수 단위 표시 */
    struct { int p; const char* fmt; float v; const char* unit; } rows[] = {
        {1, "%+6.1f", p_vca ? vca_param_get(1) : 0.0f, "dBFS"},
        {2,  "%6.2f", p_vca ? vca_param_get(2) : 1.0f, ":1  "},
        {3,  "%5.1f", p_vca ? vca_param_get(3) : 0.0f, "dB  "},
        {4,  "%6.1f", p_vca ? vca_param_get(4) : 0.0f, "ms  "},
        {5,  "%7.1f", p_vca ? vca_param_get(5) : 0.0f, "ms  "},
        {6,  "%5.2f", p_vca ? vca_param_get(6) : 0.0f, "dB  "},
    };
    for (int i = 0; i < 6; i++) {
        int pi = rows[i].p;
        uint16_t col = (M4.vca_param_cur == (uint8_t)pi) ? COLOR_YELLOW : COLOR_WHITE;
        char vbuf[16];
        /* Ratio: INF 처리 */
        if (pi == 2 && p_vca && isinff(rows[i].v))
            snprintf(vbuf, sizeof(vbuf), "  INF");
        else
            snprintf(vbuf, sizeof(vbuf), rows[i].fmt, rows[i].v);
        snprintf(buf, sizeof(buf), "%-8s : %s %s", M4_VCA_PNAME[pi], vbuf, rows[i].unit);
        uint16_t y = (uint16_t)(42 + pi * 17);
        if (M4.vca_param_cur == (uint8_t)pi)
            lcd_string(0, y, ">", COLOR_YELLOW, COLOR_BLACK);
        lcd_string(8, y, buf, col, COLOR_BLACK);
    }
    /* ENV MODE */
    {
        uint16_t col = (M4.vca_param_cur == 7) ? COLOR_YELLOW : COLOR_WHITE;
        uint8_t env = p_vca ? (uint8_t)(reg_rd(p_vca, VCA_ENV_MODE_OFF) & 1) : 0;
        snprintf(buf, sizeof(buf), "%-8s : %s", M4_VCA_PNAME[7], env ? "RMS" : "PEAK");
        lcd_string(8, (uint16_t)(42 + 7 * 17), buf, col, COLOR_BLACK);
        if (M4.vca_param_cur == 7) lcd_string(0, (uint16_t)(42 + 7 * 17), ">", COLOR_YELLOW, COLOR_BLACK);
    }
    /* DITHER */
    {
        uint16_t col = (M4.vca_param_cur == 8) ? COLOR_YELLOW : COLOR_WHITE;
        uint8_t dith = p_vca ? (uint8_t)(reg_rd(p_vca, VCA_DITHER_OFF) & 1) : 0;
        snprintf(buf, sizeof(buf), "%-8s : %s", M4_VCA_PNAME[8], dith ? "ON" : "OFF");
        lcd_string(8, (uint16_t)(42 + 8 * 17), buf, col, COLOR_BLACK);
        if (M4.vca_param_cur == 8) lcd_string(0, (uint16_t)(42 + 8 * 17), ">", COLOR_YELLOW, COLOR_BLACK);
    }
    /* 노브 바 */
    uint16_t fw = (uint16_t)((uint32_t)adc_buf[ADC_IDX_VOL] * 110u / ADC_MAX);
    uint16_t cw = (uint16_t)((uint32_t)adc_buf[ADC_IDX_PITCH] * 110u / ADC_MAX);
    lcd_fill_rect(200, 44, 110, 7, 0x2104u); lcd_fill_rect(200, 44, fw, 7, 0x07E0u);
    lcd_fill_rect(200, 56, 110, 7, 0x2104u); lcd_fill_rect(200, 56, cw, 7, 0xFD20u);
    lcd_string(200, 64, "fine/coarse", 0x5AEBu, COLOR_BLACK);
}

static void m4_draw_vca_edit(void)
{
    lcd_clear(COLOR_BLACK);

    /* 헤더: 현재 프리셋 이름 */
    {
        char hdr[52];
        snprintf(hdr, sizeof(hdr), "  VCA [%u] %s",
            M4.edit.vca_preset, VCA_PRESET_NAMES[M4.edit.vca_preset]);
        lcd_draw_header(0x4A10u, hdr, COLOR_WHITE);
    }

    /* 프리셋 특성 힌트 한 줄 */
    static const char* const VCA_HINTS[VCA_PRESET_COUNT] = {
        "1:1 투명 통과, 압축 없음",
        "부드러운 버스 글루 / RMS 2:1",
        "4:1 압축 + makeup +3dB",
        "어택 강조, 킥/퍼커션 펀치감",
        "강한 압축 8:1, 빈티지 스매시",
        "긴 릴리즈로 FM 음 꼬리 연장",
        "브릭월 리미터, 피크 완전 제한",
        "두껍고 따뜻한 버스 질감",
        "빠른 어택으로 선명한 트랜지언트",
        "강한 펌핑, 사이드체인 느낌",
    };
    lcd_string(4, 20, VCA_HINTS[M4.edit.vca_preset], 0xFD20u, COLOR_BLACK);

    m4_draw_vca_detail_values();
    lcd_string(4, 210, "JY:항목  VOL:fine  PITCH:coarse  JX/SW1/2:프리셋·토글",
        COLOR_DKGREY, COLOR_BLACK);
    lcd_string(4, 224, "SW3:Apply+미리듣기  SW4:→POSTEQ  SW5:저장→POSTEQ",
        COLOR_CYAN, COLOR_BLACK);
}

static int m4_update_vca_edit(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    int p = (int)M4.vca_param_cur;

    /* ── SW4: → PRE EQ 모드선택으로 뒤로 ── */
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        M4.eq_target = 0;
        M4.state = M4_PRE_EQ_MODE_SEL;
        mode4_eq_mode_sel_enter(false, sw);
        return ZYNQ_MODE_INSTSETUP;
    }
    /* ── SW5: Apply + → POST EQ 모드선택 (LIGHT-CHAIN) ── */
    if (sw == SW_SAVE && M4.prev_sw != SW_SAVE) {
        if (DIAG_USE_VCA) {
            apply_vca_preset(M4.edit.vca_preset);
            /* VCA 포함 전 단계 모두 snap에 반영: SW_CANCEL 복원 시 일관성 보장 */
            memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
            memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
        }
        diag_apply_chain_policy("vca_sw5");
        M4.eq_target = 1;
        M4.state = M4_POST_EQ_MODE_SEL;
        mode4_eq_mode_sel_enter(true, sw);
        return ZYNQ_MODE_INSTSETUP;
    }
    /* ── SW3: Apply + preview ── */
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        if (DIAG_USE_VCA) {
            apply_vca_preset(M4.edit.vca_preset);
            g_full_preset.vca_preset = M4.edit.vca_preset;
            M4.snap.vca_preset = M4.edit.vca_preset; /* [FIX] snap 동기화 */
        }
        diag_apply_chain_policy("vca_sw3");
        lcd_fill_rect(0, 208, 320, 16, COLOR_GREEN);
        lcd_string(80, 210, DIAG_USE_VCA ? "VCA APPLIED!" : "VCA DISABLED", COLOR_WHITE, COLOR_GREEN);
        if (DIAG_USE_VCA) m4_preview_fm_full((uint8_t)M4.fm_ch, M4.fm_inst_cur); /* [FIX] 체인 preview */
        usleep(200000);
        m4_draw_vca_edit();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── JOY_Y: 파라미터 이동 ─────────────────────────────────────────────
     * [BUG-FIX] 기존 코드: jy < JOY_LOW 분기에서 return을 먼저 했기 때문에
     * dead-zone 해제(M4.joy_y_mv=0) 코드에 절대 도달하지 못했다.
     * 조이스틱을 중립으로 돌려도 joy_y_mv=1이 유지되어 한 방향(아래)만 동작함.
     * 수정: 분기에서 return 제거, 함수 끝에 단일 return 사용. ─────────── */
    {
        int moved = 0;
        if (jy < JOY_LOW && !M4.joy_y_mv) {
            if (p > 0) { M4.vca_param_cur--; p--; vca_snap(p); }
            M4.joy_y_mv = 1; moved = 1;
        }
        else if (jy > JOY_HIGH && !M4.joy_y_mv) {
            if (p < M4_VCA_PCOUNT - 1) { M4.vca_param_cur++; p++; vca_snap(p); }
            M4.joy_y_mv = 1; moved = 1;
        }
        /* dead-zone: 항상 실행되어야 다음 이동이 가능 */
        if (jy >= JOY_DEAD_LO && jy <= JOY_DEAD_HI) M4.joy_y_mv = 0;
        if (moved) { m4_draw_vca_edit(); }
    }

    /* ── 연속 파라미터 1-6: 노브 절대값 매핑 ── */
    if (p >= 1 && p <= 6) {
        uint16_t vol = adc_buf[ADC_IDX_VOL];
        uint16_t pitch = adc_buf[ADC_IDX_PITCH];
        int dvol = (int)vol - (int)g_vca_edit.snap_vol;
        int dpitch = (int)pitch - (int)g_vca_edit.snap_pitch;
        float new_val = g_vca_edit.snap_val
            + (float)dvol * vca_fine_scale(p)
            + (float)dpitch * vca_coarse_scale(p);
        float lo, hi; vca_param_range(p, &lo, &hi);
        if (new_val < lo) new_val = lo;
        if (new_val > hi) new_val = hi;
        vca_param_set(p, new_val);
        /* [BUG-FIX] 무한 LCD 갱신 방지:
         * dvol/dpitch는 snap 기준 누적 delta이므로 노이즈(±1~2 ADC LSB)에 의해
         * 항상 0이 아닌 값이 되어 m4_draw_vca_detail_values()가 매 프레임 호출됨.
         * → last_drawn_vol/pitch로 "이전 그리기 기준점"을 추적하고,
         *   노브가 ±3 이상 실제로 움직였을 때만 LCD 갱신. */
        {
            static uint16_t last_drawn_vol = 0xFFFFu;
            static uint16_t last_drawn_pitch = 0xFFFFu;
            int diff_v = (int)vol - (int)last_drawn_vol;
            int diff_p = (int)pitch - (int)last_drawn_pitch;
            if (diff_v < -3 || diff_v > 3 || diff_p < -3 || diff_p > 3) {
                m4_draw_vca_detail_values();
                last_drawn_vol = vol;
                last_drawn_pitch = pitch;
            }
        }
    }

    /* ── PRESET(0): JX / SW1/SW2 ── */
    if (p == 0) {
        int dir = 0;
        if (jx < JOY_LOW && !g_vca_edit.joy_x_moved) { dir = -1; g_vca_edit.joy_x_moved = 1; }
        else if (jx > JOY_HIGH && !g_vca_edit.joy_x_moved) { dir = 1; g_vca_edit.joy_x_moved = 1; }
        if (jx >= JOY_DEAD_LO && jx <= JOY_DEAD_HI) g_vca_edit.joy_x_moved = 0;
        /* SW1(VOL_UP)=다음 프리셋, SW2(VOL_DOWN)=이전 프리셋 */
        if (sw == SW_VOL_UP && M4.prev_sw != SW_VOL_UP)   dir = 1;
        if (sw == SW_VOL_DOWN && M4.prev_sw != SW_VOL_DOWN) dir = -1;
        if (dir) {
            if (dir < 0 && M4.edit.vca_preset > 0)
                M4.edit.vca_preset--;
            if (dir > 0 && M4.edit.vca_preset < VCA_PRESET_COUNT - 1)
                M4.edit.vca_preset++;
            if (DIAG_USE_VCA) apply_vca_preset(M4.edit.vca_preset);
            diag_apply_chain_policy("vca_preset_change");
            vca_snap(0);
            m4_draw_vca_edit();
            if (DIAG_USE_VCA)
                m4_preview_fm_full((uint8_t)M4.fm_ch, M4.fm_inst_cur);
        }
        /* [BUG-FIX] return 제거: dead-zone 클리어 코드가 실행되지 않아
         * 조이스틱이 한 방향으로 고착되는 문제 방지 */
    }

    /* ── ENV MODE(7) / DITHER(8): JX / SW1/SW2 토글 ── */
    if (p == 7 || p == 8) {
        int tog = 0;
        if (jx < JOY_LOW && !g_vca_edit.joy_x_moved) { tog = 1; g_vca_edit.joy_x_moved = 1; }
        else if (jx > JOY_HIGH && !g_vca_edit.joy_x_moved) { tog = 1; g_vca_edit.joy_x_moved = 1; }
        if (jx >= JOY_DEAD_LO && jx <= JOY_DEAD_HI) g_vca_edit.joy_x_moved = 0;
        if (sw == SW_VOL_UP && M4.prev_sw != SW_VOL_UP)   tog = 1;
        if (sw == SW_VOL_DOWN && M4.prev_sw != SW_VOL_DOWN) tog = 1;
        if (tog && p_vca) {
            uint32_t off = (p == 7) ? VCA_ENV_MODE_OFF : VCA_DITHER_OFF;
            reg_wr(p_vca, off, (reg_rd(p_vca, off) ^ 1u) & 1u);
            m4_draw_vca_detail_values();
        }
        /* [BUG-FIX] return 제거: L7200 dead-zone 리셋(joy_y_mv=0)이
         * 실행되어야 다음 커서 이동이 가능. return이 있으면 p==7/8에서
         * 커서 이동 후 joy_y_mv가 영구적으로 1로 고착됨. */
    }

    /* RATIO(2): INF(limiter) 옵션 ? JX 오른쪽 끝 */
    if (p == 2) {
        if (jx > JOY_HIGH && !g_vca_edit.joy_x_moved && p_vca) {
            float cur = vca_param_get(2);
            if (cur >= 20.0f) {
                /* 이미 최대치 근처: limiter로 */
                VCA_SetLimiterRatio((uintptr_t)p_vca);
                g_vca_edit.joy_x_moved = 1;
                m4_draw_vca_detail_values();
            }
        }
        if (jx >= JOY_DEAD_LO && jx <= JOY_DEAD_HI) g_vca_edit.joy_x_moved = 0;
    }

    return ZYNQ_MODE_INSTSETUP;
}

static int m4_update_top(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];

    if (M4.apply_flash && ++M4.flash_cnt >= 60u) {
        M4.apply_flash = 0; M4.flash_cnt = 0;
        if (M4.state == M4_TOP) m4_draw_top_all();
    }
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        /* SW4(EXIT) at TOP: M4.edit → g_full_preset 동기화 후 MENU 복귀.
         * 동기화 없이 나가면 mode3에서 편집 전 값이 적용된다. */
        memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
        memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
        g_m4_preset_valid = 1;
        full_preset_apply_hw();
        analog_delay_set_bypass(&g_adly, 1);
        pt2258_set_mute(1);
        m3_full_voice_reset();
        diag_apply_chain_policy("m4_top_sw4_exit");
        return ZYNQ_MODE_MENU;
    }
    /* SW5(SAVE) at TOP: 전체 프리셋 저장 확정 후 MENU 복귀 */
    if (sw == SW_SAVE && M4.prev_sw != SW_SAVE) {
        memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
        memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
        g_m4_preset_valid = 1;
        full_preset_apply_hw();
        if (g_full_preset.pre_eq_custom_valid)  eq_band_flush();
        if (g_full_preset.post_eq_custom_valid) eq_band_flush_post();
        analog_delay_set_bypass(&g_adly, 1);
        pt2258_set_mute(1);
        m3_full_voice_reset();
        lcd_fill_rect(0, 208, 320, 24, COLOR_GREEN);
        lcd_string(8, 212, "SAVED: 전체 프리셋 저장 완료 → Mode 3에서 확인하세요",
            COLOR_WHITE, COLOR_GREEN);
        usleep(600000);
        diag_apply_chain_policy("m4_top_sw5_save");
        return ZYNQ_MODE_MENU;
    }
    if (sw == SW_CANCEL && M4.prev_sw != SW_CANCEL) {
        memcpy(&M4.edit, &M4.snap, sizeof(full_preset_t));
        /* [FIX-P9] edit을 snap으로 되돌렸으면 HW도 즉시 복원 */
        full_preset_apply_hw();
        /* [FIX-6] custom 편집 버퍼 무효화: 이후 편집기 진입 시 snap 프리셋 기반으로 재로드 */
        /* delay snap 복원 */
        g_dly_snap_preset = M4.snap.dly_preset;
        dly_edit_read_from_hw();
        m4_draw_top_all();
    }

    //4. 메인 메뉴(TOP)에서 하위 메뉴로 진입하도록 연결
    //마지막으로 TOP 메뉴에서 EQ나 VCA를 누를 때, 하위 메뉴로 빠지도록 m4_update_top()을 수정합니다.
    //[수정 위치 6] m4_update_top() 내부 sw == SW_SELECT 처리부
    //기존의 case M4_T_EQ: 및 case M4_T_VCA:를 아래처럼 변경합니다.
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        switch (M4.top_cur) {
        case M4_T_YM:
            M4.state = M4_YM_MODE_SEL;
            m4_draw_ym_mode_sel(); break;
        case M4_T_HARMONY:
            M4.state = M4_HARMONY; M4.hm_cur = 0;
            m4_draw_hm_all(); break;

            /* ── [Rev.4] SW3 진입: EQ 모드선택 화면 → SW1=프리셋 / SW2=직접편집 ──
             *  선형 시퀀스: HARMONY → PRE EQ 모드선택 → PRE EQ → VCA → POST EQ 모드선택 → POST EQ → DELAY
             *  TOP에서 SW3로 진입해도 동일하게 모드선택을 거친다. */
        case M4_T_EQ:
            if (!DIAG_USE_PRE_EQ) { diag_apply_chain_policy("select_PRE_EQ_disabled"); break; }
            M4.eq_target = 0;
            M4.state = M4_PRE_EQ_MODE_SEL;
            mode4_eq_mode_sel_enter(false, sw);
            break;
        case M4_T_VCA:
            if (!DIAG_USE_VCA) { diag_apply_chain_policy("select_VCA_disabled"); break; }
            M4.vca_param_cur = 0;
            M4.state = M4_VCA_EDIT;
            g_vca_edit.joy_x_moved = 0;
            vca_snap(0);
            m4_draw_vca_edit();
            break;

        case M4_T_POST_EQ:
            if (!DIAG_USE_POST_EQ) { diag_apply_chain_policy("select_POST_EQ_disabled"); break; }
            M4.eq_target = 1;
            M4.state = M4_POST_EQ_MODE_SEL;
            mode4_eq_mode_sel_enter(true, sw);
            break;
        case M4_T_DELAY:
            if (!DIAG_USE_DELAY) { diag_apply_chain_policy("select_DELAY_disabled"); break; }
            /* DELAY 세부 편집 진입: snap 저장 후 HW 읽기 */
            g_dly_snap_preset = M4.edit.dly_preset;
            /* 저장된 custom delay가 있으면 HW에 먼저 복원한 뒤 읽는다. */
            memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
            apply_lfo_then_delay();   /* LFO 먼저 → bypass OFF */
            dly_edit_read_from_hw();
            g_dly_snap = g_dly_edit;  /* SW4 취소용 snap 저장 */
            M4.dly_param_cur = 0;
            M4.state = M4_DELAY_EDIT;
            g_dly_knob.armed = 0;
            m4_draw_dly_edit();
            break;
        case M4_T_LFO:
            if (!DIAG_USE_DELAY) { diag_apply_chain_policy("select_LFO_disabled"); break; }
            /* LFO만 단독으로 바꿀 때도 딜레이 bypass OFF 상태를 유지해야 하므로
             * apply_lfo_then_delay()로 순서 보장. */
            g_full_preset.lfo_preset = M4.edit.lfo_preset;
            apply_lfo_then_delay();
            m4_draw_top_row(M4_T_LFO, 1);     break;
        case M4_T_APPLY: m4_top_do_apply(); break;
        case M4_T_EXIT:
            /* EXIT 항목 선택: 전체 프리셋 동기화 후 MENU 복귀
             * apply 없이 나가면 mode3에서 M4 편집 내용이 반영되지 않는다. */
            memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
            memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
            g_m4_preset_valid = 1;
            full_preset_apply_hw();
            analog_delay_set_bypass(&g_adly, 1);
            pt2258_set_mute(1);
            m3_full_voice_reset();
            diag_apply_chain_policy("m4_top_exit_item");
            return ZYNQ_MODE_MENU;
        }
    }

    if (jy < JOY_LOW && !M4.joy_y_mv) {
        int p = M4.top_cur; if (M4.top_cur > 0) M4.top_cur--; M4.joy_y_mv = 1;
        M4.adc_armed = 0;  /* 항목 이동 시 노브 재동기 */
        m4_draw_top_row(p, 0); m4_draw_top_row(M4.top_cur, 1);
    }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) {
        int p = M4.top_cur; if (M4.top_cur < M4_T_COUNT - 1) M4.top_cur++; M4.joy_y_mv = 1;
        M4.adc_armed = 0;
        m4_draw_top_row(p, 0); m4_draw_top_row(M4.top_cur, 1);
    }

    /* ADC_VOL 노브: 현재 커서 항목의 프리셋을 연속 스캔
     * arm 방식: 노브가 현재 프리셋 위치 근처로 오면 활성화
     * 적용 항목: VCA(0~2), WARM(0~15), SVF(0~12), PRE/POST EQ */
    {
        uint16_t vol = adc_buf[ADC_IDX_VOL];
        uint8_t do_update = 0;
        switch (M4.top_cur) {
        case M4_T_VCA: {
            if (!DIAG_USE_VCA) { diag_apply_chain_policy("adc_VCA_disabled"); break; }
            uint8_t mapped = (uint8_t)((uint32_t)vol * (VCA_PRESET_COUNT - 1) / ADC_MAX);
            if (!M4.adc_armed) {
                uint16_t exp = (uint16_t)((uint32_t)M4.edit.vca_preset * ADC_MAX / (VCA_PRESET_COUNT - 1));
                if ((vol > exp ? vol - exp : exp - vol) < 250u) M4.adc_armed = 1;
            }
            if (M4.adc_armed && mapped != M4.edit.vca_preset) {
                M4.edit.vca_preset = mapped;
                apply_vca_preset(M4.edit.vca_preset);
                do_update = 1;
            }
            break;
        }
        case M4_T_DELAY: {
            if (!DIAG_USE_DELAY) { diag_apply_chain_policy("adc_DELAY_disabled"); break; }
            if (DLY_PRESET_COUNT > 1) {
                uint8_t mapped = (uint8_t)((uint32_t)vol * (DLY_PRESET_COUNT - 1) / ADC_MAX);
                if (!M4.adc_armed) {
                    uint16_t exp = (uint16_t)((uint32_t)M4.edit.dly_preset * ADC_MAX / (DLY_PRESET_COUNT - 1));
                    if ((vol > exp ? vol - exp : exp - vol) < 250u) M4.adc_armed = 1;
                }
                if (M4.adc_armed && mapped != M4.edit.dly_preset) {
                    M4.edit.dly_preset = mapped;
                    M4.edit.dly_custom_valid = 0u;
                    apply_dly_preset(M4.edit.dly_preset);
                    do_update = 1;
                }
            }
            break;
        }
        default: break;
        }
        M4.last_adc_vol = vol;
        if (do_update) {
            m4_draw_top_row(M4.top_cur, 1);
            diag_apply_chain_policy("m4_top_adc_change");
        }
    }

    /* ── ADC_PITCH(가변저항 R): DELAY 화면에서 드라이/웨트 퀵 조절,
     *    REVERB 화면에서 bypass(FDN_BYPASS_DRY) 토글용 스냅 표시 ──
     * 여기서는 TOP 레벨이므로 PITCH는 정보 표시 목적만 (편집은 세부 화면에서) */

    if (jx < JOY_LOW && !M4.joy_x_mv) { m4_top_jx_change(-1); M4.joy_x_mv = 1; }
    if (jx > JOY_HIGH && !M4.joy_x_mv) { m4_top_jx_change(+1); M4.joy_x_mv = 1; }
    return ZYNQ_MODE_INSTSETUP;
}

static int m4_update_ym(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];

    /* ── VGM6 모드: 프리셋 순환 + 즉시 적용 ──────────────────────────── */
    if (M4.edit.ch_mode == CH_MODE_VGM6) {
        /* JY: 프리셋 인덱스 순환 */
        if (jy < JOY_LOW && !M4.joy_y_mv) {
            if (M4.edit.vgm6_inst_idx > 0) M4.edit.vgm6_inst_idx--;
            M4.joy_y_mv = 1;
            m4_draw_ym_all();
        }
        else if (jy > JOY_HIGH && !M4.joy_y_mv) {
            if (M4.edit.vgm6_inst_idx < FULL6_VGM_CATALOG_COUNT - 1u)
                M4.edit.vgm6_inst_idx++;
            M4.joy_y_mv = 1;
            m4_draw_ym_all();
        }

        /* SW3(SELECT): 즉시 HW 적용 + snap/g_full_preset 동기화 */
        if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
            memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
            memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
            g_m4_preset_valid = 1;
            full_preset_apply_hw();
            m4_draw_ym_all();
        }

        /* SW4(EXIT): VGM6 편집 화면 → YM 모드 선택으로 복귀 */
        if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
            M4.state = M4_YM_MODE_SEL;
            m4_draw_ym_mode_sel();
            return ZYNQ_MODE_INSTSETUP;
        }

        /* SW5(SAVE): VGM6 설정 확정 → HARMONY 이동 */
        if (sw == SW_SAVE && M4.prev_sw != SW_SAVE) {
            memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
            memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
            g_m4_preset_valid = 1;
            full_preset_apply_hw();
            m4_preview_stop();
            M4.state = M4_HARMONY;
            M4.hm_cur = 0;
            m4_draw_hm_all();
            return ZYNQ_MODE_INSTSETUP;
        }

        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── INDEPENDENT / DUAL / TRIPLE: 기존 처리 ──────────────────────── */
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        /* SW4(뒤로): YM 편집 → YM 모드선택으로 복귀 */
        M4.state = M4_YM_MODE_SEL;
        m4_draw_ym_mode_sel();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* SW5(SAVE): YM 설정 저장 확정 → HARMONY 화면으로 이동 */
    if (sw == SW_SAVE && M4.prev_sw != SW_SAVE) {
        /* 모든 FM 채널 HW 적용 */
        for (int ch = 0; ch < 3; ch++) {
            fm_ch_cfg_t* fc = &M4.edit.fm[ch];
            ym2203_cache_reset();
            ym2203_patch_apply(&fc->patch_override, (uint8_t)ch);
        }
        /* PSG 채널 HW 적용 */
        for (int ch = 0; ch < 3; ch++) {
            psg_ch_cfg_t* pc = &M4.edit.psg[ch];
            if (pc->enable)
                ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx], (uint8_t)(1u << ch));
            else
                ym2203_ssg_set_vol((uint8_t)ch, 0);
        }
        /* snap + g_full_preset 갱신: ch_mode까지 반드시 동기화해야 Mode 3에 반영됨 */
        full_preset_sync_combo_patch(&M4.edit);
        M4.snap.ch_mode = M4.edit.ch_mode;
        g_full_preset.ch_mode = M4.edit.ch_mode;
        memcpy(&M4.snap.fm, &M4.edit.fm, sizeof(M4.edit.fm));
        memcpy(&M4.snap.psg, &M4.edit.psg, sizeof(M4.edit.psg));
        memcpy(&g_full_preset.fm, &M4.edit.fm, sizeof(M4.edit.fm));    /* [FIX] */
        memcpy(&g_full_preset.psg, &M4.edit.psg, sizeof(M4.edit.psg)); /* [FIX] */
        full_preset_apply_hw();
        lcd_fill_rect(0, 220, LCD_W, 20, COLOR_GREEN);
        lcd_string(52, 222, "YM2203 SAVED! -> HARMONY", COLOR_WHITE, COLOR_GREEN);
        usleep(500000);
        m4_preview_stop();
        M4.state = M4_HARMONY;
        M4.hm_cur = 0;
        m4_draw_hm_all();
        return ZYNQ_MODE_INSTSETUP;
    }

    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        if (M4.ym_cur < 3) {
            M4.fm_ch = M4.ym_cur;
            fm_ch_cfg_t* fc = &M4.edit.fm[M4.fm_ch];
            M4.fm_inst_cur = fc->use_full ? fc->inst_idx : 0;
            M4.fm_raw_cur = fc->use_full ? 0 : fc->inst_idx;
            M4.fm_edit_mode = fc->use_full ? 0 : 1;
            if (!fc->edited) {
                if (fc->use_full && M4.fm_inst_cur < FULL_INST_COUNT)
                    fc->patch_override = FULL_INST_CATALOG[M4.fm_inst_cur]->fm[0];
                else if (!fc->use_full && M4.fm_raw_cur < YM2203_PATCH_COUNT)
                    fc->patch_override = YM2203_PATCHES[M4.fm_raw_cur];
            }
            M4.state = M4_FM_INST; M4.adc_armed = 0; m4_draw_fm_inst();
        }
        else {
            M4.psg_ch = M4.ym_cur - 3; M4.state = M4_PSG_CH; M4.adc_armed = 0; m4_draw_psg();
        }
        return ZYNQ_MODE_INSTSETUP;
    }

    /* SW1(VOL_UP): FM 채널 선택 시 enable 토글 (ON↔OFF) */
    if (sw == SW_VOL_UP && M4.prev_sw != SW_VOL_UP) {
        if (M4.ym_cur < 3) {
            fm_ch_cfg_t* fc = &M4.edit.fm[M4.ym_cur];
            fc->enable ^= 1u;
            /* 즉시 HW 적용: key_off 또는 무음 패치 */
            if (!fc->enable) {
                ym2203_key_off(M4.ym_cur);
                static const ym2203_patch_t fm_mute = {
                    "MUTE",7,0,0,
                    {{0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0},
                     {0,1,127,0,0,0,0,0,0,0,0},{0,1,127,0,0,0,0,0,0,0,0}}
                };
                ym2203_patch_apply(&fm_mute, M4.ym_cur);
            }
            m4_draw_ym_all();
        }
        return ZYNQ_MODE_INSTSETUP;
    }
    if (jy < JOY_LOW && !M4.joy_y_mv) { if (M4.ym_cur > 0) { M4.ym_cur--; m4_draw_ym_all(); } M4.joy_y_mv = 1; }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) { if (M4.ym_cur < 5) { M4.ym_cur++; m4_draw_ym_all(); } M4.joy_y_mv = 1; }
    if (jx < JOY_LOW && !M4.joy_x_mv) { if (M4.ym_cur >= 3) { M4.ym_cur -= 3; m4_draw_ym_all(); } M4.joy_x_mv = 1; }
    else if (jx > JOY_HIGH && !M4.joy_x_mv) { if (M4.ym_cur < 3) { M4.ym_cur += 3; m4_draw_ym_all(); } M4.joy_x_mv = 1; }
    return ZYNQ_MODE_INSTSETUP;
}

static int m4_update_fm_inst(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    fm_ch_cfg_t* fc = &M4.edit.fm[M4.fm_ch];

    /* JOY-X 롱프레스(~500ms) → 편집 모드 전환 */
    if (jx < JOY_LOW || jx > JOY_HIGH) {
        M4.jx_hold_cnt++;
        if (M4.jx_hold_cnt == 50u) {
            M4.fm_edit_mode ^= 1;
            if (M4.fm_edit_mode == 0) {
                fc->use_full = 1; fc->edited = 0;
                fc->inst_idx = M4.fm_inst_cur;
                if (M4.fm_inst_cur < FULL_INST_COUNT)
                    fc->patch_override = FULL_INST_CATALOG[M4.fm_inst_cur]->fm[0];
            }
            else {
                fc->use_full = 0; fc->edited = 0;
                fc->inst_idx = M4.fm_raw_cur;
                if (M4.fm_raw_cur < YM2203_PATCH_COUNT)
                    fc->patch_override = YM2203_PATCHES[M4.fm_raw_cur];
            }
            M4.adc_armed = 0;
            m4_draw_fm_inst();
            return ZYNQ_MODE_INSTSETUP;
        }
    }

    /* SW5(SAVE): 악기 선택 확정 + snap 갱신 → YM 화면 */
    if (sw == SW_SAVE && M4.prev_sw != SW_SAVE) {
        if (!M4.fm_edit_mode) {
            fc->inst_idx = M4.fm_inst_cur; fc->use_full = 1;
            if (!fc->edited)
                fc->patch_override = FULL_INST_CATALOG[M4.fm_inst_cur]->fm[0];
        }
        else {
            fc->inst_idx = M4.fm_raw_cur; fc->use_full = 0;
            if (!fc->edited)
                fc->patch_override = YM2203_PATCHES[M4.fm_raw_cur];
        }
        /* DUAL/TRIPLE 모드에서는 FM0의 악기 번호를 multi.h 결합 패치 선택 인덱스로도 사용 */
        if (M4.fm_ch == 0 && M4.edit.ch_mode == CH_MODE_DUAL) {
            fc->use_dual = 1u;
            fc->use_triple = 0u;
            fc->dual_patch_ptr = YM_DUAL[fc->inst_idx % DUAL_IDX_COUNT];
        }
        else if (M4.fm_ch == 0 && M4.edit.ch_mode == CH_MODE_TRIPLE) {
            fc->use_dual = 0u;
            fc->use_triple = 1u;
            fc->triple_patch_ptr = YM_TRIPLE[fc->inst_idx % TRIPLE_IDX_COUNT];
        }
        else {
            full_preset_sync_combo_patch(&M4.edit);
        }

        ym2203_cache_reset();
        ym2203_patch_apply(&fc->patch_override, (uint8_t)M4.fm_ch);
        M4.snap.fm[M4.fm_ch] = *fc;   /* snap 확정 */
        lcd_fill_rect(0, 220, LCD_W, 20, COLOR_GREEN);
        lcd_string(60, 222, "FM SAVED! -> YM SELECT", COLOR_WHITE, COLOR_GREEN);
        usleep(400000);
        m4_preview_stop(); M4.adc_armed = 0; M4.state = M4_YM; m4_draw_ym_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    /* SW4: FM_INST → YM 선택 뒤로가기 (현재 선택 유지) */
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        m4_preview_stop(); M4.adc_armed = 0; M4.state = M4_YM; m4_draw_ym_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        /* OP 편집 진입: edited==0(첫 진입)이면 현재 악기 베이스 패치를 복사.
         * edited==1(이미 편집 이력)이면 patch_override의 기존 편집값을 그대로 유지.
         * 이렇게 해야 "악기 선택 → OP 세부조정 → 다시 악기화면 복귀 → 재진입"
         * 시에도 편집한 값이 날아가지 않음. */
        if (!fc->edited) {
            if (!M4.fm_edit_mode && M4.fm_inst_cur < FULL_INST_COUNT)
                fc->patch_override = FULL_INST_CATALOG[M4.fm_inst_cur]->fm[0];
            else if (M4.fm_edit_mode && M4.fm_raw_cur < YM2203_PATCH_COUNT)
                fc->patch_override = YM2203_PATCHES[M4.fm_raw_cur];
        }
        M4.op_backup = fc->patch_override;  /* 취소(SW4) 시 복원용 */
        M4.op_sel = 0; M4.op_param_cur = 0;
        M4.adc_armed = 0;
        M4.state = M4_FM_OP; m4_preview_stop(); m4_draw_fm_op();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── ADC_VOL 노브: 악기 번호 직접 매핑 ── */
    {
        uint16_t adc_v = adc_buf[ADC_IDX_VOL];
        if (!M4.adc_armed) {
            /* 현재 악기 위치와 노브 위치가 근접하면 동기화 허용 */
            int max_n = !M4.fm_edit_mode ? FULL_INST_COUNT : YM2203_PATCH_COUNT;
            uint8_t cur_idx = !M4.fm_edit_mode ? M4.fm_inst_cur : M4.fm_raw_cur;
            uint16_t expected = (max_n > 1)
                ? (uint16_t)((uint32_t)cur_idx * ADC_MAX / (max_n - 1)) : 0u;
            uint16_t d = (adc_v > expected) ? (adc_v - expected) : (expected - adc_v);
            if (d < 200u) M4.adc_armed = 1;
        }
        else {
            uint16_t diff = (adc_v > M4.last_adc_vol)
                ? (adc_v - M4.last_adc_vol) : (M4.last_adc_vol - adc_v);
            if (diff > 30u) {
                int inst_changed = 0;
                if (!M4.fm_edit_mode) {
                    uint8_t new_idx = (FULL_INST_COUNT > 1)
                        ? (uint8_t)((uint32_t)adc_v * (FULL_INST_COUNT - 1) / ADC_MAX) : 0u;
                    if (new_idx != M4.fm_inst_cur) {
                        M4.fm_inst_cur = new_idx;
                        fc->patch_override = FULL_INST_CATALOG[M4.fm_inst_cur]->fm[0];
                        m4_preview_fm_full((uint8_t)M4.fm_ch, M4.fm_inst_cur);
                        inst_changed = 1;
                    }
                }
                else {
                    uint8_t new_idx = (YM2203_PATCH_COUNT > 1)
                        ? (uint8_t)((uint32_t)adc_v * (YM2203_PATCH_COUNT - 1) / ADC_MAX) : 0u;
                    if (new_idx != M4.fm_raw_cur) {
                        M4.fm_raw_cur = new_idx;
                        fc->patch_override = YM2203_PATCHES[M4.fm_raw_cur];
                        m4_preview_fm_raw((uint8_t)M4.fm_ch, M4.fm_raw_cur);
                        inst_changed = 1;
                    }
                }
                if (inst_changed) m4_draw_fm_inst();
                M4.last_adc_vol = adc_v;
            }
        }
        M4.last_adc_vol = adc_v;
    }

    int inst_changed = 0;
    if (!M4.fm_edit_mode) {
        if (jy < JOY_LOW && !M4.joy_y_mv) {
            if (M4.fm_inst_cur > 0) { M4.fm_inst_cur--; inst_changed = 1; } M4.joy_y_mv = 1;
        }
        else if (jy > JOY_HIGH && !M4.joy_y_mv) {
            if (M4.fm_inst_cur < FULL_INST_COUNT - 1) { M4.fm_inst_cur++; inst_changed = 1; } M4.joy_y_mv = 1;
        }
        /* SW1/SW2: OP 개수(fm_n) 할당 ? SW1=OP개수+1, SW2=OP개수-1
           full_inst_t의 fm_n을 직접 조정해 채널에 할당할 OP 슬롯 수를 설정 */
        if (sw == SW_VOL_UP && M4.prev_sw != SW_VOL_UP) {
            if (M4.fm_inst_cur < FULL_INST_COUNT) {
                /* full_inst_t는 const이므로 patch_override의 op 활성 수를 편집 영역에 반영 */
                /* fm_ch_cfg_t.op_count를 증가 (0~4 클램프) */
                if (fc->op_count < 4) { fc->op_count++; inst_changed = 1; M4.adc_armed = 0; }
            }
        }
        if (sw == SW_VOL_DOWN && M4.prev_sw != SW_VOL_DOWN) {
            if (fc->op_count > 1) { fc->op_count--; inst_changed = 1; M4.adc_armed = 0; }
        }
        if (jx < JOY_LOW && !M4.joy_x_mv && M4.jx_hold_cnt < 50u) {
            uint8_t p = M4.fm_inst_cur;
            M4.fm_inst_cur = (M4.fm_inst_cur >= 5) ? M4.fm_inst_cur - 5 : 0;
            if (M4.fm_inst_cur != p) { inst_changed = 1; M4.adc_armed = 0; } M4.joy_x_mv = 1;
        }
        else if (jx > JOY_HIGH && !M4.joy_x_mv && M4.jx_hold_cnt < 50u) {
            uint8_t p = M4.fm_inst_cur;
            M4.fm_inst_cur = (M4.fm_inst_cur + 5 < FULL_INST_COUNT)
                ? M4.fm_inst_cur + 5 : (uint8_t)(FULL_INST_COUNT - 1);
            if (M4.fm_inst_cur != p) { inst_changed = 1; M4.adc_armed = 0; } M4.joy_x_mv = 1;
        }
        if (inst_changed) {
            fc->patch_override = FULL_INST_CATALOG[M4.fm_inst_cur]->fm[0];
            /* 새 악기로 바뀌면 op_count를 해당 악기의 fm_n으로 초기화 */
            fc->op_count = (uint8_t)(FULL_INST_CATALOG[M4.fm_inst_cur]->fm_n & 0x07u);
            if (fc->op_count == 0) fc->op_count = 4;  /* fm_n==0이면 4OP 기본 */
            m4_preview_fm_full((uint8_t)M4.fm_ch, M4.fm_inst_cur);
            m4_draw_fm_inst();
        }
    }
    else {
        if (jy < JOY_LOW && !M4.joy_y_mv) {
            if (M4.fm_raw_cur > 0) { M4.fm_raw_cur--; inst_changed = 1; } M4.joy_y_mv = 1;
        }
        else if (jy > JOY_HIGH && !M4.joy_y_mv) {
            if (M4.fm_raw_cur < YM2203_PATCH_COUNT - 1) { M4.fm_raw_cur++; inst_changed = 1; } M4.joy_y_mv = 1;
        }
        if (sw == SW_VOL_UP && M4.prev_sw != SW_VOL_UP) {
            if (fc->op_count < 4) { fc->op_count++; inst_changed = 1; M4.adc_armed = 0; }
        }
        if (sw == SW_VOL_DOWN && M4.prev_sw != SW_VOL_DOWN) {
            if (fc->op_count > 1) { fc->op_count--; inst_changed = 1; M4.adc_armed = 0; }
        }
        if (jx < JOY_LOW && !M4.joy_x_mv && M4.jx_hold_cnt < 50u) {
            uint8_t p = M4.fm_raw_cur;
            M4.fm_raw_cur = (M4.fm_raw_cur >= 5) ? M4.fm_raw_cur - 5 : 0;
            if (M4.fm_raw_cur != p) { inst_changed = 1; M4.adc_armed = 0; } M4.joy_x_mv = 1;
        }
        else if (jx > JOY_HIGH && !M4.joy_x_mv && M4.jx_hold_cnt < 50u) {
            uint8_t p = M4.fm_raw_cur;
            M4.fm_raw_cur = (M4.fm_raw_cur + 5 < YM2203_PATCH_COUNT)
                ? M4.fm_raw_cur + 5 : (uint8_t)(YM2203_PATCH_COUNT - 1);
            if (M4.fm_raw_cur != p) { inst_changed = 1; M4.adc_armed = 0; } M4.joy_x_mv = 1;
        }
        if (inst_changed) {
            fc->patch_override = YM2203_PATCHES[M4.fm_raw_cur];
            m4_preview_fm_raw((uint8_t)M4.fm_ch, M4.fm_raw_cur);
            m4_draw_fm_inst();
        }
    }
    return ZYNQ_MODE_INSTSETUP;
}

static int m4_update_fm_op(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    ym2203_patch_t* ep = &M4.edit.fm[M4.fm_ch].patch_override;

    /* ── 입력 배치 (M4_FM_OP) ──────────────────────────────────────
     * SW1(VOL_UP)   : OP 번호 +1  (OP0→1→2→3→0)
     * SW2(VOL_DOWN) : OP 번호 -1  (OP0→3→2→1→0)
     * SW3(SELECT)   : 현재 편집값으로 미리듣기
     * SW4(EXIT)     : 저장 없이 악기선택 화면으로 복귀
     * SW5(SAVE)     : 편집값 확정 저장 → 악기선택 화면으로 복귀
     * JOY-Y         : 파라미터 항목 이동 (ALG/FB/DT/MUL/TL/RS/AR/AM/DR/SR/SL/RR/SSG)
     * JOY-X         : 선택된 파라미터 값 ±1 (즉시 HW 반영 + 미리듣기)
     * ADC_VOL 노브  : 선택된 파라미터 직접 매핑 (픽업 방식)
     * ADC_PITCH 노브: 캐리어 OP TL 전체 오프셋
     * ─────────────────────────────────────────────────────────────── */

     /* ── SW5(SAVE): OP 편집 확정 저장 → FM_INST 복귀 ── */
    if (sw == SW_SAVE && M4.prev_sw != SW_SAVE) {
        M4.edit.fm[M4.fm_ch].edited = 1;
        M4.edit.fm[M4.fm_ch].use_full = 0;   /* patch_override 경로 확정 */
        ym2203_cache_reset();
        ym2203_patch_apply(ep, (uint8_t)M4.fm_ch);
        M4.snap.fm[M4.fm_ch] = M4.edit.fm[M4.fm_ch];
        lcd_fill_rect(0, 220, LCD_W, 20, COLOR_GREEN);
        lcd_string(20, 222, "OP SAVED! -> 악기선택 복귀", COLOR_WHITE, COLOR_GREEN);
        usleep(300000);
        m4_preview_stop();
        M4.adc_armed = 0;
        M4.state = M4_FM_INST;
        m4_draw_fm_inst();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── SW4(EXIT): 저장 없이 FM_INST 복귀 (편집값 유지) ── */
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        m4_preview_stop();
        M4.adc_armed = 0;
        M4.state = M4_FM_INST;
        m4_draw_fm_inst();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── SW3(SELECT): 현재 편집값으로 미리듣기 ── */
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        ym2203_cache_reset();
        ym2203_patch_apply(ep, (uint8_t)M4.fm_ch);
        m4_preview_fm_op((uint8_t)M4.fm_ch);
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── SW1(VOL_UP): OP 번호 +1 ── */
    if (sw == SW_VOL_UP && M4.prev_sw != SW_VOL_UP) {
        M4.op_sel = (M4.op_sel + 1u) % 4u;
        M4.op_param_cur = 0;
        M4.adc_armed = 0;
        m4_draw_fm_op();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── SW2(VOL_DOWN): OP 번호 -1 ── */
    if (sw == SW_VOL_DOWN && M4.prev_sw != SW_VOL_DOWN) {
        M4.op_sel = (M4.op_sel + 3u) % 4u;
        M4.op_param_cur = 0;
        M4.adc_armed = 0;
        m4_draw_fm_op();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── JOY-Y: 파라미터 항목 이동 ── */
    if (jy < JOY_LOW && !M4.joy_y_mv) {
        if (M4.op_param_cur > 0) { M4.op_param_cur--; M4.adc_armed = 0; m4_draw_fm_op(); }
        M4.joy_y_mv = 1;
    }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) {
        if (M4.op_param_cur < M4_OP_P_COUNT - 1) { M4.op_param_cur++; M4.adc_armed = 0; m4_draw_fm_op(); }
        M4.joy_y_mv = 1;
    }

    /* ── JOY-X: 선택된 파라미터 값 ±1, 즉시 HW 반영 + 미리듣기 ── */
    if (jx < JOY_LOW && !M4.joy_x_mv) {
        uint8_t* ptr = m4_op_ptr(M4.op_param_cur);
        if (*ptr > 0) { (*ptr)--; }
        M4.edit.fm[M4.fm_ch].edited = 1;
        M4.edit.fm[M4.fm_ch].use_full = 0;
        M4.adc_armed = 0;
        ym2203_cache_reset();
        ym2203_patch_apply(ep, (uint8_t)M4.fm_ch);
        m4_preview_fm_op((uint8_t)M4.fm_ch);
        m4_draw_fm_op();
        M4.joy_x_mv = 1;
    }
    else if (jx > JOY_HIGH && !M4.joy_x_mv) {
        uint8_t* ptr = m4_op_ptr(M4.op_param_cur);
        uint8_t mx = M4_OP_P_MAX[M4.op_param_cur];
        if (*ptr < mx) { (*ptr)++; }
        M4.edit.fm[M4.fm_ch].edited = 1;
        M4.edit.fm[M4.fm_ch].use_full = 0;
        M4.adc_armed = 0;
        ym2203_cache_reset();
        ym2203_patch_apply(ep, (uint8_t)M4.fm_ch);
        m4_preview_fm_op((uint8_t)M4.fm_ch);
        m4_draw_fm_op();
        M4.joy_x_mv = 1;
    }

    /* ── ADC_VOL 노브: 선택된 파라미터 직접 매핑 (픽업 방식) ── */
    {
        uint16_t adc_v = adc_buf[ADC_IDX_VOL];
        if (!M4.adc_armed) {
            uint8_t cur_val = *m4_op_ptr(M4.op_param_cur);
            uint8_t mx = M4_OP_P_MAX[M4.op_param_cur];
            uint16_t expected = (mx > 0)
                ? (uint16_t)((uint32_t)cur_val * ADC_MAX / mx) : 0u;
            uint16_t d = (adc_v > expected) ? (adc_v - expected) : (expected - adc_v);
            if (d < 200u) M4.adc_armed = 1;
        }
        else {
            uint16_t diff = (adc_v > M4.last_adc_vol)
                ? (adc_v - M4.last_adc_vol) : (M4.last_adc_vol - adc_v);
            if (diff > 40u) {
                uint8_t* ptr = m4_op_ptr(M4.op_param_cur);
                uint8_t mx = M4_OP_P_MAX[M4.op_param_cur];
                uint8_t new_val = (mx > 0)
                    ? (uint8_t)((uint32_t)adc_v * mx / ADC_MAX) : 0u;
                if (new_val != *ptr) {
                    *ptr = new_val;
                    M4.edit.fm[M4.fm_ch].edited = 1;
                    M4.edit.fm[M4.fm_ch].use_full = 0;
                    ym2203_cache_reset();
                    ym2203_patch_apply(ep, (uint8_t)M4.fm_ch);
                    m4_draw_fm_op();
                }
                M4.last_adc_vol = adc_v;
            }
        }
        M4.last_adc_vol = adc_v;
    }

    /* ── ADC_PITCH 노브: 캐리어 OP TL 전체 오프셋 ── */
    {
        uint16_t adc_p = adc_buf[ADC_IDX_PITCH];
        uint16_t diff = (adc_p > M4.last_adc_pitch)
            ? (adc_p - M4.last_adc_pitch) : (M4.last_adc_pitch - adc_p);
        if (diff > 50u) {
            static const uint8_t car_mask[8] = {
                0x08, 0x0C, 0x0A, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F
            };
            uint8_t mask = car_mask[ep->ALG & 0x07u];
            uint8_t tl_ofs = (uint8_t)((uint32_t)adc_p * 127u / ADC_MAX);
            for (int op = 0; op < 4; op++) {
                if (mask & (1u << op))
                    ep->ops[op].TL = tl_ofs;
            }
            M4.edit.fm[M4.fm_ch].edited = 1;
            M4.edit.fm[M4.fm_ch].use_full = 0;
            ym2203_cache_reset();
            ym2203_patch_apply(ep, (uint8_t)M4.fm_ch);
            m4_draw_fm_op();
            M4.last_adc_pitch = adc_p;
        }
        M4.last_adc_pitch = adc_p;
    }

    return ZYNQ_MODE_INSTSETUP;
}

static int m4_update_psg(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    psg_ch_cfg_t* pc = &M4.edit.psg[M4.psg_ch];

    /* SW5(SAVE): PSG 설정 HW 확정 + snap 갱신 → YM 화면 */
    if (sw == SW_SAVE && M4.prev_sw != SW_SAVE) {
        if (pc->enable) ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx], (uint8_t)(1u << M4.psg_ch));
        else ym2203_ssg_set_vol((uint8_t)M4.psg_ch, 0);
        M4.snap.psg[M4.psg_ch] = *pc;
        lcd_fill_rect(0, 220, LCD_W, 20, COLOR_GREEN);
        lcd_string(40, 222, "PSG SAVED! -> YM SELECT", COLOR_WHITE, COLOR_GREEN);
        usleep(400000);
        m4_preview_stop(); M4.adc_armed = 0; M4.state = M4_YM; m4_draw_ym_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    /* SW4: PSG → YM 선택 뒤로가기 (현재 설정 유지) */
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        m4_preview_stop(); M4.state = M4_YM; m4_draw_ym_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        pc->enable ^= 1u;
        if (pc->enable) ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx], (uint8_t)(1u << M4.psg_ch));
        else ym2203_ssg_set_vol((uint8_t)M4.psg_ch, 0);
        m4_draw_psg();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── ADC_VOL 노브: 볼륨 직접 매핑 ── */
    {
        uint16_t adc_v = adc_buf[ADC_IDX_VOL];
        if (!M4.adc_armed) {
            uint16_t expected = (uint16_t)((uint32_t)pc->amp * ADC_MAX / 15u);
            uint16_t d = (adc_v > expected) ? (adc_v - expected) : (expected - adc_v);
            if (d < 150u) M4.adc_armed = 1;
        }
        else {
            uint16_t diff = (adc_v > M4.last_adc_vol)
                ? (adc_v - M4.last_adc_vol) : (M4.last_adc_vol - adc_v);
            if (diff > 40u) {
                uint8_t new_amp = (uint8_t)((uint32_t)adc_v * 15u / ADC_MAX);
                if (new_amp != pc->amp) {
                    pc->amp = new_amp;
                    if (pc->enable) ym2203_ssg_set_vol((uint8_t)M4.psg_ch, pc->amp);
                    m4_draw_psg();
                }
                M4.last_adc_vol = adc_v;
            }
        }
        M4.last_adc_vol = adc_v;
    }

    /* JY: 패치 선택 ±1 */
    if (jy < JOY_LOW && !M4.joy_y_mv) {
        if ((int)pc->patch_idx > 0) {
            pc->patch_idx = (ym2203_ssg_idx_t)((int)pc->patch_idx - 1);
            if (pc->enable) {
                ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx], (uint8_t)(1u << M4.psg_ch));
                m4_preview_psg((uint8_t)M4.psg_ch, (uint8_t)pc->patch_idx);
            }
            m4_draw_psg();
        } M4.joy_y_mv = 1;
    }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) {
        if ((int)pc->patch_idx < SSG_COUNT - 1) {
            pc->patch_idx = (ym2203_ssg_idx_t)((int)pc->patch_idx + 1);
            if (pc->enable) {
                ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx], (uint8_t)(1u << M4.psg_ch));
                m4_preview_psg((uint8_t)M4.psg_ch, (uint8_t)pc->patch_idx);
            }
            m4_draw_psg();
        } M4.joy_y_mv = 1;
    }

    /* JX: 음정 오프셋 ±1 (기존 볼륨 역할에서 변경) */
    if (jx < JOY_LOW && !M4.joy_x_mv) {
        if (pc->note_offset > -24) {
            pc->note_offset--;
            m4_draw_psg();
        }
        M4.joy_x_mv = 1;
    }
    else if (jx > JOY_HIGH && !M4.joy_x_mv) {
        if (pc->note_offset < 24) {
            pc->note_offset++;
            m4_draw_psg();
        }
        M4.joy_x_mv = 1;
    }

    /* SW1/SW2: PSG에서는 값조절 미사용 (볼륨은 ADC_VOL 가변저항으로만 조절) */
    return ZYNQ_MODE_INSTSETUP;
}

/* HARMONY 서브메뉴 */
static int m4_update_hm(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];

    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        /* SW4(뒤로): HARMONY → YM 채널 선택으로 복귀 */
        M4.edit.harmony = M4.snap.harmony;  /* 편집 취소: snap으로 복원 */
        M4.state = M4_YM;
        M4.ym_cur = 0;
        m4_draw_ym_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_CANCEL && M4.prev_sw != SW_CANCEL) {
        M4.edit.harmony = M4.snap.harmony;
        M4.adc_armed = 0;
        m4_draw_hm_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_SAVE && M4.prev_sw != SW_SAVE) {
        /* SW5(앞으로): HARMONY 저장 → PRE EQ 모드선택으로 전진 */
        M4.snap.harmony = M4.edit.harmony;
        g_full_preset.harmony = M4.edit.harmony;
        harmony_runtime_apply();
        M4.eq_target = 0;
        M4.state = M4_PRE_EQ_MODE_SEL;
        mode4_eq_mode_sel_enter(false, sw);
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        uint8_t* ptr = m4_hm_ptr(M4.hm_cur);
        uint8_t mx = M4_H_MAX[M4.hm_cur];
        if (mx == 1) {
            /* 토글 (ON/OFF 2값) */
            *ptr ^= 1u;
        }
        else {
            /* 수치형: 0→1→...→mx→0 순환 (SW3로 값 확인/선택) */
            *ptr = (uint8_t)((*ptr + 1u) % (mx + 1u));
        }
        harmony_runtime_apply();
        m4_draw_hm_row(M4.hm_cur, 1);
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── ADC_VOL 노브: 현재 항목 직접 매핑 (수치형만) ── */
    {
        uint8_t mx = M4_H_MAX[M4.hm_cur];
        if (mx > 1) {
            uint16_t adc_v = adc_buf[ADC_IDX_VOL];
            if (!M4.adc_armed) {
                uint8_t cur = *m4_hm_ptr(M4.hm_cur);
                uint16_t expected = (uint16_t)((uint32_t)cur * ADC_MAX / mx);
                uint16_t d = (adc_v > expected) ? (adc_v - expected) : (expected - adc_v);
                if (d < 200u) M4.adc_armed = 1;
            }
            else {
                uint16_t diff = (adc_v > M4.last_adc_vol)
                    ? (adc_v - M4.last_adc_vol) : (M4.last_adc_vol - adc_v);
                if (diff > 40u) {
                    uint8_t new_val = (uint8_t)((uint32_t)adc_v * mx / ADC_MAX);
                    uint8_t* ptr = m4_hm_ptr(M4.hm_cur);
                    if (new_val != *ptr) {
                        *ptr = new_val;
                        harmony_runtime_apply();
                        m4_draw_hm_row(M4.hm_cur, 1);
                    }
                    M4.last_adc_vol = adc_v;
                }
            }
            M4.last_adc_vol = adc_v;
        }
    }

    int prev_row = -1;
    if (jy < JOY_LOW && !M4.joy_y_mv) {
        prev_row = M4.hm_cur;
        if (M4.hm_cur > 0) { M4.hm_cur--; M4.adc_armed = 0; } M4.joy_y_mv = 1;
    }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) {
        prev_row = M4.hm_cur;
        if (M4.hm_cur < M4_H_COUNT - 1) { M4.hm_cur++; M4.adc_armed = 0; } M4.joy_y_mv = 1;
    }
    if (prev_row >= 0) {
        if ((prev_row / 9) != (M4.hm_cur / 9)) m4_draw_hm_all();
        else { m4_draw_hm_row(prev_row, 0); m4_draw_hm_row(M4.hm_cur, 1); }
    }

    int dir = 0;
    if (jx < JOY_LOW && !M4.joy_x_mv) { dir = -1; M4.joy_x_mv = 1; }
    else if (jx > JOY_HIGH && !M4.joy_x_mv) { dir = +1; M4.joy_x_mv = 1; }
    /* SW1/SW2: HARMONY에서는 값조절 미사용 (JX + ADC_VOL 노브로 조절) */

    if (dir) {
        uint8_t* ptr = m4_hm_ptr(M4.hm_cur);
        uint8_t mx = M4_H_MAX[M4.hm_cur];
        if (dir > 0 && *ptr < mx) { (*ptr)++; }
        else if (dir < 0 && *ptr > 0) { (*ptr)--; }
        harmony_runtime_apply();
        m4_draw_hm_row(M4.hm_cur, 1);
    }
    return ZYNQ_MODE_INSTSETUP;
}

// ─────────────────────────────────────────────────────────────────────────────
//  §I-E  공개 API
// ─────────────────────────────────────────────────────────────────────────────

static void mode_instsetup_enter(void)
{
    printf("[M4] INST SETUP v2 진입\n");

    /* ── 이펙터 완전 바이패스 고정 (pot.c 방식) ──────────────────────────────
     * 모드4는 YM2203 악기/화성 설정 전용이므로 이펙터 체인을 간섭 없는
     * 바이패스 상태로 고정한다. 설정한 YM2203 값은 모드3 진입 시
     * full_preset_apply_hw()가 그대로 적용한다.
     *   EQ    → preset[0] = BYPASS (flat 계수)
     *   VCA   → preset[0] = BYPASS (1:1 통과)
     *   DELAY → delay_bypass_pass() (hw bypass on, 잔류 피드백 제거)
     *   LFO   → 전체 정지
     * PT2258은 편집 중 preview 시에만 unmute한다. */
    pt2258_set_mute(1);
    apply_pre_eq_preset(0);      /* EQ BYPASS  */
    apply_post_eq_preset(0);     /* POST EQ BYPASS */
    /* [FIX-VCA] VCA는 full_preset_apply_hw()가 g_full_preset.vca_preset으로 적용 */
    /* apply_vca_preset(0) 제거: BYPASS 강제하면 SOFT preset 설정이 무효화됨 */
    /* [M4-DELAY-FIX] apply_dly_custom_from_full_preset includes bypass_off.
     * Do NOT set bypass ON here: preview uses apply_dly_custom to re-enable. */
     /* LFO 먼저 → 딜레이 bypass OFF.
      * instsetup 진입 초기에는 LFO를 preset 값으로 안정화한 후 딜레이를 열어야
      * APF g 변조가 안정 상태에서 시작됨. */
    apply_lfo_then_delay();   /* DIAG_USE_DELAY=0이면 내부에서 bypass+LFO 정지 */

    /* EQ 편집 배율 초기화 (재진입 시 이전 값 잔류 방지) */
    g_eq_fine_mult = 1;
    g_eq_coarse_mult = 10;
    /* 노브 스냅 초기화 */
    g_warm_knob.armed = 0;
    g_svf_knob.armed = 0;
    g_dly_knob.armed = 0;
    g_vca_edit.joy_x_moved = 0;

    /* ── M4 컨텍스트 스냅샷 ─────────────────────────────────────────────── */
    memcpy(&M4.edit, &g_full_preset, sizeof(full_preset_t));
    memcpy(&M4.snap, &g_full_preset, sizeof(full_preset_t));

    M4.state = M4_YM_MODE_SEL;
    M4.top_cur = 0;
    M4.apply_flash = 0;
    M4.flash_cnt = 0;
    M4.ym_cur = 0;
    M4.fm_ch = 0;
    M4.psg_ch = 0;
    M4.hm_cur = 0;
    M4.op_sel = 0;
    M4.op_param_cur = 0;
    M4.preview_active = 0;
    M4.prev_sw = 0;
    M4.joy_y_mv = 0;
    M4.joy_x_mv = 0;
    M4.jx_hold_cnt = 0;
    M4.fm_edit_mode = 0;
    M4.fm_inst_cur = g_full_preset.fm[0].inst_idx;
    M4.fm_raw_cur = 0;
    M4.last_adc_vol = adc_buf[ADC_IDX_VOL];
    M4.last_adc_pitch = adc_buf[ADC_IDX_PITCH];
    M4.adc_armed = 0;
    M4.dly_param_cur = 0;

    /* ── YM2203 설정 적용 (g_full_preset 기반) ──────────────────────────── */
    full_preset_apply_hw();
    /* 보이스 채널 매핑 초기화: 이전 세션 stale 매핑 방지 */
    m3_full_voice_reset();

    m4_draw_ym_mode_sel();
}

static int mode_instsetup_update(spi_packet_t* rx, spi_packet_t* tx)
{
    tx->active_mode = ZYNQ_MODE_INSTSETUP;
    m4_deadzone();
    m4_preview_tick();

    uint8_t sw = rx->sw_status;
    int ret = ZYNQ_MODE_INSTSETUP;

    // [수정 위치 4] mode_instsetup_update() 내의 switch 문 수정
    switch (M4.state) {
    case M4_TOP:       ret = m4_update_top(sw);       break;
    case M4_YM_MODE_SEL: ret = m4_update_ym_mode_sel(sw); break;
    case M4_YM:        ret = m4_update_ym(sw);        break;
    case M4_FM_INST:   ret = m4_update_fm_inst(sw);   break;
    case M4_FM_OP:     ret = m4_update_fm_op(sw);     break;
    case M4_PSG_CH:    ret = m4_update_psg(sw);       break;
    case M4_HARMONY:   ret = m4_update_hm(sw);        break;
    case M4_VCA_EDIT:     ret = m4_update_vca_edit(sw);      break;

        /* ── [Rev.4] PRE EQ 모드 선택 화면 ── */
    case M4_PRE_EQ_MODE_SEL:
        ret = m4_update_eq_mode_sel(sw, false);
        if (ret == -1) {
            /* SW1: 프리셋 선택 모드 */
            M4.state = M4_PRE_EQ_PRESET;
            mode4_eq_preset_enter(false, SW_VOL_UP);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret == -2) {
            /* SW2: 직접 편집 모드 */
            g_eq_edit_sub = 0; g_eq_band_param = 0; g_eq_type_sel = 1;
            M4.state = M4_PRE_EQ_EDIT;
            mode4_eq_enter(false, SW_VOL_DOWN);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret == -3) {
            /* SW5: 스킵 → VCA 편집으로 */
            M4.vca_param_cur = 0;
            M4.state = M4_VCA_EDIT;
            g_vca_edit.joy_x_moved = 0;
            vca_snap(0);
            m4_draw_vca_edit();
            ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret == -4) {
            /* SW4: 이전 (HARMONY) */
            M4.state = M4_HARMONY; M4.hm_cur = 0;
            m4_draw_hm_all();
            ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret == -5) {
            /* SW3(SELECT): 8밴드 통합 프리셋 진입 (PRE 4밴드 + POST 4밴드 동시 설정) */
            M4.state = M4_EQ8_PRESET;
            mode4_eq8_preset_enter(false, SW_SELECT);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        break;

        /* ── [Rev.4] POST EQ 모드 선택 화면 ── */
    case M4_POST_EQ_MODE_SEL:
        ret = m4_update_eq_mode_sel(sw, true);
        if (ret == -1) {
            /* SW1: 프리셋 선택 모드 */
            M4.state = M4_POST_EQ_PRESET;
            mode4_eq_preset_enter(true, SW_VOL_UP);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret == -2) {
            /* SW2: 직접 편집 모드 */
            g_eq_edit_sub = 0; g_eq_band_param = 0; g_eq_type_sel = 1;
            M4.state = M4_POST_EQ_EDIT;
            mode4_eq_enter(true, SW_VOL_DOWN);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret == -3) {
            /* SW5: 스킵 → DELAY 편집으로 */
            if (DIAG_USE_DELAY && p_delay) {
                memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
                apply_lfo_then_delay();   /* LFO 먼저 → bypass OFF */
                g_dly_snap_preset = M4.edit.dly_preset;
                dly_edit_read_from_hw();
                g_dly_snap = g_dly_edit;
                M4.dly_param_cur = 0;
                M4.state = M4_DELAY_EDIT;
                g_dly_knob.armed = 0;
                m4_draw_dly_edit();
            }
            else {
                /* DELAY 없음 → 저장 완료 */
                g_m4_preset_valid = 1;
                full_preset_apply_hw();
                m3_full_voice_reset();
                pt2258_set_mute(1);
                lcd_fill_rect(0, 208, 320, 28, COLOR_GREEN);
                lcd_string(2, 214, "SAVED: 전체 체인 저장 완료", COLOR_WHITE, COLOR_GREEN);
                usleep(600000);
                ret = ZYNQ_MODE_MENU;
            }
            if (ret != ZYNQ_MODE_MENU) ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret == -4) {
            /* SW4: 이전 (VCA) */
            M4.vca_param_cur = 0;
            M4.state = M4_VCA_EDIT;
            g_vca_edit.joy_x_moved = 0;
            vca_snap(0);
            m4_draw_vca_edit();
            ret = ZYNQ_MODE_INSTSETUP;
        }
        break;

        /* ── [Rev.3] PRE EQ 프리셋 선택 화면 ── */
    case M4_PRE_EQ_PRESET:
        ret = m4_update_eq_preset_sel(rx, false);
        if (ret == -10) {
            /* SW3: 프리셋 적용 완료 → 밴드 편집으로 전진 */
            g_eq_edit_sub = 0; g_eq_band_param = 0; g_eq_type_sel = 1;
            M4.state = M4_PRE_EQ_EDIT;
            mode4_eq_enter(false, SW_SELECT);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret == -3) {
            /* SW5: 프리셋만 저장 → VCA 편집으로 전진 */
            g_full_preset.pre_eq_preset = M4.edit.pre_eq_preset;
            g_full_preset.pre_eq_custom_valid = 0u;
            M4.snap.pre_eq_preset = M4.edit.pre_eq_preset;
            M4.snap.pre_eq_custom_valid = 0u;
            M4.vca_param_cur = 0;
            M4.state = M4_VCA_EDIT;
            g_vca_edit.joy_x_moved = 0;
            vca_snap(0);
            m4_draw_vca_edit();
            ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret < 0) {
            /* SW4: 뒤로 → PRE EQ 모드선택으로 */
            M4.state = M4_PRE_EQ_MODE_SEL;
            mode4_eq_mode_sel_enter(false, SW_EXIT);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        break;

    case M4_PRE_EQ_EDIT:
        ret = m4_update_eq_edit_improved(rx, false);
        if (ret == -3) {
            /* SW5: PRE_EQ 저장 확정 → VCA 편집으로 전진 */
            g_full_preset.pre_eq_preset = M4.edit.pre_eq_preset;
            g_full_preset.pre_eq_custom_valid = M4.edit.pre_eq_custom_valid;
            M4.snap.pre_eq_preset = M4.edit.pre_eq_preset;
            M4.snap.pre_eq_custom_valid = M4.edit.pre_eq_custom_valid;
            if (g_full_preset.pre_eq_custom_valid) eq_band_flush();
            M4.vca_param_cur = 0;
            M4.state = M4_VCA_EDIT;
            g_vca_edit.joy_x_moved = 0;
            vca_snap(0);
            m4_draw_vca_edit();
            ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret < 0) {
            /* SW4 취소: PRE EQ 모드선택으로 복귀 */
            M4.state = M4_PRE_EQ_MODE_SEL;
            mode4_eq_mode_sel_enter(false, SW_EXIT);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        break;

        /* ── [Rev.3] POST EQ 프리셋 선택 화면 ── */
    case M4_POST_EQ_PRESET:
        ret = m4_update_eq_preset_sel(rx, true);
        if (ret == -10) {
            /* SW3: 프리셋 적용 완료 → 밴드 편집으로 전진 */
            g_eq_edit_sub = 0; g_eq_band_param = 0; g_eq_type_sel = 1;
            M4.state = M4_POST_EQ_EDIT;
            mode4_eq_enter(true, SW_SELECT);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret == -2) {
            /* SW5: 프리셋만 저장 → DELAY 편집으로 전진 */
            g_full_preset.post_eq_preset = M4.edit.post_eq_preset;
            g_full_preset.post_eq_custom_valid = 0u;
            M4.snap.post_eq_preset = M4.edit.post_eq_preset;
            M4.snap.post_eq_custom_valid = 0u;
            if (DIAG_USE_DELAY && p_delay) {
                memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
                apply_lfo_then_delay();   /* LFO 먼저 → bypass OFF */
                g_dly_snap_preset = M4.edit.dly_preset;
                dly_edit_read_from_hw();
                g_dly_snap = g_dly_edit;
                M4.dly_param_cur = 0;
                M4.state = M4_DELAY_EDIT;
                g_dly_knob.armed = 0;
                m4_draw_dly_edit();
            }
            else {
                g_m4_preset_valid = 1;
                full_preset_apply_hw();
                m3_full_voice_reset();
                pt2258_set_mute(1);
                lcd_fill_rect(0, 208, 320, 28, COLOR_GREEN);
                lcd_string(2, 212, "SAVED: PRE EQ->VCA->POST EQ 저장완료", COLOR_WHITE, COLOR_GREEN);
                usleep(600000);
                ret = ZYNQ_MODE_MENU;
            }
            if (ret != ZYNQ_MODE_MENU) ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret < 0) {
            /* SW4: 뒤로 → POST EQ 모드선택으로 */
            M4.state = M4_POST_EQ_MODE_SEL;
            mode4_eq_mode_sel_enter(true, SW_EXIT);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        break;

    case M4_POST_EQ_EDIT:
        ret = m4_update_eq_edit_improved(rx, true);
        if (ret == -2) {
            /* SW5: POST_EQ 저장 확정 → 전체 동기화 */
            memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
            memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
            if (g_full_preset.pre_eq_custom_valid)  eq_band_flush();
            if (g_full_preset.post_eq_custom_valid) eq_band_flush_post();
            if (DIAG_USE_DELAY && p_delay) {
                apply_lfo_then_delay();   /* LFO 먼저 → bypass OFF */
                g_dly_snap_preset = M4.edit.dly_preset;
                dly_edit_read_from_hw();
                g_dly_snap = g_dly_edit;
                M4.dly_param_cur = 0;
                M4.state = M4_DELAY_EDIT;
                g_dly_knob.armed = 0;
                m4_draw_dly_edit();
                ret = ZYNQ_MODE_INSTSETUP;
            }
            else {
                g_m4_preset_valid = 1;
                full_preset_apply_hw();
                m3_full_voice_reset();
                pt2258_set_mute(1);
                lcd_fill_rect(0, 208, 320, 28, COLOR_GREEN);
                lcd_string(2, 212, "SAVED: YM->HARMONY->PRE EQ->VCA->POST EQ", COLOR_WHITE, COLOR_GREEN);
                lcd_string(60, 224, "Mode 3에서 바로 확인하세요", COLOR_WHITE, COLOR_GREEN);
                usleep(700000);
                ret = ZYNQ_MODE_MENU;
            }
        }
        else if (ret < 0) {
            /* SW4 취소: POST EQ 모드선택으로 복귀 */
            M4.state = M4_POST_EQ_MODE_SEL;
            mode4_eq_mode_sel_enter(true, SW_EXIT);
            ret = ZYNQ_MODE_INSTSETUP;
        }
        break;
        /* ── [Rev.4] EQ8 통합 프리셋 선택 화면 ── */
    case M4_EQ8_PRESET:
        ret = m4_update_eq8_preset(sw);
        if (ret == -3) {
            /* SW3/SW5: 8밴드 프리셋 저장 확정 → POST EQ 설정 화면을 건너뛰고 DELAY 직행 */
            g_eq8_entry_valid = 0u;
            memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
            memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
            if (DIAG_USE_DELAY && p_delay) {
                apply_lfo_then_delay();
                g_dly_snap_preset = M4.edit.dly_preset;
                dly_edit_read_from_hw();
                g_dly_snap = g_dly_edit;
                M4.dly_param_cur = 0;
                M4.state = M4_DELAY_EDIT;
                g_dly_knob.armed = 0;
                m4_draw_dly_edit();
                printf("[EQ8] saved -> DELAY (POST EQ setup skipped; PRE+POST HW uses EQ8)\n");
            }
            else {
                /* DELAY 없음: 저장 완료 후 MENU 복귀 */
                g_m4_preset_valid = 1;
                full_preset_apply_hw();
                m3_full_voice_reset();
                pt2258_set_mute(1);
                lcd_fill_rect(0, 208, 320, 28, COLOR_GREEN);
                lcd_string(2, 214, "SAVED: EQ8 저장완료 -> DELAY 없음", COLOR_WHITE, COLOR_GREEN);
                usleep(600000);
                ret = ZYNQ_MODE_MENU;
            }
            if (ret != ZYNQ_MODE_MENU) ret = ZYNQ_MODE_INSTSETUP;
        }
        else if (ret == -4) {
            /* SW4: 취소 → PRE EQ 모드선택으로 복귀 */
            /* 스냅 복원 */
            if (g_eq8_entry_valid) {
                memcpy(&M4.edit, &g_eq8_entry_snap, sizeof(full_preset_t));
                if (p_pre_eq && p_post_eq) {
                    if (M4.edit.eq8_enabled)
                        apply_eq8_idx(p_pre_eq, p_post_eq, M4.edit.eq8_preset);
                    else {
                        apply_pre_eq_preset(M4.edit.pre_eq_preset);
                        apply_post_eq_preset(M4.edit.post_eq_preset);
                    }
                }
            }
            if (g_eq8_from_post) {
                M4.state = M4_POST_EQ_MODE_SEL;
                mode4_eq_mode_sel_enter(true, SW_EXIT);
            }
            else {
                M4.state = M4_PRE_EQ_MODE_SEL;
                mode4_eq_mode_sel_enter(false, SW_EXIT);
            }
            ret = ZYNQ_MODE_INSTSETUP;
        }
        break;

    case M4_DELAY_EDIT:   ret = m4_update_dly_edit(sw);      break;
    }

    diag_apply_chain_policy("mode4_update_loop");
    M4.prev_sw = sw;
    return ret;
}

static void mode_instsetup_cleanup(void)
{
    m4_preview_stop();
    pt2258_set_mute(1);          /* [PT2258] M4 종료 → mute */
    /* [FIX-M4C1] YM/SSG 완전 소거: 프리뷰 잔류음 방지 */
    if (p_ym) {
        for (int ch = 0; ch < 3; ch++) ym2203_key_off((uint8_t)ch);
        for (int ch = 0; ch < 3; ch++) ym2203_ssg_set_vol((uint8_t)ch, 0);
        ym_write(YM_SSG_MIXER, 0x3Fu);
    }
    /* [FIX-M4C2] 보이스 채널 매핑 초기화: 모드3 진입 시 clean slate */
    m3_full_voice_reset();
    /* [FIX-VCA] cleanup 시 vca_bypass() 대신 g_full_preset.vca_preset 복원.
     * vca_bypass()를 호출하면 모드3 진입 시 full_preset_apply_hw() 전에
     * BYPASS가 선행 적용돼도 바로 덮어쓰므로 실질적으로 무해하지만,
     * vca_preset=1(SOFT)이 기본값인 상태에서는 bypass 고정이 불필요. */
    if (DIAG_USE_VCA) apply_vca_preset(g_full_preset.vca_preset);
    else              diag_bypass_vca("mode4_cleanup");

    if (DIAG_USE_DELAY && p_delay) {
        analog_delay_set_feedback_q15(&g_adly, 0u);
        analog_delay_bypass_on(&g_adly);
    }
    else {
        diag_bypass_delay("mode4_cleanup");
    }
    diag_apply_chain_policy("mode4_cleanup");
    printf("[M4] INST SETUP 종료\n");
}

// =============================================================================
//  [섹션 J]  FSM 디스패처 & main()
// =============================================================================

typedef struct {
    void (*enter)(void);
    int  (*update)(spi_packet_t*, spi_packet_t*);
    void (*cleanup)(void);
} fsm_handler_t;

// FSM 핸들러: spi_packet_t 타입을 정확히 사용해야 FSM 배열에서 에러가 안 납니다.
void mode_menu_enter(void);
int  mode_menu_update(spi_packet_t* rx, spi_packet_t* tx);
void mode_menu_cleanup(void);

void mode_synth_enter(void);
int  mode_synth_update(spi_packet_t* rx, spi_packet_t* tx);
void mode_synth_cleanup(void);

void mode_vgm_enter(void);
int  mode_vgm_update(spi_packet_t* rx, spi_packet_t* tx);
void mode_vgm_cleanup(void);

void mode_midivis_enter(void);
int  mode_midivis_update(spi_packet_t* rx, spi_packet_t* tx);
void mode_midivis_cleanup(void);

void mode_instsetup_enter(void);
int  mode_instsetup_update(spi_packet_t* rx, spi_packet_t* tx);
void mode_instsetup_cleanup(void);

static const fsm_handler_t FSM[ZYNQ_MODE_COUNT] = {
    { mode_menu_enter,       mode_menu_update,       mode_menu_cleanup      },
    { mode_synth_enter,      mode_synth_update,      mode_synth_cleanup     },
    { mode_vgm_enter,        mode_vgm_update,        mode_vgm_cleanup       },
    { mode_midivis_enter,    mode_midivis_update,    mode_midivis_cleanup   },
    { mode_instsetup_enter,  mode_instsetup_update,  mode_instsetup_cleanup },
};

static void hw_cleanup(void)
{
    if (p_lfo) for (int i = 0; i < 8; i++) {
        reg_wr(p_lfo, LFO_OFF(i, 0), LFO_FCW_OFF);
        reg_wr(p_lfo, LFO_OFF(i, 1), 0u);
    }
}
// 최상위 스코프에 독립된 함수로 구현합니다.
static void system_cleanup(void)
{
    if (p_lcd) {
        lcd_shadow = LCD_RST;
        reg_wr(p_lcd, LCD_GPIO1_OFF, lcd_shadow);
    }
    if (p_ym) {
        reg_wr(p_ym, YM_GPIO1_OFF, 0u);
        reg_wr(p_ym, YM_GPIO2_OFF, 0u);
    }

    // 전역 포인터 초기화
    p_delay = p_pre_eq = p_post_eq = p_lfo = p_vca = NULL;
    p_ym = p_fifosg = p_lcd = NULL;
    p_efin = NULL;

    // [수정] 만약 uio_xxx 변수들이 이미 포인터라면 &를 제거해야 E0147 에러가 사라집니다.
    // 만약 일반 구조체 변수라면 static void uio_close(uio_dev_t* dev) 구조가 맞으므로 &를 유지합니다.
    // 에러 메시지(uio_dev_t *dev 요구)를 볼 때 &를 제거하는 것이 올바를 확률이 높습니다.
    uio_close(&uio_delay);
    uio_close(&uio_pre_eq);
    uio_close(&uio_post_eq);
    uio_close(&uio_lfo_0);
    uio_close(&uio_vca);
    uio_close(&uio_ym2203);
    uio_close(&uio_fifo_sg);
    uio_close(&uio_lcd_ctrl);
    uio_close(&uio_CODE);
    uio_close(&uio_rst_gpio);


    if (fd_lcd >= 0) { close(fd_lcd); fd_lcd = -1; }
    if (fd_esp >= 0) { close(fd_esp); fd_esp = -1; }

    pt2258_cleanup();
}

int main(void)
{
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);

    printf("=== effect_bd_uio LIGHT CHAIN ===\n");
    printf("  M0:MENU M1:SYNTH M2:VGM M3:MIDI+SOUND M4:INST SETUP v2\n");
    printf("  [LIGHT] YM2203 -> PRE EQ -> VCA -> POST EQ -> DELAY + PT2258\n");
    printf("  SW3=선택  SW4=나가기  SW5=취소\n");

    if (load_uio_module() != 0) {
        fprintf(stderr, "[FATAL] UIO 로드 실패\n"); return EXIT_FAILURE;
    }

    /* [V14] 초기화 순서:
       1. full_preset_default()   ? g_full_preset 기본값
       2. M4 컨텍스트 클리어
       ※ ym2203_init / m3_full_voice_reset / harmony_runtime_apply는
          p_ym 매핑(UIO open) 이후로 이동 ? p_ym=NULL 상태에서의
          ym_write() nop 문제 및 GPIO TRI 미설정 방지              */
    full_preset_default();
    memset(&M4, 0, sizeof(M4));

    printf("[UIO] 디바이스 탐색...\n");
    uio_delay = uio_open_name_or_addr("analog_delay_top_0", ADDR_DELAY, PROT_READ | PROT_WRITE);
    uio_pre_eq = uio_open_name_or_addr("pre_eq_4band_0", ADDR_PRE_EQ, PROT_READ | PROT_WRITE);
    uio_post_eq = uio_open_name_or_addr("post_eq_4band_0", ADDR_POST_EQ, PROT_READ | PROT_WRITE);
    uio_lfo_0 = uio_open_by_addr("delay_LFO", ADDR_LFO_0, PROT_READ | PROT_WRITE);
    uio_vca = uio_open_name_or_addr("vca_compressor_0", ADDR_VCA, PROT_READ | PROT_WRITE);
    uio_DMA = uio_open_by_addr("axi_dma", ADDR_DMA, PROT_READ | PROT_WRITE);
    uio_DMA_BUF = uio_open_by_addr("audio_dma_buffer", ADDR_DMA_BUF, PROT_READ | PROT_WRITE);
    uio_ym2203 = uio_open_by_addr("axi_ym2203", ADDR_YM2203, PROT_READ | PROT_WRITE);
    uio_lcd_ctrl = uio_open_by_addr("spi_lcd", ADDR_LCD_CTL, PROT_READ | PROT_WRITE);
    uio_fifo_sg = uio_open_by_addr("ym2203_fifo_sg", ADDR_FIFO_SG, PROT_READ);
    uio_CODE = uio_open_by_addr("CODE", ADDR_CODE, PROT_READ | PROT_WRITE);
    uio_rst_gpio = uio_open_by_addr("rst_gpio", ADDR_rst, PROT_READ | PROT_WRITE);

    if (!uio_delay.map || !uio_pre_eq.map || !uio_post_eq.map || !uio_vca.map || !uio_ym2203.map || !uio_lcd_ctrl.map) {
        fprintf(stderr, "[FATAL] 필수 UIO 열기 실패 (필수: YM2203/PRE_EQ/VCA/POST_EQ/DELAY/LCD)\n");
        hw_cleanup();
        return EXIT_FAILURE;
    }
    if (!uio_lfo_0.map)     printf("[WARN] delay_LFO 미연결 ? delay 기본 동작은 유지, 변조만 비활성\n");
    if (!uio_DMA.map)       printf("[WARN] DMA 미연결\n");
    if (!uio_DMA_BUF.map)   printf("[WARN] DMA 버퍼(audio_dma_buffer) 미연결 → 녹음 불가\n");
    if (!uio_fifo_sg.map)   printf("[WARN] ym2203_fifo_sg 미연결\n");
    if (!uio_CODE.map)      printf("[WARN] CODE 미연결 ? PT2258 주소 GPIO 없이 기본/스캔 주소 사용\n");
    if (!uio_rst_gpio.map)  printf("[WARN] rst 미연결 ? esp32 spi 통신 실패 rst 로드 실패 \n");


    /* [LIGHT-CHAIN 초기화 순서]
       fifo_to_axis → pre_eq → vca → post_eq → [delay_LFO0 →] analog_delay → DAC
       목표: 불확실한 고도화 블록을 제거하고 YM2203 + 핵심 4개 이펙터를 안정화 */

       /* [1] pre_eq (체인 1번째) ? bypass 계수 기록으로 투명 통과 보장 */
    p_pre_eq = (volatile uint8_t*)uio_pre_eq.map;
    if (p_pre_eq) {
        apply_pre_eq_preset(g_full_preset.pre_eq_preset); /* idx 0 = flat 계수, ctrl=RUN */
        printf("[pre_EQ] active 초기화 preset=%u\n", g_full_preset.pre_eq_preset);
    }

    /* [2] VCA (체인 2번째) ? bypass: thresh 최대, cs=0, makeup=1.0 */
    p_vca = (volatile uint8_t*)uio_vca.map;
    if (p_vca) {
        apply_vca_preset(g_full_preset.vca_preset);       /* 기본 idx 1 = SOFT 2:1 */
        printf("[VCA] active 초기화 preset=%u\n", g_full_preset.vca_preset);
    }


    /* [3] post_eq ? bypass 계수 기록 */
    p_post_eq = uio_post_eq.map ? (volatile uint8_t*)uio_post_eq.map : NULL;
    if (p_post_eq) {
        apply_post_eq_preset(g_full_preset.post_eq_preset); /* idx 0 = flat 계수, ctrl=RUN */
        printf("[post_EQ] active 초기화 preset=%u\n", g_full_preset.post_eq_preset);
    }

    /* [4] delay_LFO0 (analog_delay 변조원) ? delay 해제 전 먼저 안정화 */
    p_lfo = uio_lfo_0.map ? (volatile uint8_t*)uio_lfo_0.map : NULL;
    if (p_lfo) {
        apply_lfo_preset(g_full_preset.lfo_preset);
        printf("[LFO0] delay_LFO 초기화 완료 (preset=%u)\n", g_full_preset.lfo_preset);
    }

    /* [5] analog_delay ? LFO0 안정 후 bypass 해제
       adly_adapter_init()이 g_adly 구조체 완전 초기화 + bypass 해제까지 수행 */
    p_delay = (volatile uint8_t*)uio_delay.map;
    {
        /* 먼저 bypass=1 잠금 (LFO0 설정 중 APF 변조 방지) */
        analog_delay_t tmp_adly; memset(&tmp_adly, 0, sizeof(tmp_adly));
        tmp_adly.base_delay = (volatile uint32_t*)p_delay;
        analog_delay_bypass_on(&tmp_adly);
    }
    adly_adapter_init(); /* g_adly 완전 초기화 + bypass 해제 */
    apply_dly_custom_from_full_preset(); /* [FIX④] AXI 리셋값(mode=0xEF) 방지 ? preset을 즉시 기록 */
    printf("[DELAY] LFO0 이후 bypass 해제 + preset 적용 완료\n");


    /* [7] YM2203 + LCD/FIFO/CODE 포인터 할당 후 YM 초기화
        p_ym 유효 이후에만 ym_gpio_init/ym2203_init 실행 가능 ? 순서 변경 금지
        ym_gpio_init → ym2203_init → m3_full_voice_reset → harmony_runtime_apply */
    p_ym = (volatile uint8_t*)uio_ym2203.map;
    p_lcd = (volatile uint8_t*)uio_lcd_ctrl.map;
    p_fifosg = uio_fifo_sg.map ? (volatile uint8_t*)uio_fifo_sg.map : NULL;
    p_code = uio_CODE.map ? (volatile uint8_t*)uio_CODE.map : NULL;
    p_rst_gpio = uio_rst_gpio.map ? (volatile uint8_t*)uio_rst_gpio.map : NULL;
    /* [RST-INIT] 부팅 시 ESP32 RST = 1 고정 (AXI GPIO 출력 1비트) */
    if (p_rst_gpio) {
        reg_wr(p_rst_gpio, 0x00u, 1u);
        printf("[RST] ESP32 rst GPIO = 1\n");
    }

    ym_gpio_init();        /* AXI GPIO TRI=0 확정 */
    ym_reset();            /* [FIX⑦] HW 리셋 ? 파워온 후 칩 불확정 상태 제거 */
    ym2203_init(50u);      /* YM HW 레지스터 초기화 (소프트) */
    m3_full_voice_reset(); /* 보이스 테이블 초기화 */
    harmony_runtime_apply();

    /* [FIX②] PT2258 init을 diag 이전으로 이동 ? diag가 fd<0 오경고 출력하지 않도록 */
    (void)pt2258_init_linux();  /* init 내부에서 RESET 후 g_pt2258_att_db 볼륨 write */
    pt2258_set_mute(1);         /* 부팅 완료 전 mute 유지 */

    printf("[BOOT] LIGHT 체인 초기화 완료: YM2203->PRE_EQ->VCA->POST_EQ->DELAY\n");

    /* ★ [DIAG] 전체 UIO 매핑 검증 ? 필수 단계 누락 즉시 발견 */
    {
        int uio_fatal = diag_check_all_uio();
        if (uio_fatal) {
            fprintf(stderr, "[DIAG] UIO 필수 블록 %d개 매핑 실패 ? hw_cleanup 후 종료\n", uio_fatal);
            hw_cleanup(); return EXIT_FAILURE;
        }
    }

    /* ym_gpio_init은 위에서 이미 호출됨 (p_ym 매핑 직후) */
    if (p_lcd) {
        reg_wr(p_lcd, LCD_GPIO1_TRI, ~LCD_OUT_MASK & 0xFu);
        lcd_shadow = LCD_RST;
        reg_wr(p_lcd, LCD_GPIO1_OFF, lcd_shadow);
        printf("[GPIO] spi_lcd 4bit OUT\n");
    }

    fd_lcd = open(SPI_DEV_LCD, O_RDWR);
    fd_esp = open(SPI_DEV_ESP32, O_RDWR);
    if (fd_lcd < 0 || fd_esp < 0) {
        fprintf(stderr, "[FATAL] SPI open: %s\n", strerror(errno));
        hw_cleanup(); return EXIT_FAILURE;
    }
    {
        uint8_t mode = SPI_MODE_0, bits = 8; uint32_t spd;
        spd = LCD_SPI_HZ;
        if (ioctl(fd_lcd, SPI_IOC_WR_MODE, &mode) < 0 ||
            ioctl(fd_lcd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
            ioctl(fd_lcd, SPI_IOC_WR_MAX_SPEED_HZ, &spd) < 0) {
            fprintf(stderr, "[FATAL] LCD SPI 설정 실패: %s\n", strerror(errno));
            hw_cleanup(); return EXIT_FAILURE;
        }
        spd = SPI_ESP32_HZ;
        if (ioctl(fd_esp, SPI_IOC_WR_MODE, &mode) < 0 ||
            ioctl(fd_esp, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
            ioctl(fd_esp, SPI_IOC_WR_MAX_SPEED_HZ, &spd) < 0) {
            fprintf(stderr, "[FATAL] ESP32 SPI 설정 실패: %s\n", strerror(errno));
            hw_cleanup(); return EXIT_FAILURE;
        }
    }

    lcd_init(); lcd_clear(COLOR_BLACK);
    lcd_draw_header(COLOR_DKGREEN, "  LIGHT Ready", COLOR_WHITE);
    lcd_string(4, 22, "SW3:SELECT  SW4:EXIT  SW5:CANCEL", COLOR_YELLOW, COLOR_BLACK);
    lcd_string(4, 38, "M1: SYNTH/MIDI", COLOR_CYAN, COLOR_BLACK);
    lcd_string(4, 54, "M2: VGM PLAYER", COLOR_MAGENTA, COLOR_BLACK);
    lcd_string(4, 70, "M3: MIDI KBD + YM2203", COLOR_ORANGE, COLOR_BLACK);
    lcd_string(4, 86, "M4: INST SETUP v2", 0x4A10u, COLOR_BLACK);
    lcd_string(4, 102, "    YM+PRE_EQ+VCA+POST_EQ+DLY+PT2258", COLOR_WHITE, COLOR_BLACK);
    /* 전체 프리셋 HW 적용 (2차)
       LIGHT 체인(YM/PRE_EQ/VCA/POST_EQ/DELAY/LFO0)만 확정. */
    full_preset_apply_hw();
    /* ★ [DIAG] 부팅 완료 1회 전체 체인 상태 덤프 */
    diag_dump_chain("부팅 완료");
    msleep(2000);

    pthread_t vgm_tid;
    if (pthread_create(&vgm_tid, NULL, vgm_rt_thread, NULL) != 0) {
        perror("[FATAL] vgm pthread"); hw_cleanup(); return EXIT_FAILURE;
    }
    pthread_t midi_tid;
    g_midi_thread_exit = 0;
    if (pthread_create(&midi_tid, NULL, midi_rx_thread, NULL) != 0) {
        perror("[FATAL] midi pthread");
        /* vgm 스레드가 이미 실행 중 ? 안전하게 종료 후 join */
        g_vgm_play = 0; MEM_BARRIER(); g_vgm_exit = 1;
        pthread_join(vgm_tid, NULL);
        hw_cleanup(); return EXIT_FAILURE;
    }
    msleep(300);

    spi_packet_t tx_pkt, rx_pkt;
    memset(&tx_pkt, 0, sizeof(tx_pkt));
    memset(&rx_pkt, 0, sizeof(rx_pkt));
    tx_pkt.sync_marker = 0xA1;
    tx_pkt.active_mode = ZYNQ_MODE_MENU;

    int cur = ZYNQ_MODE_MENU, entered = -1;
    uint32_t loop_cnt = 0; uint32_t sync_fail_run = 0;
    time_t last_stat = time(NULL);
    static uint8_t last_vol_sw = 0;  /* 볼륨 버튼 엣지 검출용 */

    printf("[MAIN] FSM 루프 시작\n");

    while (!g_exit) {
        tx_pkt.sync_marker = 0xA1;
        spi_xfer(fd_esp, (uint8_t*)&tx_pkt, (uint8_t*)&rx_pkt,
            (int)sizeof(spi_packet_t), SPI_ESP32_HZ);

        if (rx_pkt.sync_marker != 0xA1) {
            sync_fail_cnt++; sync_fail_run++;
            if (sync_fail_run >= 5u) {
                fprintf(stderr, "[WARN] SPI sync %u회 실패\n", sync_fail_run);
                lcd_fill_rect(0, 0, LCD_W, 18, COLOR_RED);
                lcd_string(4, 5, "  !! SPI SYNC ERROR !!", COLOR_WHITE, COLOR_RED);
                if (entered >= 0 && entered < ZYNQ_MODE_COUNT) FSM[entered].cleanup();
                /* [FIX-P8] sync 복구 시 오디오 체인 안전 상태 보장 */
                vca_bypass();
                delay_mute();
                /* [RST-SPI] SPI 5회 실패 → ESP32 RST 펄스 (0→100ms→1, 500ms 부팅 대기) */
                if (p_rst_gpio) {
                    reg_wr(p_rst_gpio, 0x00u, 0u);
                    fprintf(stderr, "[RST] ESP32 rst = 0 (\xeb\xa6\xac\xec\x85\x8b)\n");
                    usleep(100000);
                    reg_wr(p_rst_gpio, 0x00u, 1u);
                    fprintf(stderr, "[RST] ESP32 rst = 1 (\xeb\xb3\xb5\xea\xb5\xac)\n");
                    usleep(500000);
                }
                cur = ZYNQ_MODE_MENU; entered = -1; sync_fail_run = 0;
            }
            usleep(5000); continue;
        }
        sync_ok_cnt++; sync_fail_run = 0;

        adc_buf[ADC_IDX_VOL] = adc_smooth(adc_buf[ADC_IDX_VOL], rx_pkt.adc_vol);
        adc_buf[ADC_IDX_PITCH] = adc_smooth(adc_buf[ADC_IDX_PITCH], rx_pkt.adc_pitch);
        adc_buf[ADC_IDX_JOYX] = adc_smooth(adc_buf[ADC_IDX_JOYX], rx_pkt.joy_x);
        adc_buf[ADC_IDX_JOYY] = adc_smooth(adc_buf[ADC_IDX_JOYY], rx_pkt.joy_y);

        /* ── 신규: 버튼식 볼륨 제어 로직 ── */
        //[수정 위치 2] main() 함수 내부 while(!g_exit) 루프 안 (adc_buf 갱신 바로 아래)
        if (rx_pkt.sw_status != last_vol_sw) {
            if (rx_pkt.sw_status == SW_VOL_UP) {
                if (g_pt2258_att_db > 0) {
                    g_pt2258_att_db--;
                    pt2258_set_all_volume_db(g_pt2258_att_db);
                    if (cur == ZYNQ_MODE_MENU) {
                        char vbuf[32];
                        snprintf(vbuf, sizeof(vbuf), "L:-%2udB  R:-%2udB",
                            (unsigned)g_pt2258_att_l_db, (unsigned)g_pt2258_att_r_db);
                        lcd_string(4, 212, vbuf, COLOR_CYAN, COLOR_BLACK);
                    }
                }
            }
            else if (rx_pkt.sw_status == SW_VOL_DOWN) {
                if (g_pt2258_att_db < 79) {
                    g_pt2258_att_db++;
                    pt2258_set_all_volume_db(g_pt2258_att_db);
                    if (cur == ZYNQ_MODE_MENU) {
                        char vbuf[32];
                        snprintf(vbuf, sizeof(vbuf), "L:-%2udB  R:-%2udB",
                            (unsigned)g_pt2258_att_l_db, (unsigned)g_pt2258_att_r_db);
                        lcd_string(4, 212, vbuf, COLOR_CYAN, COLOR_BLACK);
                    }
                }
            }
            last_vol_sw = rx_pkt.sw_status;
        }

        if (cur != entered) {
            if (entered >= 0 && entered < ZYNQ_MODE_COUNT) FSM[entered].cleanup();
            if (cur >= 0 && cur < ZYNQ_MODE_COUNT) {
                FSM[cur].enter();
                diag_pt2258_gate_check(cur);  /* [DIAG] 모드 전환 mute gate 검증 */
                tx_pkt.active_mode = (uint8_t)cur;
            }
            entered = cur;
        }

        int next = cur;
        if (cur >= 0 && cur < ZYNQ_MODE_COUNT)
            next = FSM[cur].update(&rx_pkt, &tx_pkt);
        if (next < 0 || next >= ZYNQ_MODE_COUNT) next = ZYNQ_MODE_MENU;
        cur = next;

        if (++loop_cnt % 10 == 0)
            lcd_draw_statusbar((sync_fail_run == 0), adc_buf[ADC_IDX_VOL], cur);

        time_t now = time(NULL);
        if (now - last_stat >= 10) {
            uint32_t tot = sync_ok_cnt + sync_fail_cnt;
            printf("[STAT] ok=%u fail=%u (%.1f%%) mode=%d\n",
                sync_ok_cnt, sync_fail_cnt,
                tot ? (double)sync_ok_cnt * 100.0 / tot : 0.0, cur);
            last_stat = now;
        }
        diag_periodic_dump();  /* ★ [DIAG] 10초 주기 체인 상태 덤프 */
        usleep(10000u);
    }

    printf("\n[MAIN] 종료...\n");
    g_vgm_play = 0; MEM_BARRIER(); g_vgm_exit = 1;
    g_midi_thread_exit = 1;
    pthread_join(vgm_tid, NULL);
    pthread_join(midi_tid, NULL);
    if (m3_fd_midi >= 0) { close(m3_fd_midi); m3_fd_midi = -1; }
    m3_full_all_off();
    ym_silence();
    pt2258_set_mute(1);
    hw_cleanup();
    printf("[MAIN] 완료\n");
    return EXIT_SUCCESS;
}