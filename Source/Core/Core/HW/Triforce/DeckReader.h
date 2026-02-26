// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"

class PointerWrap;

namespace Triforce
{

// Serial deck reader used by The Key of Avalon.
class DeckReader
{
public:
  void Process(u8 cd_reader_command);

  void DoState(PointerWrap& p);

private:
};

}  // namespace Triforce
