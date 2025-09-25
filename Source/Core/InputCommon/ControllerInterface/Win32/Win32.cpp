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
struct HCMNotificationHandle final : OpaqueHandle
{
  Common::MoveOnlyFunction<void(DeviceChangeEvent)> callback;
  HCMNOTIFICATION notify_handle{nullptr};
  ~HCMNotificationHandle() override;
};

}  // namespace ciface::Win32

_Pre_satisfies_(EventDataSize >= sizeof(CM_NOTIFY_EVENT_DATA)) static DWORD CALLBACK
    OnDevicesChanged(_In_ HCMNOTIFICATION hNotify, _In_opt_ PVOID Context,
                     _In_ CM_NOTIFY_ACTION Action,
                     _In_reads_bytes_(EventDataSize) PCM_NOTIFY_EVENT_DATA EventData,
                     _In_ DWORD EventDataSize)
{
  auto* const ptr = static_cast<ciface::Win32::HCMNotificationHandle*>(Context);

  using ciface::Win32::DeviceChangeEvent;

  switch (Action)
  {
  case CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL:
    ptr->callback(DeviceChangeEvent::Arrival);
    break;
  case CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL:
    ptr->callback(DeviceChangeEvent::Removal);
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

DeviceChangeNotifactionHandle
CreateDeviceChangeNotification(Common::MoveOnlyFunction<void(DeviceChangeEvent)> func)
{
  DEBUG_LOG_FMT(CONTROLLERINTERFACE, "CM_Register_Notification");

  auto result = std::make_unique<HCMNotificationHandle>();
  result->callback = std::move(func);

  CM_NOTIFY_FILTER notify_filter{.cbSize = sizeof(notify_filter),
                                 .FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE,
                                 .u{.DeviceInterface{.ClassGuid = GUID_DEVINTERFACE_HID}}};
  const CONFIGRET cfg_rv = CM_Register_Notification(&notify_filter, result.get(), OnDevicesChanged,
                                                    &result->notify_handle);
  if (cfg_rv != CR_SUCCESS)
  {
    ERROR_LOG_FMT(CONTROLLERINTERFACE, "CM_Register_Notification failed: {:x}", cfg_rv);
  }

  return result;
}

HCMNotificationHandle::~HCMNotificationHandle()
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
