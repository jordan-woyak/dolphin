// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <span>

#include "Common/CommonTypes.h"

class PointerWrap;

namespace Triforce
{

// Serial deck reader used by The Key of Avalon.
class DeckReader
{
public:
  // TODO: This is an ugly interface..
  void Process(u8 cd_reader_command, const std::function<void(std::span<const u8>)>& callback);

  void DoState(PointerWrap& p);

private:
};

}  // namespace Triforce
