// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/USB/Emulated/Microphone-Logitech.h"

#include "Core/Config/MainSettings.h"
#include "Core/IOS/USB/Emulated/LogitechMic.h"
#include "Core/IOS/USB/Emulated/Microphone.h"

namespace IOS::HLE::USB
{
MicrophoneLogitech::MicrophoneLogitech(const LogitechMicState& sampler, u8 index)
    : Microphone(sampler, GetWorkerName()), m_sampler(sampler), m_index(index)
{
}

MicrophoneLogitech::~MicrophoneLogitech()
{
}

#ifdef HAVE_CUBEB

std::string MicrophoneLogitech::GetWorkerName() const
{
  return "Logitech USB Microphone Worker " + std::to_string(m_index);
}

std::string MicrophoneLogitech::GetInputDeviceId() const
{
  if (m_index == 0)
    return Config::Get(Config::MAIN_LOGITECH_MIC_1_MICROPHONE);
  else if (m_index == 1)
    return Config::Get(Config::MAIN_LOGITECH_MIC_2_MICROPHONE);
  else if (m_index == 2)
    return Config::Get(Config::MAIN_LOGITECH_MIC_3_MICROPHONE);
  else if (m_index == 3)
    return Config::Get(Config::MAIN_LOGITECH_MIC_4_MICROPHONE);

  return Config::Get(Config::MAIN_LOGITECH_MIC_1_MICROPHONE);
}

std::string MicrophoneLogitech::GetCubebStreamName() const
{
  return "Dolphin Emulated Logitech USB Microphone " + std::to_string(m_index);
}

s16 MicrophoneLogitech::GetVolumeModifier() const
{
  if (m_index == 0)
    return Config::Get(Config::MAIN_LOGITECH_MIC_1_VOLUME_MODIFIER);
  else if (m_index == 1)
    return Config::Get(Config::MAIN_LOGITECH_MIC_2_VOLUME_MODIFIER);
  else if (m_index == 2)
    return Config::Get(Config::MAIN_LOGITECH_MIC_3_VOLUME_MODIFIER);
  else if (m_index == 3)
    return Config::Get(Config::MAIN_LOGITECH_MIC_4_VOLUME_MODIFIER);

  return Config::Get(Config::MAIN_LOGITECH_MIC_1_VOLUME_MODIFIER);
}

bool MicrophoneLogitech::AreSamplesByteSwapped() const
{
  return false;
}

#endif

bool MicrophoneLogitech::IsMicrophoneMuted() const
{
  if (m_index == 0)
    return Config::Get(Config::MAIN_LOGITECH_MIC_1_MUTED);
  else if (m_index == 1)
    return Config::Get(Config::MAIN_LOGITECH_MIC_2_MUTED);
  else if (m_index == 2)
    return Config::Get(Config::MAIN_LOGITECH_MIC_3_MUTED);
  else if (m_index == 3)
    return Config::Get(Config::MAIN_LOGITECH_MIC_4_MUTED);

  return Config::Get(Config::MAIN_LOGITECH_MIC_1_MUTED);
}

u32 MicrophoneLogitech::GetStreamSize() const
{
  return BUFF_SIZE_SAMPLES * m_sampler.srate / 250;
}
}  // namespace IOS::HLE::USB
