// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "InputCommon/ControllerInterface/InputBackend.h"

#include "Common/Functional.h"

#include <functional>
#include <memory>
#include <vector>

namespace ciface::Win32
{
enum class DeviceChangeEvent : bool
{
  Arrival,
  Removal,
};

// Opaque type to avoid inclusion of Windows headers here.
class OpaqueHandle
{
public:
  virtual ~OpaqueHandle() = default;
};
using DeviceChangeNotifactionHandle = std::unique_ptr<OpaqueHandle>;

// Returns a handle that unregisters the notification on destruction.
DeviceChangeNotifactionHandle
    CreateDeviceChangeNotification(Common::MoveOnlyFunction<void(DeviceChangeEvent)>);

std::vector<std::unique_ptr<ciface::InputBackend>>
CreateInputBackends(ControllerInterface* controller_interface);

}  // namespace ciface::Win32
