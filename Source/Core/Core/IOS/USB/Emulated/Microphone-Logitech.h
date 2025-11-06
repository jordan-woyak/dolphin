// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"
#include "Core/IOS/USB/Emulated/Microphone.h"

namespace IOS::HLE::USB
{
class LogitechMicState;

class MicrophoneLogitech final : public Microphone
{
public:
  MicrophoneLogitech(const LogitechMicState& sampler, u8 index);
  ~MicrophoneLogitech();

private:
#ifdef HAVE_CUBEB
  std::string GetWorkerName() const override;
  std::string GetInputDeviceId() const override;
  std::string GetCubebStreamName() const override;
  s16 GetVolumeModifier() const override;
  bool AreSamplesByteSwapped() const override;
#endif

  bool IsMicrophoneMuted() const override;
  u32 GetStreamSize() const override;

  const LogitechMicState& m_sampler;
  const u8 m_index;
};
}  // namespace IOS::HLE::USB
