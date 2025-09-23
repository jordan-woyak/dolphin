// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/InputBackend.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ciface
{
InputBackend::InputBackend(ControllerInterface* controller_interface)
    : m_controller_interface(*controller_interface)
{
}

InputBackend::~InputBackend() = default;

void InputBackend::UpdateBeforeInput()
{
}

void InputBackend::HandleWindowChange()
{
}

ControllerInterface& InputBackend::GetControllerInterface()
{
  return m_controller_interface;
}

void InputBackend::AddDevice(std::shared_ptr<Core::Device> device)
{
  GetControllerInterface().AddDevice(this, std::move(device));
}

void InputBackend::RemoveAllDevices()
{
  RemoveDevices([](auto&&) constexpr { return true; });
}

void InputBackend::RemoveDevices(Common::MoveOnlyFunction<bool(ciface::Core::Device*)> callback)
{
  GetControllerInterface().RemoveDevices(this, std::move(callback));
}

}  // namespace ciface
