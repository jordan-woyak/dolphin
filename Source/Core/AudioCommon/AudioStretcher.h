// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>

#include <soundtouch/SoundTouch.h>

namespace AudioCommon
{
class AudioStretcher
{
public:
  explicit AudioStretcher(unsigned int sample_rate);
  void ProcessSamples(const short* in, unsigned int num_in, unsigned int num_out);
  void GetStretchedSamples(short* out, unsigned int num_out);
  void Clear();

private:
  static constexpr int CHANNEL_COUNT = 2;

  unsigned int m_sample_rate;
  std::array<short, CHANNEL_COUNT> m_last_stretched_sample = {};
  soundtouch::SoundTouch m_sound_touch;
  double m_stretch_ratio = 1.0;
#if SOUNDTOUCH_FLOAT_SAMPLES
  std::vector<soundtouch::SAMPLETYPE> m_float_buffer;
#endif
};

}  // namespace AudioCommon
