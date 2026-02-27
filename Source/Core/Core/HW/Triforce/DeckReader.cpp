// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/DeckReader.h"

#include <string_view>

#include "Common/BitUtils.h"
#include "Common/Logging/Log.h"
#include "Common/Swap.h"

namespace
{

constexpr std::string_view CDR_PROGRAM_VERSION = "Version 1.22,2003/09/19,171-8213B";
constexpr std::string_view CDR_BOOT_VERSION = "Version 1.04,2003/06/17,171-8213B";

constexpr u8 CDR_CARD_DATA[] = {
    0x6E, 0x00, 0x00, 0x01, 0x00, 0x00, 0x06, 0x00, 0x00, 0x07, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x0E,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x17, 0x00, 0x00, 0x19, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x1B, 0x00,
    0x00, 0x1D, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x20, 0x00, 0x00, 0x22, 0x00, 0x00, 0x23, 0x00, 0x00,
    0x24, 0x00, 0x00, 0x27, 0x00, 0x00, 0x28, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x34,
    0x00, 0x00, 0x35, 0x00, 0x00, 0x37, 0x00, 0x00, 0x38, 0x00, 0x00, 0x39, 0x00, 0x00, 0x3D,
};

// Note: Avalon literally just cuts off `strlen("Version ")` bytes.
constexpr std::size_t EXPECTED_VERSION_FRONT_PADDING = 8;
constexpr std::size_t EXPECTED_VERSION_LENGTH = 40;

}  // namespace

namespace Triforce
{

enum class CDReaderCommand : u8
{
  ShutterAuto = 0x61,
  BootVersion = 0x62,
  SensLock = 0x63,
  SensCard = 0x65,
  FirmwareUpdate = 0x66,
  ShutterGet = 0x67,
  CameraCheck = 0x68,
  Shutter = 0x69,
  ProgramChecksum = 0x6b,
  ShutterAlt = 0x6c,  // Not sure what the difference is. Avalon seems to only use 0x69.
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
  // FYI: byte[0] of a response set to 0xaa seems to trigger poorly implemented error handling.
  // Other that that, Avalon appears to largely not inspect the leading 2 bytes,
  //  and trailing 1 byte that are expected in most responses.

  const auto put_header = [&] {
    std::array<u8, 2> header{};
    callback(header);
  };

  const auto put_footer = [&] {
    std::array<u8, 1> header{};
    callback(header);
  };

  switch (CDReaderCommand(cd_reader_command))
  {
  case CDReaderCommand::SelfTest:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SelfTest");

    put_header();

    std::array<u8, 1> result = {0x00};
    callback(result);

    put_footer();
    break;
  }
  case CDReaderCommand::SensLock:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SensLock");

    put_header();

    const u8 result[] = {0x01};
    callback(result);

    put_footer();
    break;
  }
  case CDReaderCommand::ProgramVersion:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ProgramVersion");

    put_header();

    std::array<u8, EXPECTED_VERSION_FRONT_PADDING> front_padding{};
    front_padding.fill(' ');
    callback(front_padding);

    callback(Common::AsU8Span(CDR_PROGRAM_VERSION));

    std::array<u8, EXPECTED_VERSION_LENGTH - CDR_PROGRAM_VERSION.size()> end_padding{};
    callback(end_padding);

    put_footer();
    break;
  }
  case CDReaderCommand::BootVersion:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "BootVersion");

    put_header();

    std::array<u8, EXPECTED_VERSION_FRONT_PADDING> front_padding{};
    front_padding.fill(' ');
    callback(front_padding);

    callback(Common::AsU8Span(CDR_BOOT_VERSION));

    std::array<u8, EXPECTED_VERSION_LENGTH - CDR_BOOT_VERSION.size()> end_padding{};
    callback(end_padding);

    put_footer();
    break;
  }
  case CDReaderCommand::ProgramChecksum:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ProgramChecksum");

    put_header();

    // Avalon seems to not care.
    Common::BigEndianValue<u32> some_value{0xdeadbeef};
    callback(Common::AsU8Span(some_value));

    put_footer();
    break;
  }
  case CDReaderCommand::BootChecksum:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "BootChecksum");

    put_header();

    // Avalon seems to not care.
    Common::BigEndianValue<u32> some_value{0xdeadbeef};
    callback(Common::AsU8Span(some_value));

    put_footer();
    break;
  }
  case CDReaderCommand::CameraCheck:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "CameraCheck");

    put_header();

    std::array<u8, 10> response{};
    callback(response);

    put_footer();
    break;
  }
  case CDReaderCommand::ShutterGet:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterGet");

    put_header();

    Common::BigEndianValue<u32> some_value{1234567890};
    callback(Common::AsU8Span(some_value));

    put_footer();
    break;
  }
  case CDReaderCommand::ShutterAuto:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterAuto");

    put_header();

    std::array<u8, 4> response{};
    callback(response);

    put_footer();
    break;
  }
  case CDReaderCommand::Shutter:
  case CDReaderCommand::ShutterAlt:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "Shutter");

    put_header();
    put_footer();
    break;
  }
  case CDReaderCommand::ShutterLoad:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterLoad");

    put_header();
    put_footer();
    break;
  }
  case CDReaderCommand::ShutterSave:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterSave");

    put_header();
    put_footer();
    break;
  }
  case CDReaderCommand::SensCard:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SensCard");

    put_header();

    std::array<u8, 1> response{};
    callback(response);

    put_footer();
    break;
  }
  case CDReaderCommand::ReadCard:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ReadCard");

    put_header();

    callback(CDR_CARD_DATA);

    put_footer();
    break;
  }
  case CDReaderCommand::FirmwareUpdate:
  {
    WARN_LOG_FMT(SERIALINTERFACE_CARD, "FirmwareUpdate");

    // The game will send many raw bytes over the stream. I don't know what signifies how many.
    // Afterward I think we are supposed to respond with 3 bytes.
    constexpr bool actually_start_update = false;

    std::array<u8, 2> response{0xaa, actually_start_update ? 0x66 : 0};
    callback(response);

    put_footer();
    break;
  }
  default:
  {
    // Responses seem to be variable in length and unspecified so we can't do much.
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Unknown command: {:02x}", cd_reader_command);
    break;
  }
  }
}

void DeckReader::DoState(PointerWrap& p)
{
}

}  // namespace Triforce
