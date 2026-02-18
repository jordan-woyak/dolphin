// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/HW/Triforce/ICCardReader.h"
#include "Core/HW/Triforce/TriforcePeripheral.h"

namespace TriforcePeripheral
{

class VirtuaStrikerCommon : public Peripheral
{
public:
  VirtuaStrikerCommon();

protected:
  JVSIOReportCode HandleJVSIORequest(JVSIOCommand cmd, JVSIOFrameContext* ctx) override;
};

class VirtuaStriker3 final : public VirtuaStrikerCommon
{
public:
  VirtuaStriker3();

protected:
  JVSIOReportCode HandleJVSIORequest(JVSIOCommand cmd, JVSIOFrameContext* ctx) override;
};

// Handles both VirtuaStriker4 and VirtuaStriker4_2006.
class VirtuaStriker4 final : public VirtuaStrikerCommon
{
public:
  VirtuaStriker4();

  u32 SerialA(std::span<const u8> data_in, std::span<u8> data_out) override;

protected:
  JVSIOReportCode HandleJVSIORequest(JVSIOCommand cmd, JVSIOFrameContext* ctx) override;

private:
  ICCardReader m_ic_card_reader;
};

}  // namespace TriforcePeripheral
