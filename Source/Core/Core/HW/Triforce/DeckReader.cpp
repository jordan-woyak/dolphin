// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/DeckReader.h"

#include <string_view>

#include <fmt/ranges.h>

#include "Common/BitUtils.h"
#include "Common/ChunkFile.h"
#include "Common/FileUtil.h"
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

auto GetFirmwareDumpFilename()
{
  return fmt::format("{}card_deck_reader_firmware.bin", File::GetUserPath(D_TRIUSER_IDX));
}

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
  // When 0x01 bit is set, Avalon indexes a separate table.
  // Avalon requires 0x80, 0x40, and 0x20 bits are not set.
  // Maybe this is like "card type" ?
  u8 use_alternate_table;

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

        m_firmware_dump_file.Close();
      }
    }
    else
    {
      if (m_firmware_dump_file.IsOpen())
      {
        if (!m_firmware_dump_file.Write(input_span))
        {
          ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Failed to write {} bytes to firmware.",
                        input_span.size());
        }
      }

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

    // Avalon will ask the user to close the shutter if open.
    constexpr bool is_closed = true;

    OutputByte(is_closed ? 0x01 : 0x00);

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
    Common::BigEndianValue<u32> fake_checksum{0x89abcdef};
    OutputBytes(Common::AsU8Span(fake_checksum));

    break;
  }
  case CDReaderCommand::BootChecksum:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "BootChecksum");

    // Avalon seems to not care about the actual value.
    Common::BigEndianValue<u32> fake_checksum{0x01234567};
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

    Common::BigEndianValue<u32> shutter_time{SHUTTER_TIME};
    OutputBytes(Common::AsU8Span(shutter_time));

    break;
  }
  case CDReaderCommand::ShutterAuto:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ShutterAuto");

    Common::BigEndianValue<u32> shutter_time{SHUTTER_TIME};
    OutputBytes(Common::AsU8Span(shutter_time));

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

    // TODO: What is the relevance of this value ?
    // Avalon tests for 0x01.
    OutputByte(0x01);

    break;
  }
  case CDReaderCommand::ReadCard:
  {
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "ReadCard");

    constexpr bool is_deck_empty = false;

    if (is_deck_empty)
    {
      // I think this means that there are no cards.
      OutputBytes(std::array<u8, 2>{0xaa, 0x52});
    }
    else
    {
      // We've had one, yes, but what about second header ?
      OutputBytes(std::array<u8, 2>{0xaa, u8(CDReaderCommand::ReadCard)});

      // What happens with more than 30 cards ?
      constexpr u32 card_count = 30;

      // TODO: Allow this to be loaded from a .json file or something.
      std::pair<u8, u16> cards[] = {
          {1, 1}, {1, 1}, {1, 1}, {1, 2}, {1, 2}, {1, 2}, {1, 3}, {1, 3}, {1, 3}, {1, 1},
          {1, 1}, {1, 1}, {1, 2}, {1, 2}, {1, 2}, {1, 3}, {1, 3}, {1, 3}, {1, 1}, {1, 1},
          {1, 1}, {1, 2}, {1, 2}, {1, 2}, {1, 3}, {1, 3}, {1, 3}, {1, 1}, {1, 1}, {1, 1},
      };
      static_assert(std::size(cards) == card_count);

      OutputByte(u8(card_count * sizeof(CardIdentifier)));

      for (const auto& c : cards)
      {
        CardIdentifier card_id{.use_alternate_table = c.first};
        card_id.index = c.second;

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
    // We save it to a file.

    const auto filename = GetFirmwareDumpFilename();
    if (m_firmware_dump_file.Open(filename, File::AccessMode::Write))
    {
      NOTICE_LOG_FMT(SERIALINTERFACE_CARD, "Writing firmware to: {}", filename);
    }
    else
    {
      ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Failed to open: {}", filename);
    }

    break;
  }
  default:
  {
    // TODO: I think maybe we're supposed to output {0xaa, 0x55} ?

    // Responses seem to be variable in length and unspecified so we can't do much.
    ERROR_LOG_FMT(SERIALINTERFACE_CARD, "Unknown command: {:02x}", cd_reader_command);
    break;
  }
  }

  // TODO: Is this the error code maybe ?

  // Write footer.
  OutputByte(0x00);

  // Every known command is just one byte.
  ChewBytes(1);
}

void DeckReader::DoState(PointerWrap& p)
{
  m_ic_card_reader.DoState(p);

  p.Do(m_firmware_update_timeout);

  if (p.IsReadMode())
    m_firmware_dump_file.Close();
}

}  // namespace Triforce
