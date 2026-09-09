// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <map>

#include "Common/BitUtils.h"
#include "Common/DirectIOFile.h"
#include "Common/Swap.h"

#include "Core/HW/Triforce/ICCardReader.h"
#include "Core/HW/Triforce/SerialDevice.h"

class PointerWrap;

namespace Triforce
{

// This structure mirrors the 3 bytes per card the deck reader hardware produces.
#pragma pack(push, 1)
struct CardIdentifier
{
  // When the 0x01 bit is set, Avalon indexes a separate smaller table.
  // Avalon specifically requires 0x80, 0x40, and 0x20 bits are not set.
  // We don't know the relevance of this second table.
  // There are even some duplicates between the two tables.
  u8 table_index;

  Common::BigEndianValue<u16> card_index;

  bool operator==(const CardIdentifier& other) const
  {
    return std::ranges::equal(Common::AsU8Span(*this), Common::AsU8Span(other));
  }
};
#pragma pack(pop)

struct CardDatabaseEntry
{
  std::string name_eng;
  std::string name_jpn;

  // TODO: We just load the first {table,index} pair.
  CardIdentifier card_id;
};

// The map key is the printed card number, e.g. "N27" or "Ex11".
using CardDatabase = std::map<std::string, CardDatabaseEntry>;

CardDatabase LoadCardDatabaseFromFile();

// TODO: It's a bit odd to use CardIdentifier here when cards may be specified by "number".
std::optional<std::vector<CardIdentifier>> LoadCardDeckFromFile(const CardDatabase&);

// Serial deck reader used by The Key of Avalon games.
class DeckReader final : public SerialDevice
{
public:
  void Update() override;

  void DoState(PointerWrap& p) override;

  auto* GetICCardReader() { return &m_ic_card_reader; }

private:
  // It seems that the IC Card Reader must be connected through the Deck Reader.
  // The Deck Reader forwards appropriate commands to the IC Card Reader.
  // IC Card Reader responses are passed through back to the baseboard as-is.
  // It can't be the other way around because FirmwareUpdate sends many raw bytes.
  // Unless FirmwareUpdate temporarily cuts off the IC Card Reader ?
  ICCardReader m_ic_card_reader{0};

  void HandleFirmwareUpdate();

  u8 m_firmware_update_timeout = 0;

  File::DirectIOFile m_firmware_dump_file;
};

}  // namespace Triforce
