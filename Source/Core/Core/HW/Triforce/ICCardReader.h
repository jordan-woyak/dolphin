// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "Common/CommonTypes.h"

namespace TriforcePeripheral
{

enum ICCARDStatus
{
  Okay = 0,
  NoCard = 0x8000,
  Unknown = 0x800E,
  BadCard = 0xFFFF,
};

// NOTE: Used to be an union with `u8 data[81 + 4 + 4 + 4]`
// TODO: Should the struct be packed?
struct ICCommand
{
  // u32 pktcmd : 8;
  // u32 pktlen : 8;
  u32 fixed : 8;
  u32 command : 8;

  u32 flag : 8;
  u32 length : 8;
  u32 status : 16;

  u8 extdata[81] = {};
  u32 extlen;
};

class ICCardReader
{
public:
  ICCardReader();

  u32 SerialA(std::span<const u8> data_in, std::span<u8> data_out);

private:
  void ICCardSendReply(ICCommand* iccommand, u8* buffer, u32* length);

  u8 m_ic_card_data[2048] = {};

  // Setup IC-card
  u16 m_ic_card_state = 0x20;
  u16 m_ic_card_status = ICCARDStatus::Okay;
  u16 m_ic_card_session = 0x23;

  u8 m_ic_write_buffer[512] = {};
  u32 m_ic_write_offset = 0;
  u32 m_ic_write_size = 0;
};

}  // namespace TriforcePeripheral
