
// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/ICCardReader.h"

#include <numeric>

#include "Common/BitUtils.h"
#include "Common/ChunkFile.h"
#include "Common/Logging/Log.h"
#include "Common/Swap.h"

#include "Core/HW/DVD/AMMediaboard.h"

namespace
{

constexpr std::string_view cdr_program_version = "           Version 1.22,2003/09/19,171-8213B";
constexpr std::string_view cdr_boot_version = "           Version 1.04,2003/06/17,171-8213B";

constexpr u8 cdr_card_data[] = {
    0x00, 0x6E, 0x00, 0x00, 0x01, 0x00, 0x00, 0x06, 0x00, 0x00, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00,
    0x0E, 0x00, 0x00, 0x10, 0x00, 0x00, 0x17, 0x00, 0x00, 0x19, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x1B,
    0x00, 0x00, 0x1D, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x20, 0x00, 0x00, 0x22, 0x00, 0x00, 0x23, 0x00,
    0x00, 0x24, 0x00, 0x00, 0x27, 0x00, 0x00, 0x28, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x2F, 0x00, 0x00,
    0x34, 0x00, 0x00, 0x35, 0x00, 0x00, 0x37, 0x00, 0x00, 0x38, 0x00, 0x00, 0x39, 0x00, 0x00, 0x3D,
};

constexpr u8 CheckSumXOR(std::span<const u8> data)
{
  return std::accumulate(data.data(), data.data() + data.size(), u8{}, std::bit_xor());
}

constexpr u32 USE_COUNT_OFFSET = 0x28;

}  // namespace

namespace Triforce
{

struct ICCardReply
{
  u8 fixed;
  u8 command;

  u8 flag;
  u8 length;  // Number of payload bytes (status + extdata).
  u16 status;

  // TODO: Is this supposed to just be as large as needed ?
  // Is it 80 to not overflow in RunBuffer ?
  // That should be resolved now with SERIAL_PORT_MAX_READ_SIZE.
  u8 extdata[80];  // Variable in size.

  // A checksum byte follows.
};

enum CDReaderCommand
{
  ShutterAuto = 0x61,
  BootVersion = 0x62,
  SensLock = 0x63,
  SensCard = 0x65,
  FirmwareUpdate = 0x66,
  ShutterGet = 0x67,
  CameraCheck = 0x68,
  ShutterCard = 0x69,
  ProgramChecksum = 0x6b,
  BootChecksum = 0x6d,
  ShutterLoad = 0x6f,
  ReadCard = 0x72,
  ShutterSave = 0x73,
  SelfTest = 0x74,
  ProgramVersion = 0x76,
};

enum ICCARDCommand
{
  GetStatus = 0x10,
  SetBaudrate = 0x11,
  FieldOn = 0x14,
  FieldOff = 0x15,
  InsertCheck = 0x20,
  AntiCollision = 0x21,
  SelectCard = 0x22,
  ReadPage = 0x24,
  WritePage = 0x25,
  DecreaseUseCount = 0x26,
  ReadUseCount = 0x33,
  ReadPages = 0x34,
  WritePages = 0x35,
};

ICCardReader::ICCardReader()
{
  // Card ID
  m_ic_card_data[0x20] = 0x95;
  m_ic_card_data[0x21] = 0x71;

  switch (AMMediaboard::GetGameType())
  {
  case AMMediaboard::KeyOfAvalon:
    m_ic_card_data[0x22] = 0x26;
    m_ic_card_data[0x23] = 0x40;
    break;

  case AMMediaboard::VirtuaStriker4:
  case AMMediaboard::VirtuaStriker4_2006:
    m_ic_card_data[0x22] = 0x44;
    m_ic_card_data[0x23] = 0x00;
    break;

  default:
    break;
  }

  // Use count
  m_ic_card_data[USE_COUNT_OFFSET + 0] = 0xff;
  m_ic_card_data[USE_COUNT_OFFSET + 1] = 0xff;
}

void ICCardReader::Process()
{
  const auto input_span = GetInputSpan();
  if (input_span.size() < 4)
    return;  // Wait for more data.

  const u16 payload_size = Common::swap16(input_span.data() + 2);
  // 4 header bytes + 1 checksum byte
  const u32 total_size = payload_size + 5u;

  if (input_span.size() < total_size)
    return;  // Wait for more data.

  const auto request_data = input_span.first(total_size);

  const u8 read_checksum = request_data.back();
  const u8 proper_checksum = CheckSumXOR(std::span{request_data}.first(total_size - 1));

  if (read_checksum != proper_checksum)
  {
    WARN_LOG_FMT(SERIALINTERFACE_CARD, "Bad checksum!");
    return;
  }

  const u8 card_command = request_data[1];

  ICCardReply reply{
      .fixed = 0x10,
      .command = card_command,
      .flag = 0,
      .length = sizeof(ICCardReply::status),
      .status = 0,
  };

  switch (ICCARDCommand(card_command))
  {
  case ICCARDCommand::GetStatus:
    reply.status = m_ic_card_state;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Get Status:{:02x}",
                 m_ic_card_state);
    break;
  case ICCARDCommand::SetBaudrate:
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Set Baudrate");
    break;
  case ICCARDCommand::FieldOn:
    m_ic_card_state |= 0x10;
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Field On");
    break;
  case ICCARDCommand::InsertCheck:
    reply.status = m_ic_card_status;
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Insert Check:{:02x}",
                 m_ic_card_status);
    break;
  case ICCARDCommand::AntiCollision:
    reply.length += 8;

    // Card ID
    reply.extdata[0] = 0x00;
    reply.extdata[1] = 0x00;
    reply.extdata[2] = 0x54;
    reply.extdata[3] = 0x4D;
    reply.extdata[4] = 0x50;
    reply.extdata[5] = 0x00;
    reply.extdata[6] = 0x00;
    reply.extdata[7] = 0x00;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Anti Collision");
    break;
  case ICCARDCommand::SelectCard:
    reply.length += 8;

    // Session
    reply.extdata[0] = 0x00;
    reply.extdata[1] = m_ic_card_session;
    reply.extdata[2] = 0x00;
    reply.extdata[3] = 0x00;
    reply.extdata[4] = 0x00;
    reply.extdata[5] = 0x00;
    reply.extdata[6] = 0x00;
    reply.extdata[7] = 0x00;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Select Card:{}",
                 m_ic_card_session);
    break;
  case ICCARDCommand::ReadPage:
  case ICCARDCommand::ReadUseCount:
  {
    const std::size_t page = Common::swap16(request_data.data() + 6) & 0xFF;  // 255 is max page

    constexpr u32 read_size = 8;

    reply.length += read_size;
    std::copy_n(m_ic_card_data + (page * 8), read_size, reply.extdata);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 31 (IC-CARD) Read Page:{}", page);
    break;
  }
  case ICCARDCommand::WritePage:
  {
    const std::size_t page = Common::swap16(request_data.data() + 8) & 0xFF;  // 255 is max page

    // Write only one page
    if (page == 4)
    {
      reply.status = 0x80;
    }
    else
    {
      std::copy_n(request_data.data() + 10, 8, m_ic_card_data + (page * 8));
    }

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Write Page:{}", page);
    break;
  }
  case ICCARDCommand::DecreaseUseCount:
  {
    const u16 page = Common::swap16(request_data.data() + 6) & 0xFF;  // 255 is max page

    auto ic_card_data = Common::BitCastPtr<u16>(m_ic_card_data + USE_COUNT_OFFSET);
    ic_card_data = ic_card_data - 1;

    reply.length += 2;

    // Counter
    reply.extdata[0] = m_ic_card_data[USE_COUNT_OFFSET + 0];
    reply.extdata[1] = m_ic_card_data[USE_COUNT_OFFSET + 1];

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 31 (IC-CARD) Decrease Use Count:{}", page);
    break;
  }
  case ICCARDCommand::ReadPages:
  {
    const u16 page = Common::swap16(request_data.data() + 6) & 0xFF;  // 255 is max page
    const u16 count = Common::swap16(request_data.data() + 8);

    const u32 byte_offset = page * 8;
    u32 byte_count = count * 8;

    // Limit read size to not overwrite the reply buffer
    // TODO: Is this really how the hardware behaves ?
    constexpr std::size_t reply_buffer_size = std::size(reply.extdata);
    if (byte_count > reply_buffer_size)
    {
      WARN_LOG_FMT(SERIALINTERFACE_CARD, "ReadPages: Limiting read size of {}.", byte_count);
      byte_count = 5 * 8;
    }

    reply.length += byte_count;

    std::copy_n(m_ic_card_data + byte_offset, byte_count, reply.extdata);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 31 (IC-CARD) Read Pages:{} Count:{}", page,
                 count);
    break;
  }
  case ICCARDCommand::WritePages:
  {
    const u32 page = Common::swap16(request_data.data() + 6) & 0xff;  // 255 is max page
    const u32 count = Common::swap16(request_data.data() + 8);
    const u32 write_size = count * 8;
    const u32 write_offset = page * 8;

    if (page == 4)  // Read Only Page, must return error
    {
      reply.status = 0x80;
    }
    else
    {
      if (write_size + write_offset > sizeof(m_ic_card_data))
      {
        ERROR_LOG_FMT(SERIALINTERFACE_CARD,
                      "GC-AM: Command 0x31 (IC-CARD) Data overflow: Pages:{} Count:{}({:x})", page,
                      count, payload_size);
      }
      else
      {
        std::copy_n(request_data.data() + 13, write_size, m_ic_card_data + write_offset);
      }
    }

    INFO_LOG_FMT(SERIALINTERFACE_CARD,
                 "GC-AM: Command 0x31 (IC-CARD) Write Pages:{} Count:{}({:x})", page, count,
                 payload_size);

    break;
  }
  default:
    // Handle Deck Reader commands
    const u8 cd_reader_command = request_data[0];
    reply.command = cd_reader_command;
    reply.flag = 0;
    switch (CDReaderCommand(cd_reader_command))
    {
    case CDReaderCommand::ProgramVersion:
    {
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Program Version");
      const auto extlen = cdr_program_version.size();
      reply.length += extlen;
      std::ranges::copy(cdr_program_version, reply.extdata);
      break;
    }
    case CDReaderCommand::BootVersion:
    {
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Boot Version");
      const auto extlen = cdr_boot_version.size();
      reply.length += extlen;
      std::ranges::copy(cdr_boot_version, reply.extdata);
      break;
    }
    case CDReaderCommand::ShutterGet:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Shutter Get");
      reply.length += 4;
      reply.extdata[0] = 0;
      reply.extdata[1] = 0;
      reply.extdata[2] = 0;
      reply.extdata[3] = 0;
      break;
    case CDReaderCommand::CameraCheck:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Camera Check");
      reply.length += 6;
      reply.extdata[0] = 0x23;
      reply.extdata[1] = 0x28;
      reply.extdata[2] = 0x45;
      reply.extdata[3] = 0x29;
      reply.extdata[4] = 0x45;
      reply.extdata[5] = 0x29;
      break;
    case CDReaderCommand::ProgramChecksum:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Program Checksum");
      reply.length += 4;
      reply.extdata[0] = 0x23;
      reply.extdata[1] = 0x28;
      reply.extdata[2] = 0x45;
      reply.extdata[3] = 0x29;
      break;
    case CDReaderCommand::BootChecksum:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Boot Checksum");
      reply.length += 4;
      reply.extdata[0] = 0x23;
      reply.extdata[1] = 0x28;
      reply.extdata[2] = 0x45;
      reply.extdata[3] = 0x29;
      break;
    case CDReaderCommand::SelfTest:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Self Test");
      reply.flag = 0x00;
      break;
    case CDReaderCommand::SensLock:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Sens Lock");
      reply.flag = 0x01;
      break;
    case CDReaderCommand::SensCard:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Sens Card");
      break;
    case CDReaderCommand::ShutterCard:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Shutter Card");
      break;
    case CDReaderCommand::ReadCard:
    {
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Read Card");

      reply.fixed = 0xAA;
      reply.flag = 0xAA;

      const u16 extlen = sizeof(cdr_card_data);

      // TODO: What is the meaning of this ?
      reply.length += 0x70;

      // TODO: This is swapped back later. Is that correct ?
      reply.status = Common::swap16(extlen);

      std::ranges::copy(cdr_card_data, reply.extdata);

      break;
    }
    default:
      WARN_LOG_FMT(SERIALINTERFACE_CARD,
                   "GC-AM: Command 0x31 (IC-Card) {:02x} {:02x} {:02x} {:02x} {:02x} "
                   "{:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                   request_data[2], request_data[3], request_data[4], request_data[5],
                   request_data[6], request_data[7], request_data[8], request_data[9],
                   request_data[10], request_data[11], request_data[12], request_data[13]);
      break;
    }
    break;
  }

  reply.status = Common::swap16(reply.status);

  const u32 reply_size_without_checksum = reply.length + 4u;
  const auto reply_span = Common::AsU8Span(reply).first(reply_size_without_checksum);

  OutputBytes(reply_span);
  OutputByte(CheckSumXOR(reply_span));
}

void ICCardReader::ToggleCardState()
{
  NOTICE_LOG_FMT(SERIALINTERFACE_CARD, "ICCardReader::ToggleCardState");
  m_ic_card_status ^= ICCARDStatus::NoCard;
}

void ICCardReader::DoState(PointerWrap& p)
{
  p.Do(m_ic_card_data);

  p.Do(m_ic_card_state);
  p.Do(m_ic_card_status);
  p.Do(m_ic_card_session);
}

}  // namespace Triforce
