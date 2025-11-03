// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/USB/Emulated/Microphone-Logitech.h"

#include "Core/Config/MainSettings.h"
#include "Core/IOS/USB/Emulated/LogitechMic.h"
#include "Core/IOS/USB/Emulated/Microphone.h"

namespace IOS::HLE::USB
{
MicrophoneLogitech::MicrophoneLogitech(const LogitechMicState& sampler)
    : Microphone(sampler, GetWorkerName()), m_sampler(sampler)
{
}

MicrophoneLogitech::~MicrophoneLogitech()
{
}

#ifdef HAVE_CUBEB

std::string MicrophoneLogitech::GetWorkerName() const
{
  return "Logitech USB Microphone Worker";
}

std::string MicrophoneLogitech::GetInputDeviceId() const
{
  return Config::Get(Config::MAIN_LOGITECH_MIC_1_MICROPHONE);
}

std::string MicrophoneLogitech::GetCubebStreamName() const
{
  return "Dolphin Emulated Logitech USB Microphone";
}

s16 MicrophoneLogitech::GetVolumeModifier() const
{
  return Config::Get(Config::MAIN_LOGITECH_MIC_1_VOLUME_MODIFIER);
}

bool MicrophoneLogitech::AreSamplesByteSwapped() const
{
  return false;
}

#endif

bool MicrophoneLogitech::IsMicrophoneMuted() const
{
  return Config::Get(Config::MAIN_LOGITECH_MIC_1_MUTED);
}

u32 MicrophoneLogitech::GetStreamSize() const
{
  return BUFF_SIZE_SAMPLES * m_sampler.srate[0] / 250;
}
}  // namespace IOS::HLE::USB
