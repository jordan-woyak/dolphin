// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/TriforcePeripheral.h"
#include "Common/BitUtils.h"

namespace TriforcePeripheral
{

Peripheral::Peripheral() = default;

std::pair<u8, u8> Peripheral::GetDipSwitches() const
{
  // TODO: Specialized implementations..
  return {m_dip_switch_0, m_dip_switch_1};
}

JVSIOReportCode Peripheral::HandleJVSIORequest(JVSIOCommand cmd, JVSIOFrameContext* ctx)
{
  // Vendor-specific commands:
  static constexpr auto NAMCO_COMMAND = JVSIOCommand(0x70);

  switch (cmd)
  {
  case JVSIOCommand::IOIdentify:
  {
    ctx->message.AddData(
        Common::AsU8Span(std::span{"namco ltd.;FCA-1;Ver1.01;JPN,Multipurpose + Rotary Encoder"}));
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x10, BoardID");
    return JVSIOReportCode::Normal;
  }
  case JVSIOCommand::CommandRevision:
  {
    ctx->message.AddData(0x11);
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x11, CommandRevision");
    return JVSIOReportCode::Normal;
  }
  case JVSIOCommand::JVSRevision:
  {
    ctx->message.AddData(0x20);
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x12, JVRevision");
    return JVSIOReportCode::Normal;
  }
  case JVSIOCommand::CommVersion:
  {
    ctx->message.AddData(0x10);
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x13, CommunicationVersion");
    return JVSIOReportCode::Normal;
  }
  case JVSIOCommand::MainID:
  {
    const auto null_termination = std::find(ctx->request.m_data, ctx->request.m_data_end, u8{});
    if (null_termination == ctx->request.m_data_end)
      return JVSIOReportCode::ParameterDataError;

    std::string_view main_id_str{reinterpret_cast<const char*>(ctx->request.m_data),
                                 reinterpret_cast<const char*>(null_termination)};
    ctx->request.m_data = null_termination;

    INFO_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command MainId: {}", main_id_str);
    return JVSIOReportCode::Normal;
  }
  case JVSIOCommand::CoinCounterDec:
  {
    if (!ctx->request.HasCount(3))
      return JVSIOReportCode::ParameterSizeError;

    const u8 slot = ctx->request.ReadByte();
    const u8 coin_msb = ctx->request.ReadByte();
    const u8 coin_lsb = ctx->request.ReadByte();

    AdjustCoins(slot, -((coin_msb << 8u) | coin_lsb));
    return JVSIOReportCode::Normal;
  }
  case JVSIOCommand::CoinCounterInc:
  {
    if (!ctx->request.HasCount(3))
      return JVSIOReportCode::ParameterSizeError;

    const u8 slot = ctx->request.ReadByte();
    const u8 coin_msb = ctx->request.ReadByte();
    const u8 coin_lsb = ctx->request.ReadByte();

    AdjustCoins(slot, (coin_msb << 8u) | coin_lsb);
    return JVSIOReportCode::Normal;
  }
  case NAMCO_COMMAND:
  {
    if (!ctx->request.HasCount(1))
      return JVSIOReportCode::ParameterSizeError;

    const u32 namco_command = ctx->request.ReadByte();

    if (namco_command == 0x18)
    {
      if (!ctx->request.HasCount(4))
        return JVSIOReportCode::ParameterSizeError;

      ctx->request.Skip(4);
      ctx->message.AddData(0xff);
    }
    else
    {
      ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Unknown:{:02x}", namco_command);
      return JVSIOReportCode::ParameterDataError;
    }

    return JVSIOReportCode::Normal;
  }
  default:
    // TODO: This is wrong..
    return JVSIOReportCode::ParameterDataError;
  }
}

Peripheral::~Peripheral() = default;

void Peripheral::DoState(PointerWrap& p)
{
}

u32 Peripheral::SerialA(std::span<const u8>, std::span<u8>)
{
  return 0;
}

void Peripheral::AdjustCoins(u8 slot, s32 adjustment)
{
  DEBUG_LOG_FMT(SERIALINTERFACE_JVSIO, "AdjustCoins: {}", slot);
}

}  // namespace TriforcePeripheral
