// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/ChunkFile.h"

namespace TriforcePeripheral
{

class Peripheral
{
public:
  Peripheral();
  virtual ~Peripheral();

  Peripheral(const Peripheral&) = delete;
  Peripheral& operator=(const Peripheral&) = delete;
  Peripheral(Peripheral&&) = delete;
  Peripheral& operator=(Peripheral&&) = delete;

  virtual void DoState(PointerWrap& p);
};

}  // namespace TriforcePeripheral
