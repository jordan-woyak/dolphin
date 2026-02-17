// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/GekitouProYakyuu.h"

#include "Common/BitUtils.h"

namespace TriforcePeripheral
{

u32 GekitouProYakyuu::SerialA(std::span<const u8> data_in, std::span<u8> data_out)
{
  // Serial - Unknown

  //   if (!validate_data_in_out(sizeof(u32), 0, "SerialA (Unknown)"))
  //     break;
  const u32 serial_command = Common::BitCastPtr<u32>(data_in.data());

  u32 data_offset = 0;

  if (serial_command == 0x00001000)
  {
    // if (!validate_data_in_out(0, 5, "SerialA (Unknown)"))
    //   break;
    data_out[data_offset++] = 1;
    data_out[data_offset++] = 2;
    data_out[data_offset++] = 3;
  }

  return data_offset;
}

}  // namespace TriforcePeripheral
