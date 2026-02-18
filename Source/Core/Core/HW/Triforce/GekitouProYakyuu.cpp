// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/GekitouProYakyuu.h"

#include "Common/BitUtils.h"

namespace TriforcePeripheral
{

GekitouProYakyuu::GekitouProYakyuu() = default;

JVSIOReportCode GekitouProYakyuu::HandleJVSIORequest(JVSIOCommand cmd, JVSIOFrameContext* ctx)
{
  switch (cmd)
  {
  case JVSIOCommand::FeatureCheck:
  {  // 2 Player (13bit), 2 Coin slot, 4 Analog-in, 1 CARD, 8 Driver-out
    // ctx.message.AddData("\x01\x02\x0D\x00", 4);
    // ctx.message.AddData("\x02\x02\x00\x00", 4);
    // ctx.message.AddData("\x03\x04\x00\x00", 4);
    // ctx.message.AddData("\x10\x01\x00\x00", 4);
    // ctx.message.AddData("\x12\x08\x00\x00", 4);
    // ctx.message.AddData("\x00\x00\x00\x00", 4);

    return JVSIOReportCode::Normal;
  }
  default:
    return Peripheral::HandleJVSIORequest(cmd, ctx);
  }
}

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
