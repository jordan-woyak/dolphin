// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "InputCommon/ControllerInterface/OpenXR/OpenXR.h"

#include <cassert>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
// TODO: remove
#include <iostream>

#include <d3d11.h>

#pragma comment(lib, "D3D11.lib")

#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr_platform.h>

#include "Common/CommonFuncs.h"
#include "Common/Logging/Log.h"

namespace ciface::OpenXR
{
static XrInstance s_instance;
static XrSystemId s_system_id;
static XrSession s_session;
static XrActionSet s_action_set;
static XrAction s_action;

std::string PathToString(XrPath path)
{
  std::uint32_t capacity = {};
  XrResult result = xrPathToString(s_instance, path, 0, &capacity, nullptr);
  assert(XR_SUCCESS == result);

  std::string output(capacity - 1, '\0');
  result = xrPathToString(s_instance, path, capacity, &capacity, output.data());
  assert(XR_SUCCESS == result);

  return output;
}

XrPath GetXrPath(const std::string& str)
{
  XrPath path = {};
  const XrResult result = xrStringToPath(s_instance, str.c_str(), &path);
  assert(XR_SUCCESS == result);

  return path;
}

void Init()
{
  // uint32_t extensionCount;
  // xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
  // std::vector<XrExtensionProperties> extensionProperties(extensionCount,
  //                                                       {XR_TYPE_EXTENSION_PROPERTIES});
  // xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount,
  //                                                   extensionProperties.data());

  // for (auto& ext : extensionProperties)
  //  std::cout << "extension: " << ext.extensionName << std::endl;

  XrInstanceCreateInfo info = {};
  info.type = XR_TYPE_INSTANCE_CREATE_INFO;
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
  INFO_LOG(SERIALINTERFACE, "xrCreateInstance");

  XrSystemGetInfo systemGetInfo = {};
  systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
  systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

  result = xrGetSystem(s_instance, &systemGetInfo, &s_system_id);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrGetSystem");

  ID3D11Device* device = {};
  const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, 0, nullptr, 0,
                                       D3D11_SDK_VERSION, &device, nullptr, nullptr);
  assert(SUCCEEDED(hr));

  XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
  graphicsBinding.device = device;

  XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
  sessionCreateInfo.systemId = s_system_id;
  sessionCreateInfo.next = &graphicsBinding;

  result = xrCreateSession(s_instance, &sessionCreateInfo, &s_session);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrCreateSession");

  XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
  std::strcpy(actionSetInfo.actionSetName, "gameplay");
  std::strcpy(actionSetInfo.localizedActionSetName, "Gameplay");

  result = xrCreateActionSet(s_instance, &actionSetInfo, &s_action_set);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrCreateActionSet");

  XrActionCreateInfo action_info{XR_TYPE_ACTION_CREATE_INFO};
  action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
  std::strcpy(action_info.actionName, "action");
  std::strcpy(action_info.localizedActionName, "Action");

   const XrPath subaction_paths[] = {
      GetXrPath("/user/hand/left"),
      GetXrPath("/user/hand/right"),
  };

   action_info.countSubactionPaths = uint32_t(std::size(subaction_paths));
   action_info.subactionPaths = subaction_paths;

  result = xrCreateAction(s_action_set, &action_info, &s_action);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrCreateAction");

  std::vector<XrActionSuggestedBinding> bindings;
  // bindings.push_back({action, GetXrPath("/user/hand/left/input/grip/pose")});
  // bindings.push_back({s_action, GetXrPath("/user/hand/right/input/grip/pose")});
  bindings.push_back({s_action, GetXrPath("/user/hand/right/input/select/click")});
  bindings.push_back({s_action, GetXrPath("/user/hand/left/input/select/click")});
  //bindings.push_back({s_action, GetXrPath("/user/gamepad/input/a/click")});

  XrInteractionProfileSuggestedBinding suggestedBindings{
      XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  suggestedBindings.interactionProfile = GetXrPath("/interaction_profiles/khr/simple_controller");
  //suggestedBindings.interactionProfile = GetXrPath("/interaction_profiles/microsoft/xbox_controller");
  suggestedBindings.suggestedBindings = bindings.data();
  suggestedBindings.countSuggestedBindings = uint32_t(bindings.size());
  result = xrSuggestInteractionProfileBindings(s_instance, &suggestedBindings);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "XrInteractionProfileSuggestedBinding");

  XrSessionActionSetsAttachInfo attach_info{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attach_info.actionSets = &s_action_set;
  attach_info.countActionSets = 1;
  result = xrAttachSessionActionSets(s_session, &attach_info);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrAttachSessionActionSets");

  auto func = [] {
    //std::this_thread::sleep_for(std::chrono::seconds(3));

    XrSessionState session_state = XR_SESSION_STATE_IDLE;

    while (session_state != XR_SESSION_STATE_READY)
    {
      XrEventDataBuffer buffer = {XR_TYPE_EVENT_DATA_BUFFER};
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

    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrResult result = xrBeginSession(s_session, &begin);
    assert(XR_SUCCESS == result);

    INFO_LOG(SERIALINTERFACE, "xrBeginSession");

    while (true)
    {
      XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
      XrFrameState frame_state{XR_TYPE_FRAME_STATE};
      result = xrWaitFrame(s_session, &wait_info, &frame_state);
      assert(XR_SUCCESS == result);
      INFO_LOG(SERIALINTERFACE, "xrWaitFrame");

      XrFrameBeginInfo frame_info{XR_TYPE_FRAME_BEGIN_INFO};
      result = xrBeginFrame(s_session, &frame_info);
      assert(XR_SUCCESS == result);
      INFO_LOG(SERIALINTERFACE, "xrBeginFrame");

      XrActiveActionSet activeActionSet{s_action_set, XR_NULL_PATH};
      XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
      syncInfo.countActiveActionSets = 1;
      syncInfo.activeActionSets = &activeActionSet;
      result = xrSyncActions(s_session, &syncInfo);
      INFO_LOG(SERIALINTERFACE, "xrSyncActions: %d", result);
      assert(XR_SUCCESS == result || XR_SESSION_NOT_FOCUSED == result);

      XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
      XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
      getInfo.action = s_action;
      //getInfo.subactionPath = GetXrPath("/user/hand/right");
      result = xrGetActionStateBoolean(s_session, &getInfo, &state);
      assert(XR_SUCCESS == result);
      INFO_LOG(SERIALINTERFACE, "xrGetActionStateBoolean");

      XrFrameEndInfo frame_end_info{XR_TYPE_FRAME_END_INFO};
      frame_end_info.displayTime = frame_state.predictedDisplayTime;
      frame_end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
      result = xrEndFrame(s_session, &frame_end_info);
      assert(XR_SUCCESS == result);
      INFO_LOG(SERIALINTERFACE, "xrEndFrame");

      INFO_LOG(SERIALINTERFACE, "bool active: %d state: %d", state.isActive, state.currentState);
    }
  };

  std::thread t(func);
  t.detach();
}

void PopulateDevices()
{
  uint32_t extensionCount;
  xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
  std::vector<XrExtensionProperties> extensionProperties(extensionCount,
                                                         {XR_TYPE_EXTENSION_PROPERTIES});
  xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount,
                                         extensionProperties.data());

  for (auto& ext : extensionProperties)
    INFO_LOG(SERIALINTERFACE, "extension: %s", ext.extensionName);

  XrSystemProperties sys_props = {};
  sys_props.type = XR_TYPE_SYSTEM_PROPERTIES;
  XrResult result = xrGetSystemProperties(s_instance, s_system_id, &sys_props);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "sys name: %s", sys_props.systemName);
  INFO_LOG(SERIALINTERFACE, "vendor ID: %d", sys_props.vendorId);

  // XrPath user_path = {};
  // xrStringToPath(s_instance, "/user/hand/gamepad", &user_path);

  // XrInteractionProfileState profile = {};
  // profile.type = XR_TYPE_INTERACTION_PROFILE_STATE;

  // result = xrGetCurrentInteractionProfile(s_session, user_path, &profile);
  // assert(XR_SUCCESS == result);

  // INFO_LOG(SERIALINTERFACE, "xrGetCurrentInteractionProfile: %s",
  //         PathToString(profile.interactionProfile).c_str());

  INFO_LOG(SERIALINTERFACE, "xr populate");
}

void DeInit()
{
  xrDestroyInstance(s_instance);

  INFO_LOG(SERIALINTERFACE, "xr deinit");
}

}  // namespace ciface::OpenXR
