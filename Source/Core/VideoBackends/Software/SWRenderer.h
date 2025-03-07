// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"

#include "VideoCommon/RenderBase.h"

namespace SW
{
class SWRenderer final : public Renderer
{
public:
  u32 PeekEFBColor(u32 x, u32 y, u32 poke_data) override;
  u32 PeekEFBDepth(u32 x, u32 y, u32 poke_data) override;

  void PokeEFBColor(u32 x, u32 y, u32 poke_data) override {}
  void PokeEFBDepth(u32 x, u32 y, u32 poke_data) override {}

  void ReinterpretPixelData(EFBReinterpretType convtype) override {}
};
}  // namespace SW
