// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/IOS/USB/Common.h"
#include "Core/IOS/USB/Emulated/Microphone.h"

namespace IOS::HLE::USB
{

class LogitechMicState final : public MicrophoneState
{
public:
  // Use atomic for members concurrently used by the data callback
  std::atomic<bool> mute;
  std::atomic<u8> vol;
  std::atomic<u32> srate = DEFAULT_SAMPLING_RATE;

  static constexpr u32 DEFAULT_SAMPLING_RATE = 48000;

  bool IsSampleOn() const;
  bool IsMuted() const;
  u32 GetDefaultSamplingRate() const;
};

class MicrophoneLogitech final : public Microphone
{
public:
  explicit MicrophoneLogitech(const LogitechMicState& sampler, u8 index);

private:
#ifdef HAVE_CUBEB
  std::string GetWorkerName() const;
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

class LogitechMic final : public Device
{
public:
  LogitechMic(u8 index);
  ~LogitechMic() override;

  DeviceDescriptor GetDeviceDescriptor() const override;
  std::vector<ConfigDescriptor> GetConfigurations() const override;
  std::vector<InterfaceDescriptor> GetInterfaces(u8 config) const override;
  std::vector<EndpointDescriptor> GetEndpoints(u8 config, u8 interface, u8 alt) const override;
  bool Attach() override;
  bool AttachAndChangeInterface(u8 interface) override;
  int CancelTransfer(u8 endpoint) override;
  int ChangeInterface(u8 interface) override;
  int GetNumberOfAltSettings(u8 interface) override;
  int SetAltSetting(u8 alt_setting) override;
  int SubmitTransfer(std::unique_ptr<CtrlMessage> message) override;
  int SubmitTransfer(std::unique_ptr<BulkMessage> message) override;
  int SubmitTransfer(std::unique_ptr<IntrMessage> message) override;
  int SubmitTransfer(std::unique_ptr<IsoMessage> message) override;

private:
  LogitechMicState m_sampler{};

  int GetAudioControl(std::unique_ptr<CtrlMessage>& cmd);
  int SetAudioControl(std::unique_ptr<CtrlMessage>& cmd);
  int EndpointAudioControl(std::unique_ptr<CtrlMessage>& cmd);

  const u16 m_vid = 0x046d;
  const u16 m_pid = 0x0a03;
  u8 m_index = 0;
  u8 m_active_interface = 0;
  bool m_device_attached = false;
  std::unique_ptr<MicrophoneLogitech> m_microphone{};
};
}  // namespace IOS::HLE::USB
