# Credits

이 프로젝트의 YM2203(OPN) FM 음원과 YM2149 PSG 부분은 Jose Tejada Gomez(jotego)의 오픈소스 코어를 기반으로 합니다.

- **JT12** — YM2612 / YM2203 / YM2610 호환 FM 음원 코어: https://github.com/jotego/jt12
- **JT49** — AY-3-8910 / YM2149 호환 PSG 코어: https://github.com/jotego/jt49

두 코어는 모두 GNU GPL v3 라이선스이며, 원본의 `jt12_*`, `jt49_*` 모듈 이름을 `ym_*`, `ym49_*` 등으로 바꾸고 Zynq-7020 환경에 맞게 수정해서 사용했습니다. 각 파일 상단에는 원본 파일명과 저작권·라이선스 고지를 표기해 두었습니다.

## JT12 / JT49 기반 파일 (72개)

| 파일 | 원본 프로젝트 | 원본 파일 |
|---|---|---|
| `dac2.v` | JT12 | `hdl/dac/jt12_dac2.v` |
| `dcrm.v` | JT49 | `hdl/filter/jt49_dcrm2.v` |
| `dcrm2.v` | JT49 | `hdl/filter/jt49_dcrm2.v` |
| `eg_cnt.v` | JT12 | `hdl/alt/eg_cnt.v` |
| `eg_comb.v` | JT12 | `hdl/alt/eg_comb.v` |
| `eg_mux.v` | JT12 | `hdl/alt/eg_mux.v` |
| `eg_step.v` | JT12 | `hdl/alt/eg_step.v` |
| `eg_step_ram.v` | JT12 | `hdl/alt/eg_step_ram.v` |
| `psg_fm_comb.v` | JT12 | `hdl/mixer/jt12_comb.v` |
| `psg_fm_decim.v` | JT12 | `hdl/mixer/jt12_decim.v` |
| `psg_fm_genmix.v` | JT12 | `hdl/mixer/jt12_genmix.v` |
| `psg_fm_interpol.v` | JT12 | `hdl/mixer/jt12_interpol.v` |
| `psg_fm_mix.v` | JT12 | `hdl/mixer/jt12_mixer.v` |
| `psg_fm_uprate.v` | JT12 | `hdl/mixer/jt12_fm_uprate.v` |
| `sound_amp.v` | JT12 | `hdl/deprecated/jt12_amp.v` |
| `ym03_acc.v` | JT12 | `hdl/jt03_acc.v` |
| `ym12_acc.v` | JT12 | `hdl/jt12_acc.v` |
| `ym2149.v` | JT49 | `hdl/jt49.v` |
| `ym2203.v` | JT12 | `hdl/jt03.v` |
| `ym49_cen.v` | JT49 | `hdl/jt49_cen.v` |
| `ym49_div.v` | JT49 | `hdl/jt49_div.v` |
| `ym49_dly.v` | JT49 | `hdl/filter/jt49_dly.v` |
| `ym49_eg.v` | JT49 | `hdl/jt49_eg.v` |
| `ym49_exp.v` | JT49 | `hdl/jt49_exp.v` |
| `ym49_mave.v` | JT49 | `hdl/filter/jt49_mave.v` |
| `ym49_noise.v` | JT49 | `hdl/jt49_noise.v` |
| `ym_acc.v` | JT12 | `hdl/jt10_acc.v` |
| `ym_adpcm.v` | JT12 | `hdl/adpcm/jt10_adpcm.v` |
| `ym_adpcm_acc.v` | JT12 | `hdl/adpcm/jt10_adpcm_acc.v` |
| `ym_adpcm_cnt.v` | JT12 | `hdl/adpcm/jt10_adpcm_cnt.v` |
| `ym_adpcm_div.v` | JT12 | `hdl/adpcm/jt10_adpcm_div.v` |
| `ym_adpcm_drvA.v` | JT12 | `hdl/adpcm/jt10_adpcm_drvA.v` |
| `ym_adpcm_drvB.v` | JT12 | `hdl/adpcm/jt10_adpcm_drvB.v` |
| `ym_adpcm_gain.v` | JT12 | `hdl/adpcm/jt10_adpcm_gain.v` |
| `ym_adpcma_lut.v` | JT12 | `hdl/adpcm/jt10_adpcma_lut.v` |
| `ym_adpcmb.v` | JT12 | `hdl/adpcm/jt10_adpcmb.v` |
| `ym_adpcmb_cnt.v` | JT12 | `hdl/adpcm/jt10_adpcmb_cnt.v` |
| `ym_adpcmb_gain.v` | JT12 | `hdl/adpcm/jt10_adpcmb_gain.v` |
| `ym_adpcmb_interpol.v` | JT12 | `hdl/adpcm/jt10_adpcmb_interpol.v` |
| `ym_csr.v` | JT12 | `hdl/jt12_csr.v` |
| `ym_div.v` | JT12 | `hdl/jt12_div.v` |
| `ym_dout.v` | JT12 | `hdl/jt12_dout.v` |
| `ym_eg.v` | JT12 | `hdl/jt12_eg.v` |
| `ym_eg_cnt.v` | JT12 | `hdl/jt12_eg_cnt.v` |
| `ym_eg_comb.v` | JT12 | `hdl/jt12_eg_comb.v` |
| `ym_eg_ctrl.v` | JT12 | `hdl/jt12_eg_ctrl.v` |
| `ym_eg_final.v` | JT12 | `hdl/jt12_eg_final.v` |
| `ym_eg_pure.v` | JT12 | `hdl/jt12_eg_pure.v` |
| `ym_eg_step.v` | JT12 | `hdl/jt12_eg_step.v` |
| `ym_exprom.v` | JT12 | `hdl/jt12_exprom.v` |
| `ym_kon.v` | JT12 | `hdl/jt12_kon.v` |
| `ym_lfo.v` | JT12 | `hdl/jt12_lfo.v` |
| `ym_logsin.v` | JT12 | `hdl/jt12_logsin.v` |
| `ym_mmr.v` | JT12 | `hdl/jt12_mmr.v` |
| `ym_mod.v` | JT12 | `hdl/jt12_mod.v` |
| `ym_op.v` | JT12 | `hdl/jt12_op.v` |
| `ym_pcm_interpol.v` | JT12 | `hdl/jt12_pcm_interpol.v` |
| `ym_pg.v` | JT12 | `hdl/jt12_pg.v` |
| `ym_pg_comb.v` | JT12 | `hdl/jt12_pg_comb.v` |
| `ym_pg_dt.v` | JT12 | `hdl/jt12_pg_dt.v` |
| `ym_pg_inc.v` | JT12 | `hdl/jt12_pg_inc.v` |
| `ym_pg_sum.v` | JT12 | `hdl/jt12_pg_sum.v` |
| `ym_pm.v` | JT12 | `hdl/jt12_pm.v` |
| `ym_reg.v` | JT12 | `hdl/jt12_reg.v` |
| `ym_reg_ch.v` | JT12 | `hdl/jt12_reg_ch.v` |
| `ym_rst.v` | JT12 | `hdl/jt12_rst.v` |
| `ym_sh.v` | JT12 | `hdl/jt12_sh.v` |
| `ym_sh_rst.v` | JT12 | `hdl/jt12_sh_rst.v` |
| `ym_single_acc.v` | JT12 | `hdl/jt12_single_acc.v` |
| `ym_sumch.v` | JT12 | `hdl/jt12_sumch.v` |
| `ym_timers.v` | JT12 | `hdl/jt12_timers.v` |
| `ym_top.v` | JT12 | `hdl/jt12_top.v` |

위 목록에 없는 `src/hdl/`의 나머지 모듈(이펙터, 리버브, FFT, AXI 브리지 등)과 `src/linux/`, `src/gen_luts.py`는 팀에서 직접 작성했습니다.
