// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/TriforcePeripheral.h"

namespace TriforcePeripheral
{

Peripheral::Peripheral() = default;

Peripheral::~Peripheral() = default;

void Peripheral::DoState(PointerWrap& p)
{
}

u32 Peripheral::SerialA(std::span<const u8>, std::span<u8>)
{
  return 0;
}

}  // namespace TriforcePeripheral
