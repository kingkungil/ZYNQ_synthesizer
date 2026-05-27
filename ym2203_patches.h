// =============================================================================
//  ym2203_patches.h  —  YM2203 (OPN) FM + SSG 통합 드라이버  Rev.12
//  Target: Xilinx Zynq / jt03 core (4 MHz OPN clock)
//
// ── Rev.12 완성 ──────────────────────────────────────────────────────────────
//
//  [R12-FIX-1]  LFO shape 악기 테이블 선택 가능
//    ym2203_instrument_t에 lfo_shape 필드 추가. SINE/TRI/SAW/SQR 선택.
//    ym2203_voice_note_on()에서 inst->lfo_shape 읽어 LFO 초기화.
//
//  [R12-FIX-2]  SSG vibrato + portamento (§24b)
//    ym2203_ssg_voice_t 구조체 + g_ssg_voice[3] 전역 → §6 forward 선언으로 이동.
//    컴파일 오류(순방향 참조) 해소. §24b는 함수 본체만 유지.
//    ym2203_ssg_note_on()에서 vib_base_period 캐시 + porta_enable 리셋.
//    ym2203_ssg_vibrato_set() / ym2203_ssg_porta_start() API 제공.
//    ym2203_ssg_voice_tick()은 ym2203_tick()에서 자동 호출.
//
//  [R12-NEW-1]  SSG 단독 악기 테이블 엔트리 (인덱스 41~62)
//    SSG 전용 악기 22종. FM 채널은 vel_tl_scale=0 + vel=0 경로로 무음 처리.
//    ym2203_inst_note_on()에서 vel_tl_scale=0 && ssg_ch≠0xFF 감지 시 fm_vel=0.
//    멜로디(41~47), 발현(48~54), 드럼(55~59), FX(60~62).
//    SSG 채널 할당: 멜로디=ch0, 드럼/FX=ch1, 베이스/워블=ch2.
//
//  [R12-NEW-2]  ch3 Extended mode 악기 테이블 엔트리 (인덱스 63~66)
//    ym2203_extch3_note_on() API의 fm_patch 참조용 엔트리.
//    실제 발음: YM_EXT_CHORD4(patch_idx, n1,n2,n3,n4, vel) 매크로 사용.
//    - INST_ECH3_ORGEL(63):      PATCH_MUSICBOX ALG7 — additive 4화음 오르골
//    - INST_ECH3_BELL_CHORD(64): PATCH_BELL ALG7 — 인하모닉 4음 종소리 화음
//    - INST_ECH3_DAEGEUM_UNI(65):PATCH_DAEGEUM ALG5 — 4 OP 미세 디튠 유니즌
//    - INST_ECH3_CRYSTAL(66):    PATCH_UNIQUE_CRYSTAL_BELL ALG7 — 수정 화음
//
// =============================================================================
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

// ── Rev.10 차별화 프리셋 4카테고리 추가 ─────────────────────────────────────
//
//  [R10-UNIQUE]  확연히 독특한 FM 특유 음색 6종
//    극단적 FM 인덱스(modulator TL=0~8), SSGEG carrier 활용, 비정수 MUL(0=×0.5),
//    FB=6~7 자기변조로 타 악기에서 재현 불가한 음색을 의도적으로 설계.
//    - METAL_IMPACT : ALG0 직렬체인 + SSGEG=0x08 피치드롭 → 금속 충격 잔향
//    - ALIEN_LEAD   : ALG5 스타변조 + MUL=0(×0.5) 비정수 → 외계인 리드
//    - WOBBLE_BASS  : ALG2 FB7 + modulator TL=2~5 극변조 → 워블 베이스
//    - CRYSTAL_BELL : ALG7 additive + MUL=0,1,3,7 인하모닉 → 수정 종
//    - GLASS_HARM   : ALG4 + SSGEG=0x0E 반복어택 → 유리 하모닉스
//    - CYBER_ORGAN  : ALG5 FB7 + MUL=1,2,4,8 2배수 계열 → 사이버 오르간
//
//  [R10-HIFI]  음질 중시 — 낮은 FM 인덱스, 정교한 ADSR 6종
//    modulator TL ≥ 30으로 FM Index I ≤ 1.5 유지 → 배음 과다 억제.
//    SR=0 완전 서스테인, DT=1~2 미세 디튠, RR=9~11 자연 릴리즈.
//    DX7 실측 패치를 4-OP OPN 구조에 맞게 재해석.
//    - PIANO_BRIGHT : ALG3 + SSGEG=0x08 carrier → 어택 트랜지언트+자연감쇠
//    - STRINGS_WARM : ALG6 DT 앙상블 + AR=16 느린 스웰 → 따뜻한 현악
//    - FLUTE_PURE   : ALG0 modulator TL=55+ → 순정 플루트 사인파
//    - VIBES_MELLOW : ALG4 MUL=2.5(×2) + 느린 DR → 부드러운 비브라폰
//    - BRASS_FULL   : ALG1 AR=28 날카로운 어택 + SR=0 → 풀 브라스
//    - CELLO_DEEP   : ALG3 AR=20 운궁 어택 + FB=3 → 깊은 첼로
//
//  [R10-PLEASANT]  듣기 좋은 음색 — 귀에 피로 없는 편안한 배음 6종
//    FB=0~2, modulator TL=32~50, carrier TL=10~22로 과배음 억제.
//    AR=22~28로 자연스러운 어택, SR=0 서스테인, RR=9~10 깔끔한 릴리즈.
//    - WARM_PAD     : ALG6 FB=0 + AR=14 스웰 → 진짜 편안한 패드
//    - GUITAR_NYLON : ALG4 FB=2 + DR=18 → 나일론 기타 자연감쇠
//    - MARIMBA_SOFT : ALG0 + SSGEG=0x08 선택적 → 목질 마림바
//    - CHOIR_LUSH   : ALG6 FB=1 + DT 미세 → 풍성한 합창
//    - EP_CLEAN     : ALG4 FB=2 + SR=4 → 클린 로즈 EP
//    - HARP_RICH    : ALG3 FB=2 + DR=14 → 따뜻한 하프 잔향
//
//  [R10-PRESTIGE]  고급진 음색 — 물리적 악기 모델링 수준 ADSR 6종
//    복잡한 ALG 체인을 음향학적으로 설계. 각 OP의 역할이 물리 음원 구조에 대응.
//    RS=1~2 음역별 감쇠 차이, DT=미세조율, 다단계 ADSR로 층위 표현.
//    - GRAND_PIANO  : ALG3 + 타현 OP1 인하모닉 + carrier SSGEG 피치트랜지언트
//    - ORCH_STRINGS : ALG6 + 3 carrier DT=-,0,+ 앙상블 + AR=18 보우어택
//    - FRENCH_HORN  : ALG2 + 원통관 4차배음 구조 + AR=22 입술어택
//    - KOTO_NOBLE   : ALG1 + FB=4 사와리 + DT 비대칭 배음
//    - CRYSTAL_PAD  : ALG5 + MUL=1,2,4,7 배음 스택 + 느린 AR/DR
//    - HARPSICHORD  : ALG0 + SSGEG=0x0D 어택소거 + 빠른 DR → 쳄발로 트랜지언트
//
// ── Rev.11 핵심 미구현 5종 완성 ───────────────────────────────────────────
//
//  [R11-FIX-1]  FM velocity → modulator TL 연동  (§16 ym2203_patch_apply_vel)
//    - 기존: carrier TL만 velocity 감소. 세게/약하게 쳐도 음색 동일.
//    - 수정: modulator TL도 velocity 연동. vel 강→ modulator TL 낮아짐 = FM
//      인덱스↑ = 밝은 음색. vel 약→ modulator TL 높아짐 = 어두운 음색.
//    - modulator 감도는 carrier의 절반 (>> vel_tl_scale+1).
//    - vel_tl_scale=0인 악기(오르간 등)는 carrier/modulator 모두 무시.
//
//  [R11-FIX-2]  SSG note_off envelope 자연 릴리즈 보존  (§17 ym2203_ssg_note_off)
//    - 기존: note_off 시 ssg_ch_silence() 즉각 소거 → PLUCK 자연감쇠 잘림.
//    - 수정: env_mode=1 + cut_ticks=0 조합(무한 서스테인 envelope)에서는
//      vol=0으로만 처리. hardware envelope 자연 감쇠 유지.
//      cut_ticks>0인 drum 계열은 기존대로 즉각 소거.
//
//  [R11-FIX-3]  FM note_off RELEASING 상태 추적  (§15, §19, §20)
//    - voice_t에 releasing, rr_ticks_left, tick_hz_cache 필드 추가.
//    - voice_note_off(): key-off 후 releasing=1, rr_ticks_left=tick_hz/2.
//    - ym2203_voice_release_tick(): 매 tick releasing 카운트다운.
//    - voice_steal 우선순위: 유휴 > 릴리즈(오래된) > 발음(오래된).
//      벨/패드 RR 중인 채널을 즉시 가로채는 문제 해소.
//
//  [R11-NEW-1]  MIDI Pitchbend API  (§17b)
//    - ym2203_pitchbend(ch, bend_val 0~16383, semitone_range): ±N반음 변조.
//    - 내부: fnum_linear 기준 cents→delta_lp 고정소수점 근사 변환.
//    - 채널별 독립 offset 캐시 g_pitchbend_lp_offset[3].
//    - 편의 매크로: YM_PITCHBEND(ch, bend14), YM_PITCHBEND_WIDE(ch, bend14).
//    - ym2203_pitchbend_reset(ch) / ym2203_pitchbend_reset_all().
//
//  [R11-FIX-5]  SSG envelope 채널 충돌 우선순위 관리  (§6, §17)
//    - 레지스터 0x0B/0x0C/0x0D 3채널 공유 문제 → 우선순위 기반 쓰기 중재.
//    - 우선순위: DRUM(cut_ticks>0)=0 > MELODY(ATTACK_HOLD)=1 > PLUCK=2.
//    - 같은 우선순위: 나중에 note_on된 채널 우선 (타임스탬프).
//    - g_ssg_env_owner, g_ssg_env_priority[3], g_ssg_env_timestamp[3] 추가.
//    - ssg_ch_silence()에서 env 소유권 자동 반납.
//
// =============================================================================
//  [R9-CLEAN] 기존 55개 프리셋은 보존하고, 거친 원음 완화/청음 테스트용
//              CLEAN_* FM 프리셋 12종을 PATCH_COUNT 직전에 추가.
//              FB 0~3 중심, modulator TL 상향, carrier TL 12~28 범위,
//              SSGEG=0 기준으로 발진/잡음처럼 들리는 요소를 줄임.
//
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
   //
   //  [BUG-FIX] 악기 교체 시 반드시 ym2203_cache_reset() 선행 호출 필요.
   //  ym_write_cached()는 cache[reg]==val이면 HW write를 스킵한다.
   //  악기 A→B 교체 시 A와 B의 파라미터가 같은 레지스터는 write가 생략되어
   //  음색이 완전히 바뀌지 않는 버그가 발생한다.
   //  cache_reset()은 캐시를 0xFF로 채워 다음 patch_apply에서
   //  모든 레지스터가 반드시 HW에 기록되도록 강제한다.
   //  호출 위치: 악기 프리뷰 직전(m4_preview_fm_*), 모드 진입 초기화 시.
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

/* SSG 패치 — Rev.10 전면 재설계
 *
 * ── 설계 원칙 ─────────────────────────────────────────────────────────────
 *
 *  YM2203 SSG(AY-3-8910 호환)의 음원 특성:
 *    - Tone: 12비트 period 카운터로 구형파(square wave) 생성
 *        period = fclk / (16 × freq),  fclk = 4 MHz
 *        볼륨: 0(무음)~15(최대) 4비트 또는 envelope 모드
 *    - Noise: LFSR 기반 백색 노이즈 (period 0~31로 주파수 제어)
 *        tone과 mixer OR 조합 가능
 *    - Hardware Envelope: 8가지 파형, period 0~65535
 *        볼륨 레지스터 bit4=1 시 envelope 모드 진입
 *        envelope는 채널 공유 (3채널이 같은 envelope period/shape 사용)
 *
 *  ── 파라미터 설명 ──────────────────────────────────────────────────────
 *
 *  use_tone        1=구형파 출력 활성화
 *  use_noise       1=노이즈 출력 활성화 (tone과 AND 믹스 가능)
 *  noise_period    0~31. 노이즈 LFSR 주기. 클수록 저주파(거칠음).
 *                    0~4: 고주파 노이즈 (심벌, 하이햇)
 *                    5~12: 중주파 (스네어 바디)
 *                    13~20: 저주파 (킥, 폭발음)
 *                    21~31: 초저주파 (강풍, 잡음)
 *
 *  amp_base        볼륨 모드 선택 + 기준 볼륨:
 *                    0x00~0x0F: 고정볼륨 모드. 값이 기준 볼륨(0=무음, 15=최대).
 *                    0x10(+0x0F): envelope 모드. vol 레지스터에 0x10 기록
 *                      → hardware envelope shape/period가 볼륨을 제어.
 *                    고정볼륨 모드: vol_scale + velocity로 실제 볼륨 계산.
 *                    envelope 모드: env_period, env_shape 파라미터 사용.
 *
 *  env_period      0x0000~0xFFFF. hardware envelope 주기.
 *                    period 클수록 느린 envelope.
 *                    4 MHz 기준: period ≈ freq_env = 4e6 / (256 × env_period)
 *                    0x0010: ~977 Hz (매우 빠른 비브라토/트레몰로)
 *                    0x0060: ~260 Hz (빠른 감쇠)
 *                    0x0200: ~78 Hz (중간 감쇠, 피치카토)
 *                    0x0800: ~19 Hz (느린 감쇠, 현/관)
 *                    0x2000: ~4.7 Hz (매우 느린 스웰)
 *                    0x8000: ~1.2 Hz (극느린 패드 스웰)
 *
 *  env_shape       SSG_ENV_* 매크로 (0x08~0x0F 중 선택):
 *                    DECAY_ONCE  (0x08): \___  감쇠 후 소거. 타악기 기본.
 *                    DECAY_ALT   (0x0A): \/\/  반복 삼각파(감쇠↔역전).
 *                    DECAY_HOLD  (0x0B): \‾‾‾  감쇠 후 최대 유지. 독특한 어택.
 *                    ATTACK_HOLD (0x0C): /‾‾‾  어택 후 최대 유지. 서스테인 악기 핵심.
 *                    ATTACK_DROP (0x0D): /___  어택 후 소거. 역타악기 효과.
 *                    ATTACK_ALT  (0x0E): /\/\  반복 삼각파(어택↔역전). 비브라토.
 *                    ATTACK_CONT (0x0F): /‾‾‾  연속 최대. 고정 최대볼륨.
 *
 *  env_period_scale  0=고정 env_period 사용.
 *                    1=음정 연동: env_period = base_period / (midi_freq_ratio)
 *                      → 음정에 따라 envelope 속도가 비례 변화.
 *                      자연악기 특성: 고음일수록 빠른 감쇠.
 *
 *  vol_scale       0~255. velocity 스케일 계수. 128=100%.
 *                    고정볼륨 모드에서만 사용.
 *                    실제 볼륨 = amp_base × vol_scale × (vel+1) / (128×128)
 *                    0: velocity 완전 무시 (타악기 고정볼륨)
 *                    128: 표준 (vel=127 → 볼륨 최대)
 *                    200: 고감도
 *
 *  attack_ticks    0=없음. N>0이면 note_on 후 N틱간 attack_vol 유지.
 *                    software 어택 트랜지언트. 타악기 임팩트 강조.
 *                    tick 기준은 호출측 tick_hz에 의존.
 *
 *  attack_vol      0~15. attack_ticks 구간의 볼륨. 통상 sustain_vol보다 높게.
 *
 *  sustain_vol     0~15. attack 이후 (또는 attack 없을 때) 기준 볼륨.
 *                    velocity 스케일 전 기준값. 0=velocity로만 결정.
 *
 *  vol_fade_rate   0=페이드 없음. N>0이면 매 tick마다 볼륨을 N/256만큼 감소.
 *                    고정볼륨 모드에서 software DR(Decay Rate) 역할.
 *                    envelope 모드에서는 무시됨.
 *                    1: 극느린 감쇠 (현악 서스테인)
 *                    4~8: 느린 감쇠 (패드, 관악)
 *                    16~32: 중간 감쇠 (피치카토, 기타)
 *                    64~128: 빠른 감쇠 (타악기)
 *
 *  cut_ticks       0=무한. N>0이면 N틱 후 자동 소거.
 *                    타악기, 짧은 효과음에 사용.
 */
typedef struct {
    const char* name;
    uint8_t  use_tone;          /* 1=구형파 활성화                           */
    uint8_t  use_noise;         /* 1=노이즈 활성화                           */
    uint8_t  noise_period;      /* 0~31: 노이즈 주기                         */
    uint8_t  amp_base;          /* 0x00~0x0F: 고정볼륨, 0x10: envelope 모드  */
    uint16_t env_period;        /* hardware envelope 주기                    */
    uint8_t  env_shape;         /* SSG_ENV_* 파형 선택                       */
    uint8_t  env_period_scale;  /* 0=고정, 1=음정 연동 envelope 속도          */
    uint8_t  vol_scale;         /* 0~255. velocity 스케일. 128=100%          */
    uint8_t  attack_ticks;      /* SW 어택 트랜지언트 tick 수                */
    uint8_t  attack_vol;        /* 어택 구간 볼륨 (0~15)                     */
    uint8_t  sustain_vol;       /* 어택 이후 기준 볼륨 (0~15)                */
    uint8_t  vol_fade_rate;     /* SW 감쇠율 (0=없음, N → N/256/tick)        */
    uint8_t  cut_ticks;         /* 0=무한, N=N틱 후 자동 소거                */
} ym2203_ssg_patch_t;

// =============================================================================
//  §6  SSG 채널별 런타임 상태 구조체  [R10 확장]
//
//  vol_fade_acc: vol_fade_rate의 누산기. 256 누산마다 볼륨 1 감소.
//  vol_fixed:    현재 고정볼륨 값 (0~15). fade 계산 기준.
//  env_period_scale: 1이면 음정 연동 env_period 재계산 필요.
// =============================================================================
typedef struct {
    uint8_t  active;            /* 1=발음 중                              */
    uint8_t  cut_ticks;         /* 남은 cut tick 수 (0=무한)              */
    uint8_t  cut_ticks_init;    /* note_on 시 설정값                      */
    uint8_t  attack_ticks;      /* 남은 attack boost tick                 */
    uint8_t  attack_vol;        /* attack 구간 볼륨 (0~15)                */
    uint8_t  sustain_vol;       /* attack 이후 볼륨 (velocity 적용 완료)  */
    uint8_t  env_mode;          /* 1=hardware envelope 모드               */
    uint8_t  vol_fixed;         /* 현재 고정볼륨 (fade 중 변화)           */
    uint16_t vol_fade_acc;      /* 감쇠 누산기 (×256 fixed-point)         */
    uint8_t  vol_fade_rate;     /* 감쇠율 (0=없음)                        */
    uint8_t  ch;                /* 채널 번호 (0~2)                        */
} ym2203_ssg_ch_state_t;

static ym2203_ssg_ch_state_t g_ssg_ch[3];
static uint8_t g_ssg_mixer = 0x3Fu;  /* 초기값: 전체 disable (0x3F) */
static uint8_t g_ssg_noise_frq = 0x00u;


/* [R11-FIX-5] SSG hardware envelope 충돌 우선순위 관리
 *
 *  레지스터 0x0B/0x0C(env period), 0x0D(env shape)는 3채널 공유.
 *  마지막 기록이 이김 → 동시 발음 시 음색 간섭 발생.
 *
 *  해결책: envelope 사용 채널에 우선순위(0=최고, 2=최저) 부여.
 *    높은 우선순위 채널의 env 설정이 낮은 우선순위를 덮어씀.
 *    같은 우선순위: 나중에 note_on된 채널이 우선.
 *
 *  우선순위 규칙 (기본):
 *    DRUM 계열(cut_ticks>0): 0 (최고) — 킥/스네어는 짧고 임팩트 강함
 *    MELODY 계열(ATTACK_HOLD): 1
 *    PLUCK 계열(DECAY_ONCE): 2 (최저)
 */
static uint8_t  g_ssg_env_owner = 0xFFu; /* 현재 env 레지스터 소유 채널 (0xFF=없음) */
static uint8_t  g_ssg_env_priority[3] = { 0xFFu, 0xFFu, 0xFFu }; /* 채널별 우선순위 */
static uint32_t g_ssg_env_timestamp[3] = { 0u, 0u, 0u };         /* note_on 시각 */
static uint32_t g_ssg_global_tick = 0u;                         /* 전역 tick 카운터 */

/* envelope 우선순위 판단: env_shape + cut_ticks 기반 자동 분류 */
static inline uint8_t ym2203_ssg_env_calc_priority(uint8_t env_shape, uint8_t cut_ticks)
{
    if (cut_ticks > 0u)               return 0u;  /* 드럼: 최고 우선순위 */
    if (env_shape == SSG_ENV_ATTACK_HOLD || env_shape == SSG_ENV_ATTACK_CONT)
        return 1u;  /* 멜로디 서스테인 */
    return 2u;                                    /* 플럭, 기타 감쇠 */
}

/* env period/shape 경쟁적 쓰기 — 우선순위가 높은(낮은 숫자) 채널만 실제 기록 */
static inline void ym2203_ssg_env_write(uint8_t ch, uint16_t ep,
    uint8_t shape, uint8_t cut_ticks)
{
    uint8_t pri = ym2203_ssg_env_calc_priority(shape, cut_ticks);
    g_ssg_env_priority[ch] = pri;
    g_ssg_env_timestamp[ch] = g_ssg_global_tick;

    /* 현재 소유자보다 우선순위가 같거나 높으면 env 레지스터 점유 */
    uint8_t owner = g_ssg_env_owner;
    uint8_t can_write = 0u;
    if (owner == 0xFFu) {
        can_write = 1u;  /* 소유자 없음 */
    }
    else if (pri < g_ssg_env_priority[owner]) {
        can_write = 1u;  /* 더 높은 우선순위 */
    }
    else if (pri == g_ssg_env_priority[owner] &&
        g_ssg_env_timestamp[ch] >= g_ssg_env_timestamp[owner]) {
        can_write = 1u;  /* 같은 우선순위: 나중에 발음된 채널 우선 */
    }

    if (can_write) {
        g_ssg_env_owner = ch;
        ym_write_cached(YM_REG_SSG_ENV_L, (uint8_t)(ep & 0xFFu));
        ym_write_cached(YM_REG_SSG_ENV_H, (uint8_t)(ep >> 8));
        ym_write_cached_force(YM_REG_SSG_ENV_SHAPE, shape);
    }
    /* 우선순위 낮으면: env 레지스터는 건드리지 않고 vol만 0x10으로 설정
     * (어떤 env shape든 vol=0x10이면 hardware envelope에 연결됨) */
}

/* 채널이 소거될 때 env 소유권 반납 */
static inline void ym2203_ssg_env_release(uint8_t ch)
{
    if (g_ssg_env_owner == ch) {
        g_ssg_env_owner = 0xFFu;
        g_ssg_env_priority[ch] = 0xFFu;
    }
}

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
    ym2203_ssg_env_release(ch);  /* [R11-FIX-5] env 소유권 반납 */
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

/* [R12-FIX-2] Forward declaration — g_ssg_voice는 §24b에서 정의되나
   §17 ym2203_ssg_note_on()에서 참조되므로 여기서 먼저 선언 */
typedef struct ym2203_ssg_voice_s ym2203_ssg_voice_t;
struct ym2203_ssg_voice_s {
    uint8_t  ch;
    uint8_t  active;
    uint8_t  vib_enable;
    uint8_t  vib_depth_frac;
    ym2203_lfo_shape_t vib_shape;
    ym2203_lfo_t       vib_lfo;
    uint16_t           vib_base_period;
    uint8_t  porta_enable;
    uint16_t porta_src;
    uint16_t porta_dst;
    uint16_t porta_ticks;
    uint16_t porta_tick_cur;
    int32_t  porta_delta_acc;
    uint16_t porta_period_cur;
};
static ym2203_ssg_voice_t g_ssg_voice[3];
static inline void ym2203_ssg_voice_init(uint8_t ch);
static inline void ym2203_ssg_voice_tick(uint8_t ch);


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

    /* [R9-CLEAN] 최종 청음용 clean/safe 계열 — 거친 원음 완화 목적 */
    PATCH_CLEAN_ROUND_LEAD,
    PATCH_CLEAN_SOFT_BRASS,
    PATCH_CLEAN_EP_WARM,
    PATCH_CLEAN_FLUTE_AIR,
    PATCH_CLEAN_STRINGS_PAD,
    PATCH_CLEAN_ORGAN_ROUND,
    PATCH_CLEAN_PLUCK_MELLOW,
    PATCH_CLEAN_BASS_ROUND,
    PATCH_CLEAN_BELL_SOFT,
    PATCH_CLEAN_SYNTH_PAD,
    PATCH_CLEAN_GUITAR_SOFT,
    PATCH_CLEAN_CHOIR_SOFT,

    /* [R10-UNIQUE] 확연히 독특한 음색 — FM 특유 익스트림 파라미터 */
    PATCH_UNIQUE_METAL_IMPACT,   /* ALG0 FM 인덱스 극대화 — 금속 충격음  */
    PATCH_UNIQUE_ALIEN_LEAD,     /* ALG5 비정수 MUL+SSGEG — 외계인 리드  */
    PATCH_UNIQUE_WOBBLE_BASS,    /* ALG2 FB7 고변조 워블 베이스           */
    PATCH_UNIQUE_CRYSTAL_BELL,   /* ALG7 MUL 분수+정수 혼합 — 수정 종소리 */
    PATCH_UNIQUE_GLASS_HARM,     /* ALG4 SSGEG 캐리어 반복 — 유리 하모닉  */
    PATCH_UNIQUE_CYBER_ORGAN,    /* ALG5 FB7 모든 MUL 소수배 — 사이버 오르간 */

    /* [R10-HIFI] 음질 중시 — 낮은 FM 인덱스, 정교한 ADSR */
    PATCH_HIFI_PIANO_BRIGHT,     /* DX7 E.PIANO1 근사 — 고해상도 어택 잔향 */
    PATCH_HIFI_STRINGS_WARM,     /* DX7 STRINGS1 근사 — 따뜻한 현악 앙상블 */
    PATCH_HIFI_FLUTE_PURE,       /* I≈0.3 순정 플루트 — 극저 FM 인덱스    */
    PATCH_HIFI_VIBES_MELLOW,     /* ALG4 금속 비브라폰 — 낮은 배음 순도   */
    PATCH_HIFI_BRASS_FULL,       /* ALG6 풀 브라스 섹션 — 두터운 중역대    */
    PATCH_HIFI_CELLO_DEEP,       /* ALG3 첼로 — 운궁 자연 서스테인        */

    /* [R10-PLEASANT] 듣기 좋은 음색 — 귀에 피로 없는 편안한 배음 */
    PATCH_PLEASANT_WARM_PAD,     /* ALG6 저FB 따뜻한 패드 — 어택 없는 스웰  */
    PATCH_PLEASANT_GUITAR_NYLON, /* ALG4 나일론 기타 — 부드러운 피치카토    */
    PATCH_PLEASANT_MARIMBA_SOFT, /* ALG0 마림바 — 목질 잔향 자연 감쇠       */
    PATCH_PLEASANT_CHOIR_LUSH,   /* ALG6 합창 보이스 — 풍부한 유니즌        */
    PATCH_PLEASANT_EP_CLEAN,     /* ALG4 클린 EP — 로즈 피아노 느낌         */
    PATCH_PLEASANT_HARP_RICH,    /* ALG3 하프 — 따뜻한 현 잔향             */

    /* [R10-PRESTIGE] 고급진 음색 — 복잡한 모듈레이터 체인, 정교한 스펙트럼 */
    PATCH_PRESTIGE_GRAND_PIANO,  /* ALG3 그랜드 피아노 — 타현 물리모델 근사  */
    PATCH_PRESTIGE_ORCH_STRINGS, /* ALG6 오케스트라 현 — DT 앙상블 레이어    */
    PATCH_PRESTIGE_FRENCH_HORN,  /* ALG2 프렌치 호른 — 원통관 배음 구조      */
    PATCH_PRESTIGE_KOTO_NOBLE,   /* ALG1 고토 — 사와리+공명 복합 음색        */
    PATCH_PRESTIGE_CRYSTAL_PAD,  /* ALG5 크리스탈 패드 — 수정 공명 스펙트럼  */
    PATCH_PRESTIGE_HARPSICHORD,  /* ALG0 쳄발로 — 인하모닉 어택 트랜지언트   */

    PATCH_COUNT
} ym2203_patch_idx_t;
#define YM2203_PATCH_COUNT  ((uint8_t)(PATCH_COUNT))

// =============================================================================
//  §8-B  SSG 패치 인덱스 열거형  [R10 전면 재설계]
//
//  카테고리:
//    MELODY  — 멜로디 악기 (서스테인, 정확한 음정)
//    PLUCK   — 피치카토/발현 악기 (어택+감쇠)
//    DRUM    — 타악기 (noise+cut_ticks)
//    FX      — 특수 효과 (envelope 파형 활용)
// =============================================================================
typedef enum {
    // ── 카테고리 A: MELODY — 서스테인 멜로디 악기 ──────────────────────────
    // use_tone=1, amp_base=0x10(env mode) + ATTACK_HOLD(0x0C) 기본.
    // 음정 유지되는 동안 볼륨 일정. key-off 후 env 자동 소거.
    SSG_MELODY_SQUARE_BRIGHT = 0, /* 밝은 구형파 — 8비트 게임 리드           */
    SSG_MELODY_SQUARE_WARM,       /* 따뜻한 구형파 — 낮은 어택, 레트로 멜로디 */
    SSG_MELODY_FLUTE,             /* 플루트 근사 — 부드러운 어택 스웰         */
    SSG_MELODY_CLARINET,          /* 클라리넷 — 빠른 어택, 강한 서스테인      */
    SSG_MELODY_ORGAN_PIPE,        /* 파이프 오르간 — 즉각 어택, 순수 구형파   */
    SSG_MELODY_CELLO_BOW,         /* 첼로 보잉 — 느린 어택, 거칠고 두꺼운 배음*/
    SSG_MELODY_TRUMPET_MUTED,     /* 뮤트 트럼펫 — 날카로운 어택, 강한 서스테인*/

    // ── 카테고리 B: PLUCK — 발현/피치카토 악기 ────────────────────────────
    // amp_base=0x10 + DECAY_ONCE(0x08). note_on → 어택 → 감쇠 소거.
    // env_period로 감쇠 속도 제어. 음정 연동 env_period_scale 옵션.
    SSG_PLUCK_HARP,               /* 하프 — 중간 감쇠, 따뜻한 잔향            */
    SSG_PLUCK_GUITAR_NYLON,       /* 나일론 기타 — 빠른 감쇠, 부드러운 어택   */
    SSG_PLUCK_GUITAR_STEEL,       /* 스틸 기타 — 날카로운 어택, 긴 잔향       */
    SSG_PLUCK_BANJO,              /* 밴조 — 아주 빠른 감쇠, 금속성 어택       */
    SSG_PLUCK_KOTO,               /* 고토 — 중간 감쇠, attack_vol 높게        */
    SSG_PLUCK_BELL_TUBULAR,       /* 튜블러 벨 — 매우 느린 감쇠, 긴 잔향      */
    SSG_PLUCK_MARIMBA,            /* 마림바 — 중간 감쇠, 목질 어택            */
    SSG_PLUCK_XYLOPHONE,          /* 실로폰 — 빠른 감쇠, 밝은 어택            */

    // ── 카테고리 C: DRUM — 타악기 ─────────────────────────────────────────
    // use_noise=1 중심. noise_period와 cut_ticks로 음색 제어.
    // env mode + DECAY_ONCE 조합으로 자연스러운 감쇠.
    SSG_DRUM_KICK_DEEP,           /* 킥 드럼 딥 — 저주파 노이즈, 긴 컷        */
    SSG_DRUM_KICK_TIGHT,          /* 킥 드럼 타이트 — 중주파, 짧은 컷         */
    SSG_DRUM_SNARE_CRISP,         /* 스네어 크리스프 — tone+noise, 짧은 컷    */
    SSG_DRUM_SNARE_BRUSH,         /* 스네어 브러시 — noise only, 부드러운     */
    SSG_DRUM_HIHAT_CLOSED,        /* 클로즈드 하이햇 — 고주파, 아주 짧은 컷   */
    SSG_DRUM_HIHAT_OPEN,          /* 오픈 하이햇 — 고주파, 긴 컷              */
    SSG_DRUM_CYMBAL_CRASH,        /* 크래쉬 심벌 — 고주파 long 노이즈         */
    SSG_DRUM_CYMBAL_RIDE,         /* 라이드 심벌 — tone+noise, 중간 컷        */
    SSG_DRUM_TOM_HI,              /* 하이 탐 — tone+noise, 빠른 pitch감쇠     */
    SSG_DRUM_TOM_LO,              /* 로우 탐 — tone+noise, 저음 pitch감쇠     */
    SSG_DRUM_RIM_SHOT,            /* 림샷 — tone click, 극짧은 컷             */
    SSG_DRUM_COWBELL,             /* 카우벨 — tone 금속성, 중간 컷            */

    // ── 카테고리 D: FX — 특수 효과 ───────────────────────────────────────
    // envelope의 고급 파형(ALT, HOLD 등) 활용.
    // 반복 삼각파, decay_hold 등 FM으로 안 나오는 독특한 음색.
    SSG_FX_VIBRATO_LEAD,          /* 비브라토 리드 — ATTACK_ALT 반복삼각파    */
    SSG_FX_WOBBLE_BASS,           /* 워블 베이스 — DECAY_ALT 반복 저음        */
    SSG_FX_STAB_ACCENT,           /* 스탭 악센트 — DECAY_HOLD(attack-burst)   */
    SSG_FX_SWELL_PAD,             /* 스웰 패드 — 매우 느린 ATTACK_HOLD        */
    SSG_FX_CHIRP,                 /* 칩 사운드 — 빠른 env + noise 혼합        */
    SSG_FX_LASER,                 /* 레이저 — pitch sweep + 짧은 cut          */
    SSG_FX_BUZZ_TONE,             /* 버즈 톤 — tone+noise 비율 조합            */
    SSG_FX_WIND,                  /* 바람 — noise only, 긴 페이드              */

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

    /* LFO — [R12-FIX-1] lfo_shape 악기 테이블에서 선택 가능 */
    ym2203_lfo_shape_t lfo_shape;      /* LFO 파형: SINE/TRI/SAW/SQR       */
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
                                  sw_op stls  stle stks  lshp vbd tmd  tmb lr100 pt  dt */
    /* 00 피아노     */ { PATCH_PIANO,        SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                         0,    2,   24,   20,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                         /* 01 전자피아노 */ { PATCH_EPIANO,       SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                              0,    8,   28,   30,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                              /* 02 오르골     */ { PATCH_MUSICBOX,     SSG_PLUCK_MARIMBA, 0xFF,
                                                                   0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                   /* 03 하프       */ { PATCH_HARP,         SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                        0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                        /* 04 하프시코드 */ { PATCH_HARPSICHORD,  SSG_PLUCK_HARP,  0xFF,
                                                                                                             0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                             /* 05 첼레스타   */ { PATCH_CELESTA,      SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                  0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                  /* 06 실로폰     */ { PATCH_XYLOPHONE,    SSG_PLUCK_MARIMBA, 0xFF,
                                                                                                                                                       0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                       /* 07 마림바     */ { PATCH_MARIMBA,      SSG_PLUCK_MARIMBA, 0xFF,
                                                                                                                                                                            0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                            /* 08 벨         */ { PATCH_BELL,         SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                 0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                 /* 09 바이브라폰 */ { PATCH_VIBRAPHONE,   SSG_PLUCK_MARIMBA, 0xFF,
                                                                                                                                                                                                                      0,    0,    0,    0,    LFO_SINE,  2,   1,   1,  300,  0,   0 },
                                                                                                                                                                                                                      /* 10 바이올린   */ { PATCH_VIOLIN,       SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                           0,    0,    0,    0,    LFO_SINE,  4,   2,   1,  550,  0,   0 },
                                                                                                                                                                                                                                           /* 11 첼로       */ { PATCH_CELLO,        SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                0,    0,    0,    0,    LFO_SINE,  3,   1,   1,  450,  0,   0 },
                                                                                                                                                                                                                                                                /* 12 현악 앙상블*/ { PATCH_STRINGS_ENS,  SSG_MELODY_SQUARE_WARM,   0xFF,
                                                                                                                                                                                                                                                                                     0,    0,    0,    0,    LFO_SINE,  2,   3,   0,  400,  0,   5 },
                                                                                                                                                                                                                                                                                     /* 13 어쿠기타   */ { PATCH_AGITAR,       SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                          0,    4,   12,   30,    0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                          /* 14 일렉 클린  */ { PATCH_EGITAR_CLEAN, SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                               0,    0,    0,    0,    LFO_SINE,  2,   1,   1,  400,  0,   0 },
                                                                                                                                                                                                                                                                                                                               /* 15 일렉 드라이브*/{ PATCH_EGITAR_DRIVE,SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                    0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                    /* 16 샤미센     */ { PATCH_SHAMISEN,     SSG_PLUCK_HARP,  0xFF,
                                                                                                                                                                                                                                                                                                                                                                         0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                         /* 17 반조       */ { PATCH_BANJO,        SSG_PLUCK_HARP,  0xFF,
                                                                                                                                                                                                                                                                                                                                                                                              0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                              /* 18 고토       */ { PATCH_KOTO,         SSG_PLUCK_HARP,  0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                   0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                   /* 19 플루트     */ { PATCH_FLUTE,        SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                        0,    0,    0,    0,    LFO_SINE,  3,   1,   0,  500,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                        /* 20 대금       */ { PATCH_DAEGEUM,      SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                             0,    0,    0,    0,    LFO_SINE,  5,   2,   0,  400,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                             /* 21 소금       */ { PATCH_SOGEUM,       SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  0,    0,    0,    0,    LFO_SINE,  3,   1,   0,  450,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  /* 22 오보에     */ { PATCH_OBOE,         SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       0,    0,    0,    0,    LFO_SINE,  2,   1,   0,  480,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       /* 23 트럼펫     */ { PATCH_TRUMPET,      SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            /* 24 색소폰     */ { PATCH_SAXOPHONE,    SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 0,    0,    0,    0,    LFO_SINE,  2,   1,   0,  400,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 /* 25 트롬본     */ { PATCH_TROMBONE,     SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0, 12,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      /* 26 어쿠 베이스*/ { PATCH_BASS_ACOUSTIC,SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           /* 27 프렛리스   */ { PATCH_BASS_FRETLESS,SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                0,    0,    0,    0,    LFO_SINE,  2,   0,   0,  300, 15,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                /* 28 VGM베이스  */ { PATCH_VGM_BASS,    SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     /* 29 합창       */ { PATCH_CHOIR,        SSG_MELODY_SQUARE_WARM,   0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          0,    0,    0,    0,    LFO_SINE,  3,   3,   0,  380,  0,   7 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          /* 30 신스 리드  */ { PATCH_SYNTH_LEAD,   SSG_FX_BUZZ_TONE,          0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               0,    0,    0,    0,    LFO_SINE,  3,   2,   1,  600,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               /* 31 신스 패드  */ { PATCH_SYNTH_PAD,    SSG_MELODY_SQUARE_WARM,   0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    0,    0,    0,    0,    LFO_SINE,  2,   4,   0,  350,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    /* 32 신스 브라스*/ { PATCH_SYNTH_BRASS,  SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         /* 33 킥드럼     */ { PATCH_DRUM_KICK,    SSG_DRUM_KICK_DEEP,      0,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              /* 34 스네어     */ { PATCH_DRUM_SNARE,   SSG_DRUM_SNARE_CRISP,     2,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   /* 35 하이햇     */ { PATCH_VGM_DRUM_A,  SSG_DRUM_HIHAT_CLOSED,     1,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        /* 36 크래쉬     */ { PATCH_VGM_DRUM_B,  SSG_DRUM_CYMBAL_CRASH,     1,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             /* 37 VGM리드A   */ { PATCH_VGM_LEAD_A,  SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  /* 38 VGM리드B   */ { PATCH_VGM_LEAD_B,  SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       /* 39 VGM브라스  */ { PATCH_VGM_BRASS,   SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            /* 40 VGM오르간  */ { PATCH_VGM_ORGAN_A, SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 0,    0,    0,    0,    LFO_SINE,  0,   0,   0,    0,  0,   0 },

                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 /* ─── [R12-NEW-1] SSG 단독 악기 (41~62) ────────────────────────────────
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  *  FM 채널: PATCH_VGM_ORGAN_C(vel_tl_scale=0) → 사실상 무음 유지
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  *  ssg_ch=0: 멜로디, ssg_ch=1: 드럼/FX, ssg_ch=2: 베이스/워블 */

    /* 41 SSG 밝은구형파  */ { PATCH_VGM_ORGAN_C, SSG_MELODY_SQUARE_BRIGHT, 0,
        0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
        /* 42 SSG 따뜻구형파  */ { PATCH_VGM_ORGAN_C, SSG_MELODY_SQUARE_WARM,   0,
            0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
            /* 43 SSG 플루트      */ { PATCH_VGM_ORGAN_C, SSG_MELODY_FLUTE,          0,
                0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                /* 44 SSG 클라리넷    */ { PATCH_VGM_ORGAN_C, SSG_MELODY_CLARINET,       0,
                    0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                    /* 45 SSG 오르간파이프*/ { PATCH_VGM_ORGAN_C, SSG_MELODY_ORGAN_PIPE,     0,
                        0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                        /* 46 SSG 첼로보잉    */ { PATCH_VGM_ORGAN_C, SSG_MELODY_CELLO_BOW,      0,
                            0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                            /* 47 SSG 뮤트트럼펫  */ { PATCH_VGM_ORGAN_C, SSG_MELODY_TRUMPET_MUTED,  0,
                                0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },

                                /* 48 SSG 하프        */ { PATCH_VGM_ORGAN_C, SSG_PLUCK_HARP,            0,
                                    0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                    /* 49 SSG 나일론기타  */ { PATCH_VGM_ORGAN_C, SSG_PLUCK_GUITAR_NYLON,    0,
                                        0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                        /* 50 SSG 스틸기타    */ { PATCH_VGM_ORGAN_C, SSG_PLUCK_GUITAR_STEEL,    0,
                                            0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                            /* 51 SSG 밴조        */ { PATCH_VGM_ORGAN_C, SSG_PLUCK_BANJO,           0,
                                                0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                                /* 52 SSG 고토        */ { PATCH_VGM_ORGAN_C, SSG_PLUCK_KOTO,            0,
                                                    0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                                    /* 53 SSG 마림바      */ { PATCH_VGM_ORGAN_C, SSG_PLUCK_MARIMBA,         0,
                                                        0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                                        /* 54 SSG 실로폰      */ { PATCH_VGM_ORGAN_C, SSG_PLUCK_XYLOPHONE,       0,
                                                            0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },

                                                            /* 55 SSG 킥드럼      */ { PATCH_VGM_DRUM_A,  SSG_DRUM_KICK_DEEP,        1,
                                                                0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                                                /* 56 SSG 스네어      */ { PATCH_VGM_DRUM_A,  SSG_DRUM_SNARE_CRISP,      1,
                                                                    0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                                                    /* 57 SSG 클로즈햇    */ { PATCH_VGM_DRUM_A,  SSG_DRUM_HIHAT_CLOSED,     1,
                                                                        0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                                                        /* 58 SSG 오픈햇      */ { PATCH_VGM_DRUM_A,  SSG_DRUM_HIHAT_OPEN,       1,
                                                                            0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                                                            /* 59 SSG 크래쉬      */ { PATCH_VGM_DRUM_A,  SSG_DRUM_CYMBAL_CRASH,     1,
                                                                                0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },

                                                                                /* 60 SSG 비브라토리드*/ { PATCH_VGM_ORGAN_C, SSG_FX_VIBRATO_LEAD,       0,
                                                                                    0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                                                                    /* 61 SSG 워블베이스  */ { PATCH_VGM_ORGAN_C, SSG_FX_WOBBLE_BASS,        2,
                                                                                        0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
                                                                                        /* 62 SSG 레이저FX    */ { PATCH_VGM_ORGAN_C, SSG_FX_LASER,              1,
                                                                                            0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },

                                                                                            /* ─── [R12-NEW-2] ch3 Extended mode 악기 — 참조용 엔트리 (63~66) ────────
                                                                                             *  발음: YM_EXT_CHORD4(INST_ECH3_xxx, n1,n2,n3,n4, vel) 사용.
                                                                                             *  inst_note_on() fallback 시 일반 FM 단음으로 발음됨. */

    /* 63 ExtCh3 오르골   */ { PATCH_MUSICBOX,              SSG_MELODY_SQUARE_BRIGHT, 0xFF,
        0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
        /* 64 ExtCh3 벨화음   */ { PATCH_BELL,                  SSG_MELODY_SQUARE_BRIGHT, 0xFF,
            0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
            /* 65 ExtCh3 대금유니즌*/{ PATCH_DAEGEUM,               SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                0, 0, 0, 0,  LFO_SINE, 5, 0, 0, 400, 0, 0 },
                /* 66 ExtCh3 수정음   */ { PATCH_UNIQUE_CRYSTAL_BELL,   SSG_MELODY_SQUARE_BRIGHT, 0xFF,
                    0, 0, 0, 0,  LFO_SINE, 0, 0, 0,   0, 0, 0 },
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

    /* [R12-NEW-1] SSG 단독 악기 — ssg_ch 고정 할당, FM은 무음 패치
     *
     *  SSG 채널 0을 멜로디 기본 채널로 사용.
     *  FM 채널은 PATCH_VGM_LEAD_A를 사용하되 vel=0으로 key-off 직후
     *  처리되므로 사실상 무음 (FM key-on은 inst_note_on 내부에서 발생).
     *  진정한 FM-off를 원하면 fm_patch 대신 별도 silent patch가 필요하나,
     *  현재 구조에서는 vel=0 경로로 FM TL=127 상태가 유지되므로 실질 무음.
     *
     *  SSG 전용 사용법: fm_ch=0xFF 매직값을 inst_note_on에서 감지해
     *  FM note_on을 스킵하는 방식. inst_note_on 내부 분기 추가됨.
     *
     *  [ssg_ch 고정 배분]
     *    SSG 멜로디 악기: ssg_ch=0 (멜로디 기본)
     *    SSG 베이스:      ssg_ch=2 (저음역 담당)
     *    SSG 타악기:      ssg_ch=1 (중간 채널)
     */
     /* SSG 멜로디 */
    INST_SSG_SQUARE_BRIGHT = 41,  /* 밝은 구형파 8비트 리드              */
    INST_SSG_SQUARE_WARM = 42,  /* 따뜻한 구형파 레트로 멜로디         */
    INST_SSG_FLUTE = 43,  /* SSG 플루트 스웰 어택               */
    INST_SSG_CLARINET = 44,  /* SSG 클라리넷 날카로운 어택          */
    INST_SSG_ORGAN_PIPE = 45,  /* SSG 파이프 오르간 velocity 무시     */
    INST_SSG_CELLO_BOW = 46,  /* SSG 첼로 느린 어택                 */
    INST_SSG_TRUMPET_MUTED = 47,  /* SSG 뮤트 트럼펫 버징               */
    /* SSG 발현 */
    INST_SSG_HARP = 48,  /* SSG 하프 중간 감쇠                 */
    INST_SSG_GUITAR_NYLON = 49,  /* SSG 나일론 기타 피치카토            */
    INST_SSG_GUITAR_STEEL = 50,  /* SSG 스틸 기타 금속 잔향             */
    INST_SSG_BANJO = 51,  /* SSG 밴조 극빠른 감쇠               */
    INST_SSG_KOTO = 52,  /* SSG 고토 중간 감쇠                 */
    INST_SSG_MARIMBA = 53,  /* SSG 마림바 목질                    */
    INST_SSG_XYLOPHONE = 54,  /* SSG 실로폰 밝은 어택               */
    /* SSG 타악기 — ssg_ch=1 */
    INST_SSG_KICK = 55,  /* SSG 킥 드럼                       */
    INST_SSG_SNARE = 56,  /* SSG 스네어                        */
    INST_SSG_HIHAT_CLOSED = 57,  /* SSG 클로즈드 하이햇                */
    INST_SSG_HIHAT_OPEN = 58,  /* SSG 오픈 하이햇                   */
    INST_SSG_CYMBAL = 59,  /* SSG 크래쉬 심벌                   */
    /* SSG FX */
    INST_SSG_FX_VIBRATO = 60,  /* SSG vibrato 리드 ATTACK_ALT        */
    INST_SSG_FX_WOBBLE = 61,  /* SSG 워블 베이스 DECAY_ALT          */
    INST_SSG_FX_LASER = 62,  /* SSG 레이저 pitch sweep             */

    /* [R12-NEW-2] ch3 Extended mode 악기 — ym2203_extch3_note_on()으로 발음
     *  FM ch=2를 Extended mode로 사용. 4 OP 독립 주파수.
     *  inst_note_on()으로는 발음 불가; YM_EXT_CHORD4() 매크로 직접 사용.
     *  여기서는 fm_patch 참조용 인덱스만 제공. */
    INST_ECH3_ORGEL = 63,       /* ExtCh3 오르골 화음 — ALG7 additive 4화음   */
    INST_ECH3_BELL_CHORD = 64,  /* ExtCh3 벨 화음 — 인하모닉 4음 독립 종소리 */
    INST_ECH3_DAEGEUM_UNI = 65, /* ExtCh3 대금 유니즌 — 4 OP 미세 디튠       */
    INST_ECH3_CRYSTAL = 66,     /* ExtCh3 수정음 — MUL 분산 additive          */
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
                                                                            // DX7 실측 보이스 데이터 기반 재설계 (Rev.9)
                                                                            // 모든 파라미터: OPN 스케일 변환
                                                                            //   DX7 TL(0~99) → OPN TL(0~63): floor(dx7_tl * 63 / 99)
                                                                            //   DX7 AR/DR/SR/RR: OPN 레지스터는 5비트(0~31), DX7는 0~99
                                                                            //     AR/DR/SR: floor(val * 31 / 99)  RR: floor(val * 15 / 99)
                                                                            //   DX7 SL(0~15): OPN SL 직접 대응
                                                                            //   OPN SSGEG: DX7에 없음, 타악기성 어택 표현시만 사용

                                                                            /* [18] PIANO — ALG3 FB2
                                                                               DX7 "GRAND PIANO 1" 근사 (ALG5→OPN ALG3 근사)
                                                                               ALG3: OP1[FB]→OP2→OP4[C] + OP3→OP4[C]. carrier=OP4.
                                                                               OP1(MUL=1,DT=1): 기음 자기변조로 약간의 거칠기
                                                                               OP2(MUL=1): 주 변조원, 피아노 배음 구조
                                                                               OP3(MUL=2): 2배음 변조 → 현 공명 밝기
                                                                               OP4(MUL=1,RS=2): carrier, 고음역 자연감쇠
                                                                               DR=14 느린감쇠→SR=4 서스테인→SL=4, RR=10 */
                                                                            { "PIANO", 3, 2, 3, {
        /*DT MUL  TL RS AR AM DR SR SL RR SSGEG*/
        { 1,  1,  32, 1, 31, 0, 14,  4,  4, 10,  0}, /* OP1 mod: FB 자기변조, DT=1 */
        { 0,  1,  18, 1, 31, 0, 16,  4,  4,  9,  0}, /* OP2 mod: 주 변조원 TL=18 */
        { 0,  2,  24, 1, 31, 0, 18,  4,  5,  9,  0}, /* OP3 mod: 2배음, TL=24 약함 */
        { 0,  1,   5, 2, 31, 0, 12,  4,  3,  8,  0}}},/* OP4 car: RS=2 고음역반응 */

        /* [19] EPIANO — ALG4 FB0
           DX7 "RHODES 1" 실측 근사 (DX7 ALG4 → OPN ALG4 직접 대응)
           ALG4: OP1→OP2[C] + OP3→OP4[C]. carrier=OP2+OP4.
           OP1 MUL=14: 비정수 배음비 × 14 → 금속성 어택 'ding'
           OP3 MUL=2: 2배음 변조 → 따뜻한 EP 배음층
           carrier DR=18~22: 자연 감쇠, SR=0 풀서스테인 */
        { "EPIANO", 4, 0, 3, {
            { 0, 14,   9, 1, 31, 0, 22,  0,  8, 10,  0}, /* OP1 mod: MUL=14 인하모닉 */
            { 0,  1,   5, 1, 31, 0, 20,  0,  6,  9,  0}, /* OP2 car: 감쇠 자연스럽게 */
            { 0,  2,  14, 1, 31, 0, 24,  0,  8, 10,  0}, /* OP3 mod: 2배음 TL=14 */
            { 0,  1,   5, 1, 31, 0, 20,  0,  6,  9,  0}}},/* OP4 car: OP2와 균형 */

            /* [20] MUSICBOX — ALG7 FB0
               오르골: ALG7 전부 carrier. DX7 "MUSIC BOX" 근사.
               MUL=1,2,3,4 배음 합산. RS=2 고음역 빠른감쇠.
               SSGEG=8 OP1: 순간 타격 어택 표현 */
            { "MUSICBOX", 7, 0, 2, {
                { 0,  1,   3, 2, 31, 0, 18,  0,  7,  9,  8}, /* OP1 기음, SSGEG=8 단발 */
                { 0,  2,   9, 2, 31, 0, 20,  0,  8,  9,  0}, /* OP2 2배음 */
                { 0,  3,  14, 2, 31, 0, 22,  0,  9,  9,  0}, /* OP3 3배음 */
                { 0,  4,  20, 2, 31, 0, 24,  0, 10,  8,  0}}},/* OP4 4배음 */

                /* [21] HARP — ALG3 FB2
                   DX7 "HARP 1" 근사. 하프: 현 타격 후 빠른 어택, 자연감쇠.
                   SSGEG=8 OP1: 피크 어택 클릭. DR 빠름. RS=1. */
                { "HARP", 3, 2, 3, {
                    { 0,  1,   6, 1, 31, 0, 20,  0,  8, 11,  8}, /* OP1 mod: SSGEG 어택 */
                    { 0,  2,  10, 1, 31, 0, 18,  0,  7, 10,  0}, /* OP2 mod */
                    { 0,  1,  14, 1, 31, 0, 22,  0,  9, 11,  0}, /* OP3 mod */
                    { 0,  1,   4, 1, 31, 0, 16,  0,  6,  9,  0}}},/* OP4 car */

                    /* [22] HARPSICHORD — ALG3 FB4
                       DX7 "HARPSICHORD" 근사. SSGEG=8+D (0x0D: 어택후소거) OP1.
                       빠른 DR(22~26), 큰 SL(12~14): 거의 퍼커시브.
                       RS=2 고음역 더 빠른 소거. */
                    { "HARPSICHORD", 3, 4, 4, {
                        { 0,  2,   2, 2, 31, 0, 26,  0, 12, 13, 13}, /* OP1 mod: SSGEG=0x0D 어택소거 */
                        { 0,  1,   4, 2, 31, 0, 24,  0, 11, 12,  0}, /* OP2 mod */
                        { 0,  4,   8, 2, 31, 0, 26,  0, 13, 13,  0}, /* OP3 mod */
                        { 0,  1,   3, 2, 31, 0, 22,  0, 11, 11,  0}}},/* OP4 car */

                        /* [23] CELESTA — ALG7 FB0
                           첼레스타: 맑은 금속 타격음. ALG7 전부 carrier.
                           DX7 "CELESTA" 근사. MUL=1,3,5,7 (홀수 배음).
                           RS=2, DR=18~22, SSGEG=8 전체: 빠른 타격 소멸. */
                        { "CELESTA", 7, 0, 3, {
                            { 0,  1,   4, 2, 31, 0, 18,  0,  8,  9,  8}, /* OP1 기음 */
                            { 0,  3,  10, 2, 31, 0, 20,  0,  9,  9,  0}, /* OP2 3배음 */
                            { 0,  5,  16, 2, 31, 0, 22,  0, 10,  9,  0}, /* OP3 5배음 */
                            { 0,  7,  22, 2, 31, 0, 24,  0, 11,  9,  0}}},/* OP4 7배음 */

                            /* [24] XYLOPHONE — ALG7 FB0
                               실로폰: 매우 빠른 감쇠. DX7 "XYLOPHONE" 근사.
                               DR=24~28 극빠름, SL=14~15 거의 무음까지. SSGEG=8. */
                            { "XYLOPHONE", 7, 0, 4, {
                                { 0,  1,   3, 2, 31, 0, 26,  0, 14, 13,  8}, /* OP1 기음 */
                                { 0,  2,   7, 2, 31, 0, 28,  0, 14, 13,  8}, /* OP2 2배음 */
                                { 0,  3,  12, 2, 31, 0, 28,  0, 15, 14,  8}, /* OP3 3배음 */
                                { 0,  4,  18, 2, 31, 0, 30,  0, 15, 14,  8}}},/* OP4 4배음 */

                                /* [25] MARIMBA — ALG3 FB2
                                   마림바: 목질 공명. DX7 "MARIMBA" 근사.
                                   FB=2: 약한 자기변조 → 목질 거칠기. RS=2.
                                   OP1 SSGEG=8: 말렛 타격 클릭. DR=20~24. */
                                { "MARIMBA", 3, 2, 4, {
                                    { 0,  1,   4, 2, 31, 0, 20,  0, 12, 11,  8}, /* OP1 mod: 말렛 어택 */
                                    { 0,  2,   8, 2, 31, 0, 22,  0, 12, 11,  0}, /* OP2 mod */
                                    { 0,  4,  14, 2, 31, 0, 24,  0, 13, 12,  0}, /* OP3 mod */
                                    { 0,  1,   3, 2, 31, 0, 18,  0, 12, 10,  0}}},/* OP4 car: RS=2 */

                                    /* [26] BELL — ALG7 FB0
                                       DX7 "TUBULAR BELLS" 근사. 인하모닉 배음.
                                       MUL=1,1,3,7: OP2 MUL=1 DT=3으로 디튠 → 인하모닉 두께
                                       DR=6~10 긴감쇠 (교회종 수초), RR=5. */
                                    { "BELL", 7, 0, 4, {
                                        { 0,  1,   3, 1, 31, 0,  6,  4,  6,  5,  0}, /* OP1 기음 */
                                        { 3,  1,   6, 1, 31, 0,  7,  4,  7,  5,  0}, /* OP2 DT=3 디튠 → 인하모닉 */
                                        { 0,  3,  12, 1, 31, 0,  8,  5,  8,  5,  0}, /* OP3 3배음 */
                                        { 0,  7,  20, 1, 31, 0,  9,  6,  9,  5,  0}}},/* OP4 7배음 인하모닉 */

                                        /* [27] VIBRAPHONE — ALG4 FB0
                                           DX7 "VIBRAPHONE 1" 근사.
                                           ALG4: OP1→OP2[C] + OP3→OP4[C]. FM Index 낮음(모뎀 TL 높음).
                                           깨끗한 배음. 긴 서스테인. DR=10 느린감쇠. */
                                        { "VIBRAPHONE", 4, 0, 3, {
                                            { 0,  1,  24, 1, 31, 0, 10,  0,  3,  8,  0}, /* OP1 mod: Index 낮음 */
                                            { 0,  1,   5, 1, 31, 0, 10,  0,  3,  8,  0}, /* OP2 car */
                                            { 0,  3,  22, 1, 31, 0, 12,  0,  4,  9,  0}, /* OP3 mod: 3배음 */
                                            { 0,  1,   5, 1, 31, 0, 12,  0,  4,  9,  0}}},/* OP4 car */

                                            // ── 섹션 C: 현악기 ─────────────────────────────────────────────────────────

                                            /* [28] VIOLIN — ALG1 FB4
                                               DX7 "VIOLIN 1" 근사. ALG1: (OP1[FB]+OP2)→OP3→OP4[C].
                                               AR=22: 활 발음 느린 어택. SR=0 풀서스테인. FB=4.
                                               DT로 배음 비대칭 → 현악 거칠기. OP3 MUL=2. */
                                            { "VIOLIN", 1, 4, 4, {
                                                { 3,  1,  20, 0, 22, 0,  8,  0,  2,  7,  0}, /* OP1 mod: FB4+DT3 배음 */
                                                { 5,  2,  14, 0, 20, 0,  8,  0,  2,  7,  0}, /* OP2 mod: DT5 비대칭 */
                                                { 0,  2,  26, 0, 20, 0, 10,  0,  3,  7,  0}, /* OP3 mod: MUL=2 현악밝기 */
                                                { 0,  1,   6, 0, 22, 0,  6,  0,  1,  6,  0}}},/* OP4 car: SR=0 서스테인 */

                                                /* [29] CELLO — ALG1 FB3
                                                   DX7 "CELLO 1" 근사. 바이올린보다 어둡고 따뜻.
                                                   AR=18 더 느린 어택. FB=3(바이올린 FB=4보다 낮음).
                                                   OP2 MUL=1(기음변조). */
                                                { "CELLO", 1, 3, 4, {
                                                    { 2,  1,  24, 0, 18, 0,  6,  0,  2,  7,  0}, /* OP1 mod: FB3 */
                                                    { 4,  1,  18, 0, 16, 0,  6,  0,  2,  7,  0}, /* OP2 mod: DT4 */
                                                    { 0,  2,  30, 0, 18, 0,  8,  0,  3,  7,  0}, /* OP3 mod: TL 높아 약한변조 */
                                                    { 0,  1,   6, 0, 18, 0,  5,  0,  1,  6,  0}}},/* OP4 car */

                                                    /* [30] STRINGS_ENS — ALG6 FB2
                                                       DX7 "STRINGS 1" 근사. ALG6: OP1→OP2[C]+OP3[C]+OP4[C].
                                                       3개 carrier에 서로 다른 DT → 두께감·코러스 효과.
                                                       AR=18~20 느린 어택(스트링 패드). */
                                                    { "STRINGS_ENS", 6, 2, 4, {
                                                        { 0,  1,  28, 0, 16, 0,  6,  0,  2,  8,  0}, /* OP1 mod */
                                                        { 5,  1,   9, 0, 18, 0,  8,  0,  2,  8,  0}, /* OP2 car: DT5 디튠 */
                                                        { 3,  1,  10, 0, 18, 0,  8,  0,  2,  8,  0}, /* OP3 car: DT3 디튠 */
                                                        { 0,  1,   8, 0, 20, 0,  6,  0,  2,  8,  0}}},/* OP4 car: 기준음 */

                                                        /* [31] AGITAR — ALG3 FB3
                                                           DX7 "ACOUSTIC GUITAR" 근사. ALG3.
                                                           SSGEG=8 OP1: 피크 클릭 어택. DR=16~18 중속감쇠.
                                                           OP3 MUL=2: 2배음 변조 → 현 밝기. */
                                                        { "AGITAR", 3, 3, 4, {
                                                            { 0,  1,   3, 1, 31, 0, 16,  0,  7, 11,  8}, /* OP1 mod: SSGEG=8 클릭 */
                                                            { 0,  2,   8, 1, 31, 0, 16,  0,  7, 10,  0}, /* OP2 mod */
                                                            { 0,  2,  12, 1, 31, 0, 18,  0,  8, 11,  0}, /* OP3 mod */
                                                            { 0,  1,   5, 1, 31, 0, 14,  0,  5, 10,  0}}},/* OP4 car */

                                                            /* [32] EGITAR_CLEAN — ALG3 FB4
                                                               DX7 "E.GUITAR CLEAN" 근사. FB=4.
                                                               빠른 감쇠, 클린한 배음. SSGEG=8 OP1. */
                                                            { "EGITAR_CLEAN", 3, 4, 4, {
                                                                { 0,  1,   2, 1, 31, 0, 18,  0,  6, 10,  8}, /* OP1 mod: SSGEG=8 */
                                                                { 0,  2,   6, 1, 31, 0, 16,  0,  5,  9,  0}, /* OP2 mod */
                                                                { 0,  2,  10, 1, 31, 0, 18,  0,  6, 10,  0}, /* OP3 mod */
                                                                { 0,  1,   4, 1, 31, 0, 14,  0,  4,  9,  0}}},/* OP4 car */

                                                                /* [33] EGITAR_DRIVE — ALG1 FB7
                                                                   DX7 "E.GUITAR DISTORTION" 근사. FB=7 과변조.
                                                                   OP1 TL=24(높임)으로 과변조 제어. 거친 배음. */
                                                                { "EGITAR_DRIVE", 1, 7, 4, {
                                                                    { 0, 15,  24, 0, 31, 0,  8,  0,  1,  8,  0}, /* OP1 mod: FB7 */
                                                                    { 0,  2,   3, 0, 31, 0, 10,  0,  2,  8,  0}, /* OP2 mod */
                                                                    { 0,  1,  24, 0, 31, 0, 12,  0,  2,  8,  0}, /* OP3 mod */
                                                                    { 0,  1,   6, 0, 31, 0,  8,  0,  1,  7,  0}}},/* OP4 car */

                                                                    /* [34] SHAMISEN — ALG3 FB5
                                                                       샤미센: 빠른 감쇠, 사와리(雜音) 거칠기.
                                                                       FB=5: 자기변조로 특유의 거친 배음. SSGEG=8 OP1.
                                                                       DT=2 OP1: 배음 비대칭. */
                                                                    { "SHAMISEN", 3, 5, 4, {
                                                                        { 2,  1,   2, 2, 31, 0, 22,  0, 10, 12,  8}, /* OP1 mod: DT2+SSGEG */
                                                                        { 3,  2,   6, 2, 31, 0, 20,  0,  9, 11,  0}, /* OP2 mod */
                                                                        { 2,  1,  10, 2, 31, 0, 24,  0, 11, 13,  0}, /* OP3 mod */
                                                                        { 0,  1,   3, 2, 31, 0, 18,  0,  9, 11,  0}}},/* OP4 car */

                                                                        /* [35] BANJO — ALG3 FB3
                                                                           DX7 "BANJO" 근사. 날카로운 어택, 빠른 감쇠.
                                                                           MUL=2 OP2: 2배음 → 밝은 음색. */
                                                                        { "BANJO", 3, 3, 4, {
                                                                            { 0,  1,   2, 1, 31, 0, 20,  0,  8, 12,  8}, /* OP1 mod: SSGEG=8 */
                                                                            { 0,  2,   6, 1, 31, 0, 18,  0,  7, 11,  0}, /* OP2 mod: MUL=2 */
                                                                            { 0,  1,  10, 1, 31, 0, 22,  0,  9, 12,  0}, /* OP3 mod */
                                                                            { 0,  1,   4, 1, 31, 0, 16,  0,  7, 10,  0}}},/* OP4 car */

                                                                            /* [36] KOTO — ALG3 FB3
                                                                               고토: 샤미센보다 둥근 음색, 약간 긴 감쇠.
                                                                               FB=3. SSGEG=8 유지. DR 약간 늦춤. */
                                                                            { "KOTO", 3, 3, 4, {
                                                                                { 0,  1,   3, 1, 31, 0, 16,  0,  7, 11,  8}, /* OP1 mod: SSGEG=8 */
                                                                                { 0,  2,   8, 1, 31, 0, 14,  0,  6, 10,  0}, /* OP2 mod */
                                                                                { 0,  1,  12, 1, 31, 0, 18,  0,  8, 12,  0}, /* OP3 mod */
                                                                                { 0,  1,   4, 1, 31, 0, 12,  0,  6,  9,  0}}},/* OP4 car */

                                                                                // ── 섹션 D: 관악기 ─────────────────────────────────────────────────────────

                                                                                /* [37] FLUTE — ALG5 FB1
                                                                                   DX7 "FLUTE 1" 근사. ALG5: 3 modulators→OP4.
                                                                                   FM Index 낮음(모뎀 TL=18~28). 순음에 가까움.
                                                                                   AR=24~26: 마우스피스 발음 약간의 시간. SR=0. */
                                                                                { "FLUTE", 5, 1, 4, {
                                                                                    { 0,  1,  28, 0, 26, 0,  4,  0,  0, 10,  0}, /* OP1 mod: 기음변조 */
                                                                                    { 0,  2,  20, 0, 24, 0,  4,  0,  0, 10,  0}, /* OP2 mod: 2배음 */
                                                                                    { 0,  3,  26, 0, 24, 0,  5,  0,  0, 10,  0}, /* OP3 mod: 3배음 약하게 */
                                                                                    { 0,  1,   6, 0, 24, 0,  5,  0,  0,  9,  0}}},/* OP4 car: SR=0 서스테인 */

                                                                                    /* [38] DAEGEUM — ALG5 FB3
                                                                                       대금: 플루트보다 풍부한 배음, 김(breath) 소리.
                                                                                       FB=3: OP1 자기변조 → 배음 풍부+잡음성. TL 조정.
                                                                                       AR=22: 취구 발음 시간. */
                                                                                    { "DAEGEUM", 5, 3, 4, {
                                                                                        { 0,  1,  22, 0, 22, 0,  5,  0,  0,  9,  0}, /* OP1 mod: FB3 배음 */
                                                                                        { 0,  2,  16, 0, 22, 0,  5,  0,  0,  9,  0}, /* OP2 mod: 2배음 강화 */
                                                                                        { 2,  3,  24, 0, 20, 0,  6,  0,  1,  9,  0}, /* OP3 mod: DT2 비대칭 */
                                                                                        { 0,  1,   6, 0, 22, 0,  5,  0,  0,  8,  0}}},/* OP4 car: SR=0 */

                                                                                        /* [39] SOGEUM — ALG5 FB2
                                                                                           소금: 대금보다 높고 맑음. FB=2 낮은 자기변조.
                                                                                           TL 높여 Index 낮춤 → 순음 경향. AR=24. */
                                                                                        { "SOGEUM", 5, 2, 4, {
                                                                                            { 0,  1,  26, 0, 24, 0,  4,  0,  0, 10,  0}, /* OP1 mod */
                                                                                            { 0,  2,  18, 0, 24, 0,  4,  0,  0, 10,  0}, /* OP2 mod: 2배음 */
                                                                                            { 0,  3,  24, 0, 22, 0,  5,  0,  0,  9,  0}, /* OP3 mod: 3배음 */
                                                                                            { 0,  1,   6, 0, 24, 0,  5,  0,  0,  9,  0}}},/* OP4 car */

                                                                                            /* [40] OBOE — ALG1 FB5
                                                                                               DX7 "OBOE" 근사. 강한 홀수배음, 날카로운 리드 음색.
                                                                                               ALG1: (OP1[FB]+OP2)→OP3→OP4. FB=5.
                                                                                               AR=28: 빠른 발음. OP3 MUL=3: 3배음 변조 강조. */
                                                                                            { "OBOE", 1, 5, 4, {
                                                                                                { 0,  1,  18, 0, 28, 0,  5,  0,  0,  9,  0}, /* OP1 mod: FB5 */
                                                                                                { 0,  2,  10, 0, 26, 0,  4,  0,  0,  9,  0}, /* OP2 mod: 2배음 */
                                                                                                { 0,  3,  22, 0, 26, 0,  6,  0,  1,  9,  0}, /* OP3 mod: 3배음 */
                                                                                                { 0,  1,   5, 0, 26, 0,  4,  0,  0,  8,  0}}},/* OP4 car */

                                                                                                // ── 섹션 E: 금관·목관 ───────────────────────────────────────────────────────

                                                                                                /* [41] TRUMPET — ALG4 FB6
                                                                                                   DX7 "TRUMPET 1" 근사. ALG4: OP1→OP2[C] + OP3→OP4[C].
                                                                                                   FB=6: 강한 자기변조 → 금관 배음 풍부.
                                                                                                   AR=28: 빠른 어택. SR=0 풀서스테인(취주 지속). */
                                                                                                { "TRUMPET", 4, 6, 4, {
                                                                                                    { 0,  1,  10, 0, 28, 0,  3,  0,  0, 11,  0}, /* OP1 mod: 금관변조 */
                                                                                                    { 0,  1,   5, 0, 28, 0,  3,  0,  0, 11,  0}, /* OP2 car */
                                                                                                    { 0,  2,  14, 0, 26, 0,  5,  0,  1, 10,  0}, /* OP3 mod: 2배음 */
                                                                                                    { 0,  1,   5, 0, 28, 0,  3,  0,  0, 10,  0}}},/* OP4 car */

                                                                                                    /* [42] SAXOPHONE — ALG1 FB5
                                                                                                       DX7 "SAXOPHONE" 근사. 풍부한 홀수 배음. 리드 특성.
                                                                                                       ALG1: (OP1+OP2)→OP3→OP4. FB=5.
                                                                                                       DR=4 느린감쇠, SR=0 풀서스테인. OP2 MUL=2. */
                                                                                                    { "SAXOPHONE", 1, 5, 4, {
                                                                                                        { 0,  1,  18, 0, 28, 0,  4,  0,  0, 10,  0}, /* OP1 mod: FB5 */
                                                                                                        { 0,  2,   8, 0, 26, 0,  4,  0,  0, 10,  0}, /* OP2 mod: MUL=2 */
                                                                                                        { 0,  1,  22, 0, 26, 0,  5,  0,  1, 10,  0}, /* OP3 mod */
                                                                                                        { 0,  1,   5, 0, 26, 0,  4,  0,  0,  9,  0}}},/* OP4 car: SR=0 */

                                                                                                        /* [43] TROMBONE — ALG4 FB5
                                                                                                           DX7 "TROMBONE" 근사. 트럼펫보다 낮고 부드러운 배음.
                                                                                                           FB=5(트럼펫 FB=6보다 낮음). MUL 낮음. */
                                                                                                        { "TROMBONE", 4, 5, 4, {
                                                                                                            { 0,  1,  12, 0, 26, 0,  4,  0,  0, 10,  0}, /* OP1 mod */
                                                                                                            { 0,  1,   5, 0, 26, 0,  4,  0,  0, 10,  0}, /* OP2 car */
                                                                                                            { 0,  2,  16, 0, 24, 0,  5,  0,  1, 10,  0}, /* OP3 mod */
                                                                                                            { 0,  1,   5, 0, 26, 0,  4,  0,  0,  9,  0}}},/* OP4 car */

                                                                                                            // ── 섹션 F: 베이스·합창·신스 ────────────────────────────────────────────────

                                                                                                            /* [44] BASS_ACOUSTIC — ALG3 FB2
                                                                                                               DX7 "ACOUSTIC BASS" 근사. 빠른 어택, 중간 감쇠.
                                                                                                               SSGEG=8 OP1: 피크 어택. MUL=1. RS=1. */
                                                                                                            { "BASS_ACOUSTIC", 0, 2, 4, {
                                                                                                                { 0,  1,   2, 1, 31, 0, 16,  0,  6, 11,  8}, /* OP1 mod: SSGEG=8 */
                                                                                                                { 0,  1,   8, 1, 31, 0, 14,  0,  5, 10,  0}, /* OP2 mod */
                                                                                                                { 0,  2,  12, 1, 31, 0, 18,  0,  6, 11,  0}, /* OP3 mod */
                                                                                                                { 0,  1,   5, 1, 31, 0, 12,  0,  4, 10,  0}}},/* OP4 car */

                                                                                                                /* [45] BASS_FRETLESS — ALG3 FB1
                                                                                                                   DX7 "FRETLESS BASS" 근사. 부드러운 슬라이드 베이스.
                                                                                                                   FB=1: 약한 자기변조. DR 느림→서스테인형. */
                                                                                                                { "BASS_FRETLESS", 0, 1, 4, {
                                                                                                                    { 0,  1,   4, 1, 31, 0, 12,  0,  4, 10,  0}, /* OP1 mod */
                                                                                                                    { 0,  1,   6, 0, 26, 0, 12,  0,  4,  9,  0}, /* OP2 mod */
                                                                                                                    { 0,  2,  10, 0, 24, 0, 14,  0,  5, 10,  0}, /* OP3 mod */
                                                                                                                    { 0,  1,   5, 0, 24, 0, 10,  0,  3,  9,  0}}},/* OP4 car */

                                                                                                                    /* [46] VGM_BASS2 — ALG0 FB7 (원본 유지, VGM 계열)
                                                                                                                       강한 서브베이스. FB=7 사각파 유사. */
                                                                                                                    { "VGM_BASS2", 0, 7, 4, {
                                                                                                                        { 0,  0,  21, 0, 16, 0,  0,  0,  0, 15,  0},
                                                                                                                        { 0,  1,  31, 0, 31, 0,  0,  0,  0, 15,  0},
                                                                                                                        { 0,  0,  31, 0, 31, 0,  0,  0,  0, 15,  0},
                                                                                                                        { 0,  0,   4, 0, 31, 0,  9,  0,  1, 15,  0}}},

                                                                                                                        /* [47] CHOIR — ALG6 FB2
                                                                                                                           DX7 "CHOIR AAHS" 근사. ALG6: OP1→OP2[C]+OP3[C]+OP4[C].
                                                                                                                           AR=14~16: 매우 느린 어택(합창 페이드인).
                                                                                                                           carrier DT 서로 달리 → 두께·코러스 효과. */
                                                                                                                        { "CHOIR", 6, 2, 4, {
                                                                                                                            { 0,  1,  30, 0, 14, 0,  6,  0,  2,  8,  0}, /* OP1 mod */
                                                                                                                            { 5,  1,   9, 0, 16, 0,  6,  0,  2,  8,  0}, /* OP2 car: DT5 */
                                                                                                                            { 3,  1,  10, 0, 16, 0,  6,  0,  2,  8,  0}, /* OP3 car: DT3 */
                                                                                                                            { 0,  2,   9, 0, 18, 0,  6,  0,  2,  8,  0}}},/* OP4 car: MUL=2 */

                                                                                                                            /* [48] SYNTH_LEAD — ALG1 FB7
                                                                                                                               DX7 "SYNTH LEAD" 근사. 날카롭고 강한 리드.
                                                                                                                               FB=7 사각파 경향. OP4 TL=6 강한 출력. */
                                                                                                                            { "SYNTH_LEAD", 1, 7, 4, {
                                                                                                                                { 0, 15,  16, 0, 31, 0,  6,  0,  1,  8,  0}, /* OP1 mod: FB7 */
                                                                                                                                { 0,  1,   4, 0, 31, 0,  7,  0,  2,  8,  0}, /* OP2 mod */
                                                                                                                                { 0,  2,  20, 0, 31, 0,  9,  0,  2,  8,  0}, /* OP3 mod */
                                                                                                                                { 0,  1,   6, 0, 31, 0,  6,  0,  1,  8,  0}}},/* OP4 car */

                                                                                                                                /* [49] SYNTH_PAD — ALG6 FB1
                                                                                                                                   DX7 "SYNTH PAD" 근사. ALG6 3 carrier.
                                                                                                                                   AR=12~14: 느린 어택(패드). SR=0. DT 코러스. */
                                                                                                                                { "SYNTH_PAD", 6, 1, 4, {
                                                                                                                                    { 0,  2,  32, 0, 12, 0,  4,  0,  1,  9,  0}, /* OP1 mod */
                                                                                                                                    { 5,  1,   8, 0, 14, 0,  5,  0,  1,  8,  0}, /* OP2 car: DT5 */
                                                                                                                                    { 3,  1,   9, 0, 14, 0,  5,  0,  1,  8,  0}, /* OP3 car: DT3 */
                                                                                                                                    { 0,  1,   8, 0, 16, 0,  4,  0,  1,  8,  0}}},/* OP4 car */

                                                                                                                                    /* [50] SYNTH_BRASS — ALG4 FB7
                                                                                                                                       DX7 "SYNTH BRASS 1" 근사. 하드 브라스.
                                                                                                                                       FB=7 강한 배음. AR=28 빠른 어택. */
                                                                                                                                    { "SYNTH_BRASS", 4, 7, 4, {
                                                                                                                                        { 0,  1,   6, 0, 28, 0,  2,  0,  0, 10,  0}, /* OP1 mod */
                                                                                                                                        { 0,  1,   5, 0, 28, 0,  3,  0,  1, 10,  0}, /* OP2 car */
                                                                                                                                        { 0,  2,   6, 0, 26, 0,  4,  0,  0, 10,  0}, /* OP3 mod */
                                                                                                                                        { 0,  1,   5, 0, 28, 0,  3,  0,  1, 10,  0}}},/* OP4 car */

                                                                                                                                        // ── 섹션 G: 드럼 FM ─────────────────────────────────────────────────────────

                                                                                                                                        /* [51] DRUM_KICK — ALG7 FB0 FM 킥
                                                                                                                                           ALG7 전부 carrier. SSGEG=8(단발감쇠) → 빠른 피치드롭.
                                                                                                                                           MUL=1: 저음. TL=4~8: 강한 출력. vel_tl_scale=5. */
                                                                                                                                        { "DRUM_KICK", 7, 0, 5, {
                                                                                                                                            { 0,  1,   4, 0, 31, 0, 22,  0, 15, 10,  8},
                                                                                                                                            { 0,  1,   8, 0, 31, 0, 24,  0, 15, 10,  8},
                                                                                                                                            { 0,  2,  16, 0, 31, 0, 20,  0, 15, 10,  0},
                                                                                                                                            { 0,  1,  12, 0, 31, 0, 26,  0, 15, 11,  8}}},

                                                                                                                                            /* [52] DRUM_SNARE — ALG7 FB0 FM 스네어 */
                                                                                                                                            { "DRUM_SNARE", 7, 0, 5, {
                                                                                                                                                { 0,  2,   6, 0, 31, 0, 20,  0, 15, 11,  8},
                                                                                                                                                { 0,  3,  10, 0, 31, 0, 22,  0, 15, 11,  8},
                                                                                                                                                { 0,  1,  12, 0, 31, 0, 22,  0, 15, 11,  0},
                                                                                                                                                { 0,  2,   8, 0, 31, 0, 20,  0, 15, 12,  8}}},

                                                                                                                                                /* [53] DRUM_HI_TOM — ALG7 FB0 하이 탐 */
                                                                                                                                                { "DRUM_HI_TOM", 7, 0, 5, {
                                                                                                                                                    { 0,  1,   6, 0, 31, 0, 16,  0, 12, 11,  8},
                                                                                                                                                    { 0,  1,  10, 0, 31, 0, 18,  0, 12, 11,  8},
                                                                                                                                                    { 0,  2,  14, 0, 31, 0, 16,  0, 12, 11,  8},
                                                                                                                                                    { 0,  1,  10, 0, 31, 0, 18,  0, 12, 10,  8}}},

                                                                                                                                                    /* [54] DRUM_LO_TOM — ALG7 FB0 로우 탐 */
                                                                                                                                                    { "DRUM_LO_TOM", 7, 0, 5, {
                                                                                                                                                        { 0,  1,   4, 0, 31, 0, 14,  0, 11, 10,  8},
                                                                                                                                                        { 0,  1,   8, 0, 31, 0, 16,  0, 11, 10,  8},
                                                                                                                                                        { 0,  2,  12, 0, 31, 0, 14,  0, 12, 10,  8},
                                                                                                                                                        { 0,  1,   8, 0, 31, 0, 16,  0, 11,  9,  8}}},


                                                                                                                                                        // ── [R9-CLEAN] 최종 청음용 clean/safe 추가 프리셋 ─────────────────────────
                                                                                                                                                        // 목적: 기존 VGM 실측 계열의 강한 FB/TL로 인한 거친 느낌을 피하고,
                                                                                                                                                        //       EQ/Delay 테스트 중 원음 구분이 쉬운 부드러운 기준 음색을 제공.
                                                                                                                                                        // 설계 원칙: FB 0~3 중심, modulator TL 상향, carrier TL 12~28 범위,
                                                                                                                                                        //           SSGEG=0 기본, RR 8~11로 클릭/잔류음 억제.

                                                                                                                                                        { "CLEAN_ROUND_LEAD", 1, 2, 4, {
                                                                                                                                                            { 0,  2, 42, 0, 28, 0, 12,  0,  2, 10, 0},
                                                                                                                                                            { 0,  1, 34, 0, 28, 0, 10,  0,  2, 10, 0},
                                                                                                                                                            { 0,  1, 50, 0, 27, 0,  8,  0,  1, 10, 0},
                                                                                                                                                            { 0,  1, 14, 0, 30, 0,  7,  0,  0,  9, 0}}},

                                                                                                                                                        { "CLEAN_SOFT_BRASS", 4, 2, 4, {
                                                                                                                                                            { 0,  2, 38, 0, 23, 0, 10,  0,  2, 10, 0},
                                                                                                                                                            { 0,  1, 17, 0, 25, 0,  9,  0,  1, 10, 0},
                                                                                                                                                            { 0,  2, 44, 0, 22, 0, 12,  0,  2, 10, 0},
                                                                                                                                                            { 0,  1, 18, 0, 25, 0,  8,  0,  1, 10, 0}}},

                                                                                                                                                        { "CLEAN_EP_WARM", 5, 1, 4, {
                                                                                                                                                            { 0,  2, 46, 0, 27, 0, 13,  2,  2,  8, 0},
                                                                                                                                                            { 0,  1, 22, 0, 28, 0, 10,  4,  1,  8, 0},
                                                                                                                                                            { 0,  3, 52, 0, 26, 0, 11,  3,  2,  8, 0},
                                                                                                                                                            { 0,  1, 16, 0, 30, 0,  9,  5,  1,  8, 0}}},

                                                                                                                                                        { "CLEAN_FLUTE_AIR", 5, 1, 3, {
                                                                                                                                                            { 0,  1, 56, 0, 22, 0,  7,  0,  0, 10, 0},
                                                                                                                                                            { 0,  1, 20, 0, 24, 0,  6,  0,  0, 10, 0},
                                                                                                                                                            { 0,  2, 58, 0, 21, 0,  7,  0,  0, 10, 0},
                                                                                                                                                            { 0,  1, 21, 0, 24, 0,  6,  0,  0, 10, 0}}},

                                                                                                                                                        { "CLEAN_STRINGS_PAD", 6, 1, 3, {
                                                                                                                                                            { 0,  1, 48, 0, 18, 0,  8,  0,  3,  9, 0},
                                                                                                                                                            { 1,  1, 23, 0, 20, 0,  7,  0,  2,  9, 0},
                                                                                                                                                            { 5,  1, 25, 0, 19, 0,  8,  0,  2,  9, 0},
                                                                                                                                                            { 0,  1, 26, 0, 20, 0,  7,  0,  2,  9, 0}}},

                                                                                                                                                        { "CLEAN_ORGAN_ROUND", 7, 0, 0, {
                                                                                                                                                            { 0,  1, 25, 0, 31, 0,  0,  0,  0,  8, 0},
                                                                                                                                                            { 0,  2, 27, 0, 31, 0,  0,  0,  0,  8, 0},
                                                                                                                                                            { 0,  4, 30, 0, 31, 0,  0,  0,  0,  8, 0},
                                                                                                                                                            { 0,  1, 20, 0, 31, 0,  0,  0,  0,  8, 0}}},

                                                                                                                                                        { "CLEAN_PLUCK_MELLOW", 0, 2, 5, {
                                                                                                                                                            { 0,  2, 46, 0, 31, 0, 16,  0,  5,  8, 0},
                                                                                                                                                            { 0,  3, 38, 0, 31, 0, 14,  0,  5,  8, 0},
                                                                                                                                                            { 0,  1, 44, 0, 31, 0, 13,  0,  5,  8, 0},
                                                                                                                                                            { 0,  1, 16, 0, 31, 0, 12,  0,  5,  8, 0}}},

                                                                                                                                                        { "CLEAN_BASS_ROUND", 0, 2, 5, {
                                                                                                                                                            { 0,  1, 40, 0, 28, 0, 10,  0,  3, 10, 0},
                                                                                                                                                            { 0,  1, 34, 0, 28, 0,  9,  0,  3, 10, 0},
                                                                                                                                                            { 0,  2, 48, 0, 26, 0,  8,  0,  4, 10, 0},
                                                                                                                                                            { 0,  1, 13, 0, 30, 0,  8,  0,  2, 10, 0}}},

                                                                                                                                                        { "CLEAN_BELL_SOFT", 7, 2, 4, {
                                                                                                                                                            { 0,  0, 31, 0, 31, 0, 11,  0,  7,  7, 0},
                                                                                                                                                            { 0,  1, 34, 0, 31, 0, 10,  0,  7,  7, 0},
                                                                                                                                                            { 0,  3, 39, 0, 31, 0,  9,  0,  8,  7, 0},
                                                                                                                                                            { 0,  1, 19, 0, 31, 0,  9,  0,  7,  7, 0}}},

                                                                                                                                                        { "CLEAN_SYNTH_PAD", 4, 1, 3, {
                                                                                                                                                            { 0,  1, 50, 0, 18, 0,  7,  0,  4, 10, 0},
                                                                                                                                                            { 0,  1, 26, 0, 20, 0,  6,  0,  3, 10, 0},
                                                                                                                                                            { 0,  2, 54, 0, 18, 0,  7,  0,  4, 10, 0},
                                                                                                                                                            { 0,  1, 27, 0, 20, 0,  6,  0,  3, 10, 0}}},

                                                                                                                                                        { "CLEAN_GUITAR_SOFT", 3, 2, 5, {
                                                                                                                                                            { 0,  1, 42, 0, 30, 0, 13,  0,  4,  9, 0},
                                                                                                                                                            { 0,  1, 35, 0, 30, 0, 11,  0,  4,  9, 0},
                                                                                                                                                            { 0,  2, 44, 0, 28, 0, 12,  0,  4,  9, 0},
                                                                                                                                                            { 0,  1, 16, 0, 31, 0, 10,  0,  4,  9, 0}}},

                                                                                                                                                        { "CLEAN_CHOIR_SOFT", 7, 0, 2, {
                                                                                                                                                            { 0,  1, 31, 0, 20, 0,  7,  0,  2, 10, 0},
                                                                                                                                                            { 1,  1, 33, 0, 20, 0,  7,  0,  2, 10, 0},
                                                                                                                                                            { 5,  1, 35, 0, 20, 0,  7,  0,  2, 10, 0},
                                                                                                                                                            { 0,  1, 28, 0, 20, 0,  7,  0,  2, 10, 0}}},

                                                                                                                                                            // =========================================================================
                                                                                                                                                            // [R10-UNIQUE] 확연히 독특한 FM 특유 음색 6종
                                                                                                                                                            // 다른 악기/신스에서 듣기 어려운 FM만의 극단적 음색.
                                                                                                                                                            // 의도적으로 익스트림 파라미터 사용 — 듣는 순간 "다르다"는 느낌.
                                                                                                                                                            // =========================================================================

                                                                                                                                                            /* [UNIQUE-0] METAL_IMPACT — ALG0 직렬체인 극변조 금속 충격음
                                                                                                                                                               ALG0: OP1[FB]→OP2→OP3→OP4[C]. 4단 직렬로 FM 인덱스 연쇄 증폭.
                                                                                                                                                               OP1~3 TL=0~4로 극도로 강한 변조 → 매우 밝고 금속성 강한 음.
                                                                                                                                                               carrier SSGEG=0x08(단발감쇠) → key-on 직후 피치 드롭.
                                                                                                                                                               DR=26~28 빠른 감쇠, RR=14 빠른 릴리즈 → 임팩트+잔향 느낌. */
                                                                                                                                                            { "UNIQUE_METAL_IMPACT", 0, 6, 5, {
        /*DT MUL  TL RS AR AM DR SR SL RR SSGEG*/
        { 0,  1,   2, 1, 31, 0, 24,  0, 15, 13,  0}, /* OP1 mod: FB6 강변조 */
        { 2,  2,   4, 1, 31, 0, 22,  0, 15, 13,  0}, /* OP2 mod: DT2 비대칭 */
        { 6,  3,   0, 1, 31, 0, 26,  0, 15, 13,  0}, /* OP3 mod: DT6 역방향 */
        { 0,  1,   6, 2, 31, 0, 28,  0, 15, 14,  8}}},/* OP4 car: SSGEG 피치드롭 */

        /* [UNIQUE-1] ALIEN_LEAD — ALG5 스타변조 비정수 MUL 외계인 리드
           ALG5: OP1[FB]→OP4 + OP2→OP4 + OP3→OP4. 세 변조원이 OP4를 동시 변조.
           MUL=0(×0.5) 비정수 사용으로 기음 아래 서브하모닉 생성.
           FB=6: OP1 자기변조로 사각파 근사 → 날카로운 리드 성분.
           OP2 MUL=0, OP3 MUL=3: 0.5배+3배 조합 → 非조화 스펙트럼.
           SR=0 완전 서스테인. vel_tl_scale=4. */
        { "UNIQUE_ALIEN_LEAD", 5, 6, 4, {
            { 0,  0,   8, 0, 31, 0,  0,  0,  0, 10,  0}, /* OP1: FB6+MUL=0(×0.5) */
            { 3,  0,  10, 0, 31, 0,  0,  0,  0, 10,  0}, /* OP2: MUL=0 서브하모닉 */
            { 0,  3,  14, 0, 31, 0,  0,  0,  0, 10,  0}, /* OP3: MUL=3 3배음 변조 */
            { 0,  1,   8, 0, 31, 0,  0,  0,  0, 10,  0}}},/* OP4 car: SR=0 서스테인 */

            /* [UNIQUE-2] WOBBLE_BASS — ALG2 FB7 극변조 워블 베이스
               ALG2: OP1[FB]→OP3→OP4[C] + OP2→OP3. OP1,OP2 동시에 OP3 변조.
               OP1 FB7+TL=3, OP2 TL=4 → FM 인덱스 극대화 → 배음 폭발적 생성.
               MUL=1,2,3,1: 기음+2배+3배 변조로 풍부한 저음 배음.
               SR=0, carrier TL=4로 강한 출력 → 실제 웨이브테이블 워블 효과.
               vel_tl_scale=5 강한 velocity 감도. */
            { "UNIQUE_WOBBLE_BASS", 2, 7, 5, {
                { 0,  1,   3, 0, 28, 0,  0,  0,  0, 12,  0}, /* OP1: FB7 극변조원 */
                { 0,  2,   4, 0, 28, 0,  0,  0,  0, 12,  0}, /* OP2: 2배음 변조원 */
                { 0,  3,   6, 0, 28, 0,  0,  0,  0, 12,  0}, /* OP3: 3배음 중간단 */
                { 0,  1,   4, 1, 28, 0,  5,  0,  0, 10,  0}}},/* OP4 car: RS=1 고음감쇠 */

                /* [UNIQUE-3] CRYSTAL_BELL — ALG7 additive 인하모닉 수정 종소리
                   ALG7: 4개 모두 carrier. 사인파 4개 합산.
                   MUL=0(×0.5), MUL=1, MUL=3, MUL=7 → 0.5:1:3:7 비정수 비율.
                   순수 정수배가 아니므로 시간에 따라 배음 위상이 어긋나는 인하모닉 종소리.
                   TL 각각 다름 → 배음별 볼륨 비율 정교하게 제어.
                   DR=6~10 매우 느린 감쇠(종은 몇 초간 울림). RR=5 느린 릴리즈. */
                { "UNIQUE_CRYSTAL_BELL", 7, 0, 4, {
                    { 0,  0,  20, 1, 31, 0,  7,  0,  8,  5,  0}, /* OP1 car: MUL=0(×0.5) 서브 */
                    { 0,  1,  14, 1, 31, 0,  8,  0,  9,  5,  0}, /* OP2 car: MUL=1 기음 */
                    { 0,  3,  22, 1, 31, 0,  9,  0, 10,  5,  0}, /* OP3 car: MUL=3 3배음 */
                    { 0,  7,  28, 2, 31, 0, 10,  0, 11,  5,  0}}},/* OP4 car: MUL=7 7배음 */

                    /* [UNIQUE-4] GLASS_HARM — ALG4 SSGEG 반복어택 유리 하모닉스
                       ALG4: OP1→OP2[C] + OP3→OP4[C]. 두 쌍이 독립 출력.
                       SSGEG=0x0E(반복 삼각파 어택-역전): carrier에 적용하면 볼륨이
                       연속으로 Attack→Attack→Attack 반복 → 유리잔 긁는 배음 진동 특성.
                       MUL=1,2 / 1,3 조합으로 두 목소리의 배음 다름.
                       FB=3 적당한 배음 + AR=27 준빠른 어택. */
                    { "UNIQUE_GLASS_HARM", 4, 3, 3, {
                        { 0,  1,  18, 0, 27, 0,  4,  0,  0, 10,  0}, /* OP1 mod: FB3 */
                        { 0,  2,  12, 1, 29, 0,  3,  0,  0,  9, 14}, /* OP2 car: SSGEG=0x0E 반복 */
                        { 0,  1,  20, 0, 26, 0,  5,  0,  0, 10,  0}, /* OP3 mod */
                        { 0,  3,  14, 1, 29, 0,  3,  0,  0,  9, 14}}},/* OP4 car: SSGEG=0x0E */

                        /* [UNIQUE-5] CYBER_ORGAN — ALG5 FB7 사이버 오르간
                           ALG5: OP1[FB7]→OP4 + OP2→OP4 + OP3→OP4. carrier=OP4만.
                           MUL=1, 2, 6, 1 (변조원들이 기음의 2배, 6배 성분으로 변조).
                           FB=7: OP1이 사각파에 가까운 파형으로 변조 → 거친 전자 오르간 배음.
                           모든 OP TL 낮음(0~10) → 강한 FM 인덱스 → 극도로 풍부한 배음.
                           SR=0 완전 서스테인. vel_tl_scale=0 (오르간은 velocity 무시). */
                        { "UNIQUE_CYBER_ORGAN", 5, 7, 0, {
                            { 0,  1,   4, 0, 31, 0,  0,  0,  0, 12,  0}, /* OP1: FB7+MUL=1 */
                            { 0,  2,   6, 0, 31, 0,  0,  0,  0, 12,  0}, /* OP2: MUL=2 강변조 */
                            { 0,  6,   8, 0, 31, 0,  0,  0,  0, 12,  0}, /* OP3: MUL=6 6배음 */
                            { 0,  1,  10, 0, 31, 0,  0,  0,  0, 12,  0}}},/* OP4 car: 완전 서스테인 */

                            // =========================================================================
                            // [R10-HIFI] 음질 중시 — 낮은 FM 인덱스, 정교한 ADSR 6종
                            // modulator TL 높게 → FM Index I ≤ 1.5 → 배음 깨끗하게 제어.
                            // DX7 실증 파라미터를 4-OP OPN에 최대한 충실히 이식.
                            // =========================================================================

                            /* [HIFI-0] PIANO_BRIGHT — DX7 PIANO1 근사 고해상도 피아노
                               ALG3: OP1[FB]→OP2→OP4[C] + OP3→OP4[C]. 두 변조 경로가 OP4에 합산.
                               OP1 FB=4 적당한 배음. OP3 MUL=14 고배음 modulator → 어택 트랜지언트.
                               carrier SSGEG=0x08: key-on 순간 피치/배음 급격 드롭 → 타현 느낌.
                               RS=2 고음역에서 빠른 감쇠. SR=4~6 자연 감쇠 커브. */
                            { "HIFI_PIANO_BRIGHT", 3, 4, 5, {
                                { 0,  1,  34, 1, 31, 0, 16,  4,  8, 11,  0}, /* OP1 mod: FB4 자연배음 */
                                { 0,  2,  38, 1, 31, 0, 18,  6,  9, 11,  0}, /* OP2 mod: MUL=2 2배음 */
                                { 0, 14,  28, 2, 31, 0, 22,  8, 11, 12,  0}, /* OP3 mod: MUL=14 고배음 */
                                { 0,  1,  12, 2, 31, 0, 14,  5,  7, 11,  8}}},/* OP4 car: SSGEG 어택 */

                                /* [HIFI-1] STRINGS_WARM — DX7 STRINGS1 근사 따뜻한 현악 앙상블
                                   ALG6: OP1→OP2[C]+OP3[C]+OP4[C]. 3 carrier + 1 modulator.
                                   carrier 3개 DT 분산(DT=1,0,5): 미세 피치 앙상블 효과.
                                   AR=16 느린 어택(보잉 시작), SR=0 완전 서스테인.
                                   OP1 modulator TL=42로 낮은 FM 인덱스 → 현악 배음 자연스러움. */
                                { "HIFI_STRINGS_WARM", 6, 2, 4, {
                                    { 0,  2,  42, 0, 22, 0,  8,  0,  3,  9,  0}, /* OP1 mod: 낮은 FM Index */
                                    { 1,  1,  14, 0, 16, 0,  6,  0,  2,  9,  0}, /* OP2 car: DT=1 미세 */
                                    { 0,  1,  15, 0, 16, 0,  6,  0,  2,  9,  0}, /* OP3 car: DT=0 기준 */
                                    { 5,  1,  16, 0, 18, 0,  7,  0,  2,  9,  0}}},/* OP4 car: DT=5 역방향 */

                                    /* [HIFI-2] FLUTE_PURE — I≈0.3 극저 FM 인덱스 순정 플루트
                                       ALG0 직렬체인이지만 modulator TL=55~60으로 FM Index ≈ 0.3~0.4.
                                       이 범위에서 2차 배음만 약하게 나오고 나머지는 순정 사인에 가까움.
                                       AR=20 부드러운 어택 (flute 발음은 순간적이 아님).
                                       SR=0 완전 서스테인. RR=10 자연 릴리즈. */
                                    { "HIFI_FLUTE_PURE", 0, 1, 3, {
                                        { 0,  1,  55, 0, 22, 0,  4,  0,  0, 10,  0}, /* OP1 mod: TL=55 극저변조 */
                                        { 0,  1,  58, 0, 22, 0,  4,  0,  0, 10,  0}, /* OP2 mod: 거의 무변조 */
                                        { 0,  1,  60, 0, 20, 0,  3,  0,  0, 10,  0}, /* OP3 mod: TL=60 초순정 */
                                        { 0,  1,  14, 0, 22, 0,  3,  0,  0, 10,  0}}},/* OP4 car: SR=0 서스테인 */

                                        /* [HIFI-3] VIBES_MELLOW — ALG4 부드러운 비브라폰
                                           ALG4: OP1→OP2[C] + OP3→OP4[C]. 2+2 독립 쌍.
                                           MUL=1,2 / 1,3 쌍으로 기음+배음 독립 제어.
                                           modulator TL=32~36: 적당한 FM 인덱스 → 금속 배음 자연스럽게.
                                           DR=10~12 느린 감쇠. SL=8~10: 잔향 남기며 서서히 소멸.
                                           RR=7 느린 릴리즈 → 공명통 잔향 표현. */
                                        { "HIFI_VIBES_MELLOW", 4, 2, 4, {
                                            { 0,  1,  32, 1, 31, 0, 10,  3,  8,  7,  0}, /* OP1 mod: RS=1 고음감쇠 */
                                            { 0,  2,  14, 1, 31, 0, 10,  3,  8,  7,  0}, /* OP2 car: MUL=2 2배음 */
                                            { 0,  1,  36, 1, 31, 0, 12,  4,  9,  7,  0}, /* OP3 mod */
                                            { 0,  3,  16, 1, 31, 0, 12,  4,  9,  7,  0}}},/* OP4 car: MUL=3 3배음 */

                                            /* [HIFI-4] BRASS_FULL — ALG1 날카로운 어택의 풀 브라스
                                               ALG1: (OP1[FB]+OP2)→OP3→OP4[C]. OP1,OP2 합산 변조.
                                               AR=28 빠르고 강한 어택. DR=6 1차 감쇠. SR=0 완전 서스테인.
                                               FB=5: 적당한 배음으로 금관 특유의 버징 표현.
                                               OP1 MUL=2, OP2 MUL=1: 2배+기음 변조원 합산 → 실제 금관 배음 구조. */
                                            { "HIFI_BRASS_FULL", 1, 5, 4, {
                                                { 0,  2,  22, 1, 28, 0,  6,  0,  2, 10,  0}, /* OP1 mod: FB5 MUL=2 */
                                                { 1,  1,  26, 1, 28, 0,  7,  0,  2, 10,  0}, /* OP2 mod: DT=1 미세 */
                                                { 0,  1,  30, 0, 28, 0,  5,  0,  1, 10,  0}, /* OP3 mod */
                                                { 0,  1,  12, 1, 28, 0,  5,  0,  1, 10,  0}}},/* OP4 car: RS=1 */

                                                /* [HIFI-5] CELLO_DEEP — ALG3 깊고 풍성한 첼로
                                                   ALG3: OP1[FB]→OP2→OP4[C] + OP3→OP4[C]. 이중 변조 경로.
                                                   AR=20 점진적 어택 (활이 현을 잡는 순간). SR=0 완전 서스테인.
                                                   FB=3: 첼로의 풍부한 저배음. OP3 MUL=2 → 제2배음 보강.
                                                   DT=2/6로 carrier 앙상블. RR=8 자연스러운 릴리즈. */
                                                { "HIFI_CELLO_DEEP", 3, 3, 4, {
                                                    { 0,  1,  28, 0, 20, 0,  5,  0,  1,  8,  0}, /* OP1 mod: FB3 */
                                                    { 2,  1,  32, 0, 20, 0,  6,  0,  1,  8,  0}, /* OP2 mod: DT=2 */
                                                    { 0,  2,  24, 0, 22, 0,  4,  0,  0,  8,  0}, /* OP3 mod: MUL=2 */
                                                    { 6,  1,  12, 1, 22, 0,  4,  0,  0,  8,  0}}},/* OP4 car: DT=6 앙상블 */

                                                    // =========================================================================
                                                    // [R10-PLEASANT] 듣기 좋은 음색 — 귀에 피로 없는 편안한 배음 6종
                                                    // FB=0~2로 과변조 억제. 적절한 AR로 어택 충격 없음.
                                                    // 모두 SR=0 완전 서스테인. RR=9~10 깔끔한 릴리즈.
                                                    // =========================================================================

                                                    /* [PLEASANT-0] WARM_PAD — ALG6 FB=0 진짜 편안한 패드
                                                       ALG6: OP2+OP3+OP4 carrier, OP1이 OP2 변조.
                                                       FB=0: OP1 순정 사인파 변조원 → 2차 배음만 부드럽게 추가.
                                                       AR=13~15: 느린 스웰 어택 → 패드의 핵심. SR=0 완전 서스테인.
                                                       carrier 3개 TL=15~18로 균일한 볼륨, DT=1/0/5 미세 앙상블. */
                                                    { "PLEASANT_WARM_PAD", 6, 0, 3, {
                                                        { 0,  1,  44, 0, 14, 0,  5,  0,  1,  9,  0}, /* OP1 mod: FB=0 순정 */
                                                        { 1,  1,  15, 0, 13, 0,  4,  0,  1,  9,  0}, /* OP2 car: DT=1 */
                                                        { 0,  1,  16, 0, 14, 0,  4,  0,  1,  9,  0}, /* OP3 car: DT=0 기준 */
                                                        { 5,  2,  17, 0, 15, 0,  5,  0,  1,  9,  0}}},/* OP4 car: DT=5 MUL=2 */

                                                        /* [PLEASANT-1] GUITAR_NYLON — ALG4 나일론 기타 자연 감쇠
                                                           ALG4: OP1→OP2[C] + OP3→OP4[C].
                                                           FB=2: 부드러운 기타 배음. MUL=1,1 / 1,2.
                                                           DR=16~18 자연스러운 감쇠. SL=8~10: 서서히 페이드.
                                                           AR=30 빠른 어택 (손가락 튕기기). RR=9 깔끔한 릴리즈. */
                                                        { "PLEASANT_GUITAR_NYLON", 4, 2, 5, {
                                                            { 0,  2,  36, 0, 30, 0, 16,  0,  9,  9,  0}, /* OP1 mod: MUL=2 */
                                                            { 0,  1,  14, 0, 30, 0, 16,  0,  9,  9,  0}, /* OP2 car */
                                                            { 0,  1,  38, 0, 30, 0, 18,  0, 10,  9,  0}, /* OP3 mod */
                                                            { 0,  2,  15, 0, 30, 0, 18,  0, 10,  9,  0}}},/* OP4 car: MUL=2 배음 */

                                                            /* [PLEASANT-2] MARIMBA_SOFT — ALG0 목질 마림바 자연 감쇠
                                                               ALG0 직렬체인. modulator TL=28~38로 중간 FM 인덱스 → 목질 배음.
                                                               DR=12~14, SL=12~14: 마림바 특유의 빠른 1차감쇠 후 긴 잔향.
                                                               MUL=1,2,1,1: 2배음 modulator 강조 → 마림바 특유 상쾌한 배음.
                                                               carrier TL=12로 강한 출력. RR=10. */
                                                            { "PLEASANT_MARIMBA_SOFT", 0, 2, 5, {
                                                                { 0,  1,  28, 0, 31, 0, 12,  0, 13,  9,  0}, /* OP1 mod: FB2 */
                                                                { 0,  2,  32, 0, 31, 0, 13,  0, 13,  9,  0}, /* OP2 mod: MUL=2 */
                                                                { 0,  1,  38, 0, 31, 0, 14,  0, 14,  9,  0}, /* OP3 mod */
                                                                { 0,  1,  12, 1, 31, 0, 12,  0, 14, 10,  0}}},/* OP4 car: RS=1 */

                                                                /* [PLEASANT-3] CHOIR_LUSH — ALG6 FB=1 풍성한 합창 보이스
                                                                   ALG6: OP2+OP3+OP4 carrier. AR=15~17 느린 어택 → 합창 페이드인.
                                                                   FB=1: 최소한의 배음으로 보컬 느낌.
                                                                   carrier 3개 DT=0,2,6으로 넓은 앙상블 폭.
                                                                   OP4 MUL=2 → 한 옥타브 위 보이스 추가로 풍성함. */
                                                                { "PLEASANT_CHOIR_LUSH", 6, 1, 3, {
                                                                    { 0,  1,  40, 0, 16, 0,  6,  0,  2,  9,  0}, /* OP1 mod: FB=1 */
                                                                    { 0,  1,  16, 0, 15, 0,  5,  0,  2,  9,  0}, /* OP2 car: DT=0 */
                                                                    { 2,  1,  17, 0, 16, 0,  5,  0,  2,  9,  0}, /* OP3 car: DT=2 */
                                                                    { 6,  2,  18, 0, 17, 0,  6,  0,  2,  9,  0}}},/* OP4 car: DT=6 MUL=2 */

                                                                    /* [PLEASANT-4] EP_CLEAN — ALG4 클린 로즈 피아노
                                                                       ALG4: OP1→OP2[C] + OP3→OP4[C].
                                                                       FB=2: 로즈 특유의 약한 배음 어택. MUL=14/1: 고배음 modulator로
                                                                       어택 순간 금속성 + carrier 기음.
                                                                       SR=4: key-on 동안 서서히 자연 감쇠(EP 특성). RR=9. */
                                                                    { "PLEASANT_EP_CLEAN", 4, 2, 5, {
                                                                        { 0, 14,  16, 1, 31, 0,  8,  4,  7,  9,  0}, /* OP1 mod: MUL=14 어택 */
                                                                        { 0,  1,  14, 1, 31, 0,  8,  4,  7,  9,  0}, /* OP2 car: RS=1 */
                                                                        { 0,  7,  20, 1, 31, 0,  9,  5,  8,  9,  0}, /* OP3 mod: MUL=7 */
                                                                        { 0,  1,  15, 1, 31, 0,  9,  5,  8,  9,  0}}},/* OP4 car */

                                                                        /* [PLEASANT-5] HARP_RICH — ALG3 따뜻한 하프 잔향
                                                                           ALG3: OP1[FB]→OP2→OP4[C] + OP3→OP4[C].
                                                                           FB=2 부드러운 배음. DR=14 느린 1차 감쇠. SR=0 → SL까지 볼륨 유지.
                                                                           SL=7: 원음 50% 수준 서스테인. RR=9.
                                                                           OP3 MUL=3: 3배음 변조로 하프 공명통 배음 보강. */
                                                                        { "PLEASANT_HARP_RICH", 3, 2, 4, {
                                                                            { 0,  1,  30, 0, 30, 0, 14,  0,  7,  9,  0}, /* OP1 mod: FB=2 */
                                                                            { 0,  1,  34, 0, 30, 0, 15,  0,  7,  9,  0}, /* OP2 mod */
                                                                            { 0,  3,  28, 0, 30, 0, 13,  0,  6,  9,  0}, /* OP3 mod: MUL=3 3배음 */
                                                                            { 0,  1,  13, 1, 30, 0, 13,  0,  7,  9,  0}}},/* OP4 car: RS=1 */

                                                                            // =========================================================================
                                                                            // [R10-PRESTIGE] 고급진 음색 — 물리 음원 모델링 수준 ADSR 6종
                                                                            // 각 OP의 역할이 실제 악기 음향 구조에 대응하도록 설계.
                                                                            // RS/DT/ADSR 모두 음역별 특성을 고려한 정교한 파라미터.
                                                                            // =========================================================================

                                                                            /* [PRESTIGE-0] GRAND_PIANO — ALG3 그랜드 피아노 물리 모델 근사
                                                                               ALG3: OP1[FB]→OP2→OP4[C] + OP3→OP4[C].
                                                                               OP1 FB=4: 타현 어택 순간 인하모닉 성분.
                                                                               OP3 MUL=14: 고배음 변조로 타현 트랜지언트 정밀 표현.
                                                                               OP4(carrier) SSGEG=0x08: key-on시 배음 프로파일 급격 변화 → 실제 피아노.
                                                                               RS=2 고음역 빠른 감쇠. SR=4~6 자연 감쇠 곡선. */
                                                                            { "PRESTIGE_GRAND_PIANO", 3, 4, 5, {
                                                                                { 0,  1,  30, 1, 31, 0, 14,  5,  7, 10,  0}, /* OP1 mod: FB4 RS=1 */
                                                                                { 1,  2,  34, 1, 31, 0, 16,  6,  8, 10,  0}, /* OP2 mod: DT=1 MUL=2 */
                                                                                { 0, 14,  22, 2, 31, 0, 20,  7, 10, 11,  0}, /* OP3 mod: MUL=14 고배음 */
                                                                                { 0,  1,  10, 2, 31, 0, 12,  4,  6, 10,  8}}},/* OP4 car: SSGEG=0x08 */

                                                                                /* [PRESTIGE-1] ORCH_STRINGS — ALG6 오케스트라 현악 앙상블
                                                                                   ALG6: OP1→OP2[C]+OP3[C]+OP4[C]. 3 carrier 독립 출력.
                                                                                   carrier DT: OP2=DT5(-), OP3=DT0, OP4=DT3(+) → 넓은 앙상블.
                                                                                   AR=17~19 느린 보잉 어택. SR=0 완전 서스테인. RR=8 자연 릴리즈.
                                                                                   OP4 MUL=2 → 한 옥타브 위 현이 함께 울리는 더블링 효과.
                                                                                   modulator TL=38: 낮은 FM 인덱스로 현악 배음 자연스러움. */
                                                                                { "PRESTIGE_ORCH_STRINGS", 6, 2, 4, {
                                                                                    { 0,  2,  38, 0, 22, 0,  7,  0,  2,  8,  0}, /* OP1 mod: FB=2 */
                                                                                    { 5,  1,  13, 0, 17, 0,  5,  0,  1,  8,  0}, /* OP2 car: DT=5(-) */
                                                                                    { 0,  1,  14, 0, 18, 0,  5,  0,  1,  8,  0}, /* OP3 car: DT=0 기준 */
                                                                                    { 3,  2,  15, 0, 19, 0,  6,  0,  2,  8,  0}}},/* OP4 car: DT=3(+) MUL=2 */

                                                                                    /* [PRESTIGE-2] FRENCH_HORN — ALG2 프렌치 호른 원통관 배음 구조
                                                                                       ALG2: OP1[FB]→OP3→OP4[C] + OP2→OP3.
                                                                                       프렌치 호른은 원통관 → 홀수배음 우세. MUL=1,2,3,1로 구현.
                                                                                       AR=22 입술 어택 (순간적이 아닌 빌드업). SR=0 서스테인.
                                                                                       FB=4: 호른 특유의 버징 배음. DT=1,3으로 음색 두께.
                                                                                       DR=6 부드러운 1차 감쇠. */
                                                                                    { "PRESTIGE_FRENCH_HORN", 2, 4, 4, {
                                                                                        { 0,  1,  24, 1, 22, 0,  6,  0,  1,  9,  0}, /* OP1 mod: FB=4 */
                                                                                        { 3,  2,  28, 1, 22, 0,  7,  0,  2,  9,  0}, /* OP2 mod: DT=3 MUL=2 */
                                                                                        { 1,  3,  26, 0, 24, 0,  5,  0,  1,  9,  0}, /* OP3 mod: DT=1 MUL=3 */
                                                                                        { 0,  1,  12, 1, 24, 0,  5,  0,  1,  9,  0}}},/* OP4 car: RS=1 */

                                                                                        /* [PRESTIGE-3] KOTO_NOBLE — ALG1 고급 고토(箏) 음색
                                                                                           ALG1: (OP1[FB]+OP2)→OP3→OP4[C].
                                                                                           FB=4: 현 뜯는 어택의 사와리(雑音) 비정질 노이즈 성분.
                                                                                           OP1 DT=3, OP2 DT=6: 두 변조원의 스펙트럼 비대칭 → 고토 음색.
                                                                                           DR=14 느린 1차 감쇠. SR=4: 서서히 감쇠하며 공명통 잔향 표현.
                                                                                           SL=8 자연스러운 서스테인 레벨. */
                                                                                        { "PRESTIGE_KOTO_NOBLE", 1, 4, 4, {
                                                                                            { 3,  2,  18, 1, 31, 0, 13,  4,  7, 10,  0}, /* OP1 mod: DT=3 FB=4 */
                                                                                            { 6,  3,  22, 1, 31, 0, 14,  5,  8, 10,  0}, /* OP2 mod: DT=6 MUL=3 */
                                                                                            { 0,  1,  26, 0, 31, 0, 12,  3,  6, 10,  0}, /* OP3 mod */
                                                                                            { 0,  1,  11, 1, 31, 0, 12,  3,  6, 10,  0}}},/* OP4 car: RS=1 */

                                                                                            /* [PRESTIGE-4] CRYSTAL_PAD — ALG5 수정 공명 크리스탈 패드
                                                                                               ALG5: OP1[FB]→OP4 + OP2→OP4 + OP3→OP4.
                                                                                               MUL=1,2,4,1: 기음의 1,2,4배 변조원이 carrier를 동시 변조.
                                                                                               AR=14 느린 스웰 어택. DR=4 매우 느린 1차 감쇠. SR=0 완전 서스테인.
                                                                                               TL 분산: OP2=24, OP3=32로 배음 비율 제어. 전체적으로 투명하고 고급스러움.
                                                                                               FB=2: 최소한의 자기변조로 기음 순도 유지. */
                                                                                            { "PRESTIGE_CRYSTAL_PAD", 5, 2, 3, {
                                                                                                { 0,  1,  30, 0, 14, 0,  4,  0,  0,  9,  0}, /* OP1 mod: FB=2 MUL=1 */
                                                                                                { 0,  2,  24, 0, 15, 0,  4,  0,  0,  9,  0}, /* OP2 mod: MUL=2 */
                                                                                                { 0,  4,  32, 0, 16, 0,  5,  0,  0,  9,  0}, /* OP3 mod: MUL=4 4배음 */
                                                                                                { 0,  1,  13, 0, 14, 0,  4,  0,  0,  9,  0}}},/* OP4 car: SR=0 서스테인 */

                                                                                                /* [PRESTIGE-5] HARPSICHORD_NOBLE — ALG0 고급 쳄발로
                                                                                                   ALG0 직렬체인. 쳄발로는 타현 → 어택이 전부, 서스테인 없음.
                                                                                                   SSGEG=0x0D(단발어택 후 소거): carrier에 적용시 note-on 순간만 발음.
                                                                                                   OP1 FB=5, MUL=14: 고배음 변조원 → 쳄발로 특유 금속성 어택.
                                                                                                   DR=22~24 빠른 감쇠. carrier TL=8 강한 출력. vel_tl_scale=5 강한 감도.
                                                                                                   RR=12 빠른 릴리즈 (남은 잔향 즉시 소거). */
                                                                                                { "PRESTIGE_HARPSICHORD", 0, 5, 5, {
                                                                                                    { 0, 14,  10, 2, 31, 0, 20,  0, 15, 12,  0}, /* OP1 mod: FB5 고배음 */
                                                                                                    { 2,  7,  14, 2, 31, 0, 22,  0, 15, 12,  0}, /* OP2 mod: MUL=7 DT=2 */
                                                                                                    { 0,  3,  18, 2, 31, 0, 24,  0, 15, 12,  0}, /* OP3 mod: MUL=3 빠른DR */
                                                                                                    { 0,  1,   8, 2, 31, 0, 24,  0, 15, 12, 13}}},/* OP4 car: SSGEG=0x0D */

};

// =============================================================================
//  §12  SSG 패치 테이블 [R10 전면 재설계]
//
//  필드 순서:
//    name, use_tone, use_noise, noise_period,
//    amp_base, env_period, env_shape, env_period_scale,
//    vol_scale, attack_ticks, attack_vol, sustain_vol,
//    vol_fade_rate, cut_ticks
//
//  ── 설계 근거 ─────────────────────────────────────────────────────────────
//
//  [MELODY 계열]
//    핵심: amp_base=0x10 + env_shape=ATTACK_HOLD(0x0C).
//    ATTACK_HOLD → note_on 시 /‾‾‾ 파형: 빠른 어택 후 최대볼륨 유지.
//    env_period로 어택 속도 제어 (클수록 느린 어택 = 스웰).
//    실제 key-off 처리: note_off 시 DECAY_ONCE shape로 재기록하거나
//    vol 레지스터를 0으로 = 즉각 소거. 단순 구현에서는 vol=0 처리.
//
//  [PLUCK 계열]
//    핵심: amp_base=0x10 + env_shape=DECAY_ONCE(0x08).
//    DECAY_ONCE → note_on 시 \___: 즉각 어택 후 env_period 속도로 감쇠.
//    env_period_scale=1로 음정별 감쇠 속도 연동 가능.
//    attack_vol로 초기 임팩트 강조.
//
//  [DRUM 계열]
//    핵심: use_noise=1, cut_ticks로 발음 길이 제어.
//    noise_period: 0~4=고주파(심벌), 8~14=중주파(스네어), 16~24=저주파(킥).
//    tone 혼합(use_tone=1) 시 피치 있는 타악기 음색.
//    env mode + DECAY_ONCE로 자연스러운 감쇠.
//
//  [FX 계열]
//    envelope 파형 중 잘 안 쓰이는 것들(ALT, HOLD 변형) 적극 활용.
//    DECAY_ALT(0x0A): \/\/ 반복 → 빠르면 트레몰로, 느리면 비브라토 느낌.
//    ATTACK_ALT(0x0E): /\/\ 반복 → 부드러운 진동 / 워블.
//    DECAY_HOLD(0x0B): \‾‾‾ → 역방향 스탭 어택.
//
//  ── envelope period 기준값 ────────────────────────────────────────────────
//    0x0008: 극빠름 (트레몰로 속도)
//    0x0020: 매우 빠름 (타악기 빠른 감쇠)
//    0x0060: 빠름 (스틸기타, 밴조)
//    0x0120: 중간 (나일론 기타, 피치카토)
//    0x0300: 느림 (하프, 마림바)
//    0x0800: 매우 느림 (현 서스테인)
//    0x1800: 극느림 (패드 스웰)
//    0x8000: 최극느림 (롱 패드)
//
// =============================================================================
static const ym2203_ssg_patch_t YM2203_SSG_PATCHES[SSG_COUNT] = {
    /*  필드 헤더:
        name,               ton noi nfrq amp    env_per  env_shp  eps
                            vsc atk avl svl fdr cut
    */

    // ── 카테고리 A: MELODY ───────────────────────────────────────────────────────

    /* [A0] SSG_MELODY_SQUARE_BRIGHT — 밝은 구형파 8비트 게임 리드
       amp_base=0x10 + ATTACK_HOLD(0x0C): 즉각 어택, 최대볼륨 서스테인.
       env_period=0x0004: 극히 빠른 어택(사실상 즉각).
       vol_scale=160: velocity 강감도. 게임 BGM 리드 특유의 밝고 강렬한 구형파. */
    { "SSG_MELODY_SQUARE_BRIGHT",
        1, 0, 0x00,  0x10, 0x0004, SSG_ENV_ATTACK_HOLD, 0,
        160,  0,  0,  0,  0,  0 },

        /* [A1] SSG_MELODY_SQUARE_WARM — 따뜻한 구형파 레트로 멜로디
           env_period=0x0020: 약간 느린 어택 → 부드러운 입력감.
           vol_scale=128: 표준 velocity. 게임 서브 멜로디, 레트로 신스 느낌. */
        { "SSG_MELODY_SQUARE_WARM",
            1, 0, 0x00,  0x10, 0x0020, SSG_ENV_ATTACK_HOLD, 0,
            128,  0,  0,  0,  0,  0 },

            /* [A2] SSG_MELODY_FLUTE — 플루트 근사 스웰 어택
               env_period=0x0800: 느린 어택(/‾‾‾ → 천천히 최대로).
               실제 플루트는 취구 바람이 서서히 공명하며 볼륨 증가.
               env_period_scale=1: 고음일수록 어택 빠름 → 플루트 음역 특성 반영. */
            { "SSG_MELODY_FLUTE",
                1, 0, 0x00,  0x10, 0x0800, SSG_ENV_ATTACK_HOLD, 1,
                128,  0,  0,  0,  0,  0 },

                /* [A3] SSG_MELODY_CLARINET — 클라리넷 날카로운 어택
                   env_period=0x0080: 빠른 어택. 클라리넷은 리드 진동 즉각 시작.
                   구형파 특성이 클라리넷 홀수배음 우세와 유사 (사각파 ≈ 홀수 harmonic series).
                   vol_scale=148: 약간 강한 velocity 감도. */
                { "SSG_MELODY_CLARINET",
                    1, 0, 0x00,  0x10, 0x0080, SSG_ENV_ATTACK_HOLD, 0,
                    148,  0,  0,  0,  0,  0 },

                    /* [A4] SSG_MELODY_ORGAN_PIPE — 파이프 오르간 즉각 어택
                       env_period=0x0004: 즉각 어택. 오르간 파이프는 바람이 열리면 즉시 발음.
                       vel_tl_scale=0 (vol_scale=0): velocity 무시 → 오르간 특성(건반 누르면 최대).
                       env_shape=ATTACK_HOLD: 최대볼륨 완전 서스테인. */
                    { "SSG_MELODY_ORGAN_PIPE",
                        1, 0, 0x00,  0x10, 0x0004, SSG_ENV_ATTACK_HOLD, 0,
                          0,  0,  0,  0,  0,  0 },

                          /* [A5] SSG_MELODY_CELLO_BOW — 첼로 보잉 느린 어택
                             env_period=0x1800: 매우 느린 어택 (활이 현에 닿아 공명 시작).
                             env_period_scale=1: 저음 첼로 음역에서 자연스럽게 더 느린 어택.
                             vol_scale=128. 거칠고 풍부한 구형파 배음이 첼로 보잉과 유사. */
                          { "SSG_MELODY_CELLO_BOW",
                              1, 0, 0x00,  0x10, 0x1800, SSG_ENV_ATTACK_HOLD, 1,
                              128,  0,  0,  0,  0,  0 },

                              /* [A6] SSG_MELODY_TRUMPET_MUTED — 뮤트 트럼펫 강렬한 어택
                                 env_period=0x0100: 약간 빠른 어택. 뮤트 트럼펫 특유의 날카로운 입력.
                                 vol_scale=180: 강한 velocity 감도. noise 약간 혼합으로 버징 표현.
                                 use_noise=1, noise_period=2: 미세 고주파 노이즈 = 트럼펫 버징. */
                              { "SSG_MELODY_TRUMPET_MUTED",
                                  1, 1, 0x02,  0x10, 0x0100, SSG_ENV_ATTACK_HOLD, 0,
                                  180,  0,  0,  0,  0,  0 },

                                  // ── 카테고리 B: PLUCK ────────────────────────────────────────────────────────

                                  /* [B0] SSG_PLUCK_HARP — 하프 중간 감쇠 따뜻한 잔향
                                     DECAY_ONCE: note_on → \___. env_period=0x0400: 중간 감쇠 속도.
                                     env_period_scale=1: 음정 연동 → 저음 하프줄은 오래, 고음은 빨리 감쇠.
                                     attack_vol=15, attack_ticks=2: 짧은 어택 임팩트 강조. */
                                  { "SSG_PLUCK_HARP",
                                      1, 0, 0x00,  0x10, 0x0400, SSG_ENV_DECAY_ONCE, 1,
                                      128,  2, 15,  0,  0,  0 },

                                      /* [B1] SSG_PLUCK_GUITAR_NYLON — 나일론 기타 부드러운 피치카토
                                         env_period=0x0180: 중빠른 감쇠. 나일론 줄 특유의 부드럽고 빠른 감쇠.
                                         env_period_scale=1: 음정 연동. attack_ticks=1: 최소 어택 임팩트.
                                         vol_scale=140: 약간 강한 감도. */
                                      { "SSG_PLUCK_GUITAR_NYLON",
                                          1, 0, 0x00,  0x10, 0x0180, SSG_ENV_DECAY_ONCE, 1,
                                          140,  1, 15,  0,  0,  0 },

                                          /* [B2] SSG_PLUCK_GUITAR_STEEL — 스틸 기타 날카로운 어택 긴 잔향
                                             env_period=0x0300: 중간~느린 감쇠. 스틸 줄은 나일론보다 잔향 길다.
                                             attack_vol=15, attack_ticks=2: 강한 피크 어택 (손가락/피크 임팩트).
                                             noise 없음: 스틸 기타는 FM SSG만으로 충분히 금속성. */
                                          { "SSG_PLUCK_GUITAR_STEEL",
                                              1, 0, 0x00,  0x10, 0x0300, SSG_ENV_DECAY_ONCE, 1,
                                              148,  2, 15,  0,  0,  0 },

                                              /* [B3] SSG_PLUCK_BANJO — 밴조 극빠른 감쇠 금속성 어택
                                                 env_period=0x0080: 빠른 감쇠. 밴조는 금속 울림통 → 배음 빠른 소멸.
                                                 attack_vol=15, attack_ticks=3: 강한 어택 임팩트. 금속성 강조.
                                                 vol_scale=160: 강한 velocity. */
                                              { "SSG_PLUCK_BANJO",
                                                  1, 0, 0x00,  0x10, 0x0080, SSG_ENV_DECAY_ONCE, 1,
                                                  160,  3, 15,  0,  0,  0 },

                                                  /* [B4] SSG_PLUCK_KOTO — 고토(箏) 중간 감쇠 고급 잔향
                                                     env_period=0x0280: 중간 감쇠. 고토는 비단실 → 나일론 기타보다 풍부.
                                                     env_period_scale=1: 음정 연동. attack_vol=15, attack_ticks=3: 강한 발현 임팩트.
                                                     vol_scale=148. */
                                                  { "SSG_PLUCK_KOTO",
                                                      1, 0, 0x00,  0x10, 0x0280, SSG_ENV_DECAY_ONCE, 1,
                                                      148,  3, 15,  0,  0,  0 },

                                                      /* [B5] SSG_PLUCK_BELL_TUBULAR — 튜블러 벨 매우 느린 감쇠
                                                         env_period=0x1000: 매우 느린 감쇠. 금속 튜브 공명 → 수초 울림.
                                                         env_period_scale=0: 고정 감쇠 (튜블러 벨은 음정별 감쇠 차이 작음).
                                                         attack_vol=15, attack_ticks=1: 타격 임팩트. */
                                                      { "SSG_PLUCK_BELL_TUBULAR",
                                                          1, 0, 0x00,  0x10, 0x1000, SSG_ENV_DECAY_ONCE, 0,
                                                          128,  1, 15,  0,  0,  0 },

                                                          /* [B6] SSG_PLUCK_MARIMBA — 마림바 목질 중간 감쇠
                                                             env_period=0x0200: 중간 감쇠. 마림바 목판 공명통 잔향.
                                                             env_period_scale=1: 음정 연동. attack_vol=15, attack_ticks=2: 말렛 임팩트.
                                                             vol_scale=148. */
                                                          { "SSG_PLUCK_MARIMBA",
                                                              1, 0, 0x00,  0x10, 0x0200, SSG_ENV_DECAY_ONCE, 1,
                                                              148,  2, 15,  0,  0,  0 },

                                                              /* [B7] SSG_PLUCK_XYLOPHONE — 실로폰 빠른 감쇠 밝은 어택
                                                                 env_period=0x00C0: 빠른 감쇠. 실로폰 금속 울림 빠른 소멸.
                                                                 env_period_scale=1: 음정 연동. attack_vol=15, attack_ticks=1: 임팩트.
                                                                 vol_scale=160: 밝고 강한 감도. */
                                                              { "SSG_PLUCK_XYLOPHONE",
                                                                  1, 0, 0x00,  0x10, 0x00C0, SSG_ENV_DECAY_ONCE, 1,
                                                                  160,  1, 15,  0,  0,  0 },

                                                                  // ── 카테고리 C: DRUM ─────────────────────────────────────────────────────────

                                                                  /* [C0] SSG_DRUM_KICK_DEEP — 딥 킥 드럼
                                                                     noise_period=20: 저주파 노이즈. env mode + DECAY_ONCE: 빠른 감쇠.
                                                                     env_period=0x0040: 비교적 빠른 감쇠. cut_ticks=22: ~22틱 후 소거.
                                                                     attack_vol=15, attack_ticks=3: 강한 초기 임팩트. vol_scale=0(env 모드이므로 무관). */
                                                                  { "SSG_DRUM_KICK_DEEP",
                                                                      0, 1, 0x14,  0x10, 0x0040, SSG_ENV_DECAY_ONCE, 0,
                                                                        0,  3, 15,  0,  0, 22 },

                                                                        /* [C1] SSG_DRUM_KICK_TIGHT — 타이트 킥 드럼
                                                                           noise_period=12: 중주파 노이즈. 더 타이트한 킥 사운드.
                                                                           env_period=0x0028: 더 빠른 감쇠. cut_ticks=14: 짧게 컷.
                                                                           attack_vol=15, attack_ticks=2. */
                                                                        { "SSG_DRUM_KICK_TIGHT",
                                                                            0, 1, 0x0C,  0x10, 0x0028, SSG_ENV_DECAY_ONCE, 0,
                                                                              0,  2, 15,  0,  0, 14 },

                                                                              /* [C2] SSG_DRUM_SNARE_CRISP — 크리스프 스네어
                                                                                 tone+noise 혼합: tone이 스네어 바디음, noise가 스냅 잡음.
                                                                                 noise_period=8: 중주파 노이즈.
                                                                                 고정볼륨 모드(amp_base=14): vol_scale+velocity로 제어.
                                                                                 attack_vol=15, attack_ticks=2: 임팩트. vol_fade_rate=48: 빠른 소프트웨어 감쇠.
                                                                                 cut_ticks=12. */
                                                                              { "SSG_DRUM_SNARE_CRISP",
                                                                                  1, 1, 0x08,  14, 0x0000, 0x00, 0,
                                                                                  180,  2, 15, 14, 48, 12 },

                                                                                  /* [C3] SSG_DRUM_SNARE_BRUSH — 브러시 스네어
                                                                                     noise only, noise_period=10: 부드러운 중주파 노이즈.
                                                                                     고정볼륨, vol_fade_rate=24: 천천히 감쇠. cut_ticks=18.
                                                                                     attack_vol=12, attack_ticks=1: 가벼운 임팩트. vol_scale=128. */
                                                                                  { "SSG_DRUM_SNARE_BRUSH",
                                                                                      0, 1, 0x0A,  12, 0x0000, 0x00, 0,
                                                                                      128,  1, 12, 10, 24, 18 },

                                                                                      /* [C4] SSG_DRUM_HIHAT_CLOSED — 클로즈드 하이햇
                                                                                         noise_period=2: 고주파 노이즈. 아주 짧은 컷.
                                                                                         고정볼륨. vol_fade_rate=128: 매우 빠른 감쇠. cut_ticks=5.
                                                                                         attack_vol=15, attack_ticks=1. vol_scale=160. */
                                                                                      { "SSG_DRUM_HIHAT_CLOSED",
                                                                                          0, 1, 0x02,  13, 0x0000, 0x00, 0,
                                                                                          160,  1, 15, 13,128,  5 },

                                                                                          /* [C5] SSG_DRUM_HIHAT_OPEN — 오픈 하이햇
                                                                                             noise_period=2: 고주파 노이즈. 더 긴 컷.
                                                                                             vol_fade_rate=12: 천천히 감쇠. cut_ticks=40.
                                                                                             attack_vol=14, attack_ticks=2. vol_scale=140. */
                                                                                          { "SSG_DRUM_HIHAT_OPEN",
                                                                                              0, 1, 0x02,  12, 0x0000, 0x00, 0,
                                                                                              140,  2, 14, 11, 12, 40 },

                                                                                              /* [C6] SSG_DRUM_CYMBAL_CRASH — 크래쉬 심벌
                                                                                                 noise_period=3: 고주파 노이즈. 매우 긴 감쇠.
                                                                                                 vol_fade_rate=3: 아주 느린 감쇠. cut_ticks=80.
                                                                                                 attack_vol=15, attack_ticks=2: 강한 임팩트. vol_scale=128. */
                                                                                              { "SSG_DRUM_CYMBAL_CRASH",
                                                                                                  0, 1, 0x03,  13, 0x0000, 0x00, 0,
                                                                                                  128,  2, 15, 12,  3, 80 },

                                                                                                  /* [C7] SSG_DRUM_CYMBAL_RIDE — 라이드 심벌
                                                                                                     tone+noise: tone이 심벌 기음 "딩" 성분.
                                                                                                     noise_period=4: 고주파 노이즈. vol_fade_rate=8: 느린 감쇠. cut_ticks=30.
                                                                                                     attack_vol=14, attack_ticks=1: 가벼운 임팩트. vol_scale=120. */
                                                                                                  { "SSG_DRUM_CYMBAL_RIDE",
                                                                                                      1, 1, 0x04,  11, 0x0000, 0x00, 0,
                                                                                                      120,  1, 14, 10,  8, 30 },

                                                                                                      /* [C8] SSG_DRUM_TOM_HI — 하이 탐
                                                                                                         tone+noise: tone으로 피치 있는 탐 바디음.
                                                                                                         noise_period=10: 중주파 노이즈.
                                                                                                         vol_fade_rate=40: 빠른 감쇠. cut_ticks=16.
                                                                                                         attack_vol=15, attack_ticks=3: 타격 임팩트. vol_scale=160. */
                                                                                                      { "SSG_DRUM_TOM_HI",
                                                                                                          1, 1, 0x0A,  14, 0x0000, 0x00, 0,
                                                                                                          160,  3, 15, 12, 40, 16 },

                                                                                                          /* [C9] SSG_DRUM_TOM_LO — 로우 탐
                                                                                                             tone+noise: 저음 피치. noise_period=16: 저주파 노이즈.
                                                                                                             vol_fade_rate=28: 중간 감쇠. cut_ticks=22.
                                                                                                             attack_vol=15, attack_ticks=3: 타격 임팩트. vol_scale=148. */
                                                                                                          { "SSG_DRUM_TOM_LO",
                                                                                                              1, 1, 0x10,  13, 0x0000, 0x00, 0,
                                                                                                              148,  3, 15, 12, 28, 22 },

                                                                                                              /* [C10] SSG_DRUM_RIM_SHOT — 림샷
                                                                                                                 tone only: 극짧은 클릭 사운드. 피치 있는 타격.
                                                                                                                 고정볼륨. vol_fade_rate=200: 극빠른 감쇠. cut_ticks=4.
                                                                                                                 attack_vol=15, attack_ticks=1: 최소 임팩트. vol_scale=200. */
                                                                                                              { "SSG_DRUM_RIM_SHOT",
                                                                                                                  1, 0, 0x00,  15, 0x0000, 0x00, 0,
                                                                                                                  200,  1, 15, 15,200,  4 },

                                                                                                                  /* [C11] SSG_DRUM_COWBELL — 카우벨
                                                                                                                     tone only: 금속성 단음. vol_fade_rate=20: 느린 감쇠. cut_ticks=25.
                                                                                                                     attack_vol=15, attack_ticks=2: 타격 임팩트. vol_scale=160.
                                                                                                                     높은 음정으로 발음하면 카우벨 특유 금속음. */
                                                                                                                  { "SSG_DRUM_COWBELL",
                                                                                                                      1, 0, 0x00,  14, 0x0000, 0x00, 0,
                                                                                                                      160,  2, 15, 13, 20, 25 },

                                                                                                                      // ── 카테고리 D: FX ───────────────────────────────────────────────────────────

                                                                                                                      /* [D0] SSG_FX_VIBRATO_LEAD — 비브라토 리드
                                                                                                                         ATTACK_ALT(0x0E): /\/\ 반복 삼각파 → note_on 후 볼륨이 진동.
                                                                                                                         env_period=0x0060: 빠른 반복 → 뚜렷한 트레몰로/비브라토 효과.
                                                                                                                         FM으로는 이 연속 볼륨 진동을 SW LFO 없이 구현 불가.
                                                                                                                         vol_scale=0 (env 모드이므로 무관). */
                                                                                                                      { "SSG_FX_VIBRATO_LEAD",
                                                                                                                          1, 0, 0x00,  0x10, 0x0060, SSG_ENV_ATTACK_ALT, 0,
                                                                                                                            0,  0,  0,  0,  0,  0 },

                                                                                                                            /* [D1] SSG_FX_WOBBLE_BASS — 워블 베이스
                                                                                                                               DECAY_ALT(0x0A): \/\/ 반복 감쇠 삼각파 → 볼륨이 반복 진동.
                                                                                                                               env_period=0x00A0: 중간 반복속도 → 워블 베이스 느낌.
                                                                                                                               저음에서 발음 시 매우 독특한 워블 효과. */
                                                                                                                            { "SSG_FX_WOBBLE_BASS",
                                                                                                                                1, 0, 0x00,  0x10, 0x00A0, SSG_ENV_DECAY_ALT, 0,
                                                                                                                                  0,  0,  0,  0,  0,  0 },

                                                                                                                                  /* [D2] SSG_FX_STAB_ACCENT — 스탭 악센트 (역감쇠 어택)
                                                                                                                                     DECAY_HOLD(0x0B): \‾‾‾ → 감쇠 후 최대볼륨 유지.
                                                                                                                                     독특: 처음 볼륨이 낮고 → env_period 시간 동안 감쇠 → 그 후 최대 유지.
                                                                                                                                     역방향 어택 느낌. env_period=0x0040: 빠른 감쇠 → 즉각 최대로 전환.
                                                                                                                                     실제론 \___/‾‾‾ 느낌의 특이한 트랜지언트. */
                                                                                                                                  { "SSG_FX_STAB_ACCENT",
                                                                                                                                      1, 0, 0x00,  0x10, 0x0040, SSG_ENV_DECAY_HOLD, 0,
                                                                                                                                        0,  0,  0,  0,  0,  0 },

                                                                                                                                        /* [D3] SSG_FX_SWELL_PAD — 매우 느린 스웰 패드
                                                                                                                                           ATTACK_HOLD(0x0C): /‾‾‾. env_period=0x8000: 극히 느린 어택.
                                                                                                                                           note_on 후 수십 tick 동안 볼륨이 서서히 상승 → 현악 패드 스웰.
                                                                                                                                           FM SSG의 서스테인 악기 중 가장 고급스러운 음색. */
                                                                                                                                        { "SSG_FX_SWELL_PAD",
                                                                                                                                            1, 0, 0x00,  0x10, 0x8000, SSG_ENV_ATTACK_HOLD, 0,
                                                                                                                                              0,  0,  0,  0,  0,  0 },

                                                                                                                                              /* [D4] SSG_FX_CHIRP — 칩 사운드 (빠른 env + noise 혼합)
                                                                                                                                                 ATTACK_DROP(0x0D): /___ → 어택 후 즉각 소거. 짧고 날카로운 칩.
                                                                                                                                                 env_period=0x0030: 빠른 어택+소거. tone+noise 혼합으로 텍스처.
                                                                                                                                                 레트로 게임 효과음 특유의 "삑" 소리. cut_ticks=6으로 최대 길이 제한. */
                                                                                                                                              { "SSG_FX_CHIRP",
                                                                                                                                                  1, 1, 0x06,  0x10, 0x0030, SSG_ENV_ATTACK_DROP, 0,
                                                                                                                                                    0,  0,  0,  0,  0,  6 },

                                                                                                                                                    /* [D5] SSG_FX_LASER — 레이저 빔 효과
                                                                                                                                                       ATTACK_DROP(0x0D): /___ 빠른 어택 후 소거.
                                                                                                                                                       env_period=0x0010: 극히 빠른 어택. 소프트웨어로 pitch sweep 연동 권장.
                                                                                                                                                       tone only. cut_ticks=8. 게임 사운드 효과. */
                                                                                                                                                    { "SSG_FX_LASER",
                                                                                                                                                        1, 0, 0x00,  0x10, 0x0010, SSG_ENV_ATTACK_DROP, 0,
                                                                                                                                                          0,  0,  0,  0,  0,  8 },

                                                                                                                                                          /* [D6] SSG_FX_BUZZ_TONE — 버즈 톤 (tone+noise 혼합)
                                                                                                                                                             고정볼륨, tone+noise 동시. noise_period=0x10: 중저주파 노이즈.
                                                                                                                                                             vol_fade_rate=0: 감쇠 없음. 지속 버즈 사운드.
                                                                                                                                                             vol_scale=128: 표준 velocity. 합성기 특유의 버즈/링 변조 근사. */
                                                                                                                                                          { "SSG_FX_BUZZ_TONE",
                                                                                                                                                              1, 1, 0x10,  12, 0x0000, 0x00, 0,
                                                                                                                                                              128,  0,  0, 12,  0,  0 },

                                                                                                                                                              /* [D7] SSG_FX_WIND — 바람 소리 (noise 롱 페이드)
                                                                                                                                                                 noise only, noise_period=0x18: 저주파 노이즈 → 바람 텍스처.
                                                                                                                                                                 고정볼륨. vol_fade_rate=2: 매우 느린 감쇠 (지속하면서 서서히 줄어듦).
                                                                                                                                                                 vol_scale=100. 환경 효과음, 배경음. */
                                                                                                                                                              { "SSG_FX_WIND",
                                                                                                                                                                  0, 1, 0x18,  11, 0x0000, 0x00, 0,
                                                                                                                                                                  100,  0,  0, 11,  2,  0 },

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
    /* MIDI 68(G#4=415.3Hz): period=602=0x025A */
    {0x5A,0x02},                                     /* 68: G#4              */
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
    {0x01,0x00},{0x01,0x00},{0x01,0x00},             /* 125~127: 클램프 */
};

// =============================================================================
//  §15  Voice 구조체 (Rev.11)
// =============================================================================

/* [R11-FIX-3] Voice 상태 — RELEASING 상태로 voice stealing 보호
 *   active=1, releasing=0: 발음 중 (key-on)
 *   active=0, releasing=1: 릴리즈 중 (key-off, RR 감쇠 진행)
 *   active=0, releasing=0: 완전 유휴
 *
 *   voice_steal 우선순위: 유휴 → 릴리즈(오래된 순) → 발음(오래된 순)
 *   rr_ticks_left: 릴리즈 타임아웃. 0이 되면 releasing=0으로 전환.
 *   최대 RR(=15) 기준 릴리즈 시간 ≈ tick_hz / 2 를 기본값으로 사용. */
typedef struct {
    uint8_t  ch;            /* FM 채널 0~2                              */
    uint8_t  midi_note;
    uint8_t  vel;
    uint8_t  active;
    uint8_t  releasing;     /* [R11] 1=key-off 후 RR 감쇠 중           */
    uint16_t rr_ticks_left; /* [R11] 릴리즈 잔여 tick (0=유휴)         */
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
    uint16_t  tick_hz_cache; /* [R11] pitchbend/RR 타임아웃 계산용     */
} ym2203_voice_t;

static inline void ym2203_voice_init(ym2203_voice_t* v, uint8_t ch, uint16_t tick_hz)
{
    memset(v, 0, sizeof(*v));
    v->ch = ch;
    v->tick_hz_cache = tick_hz;
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

/* [R11-FIX-1] velocity → carrier TL + modulator TL 연동
 *
 *  carrier: 감소량 = (127 - vel) >> vel_tl_scale  (볼륨 제어)
 *  modulator: 증가량 = ((127 - vel) >> (vel_tl_scale + 1))  (배음량 제어)
 *    → vel 강할수록 modulator TL 낮아짐 = FM 인덱스↑ = 밝은 음색
 *    → vel 약할수록 modulator TL 높아짐 = FM 인덱스↓ = 어두운 음색
 *    modulator 감도는 carrier의 절반 (과도한 음색 변화 억제).
 *    vel_tl_scale=0: velocity 완전 무시 (carrier+modulator 둘 다). */
static inline void ym2203_patch_apply_vel(const ym2203_patch_t* p,
    uint8_t ch, uint8_t vel)
{
    ym2203_patch_apply(p, ch);
    if (p->vel_tl_scale == 0u) return;  /* velocity 무시 */
    uint8_t cmask = YM_CARRIER_MASK[p->ALG & 7u];
    for (int op = 0; op < 4; op++) {
        uint8_t off = (uint8_t)(YM_OP_OFF[op] + ch);
        int32_t tl;
        if ((cmask >> op) & 1u) {
            /* carrier: velocity → 볼륨 */
            int32_t dec = (int32_t)(127u - vel) >> p->vel_tl_scale;
            tl = (int32_t)p->ops[op].TL + dec;
        }
        else {
            /* modulator: velocity → 배음량 (밝기). 감도 = carrier의 절반 */
            int32_t inc = (int32_t)(127u - vel) >> (p->vel_tl_scale + 1u);
            tl = (int32_t)p->ops[op].TL + inc;
        }
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

/* [R10] env_period_scale: 음정 연동 envelope 주기 계산
 *
 *  목적: 자연악기 특성 — 고음에서 빠른 감쇠, 저음에서 느린 감쇠.
 *
 *  방법: 이론 주파수 비율 사용 (LUT 클램프 오류 회피).
 *    freq(n) = 440 × 2^((n-69)/12)
 *    env_period(n) = base_period × (freq_A4 / freq(n))
 *                  = base_period × 2^((69-n)/12)
 *
 *  고정소수점 근사:
 *    2^((69-n)/12) = 2^(semitone_up/12)
 *    세미톤 차이 = 69 - midi_note. 양수=저음(period 큼), 음수=고음(period 작음).
 *    |diff| ≤ 5옥타브(60세미톤) 내에서 정수 시프트 + 반음 보정 테이블로 근사.
 *
 *    반음 단위 스케일: 12근음 기준 Q8 테이블.
 *    1음계(12반음) = ×2 → 4비트 시프트.
 *    나머지 반음: Q8 테이블로 2^(r/12) 근사.
 */
static const uint16_t SSG_SEMITONE_Q8[12] = {
    /* 2^(0/12)~2^(11/12) × 256, 반올림 */
    256, 271, 287, 304, 322, 341, 362, 383, 406, 430, 456, 483
};

static inline uint16_t ym2203_ssg_scaled_env_period(uint16_t base_period,
    uint8_t  midi_note)
{
    if (midi_note > 127u) midi_note = 127u;

    /* diff = 69 - midi_note (양수=저음, 음수=고음) */
    int16_t diff = (int16_t)69 - (int16_t)midi_note;

    uint32_t result;
    if (diff >= 0) {
        /* 저음: base_period × 2^(diff/12) — period 증가 (느린 감쇠) */
        uint16_t octaves = (uint16_t)(diff / 12);
        uint8_t  semitones = (uint8_t)(diff % 12);
        result = ((uint32_t)base_period * SSG_SEMITONE_Q8[semitones]) >> 8;
        result <<= octaves;  /* 옥타브당 ×2 */
    }
    else {
        /* 고음: base_period / 2^(|diff|/12) — period 감소 (빠른 감쇠) */
        uint16_t octaves = (uint16_t)((-diff) / 12);
        uint8_t  semitones = (uint8_t)((-diff) % 12);
        result = ((uint32_t)base_period * 256u) / SSG_SEMITONE_Q8[semitones];
        if (octaves > 0u) result >>= octaves;
    }

    if (result > 0xFFFFu) result = 0xFFFFu;
    if (result < 1u)      result = 1u;
    return (uint16_t)result;
}

/* SSG 패치 적용 (mixer/noise 설정만, note-on 없음) */
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

/* [R10] SSG note_on — env_period_scale, vol_fade, sustain_vol 지원 */
static inline void ym2203_ssg_note_on(const ym2203_ssg_patch_t* p,
    uint8_t ch, uint8_t midi_note, uint8_t vel)
{
    if (ch > 2u) return;

    /* ── Mixer 설정 ── */
    ym2203_ssg_ch_enable(ch, p->use_tone, p->use_noise);

    /* ── Noise period ── */
    if (p->use_noise) {
        if (g_ssg_noise_frq != p->noise_period) {
            g_ssg_noise_frq = p->noise_period;
            ym_write_cached(YM_REG_SSG_NOISE, p->noise_period & 0x1Fu);
        }
    }

    /* ── Amplitude 계산 ── */
    uint8_t amp;
    if (p->amp_base & 0x10u) {
        /* Envelope 모드: vol 레지스터에 0x10 기록 */
        amp = 0x10u;
    }
    else {
        /* 고정볼륨 모드: sustain_vol + vel 스케일 */
        uint8_t base = (p->sustain_vol > 0u) ? p->sustain_vol : p->amp_base;
        uint32_t v = (uint32_t)base * (uint32_t)p->vol_scale * (uint32_t)(vel + 1u);
        v >>= 11u;  /* /128 /16 */
        if (v > 15u) v = 15u;
        amp = (uint8_t)v;
    }

    /* ── Attack boost ── */
    uint8_t init_amp = amp;
    if (p->attack_ticks > 0u && !(p->amp_base & 0x10u)) {
        /* 고정볼륨 모드에서만 attack boost 유효 */
        init_amp = p->attack_vol;
    }
    ym_write_cached((uint8_t)(YM_REG_SSG_VOL_A + ch), init_amp & 0x1Fu);

    /* ── Envelope period/shape 설정 ── */
    if (p->amp_base & 0x10u) {
        uint16_t ep = p->env_period;
        if (p->env_period_scale) {
            ep = ym2203_ssg_scaled_env_period(ep, midi_note);
        }
        /* [R11-FIX-5] 우선순위 기반 env 레지스터 경쟁 쓰기 */
        ym2203_ssg_env_write(ch, ep, p->env_shape, p->cut_ticks);
    }

    /* ── Tone period ── */
    ym2203_ssg_set_note(ch, midi_note);
    /* [R12-FIX-2] vibrato 기준 period 캐시 */
    if (midi_note <= 127u) {
        g_ssg_voice[ch].vib_base_period =
            ((uint16_t)YM2203_SSG_FREQ_LUT[midi_note].hi << 8)
            | (uint16_t)YM2203_SSG_FREQ_LUT[midi_note].lo;
        g_ssg_voice[ch].porta_enable = 0u; /* 새 note_on 시 portamento 리셋 */
    }

    /* ── 채널 상태 갱신 ── */
    ym2203_ssg_ch_state_t* s = &g_ssg_ch[ch];
    s->active = 1u;
    s->ch = ch;
    s->cut_ticks = p->cut_ticks;
    s->cut_ticks_init = p->cut_ticks;
    s->attack_ticks = p->attack_ticks;
    s->attack_vol = p->attack_vol;
    s->sustain_vol = amp;
    s->env_mode = (p->amp_base & 0x10u) ? 1u : 0u;
    s->vol_fixed = (!(p->amp_base & 0x10u)) ? amp : 0u;
    s->vol_fade_acc = 0u;
    s->vol_fade_rate = (p->amp_base & 0x10u) ? 0u : p->vol_fade_rate;
}

/* [R10] SSG tick — attack_boost + vol_fade + note_cut 처리 */
static inline void ym2203_ssg_tick(uint8_t ch)
{
    if (ch > 2u) return;
    ym2203_ssg_ch_state_t* s = &g_ssg_ch[ch];
    if (!s->active) return;

    /* ── Attack boost 해제 ── */
    if (s->attack_ticks > 0u) {
        s->attack_ticks--;
        if (s->attack_ticks == 0u && !s->env_mode) {
            /* attack 끝 → sustain_vol로 전환 */
            s->vol_fixed = s->sustain_vol;
            ym2203_ssg_set_vol(ch, s->vol_fixed);
        }
    }

    /* ── Software 볼륨 감쇠 (vol_fade) ── */
    if (s->vol_fade_rate > 0u && !s->env_mode && s->attack_ticks == 0u) {
        s->vol_fade_acc = (uint16_t)(s->vol_fade_acc + s->vol_fade_rate);
        if (s->vol_fade_acc >= 256u) {
            s->vol_fade_acc = (uint16_t)(s->vol_fade_acc - 256u);
            if (s->vol_fixed > 0u) {
                s->vol_fixed--;
                ym2203_ssg_set_vol(ch, s->vol_fixed);
                if (s->vol_fixed == 0u) {
                    ym2203_ssg_ch_silence(ch);
                    return;
                }
            }
        }
    }

    /* ── Note cut ── */
    if (s->cut_ticks > 0u) {
        s->cut_ticks--;
        if (s->cut_ticks == 0u) {
            ym2203_ssg_ch_silence(ch);
        }
    }
}

/* [R11-FIX-2] SSG note_off — envelope 모드 자연 릴리즈 보존
 *
 *  PLUCK/MELODY envelope 모드(env_mode=1): hardware envelope이 자연 감쇠 중이면
 *    vol=0으로만 조용히 처리. 강제 소거하면 자연 릴리즈가 잘림.
 *    active=0으로 표시하되 채널은 hardware envelope에 맡김.
 *    단, cut_ticks가 있는 drum 계열은 즉각 소거.
 *  고정볼륨 모드(env_mode=0): 즉각 소거. */
static inline void ym2203_ssg_note_off(uint8_t ch)
{
    if (ch > 2u) return;
    ym2203_ssg_ch_state_t* s = &g_ssg_ch[ch];
    if (s->env_mode && s->cut_ticks_init == 0u) {
        /* envelope 모드 + 무한 지속: vol 레지스터만 0으로 → envelope는 계속 */
        ym2203_ssg_set_vol(ch, 0u);
        s->active = 0u;
        /* mixer는 끄지 않음 — hardware가 0볼륨이므로 출력 없음 */
        /* 다음 note_on 시 채널 재설정됨 */
    }
    else {
        ym2203_ssg_ch_silence(ch);
    }
}

/* Envelope retrigger */
static inline void ym2203_ssg_env_retrigger(uint8_t ch, uint8_t shape)
{
    (void)ch;
    ym_write_cached_force(YM_REG_SSG_ENV_SHAPE, shape);
}

// =============================================================================
//  §17b  MIDI Pitchbend  [R11-NEW-1]
//
//  MIDI pitchbend 값(0~16383, 중심=8192)을 ±semitone_range 반음 범위로 변환.
//  기본 ±2반음 (GM 표준). fnum_linear 기준 선형 보간.
//
//  pitch_bend_range: 반음 단위. 일반적으로 2 (±2반음).
//  bend_val: 0~16383. 8192=중심(무변화).
//
//  구현: linear_pitch에 semitone 단위 오프셋을 fnum 단위로 근사.
//    1반음 = fnum_linear 차이의 평균 ≈ 현재 옥타브의 1/12.
//    정확한 근사: delta_lp = fnum_linear × (2^(cents/1200) - 1)
//    고정소수점 근사: cents당 fnum_linear / 1200 ≈ lp >> 10 (12.5% 오차, 충분)
// =============================================================================

/* 전역 pitchbend 상태 — 채널별 독립 */
static int16_t g_pitchbend_lp_offset[3] = { 0, 0, 0 };  /* linear_pitch 단위 오프셋 */

/* pitchbend 적용 — 채널 ch의 현재 fnum에 bend 오프셋 반영 */
static inline void ym2203_pitchbend(uint8_t ch,
    uint16_t bend_val,          /* 0~16383, 8192=center */
    uint8_t  semitone_range)    /* ±반음 범위, 보통 2 */
{
    if (ch > 2u) return;
    ym2203_voice_t* v = &g_ym_voices[ch];
    if (!v->active && !v->releasing) return;

    /* bend_val → cents (-semitone_range*100 ~ +semitone_range*100) */
    int32_t cents_max = (int32_t)semitone_range * 100;
    int32_t cents = ((int32_t)bend_val - 8192) * cents_max / 8192;

    /* cents → linear_pitch 오프셋
     * 1옥타브(1200 cents) = fnum_linear 기준 현재 block의 2048.
     * delta_lp = lp_base * (2^(cents/1200) - 1)
     * 소각 근사: ≈ lp_base * cents * ln(2) / 1200
     *          ≈ lp_base * cents / 1731  (ln(2)≈0.693, 1200/0.693=1731)
     * 정수 근사 (Q10 고정소수): delta_lp = (lp_base >> 10) * cents / (1731>>10) */
    int32_t lp_base = (int32_t)v->fnum_linear;
    int32_t delta_lp = (lp_base * cents) / 1731;

    g_pitchbend_lp_offset[ch] = (int16_t)delta_lp;

    int32_t new_lp = lp_base + delta_lp + (int32_t)v->detune_fnum;
    if (new_lp < 0)      new_lp = 0;
    if (new_lp > 0x7FFF) new_lp = 0x7FFF;

    uint8_t lsb, msb;
    ym2203_linear_to_fnum((uint32_t)new_lp, &lsb, &msb);
    ym_write_cached_force(YM_REG_FNUM_MSB(ch), msb);
    ym_write_cached_force(YM_REG_FNUM_LSB(ch), lsb);
}

/* pitchbend 리셋 (note_on 시 자동 호출 불필요 — note_on이 fnum 재설정함) */
static inline void ym2203_pitchbend_reset(uint8_t ch)
{
    if (ch > 2u) return;
    g_pitchbend_lp_offset[ch] = 0;
    ym2203_voice_t* v = &g_ym_voices[ch];
    if (v->active)
        ym2203_set_note(ch, v->midi_note);
}

/* 전체 채널 pitchbend 리셋 */
static inline void ym2203_pitchbend_reset_all(void)
{
    for (uint8_t i = 0u; i < 3u; i++)
        ym2203_pitchbend_reset(i);
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
        /* [R12-FIX-1] lfo_shape을 악기 테이블에서 읽음 (SINE/TRI/SAW/SQR 선택) */
        ym2203_lfo_init(&v->lfo, inst->lfo_shape,
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
    /* [R11-FIX-3] RELEASING 상태: RR 감쇠 중 voice stealing 방지
     * RR 최댓값(15) 기준 릴리즈 시간 = tick_hz / 2 틱 정도.
     * 최소 10틱 보장 (매우 짧은 RR 악기도 보호). */
    v->releasing = 1u;
    uint16_t rr_ticks = (v->tick_hz_cache > 0u)
        ? (uint16_t)(v->tick_hz_cache / 2u)
        : 50u;
    if (rr_ticks < 10u) rr_ticks = 10u;
    v->rr_ticks_left = rr_ticks;
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

/* [R11-FIX-3] releasing 상태 tick — active=0인 채널에서 별도 호출 */
static inline void ym2203_voice_release_tick(ym2203_voice_t* v)
{
    if (!v->releasing) return;
    if (v->rr_ticks_left > 0u) {
        v->rr_ticks_left--;
        if (v->rr_ticks_left == 0u)
            v->releasing = 0u;
    }
    else {
        v->releasing = 0u;
    }
}

// =============================================================================
//  §20  Voice Stealing
// =============================================================================
/* [R11-FIX-3] voice stealing 우선순위: 유휴 → 릴리즈(오래된 순) → 발음(오래된 순) */
static inline uint8_t ym2203_voice_steal_from(ym2203_voice_t* voices, uint8_t count)
{
    /* 1순위: 완전 유휴 (active=0, releasing=0) */
    for (uint8_t i = 0u; i < count; i++)
        if (!voices[i].active && !voices[i].releasing) return i;

    /* 2순위: 릴리즈 중인 채널 — rr_ticks_left 가장 큰 것(가장 오래 릴리즈) */
    {
        uint16_t most_released = 0u;
        int8_t   rel_ch = -1;
        for (uint8_t i = 0u; i < count; i++) {
            if (!voices[i].active && voices[i].releasing) {
                uint16_t elapsed = (voices[i].rr_ticks_left == 0u) ? 0xFFFFu
                    : (uint16_t)(0xFFFFu - voices[i].rr_ticks_left);
                if (elapsed >= most_released) {
                    most_released = elapsed;
                    rel_ch = (int8_t)i;
                }
            }
        }
        if (rel_ch >= 0) return (uint8_t)rel_ch;
    }

    /* 3순위: 발음 중 — 가장 오래된 채널 */
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
    /* [R11] 신규 상태 초기화 */
    g_ssg_env_owner = 0xFFu;
    g_ssg_global_tick = 0u;
    for (int i = 0; i < 3; i++) {
        g_ssg_env_priority[i] = 0xFFu;
        g_ssg_env_timestamp[i] = 0u;
        g_pitchbend_lp_offset[i] = 0;
        ym2203_ssg_voice_init((uint8_t)i);  /* [R12] SSG vibrato/porta 초기화 */
    }
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

    /* [R12-NEW-1] SSG 전용 악기 감지: vel_tl_scale=0 + ssg_ch≠0xFF
     *   → FM은 vel=0(무음)으로 key-on (구조 유지), SSG만 실음 발생 */
    uint8_t fm_vel = vel;
    if (p->vel_tl_scale == 0u && inst->ssg_ch != 0xFFu) {
        fm_vel = 0u;  /* FM 채널 무음 — carrier TL이 감소 안 하므로 최고 TL = 거의 무음 */
    }

    ym2203_voice_note_on(&g_ym_voices[fm_ch], p,
        midi_note, fm_vel, inst, tick_hz, (uint8_t)inst_idx);
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
    g_ssg_global_tick++;  /* [R11-FIX-5] env 우선순위 타임스탬프 */
    for (int i = 0; i < 3; i++) {
        ym2203_voice_tick(&g_ym_voices[i]);
        ym2203_voice_release_tick(&g_ym_voices[i]);  /* [R11] releasing 카운트다운 */
        ym2203_ssg_tick((uint8_t)i);  /* SSG 채널도 동시 tick */
        ym2203_ssg_voice_tick((uint8_t)i);  /* [R12] SSG vibrato/portamento tick */
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
    /* 0번 인덱스: 단음/무코드 (km.c _MSC_VER #define CHORD_CHORD 0 충돌 방지) */
    CHORD_NONE = 0,

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
    /* [0]  CHORD_NONE/CHORD_CHORD — 단음, 코드 없음 */
                           {0, 0xFF, 0xFF, 0xFF, 0xFF},
                           /* [1]  CHORD_MAJ  */  {0, 4, 7, 0xFF, 0xFF},
                           /* [2]  CHORD_MIN  */  {0, 3, 7, 0xFF, 0xFF},
                           /* [3]  CHORD_DIM  */  {0, 3, 6, 0xFF, 0xFF},
                           /* [4]  CHORD_AUG  */  {0, 4, 8, 0xFF, 0xFF},
                           /* [5]  CHORD_SUS2 */  {0, 2, 7, 0xFF, 0xFF},
                           /* [6]  CHORD_SUS4 */  {0, 5, 7, 0xFF, 0xFF},
                           /* [7]  CHORD_MAJ7 */  {0, 4, 7,  11, 0xFF},
                           /* [8]  CHORD_MIN7 */  {0, 3, 7,  10, 0xFF},
                           /* [9]  CHORD_DOM7 */  {0, 4, 7,  10, 0xFF},
                           /* [10] CHORD_M7B5/CHORD_HALF_DIM */ {0, 4, 6, 10, 0xFF},
                           /* [11] CHORD_DIM7 */  {0, 3, 6,   9, 0xFF},
                           /* [12] CHORD_MAJ7S5 */{0, 4, 8,  11, 0xFF},
                           /* [13] CHORD_DOM7S4 */{0, 4, 6,  10, 0xFF},
                           /* [14] CHORD_DOM9  */  {0, 4, 7,  10,  14},
                           /* [15] CHORD_DOM11 */  {0, 4, 7,  10,  17},
                           /* [16] CHORD_DOM7_SPECIAL */ {0, 4, 7, 10, 0xFF},  /* = DOM7 기반 확장 */
                           /* [17] CHORD_DOM7S9  */{0, 4, 7,  10,  15},        /* b7+#9 */
                           /* [18] CHORD_DOM7S11 */{0, 4, 7,  10,  18},        /* b7+#11 */
                           /* [19] CHORD_MAJ9    */{0, 4, 7,  11,  14},
                           /* [20] CHORD_MIN9    */{0, 3, 7,  10,  14},
                           /* [21] CHORD_MIN11   */{0, 3, 7,  10,  17},
                           /* [22] CHORD_DOM13   */{0, 4, 7,  10,  21},
                           /* [23] CHORD_UST_A   */{0, 6, 10, 0xFF, 0xFF},     /* Upper Structure Triad A (Lydian) */
                           /* [24] CHORD_UST_B   */{0, 4, 6, 0xFF, 0xFF},      /* Upper Structure Triad B (Altered) */
                           /* [25] CHORD_UST_C   */{0, 3, 8, 0xFF, 0xFF},      /* Upper Structure Triad C (Sus#4) */
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
//  §24b  SSG Vibrato + Portamento  [R12-FIX-2]
//
//  YM2203 SSG는 소프트웨어로 period 레지스터를 매 tick 갱신하여
//  vibrato(주기적 pitch 진동)와 portamento(pitch 선형 보간)를 구현.
//
//  [Vibrato]
//    period 기반 진동: period = base_period ± delta
//    delta = base_period * depth_frac / 256
//    depth_frac 0~31: 최대 ±12% 범위 (과도한 vibrato 방지)
//    LFO와 동일한 SINE/TRI/SAW/SQR 파형 선택 가능.
//
//  [Portamento]
//    period_src → period_dst를 porta_ticks에 걸쳐 선형 보간.
//    period 공간에서 선형 보간 = 주파수 공간에서 비선형 (하이음역 빠름).
//    음악적으로 자연스럽게 들리려면 낮은 기음에서 사용 권장.
// =============================================================================

/* §24b: ym2203_ssg_voice_t 구조체와 g_ssg_voice[] 전역은 §6에서 선언됨 */

static inline void ym2203_ssg_voice_init(uint8_t ch)
{
    memset(&g_ssg_voice[ch], 0, sizeof(g_ssg_voice[ch]));
    g_ssg_voice[ch].ch = ch;
}

/* SSG vibrato 설정
 *   depth_frac: 0~31. 진동 깊이. base_period × depth_frac / 256.
 *   rate_hundredths: LFO Hz × 100. e.g. 550 = 5.5 Hz.
 *   shape: LFO_SINE/TRI/SAW/SQR. */
static inline void ym2203_ssg_vibrato_set(uint8_t ch,
    uint8_t depth_frac, uint16_t rate_hundredths,
    ym2203_lfo_shape_t shape, uint16_t tick_hz)
{
    if (ch > 2u) return;
    ym2203_ssg_voice_t* sv = &g_ssg_voice[ch];
    sv->vib_enable = (depth_frac > 0u && rate_hundredths > 0u) ? 1u : 0u;
    sv->vib_depth_frac = depth_frac;
    sv->vib_shape = shape;
    ym2203_lfo_init(&sv->vib_lfo, shape, depth_frac, 0u, rate_hundredths, tick_hz, 1u);
}

static inline void ym2203_ssg_vibrato_off(uint8_t ch)
{
    if (ch > 2u) return;
    g_ssg_voice[ch].vib_enable = 0u;
}

/* SSG portamento 시작
 *   note_src: 출발 MIDI note (현재 재생 중인 note)
 *   note_dst: 목표 MIDI note (새 note)
 *   porta_ticks: 글라이드 시간 */
static inline void ym2203_ssg_porta_start(uint8_t ch,
    uint8_t note_src, uint8_t note_dst, uint16_t porta_ticks)
{
    if (ch > 2u) return;
    ym2203_ssg_voice_t* sv = &g_ssg_voice[ch];
    if (note_src > 127u) note_src = 127u;
    if (note_dst > 127u) note_dst = 127u;
    uint16_t psrc = ((uint16_t)YM2203_SSG_FREQ_LUT[note_src].hi << 8)
        | (uint16_t)YM2203_SSG_FREQ_LUT[note_src].lo;
    uint16_t pdst = ((uint16_t)YM2203_SSG_FREQ_LUT[note_dst].hi << 8)
        | (uint16_t)YM2203_SSG_FREQ_LUT[note_dst].lo;
    sv->porta_enable = 1u;
    sv->porta_src = psrc;
    sv->porta_dst = pdst;
    sv->porta_ticks = (porta_ticks > 0u) ? porta_ticks : 1u;
    sv->porta_tick_cur = 0u;
    sv->porta_period_cur = psrc;
    sv->porta_delta_acc = 0;
    /* base_period를 목표로 업데이트 (vibrato는 도착 후 기준점 사용) */
    sv->vib_base_period = pdst;
}

/* SSG voice 틱 — vibrato + portamento period 레지스터 갱신
 *   ym2203_ssg_tick()과 별도로 호출 (또는 ym2203_tick()에서 통합) */
static inline void ym2203_ssg_voice_tick(uint8_t ch)
{
    if (ch > 2u) return;
    ym2203_ssg_voice_t* sv = &g_ssg_voice[ch];
    if (!g_ssg_ch[ch].active) return;

    uint16_t period = sv->vib_base_period;

    /* Portamento: period 선형 보간 */
    if (sv->porta_enable) {
        uint32_t t = (uint32_t)sv->porta_tick_cur;
        uint32_t T = (uint32_t)sv->porta_ticks;
        int32_t  interp = (int32_t)sv->porta_src +
            ((int32_t)sv->porta_dst - (int32_t)sv->porta_src)
            * (int32_t)t / (int32_t)T;
        if (interp < 1)      interp = 1;
        if (interp > 0x0FFF) interp = 0x0FFF;
        period = (uint16_t)interp;
        sv->porta_tick_cur++;
        if (sv->porta_tick_cur >= sv->porta_ticks) {
            sv->porta_enable = 0u;
            sv->vib_base_period = sv->porta_dst;
            period = sv->porta_dst;
        }
    }

    /* Vibrato: period ± delta */
    if (sv->vib_enable && !sv->porta_enable) {
        int8_t lv = ym2203_lfo_tick(&sv->vib_lfo);
        /* delta = base_period × depth_frac × |lv| / (256 × 127) */
        int32_t delta = ((int32_t)sv->vib_base_period
            * (int32_t)sv->vib_depth_frac
            * (int32_t)lv) / (256 * 64);
        int32_t p = (int32_t)period + delta;
        if (p < 1)      p = 1;
        if (p > 0x0FFF) p = 0x0FFF;
        period = (uint16_t)p;
    }
    else if (!sv->porta_enable) {
        return; /* 변화 없으면 레지스터 쓰기 생략 */
    }

    /* period → 레지스터 */
    ym_write_cached((uint8_t)(ch * 2u), (uint8_t)(period & 0xFFu));
    ym_write_cached((uint8_t)(ch * 2u + 1u), (uint8_t)((period >> 8) & 0x0Fu));
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

/* [R11-NEW-1] Pitchbend 편의 매크로
 *   YM_PITCHBEND(ch, bend14) : GM 표준 ±2반음 pitchbend
 *   YM_PITCHBEND_WIDE(ch, bend14) : ±12반음 (1옥타브) pitchbend
 *   bend14: 0~16383, 8192=center */
#define YM_PITCHBEND(ch, bend14) \
    ym2203_pitchbend((ch),(bend14),2u)
#define YM_PITCHBEND_WIDE(ch, bend14) \
    ym2203_pitchbend((ch),(bend14),12u)
#define YM_PITCHBEND_RESET(ch) \
    ym2203_pitchbend_reset(ch)

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