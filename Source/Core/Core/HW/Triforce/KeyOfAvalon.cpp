// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/KeyOfAvalon.h"

namespace TriforcePeripheral
{

KeyOfAvalon::KeyOfAvalon()
{
  //   m_ic_card_data[0x22] = 0x26;
  //   m_ic_card_data[0x23] = 0x40;

  SetJVSIOHandler(JVSIOCommand::FeatureCheck, [](JVSIOFrameContext ctx) {
    // 1 Player (15bit), 1 Coin slot, 3 Analog-in, Touch, 1 CARD, 1 Driver-out
    // (Unconfirmed)
    ctx.message.AddData("\x01\x01\x0F\x00", 4);
    ctx.message.AddData("\x02\x01\x00\x00", 4);
    ctx.message.AddData("\x03\x03\x00\x00", 4);
    ctx.message.AddData("\x06\x10\x10\x01", 4);
    ctx.message.AddData("\x10\x01\x00\x00", 4);
    ctx.message.AddData("\x12\x01\x00\x00", 4);
    ctx.message.AddData("\x00\x00\x00\x00", 4);

    return JVSIOReportCode::Normal;
  });
}

u32 KeyOfAvalon::SerialA(std::span<const u8> data_in, std::span<u8> data_out)
{
  return m_ic_card_reader.SerialA(data_in, data_out);
}

}  // namespace TriforcePeripheral
