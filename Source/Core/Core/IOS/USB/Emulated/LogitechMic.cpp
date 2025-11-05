// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/USB/Emulated/LogitechMic.h"

#include <algorithm>

#include "Core/HW/Memmap.h"
#include "Core/System.h"

namespace IOS::HLE::USB
{
bool LogitechMicState::IsSampleOn() const
{
  return true;
}

bool LogitechMicState::IsMuted() const
{
  return mute;
}

u32 LogitechMicState::GetDefaultSamplingRate() const
{
  return DEFAULT_SAMPLING_RATE;
}

LogitechMic::LogitechMic()
{
  m_id = u64(m_vid) << 32 | u64(m_pid) << 16 | u64(9) << 8 | u64(1);
}

LogitechMic::~LogitechMic() = default;

DeviceDescriptor LogitechMic::GetDeviceDescriptor() const
{
  return m_device_descriptor;
}

std::vector<ConfigDescriptor> LogitechMic::GetConfigurations() const
{
  return m_config_descriptor;
}

std::vector<InterfaceDescriptor> LogitechMic::GetInterfaces(u8 config) const
{
  return m_interface_descriptor[m_active_interface];
}

std::vector<EndpointDescriptor> LogitechMic::GetEndpoints(u8 config, u8 interface, u8 alt) const
{
  return m_endpoint_descriptor[m_active_interface];
}

bool LogitechMic::Attach()
{
  if (m_device_attached)
    return true;

  DEBUG_LOG_FMT(IOS_USB, "[{:04x}:{:04x}] Opening device", m_vid, m_pid);
  if (!m_microphone)
  {
    m_microphone = std::make_unique<MicrophoneLogitech>(m_sampler);
    m_microphone->Initialize();
  }
  m_device_attached = true;
  return true;
}

bool LogitechMic::AttachAndChangeInterface(const u8 interface)
{
  if (!Attach())
    return false;

  if (interface != m_active_interface)
    return ChangeInterface(interface) == 0;

  return true;
}

int LogitechMic::CancelTransfer(const u8 endpoint)
{
  DEBUG_LOG_FMT(IOS_USB, "[{:04x}:{:04x} {}] Cancelling transfers (endpoint {:#x})", m_vid, m_pid,
                m_active_interface, endpoint);

  return IPC_SUCCESS;
}

int LogitechMic::ChangeInterface(const u8 interface)
{
  DEBUG_LOG_FMT(IOS_USB, "[{:04x}:{:04x} {}] Changing interface to {}", m_vid, m_pid,
                m_active_interface, interface);
  m_active_interface = interface;
  return 0;
}

int LogitechMic::GetNumberOfAltSettings(u8 interface)
{
  return 0;
}

int LogitechMic::SetAltSetting(u8 alt_setting)
{
  return 0;
}

static constexpr u32 USBGETAID(u8 cs, u8 request, u16 index)
{
  return static_cast<u32>((cs << 24) | (request << 16) | index);
}

int LogitechMic::GetAudioControl(std::unique_ptr<CtrlMessage>& cmd)
{
  auto& system = cmd->GetEmulationKernel().GetSystem();
  auto& memory = system.GetMemory();
  const u8 cs = static_cast<u8>(cmd->value >> 8);
  const u8 cn = static_cast<u8>(cmd->value - 1);
  int ret = IPC_STALL;
  DEBUG_LOG_FMT(IOS_USB,
                "GetAudioControl: bCs={:02x} bCn={:02x} bRequestType={:02x} bRequest={:02x} "
                "bIndex={:02x} aid={:08x}",
                cs, cn, cmd->request_type, cmd->request, cmd->index,
                USBGETAID(cs, cmd->request, cmd->index));
  switch (USBGETAID(cs, cmd->request, cmd->index))
  {
  case USBGETAID(AUDIO_MUTE_CONTROL, REQUEST_GET_CUR, 0x0200):
  {
    memory.Write_U8(m_sampler.mute ? 1 : 0, cmd->data_address);
    ret = 1;
    break;
  }
  case USBGETAID(AUDIO_VOLUME_CONTROL, REQUEST_GET_CUR, 0x0200):
  {
    if (cn < 1 || cn == 0xff)
    {
      const uint16_t vol = (m_sampler.vol * 0x8800 + 127) / 255 + 0x8000;
      DEBUG_LOG_FMT(IOS_USB, "GetAudioControl: Get volume {:04x}", vol);
      memory.Write_U16(vol, cmd->data_address);
      ret = 2;
    }
    break;
  }
  case USBGETAID(AUDIO_VOLUME_CONTROL, REQUEST_GET_MIN, 0x0200):
  {
    if (cn < 1 || cn == 0xff)
    {
      memory.Write_U16(0x8001, cmd->data_address);
      ret = 2;
    }
    break;
  }
  case USBGETAID(AUDIO_VOLUME_CONTROL, REQUEST_GET_MAX, 0x0200):
  {
    if (cn < 1 || cn == 0xff)
    {
      memory.Write_U16(0x0800, cmd->data_address);
      ret = 2;
    }
    break;
  }
  case USBGETAID(AUDIO_VOLUME_CONTROL, REQUEST_GET_RES, 0x0200):
  {
    if (cn < 1 || cn == 0xff)
    {
      memory.Write_U16(0x0088, cmd->data_address);
      ret = 2;
    }
    break;
  }
  }
  return ret;
}

int LogitechMic::SetAudioControl(std::unique_ptr<CtrlMessage>& cmd)
{
  auto& system = cmd->GetEmulationKernel().GetSystem();
  auto& memory = system.GetMemory();
  const u8 cs = static_cast<u8>(cmd->value >> 8);
  const u8 cn = static_cast<u8>(cmd->value - 1);
  int ret = IPC_STALL;
  DEBUG_LOG_FMT(IOS_USB,
                "SetAudioControl: bCs={:02x} bCn={:02x} bRequestType={:02x} bRequest={:02x} "
                "bIndex={:02x} aid={:08x}",
                cs, cn, cmd->request_type, cmd->request, cmd->index,
                USBGETAID(cs, cmd->request, cmd->index));
  switch (USBGETAID(cs, cmd->request, cmd->index))
  {
  case USBGETAID(AUDIO_MUTE_CONTROL, REQUEST_SET_CUR, 0x0200):
  {
    m_sampler.mute = memory.Read_U8(cmd->data_address) & 0x01;
    DEBUG_LOG_FMT(IOS_USB, "SetAudioControl: Setting mute to {}", m_sampler.mute.load());
    ret = 0;
    break;
  }
  case USBGETAID(AUDIO_VOLUME_CONTROL, REQUEST_SET_CUR, 0x0200):
  {
    if (cn < 1 || cn == 0xff)
    {
      // TODO: Lego Rock Band's mic sensitivity setting provides unknown values to this.
      uint16_t vol = memory.Read_U16(cmd->data_address);
      const uint16_t original_vol = vol;

      vol -= 0x8000;
      vol = (vol * 255 + 0x4400) / 0x8800;
      if (vol > 255)
        vol = 255;

      if (cn == 0xff)
      {
        if (m_sampler.vol != vol)
          m_sampler.vol = static_cast<u8>(vol);
      }
      else
      {
        if (m_sampler.vol != vol)
          m_sampler.vol = static_cast<u8>(vol);
      }

      DEBUG_LOG_FMT(IOS_USB, "SetAudioControl: Setting volume to [{:02x}] [original={:04x}]",
                    m_sampler.vol.load(), original_vol);

      ret = 0;
    }
    break;
  }
  case USBGETAID(AUDIO_AUTOMATIC_GAIN_CONTROL, REQUEST_SET_CUR, 0x0200):
  {
    ret = 0;
    break;
  }
  }
  return ret;
}

int LogitechMic::EndpointAudioControl(std::unique_ptr<CtrlMessage>& cmd)
{
  auto& system = cmd->GetEmulationKernel().GetSystem();
  auto& memory = system.GetMemory();
  const u8 cs = static_cast<u8>(cmd->value >> 8);
  const u8 cn = static_cast<u8>(cmd->value - 1);
  int ret = IPC_STALL;
  DEBUG_LOG_FMT(IOS_USB,
                "EndpointAudioControl: bCs={:02x} bCn={:02x} bRequestType={:02x} bRequest={:02x} "
                "bIndex={:02x} aid:{:08x}",
                cs, cn, cmd->request_type, cmd->request, cmd->index,
                USBGETAID(cs, cmd->request, cmd->index));
  switch (USBGETAID(cs, cmd->request, cmd->index))
  {
  case USBGETAID(AUDIO_SAMPLING_FREQ_CONTROL, REQUEST_SET_CUR, ENDPOINT_AUDIO_IN):
  {
    if (cn == 0xff)
    {
      const uint32_t sr = memory.Read_U8(cmd->data_address) |
                          (memory.Read_U8(cmd->data_address + 1) << 8) |
                          (memory.Read_U8(cmd->data_address + 2) << 16);
      if (m_sampler.srate != sr)
      {
        m_sampler.srate = sr;
        if (m_microphone != nullptr)
        {
          DEBUG_LOG_FMT(IOS_USB, "EndpointAudioControl: Setting sampling rate to {:d}", sr);
          m_microphone->SetSamplingRate(sr);
        }
      }
    }
    else if (cn < 1)
    {
      WARN_LOG_FMT(IOS_USB, "EndpointAudioControl: Unsupported SAMPLER_FREQ_CONTROL set [cn={:d}]",
                   cn);
    }
    ret = 0;
    break;
  }
  case USBGETAID(AUDIO_SAMPLING_FREQ_CONTROL, REQUEST_GET_CUR, ENDPOINT_AUDIO_IN):
  {
    memory.Write_U8(m_sampler.srate & 0xff, cmd->data_address + 2);
    memory.Write_U8((m_sampler.srate >> 8) & 0xff, cmd->data_address + 1);
    memory.Write_U8((m_sampler.srate >> 16) & 0xff, cmd->data_address);
    ret = 3;
    break;
  }
  }
  return ret;
}

static constexpr std::array<u8, 121> FULL_DESCRIPTOR = {
    /* Configuration 1 */
    0x09,       /* bLength */
    0x02,       /* bDescriptorType */
    0x79, 0x00, /* wTotalLength */
    0x02,       /* bNumInterfaces */
    0x01,       /* bConfigurationValue */
    0x03,       /* iConfiguration */
    0x80,       /* bmAttributes */
    0x3c,       /* bMaxPower */

    /* Interface 0, Alternate Setting 0, Audio Control */
    0x09, /* bLength */
    0x04, /* bDescriptorType */
    0x00, /* bInterfaceNumber */
    0x00, /* bAlternateSetting */
    0x00, /* bNumEndpoints */
    0x01, /* bInterfaceClass */
    0x01, /* bInterfaceSubClass */
    0x00, /* bInterfaceProtocol */
    0x00, /* iInterface */

    /* Audio Control Interface */
    0x09,       /* bLength */
    0x24,       /* bDescriptorType */
    0x01,       /* bDescriptorSubtype */
    0x00, 0x01, /* bcdADC */
    0x27, 0x00, /* wTotalLength */
    0x01,       /* bInCollection */
    0x01,       /* baInterfaceNr */

    /* Audio Input Terminal */
    0x0c,       /* bLength */
    0x24,       /* bDescriptorType */
    0x02,       /* bDescriptorSubtype */
    0x0d,       /* bTerminalID */
    0x01, 0x02, /* wTerminalType */
    0x00,       /* bAssocTerminal */
    0x01,       /* bNrChannels */
    0x00, 0x00, /* wChannelConfig */
    0x00,       /* iChannelNames */
    0x00,       /* iTerminal */

    /* Audio Feature Unit */
    0x09, /* bLength */
    0x24, /* bDescriptorType */
    0x06, /* bDescriptorSubtype */
    0x02, /* bUnitID */
    0x0d, /* bSourceID */
    0x01, /* bControlSize */
    0x03, /* bmaControls(0) */
    0x00, /* bmaControls(1) */
    0x00, /* iFeature */

    /* Audio Output Terminal */
    0x09,       /* bLength */
    0x24,       /* bDescriptorType */
    0x03,       /* bDescriptorSubtype */
    0x0a,       /* bTerminalID */
    0x01, 0x01, /* wTerminalType */
    0x00,       /* bAssocTerminal */
    0x02,       /* bSourceID */
    0x00,       /* iTerminal */

    /* Interface 1, Alternate Setting 0, Audio Streaming - Zero Bandwith */
    0x09, /* bLength */
    0x04, /* bDescriptorType */
    0x01, /* bInterfaceNumber */
    0x00, /* bAlternateSetting */
    0x00, /* bNumEndpoints */
    0x01, /* bInterfaceClass */
    0x02, /* bInterfaceSubClass */
    0x00, /* bInterfaceProtocol */
    0x00, /* iInterface */

    /* Interface 1, Alternate Setting 1, Audio Streaming - 1 channel */
    0x09, /* bLength */
    0x04, /* bDescriptorType */
    0x01, /* bInterfaceNumber */
    0x01, /* bAlternateSetting */
    0x01, /* bNumEndpoints */
    0x01, /* bInterfaceClass */
    0x02, /* bInterfaceSubClass */
    0x00, /* bInterfaceProtocol */
    0x00, /* iInterface */

    /* Audio Streaming Interface */
    0x07,       /* bLength */
    0x24,       /* bDescriptorType */
    0x01,       /* bDescriptorSubtype */
    0x0a,       /* bTerminalLink */
    0x00,       /* bDelay */
    0x01, 0x00, /* wFormatTag */

    /* Audio Type I Format */
    0x17,             /* bLength */
    0x24,             /* bDescriptorType */
    0x02,             /* bDescriptorSubtype */
    0x01,             /* bFormatType */
    0x01,             /* bNrChannels */
    0x02,             /* bSubFrameSize */
    0x10,             /* bBitResolution */
    0x05,             /* bSamFreqType */
    0x40, 0x1f, 0x00, /* tSamFreq 1 */
    0x11, 0x2b, 0x00, /* tSamFreq 2 */
    0x22, 0x56, 0x00, /* tSamFreq 3 */
    0x44, 0xac, 0x00, /* tSamFreq 4 */
    0x80, 0xbb, 0x00, /* tSamFreq 5 */

    /* Endpoint - Standard Descriptor */
    0x09,       /* bLength */
    0x05,       /* bDescriptorType */
    0x84,       /* bEndpointAddress */
    0x0d,       /* bmAttributes */
    0x60, 0x00, /* wMaxPacketSize */
    0x01,       /* bInterval */
    0x00,       /* bRefresh */
    0x00,       /* bSynchAddress */

    /* Endpoint - Audio Streaming */
    0x07,       /* bLength */
    0x25,       /* bDescriptorType */
    0x01,       /* bDescriptor */
    0x01,       /* bmAttributes */
    0x02,       /* bLockDelayUnits */
    0x01, 0x00  /* wLockDelay */
};

int LogitechMic::SubmitTransfer(std::unique_ptr<CtrlMessage> cmd)
{
  // Reference: https://www.usb.org/sites/default/files/audio10.pdf
  DEBUG_LOG_FMT(IOS_USB,
                "[{:04x}:{:04x} {}] Control: bRequestType={:02x} bRequest={:02x} wValue={:04x}"
                " wIndex={:04x} wLength={:04x}",
                m_vid, m_pid, m_active_interface, cmd->request_type, cmd->request, cmd->value,
                cmd->index, cmd->length);
  switch (cmd->request_type << 8 | cmd->request)
  {
  case USBHDR(DIR_DEVICE2HOST, TYPE_STANDARD, REC_DEVICE, REQUEST_GET_DESCRIPTOR):
  {
    // It seems every game always pokes this at first twice; one with a length of 9 which gets the
    // config, and then another with the length provided by the config.
    DEBUG_LOG_FMT(IOS_USB, "[{:04x}:{:04x} {}] REQUEST_GET_DESCRIPTOR index={:04x} value={:04x}",
                  m_vid, m_pid, m_active_interface, cmd->index, cmd->value);
    cmd->FillBuffer(FULL_DESCRIPTOR.data(), std::min<size_t>(cmd->length, FULL_DESCRIPTOR.size()));
    cmd->GetEmulationKernel().EnqueueIPCReply(cmd->ios_request, IPC_SUCCESS);
    break;
  }
  case USBHDR(DIR_HOST2DEVICE, TYPE_STANDARD, REC_INTERFACE, REQUEST_SET_INTERFACE):
  {
    DEBUG_LOG_FMT(IOS_USB, "[{:04x}:{:04x} {}] REQUEST_SET_INTERFACE index={:04x} value={:04x}",
                  m_vid, m_pid, m_active_interface, cmd->index, cmd->value);
    if (static_cast<u8>(cmd->index) != m_active_interface)
    {
      const int ret = ChangeInterface(static_cast<u8>(cmd->index));
      if (ret < 0)
      {
        ERROR_LOG_FMT(IOS_USB, "[{:04x}:{:04x} {}] Failed to change interface to {}", m_vid, m_pid,
                      m_active_interface, cmd->index);
        return ret;
      }
    }
    const int ret = SetAltSetting(static_cast<u8>(cmd->value));
    if (ret == 0)
      cmd->GetEmulationKernel().EnqueueIPCReply(cmd->ios_request, cmd->length);
    return ret;
  }
  case USBHDR(DIR_DEVICE2HOST, TYPE_CLASS, REC_INTERFACE, REQUEST_GET_CUR):
  case USBHDR(DIR_DEVICE2HOST, TYPE_CLASS, REC_INTERFACE, REQUEST_GET_MIN):
  case USBHDR(DIR_DEVICE2HOST, TYPE_CLASS, REC_INTERFACE, REQUEST_GET_MAX):
  case USBHDR(DIR_DEVICE2HOST, TYPE_CLASS, REC_INTERFACE, REQUEST_GET_RES):
  {
    DEBUG_LOG_FMT(IOS_USB, "[{:04x}:{:04x} {}] Get Control index={:04x} value={:04x}", m_vid, m_pid,
                  m_active_interface, cmd->index, cmd->value);
    int ret = GetAudioControl(cmd);
    if (ret < 0)
    {
      ERROR_LOG_FMT(IOS_USB,
                    "[{:04x}:{:04x} {}] Get Control Failed index={:04x} value={:04x} ret={}", m_vid,
                    m_pid, m_active_interface, cmd->index, cmd->value, ret);
      goto fail;
    }
    cmd->GetEmulationKernel().EnqueueIPCReply(cmd->ios_request, ret);
    break;
  }
  case USBHDR(DIR_DEVICE2HOST, TYPE_CLASS, REC_INTERFACE, REQUEST_SET_CUR):
  case USBHDR(DIR_DEVICE2HOST, TYPE_CLASS, REC_INTERFACE, REQUEST_SET_MIN):
  case USBHDR(DIR_DEVICE2HOST, TYPE_CLASS, REC_INTERFACE, REQUEST_SET_MAX):
  case USBHDR(DIR_DEVICE2HOST, TYPE_CLASS, REC_INTERFACE, REQUEST_SET_RES):
  {
    DEBUG_LOG_FMT(IOS_USB, "[{:04x}:{:04x} {}] Set Control index={:04x} value={:04x}", m_vid, m_pid,
                  m_active_interface, cmd->index, cmd->value);
    int ret = SetAudioControl(cmd);
    if (ret < 0)
    {
      ERROR_LOG_FMT(IOS_USB,
                    "[{:04x}:{:04x} {}] Set Control Failed index={:04x} value={:04x} ret={}", m_vid,
                    m_pid, m_active_interface, cmd->index, cmd->value, ret);
      goto fail;
    }
    cmd->GetEmulationKernel().EnqueueIPCReply(cmd->ios_request, ret);
    break;
  }
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_ENDPOINT, REQUEST_GET_CUR):
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_ENDPOINT, REQUEST_GET_MIN):
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_ENDPOINT, REQUEST_GET_MAX):
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_ENDPOINT, REQUEST_GET_RES):
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_ENDPOINT, REQUEST_SET_CUR):
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_ENDPOINT, REQUEST_SET_MIN):
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_ENDPOINT, REQUEST_SET_MAX):
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_ENDPOINT, REQUEST_SET_RES):
  {
    DEBUG_LOG_FMT(IOS_USB, "[{:04x}:{:04x} {}] REC_ENDPOINT index={:04x} value={:04x}", m_vid,
                  m_pid, m_active_interface, cmd->index, cmd->value);
    int ret = EndpointAudioControl(cmd);
    if (ret < 0)
    {
      ERROR_LOG_FMT(IOS_USB,
                    "[{:04x}:{:04x} {}] Enndpoint Control Failed index={:04x} value={:04x} ret={}",
                    m_vid, m_pid, m_active_interface, cmd->index, cmd->value, ret);
      goto fail;
    }
    cmd->GetEmulationKernel().EnqueueIPCReply(cmd->ios_request, ret);
    break;
  }
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_INTERFACE, REQUEST_SET_CUR):
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_INTERFACE, REQUEST_SET_MIN):
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_INTERFACE, REQUEST_SET_MAX):
  case USBHDR(DIR_HOST2DEVICE, TYPE_CLASS, REC_INTERFACE, REQUEST_SET_RES):
  {
    DEBUG_LOG_FMT(IOS_USB, "[{:04x}:{:04x} {}] Set Control HOST2DEVICE index={:04x} value={:04x}",
                  m_vid, m_pid, m_active_interface, cmd->index, cmd->value);
    int ret = SetAudioControl(cmd);
    if (ret < 0)
    {
      ERROR_LOG_FMT(
          IOS_USB,
          "[{:04x}:{:04x} {}] Set Control HOST2DEVICE Failed index={:04x} value={:04x} ret={}",
          m_vid, m_pid, m_active_interface, cmd->index, cmd->value, ret);
      goto fail;
    }
    cmd->GetEmulationKernel().EnqueueIPCReply(cmd->ios_request, ret);
    break;
  }
  default:
  fail:
    NOTICE_LOG_FMT(IOS_USB, "Unknown command");
    cmd->GetEmulationKernel().EnqueueIPCReply(cmd->ios_request, IPC_STALL);
  }

  return IPC_SUCCESS;
}

int LogitechMic::SubmitTransfer(std::unique_ptr<BulkMessage> cmd)
{
  cmd->GetEmulationKernel().EnqueueIPCReply(cmd->ios_request, IPC_SUCCESS);
  return IPC_SUCCESS;
}

int LogitechMic::SubmitTransfer(std::unique_ptr<IntrMessage> cmd)
{
  cmd->GetEmulationKernel().EnqueueIPCReply(cmd->ios_request, IPC_SUCCESS);
  return IPC_SUCCESS;
}

int LogitechMic::SubmitTransfer(std::unique_ptr<IsoMessage> cmd)
{
  auto& system = cmd->GetEmulationKernel().GetSystem();
  auto& memory = system.GetMemory();

  u8* packets = memory.GetPointerForRange(cmd->data_address, cmd->length);
  if (packets == nullptr)
  {
    ERROR_LOG_FMT(IOS_USB, "Logitech USB Microphone command invalid");
    return IPC_EINVAL;
  }

  switch (cmd->endpoint)
  {
  case ENDPOINT_AUDIO_IN:
  {
    u16 size = 0;
    if (m_microphone && m_microphone->HasData(cmd->length / sizeof(s16)))
      size = m_microphone->ReadIntoBuffer(packets, cmd->length);
    for (std::size_t i = 0; i < cmd->num_packets; i++)
    {
      cmd->SetPacketReturnValue(i, std::min(size, cmd->packet_sizes[i]));
      size = (size > cmd->packet_sizes[i]) ? (size - cmd->packet_sizes[i]) : 0;
    }
    break;
  }
  default:
  {
    WARN_LOG_FMT(IOS_USB,
                 "Logitech Mic isochronous transfer, unsupported endpoint: length={:04x} "
                 "endpoint={:02x} num_packets={:02x}",
                 cmd->length, cmd->endpoint, cmd->num_packets);
  }
  }

  cmd->FillBuffer(packets, cmd->length);
  cmd->ScheduleTransferCompletion(cmd->length, 1000);
  return IPC_SUCCESS;
}
}  // namespace IOS::HLE::USB
