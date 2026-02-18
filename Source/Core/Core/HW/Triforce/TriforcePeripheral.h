// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"

#include "Core/HW/Triforce/JVSIO.h"

namespace TriforcePeripheral
{

class Peripheral : public JVSIOBoard
{
public:
  Peripheral();
  virtual ~Peripheral();

  virtual std::pair<u8, u8> GetDipSwitches() const;

  // TODO: return u8 maybe ?
  virtual u32 SerialA(std::span<const u8> data_in, std::span<u8> data_out);

  Peripheral(const Peripheral&) = delete;
  Peripheral& operator=(const Peripheral&) = delete;
  Peripheral(Peripheral&&) = delete;
  Peripheral& operator=(Peripheral&&) = delete;

  virtual void DoState(PointerWrap& p);

protected:
  JVSIOReportCode HandleJVSCommand(JVSIOCommand cmd, JVSIOFrameContext* ctx) override;

  u32 m_dip_switch_0 = 0xff;
  u32 m_dip_switch_1 = 0xfe;

private:
  void AdjustCoins(u8 slot, s32 adjustment);

  std::array<u16, 2> m_coin{};
  std::array<u32, 2> m_coin_pressed{};
};

}  // namespace TriforcePeripheral
