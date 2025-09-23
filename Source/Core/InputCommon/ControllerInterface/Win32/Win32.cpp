// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/Win32/Win32.h"

#include <Windows.h>
#include <cfgmgr32.h>
// must be before hidclass
#include <initguid.h>

#include <hidclass.h>

#include "Common/Logging/Log.h"
#include "InputCommon/ControllerInterface/DInput/DInput.h"
#include "InputCommon/ControllerInterface/WGInput/WGInput.h"
#include "InputCommon/ControllerInterface/XInput/XInput.h"

#pragma comment(lib, "OneCoreUAP.Lib")

namespace ciface::Win32
{
struct DeviceChangeNotification::Handle
{
  HCMNOTIFICATION notify_handle{nullptr};
  DeviceChangeNotification::CallbackType callback;

  ~Handle();
};

void DeviceChangeNotification::InvokeCallback(Event event)
{
  m_handle->callback(event);
}

}  // namespace ciface::Win32

_Pre_satisfies_(EventDataSize >= sizeof(CM_NOTIFY_EVENT_DATA)) static DWORD CALLBACK
    OnDevicesChanged(_In_ HCMNOTIFICATION hNotify, _In_opt_ PVOID Context,
                     _In_ CM_NOTIFY_ACTION Action,
                     _In_reads_bytes_(EventDataSize) PCM_NOTIFY_EVENT_DATA EventData,
                     _In_ DWORD EventDataSize)
{
  using ciface::Win32::DeviceChangeNotification;
  auto* const ptr = static_cast<DeviceChangeNotification*>(Context);

  switch (Action)
  {
  case CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL:
    ptr->InvokeCallback(DeviceChangeNotification::Event::DeviceArrival);
    break;
  case CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL:
    ptr->InvokeCallback(DeviceChangeNotification::Event::DeviceRemoval);
    break;
  }
  return ERROR_SUCCESS;
}

namespace ciface::Win32
{
std::vector<std::unique_ptr<ciface::InputBackend>>
CreateInputBackends(ControllerInterface* controller_interface)
{
  std::vector<std::unique_ptr<ciface::InputBackend>> results;
  results.emplace_back(DInput::CreateInputBackend(controller_interface));
  results.emplace_back(XInput::CreateInputBackend(controller_interface));
  results.emplace_back(WGInput::CreateInputBackend(controller_interface));
  return results;
}

DeviceChangeNotification::DeviceChangeNotification() = default;
DeviceChangeNotification::~DeviceChangeNotification() = default;

void DeviceChangeNotification::Register(CallbackType func)
{
  DEBUG_LOG_FMT(CONTROLLERINTERFACE, "CM_Register_Notification");
  HCMNOTIFICATION notify_handle{nullptr};
  CM_NOTIFY_FILTER notify_filter{.cbSize = sizeof(notify_filter),
                                 .FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE,
                                 .u{.DeviceInterface{.ClassGuid = GUID_DEVINTERFACE_HID}}};
  const CONFIGRET cfg_rv =
      CM_Register_Notification(&notify_filter, this, OnDevicesChanged, &notify_handle);
  if (cfg_rv != CR_SUCCESS)
  {
    ERROR_LOG_FMT(CONTROLLERINTERFACE, "CM_Register_Notification failed: {:x}", cfg_rv);
  }

  m_handle = std::make_unique<Handle>(notify_handle, std::move(func));
}

void DeviceChangeNotification::Unregister()
{
  m_handle.reset();
}

bool DeviceChangeNotification::IsRegistered() const
{
  return m_handle != nullptr;
}

DeviceChangeNotification::Handle::~Handle()
{
  if (notify_handle != nullptr)
  {
    DEBUG_LOG_FMT(CONTROLLERINTERFACE, "CM_Unregister_Notification");
    const CONFIGRET cfg_rv = CM_Unregister_Notification(notify_handle);
    if (cfg_rv != CR_SUCCESS)
    {
      ERROR_LOG_FMT(CONTROLLERINTERFACE, "CM_Unregister_Notification failed: {:x}", cfg_rv);
    }
    notify_handle = nullptr;
  }
}

}  // namespace ciface::Win32
