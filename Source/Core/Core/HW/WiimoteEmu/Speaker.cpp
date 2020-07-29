// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/HW/WiimoteEmu/Speaker.h"

#include <memory>

#include "AudioCommon/AudioCommon.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"
#include "Core/ConfigManager.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"

//#define WIIMOTE_SPEAKER_DUMP
#ifdef WIIMOTE_SPEAKER_DUMP
#include <cstdlib>
#include <fstream>
#include "AudioCommon/WaveFile.h"
#include "Common/FileUtil.h"
#endif

namespace WiimoteEmu
{
// Yamaha ADPCM decoder code based on The ffmpeg Project (Copyright (s) 2001-2003)

static const s32 yamaha_difflookup[] = {1,  3,  5,  7,  9,  11,  13,  15,
                                        -1, -3, -5, -7, -9, -11, -13, -15};

constexpr s32 yamaha_indexscale[] = {230, 230, 230, 230, 307, 409, 512, 614};

constexpr double index_scale[8] = {0.8984375,  0.8984375,  0.8984375, 0.8984375,
                                   1.19921875, 1.59765625, 2.0,       2.3984375};

static s16 av_clip16(s32 a)
{
  if ((a + 32768) & ~65535)
    return (a >> 31) ^ 32767;
  else
    return a;
}

static s32 av_clip(s32 a, s32 amin, s32 amax)
{
  if (a < amin)
    return amin;
  else if (a > amax)
    return amax;
  else
    return a;
}

static s16 adpcm_yamaha_expand_nibble(ADPCMState& s, u8 nibble)
{
  s.predictor += (s.step * yamaha_difflookup[nibble]) / 8;
  s.predictor = av_clip16(s.predictor);
  s.step = (s.step * yamaha_indexscale[nibble & 0xf]) >> 8;
  s.step = av_clip(s.step, 127, 24576);
  return s.predictor;
}

u32 SpeakerLogic::Encode(ADPCMState* encoder_state, const s16* input_samples, u32 sample_count,
                         u8* output)

{
  if (sample_count)
    std::memset(output, 0, (sample_count + 1) / 2);

  s32 predictor = encoder_state->predictor;
  s32 step = encoder_state->step;

  for (u32 i = 0; i != sample_count; ++i)
  {
    const s32 current_sample = input_samples[i];
    s32 abs_delta = std::abs(current_sample - predictor);

    const bool is_step_lte_val = step <= abs_delta;
    if (is_step_lte_val)
      abs_delta -= step;

    const s32 half_step = step / 2;
    const bool is_half_step_lte_val = half_step <= abs_delta;
    if (is_half_step_lte_val)
      abs_delta -= half_step;

    const s32 qtr_step = half_step / 2;
    const bool is_qtr_step_lte_val = qtr_step <= abs_delta;

    const bool is_less_than_predictor = current_sample < predictor;
    s32 predictor_delta = (is_less_than_predictor ? -1 : 1) *
                          (step * is_step_lte_val + half_step * is_half_step_lte_val +
                           qtr_step * is_qtr_step_lte_val + qtr_step / 2);
    // TODO: needed?
    predictor_delta = std::clamp<s32>(predictor_delta, -0x10000, 0xffff);

    predictor = std::clamp<s32>(predictor + predictor_delta, -0x8000, 0x7fff);

    const u8 nibble = is_less_than_predictor * 8 + is_step_lte_val * 4 + is_half_step_lte_val * 2 +
                      is_qtr_step_lte_val;

    const u32 nibble_shift = (i % 2) ? 0 : 4;
    output[i / 2] |= nibble << nibble_shift;

    step = s32(step * index_scale[nibble & 0x7]);
    step = std::clamp<s32>(step, 0x7f, 0x6000);
  }

  encoder_state->predictor = predictor;
  encoder_state->step = step;

  return sample_count;
}

#ifdef WIIMOTE_SPEAKER_DUMP
std::ofstream ofile;
WaveFileWriter wav;

void stopdamnwav()
{
  wav.Stop();
  ofile.close();
}
#endif

void SpeakerLogic::SpeakerData(const u8* data, int length, float speaker_pan)
{
  // TODO: should we still process samples for the decoder state?
  if (!SConfig::GetInstance().m_WiimoteEnableSpeaker)
    return;

  if (reg_data.sample_rate == 0 || length == 0)
    return;

  // Even if volume is zero we process samples to maintain proper decoder state.

  // TODO consider using static max size instead of new
  std::unique_ptr<s16[]> samples(new s16[length * 2]);

  unsigned int sample_rate_dividend, sample_length;
  u8 volume_divisor;

  if (reg_data.format == SpeakerLogic::DATA_FORMAT_PCM)
  {
    // 8 bit PCM
    for (int i = 0; i < length; ++i)
    {
      samples[i] = ((s16)(s8)data[i]) * 0x100;
    }

    // Following details from http://wiibrew.org/wiki/Wiimote#Speaker
    sample_rate_dividend = 12000000;
    volume_divisor = 0xff;
    sample_length = (unsigned int)length;
  }
  else if (reg_data.format == SpeakerLogic::DATA_FORMAT_ADPCM)
  {
    // 4 bit Yamaha ADPCM (same as dreamcast)
    for (int i = 0; i < length; ++i)
    {
      samples[i * 2] = adpcm_yamaha_expand_nibble(adpcm_state, (data[i] >> 4) & 0xf);
      samples[i * 2 + 1] = adpcm_yamaha_expand_nibble(adpcm_state, data[i] & 0xf);
    }

    // Following details from http://wiibrew.org/wiki/Wiimote#Speaker
    sample_rate_dividend = 6000000;
    volume_divisor = 0x7F;
    sample_length = (unsigned int)length * 2;
  }
  else
  {
    ERROR_LOG(IOS_WIIMOTE, "Unknown speaker format %x", reg_data.format);
    return;
  }

  if (reg_data.volume > volume_divisor)
  {
    DEBUG_LOG(IOS_WIIMOTE, "Wiimote volume is higher than suspected maximum!");
    volume_divisor = reg_data.volume;
  }

  // SetWiimoteSpeakerVolume expects values from 0 to 255.
  // Multiply by 256, cast to int, and clamp to 255 for a uniform conversion.
  const double volume = float(reg_data.volume) / volume_divisor * 256;

  // Speaker pan using "Constant Power Pan Law"
  const double pan_prime = MathUtil::PI * (speaker_pan + 1) / 4;

  const auto left_volume = std::min(int(std::cos(pan_prime) * volume), 255);
  const auto right_volume = std::min(int(std::sin(pan_prime) * volume), 255);

  g_sound_stream->GetMixer()->SetWiimoteSpeakerVolume(left_volume, right_volume);

  // ADPCM sample rate is thought to be x2.(3000 x2 = 6000).
  const unsigned int sample_rate = sample_rate_dividend / reg_data.sample_rate;
  g_sound_stream->GetMixer()->PushWiimoteSpeakerSamples(samples.get(), sample_length,
                                                        sample_rate * 2);

#ifdef WIIMOTE_SPEAKER_DUMP
  static int num = 0;

  if (num == 0)
  {
    File::Delete("rmtdump.wav");
    File::Delete("rmtdump.bin");
    atexit(stopdamnwav);
    File::OpenFStream(ofile, "rmtdump.bin", ofile.binary | ofile.out);
    wav.Start("rmtdump.wav", 6000);
  }
  wav.AddMonoSamples(samples.get(), length * 2);
  if (ofile.good())
  {
    for (int i = 0; i < length; i++)
    {
      ofile << data[i];
    }
  }
  num++;
#endif
}

void SpeakerLogic::Reset()
{
  reg_data = {};

  // Yamaha ADPCM state initialize
  adpcm_state.predictor = 0;
  adpcm_state.step = 127;
}

void SpeakerLogic::DoState(PointerWrap& p)
{
  p.Do(adpcm_state);
  p.Do(reg_data);
}

int SpeakerLogic::BusRead(u8 slave_addr, u8 addr, int count, u8* data_out)
{
  if (I2C_ADDR != slave_addr)
    return 0;

  return RawRead(&reg_data, addr, count, data_out);
}

int SpeakerLogic::BusWrite(u8 slave_addr, u8 addr, int count, const u8* data_in)
{
  if (I2C_ADDR != slave_addr)
    return 0;

  if (0x00 == addr)
  {
    SpeakerData(data_in, count, m_speaker_pan_setting.GetValue() / 100);
    return count;
  }
  else
  {
    // TODO: Does writing immediately change the decoder config even when active
    // or does a write to 0x08 activate the new configuration or something?
    return RawWrite(&reg_data, addr, count, data_in);
  }
}

}  // namespace WiimoteEmu
