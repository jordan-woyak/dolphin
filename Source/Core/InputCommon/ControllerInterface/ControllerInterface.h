// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "Common/Matrix.h"
#include "Common/WindowSystemInfo.h"
#include "InputCommon/ControllerInterface/CoreDevice.h"
#include "InputCommon/ControllerInterface/InputBackend.h"

// enable disable sources
#ifdef _WIN32
#define CIFACE_USE_WIN32
#endif
#ifdef HAVE_X11
#define CIFACE_USE_XLIB
#endif
#if defined(__APPLE__)
#define CIFACE_USE_OSX
#endif
#if defined(HAVE_LIBEVDEV) && defined(HAVE_LIBUDEV)
#define CIFACE_USE_EVDEV
#endif
#if defined(USE_PIPES)
#define CIFACE_USE_PIPES
#endif
#define CIFACE_USE_DUALSHOCKUDPCLIENT
#if defined(HAVE_SDL3)
#define CIFACE_USE_SDL
#endif
#if defined(HAVE_HIDAPI)
#define CIFACE_USE_STEAMDECK
#endif

namespace ciface
{
// A thread local "input channel" is maintained to handle the state of relative inputs.
// This allows simultaneous use of relative inputs across different input contexts.
// e.g. binding relative mouse movements to both GameCube controllers and FreeLook.
// These operate at different rates and processing one would break the other without separate state.
enum class InputChannel
{
  Host,
  SerialInterface,
  Bluetooth,
  FreeLook,
  Count,
};

}  // namespace ciface

class ControllerInterface : public ciface::Core::DeviceContainer
{
  friend ciface::InputBackend;

public:
  // Not thread safe.
  void Initialize(const WindowSystemInfo& wsi);
  void Shutdown();
  bool IsInit() const;

  void ChangeWindow(void* hwnd);
  void RefreshDevices();
  void UpdateInput();

  // Set adjustment from the full render window aspect-ratio to the drawn aspect-ratio.
  // Used to fit mouse cursor inputs to the relevant region of the render window.
  void SetAspectRatioAdjustment(float);

  // Calculated from the aspect-ratio adjustment.
  // Inputs based on window coordinates should be multiplied by this.
  Common::Vec2 GetWindowInputScale() const;

  // Request that the mouse cursor should be centered in the render window at the next opportunity.
  void SetMouseCenteringRequested(bool center);
  bool IsMouseCenteringRequested() const;

  static void SetCurrentInputChannel(ciface::InputChannel);
  static ciface::InputChannel GetCurrentInputChannel();

  WindowSystemInfo GetWindowSystemInfo() const;

private:
  void AddDevice(ciface::InputBackend*, std::shared_ptr<ciface::Core::Device>);

  // Remove devices on a particular backend for which the function returns true.
  // Each InputBackend should perform this on its own time.
  // Not every backend is prepared for random Device destruction.
  using RemoveDevicesCallback = Common::MoveOnlyFunction<bool(ciface::Core::Device*)>;
  void RemoveDevices(ciface::InputBackend*, RemoveDevicesCallback callback);

  void PerformDeviceRemoval(ContainerType&&);

  bool m_is_init{};

  std::mutex m_update_mutex;

  WindowSystemInfo m_wsi;
  std::atomic<float> m_aspect_ratio_adjustment = 1;
  std::atomic<bool> m_requested_mouse_centering = false;

  std::vector<std::unique_ptr<ciface::InputBackend>> m_input_backends;
};

namespace ciface
{
template <typename T>
class RelativeInputState
{
public:
  void Update()
  {
    const auto channel = int(ControllerInterface::GetCurrentInputChannel());

    m_value[channel] = m_delta[channel];
    m_delta[channel] = {};
  }

  T GetValue() const
  {
    const auto channel = int(ControllerInterface::GetCurrentInputChannel());

    return m_value[channel];
  }

  void Move(T delta)
  {
    for (auto& d : m_delta)
      d += delta;
  }

private:
  std::array<T, int(InputChannel::Count)> m_value;
  std::array<T, int(InputChannel::Count)> m_delta;
};
}  // namespace ciface

extern ControllerInterface g_controller_interface;
