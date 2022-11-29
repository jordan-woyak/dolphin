// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/Qt/QtInput.h"

namespace ciface::Qt
{

class KeyboardMouseDevice : public ciface::Core::Device
{
public:
  // TODO: "Qt" or something more generic?
  std::string GetSource() const override { return "Qt"; }
  std::string GetName() const override { return "Keyboard Mouse"; }
  int GetSortPriority() const override { return 6; }
  bool IsVirtualDevice() const override { return true; }
};

class InputBackend final : public ciface::InputBackend
{
public:
  using ciface::InputBackend::InputBackend;

  void PopulateDevices() override
  {
    GetControllerInterface().AddDevice(std::make_shared<KeyboardMouseDevice>());
  }
};

std::unique_ptr<ciface::InputBackend> CreateInputBackend(ControllerInterface* controller_interface)
{
  return std::make_unique<InputBackend>(controller_interface);
}

}  // namespace ciface::Qt
