// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/VirtuaStriker.h"

namespace TriforcePeripheral
{

VirtuaStriker4::VirtuaStriker4()
{
  //   m_ic_card_data[0x22] = 0x44;
  //   m_ic_card_data[0x23] = 0x00;
}

u32 VirtuaStriker4::SerialA(std::span<const u8> data_in, std::span<u8> data_out)
{
  return m_ic_card_reader.SerialA(data_in, data_out);
}

}  // namespace TriforcePeripheral
