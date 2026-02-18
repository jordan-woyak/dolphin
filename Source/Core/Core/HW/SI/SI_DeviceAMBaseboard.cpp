// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/SI/SI_DeviceAMBaseboard.h"

#include <algorithm>
#include <string>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
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
static constexpr s32 ConvertSILengthField(u32 field)
{
  return ((field - 1) & SI_XFER_LENGTH_MASK) + 1;
}

int CSIDevice_AMBaseboard::RunBuffer(u8* buffer, int request_length)
{
  const auto& serial_interface = m_system.GetSerialInterface();

  // TODO: Make these make sense..
  u32 buffer_length = ConvertSILengthField(serial_interface.GetInLength());

  // Debug logging
  ISIDevice::RunBuffer(buffer, buffer_length);

  const auto bb_command = static_cast<BaseBoardCommand>(buffer[0]);

  switch (bb_command)
  {
  case BaseBoardCommand::GCAM_Reset:  // Returns ID and dip switches
  {
    // Request is just the 1 byte.

    const u32 id = Common::swap32(SI_AM_BASEBOARD | 0x100);
    std::memcpy(buffer, &id, sizeof(id));
    return sizeof(id);
  }
  case BaseBoardCommand::GCAM_Command:
  {
    // Request:
    // [70][size][command1][params1][command2][params2]...[checksum]
    //
    // Response:
    // [command1][size1][data1...][command2][size2][data2...]...[checksum]

    const u8 command_length = buffer[1];
    const u8 read_checksum = buffer[command_length];

    const auto range_to_checksum = std::span{buffer, command_length};
    const auto proper_checksum =
        std::accumulate(range_to_checksum.begin(), range_to_checksum.end(), u8());

    if (read_checksum != proper_checksum)
      WARN_LOG_FMT(SERIALINTERFACE, "Bad checksum");

    auto& data_out = m_response_buffers[m_current_response_buffer];

    const u32 out_length = RunGCAMBuffer(std::span(buffer + 2, command_length - 2), data_out);

    break;
  }
  default:
  {
    ERROR_LOG_FMT(SERIALINTERFACE, "Unknown SI command (0x{:08x})", u8(bb_command));
    return 0;
  }
  }

  // TODO: Copy buffer..

  return STANDARD_RESPONSE_SIZE;
}

u32 CSIDevice_AMBaseboard::RunGCAMBuffer(std::span<const u8> input, std::span<u8> data_out)
{
  auto data_in = input.begin();
  const auto data_in_end = input.end();
  u32 data_offset = 2;

  // Helper to check that iterating over data n times is safe,
  // i.e. *data++ at most lead to data.end()
  const auto validate_data_in_out = [&](u32 n_in, u32 n_out, std::string_view command) -> bool {
    if (data_in + n_in > data_in_end)
      ERROR_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: data_in overflow in {}", command);
    else if (u64{data_offset} + n_out > data_out.size())
      ERROR_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: data_out overflow in {}", command);
    else
      return true;
    // ERROR_LOG_FMT(SERIALINTERFACE_AMBB,
    //               "Overflow details:\n"
    //               " - data_in(begin={}, current={}, end={}, n_in={})\n"
    //               " - data_out(offset={}, size={}, n_out={})\n"
    //               " - buffer(position={}, length={})",
    //               fmt::ptr(buffer + 2), fmt::ptr(data_in), fmt::ptr(data_in_end), n_in,
    //               data_offset, data_out.size(), n_out, buffer_position, buffer_length);
    data_in = data_in_end;
    return false;
  };

  while (data_in != data_in_end)
  {
    const auto gcam_command_offset = data_offset;
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

      memcpy(data_out.data() + data_offset, "AADE-01B98394904", 16);

      data_offset += 16;
      break;
    }
    case GCAMCommand::Unknown_12:
      if (!validate_data_in_out(2, 2, "Unknown_12"))
        break;

      NOTICE_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x12, {:02x} {:02x}", data_in[0],
                     data_in[1]);

      data_in += 2;
      break;
    case GCAMCommand::Unknown_14:
      if (!validate_data_in_out(2, 2, "Unknown_14"))
        break;

      NOTICE_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x14, {:02x} {:02x}", data_in[0],
                     data_in[1]);

      data_in += 2;
      break;
    case GCAMCommand::FirmVersion:
      if (!validate_data_in_out(1, 4, "FirmVersion"))
        break;

      NOTICE_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x15, {:02x} (READ FIRM VERSION)",
                     *data_in);
      data_in++;

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

      data_in += 2;
      break;
    case GCAMCommand::Unknown_24:
      if (!validate_data_in_out(2, 2, "Unknown_24"))
        break;

      DEBUG_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x24, {:02x} {:02x}", data_in[0],
                    data_in[1]);

      data_in += 2;
      break;
    case GCAMCommand::SerialA:
    {
      if (!validate_data_in_out(1, 0, "SerialA"))
        break;
      const u32 length = *data_in++;

      if (!validate_data_in_out(length, 0, "SerialA"))
        break;

      // INFO_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x31, length=0x{:02x}, hexdump:\n{}",
      //              length, HexDump(data_in, length));

      const u32 written = m_peripheral->SerialA(std::span{data_in, data_in_end},
                                                std::span{data_out}.subspan(data_offset));
      data_in += length;
      data_offset += written;

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

      const auto written = m_peripheral->ProcessJVSIO({data_in, data_in_end});

      const u32 in_size = frame[0] + 1;
      data_in += in_size;

      const u8 out_length = 0;

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

    // Write the 2-byte header.
    data_out[gcam_command_offset] = gcam_command;
    data_out[gcam_command_offset + 1] = data_offset = gcam_command_offset;
  }
}

DataResponse CSIDevice_AMBaseboard::GetData(u32&, u32&)
{
  return DataResponse::NoData;
}

void CSIDevice_AMBaseboard::SendCommand(u32 command, u8 poll)
{
  ERROR_LOG_FMT(SERIALINTERFACE, "Unhandled CSIDevice_AMBaseboard::SendCommand(0x{:08x}, 0x{:02x})",
                command, poll);
}

void CSIDevice_AMBaseboard::DoState(PointerWrap& p)
{
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
