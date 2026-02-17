// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/TriforcePeripheral.h"

namespace TriforcePeripheral
{

Peripheral::Peripheral()
{
  SetJVSIOHandler(JVSIOCommand::CommandRevision, [](JVSIOMessageContext ctx) {
    ctx.message.AddData(StatusOkay);
    ctx.message.AddData(0x11);
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x11, CommandRevision");
  });

  SetJVSIOHandler(JVSIOCommand::JVRevision, [](JVSIOMessageContext ctx) {
    ctx.message.AddData(StatusOkay);
    ctx.message.AddData(0x20);
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x12, JVRevision");
  });

  SetJVSIOHandler(JVSIOCommand::CommunicationVersion, [](JVSIOMessageContext ctx) {
    ctx.message.AddData(StatusOkay);
    ctx.message.AddData(0x10);
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x13, CommunicationVersion");
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
