// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/DInput/DInput.h"

#include "Common/HRWrap.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/ControllerInterface/DInput/DInputJoystick.h"
#include "InputCommon/ControllerInterface/DInput/DInputKeyboardMouse.h"
#include "InputCommon/ControllerInterface/Win32/Win32.h"

#pragma comment(lib, "Dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace ciface::DInput
{
BOOL CALLBACK DIEnumDeviceObjectsCallback(LPCDIDEVICEOBJECTINSTANCE lpddoi, LPVOID pvRef)
{
  ((std::list<DIDEVICEOBJECTINSTANCE>*)pvRef)->push_back(*lpddoi);
  return DIENUM_CONTINUE;
}

BOOL CALLBACK DIEnumDevicesCallback(LPCDIDEVICEINSTANCE lpddi, LPVOID pvRef)
{
  ((std::list<DIDEVICEINSTANCE>*)pvRef)->push_back(*lpddi);
  return DIENUM_CONTINUE;
}

std::string GetDeviceName(const LPDIRECTINPUTDEVICE8 device)
{
  DIPROPSTRING str = {};
  str.diph.dwSize = sizeof(str);
  str.diph.dwHeaderSize = sizeof(str.diph);
  str.diph.dwHow = DIPH_DEVICE;

  std::string result;
  HRESULT hr = device->GetProperty(DIPROP_PRODUCTNAME, &str.diph);
  if (SUCCEEDED(hr))
  {
    result = StripWhitespace(WStringToUTF8(str.wsz));
  }
  else
  {
    ERROR_LOG_FMT(CONTROLLERINTERFACE, "GetProperty(DIPROP_PRODUCTNAME) failed: {}",
                  Common::HRWrap(hr));
  }

  return result;
}

class InputBackend final : public ciface::InputBackend
{
public:
  explicit InputBackend(ControllerInterface* controller_interface);
  ~InputBackend() override;

  void PopulateDevices() override;

  void HandleWindowChange() override;

  void RefreshDevices() override
  {
    if (!m_idi8)
      return;

    m_notification.Unregister();
    RefreshJoysticks();
    m_notification.Register(std::bind(&InputBackend::RefreshJoysticks, this));
  }

private:
  void RefreshJoysticks();

  HWND GetHWND()
  {
    return static_cast<HWND>(GetControllerInterface().GetWindowSystemInfo().render_window);
  }

  HMODULE hXInput = nullptr;
  IDirectInput8* m_idi8 = nullptr;
  Win32::DeviceChangeNotification m_notification;
};

// Assumes hwnd had not changed from the previous call
InputBackend::InputBackend(ControllerInterface* controller_interface)
    : ciface::InputBackend{controller_interface}
{
  HRESULT hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8,
                                  (LPVOID*)&m_idi8, nullptr);
  if (FAILED(hr))
  {
    ERROR_LOG_FMT(CONTROLLERINTERFACE, "DirectInput8Create failed: {}", Common::HRWrap(hr));
  }
}

void InputBackend::PopulateDevices()
{
  if (!m_idi8)
    return;

  if (auto kbm = CreateKeyboardMouse(m_idi8, GetHWND()))
    AddDevice(std::move(kbm));

  RefreshJoysticks();

  m_notification.Register(std::bind(&InputBackend::RefreshJoysticks, this));
}

void InputBackend::RefreshJoysticks()
{
  // Remove old (invalid) devices. No need to ever remove the KeyboardMouse device.
  // Note that if we have 2+ DInput controllers, not fully repopulating devices
  // will mean that a device with index "2" could persist while there is no device with index "0".
  // This is slightly inconsistent as when we refresh all devices, they will instead reset, and
  // that happens a lot (for uncontrolled reasons, like starting/stopping the emulation).
  RemoveDevices([](const auto* dev) { return !dev->IsValid(); });

  EnumerateJoysticks(m_idi8, GetHWND(), std::bind_front(&InputBackend::AddDevice, this));
}

void InputBackend::HandleWindowChange()
{
  if (!m_idi8)
    return;

  m_notification.Unregister();

  // Remove all DInput Device objects except the KeyboardMouse.
  RemoveDevices([](const auto* dev) { return !dev->IsVirtualDevice(); });

  SetKeyboardMouseWindow(GetHWND());

  RefreshJoysticks();

  m_notification.Register(std::bind(&InputBackend::RefreshJoysticks, this));
}

InputBackend::~InputBackend()
{
  m_notification.Unregister();

  RemoveAllDevices();

  if (!m_idi8)
    return;

  m_idi8->Release();
  m_idi8 = nullptr;
}

std::unique_ptr<ciface::InputBackend> CreateInputBackend(ControllerInterface* controller_interface)
{
  return std::make_unique<InputBackend>(controller_interface);
}

}  // namespace ciface::DInput
