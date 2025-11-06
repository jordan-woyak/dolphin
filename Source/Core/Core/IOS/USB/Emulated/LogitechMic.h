// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/IOS/USB/Common.h"
#include "Core/IOS/USB/Emulated/Microphone-Logitech.h"

namespace IOS::HLE::USB
{
enum LogitechMicrophoneRequestCodes
{
  REQUEST_SET_CUR = 0x01,
  REQUEST_GET_CUR = 0x81,
  REQUEST_SET_MIN = 0x02,
  REQUEST_GET_MIN = 0x82,
  REQUEST_SET_MAX = 0x03,
  REQUEST_GET_MAX = 0x83,
  REQUEST_SET_RES = 0x04,
  REQUEST_GET_RES = 0x84,
  REQUEST_SET_MEM = 0x05,
  REQUEST_GET_MEM = 0x85,
  REQUEST_GET_STAT = 0xff
};

enum LogitechMicrophoneFeatureUnitControlSelectors
{
  AUDIO_MUTE_CONTROL = 0x01,
  AUDIO_VOLUME_CONTROL = 0x02,
  AUDIO_BASS_CONTROL = 0x03,
  AUDIO_MID_CONTROL = 0x04,
  AUDIO_TREBLE_CONTROL = 0x05,
  AUDIO_GRAPHIC_EQUALIZER_CONTROL = 0x06,
  AUDIO_AUTOMATIC_GAIN_CONTROL = 0x07,
  AUDIO_DELAY_CONTROL = 0x08,
  AUDIO_BASS_BOOST_CONTROL = 0x09,
  AUDIO_LOUDNESS_CONTROL = 0x0a
};

enum LogitechMicrophoneEndpointControlSelectors
{
  AUDIO_SAMPLING_FREQ_CONTROL = 0x01,
  AUDIO_PITCH_CONTROL = 0x02
};

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

  const DeviceDescriptor m_device_descriptor{0x12,   0x01,   0x0200, 0x00, 0x00, 0x00, 0x08,
                                             0x046d, 0x0a03, 0x0001, 0x01, 0x02, 0x00, 0x01};
  const std::vector<ConfigDescriptor> m_config_descriptor{
      ConfigDescriptor{0x09, 0x02, 0x0079, 0x02, 0x01, 0x03, 0x80, 0x3c},
  };
  const std::vector<std::vector<InterfaceDescriptor>> m_interface_descriptor{
      {
          InterfaceDescriptor{0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00},
      },
      {InterfaceDescriptor{0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00},
       InterfaceDescriptor{0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00}}};
  static constexpr u8 ENDPOINT_AUDIO_IN = 0x84;
  const std::vector<std::vector<EndpointDescriptor>> m_endpoint_descriptor{
      {
          EndpointDescriptor{0x09, 0x05, ENDPOINT_AUDIO_IN, 0x0d, 0x0060, 0x01},
      },
      {EndpointDescriptor{0x09, 0x05, 0x81, 0x05, 0x00c8, 0x01}}};
};
}  // namespace IOS::HLE::USB
