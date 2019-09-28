// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "InputCommon/ControllerInterface/OpenXR/OpenXR.h"

#include <openxr/openxr.h>

#include <cassert>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "Common/CommonFuncs.h"
#include "Common/Logging/Log.h"
#include "Common/OpenXR.h"

namespace ciface::OpenXR
{
static XrActionSet s_action_set;
static XrAction s_action;

std::string PathToString(XrPath path)
{
  std::uint32_t capacity = {};
  XrResult result = xrPathToString(Common::OpenXR::GetInstance(), path, 0, &capacity, nullptr);
  assert(XR_SUCCESS == result);

  std::string output(capacity - 1, '\0');
  result = xrPathToString(Common::OpenXR::GetInstance(), path, capacity, &capacity, output.data());
  assert(XR_SUCCESS == result);

  return output;
}

XrPath GetXrPath(const std::string& str)
{
  XrPath path = {};
  const XrResult result = xrStringToPath(Common::OpenXR::GetInstance(), str.c_str(), &path);
  assert(XR_SUCCESS == result);

  return path;
}

void Init()
{
  if (!Common::OpenXR::Init())
    return;

  if (!Common::OpenXR::CreateSession({"XR_MND_headless"}, nullptr))
  {
    ERROR_LOG(PAD, "ControllerInterface: OpenXR runtime does not support headless sessions. Input "
                   "will need to be configured in-game.");
    return;
  }

  XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
  std::strcpy(actionSetInfo.actionSetName, "gameplay");
  std::strcpy(actionSetInfo.localizedActionSetName, "Gameplay");

  XrResult result = xrCreateActionSet(Common::OpenXR::GetInstance(), &actionSetInfo, &s_action_set);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrCreateActionSet");

  XrActionCreateInfo action_info{XR_TYPE_ACTION_CREATE_INFO};
  action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
  std::strcpy(action_info.actionName, "action");
  std::strcpy(action_info.localizedActionName, "Action");

  // const XrPath subaction_paths[] = {
  //    GetXrPath("/user/hand/left"),
  //    GetXrPath("/user/hand/right"),
  //};

  // action_info.countSubactionPaths = uint32_t(std::size(subaction_paths));
  // action_info.subactionPaths = subaction_paths;

  result = xrCreateAction(s_action_set, &action_info, &s_action);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrCreateAction");

  std::vector<XrActionSuggestedBinding> bindings;
  // bindings.push_back({action, GetXrPath("/user/hand/left/input/grip/pose")});
  // bindings.push_back({s_action, GetXrPath("/user/hand/right/input/grip/pose")});
  // bindings.push_back({s_action, GetXrPath("/user/hand/right/input/select/click")});
  // bindings.push_back({s_action, GetXrPath("/user/hand/left/input/select/click")});
  bindings.push_back({s_action, GetXrPath("/user/gamepad/input/a/click")});

  XrInteractionProfileSuggestedBinding suggestedBindings{
      XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  // suggestedBindings.interactionProfile =
  // GetXrPath("/interaction_profiles/khr/simple_controller");
  suggestedBindings.interactionProfile =
      GetXrPath("/interaction_profiles/microsoft/xbox_controller");
  suggestedBindings.suggestedBindings = bindings.data();
  suggestedBindings.countSuggestedBindings = uint32_t(bindings.size());
  result = xrSuggestInteractionProfileBindings(Common::OpenXR::GetInstance(), &suggestedBindings);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "XrInteractionProfileSuggestedBinding");

  XrSessionActionSetsAttachInfo attach_info{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attach_info.actionSets = &s_action_set;
  attach_info.countActionSets = 1;
  result = xrAttachSessionActionSets(Common::OpenXR::GetSession(), &attach_info);
  assert(XR_SUCCESS == result);

  INFO_LOG(SERIALINTERFACE, "xrAttachSessionActionSets");
}

void PopulateDevices()
{
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
  // xrDestroyInstance(s_instance);

  INFO_LOG(SERIALINTERFACE, "xr deinit");
}

}  // namespace ciface::OpenXR
