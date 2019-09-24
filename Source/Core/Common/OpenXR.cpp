// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/OpenXR.h"

#include <openxr/openxr.h>

#include <cassert>

#include "Common/CommonFuncs.h"
#include "Common/Logging/Log.h"

namespace Common::OpenXR
{
static XrInstance s_instance;
static XrSystemId s_system_id;
static XrSession s_session;
// static XrActionSet s_action_set;
// static XrAction s_action;
XrSwapchain s_swapchain;
int64_t s_swapchain_format;

constexpr XrViewConfigurationType VIEW_CONFIG_TYPE = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

bool Init(const std::vector<std::string>& required_extensions)
{
  // uint32_t extensionCount;
  // xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
  // std::vector<XrExtensionProperties> extensionProperties(extensionCount,
  //                                                       {XR_TYPE_EXTENSION_PROPERTIES});
  // xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount,
  //                                                   extensionProperties.data());

  // for (auto& ext : extensionProperties)
  //  std::cout << "extension: " << ext.extensionName << std::endl;

  // TODO: extension based on gfx backend
  // TODO: test for EXTs

  XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
  const char* const ext_names[] = {
      //"XR_MND_headless",
      "XR_KHR_D3D11_enable",
      //"XR_EXT_debug_utils",
  };
  info.enabledExtensionNames = ext_names;
  info.enabledExtensionCount = uint32_t(std::size(ext_names));

  // info.applicationInfo = {"dolphin-emu", 1, "dolphin-emu engine", 1, XR_CURRENT_API_VERSION};

  std::strcpy(info.applicationInfo.applicationName, "dolphin-emu");
  info.applicationInfo.applicationVersion = 1;
  std::strcpy(info.applicationInfo.engineName, "dolphin-emu engine");
  info.applicationInfo.engineVersion = 1;
  info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

  XrResult result = xrCreateInstance(&info, &s_instance);

  INFO_LOG(SERIALINTERFACE, "xrCreateInstance: %d", result);
  assert(XR_SUCCESS == result);

  XrSystemGetInfo systemGetInfo{XR_TYPE_SYSTEM_GET_INFO};
  systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  result = xrGetSystem(s_instance, &systemGetInfo, &s_system_id);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrGetSystem");

  return true;
}

bool CreateSession(const void* graphics_binding)
{
  // TODO: kill/user proper strings
  Init({});

  XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
  sessionCreateInfo.systemId = s_system_id;
  sessionCreateInfo.next = graphics_binding;

  const XrResult result = xrCreateSession(s_instance, &sessionCreateInfo, &s_session);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrCreateSession");

  // TODO: kill
  WaitForReady();
  BeginSession();

  return true;
}

bool WaitForReady()
{
  XrSessionState session_state = XR_SESSION_STATE_IDLE;

  while (session_state != XR_SESSION_STATE_READY)
  {
    XrEventDataBuffer buffer{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult result = xrPollEvent(s_instance, &buffer);

    if (result == XR_EVENT_UNAVAILABLE)
      continue;

    if (result != XR_SUCCESS)
      assert(false);

    auto& header = *reinterpret_cast<XrEventDataBaseHeader*>(&buffer);

    INFO_LOG(SERIALINTERFACE, "xrPollEvent");

    switch (header.type)
    {
    case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
    {
      const auto& stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&buffer);
      session_state = stateEvent.state;
      INFO_LOG(SERIALINTERFACE, "XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: %d", stateEvent.state);
      break;
    }

    default:
      INFO_LOG(SERIALINTERFACE, "event: %d", header.type);
      break;
    }
  }

  return true;
}

bool BeginSession()
{
  XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
  begin.primaryViewConfigurationType = VIEW_CONFIG_TYPE;
  const XrResult result = xrBeginSession(s_session, &begin);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrBeginSession");

  return true;
}

// TODO: kill
XrFrameState s_frame_state;

bool WaitFrame()
{
  s_frame_state = XrFrameState{XR_TYPE_FRAME_STATE};
  XrResult result = xrWaitFrame(s_session, nullptr, &s_frame_state);
  assert(XR_SUCCESS == result);
  // INFO_LOG(SERIALINTERFACE, "xrWaitFrame");

  while (true)
  {
    XrEventDataBuffer buffer{XR_TYPE_EVENT_DATA_BUFFER};
    result = xrPollEvent(s_instance, &buffer);

    if (result == XR_EVENT_UNAVAILABLE)
      break;

    if (result != XR_SUCCESS)
    {
      INFO_LOG(SERIALINTERFACE, "xrPollEvent: %d", result);
      break;
    }

    auto& header = *reinterpret_cast<XrEventDataBaseHeader*>(&buffer);

    switch (header.type)
    {
    case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
    {
      const auto& stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&buffer);
      //session_state = stateEvent.state;
      INFO_LOG(SERIALINTERFACE, "XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: %d", stateEvent.state);
      break;
    }

    default:
      INFO_LOG(SERIALINTERFACE, "event: %d", header.type);
      break;
    }
  }

  return true;
}

bool BeginFrame()
{
  XrResult result = xrBeginFrame(s_session, nullptr);
  assert(XR_SUCCESS == result);
  // INFO_LOG(SERIALINTERFACE, "xrBeginFrame");

  return true;
}

bool EndFrame()
{
  XrFrameEndInfo frame_end_info{XR_TYPE_FRAME_END_INFO};
  // TODO: accept display time argument
  frame_end_info.displayTime = s_frame_state.predictedDisplayTime;
  frame_end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  const XrResult result = xrEndFrame(s_session, &frame_end_info);
  assert(XR_SUCCESS == result);
  return true;
}

bool CreateSwapchain()
{
  // TODO: check results:

  uint32_t view_count;
  XrResult result = xrEnumerateViewConfigurationViews(s_instance, s_system_id, VIEW_CONFIG_TYPE, 0,
                                                      &view_count, nullptr);
  // PRIMARY_STEREO view configuration always has 2 views
  assert(view_count == 2);

  std::vector<XrViewConfigurationView> config_views(view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
  result = xrEnumerateViewConfigurationViews(s_instance, s_system_id, VIEW_CONFIG_TYPE, view_count,
                                             &view_count, config_views.data());

  uint32_t swapchain_format_count = 0;
  result = xrEnumerateSwapchainFormats(s_session, 0, &swapchain_format_count, nullptr);

  std::vector<int64_t> swapchain_formats(swapchain_format_count);
  result = xrEnumerateSwapchainFormats(s_session, swapchain_format_count, &swapchain_format_count,
                                       swapchain_formats.data());

  // Require left/right views have identical sizes
  assert(config_views[0].recommendedImageRectWidth == config_views[1].recommendedImageRectWidth);
  assert(config_views[0].recommendedImageRectHeight == config_views[1].recommendedImageRectHeight);
  assert(config_views[0].recommendedSwapchainSampleCount ==
         config_views[1].recommendedSwapchainSampleCount);

  auto& selected_config_view = config_views[0];

  // TODO: fix
  auto& selected_swapchain_format = swapchain_formats[0];
  s_swapchain_format = selected_swapchain_format;
  assert(s_swapchain_format == 28);

  INFO_LOG(SERIALINTERFACE, "xrEnumerateSwapchainFormats: %d", s_swapchain_format);

  // Create swapchain with texture array of size view_count.
  XrSwapchainCreateInfo swapchain_info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  swapchain_info.createFlags = 0;
  swapchain_info.usageFlags =
      XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  swapchain_info.format = selected_swapchain_format;
  swapchain_info.sampleCount = selected_config_view.recommendedSwapchainSampleCount;
  swapchain_info.width = selected_config_view.recommendedImageRectWidth;
  swapchain_info.height = selected_config_view.recommendedImageRectHeight;
  swapchain_info.faceCount = 1;
  swapchain_info.arraySize = view_count;
  swapchain_info.mipCount = 1;

  result = xrCreateSwapchain(s_session, &swapchain_info, &s_swapchain);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrCreateSwapchain");

  return true;
}

bool EnumerateSwapchainImages(uint32_t count, uint32_t* capacity, void* data)
{
  XrResult result = xrEnumerateSwapchainImages(s_swapchain, count, capacity,
                                               static_cast<XrSwapchainImageBaseHeader*>(data));
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrEnumerateSwapchainImages");

  return true;
}

uint32_t AquireAndWaitForSwapchainImage()
{
  uint32_t swapchain_image_index = 0;
  XrResult result = xrAcquireSwapchainImage(s_swapchain, nullptr, &swapchain_image_index);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrAcquireSwapchainImage");

  XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait_info.timeout = XR_INFINITE_DURATION;
  result = xrWaitSwapchainImage(s_swapchain, &wait_info);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrWaitSwapchainImage");

  return swapchain_image_index;
}

bool ReleaseSwapchainImage()
{
  const XrResult result = xrReleaseSwapchainImage(s_swapchain, nullptr);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrReleaseSwapchainImage");

  return true;
}

int64_t GetSwapchainFormat()
{
  return s_swapchain_format;
}

}  // namespace Common::OpenXR
