// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/HW/Triforce/ICCardReader.h"
#include "Core/HW/Triforce/TriforcePeripheral.h"

namespace TriforcePeripheral
{

// Handles both VirtuaStriker4 and VirtuaStriker4_2006.
class VirtuaStriker4 : public Peripheral
{
public:
  VirtuaStriker4();

  u32 SerialA(std::span<const u8> data_in, std::span<u8> data_out) override;

private:
  ICCardReader m_ic_card_reader;
};

}  // namespace TriforcePeripheral
