// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ciface::Qt
{
class InputBackend;

std::unique_ptr<ciface::InputBackend> CreateInputBackend(ControllerInterface* controller_interface);

}  // namespace ciface::Qt
