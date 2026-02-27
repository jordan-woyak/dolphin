// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/DeckReader.h"

#include <string_view>

#include <fmt/ranges.h>

#include "Common/BitUtils.h"
#include "Common/ChunkFile.h"
#include "Common/Logging/Log.h"
#include "Common/Swap.h"

namespace
{

// Note: Avalon literally skips `strlen("Version ")` bytes in the display.
constexpr std::string_view CDR_PROGRAM_VERSION = "Version 1.22,2003/09/19,171-8213B";
constexpr std::string_view CDR_BOOT_VERSION = "Version 1.04,2003/06/17,171-8213B";

constexpr std::size_t EXPECTED_VERSION_STR_LENGTH = 48;

// Avalon seems to not care about the actual value.
constexpr u32 SHUTTER_TIME = 1234567890;

// It's currently just based on RunBuffer timing.
// FYI: This isn't even necessary functionality.
constexpr u32 FIRMWARE_UPDATE_TIMEOUT = 240;

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

#pragma pack(push, 1)
struct CardIdentifier
{
  // When 0x01 bit is set, Avalon indexes the table at offset 0xa0.
  // Avalon requires 0x80, 0x40, and 0x20 bits are not set.
  // Maybe this is like "card type" ?
  u8 use_second_table;

  // Avalon requires index < 0x100.
  Common::BigEndianValue<u16> index;
};
#pragma pack(pop)

void DeckReader::Process()
{
  m_ic_card_reader.Process();

  // Transfer all bytes from the downstream IC Card reader.
  // TODO: This is a bit sloppy.
  if (const std::size_t count = m_ic_card_reader.GetOutputCount())
  {
    std::array<u8, 256> buffer;
    m_ic_card_reader.TakeOutput(std::span{buffer}.first(std::min(buffer.size(), count)));
    OutputBytes(std::span{buffer}.first(count));
  }

  // The protocol is very simple.
  //
  // All commands are just a single byte (except FirmwareUpdate).
  // Responses must be the correct length, which varies by command.
  // Only the ReadCard response actually includes some kind of length field.
  //
  // A leading 2 bytes and trailing 1 byte are expected for all responses.
  // Avalon seems to largely not check the actual values,
  //  but a few handlers do verify them.

  const auto input_span = GetInputSpan();

  // FirmwareUpdate is the only known stateful command.
  if (m_firmware_update_timeout != 0)
  {
    // No idea how the real hardware works.
    // There doesn't seem to be any sort of size field so.. here's a timeout.
    if (input_span.empty())
    {
      if (--m_firmware_update_timeout == 0)
      {
        constexpr std::array<u8, 3> fw_update_done{0xaa, u8(CDReaderCommand::FirmwareUpdate), 0x00};
        OutputBytes(fw_update_done);

        INFO_LOG_FMT(SERIALINTERFACE_CARD, "Fake FirmwareUpdate done.");
      }
    }
    else
    {
      // We don't actually do anything with the bytes.
      ChewBytes(input_span.size());
      m_firmware_update_timeout = FIRMWARE_UPDATE_TIMEOUT;
    }

    return;
  }

  if (input_span.empty())
    return;  // Wait for more data.

  const u8 cd_reader_command = input_span.front();

  if (cd_reader_command == 0x00)
  {
    // This is an IC Card Reader command.

    // How does the actual hardware deal with this ?
    // Does the Deck Reader really parse the IC Card Reader commands ?

    if (input_span.size() < 4)
      return;  // Wait for more data.

    const u16 input_payload_size = Common::swap16(input_span.data() + 2);
    const u32 total_request_size = input_payload_size + 5u;

    if (input_span.size() >= total_request_size)
    {
      m_ic_card_reader.WriteBytes(input_span.first(total_request_size));
      ChewBytes(total_request_size);
    }

    return;
  }

  // Write header.
  OutputBytes(std::array<u8, 2>{0xaa, cd_reader_command});

  switch (CDReaderCommand(cd_reader_command))
  {
  case CDReaderCommand::SelfTest:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SelfTest");

    // Avalon appears to not inspect this value.
    OutputByte(0x00);

    break;
  }
  case CDReaderCommand::SensLock:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SensLock");

    constexpr bool is_closed = true;

    const u8 result[] = {is_closed ? 0x01 : 0x00};
    OutputBytes(result);

    break;
  }
  case CDReaderCommand::ProgramVersion:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ProgramVersion");

    OutputBytes(Common::AsU8Span((CDR_PROGRAM_VERSION)));

    // Pad it out with zeros.
    OutputBytes(std::array<u8, EXPECTED_VERSION_STR_LENGTH - CDR_PROGRAM_VERSION.size()>{});

    break;
  }
  case CDReaderCommand::BootVersion:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "BootVersion");

    OutputBytes(Common::AsU8Span(CDR_BOOT_VERSION));

    // Pad it out with zeros.
    OutputBytes(std::array<u8, EXPECTED_VERSION_STR_LENGTH - CDR_BOOT_VERSION.size()>{});

    break;
  }
  case CDReaderCommand::ProgramChecksum:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ProgramChecksum");

    // Avalon seems to not care about the actual value.
    Common::BigEndianValue<u32> fake_checksum{0xd01fc001};
    OutputBytes(Common::AsU8Span(fake_checksum));

    break;
  }
  case CDReaderCommand::BootChecksum:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "BootChecksum");

    // Avalon seems to not care about the actual value.
    Common::BigEndianValue<u32> fake_checksum{0xfeedd01f};
    OutputBytes(Common::AsU8Span(fake_checksum));

    break;
  }
  case CDReaderCommand::CameraCheck:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "CameraCheck");

    // FYI: If `header[1] != 0x68` the game inspects some bits and 9 bytes. Some error state ?

    std::array<u8, 10> response{};
    OutputBytes(response);

    break;
  }
  case CDReaderCommand::ShutterGet:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterGet");

    Common::BigEndianValue<u32> fake_shutter_time{SHUTTER_TIME};
    OutputBytes(Common::AsU8Span(fake_shutter_time));

    break;
  }
  case CDReaderCommand::ShutterAuto:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterAuto");

    Common::BigEndianValue<u32> fake_shutter_time{SHUTTER_TIME};
    OutputBytes(Common::AsU8Span(fake_shutter_time));

    break;
  }
  case CDReaderCommand::Shutter:
  case CDReaderCommand::ShutterAlt:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "Shutter");
    break;
  }
  case CDReaderCommand::ShutterLoad:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterLoad");
    break;
  }
  case CDReaderCommand::ShutterSave:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterSave");
    break;
  }
  case CDReaderCommand::SensCard:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "SensCard");

    // TODO: Is this what this is ? Does Avalon use the value ?
    constexpr bool are_cards_present = true;

    std::array<u8, 1> response = {are_cards_present ? 0x01 : 0x00};
    OutputBytes(response);

    break;
  }
  case CDReaderCommand::ReadCard:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ReadCard");

    constexpr bool are_results_pending = false;

    if (are_results_pending)
    {
      // This causes Avalon to retry the command again in a bit.
      // I suppose it means that the device is currently scanning the cards.
      OutputBytes(std::array<u8, 2>{0xaa, 0x52});
    }
    else
    {
      // We've had one, yes, but what about second header ?
      OutputBytes(std::array<u8, 2>{0xaa, u8(CDReaderCommand::ReadCard)});

      // What happens with more than 30 cards ?
      constexpr u32 card_count = 30;

      OutputByte(u8(card_count * sizeof(CardIdentifier)));

      for (u32 i = 0; i != card_count; ++i)
      {
        CardIdentifier card_id{.use_second_table = 0x01};
        card_id.index = u16(96 + i);

        OutputBytes(Common::AsU8Span(card_id));
      }
    }

    break;
  }
  case CDReaderCommand::FirmwareUpdate:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "FirmwareUpdate");

    m_firmware_update_timeout = FIRMWARE_UPDATE_TIMEOUT;

    // The game will now send raw bytes.
    break;
  }
  default:
  {
    // Responses seem to be variable in length and unspecified so we can't do much.
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Unknown command: {:02x}", cd_reader_command);
    break;
  }
  }

  // Write footer.
  OutputByte(0x00);

  // Every known command is just one byte (except FirmwareUpdate).
  ChewBytes(1);
}

void DeckReader::DoState(PointerWrap& p)
{
  p.Do(m_firmware_update_timeout);

  m_ic_card_reader.DoState(p);
}

}  // namespace Triforce
