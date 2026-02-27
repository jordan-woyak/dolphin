// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/DeckReader.h"

#include <string_view>

#include "Common/BitUtils.h"
#include "Common/Logging/Log.h"
#include "Common/Swap.h"

namespace
{

// Note: Game code literally cuts off strlen("Version ") characters.
constexpr std::string_view CDR_PROGRAM_VERSION = "           Version 1.22,2003/09/19,171-8213B";
constexpr std::string_view CDR_BOOT_VERSION = "           Version 1.04,2003/06/17,171-8213B";

constexpr u8 CDR_CARD_DATA[] = {
    0x00, 0x6E, 0x00, 0x00, 0x01, 0x00, 0x00, 0x06, 0x00, 0x00, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00,
    0x0E, 0x00, 0x00, 0x10, 0x00, 0x00, 0x17, 0x00, 0x00, 0x19, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x1B,
    0x00, 0x00, 0x1D, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x20, 0x00, 0x00, 0x22, 0x00, 0x00, 0x23, 0x00,
    0x00, 0x24, 0x00, 0x00, 0x27, 0x00, 0x00, 0x28, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x2F, 0x00, 0x00,
    0x34, 0x00, 0x00, 0x35, 0x00, 0x00, 0x37, 0x00, 0x00, 0x38, 0x00, 0x00, 0x39, 0x00, 0x00, 0x3D,
};

}  // namespace

namespace Triforce
{

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

void DeckReader::Process(u8 cd_reader_command,
                         const std::function<void(std::span<const u8>)>& callback)
{
  std::array<u8, 8> small_response_payload{};
  std::span<const u8> response_payload_span;

  ICCardReplyHeader reply_header{
      .fixed = 0x10,
      .command = cd_reader_command,
  };

  switch (CDReaderCommand(cd_reader_command))
  {
  case CDReaderCommand::ProgramVersion:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ProgramVersion");
    response_payload_span = Common::AsU8Span(CDR_PROGRAM_VERSION);
    break;
  }
  case CDReaderCommand::BootVersion:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "BootVersion");
    response_payload_span = Common::AsU8Span(CDR_BOOT_VERSION);
    break;
  }
  case CDReaderCommand::ShutterGet:
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterGet");

    // I think this is supposed to have no payload.

    // small_response_payload[0] = 0;
    // small_response_payload[1] = 0;
    // small_response_payload[2] = 0;
    // small_response_payload[3] = 0;

    // response_payload_span = small_response_payload;

    break;
  case CDReaderCommand::CameraCheck:
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "CameraCheck");

    small_response_payload[0] = 0x23;
    small_response_payload[1] = 0x28;
    small_response_payload[2] = 0x45;
    small_response_payload[3] = 0x29;
    small_response_payload[4] = 0x45;
    small_response_payload[5] = 0x29;

    response_payload_span = small_response_payload;

    break;
  case CDReaderCommand::ProgramChecksum:
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ProgramChecksum");

    // small_response_payload[0] = 0x23;
    // small_response_payload[1] = 0x28;
    // small_response_payload[2] = 0x45;
    // small_response_payload[3] = 0x29;

    response_payload_span = small_response_payload;

    break;
  case CDReaderCommand::BootChecksum:
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "BootChecksum");

    // small_response_payload[0] = 0x23;
    // small_response_payload[1] = 0x28;
    // small_response_payload[2] = 0x45;
    // small_response_payload[3] = 0x29;

    response_payload_span = small_response_payload;

    break;
  case CDReaderCommand::SelfTest:
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SelfTest");

    // TODO:
    // reply_header.flag = 0x00;
    break;
  case CDReaderCommand::SensLock:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SensLock");
    // TODO:
    // reply_header.status = 0x01;

    const u8 fixed_data[] = {0x10, 0x62, 0x01, 0x00};

    callback(fixed_data);

    return;

    break;
  }
  case CDReaderCommand::SensCard:
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SensCard");
    break;
  case CDReaderCommand::ShutterCard:
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterCard");
    break;
  case CDReaderCommand::ReadCard:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ReadCard");

    // TODO:
    // reply_header.fixed = 0xAA;
    // reply_header.flag = 0xAA;

    response_payload_span = CDR_CARD_DATA;

    break;
  }
  default:
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Unhandled CDReaderCommand: {:02x}", cd_reader_command);
  }

  reply_header.length = Common::swap16(sizeof(reply_header.status) + response_payload_span.size());
  reply_header.status = Common::swap16(reply_header.status);

  const auto header_span = Common::AsU8Span(reply_header);

  const u8 checksum = CheckSumXOR(header_span) ^ CheckSumXOR(response_payload_span);

  callback(header_span);
  callback(response_payload_span);
  callback(Common::AsU8Span(checksum));
}

void DeckReader::DoState(PointerWrap& p)
{
}

}  // namespace Triforce
