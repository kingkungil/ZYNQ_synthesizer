// =============================================================================
//  effect_bd_uio_v14.c  —  PetaLinux UIO 드라이버  Rev.14
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
//    D1. 전역 설정 구조체 (full_preset_t — SQLite 직렬화 준비)
//    D2. 이펙터 프리셋 테이블
//    D3. YM2203 보이스 관리 (m3_full_* — SYNTH/MIDI 공용)
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
//
//  └─ V15 변경사항 ───────────────────────────────────────────────────────────┘
//  [V15-BUG1]  mode_instsetup_update: 잘못된 x_locked 블록 제거
//                M4.state를 raw int로 조작하여 FSM 파괴하던 코드 삭제.
//                JOY-X는 각 m4_update_* 서브함수가 adc_buf[ADC_IDX_JOYX]로 처리.
//
//  [V15-FMOP]  m4_update_fm_op: OP 탭 전환 기능 추가
//                SW3 (SW_SELECT) → OP 탭 순환 (OP0→OP1→OP2→OP3→OP0)
//                SW4 (SW_EXIT)   → 취소 + 백업 복원 후 FM_INST 복귀 (기존 동일)
//                SW5 (SW_CANCEL) → 편집 확정 (edited=1, use_full=0) + FM_INST 복귀
//                JOY-Y           → 파라미터 행 이동 (기존 동일)
//                JOY-X           → 선택 파라미터 값 ±1 (기존 동일)
//
//  [V15-HINT]  m4_draw_fm_op: LCD 하단 힌트 문자열 수정
//                "JY:파라미터  JX:값+/-1  SW3:OP탭전환  SW4:취소  SW5:확정"
//
//  ▶ JOY-X 역할 정리 (모드별)
//    M4_TOP     : EQ/VCA/DLY/LFO 커서일 때 프리셋 ±1 변경
//    M4_YM      : FM 그룹 ↔ PSG 그룹 전환
//    M4_FM_INST : ±5 악기 빠른 이동 (롱프레스 ~500ms → RAW/FULL 모드 전환)
//    M4_FM_OP   : 선택된 OP 파라미터 값 ±1
//    M4_PSG_CH  : 선택된 파라미터 값 변경 (패치/볼륨/음정오프셋)
//    M4_HARMONY : 선택된 항목 값 ±1
// =============================================================================

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 A1]  헤더 / 기본 상수 / 타입
// ─────────────────────────────────────────────────────────────────────────────
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/spi/spidev.h>

#ifdef _MSC_VER
/* ==========================================================
   Visual Studio IntelliSense & Error Suppression Block
   (실제 빌드는 리눅스 환경의 GCC에서 수행)
   ========================================================== */

   // 1. 리눅스/GCC 전용 키워드 및 속성 무시
#define __attribute__(x)
#define inline __inline
#define __restrict

// 2. POSIX/Linux IO 및 메모리 상수
#define PROT_READ    0x1
#define PROT_WRITE   0x2
#define MAP_SHARED   0x01
#define MAP_FAILED   ((void*)-1)
#define O_RDONLY     0x0000
#define O_WRONLY     0x0001
#define O_RDWR       0x0002
#define O_NONBLOCK   04000

// 3. 스레드, 시간 및 poll 관련 정의 (E0070 해결)
#define CLOCK_MONOTONIC 1
#define SCHED_FIFO      1
#define POLLIN          0x0001
typedef int pthread_t;
typedef int pthread_attr_t;
struct timespec { long tv_sec; long tv_nsec; };
struct sched_param { int sched_priority; };
struct sigaction { int sa_handler; int sa_mask; int sa_flags; };
struct pollfd { int fd; short events; short revents; }; // pollfd 구조체 정의 추가
#define SIGINT  2
#define SIGTERM 15

// 4. SPI (spidev.h) 핵심 구조체 정의
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

// 5. 패치 및 악기 구조체 extern 선언 (E0020 & lvalue 에러 해결)
// 실제 구조체 이름이 다를 경우 프로젝트에 맞게 수정하세요.
typedef struct { int dummy; int pb_cent; } full_inst_t;
typedef struct { int dummy; } dual_patch_t;
typedef struct { int dummy; } triple_patch_t;

// 기존 국악기/클래식 악기
extern const full_inst_t FULL_DAEGEUM; extern const full_inst_t FULL_SOGEUM;
extern const full_inst_t FULL_PIRI;    extern const full_inst_t FULL_GEOMUNGO;
extern const full_inst_t FULL_AJAENG;  extern const full_inst_t FULL_DANSO;
extern const full_inst_t FULL_HUN;    extern const full_inst_t FULL_GAYAGEUM;
extern const full_inst_t FULL_HAEGEUM; extern const full_inst_t FULL_VIOLIN;
extern const full_inst_t FULL_CELLO;

// [추가] ym2203_multi.h에서 에러 나는 Dual/Triple 패치들
extern const dual_patch_t DUAL_PIANO;     extern const dual_patch_t DUAL_EPIANO;
extern const dual_patch_t DUAL_STRINGS;   extern const dual_patch_t DUAL_BRASS;
extern const dual_patch_t DUAL_ORGAN;     extern const dual_patch_t DUAL_CHOIR;
extern const dual_patch_t DUAL_OVERDRIVE; extern const dual_patch_t DUAL_BASS;
extern const dual_patch_t DUAL_PERC;

extern const triple_patch_t TRIPLE_ORCH_STRINGS; extern const triple_patch_t TRIPLE_PIPE_ORGAN;
extern const triple_patch_t TRIPLE_RICH_LEAD;    extern const triple_patch_t TRIPLE_DEEP_BASS;
extern const triple_patch_t TRIPLE_DRUM_KIT;     extern const triple_patch_t TRIPLE_SYNTH_TEX;

// 6. 기타 미디 관련 상수
//#define CHORD_CHORD
typedef struct { int dummy; } midi_perf_t;

// 7. 리눅스 시스템 함수 더미 선언
inline int clock_nanosleep(int clk_id, int flags, const struct timespec* req, struct timespec* rem) { return 0; }
inline int sched_setscheduler(int pid, int policy, const struct sched_param* param) { return 0; }
inline int open(const char* path, int flags, ...) { return 0; }
inline int close(int fd) { return 0; }
inline int read(int fd, void* buf, size_t count) { return 0; }
inline int poll(struct pollfd* fds, unsigned long nfds, int timeout) { return 0; }
inline int ioctl(int fd, unsigned long request, ...) { return 0; }
inline void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off) { return (void*)0; }
inline void perror(const char* s) {}
inline unsigned int usleep(unsigned int useconds) { return 0; }

// 8. MSVC 경고 억제
#pragma warning(disable:4996) // 보안 경고
#pragma warning(disable:4013) // 정의되지 않은 함수 호출
#pragma warning(disable:4047) // 포인터 간접 참조 수준 다름
#pragma warning(disable:4244) // 데이터 손실 가능성
#endif





#include "vgm_data_1.h"
#include "ym2203_multi.h"   /* full_inst_t, voice_mgr_t, key_detector_t,
                               ym_patch_vol(), fi_note_on_v(), vm_note_on(),
                               vm_note_off(), vm_cc(), vm_tick_full(),
                               kd_ji_note_ex(), vm_init_ji(), vm_panic_off(),
                               midi_perf_t, nonlinear_lfo_t, counterpoint_t,
                               mp_init(), mp_cc(), mp_pitch_bend(),
                               nlfo_init(), nlfo_tick_r(), ym_fnum_cents_blk(),
                               ym_set_fnum_raw(), ym_set_note_ex()           */
#include "ym2203_patches.h"

                               /* ── 물리 주소맵 ── */
#define ADDR_DELAY    0x40000000u
#define ADDR_EQ       0x40001000u
#define ADDR_LFO      0x40002000u
#define ADDR_VCA      0x40003000u
#define ADDR_YM2203   0x41200000u
#define ADDR_LCD_CTL  0x41210000u
#define ADDR_FIFO_SG  0x41220000u
#define ADDR_EF_IN    0x41230000u

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
#define SW_SELECT   3
#define SW_EXIT     4
#define SW_CANCEL   5

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
#define MS_TO_SAMPLES_Q8(ms) ((uint32_t)((double)(ms)*50.0*256.0))


ym2203_voice_t g_ym_voices[3];

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
typedef struct { uint32_t b0, b1, b2, a1, a2; } BiquadCoef;

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
    if (fgets(buf, sizeof(buf), f)) sz = (size_t)strtoull(buf, NULL, 0);
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
    if (system("modprobe uio_pdrv_genirq") != 0) {
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
#define DLY_TIME_OFF      0x00u
#define DLY_LFO_DEPTH_OFF 0x04u
#define DLY_FB_OFF        0x08u
#define DLY_LPF_OFF       0x0Cu
#define DLY_HPF_OFF       0x10u
#define DLY_DRYWET_OFF    0x14u
#define DLY_SAT_OFF       0x18u
#define DLY_DIFF_OFF      0x1Cu
#define DLY_FBSAT_OFF     0x20u
#define DLY_SLEW_OFF      0x24u
#define DLY_MODE_OFF      0x28u
#define DLY_COLOR_OFF     0x30u
#define DLY_TAP0_R_OFF    0x34u
#define DLY_TAP1_R_OFF    0x38u
#define DLY_TAP2_R_OFF    0x3Cu
#define DLY_TAP0_G_OFF    0x40u
#define DLY_TAP1_G_OFF    0x44u
#define DLY_TAP2_G_OFF    0x48u

/* EQ */
#define EQ_REG_OFF(b,c) (((uint32_t)((b)*8+(c)))*4u)

/* VCA */
#define VCA_THRESH_OFF  0x00u
#define VCA_KNEE_OFF    0x04u
#define VCA_KCOEFF_OFF  0x08u
#define VCA_MAKEUP_OFF  0x0Cu
#define VCA_ATK_OFF     0x10u
#define VCA_REL_OFF     0x14u

/* LFO */
#define LFO_OFF(ch,reg)  (((uint32_t)(ch)*16u+(uint32_t)(reg)*4u))
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
static uio_dev_t uio_delay, uio_eq, uio_lfo, uio_vca;
static uio_dev_t uio_ym2203, uio_fifo_sg, uio_ef_in, uio_lcd_ctrl;

static volatile uint8_t* p_delay = NULL;
static volatile uint8_t* p_eq = NULL;
static volatile uint8_t* p_lfo = NULL;
static volatile uint8_t* p_vca = NULL;
static volatile uint8_t* p_ym = NULL;
static volatile uint8_t* p_fifosg = NULL;
static volatile const uint8_t* p_efin = NULL;
static volatile uint8_t* p_lcd = NULL;

static int fd_lcd = -1;
static int fd_esp = -1;

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 B2]  YM2203 / ym 드라이버
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
static void eq_apply(const BiquadCoef p[6], const char* name)
{
    if (!p_eq) return;
    for (int b = 0; b < 6; b++) {
        reg_wr(p_eq, EQ_REG_OFF(b, 0), p[b].b0 & 0x00FFFFFFu);
        reg_wr(p_eq, EQ_REG_OFF(b, 1), p[b].b1 & 0x00FFFFFFu);
        reg_wr(p_eq, EQ_REG_OFF(b, 2), p[b].b2 & 0x00FFFFFFu);
        reg_wr(p_eq, EQ_REG_OFF(b, 3), p[b].a1 & 0x00FFFFFFu);
        reg_wr(p_eq, EQ_REG_OFF(b, 4), p[b].a2 & 0x00FFFFFFu);
    }
    printf("[EQ] %s\n", name);
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 B4]  VCA 드라이버
// ─────────────────────────────────────────────────────────────────────────────
static void vca_write(uint16_t thresh, uint16_t knee, uint16_t k_coeff,
    uint16_t makeup, uint8_t atk, uint8_t rel)
{
    if (!p_vca) return;
    reg_wr(p_vca, VCA_THRESH_OFF, thresh);
    reg_wr(p_vca, VCA_KNEE_OFF, knee);
    reg_wr(p_vca, VCA_KCOEFF_OFF, k_coeff);
    reg_wr(p_vca, VCA_MAKEUP_OFF, makeup);
    reg_wr(p_vca, VCA_ATK_OFF, atk);
    reg_wr(p_vca, VCA_REL_OFF, rel);
}

static void vca_set_makeup_checked(uint16_t new_mk)
{
    static uint16_t last = 0xFFFFu;
    if (!p_vca) return;
    uint16_t diff = (new_mk > last) ? (new_mk - last) : (last - new_mk);
    if (diff < 32u) return;
    last = new_mk;
    reg_wr(p_vca, VCA_MAKEUP_OFF, new_mk);
}

/* makeup=Q15_ONE(0dB) 명시 → 소리 보장 */
static void vca_bypass(void)
{
    vca_write(0xFFFF, 0, 0, Q15_ONE, 4, 8); printf("[VCA] BYPASS\n");
}

static void vca_soft(void)
{
    vca_write(3584, 0, 16384, Q15_ONE, 4, 8); printf("[VCA] SOFT 2:1\n");
}

static void vca_final(void)
{
    vca_write(3584, 0, 16384, 0xB505u, 4, 8); printf("[VCA] FINAL +3dB\n");
}

static void vca_drain(void)
{
    if (!p_vca) return;
    reg_wr(p_vca, VCA_THRESH_OFF, 0x0000u);
    reg_wr(p_vca, VCA_KCOEFF_OFF, 0x7FFFu);
    reg_wr(p_vca, VCA_MAKEUP_OFF, 0x0000u);
    reg_wr(p_vca, VCA_ATK_OFF, 1u);
    reg_wr(p_vca, VCA_REL_OFF, 1u);
    usleep(80000);
    printf("[VCA] drained\n");
}

static void vca_readback(void)
{
    if (!p_vca) return;
    printf("[VCA] thresh=0x%04X k=0x%04X makeup=0x%04X atk=%u rel=%u\n",
        reg_rd(p_vca, VCA_THRESH_OFF) & 0xFFFF,
        reg_rd(p_vca, VCA_KCOEFF_OFF) & 0xFFFF,
        reg_rd(p_vca, VCA_MAKEUP_OFF) & 0xFFFF,
        reg_rd(p_vca, VCA_ATK_OFF),
        reg_rd(p_vca, VCA_REL_OFF));
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 B5]  DELAY 드라이버
// ─────────────────────────────────────────────────────────────────────────────
#define DLY_COMMON_TAPS  0x4000u,0x2AABu,0x2000u,0x4CCCu,0x3333u,0x2666u

static void delay_write_all(uint32_t time, uint16_t lfo_dep, uint16_t fb,
    uint16_t lpf, uint16_t hpf, uint16_t dw, uint16_t sat, uint16_t diff,
    uint16_t fb_sat, uint16_t slew, uint8_t mode, uint8_t color,
    uint16_t tr0, uint16_t tr1, uint16_t tr2,
    uint16_t tg0, uint16_t tg1, uint16_t tg2)
{
    if (!p_delay) return;
    reg_wr(p_delay, DLY_TIME_OFF, time);
    reg_wr(p_delay, DLY_LFO_DEPTH_OFF, lfo_dep);
    reg_wr(p_delay, DLY_FB_OFF, fb);
    reg_wr(p_delay, DLY_LPF_OFF, lpf);
    reg_wr(p_delay, DLY_HPF_OFF, hpf);
    reg_wr(p_delay, DLY_DRYWET_OFF, dw);
    reg_wr(p_delay, DLY_SAT_OFF, sat);
    reg_wr(p_delay, DLY_DIFF_OFF, diff);
    reg_wr(p_delay, DLY_FBSAT_OFF, fb_sat);
    reg_wr(p_delay, DLY_SLEW_OFF, slew);
    reg_wr(p_delay, DLY_MODE_OFF, mode);
    reg_wr(p_delay, DLY_COLOR_OFF, color);
    reg_wr(p_delay, DLY_TAP0_R_OFF, tr0); reg_wr(p_delay, DLY_TAP1_R_OFF, tr1);
    reg_wr(p_delay, DLY_TAP2_R_OFF, tr2); reg_wr(p_delay, DLY_TAP0_G_OFF, tg0);
    reg_wr(p_delay, DLY_TAP1_G_OFF, tg1); reg_wr(p_delay, DLY_TAP2_G_OFF, tg2);
}

/*
 * !! 중요: delay_bypass_pass()는 DRY_WET=0x3000 (소리 통과)
 *    delay_bypass()는 DRY_WET=0x0000 → 완전 무음이므로 사용 금지!
 */
static void delay_bypass_pass(void)
{
    delay_write_all(MS_TO_SAMPLES_Q8(1), 0x0180u, 0x0000u,
        0x0C00u, 0x7800u, 0x3000u, 22000, 0x6000u, 28000, 0x0010u, 0, 0,
        DLY_COMMON_TAPS);
    printf("[DELAY] BYPASS(pass-through, dw=0x3000)\n");
}

static void delay_slapback(void)
{
    delay_write_all(MS_TO_SAMPLES_Q8(80), 0x0180u, 0x0800u,
        0x5000u, 0x7800u, 0x3000u, 24000, 0x5000u, 28000, 0x0010u, 0, 0,
        DLY_COMMON_TAPS);
    printf("[DELAY] SLAPBACK\n");
}

static void delay_full(void)
{
    delay_write_all(MS_TO_SAMPLES_Q8(200), 0x0180u, 0x3333u,
        0x1800u, 0x7800u, 0x2CCCu, 24000, 0x6000u, 28000, 0x0010u, 0, 0,
        DLY_COMMON_TAPS);
    printf("[DELAY] FULL\n");
}

static void delay_mute(void)
{
    if (!p_delay) return;
    reg_wr(p_delay, DLY_DRYWET_OFF, 0u);
    reg_wr(p_delay, DLY_FB_OFF, 0u);
    usleep(500000);
}

static void delay_readback(void)
{
    if (!p_delay) return;
    printf("[DELAY] time=%.1fms fb=0x%04X dw=0x%04X\n",
        (double)reg_rd(p_delay, DLY_TIME_OFF) / (50.0 * 256.0),
        reg_rd(p_delay, DLY_FB_OFF) & 0xFFFF,
        reg_rd(p_delay, DLY_DRYWET_OFF) & 0xFFFF);
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 B6]  LFO 드라이버
// ─────────────────────────────────────────────────────────────────────────────
static void lfo_set_ch(int ch, uint32_t fcw, uint16_t depth,
    uint32_t poff, uint8_t wave)
{
    if (!p_lfo || ch < 0 || ch > 7) return;
    reg_wr(p_lfo, LFO_OFF(ch, 0), fcw);
    reg_wr(p_lfo, LFO_OFF(ch, 1), (uint32_t)depth);
    reg_wr(p_lfo, LFO_OFF(ch, 2), poff);
    reg_wr(p_lfo, LFO_OFF(ch, 3), (uint32_t)(wave & 0x3u));
}

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
    if (!p_lfo) return;
    reg_wr(p_lfo, LFO_OFF(0, 0), 0x00000001u);
    reg_wr(p_lfo, LFO_OFF(0, 1), 0u);
    reg_wr(p_lfo, LFO_OFF(0, 2), 0u);
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
    uint8_t line[LCD_W * 2];
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
    uint8_t line[LCD_W * 2];
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
    uint8_t buf[80]; int p = 0;
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
//    eq_preset    INTEGER, vca_preset INTEGER,
//    dly_preset   INTEGER, lfo_preset INTEGER,
//    ch_mode      INTEGER, unison_inst INTEGER,
//    vm_on        INTEGER, ji_on  INTEGER,
//    cp_on        INTEGER, kd_on  INTEGER,
//    kd_min_notes INTEGER, pb_semi INTEGER,
//    swing_ratio  INTEGER, scale_type INTEGER, key_root INTEGER
//  );
// ─────────────────────────────────────────────────────────────────────────────

typedef enum { CH_MODE_INDEPENDENT = 0, CH_MODE_UNISON = 1 } ym_ch_mode_t;

/* FM 채널별 설정 */
typedef struct {
    uint8_t         inst_idx;       /* FULL_INST_CATALOG 인덱스 (0~N-1)      */
    uint8_t         use_full;       /* 1=full_inst_t 사용  0=raw patch 직접   */
    uint8_t         edited;         /* 1=OP 세밀편집 결과 사용                */
    uint8_t         vol;            /* 0~16                                  */
    ym2203_patch_t  patch_override; /* edited=1 또는 use_full=0 시 실제 패치 */
    /* SQLite: fm{N}_inst, fm{N}_edited, fm{N}_patch(BLOB) */
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

/* 완전 설정 — 전역 단일 진실 소스 */
typedef struct {
    fm_ch_cfg_t   fm[3];
    psg_ch_cfg_t  psg[3];
    uint8_t       eq_preset;
    uint8_t       vca_preset;
    uint8_t       dly_preset;
    uint8_t       lfo_preset;
    harmony_cfg_t harmony;
    uint8_t       ch_mode;          /* CH_MODE_INDEPENDENT / CH_MODE_UNISON  */
    uint8_t       unison_inst_idx;
    /* SQLite 확장용 예약 필드 (현재 미사용) */
    char          preset_name[32];  /* 향후 SQLite name 컬럼                 */
    uint32_t      created_at;       /* 향후 Unix timestamp                   */
} full_preset_t;

static full_preset_t g_full_preset;

/* 화성학 엔진 런타임 상태 — g_full_preset.harmony 설정을 참조 */
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
static const BiquadCoef EQ_BYPASS[6] = {
    {Q22_ONE,0,0,0,0},{Q22_ONE,0,0,0,0},{Q22_ONE,0,0,0,0},
    {Q22_ONE,0,0,0,0},{Q22_ONE,0,0,0,0},{Q22_ONE,0,0,0,0},
};
static const BiquadCoef EQ_TELEPHONE[6] = {
    {0x3EC9EBu,0x826C2Au,0x3EC9EBu,0x827590u,0x3D9D3Cu},
    {0x3DFC8Eu,0x8406E5u,0x3DFC8Eu,0x8420ADu,0x3C12E4u},
    {0x435937u,0x859569u,0x3838D1u,0x859569u,0x3B9208u},
    {0x44B38Au,0x8DDB05u,0x31D9EDu,0x8DDB05u,0x368D77u},
    {0x01F614u,0x03EC28u,0x01F614u,0x9E0E29u,0x29CA27u},
    {0x016043u,0x02C087u,0x016043u,0x9880FAu,0x2D0013u},
};

#define EQ_PRESET_COUNT  2
static const char* const EQ_PRESET_NAMES[EQ_PRESET_COUNT] = { "BYPASS", "TELEPHONE" };
static const BiquadCoef* const EQ_PRESET_DATA[EQ_PRESET_COUNT] = {
    EQ_BYPASS, EQ_TELEPHONE
};

#define VCA_PRESET_COUNT 3
static const char* const VCA_PRESET_NAMES[VCA_PRESET_COUNT] = { "BYPASS","SOFT 2:1","FINAL+3dB" };
typedef void (*VoidFn)(void);
static void vca_bypass_wrap(void) { vca_bypass(); }
static void vca_soft_wrap(void) { vca_soft(); }
static void vca_final_wrap(void) { vca_final(); }
static const VoidFn VCA_PRESET_FN[VCA_PRESET_COUNT] = {
    vca_bypass_wrap, vca_soft_wrap, vca_final_wrap
};

#define DLY_PRESET_COUNT 3
static const char* const DLY_PRESET_NAMES[DLY_PRESET_COUNT] = { "BYPASS","SLAPBACK","FULL DLY" };
static void dly_bypass_wrap(void) { delay_bypass_pass(); }
static void dly_slapback_wrap(void) { delay_slapback(); }
static void dly_full_wrap(void) { delay_full(); }
static const VoidFn DLY_PRESET_FN[DLY_PRESET_COUNT] = {
    dly_bypass_wrap, dly_slapback_wrap, dly_full_wrap
};

#define LFO_PRESET_COUNT 3
static const char* const LFO_PRESET_NAMES[LFO_PRESET_COUNT] = { "OFF","SLOW 0.1Hz","FAST 10Hz" };

static void apply_eq_preset(uint8_t idx)
{
    if (idx >= EQ_PRESET_COUNT) idx = 0;
    eq_apply(EQ_PRESET_DATA[idx], EQ_PRESET_NAMES[idx]);
}
static void apply_vca_preset(uint8_t idx)
{
    if (idx >= VCA_PRESET_COUNT) idx = 1;
    VCA_PRESET_FN[idx]();
}
static void apply_dly_preset(uint8_t idx)
{
    if (idx >= DLY_PRESET_COUNT) idx = 1;
    DLY_PRESET_FN[idx]();
}
static void apply_lfo_preset(uint8_t idx)
{
    switch (idx) {
    case 1: lfo_set_ch(0, LFO_FCW_0_1HZ, 0x0180u, 0u, 0u); break;
    case 2: lfo_set_ch(0, LFO_FCW_10HZ, 0x0180u, 0u, 0u); break;
    default: lfo_stop_all(); break;
    }
    printf("[LFO] %s\n", (idx < LFO_PRESET_COUNT) ? LFO_PRESET_NAMES[idx] : "OFF");
}

/* 이펙터 전체 초기화 — 소리 보장 (모든 모드 진입 시 공통) */
static void fx_init_sound_guaranteed(void)
{
    apply_eq_preset(0);   /* EQ BYPASS  */
    apply_vca_preset(1);  /* VCA SOFT   */
    apply_dly_preset(1);  /* SLAPBACK   — DRY_WET=0x3000 */
    lfo_stop_all();
    vca_readback();
    delay_readback();
}

// ─────────────────────────────────────────────────────────────────────────────
//  [섹션 D3]  YM2203 보이스 관리  (m3_full_* — SYNTH/MIDI 공용)
//
//  [V14-D3] v13의 ym_synth_* + global_preset_t 기반 발음 로직을
//           full_preset_t + harmony_cfg_t 기반 m3_full_* 로 전면 교체.
//           voice_mgr_on / ji_on / nlfo_preset 경로 분기 포함.
// ─────────────────────────────────────────────────────────────────────────────

/* §P2  full_inst_t 카탈로그 */
static const full_inst_t* const FULL_INST_CATALOG[] = {
    /* 국악기 §8 */
    &FULL_DAEGEUM,
    &FULL_SOGEUM,
    &FULL_PIRI,
    &FULL_GEOMUNGO,
    &FULL_AJAENG,
    &FULL_DANSO,
    &FULL_HUN,
    &FULL_GAYAGEUM,
    &FULL_HAEGEUM,
    /* §9 오케스트라 악기 */
    //&FULL_VIOLIN,
    //&FULL_CELLO,
    /* 추가 악기는 아래에 계속 등록:
    &FULL_TRUMPET,
    &FULL_FLUTE_ORCH,
    */
};
#define FULL_INST_COUNT  ((uint8_t)(sizeof(FULL_INST_CATALOG)/sizeof(FULL_INST_CATALOG[0])))
#define RAW_PATCH_COUNT  ((uint8_t)(YM2203_PATCH_COUNT))

/* §P3  기본값 초기화 */
static void full_preset_default(void)
{
    memset(&g_full_preset, 0, sizeof(g_full_preset));
    strncpy(g_full_preset.preset_name, "DEFAULT", sizeof(g_full_preset.preset_name) - 1);

    for (int i = 0; i < 3; i++) {
        g_full_preset.fm[i].inst_idx = (uint8_t)i; /* ch0=대금 ch1=소금 ch2=피리 */
        g_full_preset.fm[i].use_full = 1;
        g_full_preset.fm[i].edited = 0;
        g_full_preset.fm[i].vol = 15;

        g_full_preset.psg[i].enable = (i == 0) ? 1u : 0u;
        g_full_preset.psg[i].patch_idx = SSG_NOISE_SNARE;
        g_full_preset.psg[i].amp = 8;
        g_full_preset.psg[i].note_offset = 0;
    }

    g_full_preset.eq_preset = 0;
    g_full_preset.vca_preset = 1;   /* SOFT */
    g_full_preset.dly_preset = 1;   /* SLAPBACK */
    g_full_preset.lfo_preset = 0;
    g_full_preset.ch_mode = CH_MODE_INDEPENDENT;

    harmony_cfg_t* h = &g_full_preset.harmony;
    h->voice_mgr_on = 0;
    h->vm_chord_auto = 0;
    h->ji_on = 0;
    h->pb_semitones = 2;
    h->key_detect_on = 0;
    h->kd_min_notes = 4;
    h->key_root = 0;      /* C */
    h->scale_type = 0;      /* Major */
    h->counterpoint_on = 0;
    h->suspension_on = 0;
    h->vel_car_ratio = 6;
    h->vel_mod_ratio = 2;
    h->swing_ratio = 0;
    h->nlfo_preset = 0xFF;   /* OFF */
    h->nlfo_rand_range = 8;
    h->porta_mode = 0;      /* SLIDE */
    h->porta_ticks = 0;      /* OFF */
}

/* §P4  화성학 런타임 초기화 */
static void harmony_runtime_apply(void)
{
    harmony_cfg_t* h = &g_full_preset.harmony;

    if (h->voice_mgr_on) {
        if (h->ji_on && h->key_detect_on)
            vm_init_ji(&g_voice_mgr);
        else
            vm_init(&g_voice_mgr);
        g_voice_mgr.key_det.min_notes = h->kd_min_notes;
    }
    else {
        memset(&g_voice_mgr, 0, sizeof(g_voice_mgr));
    }

    mp_init(&g_midi_perf,
        h->vel_car_ratio, h->vel_mod_ratio,
        h->pb_semitones,
        0,          /* mw_ch */
        2, 7);      /* base_fb, max_fb */

    if (h->nlfo_preset < NLFO_PRESET_COUNT) {
        nlfo_init(&g_nlfo, (nlfo_preset_t)h->nlfo_preset);
    }
    else {
        memset(&g_nlfo, 0, sizeof(g_nlfo));
        g_nlfo.active = 0;
    }

    memset(&g_cp, 0, sizeof(g_cp));

    printf("[HARMONY] vm=%d ji=%d kd=%d cp=%d nlfo=%d\n",
        h->voice_mgr_on, h->ji_on, h->key_detect_on,
        h->counterpoint_on, (int)h->nlfo_preset);
}

/* §P5  HW 적용 — g_full_preset → YM2203 레지스터 */
static void full_preset_apply_hw(void)
{
    if (!p_ym) return;
    ym2203_cache_reset();

    for (uint8_t ch = 0; ch < 3; ch++) {
        fm_ch_cfg_t* fc = &g_full_preset.fm[ch];
        ym2203_key_off(ch);

        const ym2203_patch_t* p = NULL;
        if (fc->edited || !fc->use_full) {
            p = &fc->patch_override;
        }
        else {
            const full_inst_t* fi = FULL_INST_CATALOG[
                (fc->inst_idx < FULL_INST_COUNT) ? fc->inst_idx : 0];
            if (fi->fm_n > 0) p = &fi->fm[0];
        }
        if (p) ym2203_patch_apply(p, ch);
    }

    for (uint8_t ch = 0; ch < 3; ch++) {
        psg_ch_cfg_t* pc = &g_full_preset.psg[ch];
        if (!pc->enable) { ym2203_ssg_set_vol(ch, 0); continue; }
        ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx],
            (uint8_t)(1u << ch));
        ym2203_ssg_set_vol(ch, pc->amp);
    }

    harmony_runtime_apply();
}

/* ── 보이스 상태 ── */
static uint8_t s_m3_rr = 0;
static uint8_t s_m3_note_ch[128];
static uint8_t s_m3_ch_note[3];

static void m3_full_voice_reset(void)
{
    memset(s_m3_note_ch, 0xFF, sizeof(s_m3_note_ch));
    memset(s_m3_ch_note, 0xFF, sizeof(s_m3_ch_note));
    s_m3_rr = 0;
    g_nlfo_note = 0;
    if (g_full_preset.harmony.voice_mgr_on)
        vm_panic_off(&g_voice_mgr);
}

/* ── Note-On 발음 경로 선택 ── */
/* ── Note-On 발음 경로 (12-Op 독립 채널 합성 모드) ── */
static void m3_full_note_on(uint8_t note, uint8_t vel)
{
    //if (note > 127) return;
    harmony_cfg_t* h = &g_full_preset.harmony;

    /* 농음 추적 */
    g_nlfo_note = note;
    if (h->nlfo_preset < NLFO_PRESET_COUNT)
        nlfo_init(&g_nlfo, (nlfo_preset_t)h->nlfo_preset);

    /* ─ 경로 A: voice_mgr_on=1 (동적 할당 모드 - 기존 유지) ─ */
    if (h->voice_mgr_on) {
        uint8_t ch = (uint8_t)(s_m3_rr % 3u);
        fm_ch_cfg_t* fc = &g_full_preset.fm[ch];
        const ym2203_patch_t* p = (fc->edited || !fc->use_full)
            ? &fc->patch_override
            : &FULL_INST_CATALOG[(fc->inst_idx < FULL_INST_COUNT) ? fc->inst_idx : 0]->fm[0];
        vm_note_on(&g_voice_mgr, note, vel, p);

        if (h->ji_on && g_voice_mgr.key_det.valid) {
            for (int i = 0; i < VM_FM_CH; i++) {
                if (g_voice_mgr.voices[i].on && g_voice_mgr.voices[i].note == note) {
                    kd_ji_note_ex((uint8_t)i, note, &g_voice_mgr.key_det, g_midi_perf.pb_cent);
                    break;
                }
            }
        }
        if (note < 128) { s_m3_note_ch[note] = 0; s_m3_rr++; }
        return;
    }

    // 경로 B: 채널별 독립 발음
    ym_write_force_cached(0x27, 0x00);
    for (uint8_t ch = 0; ch < 3; ch++) {
        fm_ch_cfg_t* fc = &g_full_preset.fm[ch];
        const ym2203_patch_t* p = fc->edited
            ? &fc->patch_override
            : &FULL_INST_CATALOG[(fc->inst_idx < FULL_INST_COUNT)
            ? fc->inst_idx : 0]->fm[0];

        ym2203_key_off(ch);
        ym2203_patch_apply(p, ch);   // ← 각 채널에 패치 독립 적용

        // vel_car/mod_ratio가 설정된 경우 split 버전 사용
        if (h->vel_car_ratio > 0 || h->vel_mod_ratio > 0) {
            ym_patch_vol_split(p, ch, vel, fc->vol,
                h->vel_car_ratio, h->vel_mod_ratio);
        }
        else {
            ym_patch_vol(p, ch, vel, fc->vol);
        }

        ym2203_set_note(ch, note);
        ym2203_key_on(ch, 0x0Fu);

        // PSG는 ch==0에서만 (기존 로직 유지)
        if (ch == 0 && fc->use_full && !fc->edited) {
            const full_inst_t* fi = FULL_INST_CATALOG[fc->inst_idx];
            for (int pch = 0; pch < 3; pch++) {
                if (!(fi->ssg_mask & (1u << pch))) continue;
                psg_ch_cfg_t* pc = &g_full_preset.psg[pch];
                ym2203_ssg_set_vol((uint8_t)pch, pc->enable
                    ? (uint8_t)((uint32_t)pc->amp * ym_ssg_vol_from_vel(vel) / 15u)
                    : 0);
            }
        }

        s_m3_ch_note[ch] = note;
        // JI 보정 유지
        if (h->ji_on && h->key_detect_on) {
            if (ch == 0) kd_note_on(&g_voice_mgr.key_det, note, vel);
            if (g_voice_mgr.key_det.valid)
                kd_ji_note_ex(ch, note, &g_voice_mgr.key_det, g_midi_perf.pb_cent);
        }
    }
    s_m3_note_ch[note] = 0;
}


/* ── Note-Off (동시 발음된 3개 채널 동시 끄기) ── */
static void m3_full_note_off(uint8_t note)
{
    if (note > 127) return;
    harmony_cfg_t* h = &g_full_preset.harmony;

    if (h->voice_mgr_on) {
        vm_note_off(&g_voice_mgr, note);
        return;
    }

    // 0, 1, 2번 채널을 모두 돌면서 방금 뗀 건반(note)의 소리를 끕니다.
    for (uint8_t ch = 0; ch < 3; ch++) {
        if (s_m3_ch_note[ch] == note) {
            ym2203_key_off(ch);
            s_m3_ch_note[ch] = 0xFF;

            if (g_full_preset.fm[ch].use_full && !g_full_preset.fm[ch].edited) {
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

/* CC 처리 — voice_mgr / midi_perf 양쪽 전달 */
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

/* 매 10ms 틱 — 농음 LFO + voice_mgr age + FM LFO */
static void m3_full_tick(void)
{
    harmony_cfg_t* h = &g_full_preset.harmony;

    if (h->voice_mgr_on)
        vm_tick_full(&g_voice_mgr);

    if (g_nlfo.active && h->nlfo_preset < NLFO_PRESET_COUNT) {
        int16_t cent = nlfo_tick_r(&g_nlfo, h->nlfo_rand_range);
        uint8_t note = g_nlfo_note;
        if (note < 128) {
            uint8_t ch = s_m3_note_ch[note];
            if (ch < 3) {
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
}

static void m3_full_all_off(void)
{
    if (g_full_preset.harmony.voice_mgr_on)
        vm_panic_off(&g_voice_mgr);
    else {
        for (int i = 0; i < 3; i++) ym2203_key_off((uint8_t)i);
    }
    for (int i = 0; i < 3; i++) ym2203_ssg_set_vol((uint8_t)i, 0);
    m3_full_voice_reset();
}

// =============================================================================
//  [섹션 E]  모드 0: MENU SELECT
// =============================================================================
#define MENU_COUNT 4
static const char* const MENU_NAMES[MENU_COUNT] = { "1.SYNTH/MIDI","2.VGM PLAYER","3.MIDI KEYBOARD","4.INST SETUP" };
static const uint16_t       MENU_COLORS[MENU_COUNT] = { COLOR_CYAN,COLOR_MAGENTA,COLOR_ORANGE,0x4A10u };
static const uint8_t        MENU_TO_MODE[MENU_COUNT] = { ZYNQ_MODE_SYNTH,ZYNQ_MODE_VGM,ZYNQ_MODE_MIDIVIS,ZYNQ_MODE_INSTSETUP };
static int menu_idx = 0;

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
    lcd_string(4, 228, "JOY-Y:이동  SW3:선택", COLOR_DKGREY, COLOR_BLACK);
}

static void mode_menu_enter(void)
{
    mode_menu_draw();
}

static int mode_menu_update(spi_packet_t* rx, spi_packet_t* tx)
{
    static uint8_t joy_moved = 0, last_sw = 0;
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    int prev = menu_idx;

    if (jy < JOY_LOW && !joy_moved && menu_idx > 0) { menu_idx--; joy_moved = 1; }
    else if (jy > JOY_HIGH && !joy_moved && menu_idx < MENU_COUNT - 1) { menu_idx++; joy_moved = 1; }
    else if (jy >= JOY_DEAD_LO && jy <= JOY_DEAD_HI) { joy_moved = 0; }

    if (prev != menu_idx) {
        lcd_string(10, MENU_ROW_Y(prev), "  ", COLOR_WHITE, COLOR_BLACK);
        lcd_string(28, MENU_ROW_Y(prev), MENU_NAMES[prev], COLOR_GREY, COLOR_BLACK);
        lcd_string(10, MENU_ROW_Y(menu_idx), "->", COLOR_WHITE, COLOR_BLACK);
        lcd_string(28, MENU_ROW_Y(menu_idx), MENU_NAMES[menu_idx], MENU_COLORS[menu_idx], COLOR_BLACK);
    }

    tx->active_mode = ZYNQ_MODE_MENU;
    int next = ZYNQ_MODE_MENU;
    if (rx->sw_status == SW_SELECT && last_sw != SW_SELECT)
        next = (int)MENU_TO_MODE[menu_idx];
    last_sw = rx->sw_status;
    return next;
}

static void mode_menu_cleanup(void) { /* 메뉴 상태 유지 */ }

// =============================================================================
//  [섹션 F]  모드 1: SYNTH/MIDI
//  [V14] 발음은 m3_full_note_on/off 사용, 프리셋은 g_full_preset 참조
// =============================================================================
typedef struct {
    uint8_t  note_on;
    uint8_t  last_note;
    uint32_t base_dly;
    uint32_t vel_q15;
    uint8_t  last_midi_st, last_midi_nt, last_midi_vl;
} synth_state_t;
static synth_state_t sst;

static void mode_synth_enter(void)
{
    fx_init_sound_guaranteed();
    apply_eq_preset(0);
    apply_dly_preset(1);
    memset(&sst, 0, sizeof(sst));
    sst.base_dly = MS_TO_SAMPLES_Q8(100);
    sst.vel_q15 = Q15_ONE;
    full_preset_apply_hw();
    m3_full_voice_reset();

    lcd_clear(COLOR_BLACK);
    lcd_draw_header(COLOR_CYAN, "  SYNTH MODE (mode=1)", COLOR_WHITE);
    char buf[54];
    /* g_full_preset.fm[ch] 기반으로 악기명 표시 */
    for (int i = 0; i < 3; i++) {
        fm_ch_cfg_t* fc = &g_full_preset.fm[i];
        const char* nm = (fc->use_full && !fc->edited && fc->inst_idx < FULL_INST_COUNT)
            ? FULL_INST_CATALOG[fc->inst_idx]->name
            : (fc->inst_idx < YM2203_PATCH_COUNT ? YM2203_PATCHES[fc->inst_idx].name : "???");
        snprintf(buf, sizeof(buf), "ch%d: %s%s", i, nm, fc->edited ? "[E]" : "");
        lcd_string(4, (uint16_t)(22 + i * 10), buf,
            (i == 0) ? COLOR_CYAN : (i == 1) ? COLOR_GREEN : COLOR_YELLOW, COLOR_BLACK);
    }
    lcd_string(4, 228, "SW4:MENU", COLOR_RED, COLOR_BLACK);
}

static int mode_synth_update(spi_packet_t* rx, spi_packet_t* tx)
{
    static uint8_t lsw = 0;

    if (rx->sw_status == SW_EXIT && lsw != SW_EXIT) {
        lsw = rx->sw_status;
        tx->active_mode = ZYNQ_MODE_SYNTH;
        return ZYNQ_MODE_MENU;
    }
    lsw = rx->sw_status;
    tx->active_mode = ZYNQ_MODE_SYNTH;

    if (sst.note_on) {
        lfo_set_fcw_checked(0, adc_to_lfo_fcw(adc_buf[ADC_IDX_PITCH]));
        if (p_vca) {
            uint32_t vq = (uint32_t)adc_buf[ADC_IDX_VOL] * Q15_ONE / ADC_MAX;
            vca_set_makeup_checked((uint16_t)((sst.vel_q15 * vq) >> 15));
        }
        m3_full_tick();
    }

    if (rx->midi_status != 0) {
        uint8_t st = rx->midi_status, nt = rx->midi_note, vl = rx->midi_vel;
        if (!(st == sst.last_midi_st && nt == sst.last_midi_nt && vl == sst.last_midi_vl)) {
            sst.last_midi_st = st; sst.last_midi_nt = nt; sst.last_midi_vl = vl;
            uint8_t mt = st & 0xF0u;
            if (mt == 0x90u && vl > 0u) {
                sst.note_on = 1; sst.last_note = nt;
                sst.vel_q15 = (uint32_t)vl * Q15_ONE / 127u;
                uint32_t vq = (uint32_t)adc_buf[ADC_IDX_VOL] * Q15_ONE / ADC_MAX;
                vca_write(3584, 0, 16384, (uint16_t)((sst.vel_q15 * vq) >> 15), 4, 8);
                lfo_set_ch(0, adc_to_lfo_fcw(adc_buf[ADC_IDX_PITCH]), 0x0180u, 0u, 0u);
                m3_full_note_on(nt, vl);
                char buf[32]; snprintf(buf, sizeof(buf), "ON nt=%-3d vl=%-3d", nt, vl);
                lcd_fill_rect(4, 56, 250, 10, COLOR_BLACK);
                lcd_string(4, 56, buf, COLOR_GREEN, COLOR_BLACK);
            }
            else if (mt == 0x80u || (mt == 0x90u && vl == 0u)) {
                m3_full_note_off(nt);
                sst.note_on = 0; sst.vel_q15 = Q15_ONE;
                vca_write(3584, 0, 16384, 0x0000u, 4, 8);
                lfo_stop_ch(0);
                if (p_delay) reg_wr(p_delay, DLY_TIME_OFF, sst.base_dly);
                lcd_fill_rect(4, 56, 250, 10, COLOR_BLACK);
                lcd_string(4, 56, "OFF", COLOR_GREY, COLOR_BLACK);
            }
            else if (mt == 0xB0u) {
                m3_full_cc(nt, vl);
                if (nt == 1u) {
                    uint16_t d = (uint16_t)((uint32_t)vl * 0x01FFu / 127u);
                    lfo_set_ch(0, adc_to_lfo_fcw(adc_buf[ADC_IDX_PITCH]), d, 0u, 0u);
                }
                else if (nt == 7u) {
                    vca_set_makeup_checked((uint16_t)((uint32_t)vl * Q15_ONE / 127u));
                }
            }
            else if (mt == 0xE0u) {
                int16_t pb_raw = (int16_t)(((uint32_t)nt | ((uint32_t)vl << 7)) - 8192);
                m3_full_pitch_bend(pb_raw);
                /* delay time modulation (V13 동작 보존) */
                double ms = 100.0 + (double)pb_raw * 100.0 / 8192.0;
                if (ms < 10.0) ms = 10.0; if (ms > 600.0) ms = 600.0;
                if (p_delay) reg_wr(p_delay, DLY_TIME_OFF, MS_TO_SAMPLES_Q8(ms));
            }
        }
    }
    return ZYNQ_MODE_SYNTH;
}

static void mode_synth_cleanup(void)
{
    lfo_stop_all(); vca_drain(); m3_full_all_off();
}

// =============================================================================
//  [섹션 G]  모드 2: VGM PLAYER  (V13 그대로 보존)
// =============================================================================
static volatile int      g_vgm_play = 0;
static volatile int      g_vgm_exit = 0;
static volatile uint32_t g_vgm_pos = 0;

#define VGM_ROUND_COUNT 3
typedef struct { const char* label; const char* desc; uint8_t eq; uint8_t vca; uint8_t dly; } VgmRound;
static const VgmRound VGM_ROUNDS[VGM_ROUND_COUNT] = {
    {"R1:BYPASS",   "원음 확인",            0, 0, 0},
    {"R2:EQ+SOFT",  "EQ+소프트압축+슬랩백", 1, 1, 1},
    {"R3:EQ+FINAL", "EQ+FINAL VCA+DLY",    1, 2, 2},
};
static int vgm_round = 0, vgm_round_done = 0;

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

static void vgm_start_round(int r)
{
    printf("\n[VGM] ★ %s ★\n", VGM_ROUNDS[r].label);
    apply_eq_preset(VGM_ROUNDS[r].eq);
    apply_vca_preset(VGM_ROUNDS[r].vca);
    apply_dly_preset(VGM_ROUNDS[r].dly);
    vca_readback(); delay_readback();
    vgm_round_done = 0; g_vgm_pos = 0; MEM_BARRIER();
    g_vgm_play = 1; MEM_BARRIER();
}

static void mode_vgm_enter(void)
{
    if (!p_ym) { fprintf(stderr, "[VGM] p_ym=NULL\n"); return; }
    ym_gpio_init(); ym_reset(); lfo_init_default();
    lcd_clear(COLOR_BLACK);
    lcd_draw_header(COLOR_MAGENTA, "  VGM PLAYER (mode=2)", COLOR_WHITE);
    lcd_string(4, 228, "VOL:노브  SW4:종료", COLOR_RED, COLOR_BLACK);
    vgm_round = 0;
    char buf[48];
    snprintf(buf, sizeof(buf), "[1/%d] %s", VGM_ROUND_COUNT, VGM_ROUNDS[0].label);
    lcd_string(4, 26, buf, COLOR_GREEN, COLOR_BLACK);
    lcd_string(4, 42, VGM_ROUNDS[0].desc, COLOR_WHITE, COLOR_BLACK);
    lcd_string(4, 58, "Playing...", COLOR_CYAN, COLOR_BLACK);
    vgm_start_round(vgm_round);
}

static int mode_vgm_update(spi_packet_t* rx, spi_packet_t* tx)
{
    static uint8_t lsw = 0;
    if (rx->sw_status == SW_EXIT && lsw != SW_EXIT) {
        lsw = rx->sw_status; tx->active_mode = ZYNQ_MODE_VGM;
        return ZYNQ_MODE_MENU;
    }
    lsw = rx->sw_status; tx->active_mode = ZYNQ_MODE_VGM;
    static uint32_t ui_cnt = 0;
    if (++ui_cnt % 10 == 0) vgm_draw_progress();
    ym_set_ssg_volume_checked(adc_buf[ADC_IDX_VOL]);
    MEM_BARRIER();
    if (!g_vgm_play && !vgm_round_done) {
        vgm_round_done = 1; ym_silence();
        if (vgm_round < VGM_ROUND_COUNT - 1) {
            delay_mute(); vca_drain(); ym_reset(); vgm_round++;
            char buf[48];
            snprintf(buf, sizeof(buf), "[%d/%d] %s",
                vgm_round + 1, VGM_ROUND_COUNT, VGM_ROUNDS[vgm_round].label);
            lcd_fill_rect(0, 22, LCD_W, 70, COLOR_BLACK);
            lcd_string(4, 26, buf, COLOR_GREEN, COLOR_BLACK);
            lcd_string(4, 42, VGM_ROUNDS[vgm_round].desc, COLOR_WHITE, COLOR_BLACK);
            lcd_string(4, 58, "Playing...", COLOR_CYAN, COLOR_BLACK);
            vgm_start_round(vgm_round);
        }
        else {
            lcd_fill_rect(0, 22, LCD_W, 70, COLOR_BLACK);
            lcd_string(4, 26, "FINAL: EQ+VCA+DLY", COLOR_GREEN, COLOR_BLACK);
            lcd_string(4, 42, "완료. SW4:메뉴", COLOR_DKGREY, COLOR_BLACK);
            lcd_fill_rect(4, 74, 312, 6, COLOR_GREEN);
        }
    }
    return ZYNQ_MODE_VGM;
}

static void mode_vgm_cleanup(void)
{
    g_vgm_play = 0; MEM_BARRIER();
    ym_silence(); delay_mute(); vca_drain();
}

// =============================================================================
//  [섹션 H]  모드 3: USB MIDI KEYBOARD + YM2203
//
//  [V14-H] 발음 경로를 m3_full_* 계열로 전면 교체.
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

/* MIDI 건반 시각화 */
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

typedef struct {
    uint8_t note_on[128];
    uint8_t sustain;
    uint8_t pending_off[128];
    uint8_t poly_count;
    uint8_t last_note;
    uint8_t last_vel;
} m3_state_t;
static m3_state_t m3;

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
    }
    m3_draw_info();
}

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
        }
    }
    else if (mt == 0xE0u) {
        /* 피치벤드 */
        int16_t pb_raw = (int16_t)(((uint32_t)d1 | ((uint32_t)d2 << 7)) - 8192);
        m3_full_pitch_bend(pb_raw); /* [V14] 신규 */
    }
}

static void m3_state_reset(void)
{
    memset(&m3, 0, sizeof(m3));
    m3.last_note = 0xFF;
    s_midi_wptr = s_midi_rptr = 0;
    m3_full_voice_reset();          /* [V14] ym_voice_reset → m3_full_voice_reset */
}

static void mode_midivis_enter(void)
{
    printf("[M3] MIDI+SOUND 진입\n");
    m3_state_reset();
    fx_init_sound_guaranteed();
    full_preset_apply_hw();         /* [V14] preset_apply_hw → full_preset_apply_hw */

    if (m3_fd_midi >= 0) { close(m3_fd_midi); m3_fd_midi = -1; }
    m3_fd_midi = m3_open_midi();

    lcd_clear(COLOR_BLACK);
    lcd_draw_header(COLOR_ORANGE, "  MIDI KBD+SOUND (mode=3)", COLOR_BLACK);

    /* 악기명 표시 — g_full_preset.fm[] 기반 */
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
    lcd_string(2, 228, "SW4:MENU(long:RESCAN) VOL:음량", COLOR_DKGREY, COLOR_BLACK);
    if (m3_fd_midi < 0) lcd_string(2, 32, "NO MIDI - connect!", COLOR_RED, COLOR_BLACK);
}

static int mode_midivis_update(spi_packet_t* rx, spi_packet_t* tx)
{
    static uint8_t lsw = 0; static uint32_t sw4_hold = 0;
    tx->active_mode = ZYNQ_MODE_MIDIVIS;

    if (rx->sw_status == SW_EXIT) {
        sw4_hold++;
        if (sw4_hold == 50u) {
            m3_full_all_off();
            if (m3_fd_midi >= 0) { close(m3_fd_midi); m3_fd_midi = -1; }
            m3_state_reset();
            m3_fd_midi = m3_open_midi();
            for (int r = 0; r < M3_OCT_COUNT; r++) m3_draw_octave_row(r);
            m3_draw_info();
        }
    }
    else {
        if (lsw == SW_EXIT && sw4_hold < 50u) {
            sw4_hold = 0; lsw = rx->sw_status; return ZYNQ_MODE_MENU;
        }
        sw4_hold = 0;
    }
    lsw = rx->sw_status;

    { static uint16_t lv = 0xFFFFu; uint16_t vol = adc_buf[ADC_IDX_VOL];
    uint16_t d = (vol > lv) ? (vol - lv) : (lv - vol);
    if (d >= 64u) { lv = vol; vca_set_makeup_checked((uint16_t)((uint32_t)vol * Q15_ONE / ADC_MAX)); } }

    lfo_set_fcw_checked(0, adc_to_lfo_fcw(adc_buf[ADC_IDX_PITCH]));
    m3_full_tick();                 /* [V14] ym_synth_tick → m3_full_tick */

    midi_msg_t msg;
    while (midi_queue_pop(&msg)) m3_process_msg(msg.st, msg.d1, msg.d2);

    if (m3_fd_midi < 0) {
        static uint32_t rs = 0;
        if (++rs >= 100u) {
            rs = 0; m3_fd_midi = m3_open_midi();
            if (m3_fd_midi >= 0) {
                m3_state_reset();
                full_preset_apply_hw();     /* [V14] */
                fx_init_sound_guaranteed();
                for (int r = 0; r < M3_OCT_COUNT; r++) m3_draw_octave_row(r);
                m3_draw_info();
            }
        }
    }
    return ZYNQ_MODE_MIDIVIS;
}

static void mode_midivis_cleanup(void)
{
    m3_full_all_off(); ym_silence(); /* [V14] ym_synth_all_off → m3_full_all_off */
    lfo_stop_all(); vca_drain();
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

/* ── FSM 상태 ── */
typedef enum {
    M4_TOP = 0,
    M4_YM,
    M4_FM_INST,
    M4_FM_OP,
    M4_PSG_CH,
    M4_HARMONY,
} m4_state_t;

/* ── TOP 메뉴 인덱스 ── */
#define M4_T_YM        0
#define M4_T_HARMONY   1
#define M4_T_EQ        2
#define M4_T_VCA       3
#define M4_T_DELAY     4
#define M4_T_LFO       5
#define M4_T_APPLY     6
#define M4_T_EXIT      7
#define M4_T_COUNT     8

static const char* const M4_TOP_LABELS[M4_T_COUNT] = {
    "YM2203 SET ","HARMONY    ","EQ         ",
    "VCA        ","DELAY      ","LFO        ",
    "APPLY NOW  ","EXIT       "
};
static const uint16_t M4_TOP_COLORS[M4_T_COUNT] = {
    0xFD20u, 0x867Fu, 0xF81Fu, 0x4A10u,
    0x07E0u, 0x39E7u, 0x4FE0u, 0xF800u
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
    int             psg_param_cur;  /* 0=패치, 1=볼륨, 2=음정오프셋 */
    int             hm_cur;

    struct timespec preview_ts;
    int             preview_active;
    uint8_t         preview_ch;
    int             preview_is_psg;

    uint8_t         prev_sw;
    uint8_t         joy_y_mv;
    uint8_t         joy_x_mv;
    uint16_t        vca_mk_manual;  /* JOY-X 볼륨 누적값 (가변저항 VOL 대체) */
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

static void m4_deadzone(spi_packet_t* rx)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    if (jy >= JOY_DEAD_LO && jy <= JOY_DEAD_HI) M4.joy_y_mv = 0;
    if (jx >= JOY_DEAD_LO && jx <= JOY_DEAD_HI) { M4.joy_x_mv = 0; M4.jx_hold_cnt = 0; }
}

/* 프리뷰 타이머 — 350ms 후 자동 Key-Off */
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
    M4.preview_active = 0;
}

/* fi_note_on_v() 기반 프리뷰 — PSG 노이즈까지 함께 청취 */
static void m4_preview_fm_full(uint8_t ch, uint8_t inst_idx)
{
    if (!p_ym || inst_idx >= FULL_INST_COUNT) return;
    m4_preview_stop();
    const full_inst_t* fi = FULL_INST_CATALOG[inst_idx];
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
    ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pidx], (uint8_t)(1u << ch));
    ym2203_ssg_set_note(ch, 60);
    M4.preview_ch = ch; M4.preview_is_psg = 1;
    clock_gettime(CLOCK_MONOTONIC, &M4.preview_ts);
    M4.preview_active = 1;
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
    int y = 18 + idx * 27;
    if (y > 214) return;
    uint16_t bg = sel ? 0x2104u : 0x0000u;
    lcd_fill_rect(0, (uint16_t)y, LCD_W, 27, bg);
    lcd_string(2, (uint16_t)(y + 9), sel ? "->" : "  ", COLOR_WHITE, bg);
    lcd_string(18, (uint16_t)(y + 9), M4_TOP_LABELS[idx], M4_TOP_COLORS[idx], bg);

    char val[48] = "";
    switch (idx) {
    case M4_T_YM: {
        char s[3][10];
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
    case M4_T_EQ:    snprintf(val, sizeof(val), "%-10s", EQ_PRESET_NAMES[M4.edit.eq_preset]);   break;
    case M4_T_VCA:   snprintf(val, sizeof(val), "%-10s", VCA_PRESET_NAMES[M4.edit.vca_preset]); break;
    case M4_T_DELAY: snprintf(val, sizeof(val), "%-10s", DLY_PRESET_NAMES[M4.edit.dly_preset]); break;
    case M4_T_LFO:   snprintf(val, sizeof(val), "%-10s", LFO_PRESET_NAMES[M4.edit.lfo_preset]); break;
    case M4_T_APPLY: snprintf(val, sizeof(val), "%s", M4.apply_flash ? "** SAVED! **" : "[SW3:저장]"); break;
    case M4_T_EXIT:  snprintf(val, sizeof(val), "-> MENU"); break;
    }
    /* JOY-X 볼륨 제어가 활성화된 커서(YM/HARMONY/APPLY/EXIT)에서
       현재 makeup 퍼센트를 val 오른쪽에 표시하여 시각적 피드백 제공 */
    if (idx == M4_T_YM || idx == M4_T_HARMONY || idx == M4_T_APPLY || idx == M4_T_EXIT) {
        int pct = (int)((uint32_t)M4.vca_mk_manual * 100u / Q15_ONE);
        int used = (int)strlen(val);
        if (used < (int)sizeof(val) - 8)
            snprintf(val + used, sizeof(val) - (size_t)used, " V%3d%%", pct);
    }
    if (val[0]) lcd_string(120, (uint16_t)(y + 9), val, sel ? COLOR_YELLOW : COLOR_WHITE, bg);
}

static void m4_draw_top_all(void)
{
    lcd_clear(COLOR_BLACK);
    lcd_draw_header(0x4A10u, "  INST SETUP v2 (mode=4)", COLOR_WHITE);
    for (int i = 0; i < M4_T_COUNT; i++) m4_draw_top_row(i, (i == M4.top_cur));
    lcd_string(2, 228, "JY:이동  JX:효과+/-/볼륨  SW3:선택  SW4:메뉴  SW5:취소",
        COLOR_DKGREY, COLOR_BLACK);
}

/* ── YM 선택 ── */
static void m4_draw_ym_all(void)
{
    lcd_clear(COLOR_BLACK);
    lcd_draw_header(0xFD20u, "  YM2203 채널 선택", COLOR_BLACK);
    lcd_string(190, 5, "JX:FM<>PSG", COLOR_YELLOW, 0xFD20u);
    lcd_string(4, 20, "─── FM 채널 ───", COLOR_CYAN, COLOR_BLACK);
    for (int ch = 0; ch < 3; ch++) {
        uint16_t y = (uint16_t)(30 + ch * 22);
        int sel = (M4.ym_cur == ch);
        uint16_t bg = sel ? 0x2104u : 0x0000u;
        lcd_fill_rect(0, y, LCD_W, 22, bg);
        fm_ch_cfg_t* fc = &M4.edit.fm[ch];
        char tag[6] = "";
        if (fc->edited) strncpy(tag, "[E] ", 5);
        else if (!fc->use_full) snprintf(tag, 6, "[R] ");
        char buf[56];
        snprintf(buf, sizeof(buf), "%sFM%d %s#%02d %-12s",
            sel ? "->" : "  ", ch, tag, fc->inst_idx,
            fc->use_full ? m4_inst_name(fc->inst_idx) : m4_raw_patch_name(fc->inst_idx));
        lcd_string(4, (uint16_t)(y + 6), buf, COLOR_CYAN, bg);
    }
    lcd_string(4, 98, "─── PSG 채널 ───", COLOR_MAGENTA, COLOR_BLACK);
    for (int ch = 0; ch < 3; ch++) {
        uint16_t y = (uint16_t)(108 + ch * 22);
        int sel = (M4.ym_cur == ch + 3);
        uint16_t bg = sel ? 0x2104u : 0x0000u;
        lcd_fill_rect(0, y, LCD_W, 22, bg);
        psg_ch_cfg_t* pc = &M4.edit.psg[ch];
        char buf[56];
        snprintf(buf, sizeof(buf), "%sPSG%d %s #%-2d %-10s A=%2d noff=%+d",
            sel ? "->" : "  ", ch, pc->enable ? "ON " : "OFF",
            (int)pc->patch_idx, m4_ssg_name((uint8_t)pc->patch_idx),
            pc->amp, pc->note_offset);
        lcd_string(4, (uint16_t)(y + 6), buf, COLOR_MAGENTA, bg);
    }
    lcd_string(2, 228, "JY:이동  JX:FM<>PSG  SW3:편집  SW4:나가기  SW5:취소",
        COLOR_DKGREY, COLOR_BLACK);
}

/* ── FM 악기 선택 ── */
static void m4_draw_fm_inst(void)
{
    lcd_clear(COLOR_BLACK);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "  FM CH%d 악기선택 [%s모드]",
        M4.fm_ch, M4.fm_edit_mode ? "RAW패치" : "악기");
    lcd_draw_header(0x07FFu, hdr, COLOR_BLACK);

    fm_ch_cfg_t* fc = &M4.edit.fm[M4.fm_ch];
    char buf[56];

    if (!M4.fm_edit_mode) {
        uint8_t idx = M4.fm_inst_cur;
        snprintf(buf, sizeof(buf), "#%02d %-16s%s",
            idx, m4_inst_name(idx), fc->edited ? "[편집됨]" : "");
        lcd_string(4, 22, buf, COLOR_YELLOW, COLOR_BLACK);
        if (FULL_INST_COUNT > 1) {
            uint32_t denominator = (FULL_INST_COUNT > 1) ? (FULL_INST_COUNT - 1) : 1;
            uint16_t bw = (uint16_t)((uint32_t)idx * 278u / (FULL_INST_COUNT - 1));
            lcd_fill_rect(4, 36, (uint16_t)bw, 7, COLOR_CYAN);
            if (bw < 278) lcd_fill_rect((uint16_t)(4 + bw), 36, (uint16_t)(278 - bw), 7, 0x1082u);
        }
        if (idx < FULL_INST_COUNT) {
            const full_inst_t* fi = FULL_INST_CATALOG[idx];
            snprintf(buf, sizeof(buf), "FM수:%d SSG:0x%02X LFO:%dHz",
                fi->fm_n, fi->ssg_mask, fi->lfo_hz100 / 100);
            lcd_string(4, 48, buf, COLOR_GREEN, COLOR_BLACK);
            if (fi->fm_n > 0) {
                const ym2203_patch_t* p = &fi->fm[0];
                snprintf(buf, sizeof(buf), "ALG:%d FB:%d", p->ALG, p->FB);
                lcd_string(4, 62, buf, COLOR_WHITE, COLOR_BLACK);
                for (int op = 0; op < 4; op++) {
                    snprintf(buf, sizeof(buf), "OP%d TL:%3d AR:%2d DR:%2d RR:%2d",
                        op, p->ops[op].TL, p->ops[op].AR,
                        p->ops[op].DR, p->ops[op].RR);
                    lcd_string(4, (uint16_t)(76 + op * 13), buf, COLOR_GREY, COLOR_BLACK);
                }
            }
        }
        for (int d = -2; d <= 2; d++) {
            int ni = (int)M4.fm_inst_cur + d;
            if (ni < 0 || ni >= FULL_INST_COUNT) continue;
            uint16_t y = (uint16_t)(148 + d * 13 + 26);
            char lb[32]; snprintf(lb, sizeof(lb), "#%02d %-15s", ni, m4_inst_name((uint8_t)ni));
            lcd_string(8, y, lb, d == 0 ? COLOR_CYAN : COLOR_DKGREY, COLOR_BLACK);
        }
        lcd_string(2, 228, "JY:+/-1  JX:+/-5  SW3:OP편집  SW4:확정  SW5:취소  [JX롱:RAW모드]",
            COLOR_DKGREY, COLOR_BLACK);
    }
    else {
        uint8_t idx = M4.fm_raw_cur;
        snprintf(buf, sizeof(buf), "#%02d %-18s%s",
            idx, m4_raw_patch_name(idx), fc->edited ? "[편집됨]" : "");
        lcd_string(4, 22, buf, 0xFFE0u, COLOR_BLACK);
        if (YM2203_PATCH_COUNT > 1) {
            uint16_t bw = (uint16_t)((uint32_t)idx * 278u / (YM2203_PATCH_COUNT - 1));
            lcd_fill_rect(4, 36, (uint16_t)bw, 7, 0xFFE0u);
            if (bw < 278) lcd_fill_rect((uint16_t)(4 + bw), 36, (uint16_t)(278 - bw), 7, 0x1082u);
        }
        if (idx < YM2203_PATCH_COUNT) {
            const ym2203_patch_t* p = &YM2203_PATCHES[idx];
            snprintf(buf, sizeof(buf), "ALG:%d FB:%d", p->ALG, p->FB);
            lcd_string(4, 50, buf, COLOR_GREEN, COLOR_BLACK);
            for (int op = 0; op < 4; op++) {
                snprintf(buf, sizeof(buf), "OP%d TL:%3d AR:%2d DR:%2d RR:%2d",
                    op, p->ops[op].TL, p->ops[op].AR,
                    p->ops[op].DR, p->ops[op].RR);
                lcd_string(4, (uint16_t)(66 + op * 14), buf, COLOR_GREY, COLOR_BLACK);
            }
        }
        for (int d = -2; d <= 2; d++) {
            int ni = (int)M4.fm_raw_cur + d;
            if (ni < 0 || ni >= YM2203_PATCH_COUNT) continue;
            uint16_t y = (uint16_t)(134 + d * 13 + 26);
            char lb[36]; snprintf(lb, sizeof(lb), "#%02d %-16s", ni, m4_raw_patch_name((uint8_t)ni));
            lcd_string(8, y, lb, d == 0 ? 0xFFE0u : COLOR_DKGREY, COLOR_BLACK);
        }
        lcd_string(2, 228, "JY:+/-1  JX:+/-5  SW3:OP편집  SW4:확정  SW5:취소  [JX롱:악기모드]",
            COLOR_DKGREY, COLOR_BLACK);
    }
}

/* ── FM OP 편집 ── */
static void m4_draw_fm_op(void)
{
    lcd_clear(COLOR_BLACK);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "  FM CH%d OP세밀편집", M4.fm_ch);
    lcd_draw_header(0x4A10u, hdr, COLOR_WHITE);
    for (int op = 0; op < 4; op++) {
        uint16_t x = (uint16_t)(op * 78 + 2);
        char lb[8]; snprintf(lb, sizeof(lb), " OP%d ", op);
        uint16_t bg = (op == M4.op_sel) ? COLOR_YELLOW : COLOR_DKGREY;
        lcd_fill_rect(x, 18, 76, 12, bg);
        lcd_string((uint16_t)(x + 6), 19, lb,
            (op == M4.op_sel) ? COLOR_BLACK : COLOR_WHITE, bg);
    }
    lcd_fill_rect(0, 30, LCD_W, 2, COLOR_DKGREY);

    ym2203_patch_t* ep = &M4.edit.fm[M4.fm_ch].patch_override;
    ym2203_op_t* opp = &ep->ops[M4.op_sel];

    for (int i = 0; i < M4_OP_P_COUNT; i++) {
        uint16_t y = (uint16_t)(32 + i * 15);
        if (y > 212) break;
        int sel = (i == M4.op_param_cur);
        uint16_t bg = sel ? 0x2104u : 0x0000u;
        lcd_fill_rect(0, y, LCD_W, 15, bg);
        lcd_string(2, (uint16_t)(y + 4), M4_OP_P_NAMES[i], COLOR_CYAN, bg);
        if (sel) lcd_string(32, (uint16_t)(y + 4), ">>", COLOR_YELLOW, bg);
        uint8_t val = 0, maxv = M4_OP_P_MAX[i];
        switch (i) {
        case 0:val = ep->ALG; break; case 1:val = ep->FB;  break;
        case 2:val = opp->DT; break; case 3:val = opp->MUL; break;
        case 4:val = opp->TL; break; case 5:val = opp->RS; break;
        case 6:val = opp->AR; break; case 7:val = opp->AM; break;
        case 8:val = opp->DR; break; case 9:val = opp->SR; break;
        case 10:val = opp->SL; break; case 11:val = opp->RR; break;
        case 12:val = opp->SSGEG; break;
        }
        char vb[20]; snprintf(vb, sizeof(vb), "%3d/%3d", val, maxv);
        lcd_string(48, (uint16_t)(y + 4), vb, sel ? COLOR_YELLOW : COLOR_WHITE, bg);
        if (maxv > 0) {
            uint16_t bw = (uint16_t)((uint32_t)val * 108u / maxv);
            lcd_fill_rect(110, y, bw, 13, COLOR_DKGREEN);
            if (bw < 108) lcd_fill_rect((uint16_t)(110 + bw), y, (uint16_t)(108 - bw), 13, 0x1082u);
        }
    }
    lcd_string(2, 228, "JY:파라미터  JX:값+/-1  SW3:OP탭전환  SW4:취소  SW5:확정",
        COLOR_DKGREY, COLOR_BLACK);
}

/* ── PSG 편집 ── */
static void m4_draw_psg(void)
{
    lcd_clear(COLOR_BLACK);
    char hdr[48]; snprintf(hdr, sizeof(hdr), "  PSG CH%d 편집", M4.psg_ch);
    lcd_draw_header(0xF81Fu, hdr, COLOR_BLACK);
    psg_ch_cfg_t* pc = &M4.edit.psg[M4.psg_ch];
    char buf[54];

    snprintf(buf, sizeof(buf), "상태: %s", pc->enable ? "ON " : "OFF");
    lcd_fill_rect(0, 22, LCD_W, 14, COLOR_BLACK);
    lcd_string(4, 24, buf, pc->enable ? COLOR_GREEN : COLOR_RED, COLOR_BLACK);

    /* 패치 행 — 커서 0 */
    {
        uint16_t bg = (M4.psg_param_cur == 0) ? 0x2104u : 0x0000u;
        lcd_fill_rect(0, 38, LCD_W, 16, bg);
        if (M4.psg_param_cur == 0) lcd_string(2, 41, ">>", COLOR_YELLOW, bg);
        snprintf(buf, sizeof(buf), "패치: #%-2d %-16s", (int)pc->patch_idx,
            m4_ssg_name((uint8_t)pc->patch_idx));
        lcd_string(18, 41, buf, COLOR_YELLOW, bg);
    }

    for (int d = -2; d <= 2; d++) {
        int ni = (int)pc->patch_idx + d;
        if (ni < 0 || ni >= SSG_COUNT) continue;
        uint16_t y = (uint16_t)(58 + d * 12 + 24);
        char lb[32]; snprintf(lb, sizeof(lb), "#%-2d %-14s", ni, m4_ssg_name((uint8_t)ni));
        lcd_string(8, y, lb, d == 0 ? COLOR_YELLOW : COLOR_DKGREY, COLOR_BLACK);
    }

    /* 볼륨 행 — 커서 1 */
    {
        uint16_t bg = (M4.psg_param_cur == 1) ? 0x2104u : 0x0000u;
        lcd_fill_rect(0, 126, LCD_W, 16, bg);
        if (M4.psg_param_cur == 1) lcd_string(2, 129, ">>", COLOR_YELLOW, bg);
        snprintf(buf, sizeof(buf), "볼륨: %2d/15", pc->amp);
        lcd_string(18, 129, buf, COLOR_CYAN, bg);
        uint16_t bw = (uint16_t)((uint32_t)pc->amp * 180u / 15u);
        lcd_fill_rect(95, 129, bw, 10, COLOR_CYAN);
        if (bw < 180) lcd_fill_rect((uint16_t)(95 + bw), 129, (uint16_t)(180 - bw), 10, 0x1082u);
    }

    /* 음정 오프셋 행 — 커서 2 */
    {
        uint16_t bg = (M4.psg_param_cur == 2) ? 0x2104u : 0x0000u;
        lcd_fill_rect(0, 144, LCD_W, 16, bg);
        if (M4.psg_param_cur == 2) lcd_string(2, 147, ">>", COLOR_YELLOW, bg);
        snprintf(buf, sizeof(buf), "음정오프셋: %+d반음", pc->note_offset);
        lcd_string(18, 147, buf, COLOR_WHITE, bg);
    }

    lcd_string(2, 228,
        "JY:파라미터선택  JX:값+/-  SW3:ON/OFF  SW4:확정  SW5:취소",
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
    lcd_string(2, 228, "JY:항목  JX:값+/-1  SW3:ON/OFF토글  SW4:나가기  SW5:취소",
        COLOR_DKGREY, COLOR_BLACK);
}

// ─────────────────────────────────────────────────────────────────────────────
//  §I-A  TOP 메뉴 APPLY
// ─────────────────────────────────────────────────────────────────────────────

static void m4_top_jx_change(int dir)
{
    int c = 0;
    switch (M4.top_cur) {
    case M4_T_EQ:
        if (dir < 0 && M4.edit.eq_preset>0) { M4.edit.eq_preset--;  apply_eq_preset(M4.edit.eq_preset);  c = 1; }
        if (dir > 0 && M4.edit.eq_preset < EQ_PRESET_COUNT - 1) { M4.edit.eq_preset++;  apply_eq_preset(M4.edit.eq_preset);  c = 1; }
        break;
    case M4_T_VCA:
        if (dir < 0 && M4.edit.vca_preset>0) { M4.edit.vca_preset--; apply_vca_preset(M4.edit.vca_preset); c = 1; }
        if (dir > 0 && M4.edit.vca_preset < VCA_PRESET_COUNT - 1) { M4.edit.vca_preset++; apply_vca_preset(M4.edit.vca_preset); c = 1; }
        break;
    case M4_T_DELAY:
        if (dir < 0 && M4.edit.dly_preset>0) { M4.edit.dly_preset--; apply_dly_preset(M4.edit.dly_preset); c = 1; }
        if (dir > 0 && M4.edit.dly_preset < DLY_PRESET_COUNT - 1) { M4.edit.dly_preset++; apply_dly_preset(M4.edit.dly_preset); c = 1; }
        break;
    case M4_T_LFO:
        if (dir < 0 && M4.edit.lfo_preset>0) { M4.edit.lfo_preset--; apply_lfo_preset(M4.edit.lfo_preset); c = 1; }
        if (dir > 0 && M4.edit.lfo_preset < LFO_PRESET_COUNT - 1) { M4.edit.lfo_preset++; apply_lfo_preset(M4.edit.lfo_preset); c = 1; }
        break;
    default:
        /* M4_T_YM / M4_T_HARMONY / M4_T_APPLY / M4_T_EXIT 커서:
           가변저항 ADC_VOL 이 하던 VCA makeup 볼륨 역할을 JOY-X 로 대체.
           스텝 = Q15_ONE/32 ≈ 3.1 %  (좌←감소 / 우→증가)              */
    {
        const uint16_t step = (uint16_t)(Q15_ONE / 32u);
        if (dir < 0)
            M4.vca_mk_manual = (uint16_t)(M4.vca_mk_manual > step
                ? M4.vca_mk_manual - step : 0u);
        else
            M4.vca_mk_manual = (uint16_t)(M4.vca_mk_manual + step <= Q15_ONE
                ? M4.vca_mk_manual + step : (uint16_t)Q15_ONE);
        vca_set_makeup_checked(M4.vca_mk_manual);
        c = 1;
    }
    break;
    }
    if (c) m4_draw_top_row(M4.top_cur, 1);
}

static void m4_top_do_apply(void)
{
    memcpy(&g_full_preset, &M4.edit, sizeof(full_preset_t));
    full_preset_apply_hw();         /* HW + 화성학 런타임 재초기화 */
    apply_eq_preset(g_full_preset.eq_preset);
    apply_vca_preset(g_full_preset.vca_preset);
    apply_dly_preset(g_full_preset.dly_preset);
    apply_lfo_preset(g_full_preset.lfo_preset);
    m3_full_voice_reset();
    memcpy(&M4.snap, &M4.edit, sizeof(full_preset_t));
    M4.apply_flash = 1; M4.flash_cnt = 0;
    m4_draw_top_row(M4_T_APPLY, 1);
    printf("[M4] APPLY: FM0=#%d(%s) FM1=#%d FM2=#%d EQ=%d VCA=%d DLY=%d\n",
        M4.edit.fm[0].inst_idx,
        M4.edit.fm[0].use_full ? m4_inst_name(M4.edit.fm[0].inst_idx) : "RAW",
        M4.edit.fm[1].inst_idx, M4.edit.fm[2].inst_idx,
        M4.edit.eq_preset, M4.edit.vca_preset, M4.edit.dly_preset);
    printf("[M4] HARMONY: vm=%d ji=%d kd=%d cp=%d nlfo=%d\n",
        M4.edit.harmony.voice_mgr_on, M4.edit.harmony.ji_on,
        M4.edit.harmony.key_detect_on, M4.edit.harmony.counterpoint_on,
        (int)M4.edit.harmony.nlfo_preset);
}

// ─────────────────────────────────────────────────────────────────────────────
//  §I-F  FSM 업데이트 함수
// ─────────────────────────────────────────────────────────────────────────────

static int m4_update_top(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];

    if (M4.apply_flash && ++M4.flash_cnt >= 60u) {
        M4.apply_flash = 0; M4.flash_cnt = 0;
        m4_draw_top_row(M4_T_APPLY, (M4.top_cur == M4_T_APPLY));
    }
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT)   return ZYNQ_MODE_MENU;
    if (sw == SW_CANCEL && M4.prev_sw != SW_CANCEL) {
        memcpy(&M4.edit, &M4.snap, sizeof(full_preset_t));
        m4_draw_top_all();
    }
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        switch (M4.top_cur) {
        case M4_T_YM:
            M4.state = M4_YM; M4.ym_cur = 0;
            m4_draw_ym_all(); break;
        case M4_T_HARMONY:
            M4.state = M4_HARMONY; M4.hm_cur = 0;
            m4_draw_hm_all(); break;
        case M4_T_EQ:    apply_eq_preset(M4.edit.eq_preset);   m4_draw_top_row(M4_T_EQ, 1);    break;
        case M4_T_VCA:   apply_vca_preset(M4.edit.vca_preset); m4_draw_top_row(M4_T_VCA, 1);   break;
        case M4_T_DELAY: apply_dly_preset(M4.edit.dly_preset); m4_draw_top_row(M4_T_DELAY, 1); break;
        case M4_T_LFO:   apply_lfo_preset(M4.edit.lfo_preset); m4_draw_top_row(M4_T_LFO, 1);   break;
        case M4_T_APPLY: m4_top_do_apply(); break;
        case M4_T_EXIT:  return ZYNQ_MODE_MENU;
        }
    }
    if (jy < JOY_LOW && !M4.joy_y_mv) {
        int p = M4.top_cur; if (M4.top_cur > 0) M4.top_cur--; M4.joy_y_mv = 1;
        m4_draw_top_row(p, 0); m4_draw_top_row(M4.top_cur, 1);
    }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) {
        int p = M4.top_cur; if (M4.top_cur < M4_T_COUNT - 1) M4.top_cur++; M4.joy_y_mv = 1;
        m4_draw_top_row(p, 0); m4_draw_top_row(M4.top_cur, 1);
    }
    if (jx < JOY_LOW && !M4.joy_x_mv) { m4_top_jx_change(-1); M4.joy_x_mv = 1; }
    if (jx > JOY_HIGH && !M4.joy_x_mv) { m4_top_jx_change(+1); M4.joy_x_mv = 1; }
    return ZYNQ_MODE_INSTSETUP;
}

static int m4_update_ym(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) { M4.state = M4_TOP; m4_draw_top_all(); return ZYNQ_MODE_INSTSETUP; }
    if (sw == SW_CANCEL && M4.prev_sw != SW_CANCEL) {
        memcpy(&M4.edit, &M4.snap, sizeof(full_preset_t));
        full_preset_apply_hw(); m4_draw_ym_all();
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
            M4.state = M4_FM_INST; m4_draw_fm_inst();
        }
        else {
            M4.psg_ch = M4.ym_cur - 3; M4.state = M4_PSG_CH; m4_draw_psg();
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
    if (jx<JOY_LOW || jx>JOY_HIGH) {
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
            m4_draw_fm_inst();
            return ZYNQ_MODE_INSTSETUP;
        }
    }

    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
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
        ym2203_cache_reset();
        ym2203_patch_apply(&fc->patch_override, (uint8_t)M4.fm_ch);
        m4_preview_stop(); M4.state = M4_YM; m4_draw_ym_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_CANCEL && M4.prev_sw != SW_CANCEL) {
        M4.edit.fm[M4.fm_ch] = M4.snap.fm[M4.fm_ch];
        m4_preview_stop(); M4.state = M4_YM; m4_draw_ym_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        if (!M4.fm_edit_mode)
            fc->patch_override = FULL_INST_CATALOG[M4.fm_inst_cur]->fm[0];
        else
            fc->patch_override = YM2203_PATCHES[M4.fm_raw_cur];
        M4.op_backup = fc->patch_override;
        M4.op_sel = 0; M4.op_param_cur = 0;
        M4.state = M4_FM_OP; m4_preview_stop(); m4_draw_fm_op();
        return ZYNQ_MODE_INSTSETUP;
    }

    int inst_changed = 0;
    if (!M4.fm_edit_mode) {
        if (jy < JOY_LOW && !M4.joy_y_mv) {
            if (M4.fm_inst_cur > 0) { M4.fm_inst_cur--; inst_changed = 1; } M4.joy_y_mv = 1;
        }
        else if (jy > JOY_HIGH && !M4.joy_y_mv) {
            if (M4.fm_inst_cur < FULL_INST_COUNT - 1) { M4.fm_inst_cur++; inst_changed = 1; } M4.joy_y_mv = 1;
        }
        if (jx < JOY_LOW && !M4.joy_x_mv && M4.jx_hold_cnt < 50u) {
            uint8_t p = M4.fm_inst_cur;
            M4.fm_inst_cur = (M4.fm_inst_cur >= 5) ? M4.fm_inst_cur - 5 : 0;
            if (M4.fm_inst_cur != p) inst_changed = 1; M4.joy_x_mv = 1;
        }
        else if (jx > JOY_HIGH && !M4.joy_x_mv && M4.jx_hold_cnt < 50u) {
            uint8_t p = M4.fm_inst_cur;
            M4.fm_inst_cur = (M4.fm_inst_cur + 5 < FULL_INST_COUNT) ? M4.fm_inst_cur + 5 : (uint8_t)(FULL_INST_COUNT - 1);
            if (M4.fm_inst_cur != p) inst_changed = 1; M4.joy_x_mv = 1;
        }
        if (inst_changed) {
            fc->patch_override = FULL_INST_CATALOG[M4.fm_inst_cur]->fm[0];
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
        if (jx < JOY_LOW && !M4.joy_x_mv && M4.jx_hold_cnt < 50u) {
            uint8_t p = M4.fm_raw_cur;
            M4.fm_raw_cur = (M4.fm_raw_cur >= 5) ? M4.fm_raw_cur - 5 : 0;
            if (M4.fm_raw_cur != p) inst_changed = 1; M4.joy_x_mv = 1;
        }
        else if (jx > JOY_HIGH && !M4.joy_x_mv && M4.jx_hold_cnt < 50u) {
            uint8_t p = M4.fm_raw_cur;
            M4.fm_raw_cur = (M4.fm_raw_cur + 5 < YM2203_PATCH_COUNT) ? M4.fm_raw_cur + 5 : (uint8_t)(YM2203_PATCH_COUNT - 1);
            if (M4.fm_raw_cur != p) inst_changed = 1; M4.joy_x_mv = 1;
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

    /* ── SW3: OP 탭 순환 (OP0→1→2→3→0) ── */
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        M4.op_sel = (M4.op_sel + 1u) & 3u;
        m4_preview_fm_op((uint8_t)M4.fm_ch);
        m4_draw_fm_op();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── SW4: 취소 — 백업 복원 후 FM_INST 복귀 ── */
    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        M4.edit.fm[M4.fm_ch].patch_override = M4.op_backup;
        ym2203_cache_reset();
        ym2203_patch_apply(&M4.op_backup, (uint8_t)M4.fm_ch);
        m4_preview_stop(); M4.state = M4_FM_INST; m4_draw_fm_inst();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── SW5: 확정 — edited 플래그 + FM_INST 복귀 ── */
    if (sw == SW_CANCEL && M4.prev_sw != SW_CANCEL) {
        M4.edit.fm[M4.fm_ch].edited = 1;
        M4.edit.fm[M4.fm_ch].use_full = 0;
        ym2203_cache_reset();
        ym2203_patch_apply(&M4.edit.fm[M4.fm_ch].patch_override, (uint8_t)M4.fm_ch);
        m4_preview_stop(); M4.state = M4_FM_INST; m4_draw_fm_inst();
        return ZYNQ_MODE_INSTSETUP;
    }

    /* ── JOY-Y: 파라미터 행 이동 ── */
    if (jy < JOY_LOW && !M4.joy_y_mv) {
        if (M4.op_param_cur > 0) { M4.op_param_cur--; m4_draw_fm_op(); } M4.joy_y_mv = 1;
    }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) {
        if (M4.op_param_cur < M4_OP_P_COUNT - 1) { M4.op_param_cur++; m4_draw_fm_op(); } M4.joy_y_mv = 1;
    }

    /* ── JOY-X: 선택된 파라미터 값 ±1 ── */
    if (jx < JOY_LOW && !M4.joy_x_mv) {
        /* ALG/FB(파라미터 0,1)는 JOY-X 롱프레스로도 OP탭 전환이 자연스러우나,
           여기서는 단순히 값 감소만 처리한다. OP탭 전환은 SW3로 통일. */
        uint8_t* ptr = m4_op_ptr(M4.op_param_cur);
        if (*ptr > 0) {
            (*ptr)--;
            ym2203_cache_reset();
            ym2203_patch_apply(&M4.edit.fm[M4.fm_ch].patch_override, (uint8_t)M4.fm_ch);
            m4_preview_fm_op((uint8_t)M4.fm_ch); m4_draw_fm_op();
        }
        M4.joy_x_mv = 1;
    }
    else if (jx > JOY_HIGH && !M4.joy_x_mv) {
        uint8_t* ptr = m4_op_ptr(M4.op_param_cur);
        uint8_t mx = M4_OP_P_MAX[M4.op_param_cur];
        if (*ptr < mx) {
            (*ptr)++;
            ym2203_cache_reset();
            ym2203_patch_apply(&M4.edit.fm[M4.fm_ch].patch_override, (uint8_t)M4.fm_ch);
            m4_preview_fm_op((uint8_t)M4.fm_ch); m4_draw_fm_op();
        }
        M4.joy_x_mv = 1;
    }
    return ZYNQ_MODE_INSTSETUP;
}

static int m4_update_psg(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];
    psg_ch_cfg_t* pc = &M4.edit.psg[M4.psg_ch];

    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        if (pc->enable) ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx], (uint8_t)(1u << M4.psg_ch));
        else ym2203_ssg_set_vol((uint8_t)M4.psg_ch, 0);
        m4_preview_stop(); M4.state = M4_YM; m4_draw_ym_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_CANCEL && M4.prev_sw != SW_CANCEL) {
        *pc = M4.snap.psg[M4.psg_ch];
        if (pc->enable) ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx], (uint8_t)(1u << M4.psg_ch));
        else ym2203_ssg_set_vol((uint8_t)M4.psg_ch, 0);
        m4_preview_stop(); M4.state = M4_YM; m4_draw_ym_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        pc->enable ^= 1u;
        if (pc->enable) ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx], (uint8_t)(1u << M4.psg_ch));
        else ym2203_ssg_set_vol((uint8_t)M4.psg_ch, 0);
        m4_draw_psg();
    }

    /* JY: 파라미터 행 선택 (패치=0 / 볼륨=1 / 음정오프셋=2) */
    if (jy < JOY_LOW && !M4.joy_y_mv) {
        if (M4.psg_param_cur > 0) { M4.psg_param_cur--; m4_draw_psg(); }
        M4.joy_y_mv = 1;
    }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) {
        if (M4.psg_param_cur < 2) { M4.psg_param_cur++; m4_draw_psg(); }
        M4.joy_y_mv = 1;
    }

    /* JX: 선택된 파라미터 값 변경 */
    if (jx < JOY_LOW && !M4.joy_x_mv) {
        switch (M4.psg_param_cur) {
        case 0: /* 패치 */
            if ((int)pc->patch_idx > 0) {
                pc->patch_idx = (ym2203_ssg_idx_t)((int)pc->patch_idx - 1);
                if (pc->enable) {
                    ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx], (uint8_t)(1u << M4.psg_ch));
                    m4_preview_psg((uint8_t)M4.psg_ch, (uint8_t)pc->patch_idx);
                }
                m4_draw_psg();
            }
            break;
        case 1: /* 볼륨 */
            if (pc->amp > 0) { pc->amp--; ym2203_ssg_set_vol((uint8_t)M4.psg_ch, pc->amp); m4_draw_psg(); }
            break;
        case 2: /* 음정 오프셋 */
            if (pc->note_offset > -12) { pc->note_offset--; m4_draw_psg(); }
            break;
        }
        M4.joy_x_mv = 1;
    }
    else if (jx > JOY_HIGH && !M4.joy_x_mv) {
        switch (M4.psg_param_cur) {
        case 0: /* 패치 */
            if ((int)pc->patch_idx < SSG_COUNT - 1) {
                pc->patch_idx = (ym2203_ssg_idx_t)((int)pc->patch_idx + 1);
                if (pc->enable) {
                    ym2203_ssg_patch_apply(&YM2203_SSG_PATCHES[pc->patch_idx], (uint8_t)(1u << M4.psg_ch));
                    m4_preview_psg((uint8_t)M4.psg_ch, (uint8_t)pc->patch_idx);
                }
                m4_draw_psg();
            }
            break;
        case 1: /* 볼륨 */
            if (pc->amp < 15) { pc->amp++; ym2203_ssg_set_vol((uint8_t)M4.psg_ch, pc->amp); m4_draw_psg(); }
            break;
        case 2: /* 음정 오프셋 */
            if (pc->note_offset < 12) { pc->note_offset++; m4_draw_psg(); }
            break;
        }
        M4.joy_x_mv = 1;
    }
    return ZYNQ_MODE_INSTSETUP;
}

/* HARMONY 서브메뉴 */
static int m4_update_hm(uint8_t sw)
{
    uint16_t jy = adc_buf[ADC_IDX_JOYY];
    uint16_t jx = adc_buf[ADC_IDX_JOYX];

    if (sw == SW_EXIT && M4.prev_sw != SW_EXIT) {
        M4.state = M4_TOP; m4_draw_top_all();
        return ZYNQ_MODE_INSTSETUP;
    }
    if (sw == SW_CANCEL && M4.prev_sw != SW_CANCEL) {
        M4.edit.harmony = M4.snap.harmony;
        m4_draw_hm_all();
    }
    if (sw == SW_SELECT && M4.prev_sw != SW_SELECT) {
        uint8_t* ptr = m4_hm_ptr(M4.hm_cur);
        uint8_t mx = M4_H_MAX[M4.hm_cur];
        if (mx == 1) { *ptr ^= 1u; }
        else { *ptr = 0; }
        m4_draw_hm_row(M4.hm_cur, 1);
    }

    int prev_row = -1;
    if (jy < JOY_LOW && !M4.joy_y_mv) {
        prev_row = M4.hm_cur;
        if (M4.hm_cur > 0) M4.hm_cur--; M4.joy_y_mv = 1;
    }
    else if (jy > JOY_HIGH && !M4.joy_y_mv) {
        prev_row = M4.hm_cur;
        if (M4.hm_cur < M4_H_COUNT - 1) M4.hm_cur++; M4.joy_y_mv = 1;
    }
    if (prev_row >= 0) {
        if ((prev_row / 9) != (M4.hm_cur / 9)) m4_draw_hm_all();
        else { m4_draw_hm_row(prev_row, 0); m4_draw_hm_row(M4.hm_cur, 1); }
    }

    if (jx < JOY_LOW && !M4.joy_x_mv) {
        uint8_t* ptr = m4_hm_ptr(M4.hm_cur);
        if (*ptr > 0) { (*ptr)--; m4_draw_hm_row(M4.hm_cur, 1); }
        M4.joy_x_mv = 1;
    }
    else if (jx > JOY_HIGH && !M4.joy_x_mv) {
        uint8_t* ptr = m4_hm_ptr(M4.hm_cur);
        uint8_t mx = M4_H_MAX[M4.hm_cur];
        if (*ptr < mx) { (*ptr)++; m4_draw_hm_row(M4.hm_cur, 1); }
        M4.joy_x_mv = 1;
    }
    return ZYNQ_MODE_INSTSETUP;
}

// ─────────────────────────────────────────────────────────────────────────────
//  §I-E  공개 API
// ─────────────────────────────────────────────────────────────────────────────

static void mode_instsetup_enter(void)
{
    printf("[M4] INST SETUP v2 진입\n");
    apply_eq_preset(0); apply_vca_preset(1); apply_dly_preset(1); lfo_stop_all();

    memcpy(&M4.edit, &g_full_preset, sizeof(full_preset_t));
    memcpy(&M4.snap, &g_full_preset, sizeof(full_preset_t));

    M4.state = M4_TOP;
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
    M4.vca_mk_manual = (uint16_t)(Q15_ONE / 2u);  /* 초기 50% — ADC_VOL 대체 */

    full_preset_apply_hw();
    m4_draw_top_all();
}

static int mode_instsetup_update(spi_packet_t* rx, spi_packet_t* tx)
{
    tx->active_mode = ZYNQ_MODE_INSTSETUP;

    // 1. 에러 해결: 정의에 맞춰 인자 없이 호출
    m4_deadzone(rx);
    m4_preview_tick();

    uint8_t sw = rx->sw_status;
    int ret = ZYNQ_MODE_INSTSETUP;

    // 2. [V14-FIX] 각 서브함수(m4_update_*)가 adc_buf[ADC_IDX_JOYX]로 JOY-X를 직접 처리.
    //    외부에서 M4.state를 조작하는 로직은 FSM 파괴 → 제거됨.
    switch (M4.state) {
    case M4_TOP:     ret = m4_update_top(sw);     break;
    case M4_YM:      ret = m4_update_ym(sw);      break;
    case M4_FM_INST: ret = m4_update_fm_inst(sw); break;
    case M4_FM_OP:   ret = m4_update_fm_op(sw);   break;
    case M4_PSG_CH:  ret = m4_update_psg(sw);     break;
    case M4_HARMONY: ret = m4_update_hm(sw);      break;
    default: M4.state = M4_TOP; break;
    }

    M4.prev_sw = sw;
    return ret;
}


static void mode_instsetup_cleanup(void)
{
    m4_preview_stop();
    printf("[M4] INST SETUP 종료\n");
}

// =============================================================================
//  [섹션 J]  FSM 디스패처 & main()
// =============================================================================

typedef struct {
    void (*enter)(void);
    int  (*update)(const void*, void*); // void* 로 변경
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
    { mode_menu_enter,      (int (*)(const void*, void*))mode_menu_update,      mode_menu_cleanup      },
    { mode_synth_enter,     (int (*)(const void*, void*))mode_synth_update,     mode_synth_cleanup     },
    { mode_vgm_enter,       (int (*)(const void*, void*))mode_vgm_update,       mode_vgm_cleanup       },
    { mode_midivis_enter,   (int (*)(const void*, void*))mode_midivis_update,   mode_midivis_cleanup   },
    { mode_instsetup_enter, (int (*)(const void*, void*))mode_instsetup_update, mode_instsetup_cleanup },
};

static void hw_cleanup(void)
{
    if (p_lfo) for (int i = 0; i < 8; i++) {
        reg_wr(p_lfo, LFO_OFF(i, 0), LFO_FCW_OFF);
        reg_wr(p_lfo, LFO_OFF(i, 1), 0u);
    }
    if (p_lcd) { lcd_shadow = LCD_RST; reg_wr(p_lcd, LCD_GPIO1_OFF, lcd_shadow); }
    if (p_ym) { reg_wr(p_ym, YM_GPIO1_OFF, 0u); reg_wr(p_ym, YM_GPIO2_OFF, 0u); }
    p_delay = p_eq = p_lfo = p_vca = NULL; p_ym = p_fifosg = p_lcd = NULL; p_efin = NULL;
    uio_close(&uio_delay); uio_close(&uio_eq); uio_close(&uio_lfo); uio_close(&uio_vca);
    uio_close(&uio_ym2203); uio_close(&uio_fifo_sg); uio_close(&uio_ef_in); uio_close(&uio_lcd_ctrl);
    if (fd_lcd >= 0) { close(fd_lcd); fd_lcd = -1; }
    if (fd_esp >= 0) { close(fd_esp); fd_esp = -1; }
}

int main(void)
{
    struct sigaction sa; memset(&sa, 0, sizeof(sa)); sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);

    printf("=== effect_bd_uio v14 ===\n");
    printf("  M0:MENU M1:SYNTH M2:VGM M3:MIDI+SOUND M4:INST SETUP v2\n");
    printf("  [V14] full_preset_t + harmony_cfg_t + m3_full_* 통합\n");
    printf("  SW3=선택  SW4=나가기  SW5=취소\n");

    if (load_uio_module() != 0) {
        fprintf(stderr, "[FATAL] UIO 로드 실패\n"); return EXIT_FAILURE;
    }

    /* [V14] 초기화 순서:
       1. full_preset_default()   — g_full_preset 기본값
       2. ym2203_init()
       3. m3_full_voice_reset()   — 보이스 테이블 초기화
       4. harmony_runtime_apply() — 화성학 엔진 런타임 초기화
       5. M4 컨텍스트 클리어       */
    full_preset_default();
    ym2203_init(50u);
    m3_full_voice_reset();
    harmony_runtime_apply();
    memset(&M4, 0, sizeof(M4));

    printf("[UIO] 디바이스 탐색...\n");
    uio_delay = uio_open_name_or_addr("analog_delay_top", ADDR_DELAY, PROT_READ | PROT_WRITE);
    uio_eq = uio_open_name_or_addr("eq_6band_top", ADDR_EQ, PROT_READ | PROT_WRITE);
    uio_lfo = uio_open_name_or_addr("nco_multi_lfo", ADDR_LFO, PROT_READ | PROT_WRITE);
    uio_vca = uio_open_name_or_addr("vca_top", ADDR_VCA, PROT_READ | PROT_WRITE);
    uio_ym2203 = uio_open_by_addr("axi_ym2203", ADDR_YM2203, PROT_READ | PROT_WRITE);
    uio_lcd_ctrl = uio_open_by_addr("spi_lcd", ADDR_LCD_CTL, PROT_READ | PROT_WRITE);
    uio_fifo_sg = uio_open_by_addr("ym2203_fifo_sg", ADDR_FIFO_SG, PROT_READ);
    uio_ef_in = uio_open_by_addr("ef_in", ADDR_EF_IN, PROT_READ);

    if (!uio_delay.map || !uio_eq.map || !uio_vca.map || !uio_ym2203.map || !uio_lcd_ctrl.map) {
        fprintf(stderr, "[FATAL] 필수 UIO 열기 실패\n"); hw_cleanup(); return EXIT_FAILURE;
    }
    if (!uio_lfo.map)     printf("[WARN] nco_multi_lfo 미연결\n");
    if (!uio_fifo_sg.map) printf("[WARN] ym2203_fifo_sg 미연결\n");
    if (!uio_ef_in.map)   printf("[WARN] ef_in 미연결\n");

    p_delay = (volatile uint8_t*)uio_delay.map;
    p_eq = (volatile uint8_t*)uio_eq.map;
    p_lfo = uio_lfo.map ? (volatile uint8_t*)uio_lfo.map : NULL;
    p_vca = (volatile uint8_t*)uio_vca.map;
    p_ym = (volatile uint8_t*)uio_ym2203.map;
    p_lcd = (volatile uint8_t*)uio_lcd_ctrl.map;
    p_fifosg = uio_fifo_sg.map ? (volatile uint8_t*)uio_fifo_sg.map : NULL;
    p_efin = uio_ef_in.map ? (volatile const uint8_t*)uio_ef_in.map : NULL;

    ym_gpio_init();
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
    { uint8_t mode = SPI_MODE_0, bits = 8; uint32_t spd;
    spd = LCD_SPI_HZ;   ioctl(fd_lcd, SPI_IOC_WR_MODE, &mode); ioctl(fd_lcd, SPI_IOC_WR_BITS_PER_WORD, &bits); ioctl(fd_lcd, SPI_IOC_WR_MAX_SPEED_HZ, &spd);
    spd = SPI_ESP32_HZ; ioctl(fd_esp, SPI_IOC_WR_MODE, &mode); ioctl(fd_esp, SPI_IOC_WR_BITS_PER_WORD, &bits); ioctl(fd_esp, SPI_IOC_WR_MAX_SPEED_HZ, &spd); }

    lcd_init(); lcd_clear(COLOR_BLACK);
    lcd_draw_header(COLOR_DKGREEN, "  v14 Ready", COLOR_WHITE);
    lcd_string(4, 22, "SW3:SELECT  SW4:EXIT  SW5:CANCEL", COLOR_YELLOW, COLOR_BLACK);
    lcd_string(4, 38, "M1: SYNTH/MIDI", COLOR_CYAN, COLOR_BLACK);
    lcd_string(4, 54, "M2: VGM PLAYER", COLOR_MAGENTA, COLOR_BLACK);
    lcd_string(4, 70, "M3: MIDI KBD + YM2203", COLOR_ORANGE, COLOR_BLACK);
    lcd_string(4, 86, "M4: INST SETUP v2", 0x4A10u, COLOR_BLACK);
    lcd_string(4, 102, "    YM+HARMONY+EQ+VCA+DLY+LFO", COLOR_WHITE, COLOR_BLACK);
    msleep(2000);

    pthread_t vgm_tid;
    if (pthread_create(&vgm_tid, NULL, vgm_rt_thread, NULL) != 0) {
        perror("[FATAL] vgm pthread"); hw_cleanup(); return EXIT_FAILURE;
    }
    pthread_t midi_tid;
    g_midi_thread_exit = 0;
    if (pthread_create(&midi_tid, NULL, midi_rx_thread, NULL) != 0) {
        perror("[FATAL] midi pthread"); hw_cleanup(); return EXIT_FAILURE;
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
                cur = ZYNQ_MODE_MENU; entered = -1; sync_fail_run = 0;
            }
            usleep(5000); continue;
        }
        sync_ok_cnt++; sync_fail_run = 0;

        adc_buf[ADC_IDX_VOL] = adc_smooth(adc_buf[ADC_IDX_VOL], rx_pkt.adc_vol);
        adc_buf[ADC_IDX_PITCH] = adc_smooth(adc_buf[ADC_IDX_PITCH], rx_pkt.adc_pitch);
        adc_buf[ADC_IDX_JOYX] = adc_smooth(adc_buf[ADC_IDX_JOYX], rx_pkt.joy_x);
        adc_buf[ADC_IDX_JOYY] = adc_smooth(adc_buf[ADC_IDX_JOYY], rx_pkt.joy_y);

        if (cur != entered) {
            if (entered >= 0 && entered < ZYNQ_MODE_COUNT) FSM[entered].cleanup();
            if (cur >= 0 && cur < ZYNQ_MODE_COUNT) {
                FSM[cur].enter();
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
    hw_cleanup();
    printf("[MAIN] 완료\n");
    return EXIT_SUCCESS;
}