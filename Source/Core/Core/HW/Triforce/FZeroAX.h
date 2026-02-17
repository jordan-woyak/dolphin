// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/HW/Triforce/TriforcePeripheral.h"

namespace TriforcePeripheral
{

class FZeroAX : public Peripheral
{
public:
  u32 SerialA(std::span<const u8> data_in, std::span<u8> data_out) override;

private:
  u32 m_motor_init = 0;
  u8 m_motor_reply[64] = {};
  s16 m_motor_force_y = 0;
};

}  // namespace TriforcePeripheral
