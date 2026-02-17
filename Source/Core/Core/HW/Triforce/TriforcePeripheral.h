// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"

namespace TriforcePeripheral
{

class Peripheral
{
public:
  Peripheral();
  virtual ~Peripheral();

  // TODO: return u8 maybe ?
  virtual u32 SerialA(std::span<const u8> data_in, std::span<u8> data_out);

  Peripheral(const Peripheral&) = delete;
  Peripheral& operator=(const Peripheral&) = delete;
  Peripheral(Peripheral&&) = delete;
  Peripheral& operator=(Peripheral&&) = delete;

  virtual void DoState(PointerWrap& p);
};

}  // namespace TriforcePeripheral
