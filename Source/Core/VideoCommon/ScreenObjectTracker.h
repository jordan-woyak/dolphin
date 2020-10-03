// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <complex>
#include <vector>

#include "Common/MathUtil.h"
#include "Common/Matrix.h"

// TODO: move to MathUtil
template <typename T>
class GoertzelFilter
{
public:
  static constexpr T TAU = T(MathUtil::TAU);

  void SetNormalizedFrequency(T norm_frequency)
  {
    m_norm_frequency = norm_frequency;
    const auto cosine = std::cos(TAU * m_norm_frequency);
    m_coeff = 2 * cosine;
  }

  void Reset()
  {
    m_prev = {};
    m_prev2 = {};
    m_count = {};
  }

  void AddSample(T sample)
  {
    ++m_count;
    const auto s = sample - m_prev2 + m_prev * m_coeff;
    m_prev2 = m_prev;
    m_prev = s;
  }

  auto GetSampleCount() const { return m_count; }

  T GetReal() const
  {
    return (m_prev - m_prev2 * std::cos(TAU * m_norm_frequency)) * 2 / GetSampleCount();
  }
  T GetImaginary() const
  {
    return m_prev2 * std::sin(TAU * m_norm_frequency) * 2 / GetSampleCount();
  }

  auto GetResult() const { return std::complex(GetReal(), GetImaginary()); }

  T GetMagnitude() const { return std::abs(GetResult()); }
  T GetPhase() const { return std::arg(GetResult()); }

private:
  T m_norm_frequency;
  T m_coeff;
  T m_prev = {};
  T m_prev2 = {};
  std::size_t m_count = {};
};

class ScreenObjectTracker
{
public:
  using Hash = u64;

  void OnFrameAdvance();

  void AddObject(Common::Vec3 position);

  void SetCurrentHash(Hash);

private:
  struct Object
  {
    Object();

    void UpdatePosition(Common::Vec3);

    GoertzelFilter<float> filter1;
    GoertzelFilter<float> filter2;

    Common::Vec3 base_position;
    Common::Vec3 position;
    float stored_power = {};
    Hash hash;
  };

  std::vector<Object> m_objects;
  std::vector<Object> m_new_objects;
  Hash m_current_hash;
};
