// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/ICCardReader.h"

#include <fmt/ranges.h>
#include <numeric>

#include "Common/BitUtils.h"
#include "Common/ChunkFile.h"
#include "Common/Logging/Log.h"
#include "Common/ScopeGuard.h"
#include "Common/Swap.h"

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
    WARN_LOG_FMT(SERIALINTERFACE_CARD, "WritePage: Unexpected Card Session: {:04x}", card_session);
  }
}

constexpr u32 READ_ONLY_PAGE_INDEX = 4;
constexpr u32 USE_COUNT_OFFSET = 0x28;

constexpr u8 CheckSumXOR(std::span<const u8> data)
{
  return std::accumulate(data.data(), data.data() + data.size(), u8{}, std::bit_xor());
}

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

enum ICCARDCommand
{
  GetStatus = 0x10,
  SetBaudrate = 0x11,
  FieldOn = 0x14,
  FieldOff = 0x15,
  Unknown_16 = 0x16,  // Gekitou sends this.
  InsertCheck = 0x20,
  AntiCollision = 0x21,
  SelectCard = 0x22,
  ReadPage = 0x24,
  WritePage = 0x25,
  DecreaseUseCount = 0x26,
  Halt = 0x27,
  ReadUseCount = 0x33,
  ReadPages = 0x34,
  WritePages = 0x35,
};

enum ICCARDStatus
{
  Okay = 0x0000,
  // SelectFirstCard = 0x0001,  // The game tests for this sometimes.
  FieldOnStart = 0x0020,
  InitializeEnd = 0x0030,

  // These seem to trigger a re-read of a Page.
  NoCard = 0x8000,
  Unknown = 0x800e,

  BadCard = 0xffff,
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

  // For reference:
  // struct RequestLayout
  // {
  //   u8 cd_reader_command;
  //   u8 ic_card_command;
  //   u16 payload_size;  // Big-endian.
  //   u8 payload[payload_size];  // Generally starts with card session.
  //   u8 checksum;
  // };

  const u16 input_payload_size = Common::swap16(input_span.data() + 2);
  // 4 header bytes + 1 checksum byte
  const u32 total_request_size = input_payload_size + 5u;

  if (input_span.size() < total_request_size)
    return;  // Wait for more data.

  const auto request_data = input_span.first(total_request_size);
  Common::ScopeGuard chew_request{[&] { ChewBytes(total_request_size); }};

  const u8 read_checksum = request_data.back();
  const u8 proper_checksum = CheckSumXOR(std::span{request_data}.first(total_request_size - 1));

  const auto input_payload = request_data.subspan(4, input_payload_size);

  if (read_checksum != proper_checksum)
  {
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Bad checksum!");
    return;
  }

  const u8 card_command = request_data[1];

  const auto check_input_payload_size = [&](u32 expected_size) {
    if (input_payload_size < expected_size)
    {
      ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Undersized payload size {} for ICCARDCommand:{:02x}",
                    input_payload_size, card_command);
      return false;
    }

    if (input_payload_size > expected_size)
    {
      WARN_LOG_FMT(SERIALINTERFACE_CARD, "Oversized payload size {} for ICCARDCommand:{:02x}",
                   input_payload_size, card_command);
    }

    return true;
  };

  ICCardReplyHeader reply_header{
      .fixed = 0x10,
      .command = card_command,
  };

  // To avoid unnecessary dynamic storage.
  // Note that many commands expect an 8-byte response even for small amounts of data.
  std::array<u8, 8> small_response_payload{};

  // Will be later assigned to `small_response_payload` or some region of the card data itself.
  std::span<const u8> response_payload_span;

  switch (ICCARDCommand(card_command))
  {
  case ICCARDCommand::GetStatus:
  {
    if (!check_input_payload_size(0))
      break;

    // TODO:
    // reply_header.status = 0x20;
    reply_header.status = 0x30;  // Skips "FIELD ON START".

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ICCARDCommand: GetStatus: {:02x}", m_ic_card_state);
    break;
  }
  case ICCARDCommand::SetBaudrate:
  {
    if (!check_input_payload_size(8))  // d44f314eff7f0000
      break;                           // 00(also saw 10 here) 04 01 00 00 00 00 00

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ICCARDCommand: SetBaudrate: {:02x}",
                 fmt::join(input_payload, " "));

    break;
  }
  case ICCARDCommand::FieldOn:
  {
    if (!check_input_payload_size(0))
      break;

    m_ic_card_state |= 0x10;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ICCARDCommand: FieldOn");
    break;
  }
  case ICCARDCommand::InsertCheck:
  {
    if (!check_input_payload_size(8))
      break;

    // Avalon sends 0 or 1 here, not sure what the meaning is.
    const u16 unknown_parameter = Common::swap16(input_payload.data() + 0);

    // TODO: I think status is whether or not a card is present.
    // Avalon does another InsertCheck when it's non-zero.
    // reply_header.status = m_ic_card_status;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "InsertCheck: {}", unknown_parameter);
    break;
  }
  case ICCARDCommand::AntiCollision:
  {
    // FYI: Requests seem to include 16 bytes of 0x00.
    // Avalon's logic optionally includes an integer and 8 memcpy'd bytes but never seems to do it.

    // Card ID
    response_payload_span = CARD_ID;

    // Avalon seems to like a value of 0x0 or 0x1. 0x1 Causes two cards to be processed.
    // It doesn't like when the second card returns 0x1 tho?
    // reply_header.status = 0x01;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "AntiCollision: {:02x}", fmt::join(input_payload, " "));
    break;
  }
  case ICCARDCommand::SelectCard:
  {
    if (!check_input_payload_size(8))
      break;

    // FYI: The request includes the Card ID that we produced in `AntiCollision`.
    if (!std::ranges::equal(input_payload, CARD_ID))
    {
      WARN_LOG_FMT(SERIALINTERFACE_CARD, "SelectCard: Unexpected Card ID: {:02x}",
                   fmt::join(input_payload, " "));
    }

    // TODO: I think a non-zero status means there are more cards.
    // reply_header.status = 0x00;

    // Session
    Common::WriteSwap16(small_response_payload.data(), IC_CARD_SESSION);

    // Avalon seems to only use 2 of the 8 expected bytes.
    response_payload_span = small_response_payload;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SelectCard: {:02x}", fmt::join(input_payload, " "));
    break;
  }
  // FYI: These two seem to have the same parameters.
  // Perhaps ReadUseCount is only meant to copy 2 bytes, but the response is still expected to be 8.
  case ICCARDCommand::ReadPage:
  case ICCARDCommand::ReadUseCount:
  {
    if (!check_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    const std::size_t page = Common::swap16(input_payload.data() + 2) & PAGE_INDEX_MASK;

    CheckCardSession(card_session);

    const auto byte_offset = page * PAGE_SIZE;

    response_payload_span = std::span{m_ic_card_data}.subspan(byte_offset, PAGE_SIZE);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ReadPage: session:{:04x}, page:{}", card_session, page);
    break;
  }
  case ICCARDCommand::WritePage:
  {
    if (!check_input_payload_size(16))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    // Avalon specifically puts a zero here.
    const u16 unknown = Common::swap16(input_payload.data() + 2);
    const std::size_t page = Common::swap16(input_payload.data() + 4) & PAGE_INDEX_MASK;

    CheckCardSession(card_session);

    if (page == READ_ONLY_PAGE_INDEX)  // Read-only page, must return error
    {
      reply_header.status = 0x80;
    }
    else
    {
      // TODO: bounds check !
      std::copy_n(input_payload.data() + 8, PAGE_SIZE, m_ic_card_data.data() + (page * PAGE_SIZE));
    }

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "WritePage: session:{:04x} unknown:{} page:{}", card_session,
                 unknown, page);
    break;
  }
  case ICCARDCommand::DecreaseUseCount:
  {
    if (!check_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    // Avalon seems to always sends 5 and 1. Guessing on the meaning and behavior.
    const u16 page = Common::swap16(input_payload.data() + 2) & PAGE_INDEX_MASK;
    const u16 amount = Common::swap16(input_payload.data() + 4);

    CheckCardSession(card_session);

    auto* const addr = m_ic_card_data.data() + (page * PAGE_SIZE);

    const u16 use_count = Common::swap16(addr);
    Common::WriteSwap16(addr, use_count - amount);

    std::copy_n(addr, sizeof(u16), small_response_payload.data());

    response_payload_span = small_response_payload;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "DecreaseUseCount: session:{:04x}, page:{} amount:{}",
                 card_session, page, amount);
    break;
  }
  case ICCARDCommand::Halt:
  {
    if (!check_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);

    CheckCardSession(card_session);

    // TODO: I think this is supposed to make a particular card stop responding
    // to remove it from anti-collision cycles.

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "Halt: session:{:04x}", card_session);
    break;
  }
  case ICCARDCommand::ReadPages:
  {
    if (!check_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    const u16 page = Common::swap16(input_payload.data() + 2) & PAGE_INDEX_MASK;
    const u16 page_count = Common::swap16(input_payload.data() + 4);

    CheckCardSession(card_session);

    const u32 byte_offset = page * PAGE_SIZE;
    const u32 byte_count = page_count * PAGE_SIZE;

    // TODO: Check bounds !
    response_payload_span = std::span{m_ic_card_data}.subspan(byte_offset, byte_count);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ReadPages session:{:04x} page:{} page_count:{}",
                 card_session, page, page_count);
    break;
  }
  case ICCARDCommand::WritePages:
  {
    if (!check_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    const u32 page = Common::swap16(input_payload.data() + 2) & PAGE_INDEX_MASK;
    const u32 page_count = Common::swap16(input_payload.data() + 4);

    CheckCardSession(card_session);

    const u32 write_offset = page * PAGE_SIZE;
    const u32 write_size = page_count * PAGE_SIZE;

    // TODO: This should probably test for any write that touches page 4.
    if (page == READ_ONLY_PAGE_INDEX)  // Read-only page, must return error
    {
      reply_header.status = 0x80;
    }
    else
    {
      if (write_size + write_offset > sizeof(m_ic_card_data))
      {
        // TODO: better error.
        WARN_LOG_FMT(SERIALINTERFACE_CARD, "WritePages session:{:04x} page:{} page_count:{}",
                     card_session, page, page_count);
      }
      else
      {
        std::copy_n(input_payload.data() + 8, write_size, m_ic_card_data.data() + write_offset);
      }
    }

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "WritePages session:{:04x} page:{} page_count:{}",
                 card_session, page, page_count);

    break;
  }
  default:
    // Handle Deck Reader commands.
    const u8 cd_reader_command = request_data[0];
    reply_header.command = cd_reader_command;

    // TODO:
    // reply_header.flag = 0;
    m_deck_reader.Process(cd_reader_command);
  }

  reply_header.status = Common::swap16(reply_header.status);
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
  // m_ic_card_status ^= ICCARDStatus::NoCard;
}

void ICCardReader::DoState(PointerWrap& p)
{
  p.Do(m_ic_card_data);

  p.Do(m_ic_card_state);
}

}  // namespace Triforce
