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
    WARN_LOG_FMT(SERIALINTERFACE_CARD, "Unexpected card session: {:04x}", card_session);
  }
}

constexpr u32 READ_ONLY_PAGE_INDEX = 4;
constexpr u32 USE_COUNT_OFFSET = 0x28;

}  // namespace

namespace Triforce
{

// FYI: I'm not so sure that the `status` field is actually a universal status.
// The tested values seem to be very command-specific.
enum ICCARDStatus
{
  Okay = 0x0000,
  FieldOnStart = 0x0020,
  InitializeEnd = 0x0030,
  // These seem to trigger a re-read of a Page.
  NoCard = 0x8000,
  Unknown = 0x800e,
  BadCard = 0xffff,
};

enum ICCARDCommand
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
  // FYI: This data is in the READ_ONLY_PAGE_INDEX area.

  // TODO: Probably needs to be unique for multiple cards.
  // Card ID
  m_ic_card_data[0x20] = 0x95;
  m_ic_card_data[0x21] = 0x71;

  // TODO: Get this game specific stuff out of here.
  // TODO: Gekitou need anything ?

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
}

void ICCardReader::Process()
{
  // TODO: Put this whole thing in a loop ?

  const auto input_span = GetInputSpan();
  if (input_span.empty())
    return;  // Wait for more data.

  // Handle Deck Reader commands.
  const u8 cd_reader_command = input_span.front();
  if (cd_reader_command != 0x00)
  {
    m_deck_reader.Process(cd_reader_command, std::bind_front(&ICCardReader::OutputBytes, this));

    // TODO: I think the deck reader commands are variable length.
    // This potentially needs to chew more bytes. Re: FirmwareUpdate
    ChewBytes(1);
    return;
  }

  if (input_span.size() < 4)
    return;  // Wait for more data.

  // For reference:
  // struct RequestLayout
  // {
  //   u8 cd_reader_command;
  //   u8 ic_card_command;
  //   u16 payload_size;  // Big-endian.
  //   u8 payload[payload_size];
  //   u8 checksum;
  // };

  const u16 input_payload_size = Common::swap16(input_span.data() + 2);
  // 4 header bytes + 1 checksum byte
  const u32 total_request_size = input_payload_size + 5u;

  if (input_span.size() < total_request_size)
    return;  // Wait for more data.

  const auto request_data = input_span.first(total_request_size);

  const u8 read_checksum = request_data.back();
  const u8 proper_checksum = CheckSumXOR(std::span{request_data}.first(total_request_size - 1));

  const auto input_payload = request_data.subspan(4, input_payload_size);

  if (read_checksum != proper_checksum)
  {
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Bad checksum!");
    ChewBytes(1);
    return;
  }

  Common::ScopeGuard chew_request{[&] { ChewBytes(total_request_size); }};

  const u8 card_command = request_data[1];

  const auto validate_input_payload_size = [&](u32 expected_size) {
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
  // Note: Commands seem to expect full 8-byte responses even for small amounts of data.
  std::array<u8, PAGE_SIZE> small_response_payload{};

  // Will be later assigned to `small_response_payload` or some region of the card data itself.
  std::span<const u8> response_payload_span;

  // FYI: Many of the big-endian u16 parameters are potentially just u8 values.
  // Avalon writes just single bytes, but there's room for a u16..

  switch (ICCARDCommand(card_command))
  {
  case ICCARDCommand::GetStatus:
  {
    if (!validate_input_payload_size(0))
      break;

    // TODO:
    // reply_header.status = 0x20;
    reply_header.status = 0x30;  // Skips "FIELD ON START".

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ICCARDCommand: GetStatus");
    break;
  }
  case ICCARDCommand::SetBaudrate:
  {
    if (!validate_input_payload_size(8))
      break;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ICCARDCommand: SetBaudrate: {:02x}",
                 fmt::join(input_payload, " "));
    break;
  }
  case ICCARDCommand::FieldOn:
  {
    if (!validate_input_payload_size(0))
      break;

    // TODO:
    // m_ic_card_state |= 0x10;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ICCARDCommand: FieldOn");
    break;
  }
  case ICCARDCommand::FieldOff:
  {
    if (!validate_input_payload_size(0))
      break;

    // TODO:
    // m_ic_card_state &= ~0x10;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ICCARDCommand: FieldOff");
    break;
  }
  case ICCARDCommand::Unknown_16:
  {
    if (!validate_input_payload_size(0))
      break;

    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Unknown_16 (NOT IMPLEMENTED)");
    break;
  }
  case ICCARDCommand::InsertCheck:
  {
    if (!validate_input_payload_size(8))
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
    if (!validate_input_payload_size(16))
      break;

    // Avalon's logic optionally sets param0=0x20 and 8 memcpy'd bytes but never seems to do it.
    const u16 unknown_param0 = Common::swap16(input_payload.data() + 0);
    const auto unknown_param1 = input_payload.subspan(2, 8);

    // Card ID
    response_payload_span = CARD_ID;

    // TODO:
    // Avalon seems to like a value of 0x0 or 0x1. 0x1 Causes two cards to be processed.
    // reply_header.status = 0x01;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "AntiCollision: {:04x} {:02x}", unknown_param0,
                 fmt::join(unknown_param1, " "));
    break;
  }
  case ICCARDCommand::SelectCard:
  {
    if (!validate_input_payload_size(8))
      break;

    const auto card_id = input_payload.first(8);

    // FYI: The request includes the Card ID that we produced in `AntiCollision`.
    if (!std::ranges::equal(card_id, CARD_ID))
    {
      WARN_LOG_FMT(SERIALINTERFACE_CARD, "SelectCard: Unexpected Card ID: {:02x}",
                   fmt::join(card_id, " "));
    }

    // TODO: I think a non-zero status means there are multiple cards ?
    // reply_header.status = 0x00;

    // Session
    Common::WriteSwap16(small_response_payload.data(), IC_CARD_SESSION);

    response_payload_span = small_response_payload;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SelectCard: {:02x}", fmt::join(card_id, " "));
    break;
  }
  // FYI: These two seem to have the same parameters.
  // Maybe ReadUseCount is meant to copy just 2 bytes? The response is still expected to be 8.
  case ICCARDCommand::ReadPage:
  case ICCARDCommand::ReadUseCount:
  {
    if (!validate_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    const u16 page = Common::swap16(input_payload.data() + 2) & PAGE_INDEX_MASK;

    CheckCardSession(card_session);

    const auto byte_offset = page * PAGE_SIZE;

    response_payload_span = std::span{m_ic_card_data}.subspan(byte_offset, PAGE_SIZE);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ReadPage: session:{:04x} page:{}", card_session, page);
    break;
  }
  case ICCARDCommand::WritePage:
  {
    if (!validate_input_payload_size(16))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    // Avalon specifically puts a zero here.
    const u16 unknown = Common::swap16(input_payload.data() + 2);
    const u16 page = Common::swap16(input_payload.data() + 4) & PAGE_INDEX_MASK;

    CheckCardSession(card_session);

    if (page == READ_ONLY_PAGE_INDEX)  // Read-only page, must return error
    {
      reply_header.status = 0x80;
    }
    else
    {
      std::ranges::copy(input_payload.subspan(8, PAGE_SIZE),
                        m_ic_card_data.data() + (page * PAGE_SIZE));
    }

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "WritePage: session:{:04x} unknown:{} page:{}", card_session,
                 unknown, page);
    break;
  }
  case ICCARDCommand::DecreaseUseCount:
  {
    if (!validate_input_payload_size(8))
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
  case ICCARDCommand::HaltCard:
  {
    if (!validate_input_payload_size(8))
      break;

    const u16 card_session = Common::swap16(input_payload.data() + 0);

    CheckCardSession(card_session);

    // TODO: I think this is supposed to make a particular card stop responding,
    //  to remove it from responses in AntiCollision.

    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Halt (NOT IMPLEMENTED): session:{:04x}", card_session);
    break;
  }
  case ICCARDCommand::ReadPages:
  {
    if (!validate_input_payload_size(8))
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
    if (input_payload_size < 8)
    {
      ERROR_LOG_FMT(SERIALINTERFACE_CARD, "WritePages: Undersized payload size: {}",
                    input_payload_size);
      break;
    }

    const u16 card_session = Common::swap16(input_payload.data() + 0);
    const u32 page = Common::swap16(input_payload.data() + 2) & PAGE_INDEX_MASK;
    const u32 page_count = Common::swap16(input_payload.data() + 4);

    CheckCardSession(card_session);

    const u32 byte_offset = page * PAGE_SIZE;
    const u32 byte_count = page_count * PAGE_SIZE;

    if (!validate_input_payload_size(8 + byte_count))
      break;

    // TODO: This should probably test for any write that touches page 4 ?
    if (page == READ_ONLY_PAGE_INDEX)
    {
      // Read-only page, must return error
      reply_header.status = 0x80;
    }
    else
    {
      if (byte_count + byte_offset > sizeof(m_ic_card_data))
      {
        // TODO: better error.
        WARN_LOG_FMT(SERIALINTERFACE_CARD, "WritePages session:{:04x} page:{} page_count:{}",
                     card_session, page, page_count);
      }
      else
      {
        std::ranges::copy(input_payload.subspan(8, byte_count),
                          m_ic_card_data.data() + byte_offset);
      }
    }

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "WritePages session:{:04x} page:{} page_count:{}",
                 card_session, page, page_count);
    break;
  }
  default:
  {
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Unknown ICCARDCommand: {:02x}", card_command);
    break;
  }
  }

  reply_header.status = Common::swap16(reply_header.status);
  reply_header.length = Common::swap16(sizeof(reply_header.status) + response_payload_span.size());

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

  m_deck_reader.DoState(p);
}

}  // namespace Triforce
