// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/Matrix.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ciface::Qt
{
class InputBackend;

class KeyboardMouseStateReader
{
public:
  using KeyCode = int;

  virtual bool GetKeyState(KeyCode) const;
  virtual Common::TVec2<ControlState> GetMousePosition() const;
};

std::unique_ptr<ciface::InputBackend> CreateInputBackend(ControllerInterface* controller_interface);

}  // namespace ciface::Qt
