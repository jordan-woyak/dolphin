// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/HW/Triforce/ICCardReader.h"
#include "Core/HW/Triforce/SerialDevice.h"

class PointerWrap;

namespace Triforce
{

// Serial deck reader used by The Key of Avalon.
class DeckReader : public SerialDevice
{
public:
  void Process() override;

  void DoState(PointerWrap& p) override;

private:
  // Based on how commands are formatted,
  //  it seems that the IC Card Reader is connected through the Deck Reader.
  // The Deck Reader feeds the appropriate commands to the IC Card Reader.
  // It can't be the other way around because Deck Reader commands are unmarked variable lengths.
  // Unless there's some chip select going on ?
  ICCardReader m_ic_card_reader;

  u8 m_firmware_update_timeout = 0;
};

}  // namespace Triforce
