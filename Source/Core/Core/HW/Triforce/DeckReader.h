// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"

namespace Triforce
{

// Serial deck reader used by The Key of Avalon.
class DeckReader
{
public:
  void Process(u8 cd_reader_command);

private:
};

}  // namespace Triforce
