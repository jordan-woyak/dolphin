
// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"

#include "Core/HW/Triforce/SerialDevice.h"

namespace Triforce
{

enum ICCARDStatus
{
  Okay = 0,
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
  u8 m_ic_card_data[2048] = {};

  u16 m_ic_card_state = 0x20;
  u16 m_ic_card_status = ICCARDStatus::Okay;
  u16 m_ic_card_session = 0x23;
};

}  // namespace Triforce
