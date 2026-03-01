// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/ICCardReader.h"

#include <numeric>

#include <fmt/ranges.h>

#include "Common/BitUtils.h"
#include "Common/ChunkFile.h"
#include "Common/DirectIOFile.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/ScopeGuard.h"
#include "Common/Swap.h"

#include "Core/ConfigManager.h"

#include "Core/HW/DVD/AMMediaboard.h"

namespace
{

constexpr u8 CARD_ID[8] = {0x00, 0x00, 0x54, 0x4D, 0x50, 0x00, 0x00, 0x00};

// This constant is just established to show how the value is used by the game.
// It may have to be mutable in the future to handle mutiple cards.
constexpr u16 IC_CARD_SESSION = 0x2300;

void CheckCardSession(u16 card_session)
{
  if (card_session != IC_CARD_SESSION)
  {
    WARN_LOG_FMT(SERIALINTERFACE_CARD, "Unexpected card session: {:04x}", card_session);
  }
}

constexpr u8 CheckSumXOR(std::span<const u8> data)
{
  return std::accumulate(data.data(), data.data() + data.size(), u8{}, std::bit_xor());
}

constexpr u32 PAGE_INDEX_MASK = 0xff;
constexpr u32 READ_ONLY_PAGE_INDEX = 4;
constexpr u32 USE_COUNT_OFFSET = 0x28;

// TODO: Think about how to deal with multiple cards.
auto GetCardFilename()
{
  return fmt::format("{}tricard_{}.bin", File::GetUserPath(D_TRIUSER_IDX),
                     SConfig::GetInstance().GetGameID());
}

}  // namespace

namespace Triforce
{

// TODO: I'm not so sure that this is really a device status.
// The values that Avalon seems to test for seem very command-specific.
// It might be more like a command result code.
enum ICCardStatus
{
  Okay = 0x0000,
  NoCard = 0x8000,
  Unknown = 0x800e,
  BadCard = 0xffff,
};

enum class ICCardCommand : u8
{
  GetStatus = 0x10,
  SetBaudrate = 0x11,
  FieldOn = 0x14,
  FieldOff = 0x15,
  Unknown_16 = 0x16,  // Gekitou sends this on boot.
  InsertCheck = 0x20,
  AntiCollision = 0x21,
  SelectCard = 0x22,
  ReadPage = 0x24,
  WritePage = 0x25,
  DecreaseUseCount = 0x26,
  HaltCard = 0x27,
  ReadUseCount = 0x33,
  ReadPages = 0x34,
  WritePages = 0x35,
};

ICCardReader::ICCardReader()
{
  if (!LoadCardData())
  {
    NOTICE_LOG_FMT(SERIALINTERFACE_CARD, "Creating new IC Card data.");
    InitialzeDefaultCardData();
  }
}

bool ICCardReader::LoadCardData()
{
  const auto filename = GetCardFilename();
  File::DirectIOFile file{filename, File::AccessMode::Read};

  if (!File::Exists(filename))
    return false;

  const auto file_size = file.GetSize();
  if (file_size > m_ic_card_data.size())
  {
    WARN_LOG_FMT(SERIALINTERFACE_CARD, "Unexpected size of {} for file: {}", file_size, filename);
  }

  const auto read_size = std::min(file_size, m_ic_card_data.size());

  if (!file.Read(std::span{m_ic_card_data}.first(read_size)))
  {
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Failed to read from file: {}", filename);
    return false;
  }

  INFO_LOG_FMT(SERIALINTERFACE_CARD, "Loaded {} bytes from file: {}", read_size, filename);
  return true;
}

void ICCardReader::InitialzeDefaultCardData()
{
  // Card ID
  // TODO: Needs to be unique for multiple cards ?
  m_ic_card_data[0x20] = 0x95;
  m_ic_card_data[0x21] = 0x71;

  // TODO: Does Gekitou need anything ?

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

  // The use count is a big endian count down.
  Common::WriteSwap16(m_ic_card_data.data() + USE_COUNT_OFFSET, 0xffff);

  FlushCardData(READ_ONLY_PAGE_INDEX * PAGE_SIZE, PAGE_SIZE * 2);
}

void ICCardReader::Process()
{
  const auto input_span = GetInputSpan();
  if (input_span.empty())
    return;  // Wait for more data.

  if (input_span.size() < 4)
    return;  // Wait for more data.

// For reference:
#if 0
  struct RequestPacket
  {
    u8 fixed;   // Seems to be always 0x00.
    u8 ic_card_command;
    u16 payload_size;  // Big-endian.
    u8 payload[payload_size];
    u8 checksum;  // XOR of all previous bytes.
  };
#endif

  const u16 input_payload_size = Common::swap16(input_span.data() + 2);
  // 4 header bytes + 1 checksum byte
  const u32 entire_request_size = input_payload_size + 5u;

  if (input_span.size() < entire_request_size)
    return;  // Wait for more data.

  const auto entire_request = input_span.first(entire_request_size);

  const u8 read_checksum = entire_request.back();
  const u8 proper_checksum = CheckSumXOR(std::span{entire_request}.first(entire_request_size - 1));

  const auto input_payload = entire_request.subspan(4, input_payload_size);

  if (read_checksum != proper_checksum)
  {
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Bad checksum!");
    ChewBytes(1);
    return;
  }

  Common::ScopeGuard chew_request{[&] { ChewBytes(entire_request_size); }};

  // Might as well check this.
  if (entire_request.front() != 0x00)
  {
    WARN_LOG_FMT(SERIALINTERFACE_CARD, "Unexpected non-zero first byte: {:02x}",
                 entire_request.front());
  }

  const u8 card_command = entire_request[1];

  const auto validate_input_payload_size = [&](u32 expected_size) {
    if (input_payload_size < expected_size)
    {
      ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Undersized payload size {} for command: {:02x}",
                    input_payload_size, card_command);
      return false;
    }

    if (input_payload_size > expected_size)
    {
      WARN_LOG_FMT(SERIALINTERFACE_CARD, "Oversized payload size {} for command: {:02x}",
                   input_payload_size, card_command);
    }

    return true;
  };

  u16 status_code = 0x00;

  // Note: Commands expect full 8-byte responses even for small amounts of data.
  std::array<u8, 8> small_reply_payload{};

  // Will be later assigned to `small_reply_payload` or some region of the card data itself.
  std::span<const u8> reply_payload_span;

  // FYI: Many of the big-endian u16 parameters may just be u8 values.
  // Avalon writes single bytes, but at odd addresses, so u16 seems like the intention.

  switch (ICCardCommand(card_command))
  {
  case ICCardCommand::GetStatus:
  {
    if (!validate_input_payload_size(0))
      break;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GetStatus");

    status_code = m_is_field_on ? 0x30 : 0x20;

    break;
  }
  case ICCardCommand::SetBaudrate:
  {
    if (!validate_input_payload_size(8))
      break;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SetBaudrate: {:02x}", fmt::join(input_payload, " "));
    break;
  }
  case ICCardCommand::FieldOn:
  {
    if (!validate_input_payload_size(0))
      break;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "FieldOn");

    m_is_field_on = true;

    break;
  }
  case ICCardCommand::FieldOff:
  {
    if (!validate_input_payload_size(0))
      break;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "FieldOff");

    m_is_field_on = false;

    break;
  }
  case ICCardCommand::Unknown_16:
  {
    if (!validate_input_payload_size(0))
      break;

    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Unknown_16: Not implemented.");
    break;
  }
  case ICCardCommand::InsertCheck:
  {
    if (!validate_input_payload_size(8))
      break;

    // Avalon sends 0 or 1 here, not sure what the meaning is.
    const u16 unknown_parameter = Common::swap16(input_payload.data() + 0);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "InsertCheck: {}", unknown_parameter);

    // TODO: I think status is whether or not a card is present.
    // Avalon does another InsertCheck when it's non-zero.
    status_code = 0x00;

    break;
  }
  case ICCardCommand::AntiCollision:
  {
    if (!validate_input_payload_size(16))
      break;

    // Avalon's logic optionally sets param0=0x20 and 8 memcpy'd bytes but never seems to do it.
    const u16 unknown_param0 = Common::swap16(input_payload.data() + 0);
    const auto unknown_param1 = input_payload.subspan(2, 8);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "AntiCollision: {:04x} {:02x}", unknown_param0,
                 fmt::join(unknown_param1, " "));

    // Card ID
    reply_payload_span = CARD_ID;

    // TODO:
    // Avalon seems to like a value of 0x0 or 0x1. 0x1 Causes two cards to be processed.
    status_code = 0x01;

    break;
  }
  case ICCardCommand::SelectCard:
  {
    if (!validate_input_payload_size(8))
      break;

    const auto card_id = input_payload.first(8);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SelectCard: {:02x}", fmt::join(card_id, " "));

    // FYI: The request includes the Card ID that we produced in `AntiCollision`.
    if (!std::ranges::equal(card_id, CARD_ID))
    {
      WARN_LOG_FMT(SERIALINTERFACE_CARD, "SelectCard: Unexpected Card ID.");
    }

    // TODO: I think a non-zero status means there are multiple cards ?
    status_code = 0x00;

    // Session
    Common::WriteSwap16(small_reply_payload.data(), IC_CARD_SESSION);

    reply_payload_span = small_reply_payload;

    break;
  }
  // FYI: These two seem to have the same parameters.
  // Maybe ReadUseCount is meant to copy just 2 bytes? The response is still expected to be 8.
  case ICCardCommand::ReadPage:
  case ICCardCommand::ReadUseCount:
  {
    if (!validate_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    const u16 page = Common::swap16(input_payload.data() + 2) & PAGE_INDEX_MASK;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ReadPage: session:{:04x} page:{}", card_session, page);

    CheckCardSession(card_session);

    const auto byte_offset = page * PAGE_SIZE;

    reply_payload_span = std::span{m_ic_card_data}.subspan(byte_offset, PAGE_SIZE);

    DEBUG_LOG_FMT(SERIALINTERFACE_CARD, "\n{}", HexDump(reply_payload_span));

    break;
  }
  case ICCardCommand::WritePage:
  {
    if (!validate_input_payload_size(16))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    // Avalon specifically puts a zero here.
    const u16 unknown = Common::swap16(input_payload.data() + 2);
    const u16 page = Common::swap16(input_payload.data() + 4) & PAGE_INDEX_MASK;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "WritePage: session:{:04x} unknown:{} page:{}", card_session,
                 unknown, page);

    CheckCardSession(card_session);

    const u32 byte_offset = page * PAGE_SIZE;
    const u32 byte_count = PAGE_SIZE;

    if (!WriteCardData(byte_offset, input_payload.subspan(8, byte_count)))
    {
      // TODO: Is this correct ?
      status_code = 0x80;
    }

    break;
  }
  case ICCardCommand::DecreaseUseCount:
  {
    if (!validate_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    // Avalon seems to always sends 5 and 1. Guessing on the meaning and behavior.
    const u16 page = Common::swap16(input_payload.data() + 2) & PAGE_INDEX_MASK;
    const u16 amount = Common::swap16(input_payload.data() + 4);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "DecreaseUseCount: session:{:04x}, page:{} amount:{}",
                 card_session, page, amount);

    CheckCardSession(card_session);

    const u32 byte_offset = page * PAGE_SIZE;
    auto* const addr = m_ic_card_data.data() + byte_offset;

    const u16 previous_use_count = Common::swap16(addr);
    const u16 new_use_count = previous_use_count - amount;

    NOTICE_LOG_FMT(SERIALINTERFACE_CARD, "DecreaseUseCount: {} -> {}", previous_use_count,
                   new_use_count);

    Common::WriteSwap16(addr, new_use_count);

    FlushCardData(byte_offset, sizeof(u16));

    std::copy_n(addr, sizeof(u16), small_reply_payload.data());

    reply_payload_span = small_reply_payload;

    break;
  }
  case ICCardCommand::HaltCard:
  {
    if (!validate_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);

    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "HaltCard (Not implemented): session:{:04x}", card_session);

    CheckCardSession(card_session);

    // TODO: I think this is supposed to make a particular card stop responding,
    //  to remove it from responses in AntiCollision.

    break;
  }
  case ICCardCommand::ReadPages:
  {
    if (!validate_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    const u16 page = Common::swap16(input_payload.data() + 2) & PAGE_INDEX_MASK;
    const u16 page_count = Common::swap16(input_payload.data() + 4);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ReadPages: session:{:04x} page:{} page_count:{}",
                 card_session, page, page_count);

    CheckCardSession(card_session);

    const u32 byte_offset = page * PAGE_SIZE;
    const u32 byte_count = page_count * PAGE_SIZE;

    if (byte_count + byte_offset > m_ic_card_data.size())
    {
      WARN_LOG_FMT(SERIALINTERFACE_CARD, "ReadPages: Attempt to read beyond end of card.");
      // TODO: Is this correct ?
      status_code = 0x80;
    }
    else
    {
      reply_payload_span = std::span{m_ic_card_data}.subspan(byte_offset, byte_count);

      DEBUG_LOG_FMT(SERIALINTERFACE_CARD, "\n{}", HexDump(reply_payload_span));
    }

    break;
  }
  case ICCardCommand::WritePages:
  {
    if (input_payload_size < 8)
    {
      ERROR_LOG_FMT(SERIALINTERFACE_CARD, "WritePages: Undersized payload size: {}",
                    input_payload_size);
      break;
    }

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    const u32 page = Common::swap16(input_payload.data() + 2) & PAGE_INDEX_MASK;
    const u32 page_count = Common::swap16(input_payload.data() + 4);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "WritePages: session:{:04x} page:{} page_count:{}",
                 card_session, page, page_count);

    CheckCardSession(card_session);

    const u32 byte_offset = page * PAGE_SIZE;
    const u32 byte_count = page_count * PAGE_SIZE;

    if (!validate_input_payload_size(8 + byte_count))
      break;

    if (!WriteCardData(byte_offset, input_payload.subspan(8, byte_count)))
    {
      // TODO: Is this correct ?
      status_code = 0x80;
    }

    break;
  }
  default:
  {
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Unknown command: {:02x}", card_command);
    break;
  }
  }

  SendReply(card_command, status_code, reply_payload_span);
}

void ICCardReader::SendReply(u8 command, u16 status_code, std::span<const u8> payload)
{
  struct ICCardReplyHeader
  {
    u8 fixed;  // Games seem to usually expect 0x10.
    u8 command;
    Common::BigEndianValue<u16> length;  // Includes status and payload bytes.
    Common::BigEndianValue<u16> status;
  };

  ICCardReplyHeader header{
      .fixed = 0x10,
      .command = command,
  };

  header.length = sizeof(header.status) + payload.size();
  header.status = status_code;

  const auto header_span = Common::AsU8Span(header);

  const u8 checksum = CheckSumXOR(header_span) ^ CheckSumXOR(payload);

  OutputBytes(header_span);
  OutputBytes(payload);
  OutputByte(checksum);
}

bool ICCardReader::WriteCardData(u32 byte_offset, std::span<const u8> write_span)
{
  constexpr u32 read_only_area_begin = READ_ONLY_PAGE_INDEX * PAGE_SIZE;
  constexpr u32 read_only_area_end = read_only_area_begin + PAGE_SIZE;

  if ((byte_offset < read_only_area_end) &&
      (byte_offset + write_span.size() > read_only_area_begin))
  {
    WARN_LOG_FMT(SERIALINTERFACE_CARD, "WriteCardData: Read-only page.");
    // Read-only page, must return error.
    return false;
  }

  if (write_span.size() + byte_offset > m_ic_card_data.size())
  {
    WARN_LOG_FMT(SERIALINTERFACE_CARD, "WriteCardData: Attempt to write beyond end of card.");
    return false;
  }

  std::ranges::copy(write_span, m_ic_card_data.data() + byte_offset);

  DEBUG_LOG_FMT(SERIALINTERFACE_CARD, "\n{}", HexDump(write_span));

  FlushCardData(byte_offset, u32(write_span.size()));

  return true;
}

// TODO: Maybe in the future we should write to disk after a delay.
// Games seem to write many chunks when saving.
void ICCardReader::FlushCardData(u32 byte_offset, u32 byte_count)
{
  const auto filename = GetCardFilename();

  File::DirectIOFile file{filename, File::AccessMode::Write, File::OpenMode::Always};
  if (!file.OffsetWrite(byte_offset, std::span{m_ic_card_data}.subspan(byte_offset, byte_count)))
  {
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "FlushCardData: Failed to write to: {}", filename);
  }
}

void ICCardReader::ToggleCardState()
{
  INFO_LOG_FMT(SERIALINTERFACE_CARD, "ToggleCardState");

  // TODO:
  // m_ic_card_status ^= ICCARDStatus::NoCard;
}

void ICCardReader::DoState(PointerWrap& p)
{
  // TODO: Think about what to do with the card data on the filesystem.

  p.Do(m_ic_card_data);
}

}  // namespace Triforce
