// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/KeyOfAvalon.h"

namespace TriforcePeripheral
{

KeyOfAvalon::KeyOfAvalon()
{
  //   m_ic_card_data[0x22] = 0x26;
  //   m_ic_card_data[0x23] = 0x40;
}

u32 KeyOfAvalon::SerialA(std::span<const u8> data_in, std::span<u8> data_out)
{
  return m_ic_card_reader.SerialA(data_in, data_out);
}

}  // namespace TriforcePeripheral
