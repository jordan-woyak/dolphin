
// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"

#include "Core/HW/Triforce/SerialDevice.h"

namespace Triforce
{

enum ICCARDStatus
{
  Okay = 0x0000,
  // SelectFirstCard = 0x0001,  // The game tests for this sometimes.
  FieldOnStart = 0x0020,
  InitializeEnd = 0x0030,
  NoCard = 0x8000,
  Unknown = 0x800e,
  BadCard = 0xffff,
};

// Serial IC-CARD / Serial Deck Reader
class ICCardReader final : public SerialDevice
{
public:
  ICCardReader();

  // TODO: Hacky interface to connect card insertion to user input.
  void ToggleCardState();

  void DoState(PointerWrap& p) override;

protected:
  void Process() override;

private:
  // HAX
  u32 m_reply_delay = 0;

  static constexpr u32 PAGE_SIZE = 8;
  static constexpr u32 PAGE_COUNT = 256;

  static constexpr u32 PAGE_INDEX_MASK = 0xff;

  std::array<u8, PAGE_SIZE * PAGE_COUNT> m_ic_card_data{};

  // TODO:
  u16 m_ic_card_state = 0x20;
  u16 m_ic_card_status = ICCARDStatus::Okay;
};

}  // namespace Triforce
