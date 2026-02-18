// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/KeyOfAvalon.h"
#include "Common/BitUtils.h"

namespace TriforcePeripheral
{

KeyOfAvalon::KeyOfAvalon()
{
  //   m_ic_card_data[0x22] = 0x26;
  //   m_ic_card_data[0x23] = 0x40;

  SetJVSIOHandler(JVSIOCommand::FeatureCheck, [](JVSIOFrameContext ctx) {
    // 1 Player (15bit), 1 Coin slot, 3 Analog-in, Touch, 1 CARD, 1 Driver-out
    // (Unconfirmed)
    ctx.message.AddData(
        Common::AsU8Span(ClientFeatureSpec{ClientFeature::SwitchInput, 0x01, 0x0F, 0x00}));
    ctx.message.AddData(
        Common::AsU8Span(ClientFeatureSpec{ClientFeature::CoinInput, 0x01, 0x00, 0x00}));
    ctx.message.AddData(
        Common::AsU8Span(ClientFeatureSpec{ClientFeature::AnalogInput, 0x03, 0x00, 0x00}));
    ctx.message.AddData(
        Common::AsU8Span(ClientFeatureSpec{ClientFeature::ScreenPositionInput, 0x10, 0x10, 0x01}));
    ctx.message.AddData(
        Common::AsU8Span(ClientFeatureSpec{ClientFeature::CardSystem, 0x01, 0x00, 0x00}));
    ctx.message.AddData(
        Common::AsU8Span(ClientFeatureSpec{ClientFeature::GeneralPurposeOutput, 0x01, 0x00, 0x00}));
    ctx.message.AddData(Common::AsU8Span(ClientFeatureSpec{}));

    return JVSIOReportCode::Normal;
  });
}

u32 KeyOfAvalon::SerialA(std::span<const u8> data_in, std::span<u8> data_out)
{
  return m_ic_card_reader.SerialA(data_in, data_out);
}

}  // namespace TriforcePeripheral
