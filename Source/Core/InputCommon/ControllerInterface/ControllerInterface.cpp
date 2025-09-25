// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#include <algorithm>

#include "Common/Assert.h"
#include "Common/Logging/Log.h"
#include "InputCommon/ControllerInterface/Wiimote/WiimoteController.h"

#ifdef CIFACE_USE_WIN32
#include "InputCommon/ControllerInterface/Win32/Win32.h"
#endif
#ifdef CIFACE_USE_XLIB
#include "InputCommon/ControllerInterface/Xlib/XInput2.h"
#endif
#ifdef CIFACE_USE_OSX
#include "InputCommon/ControllerInterface/Quartz/Quartz.h"
#endif
#ifdef CIFACE_USE_SDL
#include "InputCommon/ControllerInterface/SDL/SDL.h"
#endif
#ifdef CIFACE_USE_ANDROID
#include "InputCommon/ControllerInterface/Android/Android.h"
#endif
#ifdef CIFACE_USE_EVDEV
#include "InputCommon/ControllerInterface/evdev/evdev.h"
#endif
#ifdef CIFACE_USE_PIPES
#include "InputCommon/ControllerInterface/Pipes/Pipes.h"
#endif
#ifdef CIFACE_USE_DUALSHOCKUDPCLIENT
#include "InputCommon/ControllerInterface/DualShockUDPClient/DualShockUDPClient.h"
#endif
#ifdef CIFACE_USE_STEAMDECK
#include "InputCommon/ControllerInterface/SteamDeck/SteamDeck.h"
#endif

ControllerInterface g_controller_interface;

// We need to save which input channel we are in by thread, so we can access the correct input
// update values in different threads by input channel. We start from InputChannel::Host on all
// threads as hotkeys are updated from a worker thread, but UI can read from the main thread. This
// will never interfere with game threads.
static thread_local ciface::InputChannel tls_input_channel = ciface::InputChannel::Host;

bool ControllerInterface::IsInit() const
{
  return m_is_init;
}

void ControllerInterface::Initialize(const WindowSystemInfo& wsi)
{
  assert(!m_is_init);

  m_is_init = true;

  m_wsi = wsi;

#ifdef CIFACE_USE_WIN32
  for (auto& ptr : ciface::Win32::CreateInputBackends(this))
    m_input_backends.emplace_back(std::move(ptr));
#endif
#ifdef CIFACE_USE_XLIB
  m_input_backends.emplace_back(ciface::XInput2::CreateInputBackend(this));
#endif
#ifdef CIFACE_USE_OSX
  m_input_backends.emplace_back(ciface::Quartz::CreateInputBackend(this));
#endif
#ifdef CIFACE_USE_SDL
  m_input_backends.emplace_back(ciface::SDL::CreateInputBackend(this));
#endif
#ifdef CIFACE_USE_ANDROID
  m_input_backends.emplace_back(ciface::Android::CreateInputBackend(this));
#endif
#ifdef CIFACE_USE_EVDEV
  m_input_backends.emplace_back(ciface::evdev::CreateInputBackend(this));
#endif
#ifdef CIFACE_USE_PIPES
  m_input_backends.emplace_back(ciface::Pipes::CreateInputBackend(this));
#endif
#ifdef CIFACE_USE_DUALSHOCKUDPCLIENT
  m_input_backends.emplace_back(ciface::DualShockUDPClient::CreateInputBackend(this));
#endif
#ifdef CIFACE_USE_STEAMDECK
  m_input_backends.emplace_back(ciface::SteamDeck::CreateInputBackend(this));
#endif

  m_input_backends.emplace_back(ciface::WiimoteController::CreateInputBackend(this));

  for (auto& backend : m_input_backends)
    backend->PopulateDevices();
}

void ControllerInterface::Shutdown()
{
  assert(m_is_init);

  {
    const auto devices = GetLockedDevices();

    // Stop additional devices from being added.
    // And prevent the current ones from being referenced by UI / ControllerInterface.
    devices->is_shutting_down = true;
  }

  // Now that the Device objects are marked as unavailable, invoke callbacks.
  // UI / ControllerInterface shall immediately release any Device pointers.
  InvokeDevicesChangedCallbacks();

  // Destruct InputBackend objects to deinitialize them.
  // Some backends will remove their own Device objects as required.
  m_input_backends.clear();

  // Remove remaining devices that the backends allowed to survive.
  PerformDeviceRemoval(std::move(GetLockedDevices()->container));

  GetLockedDevices()->is_shutting_down = false;

  m_is_init = false;
}

void ControllerInterface::ChangeWindow(void* hwnd)
{
  ASSERT(m_is_init);

  std::lock_guard lk{m_update_mutex};

  // This shouldn't use render_surface so no need to update it.
  m_wsi.render_window = hwnd;

  for (auto& backend : m_input_backends)
    backend->HandleWindowChange();
}

void ControllerInterface::RefreshDevices()
{
  ASSERT(m_is_init);

  std::lock_guard lk{m_update_mutex};

  // Note: Backend implementations may perform either blocking or async refreshes.
  for (auto& backend : m_input_backends)
    backend->RefreshDevices();
}

void ControllerInterface::AddDevice(ciface::InputBackend* backend,
                                    std::shared_ptr<ciface::Core::Device> device)
{
  ASSERT(m_is_init);

  {
    auto locked_devices = GetLockedDevices();

    // We don't need this device if we are shutting down.
    if (locked_devices->is_shutting_down)
      return;

    auto& devices = locked_devices->container;

    const auto is_id_in_use = [&](int id) {
      return std::ranges::any_of(devices, [&device, &id](const DeviceEntry& entry) {
        const auto& d = entry.device;
        return d->GetSource() == device->GetSource() && d->GetName() == device->GetName() &&
               d->GetId() == id;
      });
    };

    const auto preferred_id = device->GetPreferredId();
    if (preferred_id.has_value() && !is_id_in_use(*preferred_id))
    {
      // Use the device's preferred ID if available.
      device->SetId(*preferred_id);
    }
    else
    {
      // Find the first available ID to use.
      int id = 0;
      while (is_id_in_use(id))
        ++id;

      device->SetId(id);
    }

    NOTICE_LOG_FMT(CONTROLLERINTERFACE, "Added device: {}", device->GetQualifiedName());
    devices.emplace_back(backend, std::move(device));

    // We can't (and don't want) to control the order in which devices are added, but we need
    // their order to be consistent, and we need the same one to always be the first, where present
    // (the keyboard and mouse device usually). This is because when defaulting a controller
    // profile, it will automatically select the first device in the list as its default. It would
    // be nice to sort devices by Source then Name then ID, but it's better to leave them sorted by
    // the add order. This also avoids breaking the order on other platforms that are less tested.
    std::ranges::stable_sort(devices, std::ranges::greater{}, [](const DeviceEntry& entry) {
      return entry.device->GetSortPriority();
    });
  }

  InvokeDevicesChangedCallbacks();
}

// Note: Some backends expect Device objects to be destructed immediately, before returning.
void ControllerInterface::RemoveDevices(ciface::InputBackend* backend,
                                        RemoveDevicesCallback callback)
{
  ASSERT(m_is_init);

  ContainerType devices_to_remove;
  {
    const auto devices = GetLockedDevices();

    // Take matching Device objects out of the container.
    std::erase_if(devices->container, [&](DeviceEntry& entry) {
      return (entry.backend == backend && callback(entry.device.get())) ?
                 (devices_to_remove.emplace_back(std::move(entry)), true) :
                 false;
    });

    if (devices->is_shutting_down)
    {
      // Callbacks are not needed since `Shutdown` already took care of that for us.
      PerformDeviceRemoval(std::move(devices_to_remove));
      return;
    }
  }

  // Now that the Device objects are out of the container, invoke callbacks.
  InvokeDevicesChangedCallbacks();
  PerformDeviceRemoval(std::move(devices_to_remove));
}

void ControllerInterface::PerformDeviceRemoval(ContainerType&& devices_to_remove)
{
  for (auto& [backend, device] : devices_to_remove)
    NOTICE_LOG_FMT(CONTROLLERINTERFACE, "Removing device: {}", device->GetQualifiedName());

  // Set outputs to ZERO before destroying devices to stop all rumble effects.
  for (auto& [backend, device] : devices_to_remove)
  {
    for (ciface::Core::Device::Output* o : device->Outputs())
      o->SetState(0);

    // Did our callbacks actually release all shared_ptr<Device> immediately?
    // This assumes backends don't keep their own shared_ptrs, which is currently the case.
    assert(device.use_count() == 1);
  }
}

void ControllerInterface::UpdateInput()
{
  ASSERT(m_is_init);

  // TODO: is this fighting with hotkey scheduler or what ?
  std::unique_lock lk{m_update_mutex, std::try_to_lock};
  if (!lk.owns_lock())
    return;

  if (!m_is_init)
    return;

  for (auto& backend : m_input_backends)
    backend->UpdateBeforeInput();

  // UpdateInput each Device and remove any that return DeviceRemoval::Remove.
  ContainerType devices_to_remove;

  std::erase_if(GetLockedDevices()->container, [&](auto& entry) {
    return (entry.device->UpdateInput() == ciface::Core::DeviceRemoval::Remove) ?
               (devices_to_remove.emplace_back(std::move(entry)), true) :
               false;
  });

  if (devices_to_remove.empty())
    return;

  // Now that the Device objects are out of the container, invoke callbacks.
  InvokeDevicesChangedCallbacks();
  PerformDeviceRemoval(std::move(devices_to_remove));
}

void ControllerInterface::SetCurrentInputChannel(ciface::InputChannel input_channel)
{
  tls_input_channel = input_channel;
}

ciface::InputChannel ControllerInterface::GetCurrentInputChannel()
{
  return tls_input_channel;
}

WindowSystemInfo ControllerInterface::GetWindowSystemInfo() const
{
  return m_wsi;
}

void ControllerInterface::SetAspectRatioAdjustment(float value)
{
  m_aspect_ratio_adjustment = value;
}

Common::Vec2 ControllerInterface::GetWindowInputScale() const
{
  const auto ar = m_aspect_ratio_adjustment.load();

  if (ar > 1)
    return {1.f, ar};

  return {1 / ar, 1.f};
}

void ControllerInterface::SetMouseCenteringRequested(bool center)
{
  m_requested_mouse_centering = center;
}

bool ControllerInterface::IsMouseCenteringRequested() const
{
  return m_requested_mouse_centering.load();
}
