// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "InputCommon/ControllerInterface/InputBackend.h"

#include <functional>
#include <memory>
#include <vector>

namespace ciface::Win32
{

class DeviceChangeNotification
{
public:
  enum class Event : bool
  {
    DeviceArrival,
    DeviceRemoval,
  };

  DeviceChangeNotification();
  ~DeviceChangeNotification();

  using CallbackType = std::function<void(Event)>;

  void Register(CallbackType);
  void Unregister();

  bool IsRegistered() const;

  void InvokeCallback(Event);

  struct Handle;

private:
  std::unique_ptr<Handle> m_handle;
};

// TODO: A simplier CreateDeviceChangeNotification interface

std::vector<std::unique_ptr<ciface::InputBackend>>
CreateInputBackends(ControllerInterface* controller_interface);

}  // namespace ciface::Win32
