// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/VirtuaStriker.h"
#include "Common/BitUtils.h"

namespace TriforcePeripheral
{

VirtuaStrikerCommon::VirtuaStrikerCommon()
{
  // TODO: consolodate the common functionality of this..
  SetJVSIOHandler(JVSIOCommand::IOIdentify, [](JVSIOFrameContext ctx) {
    ctx.message.AddData(
        Common::AsU8Span(std::span{"SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551;Ver1.00"}));
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x10, BoardID");

    return JVSIOReportCode::Normal;
  });
}

VirtuaStriker3::VirtuaStriker3()
{
  SetJVSIOHandler(JVSIOCommand::FeatureCheck, [](JVSIOFrameContext ctx) {
    // 2 Player (13bit), 2 Coin slot, 4 Analog-in, 1 CARD, 8 Driver-out
    // ctx.message.AddData("\x01\x02\x0D\x00", 4);
    // ctx.message.AddData("\x02\x02\x00\x00", 4);
    // ctx.message.AddData("\x10\x01\x00\x00", 4);
    // ctx.message.AddData("\x12\x08\x00\x00", 4);
    // ctx.message.AddData("\x00\x00\x00\x00", 4);

    return JVSIOReportCode::Normal;
  });
}

VirtuaStriker4::VirtuaStriker4()
{
  //   m_ic_card_data[0x22] = 0x44;
  //   m_ic_card_data[0x23] = 0x00;

  SetJVSIOHandler(JVSIOCommand::FeatureCheck, [](JVSIOFrameContext ctx) {
    // 2 Player (13bit), 1 Coin slot, 4 Analog-in, 1 CARD
    // ctx.message.AddData("\x01\x02\x0D\x00", 4);
    // ctx.message.AddData("\x02\x01\x00\x00", 4);
    // ctx.message.AddData("\x03\x04\x00\x00", 4);
    // ctx.message.AddData("\x10\x01\x00\x00", 4);
    // ctx.message.AddData("\x00\x00\x00\x00", 4);

    return JVSIOReportCode::Normal;
  });
}

u32 VirtuaStriker4::SerialA(std::span<const u8> data_in, std::span<u8> data_out)
{
  return m_ic_card_reader.SerialA(data_in, data_out);
}

}  // namespace TriforcePeripheral
