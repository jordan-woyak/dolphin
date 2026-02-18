// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/HW/Triforce/TriforcePeripheral.h"

namespace TriforcePeripheral
{

class FZeroAXCommon : public Peripheral
{
public:
  FZeroAXCommon();

  u32 SerialA(std::span<const u8> data_in, std::span<u8> data_out) override;

protected:
  JVSIOReportCode HandleJVSIORequest(JVSIOCommand cmd, JVSIOFrameContext* ctx) override;

private:
  u32 m_motor_init = 0;
  s16 m_motor_force_y = 0;

  u8 m_rx_reply = 0xF0;
  int m_delay = 0;
};

class FZeroAX final : public FZeroAXCommon
{
public:
  FZeroAX();

protected:
  JVSIOReportCode HandleJVSIORequest(JVSIOCommand cmd, JVSIOFrameContext* ctx) override;
};

class FZeroAXMonster final : public FZeroAXCommon
{
public:
  FZeroAXMonster();
};

}  // namespace TriforcePeripheral
