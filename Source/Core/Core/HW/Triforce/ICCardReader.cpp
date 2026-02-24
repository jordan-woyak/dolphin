
// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/ICCardReader.h"

#include <numeric>

#include "Common/BitUtils.h"
#include "Common/ChunkFile.h"
#include "Common/Logging/Log.h"
#include "Common/ScopeGuard.h"
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

constexpr u32 READ_ONLY_PAGE_INDEX = 4;
constexpr u32 USE_COUNT_OFFSET = 0x28;

}  // namespace

namespace Triforce
{

struct ICCardReplyHeader
{
  u8 fixed;  // Games seem to usually expect 0x10.
  u8 command;
  u16 length;  // Big-endian, includes status and all remaining bytes.
  u16 status;  // Big-endian.
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
  // FYI: This data is in the READ_ONLY_PAGE_INDEX area.

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

  // Use count.
  // A big-endian count down from 0xffff.
  m_ic_card_data[USE_COUNT_OFFSET + 0] = 0xff;
  m_ic_card_data[USE_COUNT_OFFSET + 1] = 0xff;
}

void ICCardReader::Process()
{
  const auto input_span = GetInputSpan();
  if (input_span.size() < 4)
    return;  // Wait for more data.

  const u16 input_payload_size = Common::swap16(input_span.data() + 2);
  // 4 header bytes + 1 checksum byte
  const u32 total_request_size = input_payload_size + 5u;

  if (input_span.size() < total_request_size)
    return;  // Wait for more data.

  const auto request_data = input_span.first(total_request_size);
  Common::ScopeGuard chew_request{[&] { ChewBytes(total_request_size); }};

  const u8 read_checksum = request_data.back();
  const u8 proper_checksum = CheckSumXOR(std::span{request_data}.first(total_request_size - 1));

  if (read_checksum != proper_checksum)
  {
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Bad checksum!");
    return;
  }

  const u8 card_command = request_data[1];

  ICCardReplyHeader reply_header{
      .fixed = 0x10,
      .command = card_command,
  };

  // To avoid unnecessary dynamic storage.
  std::array<u8, 8> small_payload{};

  // Will be later assigned to part of `small_response_payload` or the card data itself.
  std::span<const u8> response_payload_span;

  switch (ICCARDCommand(card_command))
  {
  case ICCARDCommand::GetStatus:
    // TODO:
    // reply_header.status = m_ic_card_state;

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
    reply_header.status = m_ic_card_status;
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Insert Check:{:02x}",
                 m_ic_card_status);
    break;
  case ICCARDCommand::AntiCollision:

    // Card ID
    small_payload[0] = 0x00;
    small_payload[1] = 0x00;
    small_payload[2] = 0x54;
    small_payload[3] = 0x4D;
    small_payload[4] = 0x50;
    small_payload[5] = 0x00;
    small_payload[6] = 0x00;
    small_payload[7] = 0x00;

    response_payload_span = small_payload;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Anti Collision");
    break;
  case ICCARDCommand::SelectCard:

    // Session
    small_payload[0] = 0x00;
    small_payload[1] = m_ic_card_session;
    small_payload[2] = 0x00;
    small_payload[3] = 0x00;
    small_payload[4] = 0x00;
    small_payload[5] = 0x00;
    small_payload[6] = 0x00;
    small_payload[7] = 0x00;

    response_payload_span = small_payload;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Select Card:{}",
                 m_ic_card_session);
    break;
  case ICCARDCommand::ReadPage:
  case ICCARDCommand::ReadUseCount:
  {
    // TODO: Is this sane for ReadUseCount ?
    const std::size_t page = Common::swap16(request_data.data() + 6) & PAGE_INDEX_MASK;
    const auto byte_offset = page * PAGE_SIZE;

    response_payload_span = std::span{m_ic_card_data}.subspan(byte_offset, PAGE_SIZE);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 31 (IC-CARD) Read Page:{}", page);
    break;
  }
  case ICCARDCommand::WritePage:
  {
    const std::size_t page = Common::swap16(request_data.data() + 8) & PAGE_INDEX_MASK;

    if (page == READ_ONLY_PAGE_INDEX)  // Read Only Page, must return error
    {
      reply_header.status = 0x80;
    }
    else
    {
      std::copy_n(request_data.data() + 10, 8, m_ic_card_data.data() + (page * PAGE_SIZE));
    }

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Write Page:{}", page);
    break;
  }
  case ICCARDCommand::DecreaseUseCount:
  {
    const u16 page = Common::swap16(request_data.data() + 6) & PAGE_INDEX_MASK;

    auto ic_card_data = Common::BitCastPtr<u16>(m_ic_card_data.data() + USE_COUNT_OFFSET);
    ic_card_data = ic_card_data - 1;

    // TODO: I think the expected response length is 10.

    // Counter
    small_payload[0] = m_ic_card_data[USE_COUNT_OFFSET + 0];
    small_payload[1] = m_ic_card_data[USE_COUNT_OFFSET + 1];

    response_payload_span = std::span{small_payload}.first(2);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 31 (IC-CARD) Decrease Use Count:{}", page);
    break;
  }
  case ICCARDCommand::ReadPages:
  {
    const u16 page = Common::swap16(request_data.data() + 6) & PAGE_INDEX_MASK;
    const u16 count = Common::swap16(request_data.data() + 8);

    const u32 byte_offset = page * PAGE_SIZE;
    u32 byte_count = count * PAGE_SIZE;

    // TODO: Check bounds !
    response_payload_span = std::span{m_ic_card_data}.subspan(byte_offset, byte_count);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 31 (IC-CARD) Read Pages:{} Count:{}", page,
                 count);
    break;
  }
  case ICCARDCommand::WritePages:
  {
    const u32 page = Common::swap16(request_data.data() + 6) & PAGE_INDEX_MASK;
    const u32 count = Common::swap16(request_data.data() + 8);
    const u32 write_size = count * PAGE_SIZE;
    const u32 write_offset = page * PAGE_SIZE;

    // TODO: This should probably test for any write that touches page 4.
    if (page == READ_ONLY_PAGE_INDEX)  // Read Only Page, must return error
    {
      reply_header.status = 0x80;
    }
    else
    {
      if (write_size + write_offset > sizeof(m_ic_card_data))
      {
        // TODO: better error.
        ERROR_LOG_FMT(SERIALINTERFACE_CARD,
                      "GC-AM: Command 0x31 (IC-CARD) Data overflow: Pages:{} Count:{} ({})", page,
                      count, input_payload_size);
      }
      else
      {
        std::copy_n(request_data.data() + 13, write_size, m_ic_card_data.data() + write_offset);
      }
    }

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Write Pages:{} Count:{} ({})",
                 page, count, input_payload_size);

    break;
  }
  default:
    // Handle Deck Reader commands
    const u8 cd_reader_command = request_data[0];
    reply_header.command = cd_reader_command;

    // TODO:
    // reply_header.flag = 0;

    switch (CDReaderCommand(cd_reader_command))
    {
    case CDReaderCommand::ProgramVersion:
    {
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Program Version");
      response_payload_span = Common::AsU8Span(cdr_program_version);
      break;
    }
    case CDReaderCommand::BootVersion:
    {
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Boot Version");
      response_payload_span = Common::AsU8Span(cdr_boot_version);
      break;
    }
    case CDReaderCommand::ShutterGet:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Shutter Get");

      small_payload[0] = 0;
      small_payload[1] = 0;
      small_payload[2] = 0;
      small_payload[3] = 0;

      response_payload_span = std::span{small_payload}.first(4);

      break;
    case CDReaderCommand::CameraCheck:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Camera Check");

      small_payload[0] = 0x23;
      small_payload[1] = 0x28;
      small_payload[2] = 0x45;
      small_payload[3] = 0x29;
      small_payload[4] = 0x45;
      small_payload[5] = 0x29;

      response_payload_span = std::span{small_payload}.first(6);

      break;
    case CDReaderCommand::ProgramChecksum:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Program Checksum");

      small_payload[0] = 0x23;
      small_payload[1] = 0x28;
      small_payload[2] = 0x45;
      small_payload[3] = 0x29;

      response_payload_span = std::span{small_payload}.first(4);

      break;
    case CDReaderCommand::BootChecksum:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Boot Checksum");

      small_payload[0] = 0x23;
      small_payload[1] = 0x28;
      small_payload[2] = 0x45;
      small_payload[3] = 0x29;

      response_payload_span = std::span{small_payload}.first(4);

      break;
    case CDReaderCommand::SelfTest:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Self Test");

      // TODO:
      // reply_header.flag = 0x00;
      break;
    case CDReaderCommand::SensLock:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Sens Lock");
      // TODO:
      // reply_header.flag = 0x01;
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

      reply_header.fixed = 0xAA;
      // TODO:
      // reply_header.flag = 0xAA;

      response_payload_span = cdr_card_data;

      break;
    }
    default:
      ERROR_LOG_FMT(SERIALINTERFACE_CARD,
                    "ICCardReader: Unknown CDReaderCommand command {:02x} request: {}",
                    cd_reader_command, HexDump(request_data));
      break;
    }
    break;
  }

  reply_header.length = Common::swap16(sizeof(reply_header.status) + response_payload_span.size());

  // TODO:
  // reply_header.status = 0;

  const auto header_span = Common::AsU8Span(reply_header);

  const u8 checksum = CheckSumXOR(header_span) ^ CheckSumXOR(response_payload_span);

  OutputBytes(header_span);
  OutputBytes(response_payload_span);
  OutputByte(checksum);
}

void ICCardReader::ToggleCardState()
{
  NOTICE_LOG_FMT(SERIALINTERFACE_CARD, "ICCardReader::ToggleCardState");

  // TODO:
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
