// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/USB/Emulated/Microphone-WiiSpeak.h"

#include "Core/Config/MainSettings.h"
#include "Core/IOS/USB/Emulated/Microphone.h"
#include "Core/IOS/USB/Emulated/WiiSpeak.h"

namespace IOS::HLE::USB
{
MicrophoneWiiSpeak::MicrophoneWiiSpeak(const WiiSpeakState& sampler)
    : Microphone(sampler, GetWorkerName()), m_sampler(sampler)
{
}

MicrophoneWiiSpeak::~MicrophoneWiiSpeak()
{
}

#ifdef HAVE_CUBEB

std::string MicrophoneWiiSpeak::GetWorkerName() const
{
  return "Wii Speak Worker";
}

std::string MicrophoneWiiSpeak::GetInputDeviceId() const
{
  return Config::Get(Config::MAIN_WII_SPEAK_MICROPHONE);
}

std::string MicrophoneWiiSpeak::GetCubebStreamName() const
{
  return "Dolphin Emulated Wii Speak";
}

s16 MicrophoneWiiSpeak::GetVolumeModifier() const
{
  return Config::Get(Config::MAIN_WII_SPEAK_VOLUME_MODIFIER);
}

bool MicrophoneWiiSpeak::AreSamplesByteSwapped() const
{
  return true;
}

#endif

bool MicrophoneWiiSpeak::IsMicrophoneMuted() const
{
  return Config::Get(Config::MAIN_WII_SPEAK_MUTED);
}

u32 MicrophoneWiiSpeak::GetStreamSize() const
{
  return BUFF_SIZE_SAMPLES * 500;
}
}  // namespace IOS::HLE::USB
