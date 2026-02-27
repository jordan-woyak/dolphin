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

// Note: Avalon literally skips `strlen("Version ")` bytes.
constexpr std::size_t EXPECTED_VERSION_FRONT_PADDING = 8;
constexpr std::size_t EXPECTED_VERSION_LENGTH = 40;

// Avalon seems to not care about the actual value.
constexpr u32 SHUTTER_TIME = 1234567890;

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
  // A leading 2 bytes and trailing 1 byte are expected for all responses.
  // However, Avalon appears to largely never inspect the actual values.

  const auto put_header = [&] {
    std::array<u8, 2> header{0xaa, cd_reader_command};
    callback(header);
  };

  const auto put_footer = [&] { callback(Common::AsU8Span(u8{})); };

  switch (CDReaderCommand(cd_reader_command))
  {
  case CDReaderCommand::SelfTest:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SelfTest");

    put_header();

    // Avalon appears to not inspect this value.
    callback(Common::AsU8Span(u8(0x00)));

    put_footer();
    break;
  }
  case CDReaderCommand::SensLock:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SensLock");

    put_header();

    constexpr bool is_closed = true;

    const u8 result[] = {is_closed ? 0x01 : 0x00};
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

    // Avalon seems to not care about the actual value.
    Common::BigEndianValue<u32> fake_checksum{0xdeadbeef};
    callback(Common::AsU8Span(fake_checksum));

    put_footer();
    break;
  }
  case CDReaderCommand::BootChecksum:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "BootChecksum");

    put_header();

    // Avalon seems to not care about the actual value.
    Common::BigEndianValue<u32> fake_checksum{0xdeadbeef};
    callback(Common::AsU8Span(fake_checksum));

    put_footer();
    break;
  }
  case CDReaderCommand::CameraCheck:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "CameraCheck");

    // FYI: If `header[1] != 0x68` the game inspects some bits and 9 bytes. Some error state ?
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

    callback(Common::AsU8Span(Common::BigEndianValue<u32>{SHUTTER_TIME}));

    put_footer();
    break;
  }
  case CDReaderCommand::ShutterAuto:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterAuto");

    put_header();

    callback(Common::AsU8Span(Common::BigEndianValue<u32>{SHUTTER_TIME}));

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

    // TODO: Is this what this is ? Does Avalon use the value ?
    constexpr bool are_cards_present = true;

    std::array<u8, 1> response = {are_cards_present ? 0x01 : 0x00};
    callback(response);

    put_footer();
    break;
  }
  case CDReaderCommand::ReadCard:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ReadCard");

    constexpr bool are_results_pending = false;

    if (are_results_pending)
    {
      // This causes Avalon to retry the command again in a bit.
      // I suppose it means that the device is actively scanning the cards.
      callback(std::array<u8, 2>{0xaa, 0x52});
    }
    else
    {
      // Two headers are expected.
      put_header();  // Avalon has logic to handle a 0x52 response here, what's that about ? That
                     // might mean no cards ?
      put_header();

#pragma pack(push, 1)
      struct CardID
      {
        // When 0x01 bit is set, Avalon indexes the table at offset 0xa0.
        // Avalon requires 0x80, 0x40, and 0x20 bits are not set.
        // Maybe this is like "card type" ?
        u8 use_second_table;

        // Avalon requires index < 0x100.
        Common::BigEndianValue<u16> index;
      };
#pragma pack(pop)

      // What happens with more than 30 cards ?
      constexpr u32 card_count = 30;

      callback(Common::AsU8Span(u8(card_count * sizeof(CardID))));

      for (u32 i = 0; i != card_count; ++i)
      {
        CardID card_id{
            .use_second_table = 0x01,
            .index{u16(96 + i)},
        };

        callback(Common::AsU8Span(card_id));
      }
    }

    put_footer();
    break;
  }
  // case CDReaderCommand::FirmwareUpdate:
  // {
  //   WARN_LOG_FMT(SERIALINTERFACE_CARD, "FirmwareUpdate");

  //   // The game will send many raw bytes over the stream. I don't know what signifies how many.
  //   // Afterward I think we are supposed to respond with 3 bytes.

  //   // TODO: Make ICCardReader not break from this !

  //   put_header();
  //   put_footer();
  //   break;
  // }
  default:
  {
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Unknown command: {:02x}", cd_reader_command);

    // Responses seem to be variable in length and unspecified so we can't do much.
    constexpr std::array<u8, 3> fail = {0xaa, 0xff, 0xff};
    callback(fail);
    break;
  }
  }
}

void DeckReader::DoState(PointerWrap& p)
{
}

}  // namespace Triforce
