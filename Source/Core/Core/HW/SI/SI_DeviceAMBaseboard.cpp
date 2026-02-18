// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/SI/SI_DeviceAMBaseboard.h"

#include <algorithm>
#include <string>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/Swap.h"

#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/DVD/AMMediaboard.h"
#include "Core/HW/MagCard/C1231BR.h"
#include "Core/HW/MagCard/C1231LR.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/SystemTimers.h"
#include "Core/HW/Triforce/FZeroAX.h"
#include "Core/HW/Triforce/GekitouProYakyuu.h"
#include "Core/HW/Triforce/KeyOfAvalon.h"
#include "Core/HW/Triforce/MarioKartGP.h"
#include "Core/HW/Triforce/VirtuaStriker.h"
#include "Core/Movie.h"
#include "Core/System.h"

namespace SerialInterface
{
using namespace AMMediaboard;

const constexpr u8 s_region_flags[] = "\x00\x00\x30\x00"
                                      //   "\x01\xfe\x00\x00"  // JAPAN
                                      "\x02\xfd\x00\x00"  // USA
                                      //"\x03\xfc\x00\x00"  // export
                                      "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
// AM-Baseboard device on SI
CSIDevice_AMBaseboard::CSIDevice_AMBaseboard(Core::System& system, SIDevices device,
                                             int device_number)
    : ISIDevice(system, device, device_number)
{
  // Magnetic Card Reader
  m_mag_card_settings.card_path = File::GetUserPath(D_TRIUSER_IDX);
  m_mag_card_settings.card_name = fmt::format("tricard_{}.bin", SConfig::GetInstance().GetGameID());

  // TODO: Do any other games use the Magnetic Card Reader ?

  // TODO: Make a factory for the peripherals.

  switch (AMMediaboard::GetGameType())
  {
  case FZeroAX:
    m_mag_card_reader = std::make_unique<MagCard::C1231BR>(&m_mag_card_settings);
    m_peripheral = std::make_unique<TriforcePeripheral::FZeroAX>();
    break;

  case FZeroAXMonster:
    m_mag_card_reader = std::make_unique<MagCard::C1231BR>(&m_mag_card_settings);
    m_peripheral = std::make_unique<TriforcePeripheral::FZeroAXMonster>();
    break;

  case MarioKartGP:
  case MarioKartGP2:
    m_mag_card_reader = std::make_unique<MagCard::C1231LR>(&m_mag_card_settings);
    m_peripheral = std::make_unique<TriforcePeripheral::MarioKartGP>();
    break;

  case VirtuaStriker3:
    m_peripheral = std::make_unique<TriforcePeripheral::VirtuaStriker3>();
    break;

  case VirtuaStriker4:
  case VirtuaStriker4_2006:
    m_peripheral = std::make_unique<TriforcePeripheral::VirtuaStriker4>();
    break;

  case GekitouProYakyuu:
    m_peripheral = std::make_unique<TriforcePeripheral::GekitouProYakyuu>();
    break;

  case KeyOfAvalon:
    m_peripheral = std::make_unique<TriforcePeripheral::KeyOfAvalon>();
    break;

  default:
    break;
  }
}

constexpr u32 SI_XFER_LENGTH_MASK = 0x7f;

// Translate [0,1,2,...,126,127] to [128,1,2,...,126,127]
constexpr s32 ConvertSILengthField(u32 field)
{
  return ((field - 1) & SI_XFER_LENGTH_MASK) + 1;
}

void CSIDevice_AMBaseboard::SwapBuffers(u8* buffer, u32* buffer_length)
{
  memcpy(m_last[1], buffer, 0x80);     // Save current buffer
  memcpy(buffer, m_last[0], 0x80);     // Load previous buffer
  memcpy(m_last[0], m_last[1], 0x80);  // Update history

  m_lastptr[1] = *buffer_length;  // Swap lengths
  *buffer_length = m_lastptr[0];
  m_lastptr[0] = m_lastptr[1];
}

int CSIDevice_AMBaseboard::RunBuffer(u8* buffer, int request_length)
{
  const auto& serial_interface = m_system.GetSerialInterface();
  u32 buffer_length = ConvertSILengthField(serial_interface.GetInLength());

  // Debug logging
  ISIDevice::RunBuffer(buffer, buffer_length);

  u32 buffer_position = 0;

  const auto bb_command = static_cast<BaseBoardCommand>(buffer[buffer_position]);

  switch (bb_command)
  {
  case BaseBoardCommand::GCAM_Reset:  // Returns ID and dip switches
  {
    const u32 id = Common::swap32(SI_AM_BASEBOARD | 0x100);
    std::memcpy(buffer, &id, sizeof(id));
    return sizeof(id);
  }
  case BaseBoardCommand::GCAM_Command:
  {
    // Request:
    // [70][size][command][params][command][params]...[checksum]
    //
    // Response:
    // [command][size][...][checksum]

    const u8 command_length = buffer[buffer_position + 1];
    const u8 read_checksum = buffer[buffer_position + command_length];

    const auto range_to_checksum = std::span{buffer + buffer_position, command_length};
    const auto proper_checksum =
        std::accumulate(range_to_checksum.begin(), range_to_checksum.end(), u8(), std::plus{});

    if (read_checksum != proper_checksum)
      WARN_LOG_FMT(SERIALINTERFACE, "Bad checksum");

    buffer_position += 2;

    std::array<u8, 0x80> data_out{};

    const u32 out_length =
        RunGCAMBuffer(std::span(buffer + buffer_position, command_length - 2), data_out);

    buffer_position += out_length;

    break;
  }
  default:
  {
    ERROR_LOG_FMT(SERIALINTERFACE, "Unknown SI command (0x{:08x})", u8(bb_command));
    buffer_position = buffer_length;
    break;
  }
  }

  return buffer_position;
}

u32 CSIDevice_AMBaseboard::RunGCAMBuffer(std::span<const u8> input, std::span<u8> data_out)
{
  auto data_in = input.begin();
  u32 data_offset = 0;

  while (data_in != input.end())
  {
    const u8 gcam_command = *data_in++;
    switch (GCAMCommand(gcam_command))
    {
    case GCAMCommand::StatusSwitches:
    {
      std::tie(data_out[data_offset + 0], data_out[data_offset + 1]) =
          m_peripheral->GetDipSwitches();
      data_offset += 2;
      break;
    }
    case GCAMCommand::SerialNumber:
    {
      if (!validate_data_in_out(1, 18, "SerialNumber"))
        break;

      NOTICE_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x11, {:02x} (READ SERIAL NR)",
                     *data_in);
      data_in++;

      data_out[data_offset++] = gcam_command;
      data_out[data_offset++] = 16;
      memcpy(data_out.data() + data_offset, "AADE-01B98394904", 16);

      data_offset += 16;
      break;
    }
    case GCAMCommand::Unknown_12:
      if (!validate_data_in_out(2, 2, "Unknown_12"))
        break;

      NOTICE_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x12, {:02x} {:02x}", data_in[0],
                     data_in[1]);

      data_out[data_offset++] = gcam_command;
      data_out[data_offset++] = 0x00;

      data_in += 2;
      break;
    case GCAMCommand::Unknown_14:
      if (!validate_data_in_out(2, 2, "Unknown_14"))
        break;

      NOTICE_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x14, {:02x} {:02x}", data_in[0],
                     data_in[1]);

      data_out[data_offset++] = gcam_command;
      data_out[data_offset++] = 0x00;

      data_in += 2;
      break;
    case GCAMCommand::FirmVersion:
      if (!validate_data_in_out(1, 4, "FirmVersion"))
        break;

      NOTICE_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x15, {:02x} (READ FIRM VERSION)",
                     *data_in);
      data_in++;

      data_out[data_offset++] = gcam_command;
      data_out[data_offset++] = 0x02;
      // 00.26
      data_out[data_offset++] = 0x00;
      data_out[data_offset++] = 0x26;
      break;
    case GCAMCommand::FPGAVersion:
      if (!validate_data_in_out(1, 4, "FPGAVersion"))
        break;

      NOTICE_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x16, {:02x} (READ FPGA VERSION)",
                     *data_in);
      data_in++;

      data_out[data_offset++] = gcam_command;
      data_out[data_offset++] = 0x02;
      // 07.06
      data_out[data_offset++] = 0x07;
      data_out[data_offset++] = 0x06;
      break;
    case GCAMCommand::RegionSettings:
    {
      if (!validate_data_in_out(5, 0x16, "RegionSettings"))
        break;

      // Used by SegaBoot for region checks (dev mode skips this check)
      // In some games this also controls the displayed language
      NOTICE_LOG_FMT(SERIALINTERFACE_AMBB,
                     "GC-AM: Command 0x1F, {:02x} {:02x} {:02x} {:02x} {:02x} (REGION)", data_in[0],
                     data_in[1], data_in[2], data_in[3], data_in[4]);

      data_out[data_offset++] = gcam_command;
      data_out[data_offset++] = 0x14;

      for (int i = 0; i < 0x14; ++i)
        data_out[data_offset++] = s_region_flags[i];

      data_in += 5;
    }
    break;
    // No reply
    // Note: Always sends three bytes even though size is set to two
    case GCAMCommand::Unknown_21:
    {
      if (!validate_data_in_out(4, 0, "Unknown_21"))
        break;

      DEBUG_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x21, {:02x}, {:02x}, {:02x}, {:02x}",
                    data_in[0], data_in[1], data_in[2], data_in[3]);
      data_in += 4;
    }
    break;
    // No reply
    // Note: Always sends six bytes
    case GCAMCommand::Unknown_22:
    {
      if (!validate_data_in_out(7, 0, "Unknown_22"))
        break;

      DEBUG_LOG_FMT(SERIALINTERFACE_AMBB,
                    "GC-AM: Command 0x22, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}",
                    data_in[0], data_in[1], data_in[2], data_in[3], data_in[4], data_in[5],
                    data_in[6]);

      const u32 in_size = data_in[0] + 1;
      if (!validate_data_in_out(in_size, 0, "Unknown_22"))
        break;
      data_in += in_size;
    }
    break;
    case GCAMCommand::Unknown_23:
      if (!validate_data_in_out(2, 2, "Unknown_23"))
        break;

      DEBUG_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x23, {:02x} {:02x}", data_in[0],
                    data_in[1]);

      data_out[data_offset++] = gcam_command;
      data_out[data_offset++] = 0x00;

      data_in += 2;
      break;
    case GCAMCommand::Unknown_24:
      if (!validate_data_in_out(2, 2, "Unknown_24"))
        break;

      DEBUG_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x24, {:02x} {:02x}", data_in[0],
                    data_in[1]);

      data_out[data_offset++] = gcam_command;
      data_out[data_offset++] = 0x00;

      data_in += 2;
      break;
    case GCAMCommand::SerialA:
    {
      if (!validate_data_in_out(1, 0, "SerialA"))
        break;
      const u32 length = *data_in++;

      if (length)
      {
        if (!validate_data_in_out(length, 0, "SerialA"))
          break;

        INFO_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x31, length=0x{:02x}, hexdump:\n{}",
                     length, HexDump(data_in, length));

        data_out[data_offset++] = gcam_command;
        auto& written = data_out[data_offset++];

        written = m_peripheral->SerialA(std::span{data_in, data_in_end},
                                        std::span{data_out}.subspan(data_offset));
        data_in += length;
        data_offset += written;
      }

      // TODO: Is this some non-response ?
      // if (serial_command == 0x801000)
      // {
      //   if (!validate_data_in_out(0, 4, "SerialA"))
      //     break;
      // data_out[data_offset++] = 0xFF;
      // data_out[data_offset++] = 0x01;
      // }

      break;
    }
    case GCAMCommand::SerialB:
    {
      DEBUG_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 32 (CARD-Interface)");

      if (!validate_data_in_out(1, 0, "SerialB"))
        break;
      const u32 in_length = *data_in++;

      static constexpr u32 max_packet_size = 0x2f;

      // Also accounting for the 2-byte header.
      if (!validate_data_in_out(in_length, max_packet_size + 2, "SerialB"))
        break;

      if (m_mag_card_reader)
      {
        // Append the data to our buffer.
        const auto prev_size = m_mag_card_in_buffer.size();
        m_mag_card_in_buffer.resize(prev_size + in_length);
        std::ranges::copy(std::span{data_in, in_length}, m_mag_card_in_buffer.data() + prev_size);

        // Send and receive data with the magnetic card reader.
        m_mag_card_reader->Process(&m_mag_card_in_buffer, &m_mag_card_out_buffer);
      }

      data_in += in_length;
      const auto out_length = std::min(u32(m_mag_card_out_buffer.size()), max_packet_size);

      // Write the 2-byte header.
      data_out[data_offset++] = gcam_command;
      data_out[data_offset++] = u8(out_length);

      // Write the data.
      std::copy_n(m_mag_card_out_buffer.data(), out_length, data_out.data() + data_offset);
      data_offset += out_length;

      // Remove the data from our buffer.
      m_mag_card_out_buffer.erase(m_mag_card_out_buffer.begin(),
                                  m_mag_card_out_buffer.begin() + s32(out_length));
      break;
    }
    case GCAMCommand::JVSIOA:
    case GCAMCommand::JVSIOB:
    {
      // JVSIOMessage message;

      const u8* const frame = &data_in[0];
      // const u8 nr_bytes = frame[3];  // Byte after E0 xx
      //  u32 frame_len = nr_bytes + 3;  // Header(2) + length byte + payload + checksum

      // m_peripheral->ProcessJVSIO();

      const u32 in_size = frame[0] + 1;
      data_in += in_size;

      const u8 out_length = 0;

      data_out[data_offset++] = gcam_command;
      data_out[data_offset++] = u8(out_length);

      break;
    }
    case GCAMCommand::Unknown_60:
    {
      if (!validate_data_in_out(3, 0, "Unknown_60"))
        break;

      NOTICE_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x60, {:02x} {:02x} {:02x}", data_in[0],
                     data_in[1], data_in[2]);

      const u32 in_size = data_in[0] + 1;
      if (!validate_data_in_out(in_size, 0, "Unknown_60"))
        break;
      data_in += in_size;
    }
    break;
    default:
      if (!validate_data_in_out(5, 0, fmt::format("Unknown_{}", gcam_command)))
        break;
      ERROR_LOG_FMT(SERIALINTERFACE_AMBB,
                    "GC-AM: Command {:02x} (unknown) {:02x} {:02x} {:02x} {:02x} {:02x}",
                    gcam_command, data_in[0], data_in[1], data_in[2], data_in[3], data_in[4]);
      break;
    }
  }

  memset(buffer, 0, buffer_length);

  data_in = buffer;
  data_out[1] = data_offset - 2;
  checksum = 0;

  if (buffer_length >= 0x80)
  {
    for (int i = 0; i < 0x7F; ++i)
    {
      checksum += data_in[i] = data_out[i];
    }
    data_in[0x7f] = ~checksum;
    DEBUG_LOG_FMT(SERIALINTERFACE_AMBB, "Command send back: {}", HexDump(data_out.data(), 0x7F));
  }
  else
  {
    ERROR_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: overflow in GCAM_Command's checksum");
  }

  SwapBuffers(buffer, &buffer_length);
}

return buffer_position;
}

u32 CSIDevice_AMBaseboard::MapPadStatus(const GCPadStatus& pad_status)
{
  // Thankfully changing mode does not change the high bits ;)
  u32 hi = 0;
  hi = pad_status.stickY;
  hi |= pad_status.stickX << 8;
  hi |= (pad_status.button | PAD_USE_ORIGIN) << 16;
  return hi;
}

CSIDevice_AMBaseboard::EButtonCombo
CSIDevice_AMBaseboard::HandleButtonCombos(const GCPadStatus& pad_status)
{
  // Keep track of the special button combos (embedded in controller hardware... :( )
  EButtonCombo temp_combo = COMBO_NONE;
  if ((pad_status.button & 0xff00) == (PAD_BUTTON_Y | PAD_BUTTON_X | PAD_BUTTON_START))
    temp_combo = COMBO_ORIGIN;
  else if ((pad_status.button & 0xff00) == (PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_START))
    temp_combo = COMBO_RESET;

  if (temp_combo != m_last_button_combo)
  {
    m_last_button_combo = temp_combo;
    if (m_last_button_combo != COMBO_NONE)
      m_timer_button_combo_start = m_system.GetCoreTiming().GetTicks();
  }

  if (m_last_button_combo != COMBO_NONE)
  {
    const u64 current_time = m_system.GetCoreTiming().GetTicks();
    const u32 ticks_per_second = m_system.GetSystemTimers().GetTicksPerSecond();
    if (u32(current_time - m_timer_button_combo_start) > ticks_per_second * 3)
    {
      if (m_last_button_combo == COMBO_RESET)
      {
        INFO_LOG_FMT(SERIALINTERFACE, "PAD - COMBO_RESET");
        m_system.GetProcessorInterface().ResetButton_Tap();
      }
      else if (m_last_button_combo == COMBO_ORIGIN)
      {
        INFO_LOG_FMT(SERIALINTERFACE, "PAD - COMBO_ORIGIN");
        SetOrigin(pad_status);
      }

      m_last_button_combo = COMBO_NONE;
      return temp_combo;
    }
  }

  return COMBO_NONE;
}

// GetData

// Return true on new data (max 7 Bytes and 6 bits ;)
// [00?SYXBA] [1LRZUDRL] [x] [y] [cx] [cy] [l] [r]
//  |\_ ERR_LATCH (error latched - check SISR)
//  |_ ERR_STATUS (error on last GetData or SendCmd?)
DataResponse CSIDevice_AMBaseboard::GetData(u32& hi, u32& low)
{
  GCPadStatus pad_status = GetPadStatus();

  if (!pad_status.isConnected)
    return DataResponse::ErrorNoResponse;

  if (HandleButtonCombos(pad_status) == COMBO_ORIGIN)
    pad_status.button |= PAD_GET_ORIGIN;

  hi = MapPadStatus(pad_status);

  // Low bits are packed differently per mode
  if (m_mode == 0 || m_mode == 5 || m_mode == 6 || m_mode == 7)
  {
    low = (pad_status.analogB >> 4);               // Top 4 bits
    low |= ((pad_status.analogA >> 4) << 4);       // Top 4 bits
    low |= ((pad_status.triggerRight >> 4) << 8);  // Top 4 bits
    low |= ((pad_status.triggerLeft >> 4) << 12);  // Top 4 bits
    low |= ((pad_status.substickY) << 16);         // All 8 bits
    low |= ((pad_status.substickX) << 24);         // All 8 bits
  }
  else if (m_mode == 1)
  {
    low = (pad_status.analogB >> 4);             // Top 4 bits
    low |= ((pad_status.analogA >> 4) << 4);     // Top 4 bits
    low |= (pad_status.triggerRight << 8);       // All 8 bits
    low |= (pad_status.triggerLeft << 16);       // All 8 bits
    low |= ((pad_status.substickY >> 4) << 24);  // Top 4 bits
    low |= ((pad_status.substickX >> 4) << 28);  // Top 4 bits
  }
  else if (m_mode == 2)
  {
    low = pad_status.analogB;                       // All 8 bits
    low |= pad_status.analogA << 8;                 // All 8 bits
    low |= ((pad_status.triggerRight >> 4) << 16);  // Top 4 bits
    low |= ((pad_status.triggerLeft >> 4) << 20);   // Top 4 bits
    low |= ((pad_status.substickY >> 4) << 24);     // Top 4 bits
    low |= ((pad_status.substickX >> 4) << 28);     // Top 4 bits
  }
  else if (m_mode == 3)
  {
    // Analog A/B are always 0
    low = pad_status.triggerRight;         // All 8 bits
    low |= (pad_status.triggerLeft << 8);  // All 8 bits
    low |= (pad_status.substickY << 16);   // All 8 bits
    low |= (pad_status.substickX << 24);   // All 8 bits
  }
  else if (m_mode == 4)
  {
    low = pad_status.analogB;        // All 8 bits
    low |= pad_status.analogA << 8;  // All 8 bits
    // triggerLeft/Right are always 0
    low |= pad_status.substickY << 16;  // All 8 bits
    low |= pad_status.substickX << 24;  // All 8 bits
  }

  return DataResponse::Success;
}

void CSIDevice_AMBaseboard::SendCommand(u32 command, u8 poll)
{
  UCommand controller_command(command);

  if (static_cast<EDirectCommands>(controller_command.command) == EDirectCommands::CMD_WRITE)
  {
    const u32 type = controller_command.parameter1;  // 0 = stop, 1 = rumble, 2 = stop hard

    // get the correct pad number that should rumble locally when using netplay
    const int pad_num = NetPlay_InGamePadToLocalPad(m_device_number);

    if (pad_num < 4)
    {
      const SIDevices device = m_system.GetSerialInterface().GetDeviceType(pad_num);
      if (type == 1)
        CSIDevice_GCController::Rumble(pad_num, 1.0, device);
      else
        CSIDevice_GCController::Rumble(pad_num, 0.0, device);
    }

    if (poll == 0)
    {
      m_mode = controller_command.parameter2;
      INFO_LOG_FMT(SERIALINTERFACE, "PAD {} set to mode {}", m_device_number, m_mode);
    }
  }
  else if (controller_command.command != 0x00)
  {
    // Costis sent 0x00 in some demos :)
    ERROR_LOG_FMT(SERIALINTERFACE, "Unknown direct command     ({:#x})", command);
    PanicAlertFmt("SI: Unknown direct command");
  }
}

void CSIDevice_AMBaseboard::SetOrigin(const GCPadStatus& pad_status)
{
  m_origin.origin_stick_x = pad_status.stickX;
  m_origin.origin_stick_y = pad_status.stickY;
  m_origin.substick_x = pad_status.substickX;
  m_origin.substick_y = pad_status.substickY;
  m_origin.trigger_left = pad_status.triggerLeft;
  m_origin.trigger_right = pad_status.triggerRight;
}

void CSIDevice_AMBaseboard::HandleMoviePadStatus(Movie::MovieManager& movie, int device_number,
                                                 GCPadStatus* pad_status)
{
  movie.SetPolledDevice();
  if (NetPlay_GetInput(device_number, pad_status))
  {
  }
  else if (movie.IsPlayingInput())
  {
    movie.PlayController(pad_status, device_number);
    movie.InputUpdate();
  }
  else if (movie.IsRecordingInput())
  {
    movie.RecordInput(pad_status, device_number);
    movie.InputUpdate();
  }
  else
  {
    movie.CheckPadStatus(pad_status, device_number);
  }
}

GCPadStatus CSIDevice_AMBaseboard::GetPadStatus()
{
  GCPadStatus pad_status = {};

  // For netplay, the local controllers are polled in GetNetPads(), and
  // the remote controllers receive their status there as well
  if (!NetPlay::IsNetPlayRunning())
  {
    pad_status = Pad::GetStatus(m_device_number);
  }

  // Our GCAdapter code sets PAD_GET_ORIGIN when a new device has been connected.
  // Watch for this to calibrate real controllers on connection.
  if (pad_status.button & PAD_GET_ORIGIN)
    SetOrigin(pad_status);

  return pad_status;
}

void CSIDevice_AMBaseboard::DoState(PointerWrap& p)
{
  p.Do(m_origin);
  p.Do(m_mode);
  p.Do(m_timer_button_combo_start);
  p.Do(m_last_button_combo);

  p.Do(m_last);
  p.Do(m_lastptr);

  m_peripheral->DoState(p);

  // Magnetic Card Reader
  if (m_mag_card_reader)
  {
    m_mag_card_reader->DoState(p);

    p.Do(m_mag_card_in_buffer);
    p.Do(m_mag_card_out_buffer);
  }
}

}  // namespace SerialInterface
