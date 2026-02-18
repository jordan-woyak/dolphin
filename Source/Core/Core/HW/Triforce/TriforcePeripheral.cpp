// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/TriforcePeripheral.h"
#include "Core/HW/GCPad.h"
#include "InputCommon/GCPadStatus.h"

namespace TriforcePeripheral
{

Peripheral::Peripheral()
{
  // TODO: consolodate the common functionality of this..
  // Note: Immediately overwritten by derived classes.. sloppy.
  SetJVSIOHandler(JVSIOCommand::IOIdentify, [](JVSIOFrameContext ctx) {
    ctx.message.AddData("namco ltd.;FCA-1;Ver1.01;JPN,Multipurpose + Rotary Encoder");
    ctx.message.AddData((u32)0);
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x10, BoardID");

    return JVSIOReportCode::Normal;
  });

  SetJVSIOHandler(JVSIOCommand::CommandRevision, [](JVSIOFrameContext ctx) {
    ctx.message.AddData(0x11);
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x11, CommandRevision");
    return JVSIOReportCode::Normal;
  });

  SetJVSIOHandler(JVSIOCommand::JVSRevision, [](JVSIOFrameContext ctx) {
    ctx.message.AddData(0x20);
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x12, JVRevision");
    return JVSIOReportCode::Normal;
  });

  SetJVSIOHandler(JVSIOCommand::CommVersion, [](JVSIOFrameContext ctx) {
    ctx.message.AddData(0x10);
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x13, CommunicationVersion");
    return JVSIOReportCode::Normal;
  });

  SetJVSIOHandler(JVSIOCommand::MainID, [](JVSIOFrameContext ctx) {
    const u8* const main_id = jvs_io;
    while (jvs_io < jvs_end && *jvs_io++)
    {
    }
    if (main_id < jvs_io)
    {
      DEBUG_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command MainId:\n{}",
                    HexDump(main_id, jvs_io - main_id));
    }
    return JVSIOReportCode::Normal;
  });

  SetJVSIOHandler(JVSIOCommand::CoinCounterDec, [this](JVSIOFrameContext ctx) {
    const u32 slots = *jvs_io++;
    static_assert(std::tuple_size<decltype(m_coin)>{} == 2 &&
                  std::tuple_size<decltype(m_coin_pressed)>{} == 2);
    if (slots > 2)
    {
      WARN_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x21, CoinInput: invalid slots {}",
                   slots);
    }
    const u32 max_slots = std::min(slots, 2u);
    for (u32 i = 0; i < max_slots; i++)
    {
      GCPadStatus pad_status = Pad::GetStatus(i);
      if ((pad_status.switches & SWITCH_COIN) && !m_coin_pressed[i])
      {
        m_coin[i]++;
      }
      m_coin_pressed[i] = pad_status.switches & SWITCH_COIN;
      ctx.message.AddData((m_coin[i] >> 8) & 0x3f);
      ctx.message.AddData(m_coin[i] & 0xff);
    }
    DEBUG_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x21, CoinInput: {}", slots);

    return JVSIOReportCode::Normal;
  });

  SetJVSIOHandler(JVSIOCommand::CoinCounterInc, [this](JVSIOFrameContext ctx) {
    const u32 slot = *jvs_io++;
    const u8 coinh = *jvs_io++;
    const u8 coinl = *jvs_io++;

    m_coin[slot] += (coinh << 8) | coinl;

    DEBUG_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x35, CoinAddOutput: {}", slot);
    return JVSIOReportCode::Normal;
  });

  SetJVSIOHandler(JVSIOCommand::NAMCOCommand, [](JVSIOFrameContext ctx) {
    const u32 namco_command = *jvs_io++;

    if (namco_command == 0x18)
    {
      // ID check
      jvs_io += 4;

      ctx.message.AddData(0xff);
    }
    else
    {
      ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Unknown:{:02x}", namco_command);
    }

    return JVSIOReportCode::Normal;
  });

  SetJVSIOHandler(JVSIOCommand::Reset, [this](JVSIOFrameContext ctx) {
    if (*jvs_io++ == 0xD9)
    {
      NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0xF0, Reset");
      // TODO:
      // m_delay = 0;
      // m_wheel_init = 0;
      // m_ic_card_state = 0x20;
    }

    m_dip_switch_1 |= 1;

    return JVSIOReportCode::Normal;
  });

  SetJVSIOHandler(JVSIOCommand::SetAddress, [this](JVSIOFrameContext ctx) {
    u8 node = *jvs_io++;
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0xF1, SetAddress: node={}", node);
    ctx.message.AddData(node == 1);
    m_dip_switch_1 &= ~1u;

    return JVSIOReportCode::Normal;
  });
}

Peripheral::~Peripheral() = default;

void Peripheral::DoState(PointerWrap& p)
{
}

u32 Peripheral::SerialA(std::span<const u8>, std::span<u8>)
{
  return 0;
}

}  // namespace TriforcePeripheral
