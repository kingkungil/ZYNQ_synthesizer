#!/usr/bin/env python3
"""
gen_luts.py  –  vca_compressor Rev.5 LUT 생성 스크립트

생성 파일:
  log2_frac_lut.hex   256 entries × 8-bit   (기존)
  exp2_frac_lut.hex   256 entries × 9-bit   (기존)
  knee_quad_lut.hex   256 entries × 9-bit   (기존)
  sqrt_lut.hex        512 entries × 11-bit  [FIX-2 신규]
  recip_lut.hex       256 entries × 8-bit   [FIX-3 신규]

사용법:
  python3 gen_luts.py

출력 파일을 Vivado 프로젝트 디렉토리 또는 $readmemh 경로에 배치하세요.
"""

import math
import os

OUTPUT_DIR = "."


# ─────────────────────────────────────────────────────────────────────────────
#  기존 LUT (Rev.4에서 사용)
# ─────────────────────────────────────────────────────────────────────────────

def gen_log2_frac_lut():
    """
    log2_frac_lut.hex  (256 × 8-bit)
    addr = fractional mantissa after removing leading 1  (8-bit)
    data = round(log2(1 + addr/256) * 256)  범위 [0..255]

    설명:
      abs_x의 정규화된 mantissa m ∈ [1, 2)
      addr = (m - 1) * 256  ←  leading 1 제거 후 8-bit
      data = log2(m) * 256
    """
    entries = []
    for i in range(256):
        m = 1.0 + i / 256.0
        v = round(math.log2(m) * 256)
        v = max(0, min(255, v))
        entries.append(v)

    path = os.path.join(OUTPUT_DIR, "log2_frac_lut.hex")
    with open(path, "w") as f:
        for v in entries:
            f.write(f"{v:02X}\n")
    print(f"[OK] {path}  ({len(entries)} entries, max={max(entries)})")
    return entries


def gen_exp2_frac_lut():
    """
    exp2_frac_lut.hex  (256 × 9-bit)
    addr = fractional part of gain_neg  [7:0]
    data = round(2^(-(addr/256)) * 256)  범위 [128..256] → 9-bit

    설명:
      gain_neg = integer.frac  (dB 단위 감쇠량, log2 도메인)
      exp2_frac = 2^(-frac)
      정수부는 EXP_USE에서 우측 시프트로 처리
    """
    entries = []
    for i in range(256):
        v = round(math.pow(2.0, -(i / 256.0)) * 256)
        v = max(0, min(511, v))
        entries.append(v)

    path = os.path.join(OUTPUT_DIR, "exp2_frac_lut.hex")
    with open(path, "w") as f:
        for v in entries:
            f.write(f"{v:03X}\n")
    print(f"[OK] {path}  ({len(entries)} entries, range=[{min(entries)},{max(entries)}])")
    return entries


def gen_knee_quad_lut():
    """
    knee_quad_lut.hex  (256 × 9-bit)
    addr = u ∈ [0..255]  (normalized knee position)
    data = round(u^2 / 256)  범위 [0..255] → SSL soft knee quadratic factor

    설명:
      SSL knee: gain_applied = gain_full * (u^2 / 256)
      u=0 → factor=0 (threshold에서 완전히 linear)
      u=255 → factor≈255 (≈ full compression)
    """
    entries = []
    for i in range(256):
        v = round(i * i / 256)
        v = max(0, min(255, v))
        entries.append(v)

    path = os.path.join(OUTPUT_DIR, "knee_quad_lut.hex")
    with open(path, "w") as f:
        for v in entries:
            f.write(f"{v:03X}\n")
    print(f"[OK] {path}  ({len(entries)} entries, range=[{min(entries)},{max(entries)}])")
    return entries


# ─────────────────────────────────────────────────────────────────────────────
#  신규 LUT (Rev.5)
# ─────────────────────────────────────────────────────────────────────────────

def gen_sqrt_lut():
    """
    sqrt_lut.hex  (512 × 11-bit)  [FIX-2]

    addr = rms_sq_acc[20:12]  (상위 9-bit, 범위 [0..511])
    data = round(sqrt(addr / 512) * 1024)  범위 [0..1023]

    설명:
      rms_sq_acc는 leaky-integrated mean-square, Q-scale은 sq_full[47:12].
      상위 9-bit를 주소로 사용하므로 유효 분해능은 9-bit.
      sqrt 결과는 Q1.10 형식 (1024 = 1.0).

      RMS amplitude 복원:
        rms_sq_acc ≈ E[x^2] * 2^12  (IIR 결과, Q12 스케일)
        rms_sq_acc[20:12] ≈ E[x^2] * 2^0  (MSB 9-bit 추출)
        sqrt(rms_sq_acc[20:12] / 512) ≈ rms / 2^(log2(512)/2)
        → amplitude 복원 시 MSB에 보정 +6을 더해 ST_ENV에서 처리
    """
    entries = []
    for i in range(512):
        v = round(math.sqrt(i / 512.0) * 1024)
        v = max(0, min(1023, v))
        entries.append(v)

    path = os.path.join(OUTPUT_DIR, "sqrt_lut.hex")
    with open(path, "w") as f:
        for v in entries:
            f.write(f"{v:03X}\n")
    print(f"[OK] {path}  ({len(entries)} entries, range=[{min(entries)},{max(entries)}])")

    # 검증
    errors = []
    for i in range(512):
        expected = math.sqrt(i / 512.0) * 1024
        got = entries[i]
        if abs(got - expected) > 1.0:
            errors.append((i, expected, got))
    if errors:
        print(f"  WARNING: {len(errors)} entries exceed ±1 LSB error")
    else:
        print(f"  Verified: all entries within ±1 LSB")
    return entries


def gen_recip_lut():
    """
    recip_lut.hex  (256 × 8-bit)  [FIX-3]

    addr = knee_half 정규화 fractional  (leading 1 제거 후 8-bit, 범위 [0..255])
    data = min(round(256 / (1 + addr/256)), 255)

    설명:
      knee_half의 정규화된 mantissa m ∈ [1, 2)
        addr = (m - 1) * 256
        data = 256 / m  (Q8 reciprocal)

      사용 방법 (ST_KNEE_U):
        u_prod = over_raw * recip_data_r      (13 × 8 = 21-bit)
        u8     = u_prod >> (kh_msb_r + 1)    ([0..127] 범위)

      오차 분석:
        recip 오차: ≤0.5/256 (≈ 0.2%)
        최종 u8 오차: ≤1 LSB (out of 128)
        knee 왜곡: < 0.8% → Rev.4의 시프트 근사(최대 50% 오차)에 비해 대폭 개선

      출력 범위: [128..256] → 8-bit로 클램프하므로 [128..255]
      (addr=0 → 256, 클램프 후 255; addr=255 → round(256/1.996) = 128)
    """
    entries = []
    for i in range(256):
        m = 1.0 + i / 256.0
        v = round(256.0 / m)
        v = max(0, min(255, v))
        entries.append(v)

    path = os.path.join(OUTPUT_DIR, "recip_lut.hex")
    with open(path, "w") as f:
        for v in entries:
            f.write(f"{v:02X}\n")
    print(f"[OK] {path}  ({len(entries)} entries, range=[{min(entries)},{max(entries)}])")

    # 검증: u 계산 오차 시뮬레이션
    max_err_pct = 0.0
    for knee_msb in range(1, 13):
        for over_raw in range(0, min(1 << knee_msb, 256)):
            knee_half = 1 << knee_msb   # 정규화된 예시
            # 실제 u 참조값
            u_ref = (over_raw / knee_half) * 128.0

            # LUT 방식: addr=0 (정규화 mantissa=0 → m=1.0)
            recip_val = entries[0]      # worst case: addr=0 (가장 큰 오차)
            u_prod    = over_raw * recip_val
            shift_amt = knee_msb + 1
            u8        = (u_prod >> shift_amt) if shift_amt < 21 else 0

            err_pct = abs(u8 - u_ref) / 128.0 * 100 if u_ref > 0 else 0
            max_err_pct = max(max_err_pct, err_pct)

    print(f"  Worst-case u error (norm knee_half): {max_err_pct:.2f}%")
    return entries


# ─────────────────────────────────────────────────────────────────────────────
#  메인
# ─────────────────────────────────────────────────────────────────────────────

def main():
    print("=" * 60)
    print("  vca_compressor Rev.5  LUT 생성기")
    print("=" * 60)

    gen_log2_frac_lut()
    gen_exp2_frac_lut()
    gen_knee_quad_lut()
    gen_sqrt_lut()
    gen_recip_lut()

    print()
    print("모든 LUT 생성 완료.")
    print()
    print("Vivado 사용 시:")
    print("  1. 위 .hex 파일을 프로젝트 소스 디렉토리에 복사")
    print("  2. Add Sources → 각 .hex 파일을 'Data File'로 추가")
    print("  3. 또는 $readmemh 경로에 맞게 복사")
    print()
    print("신규 LUT 요약:")
    print("  sqrt_lut.hex  : 512 entries, 11-bit data")
    print("    → RMS sqrt 경로에서 iterative sqrt 대체 [FIX-2]")
    print("  recip_lut.hex : 256 entries,  8-bit data")
    print("    → knee u 계산 정밀도 개선 (오차 <1%) [FIX-3]")


if __name__ == "__main__":
    main()
