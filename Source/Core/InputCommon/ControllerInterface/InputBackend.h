// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "Common/Functional.h"

class ControllerInterface;

namespace ciface
{

namespace Core
{
class Device;
}

class InputBackend
{
public:
  explicit InputBackend(ControllerInterface* controller_interface);

  // If an implementation relies on outliving its own Device objects,
  // then it should `RemoveAllDevices` or equivalent in its destructor.
  virtual ~InputBackend();

  // Only invoked by ControllerInterface during initialization.
  // May be implemented in a blocking or async manner.
  // But keyboard "defaults" should be added immediately so GetDefaultDeviceString doesn't race.
  virtual void PopulateDevices() = 0;

  // May be implemented in a blocking or async manner.
  // Need not be implemented if managing its own hotplug, but still useful.
  virtual void RefreshDevices() = 0;

  // Invoked regularly just before device inputs are read.
  // Do whatever is needed. Add/Removing devices here is allowed.
  virtual void UpdateBeforeInput();

  virtual void HandleWindowChange();

  ControllerInterface& GetControllerInterface();

protected:
  void AddDevice(std::shared_ptr<Core::Device>);

  // Remove all devices owned by this backend.
  void RemoveAllDevices();

  // Remove devices owned by this backend for which the provided function returns true.
  void RemoveDevices(Common::MoveOnlyFunction<bool(ciface::Core::Device*)> callback);

private:
  ControllerInterface& m_controller_interface;
};

}  // namespace ciface
