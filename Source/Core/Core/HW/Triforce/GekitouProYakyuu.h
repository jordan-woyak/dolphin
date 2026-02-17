// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/HW/Triforce/TriforcePeripheral.h"

namespace TriforcePeripheral
{

class GekitouProYakyuu final : public Peripheral
{
public:
  GekitouProYakyuu();

  u32 SerialA(std::span<const u8> data_in, std::span<u8> data_out) override;
};

}  // namespace TriforcePeripheral
