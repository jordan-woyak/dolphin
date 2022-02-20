// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/OpenXR.h"

#include <openxr/openxr.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#include "Common/CommonFuncs.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"
#include "Common/Matrix.h"
#include "Common/Thread.h"
#include <VideoCommon/XFMemory.h>

namespace OpenXR
{
static XrInstance s_instance = XR_NULL_HANDLE;
static XrSystemId s_system_id = XR_NULL_SYSTEM_ID;

static std::map<XrSession, Session*> s_session_objects;
static std::mutex s_sessions_mutex;

constexpr XrViewConfigurationType VIEW_CONFIG_TYPE = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
constexpr XrQuaternionf IDENTITY_ORIENTATION = {0, 0, 0, 1};
constexpr XrVector3f IDENTITY_POSITION = {0, 0, 0};
constexpr XrPosef IDENTITY_POSE = {IDENTITY_ORIENTATION, IDENTITY_POSITION};

static std::vector<const char*> s_enabled_extensions;

static std::atomic<bool> s_run_event_thread;
static std::thread s_event_thread;

XrInstance GetInstance()
{
  return s_instance;
}

XrSystemId GetSystemId()
{
  return s_system_id;
}

bool IsInitialized()
{
  return s_instance != XR_NULL_HANDLE;
}

bool Shutdown()
{
  if (!IsInitialized())
    return true;

  s_run_event_thread = false;
  if (s_event_thread.joinable())
    s_event_thread.join();

  XrResult result = xrDestroyInstance(s_instance);

  s_instance = XR_NULL_HANDLE;

  return XR_SUCCEEDED(result);
}

void EventThreadFunc()
{
  Common::SetCurrentThreadName("OpenXR Poll Event");

  while (s_run_event_thread)
  {
    XrEventDataBuffer buffer{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult result = xrPollEvent(s_instance, &buffer);

    if (result == XR_EVENT_UNAVAILABLE)
    {
      std::this_thread::yield();
      continue;
    }

    if (XR_FAILED(result))
    {
      ERROR_LOG(VIDEO, "OpenXR: xrPollEvent: %d", result);
    }

    auto& header = *reinterpret_cast<XrEventDataBaseHeader*>(&buffer);

    switch (header.type)
    {
    case XR_TYPE_EVENT_DATA_EVENTS_LOST:
    {
      const auto& events_lost = *reinterpret_cast<const XrEventDataEventsLost*>(&buffer);
      ERROR_LOG(VIDEO, "OpenXR: Data events lost: %d", events_lost.lostEventCount);
      break;
    }
    case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
    {
      // const auto& loss_event = *reinterpret_cast<const XrEventDataInstanceLossPending*>(&buffer);
      WARN_LOG(VIDEO, "OpenXR: Instance loss pending.");
      break;
    }
    case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
    {
      const auto& state_event = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&buffer);

      std::lock_guard lk{s_sessions_mutex};

      auto it = s_session_objects.find(state_event.session);
      if (it != s_session_objects.end())
      {
        it->second->OnChangeState(state_event.state);
        INFO_LOG(VIDEO, "OpenXR: XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: %d", state_event.state);
      }
      else
      {
        WARN_LOG(VIDEO, "OpenXR: Received event for unknown session.");
      }

      break;
    }
    default:
      DEBUG_LOG(VIDEO, "OpenXR: Unhandled event: %d", header.type);
      break;
    }
  }
}

bool Init()
{
  if (IsInitialized())
    return true;

  s_enabled_extensions = {};

  uint32_t extension_count;
  xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr);
  std::vector<XrExtensionProperties> extensions(extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
  xrEnumerateInstanceExtensionProperties(nullptr, extension_count, &extension_count,
                                         extensions.data());

  for (auto& ext : extensions)
    INFO_LOG(VIDEO, "OpenXR: Available extension: %s", ext.extensionName);

  const auto IsExtensionPresent = [&](std::string_view name) {
    for (auto& ext : extensions)
    {
      if (ext.extensionName == name)
        return true;
    }

    return false;
  };

  const auto TryEnableExtension = [&](const char* name) {
    if (IsExtensionPresent(name))
    {
      s_enabled_extensions.push_back(name);
      return true;
    }

    return false;
  };

  // We'll initialize OpenXR with all available potentially used extensions.
  TryEnableExtension("XR_MND_headless");
  TryEnableExtension("XR_KHR_opengl_enable");
  TryEnableExtension("XR_KHR_vulkan_enable");

#ifdef WIN32
  TryEnableExtension("XR_KHR_D3D11_enable");
  TryEnableExtension("XR_KHR_D3D12_enable");
#endif

  XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
  info.enabledExtensionNames = s_enabled_extensions.data();
  info.enabledExtensionCount = uint32_t(s_enabled_extensions.size());

  std::strcpy(info.applicationInfo.applicationName, "Dolphin");
  info.applicationInfo.applicationVersion = 1;
  std::strcpy(info.applicationInfo.engineName, "dolphin-emu");
  info.applicationInfo.engineVersion = 1;
  info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

  XrResult result = xrCreateInstance(&info, &s_instance);
  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrCreateInstance: %d", result);
    return false;
  }

  XrInstanceProperties instance_properties{XR_TYPE_INSTANCE_PROPERTIES};
  result = xrGetInstanceProperties(s_instance, &instance_properties);
  if (XR_FAILED(result))
  {
    WARN_LOG(VIDEO, "OpenXR: xrGetInstanceProperties: %d", result);
  }
  else
  {
    INFO_LOG(VIDEO, "OpenXR: Runtime name: %s", instance_properties.runtimeName);
  }

  XrSystemGetInfo system_get_info{XR_TYPE_SYSTEM_GET_INFO};
  system_get_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  result = xrGetSystem(s_instance, &system_get_info, &s_system_id);
  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrGetSystem: %d", result);

    Shutdown();

    return false;
  }

  XrSystemProperties sys_props = {XR_TYPE_SYSTEM_PROPERTIES};
  result = xrGetSystemProperties(s_instance, s_system_id, &sys_props);
  if (XR_FAILED(result))
  {
    WARN_LOG(VIDEO, "OpenXR: xrGetSystemProperties: %d", result);
  }
  else
  {
    INFO_LOG(VIDEO, "OpenXR: System name: %s", sys_props.systemName);
  }

  s_run_event_thread = true;
  s_event_thread = std::thread(EventThreadFunc);

  return true;
}

std::unique_ptr<Session> CreateSession(const std::vector<std::string_view>& required_extensions,
                                       const void* graphics_binding,
                                       const std::vector<s64>& swapchain_formats)
{
  auto session = std::make_unique<Session>();
  if (session->Create(required_extensions, graphics_binding) &&
      session->CreateSwapchain(swapchain_formats))
  {
    return session;
  }
  else
  {
    return {};
  }
}

Session::Session() = default;

Session::~Session()
{
  Destroy();
}

bool Session::Create(const std::vector<std::string_view>& required_extensions,
                     const void* graphics_binding)
{
  Init();

  if (!IsInitialized())
    return false;

  // Fail if Create() has already been called without a matching Destroy().
  if (IsValid())
    return false;

  // FYI: OpenXR spec says zero is an invalid XrTime.
  // We won't have a valid time until after xrWaitFrame.
  m_predicted_display_time = 0;
  m_display_time = 0;
  MarkValuesDirty();

  for (auto& ext : required_extensions)
  {
    if (std::find(s_enabled_extensions.begin(), s_enabled_extensions.end(), ext) ==
        s_enabled_extensions.end())
    {
      ERROR_LOG(VIDEO, "OpenXR: Missing required extension: %s", std::string(ext).c_str());
      return false;
    }
  }

  XrSessionCreateInfo session_create_info{XR_TYPE_SESSION_CREATE_INFO};
  session_create_info.systemId = s_system_id;
  session_create_info.next = graphics_binding;

  {
    std::lock_guard lk{s_sessions_mutex};

    XrResult result = xrCreateSession(s_instance, &session_create_info, &m_session);
    if (XR_FAILED(result))
    {
      ERROR_LOG(VIDEO, "OpenXR: xrCreateSession: %d", result);
      return false;
    }

    m_is_headless_session = graphics_binding == nullptr;

    s_session_objects.insert({m_session, this});
  }


  XrReferenceSpaceCreateInfo space_create_info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  space_create_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  space_create_info.poseInReferenceSpace = IDENTITY_POSE;
  const XrResult result = xrCreateReferenceSpace(m_session, &space_create_info, &m_local_space);
  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrCreateReferenceSpace: %d", result);
    return false;
  }

  XrReferenceSpaceCreateInfo space_create_info_view{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  space_create_info_view.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  space_create_info_view.poseInReferenceSpace = IDENTITY_POSE;
  const XrResult result_view = xrCreateReferenceSpace(m_session, &space_create_info_view, &m_view_space);
  if (XR_FAILED(result_view))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrCreateReferenceSpace: %d", result);
    return false;
  }

  return true;
}

bool Session::Destroy()
{
  if (!IsValid())
    return false;

  StopWaitFrameThread();

  const XrResult result = xrDestroySession(m_session);

  {
    std::lock_guard lk{s_sessions_mutex};
    s_session_objects.erase(m_session);
  }

  m_session = XR_NULL_HANDLE;

  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrDestroySession: %d", result);
    return false;
  }

  return true;
}

bool Session::IsValid() const
{
  return m_session != XR_NULL_HANDLE;
}

bool Session::Begin()
{
  if (m_session_state != XR_SESSION_STATE_READY)
    return false;

  XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
  begin.primaryViewConfigurationType = VIEW_CONFIG_TYPE;
  const XrResult result = xrBeginSession(m_session, &begin);
  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrBeginSession: %d", result);
    return false;
  }

  if (!m_is_headless_session)
  {
    m_run_wait_frame_thread = true;
    m_wait_frame_thread = std::thread([this]() {
      Common::SetCurrentThreadName("OpenXR Wait Frame");

      while (m_run_wait_frame_thread)
      {
        WaitFrame();
      }
    });
  }

  return true;
}

void Session::StopWaitFrameThread()
{
  m_run_wait_frame_thread = false;
  if (m_wait_frame_thread.joinable())
    m_wait_frame_thread.join();
}

bool Session::End()
{
  StopWaitFrameThread();

  XrResult result = xrEndSession(m_session);
  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrEndSession: %d", result);
    return false;
  }

  return true;
}

bool Session::RequestExit()
{
  XrResult result = xrRequestExitSession(m_session);
  return XR_SUCCEEDED(result);
}

bool Session::WaitFrame()
{
  XrFrameState frame_state = XrFrameState{XR_TYPE_FRAME_STATE};
  XrResult result = xrWaitFrame(m_session, nullptr, &frame_state);

  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrWaitFrame: %d", result);
    return false;
  }

  // TODO: We could check frame_state.shouldRender but it's probably not a big deal.

  m_predicted_display_time = frame_state.predictedDisplayTime;

  return true;
}

bool Session::BeginFrame()
{
  XrResult result = xrBeginFrame(m_session, nullptr);
  return XR_SUCCEEDED(result);
}

bool Session::EndFrame()
{
  // In the rare case this hasn't been invoked throughout the rendering of the frame.
  UpdateValuesIfDirty();

  // Make sure values are updated next frame.
  MarkValuesDirty();

  // If WaitFrame has yet to complete we won't have a valid display time.
  if (!m_display_time)
  {
    return false;
  }

  XrCompositionLayerProjectionView projection_views[VIEW_COUNT] = {};
  for (u32 i = 0; i != std::size(projection_views); ++i)
  {
    auto& projection_view = projection_views[i];

    projection_view = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    projection_view.pose = m_eye_views[i].pose;
    projection_view.fov = m_eye_views[i].fov;
    projection_view.subImage.swapchain = m_swapchain;
    projection_view.subImage.imageRect = {{static_cast<int32_t>(i) * m_swapchain_size.width / 2, 0},
                                          {m_swapchain_size.width / 2, m_swapchain_size.height}};
    projection_view.subImage.imageArrayIndex = 0;
  }

  XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  layer.layerFlags = 0;
  layer.space = m_local_space;
  layer.viewCount = VIEW_COUNT;
  layer.views = projection_views;

  //XrCompositionLayerQuad quad_layer_left{XR_TYPE_COMPOSITION_LAYER_QUAD};
  //quad_layer_left.space = m_view_space;
  //quad_layer_left.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
  //quad_layer_left.subImage = projection_views[0].subImage;
  //quad_layer_left.pose = XrPosef{XrQuaternionf{0.0f, 0.0f, 0.f, 1.0f}, XrVector3f{0.0f, 0.0f, m_3d_screen_z}};
  //quad_layer_left.size = XrExtent2Df{m_3d_screen_width, m_3d_screen_height};

  //XrCompositionLayerQuad quad_layer_right{XR_TYPE_COMPOSITION_LAYER_QUAD};
  //quad_layer_right.space = m_view_space;
  //quad_layer_right.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
  //quad_layer_right.subImage = projection_views[1].subImage;
  //quad_layer_right.pose =
  //    XrPosef{XrQuaternionf{0.0f, 0.0f, 0.f, 1.0f}, XrVector3f{0.0f, 0.0f, m_3d_screen_z}};
  //quad_layer_right.size = XrExtent2Df{m_3d_screen_width, m_3d_screen_height};
  

  //const XrCompositionLayerBaseHeader* const layers[] = {
  //    reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer),
  //    reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad_layer_left),
  //    reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad_layer_right)};

   const XrCompositionLayerBaseHeader* const layers[] = {
      reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};

  XrFrameEndInfo frame_end_info{XR_TYPE_FRAME_END_INFO};
  frame_end_info.displayTime = m_display_time;
  frame_end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  frame_end_info.layerCount = uint32_t(std::size(layers));
  frame_end_info.layers = layers;

  XrResult result = xrEndFrame(m_session, &frame_end_info);

  return XR_SUCCEEDED(result);
}

bool Session::CreateSwapchain(const std::vector<s64>& supported_formats)
{
  uint32_t view_count = VIEW_COUNT;
  std::vector<XrViewConfigurationView> config_views(view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
  XrResult result = xrEnumerateViewConfigurationViews(s_instance, s_system_id, VIEW_CONFIG_TYPE,
                                                      view_count, &view_count, config_views.data());

  // We only support 2 views (one for each eye).
  if (XR_FAILED(result) || view_count != VIEW_COUNT)
  {
    ERROR_LOG(VIDEO, "OpenXR: xrEnumerateViewConfigurationViews: %d (%d)", result, view_count);
    return false;
  }

  uint32_t swapchain_format_count = 0;
  xrEnumerateSwapchainFormats(m_session, 0, &swapchain_format_count, nullptr);

  std::vector<int64_t> swapchain_formats(swapchain_format_count);
  result = xrEnumerateSwapchainFormats(m_session, swapchain_format_count, &swapchain_format_count,
                                       swapchain_formats.data());

  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrEnumerateSwapchainFormats: %d", result);
    return false;
  }

  // Require left/right views have identical sizes.
  if (config_views[0].recommendedImageRectWidth != config_views[1].recommendedImageRectWidth ||
      config_views[0].recommendedImageRectHeight != config_views[1].recommendedImageRectHeight ||
      config_views[0].recommendedSwapchainSampleCount !=
          config_views[1].recommendedSwapchainSampleCount)
  {
    ERROR_LOG(VIDEO, "OpenXR: Recommended left/right swapchain image sizes differ.");
    return false;
  }

  auto& selected_config_view = config_views[0];
  m_swapchain_size = {int32_t(selected_config_view.recommendedImageRectWidth * 2),
                      int32_t(selected_config_view.recommendedImageRectHeight)};

  const auto format = std::find_first_of(swapchain_formats.begin(), swapchain_formats.end(),
                                         supported_formats.begin(), supported_formats.end());
  if (format == swapchain_formats.end())
  {
    ERROR_LOG(VIDEO, "OpenXR: No usable swapchain formats.");
    return false;
  }

  const auto selected_swapchain_format = *format;
  m_swapchain_format = selected_swapchain_format;

  DEBUG_LOG(VIDEO, "OpenXR: Using swapchain format: %d", m_swapchain_format);

  // Create swapchain with 2 image layers (one for each view).
  XrSwapchainCreateInfo swapchain_info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  swapchain_info.createFlags = 0;
  // TODO: Are these flags sane?
  swapchain_info.usageFlags =
      XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  swapchain_info.format = selected_swapchain_format;
  swapchain_info.sampleCount = selected_config_view.recommendedSwapchainSampleCount;
  swapchain_info.width = selected_config_view.recommendedImageRectWidth * 2;
  swapchain_info.height = selected_config_view.recommendedImageRectHeight;
  swapchain_info.faceCount = 1;
  swapchain_info.arraySize = 1;
  swapchain_info.mipCount = 1;

  result = xrCreateSwapchain(m_session, &swapchain_info, &m_swapchain);
  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrCreateSwapchain: %d", result);
    return false;
  }

  INFO_LOG(VIDEO, "OpenXR: Created swapchain: %d x %d (%d samples)", swapchain_info.width,
           swapchain_info.height, swapchain_info.sampleCount);

  return true;
}

bool Session::EnumerateSwapchainImages(uint32_t count, uint32_t* capacity, void* data)
{
  const XrResult result = xrEnumerateSwapchainImages(
      m_swapchain, count, capacity, static_cast<XrSwapchainImageBaseHeader*>(data));
  return XR_SUCCEEDED(result);
}

// TODO: Allow for a timeout?
std::optional<u32> Session::AcquireAndWaitForSwapchainImage()
{
  uint32_t swapchain_image_index = 0;
  XrResult result = xrAcquireSwapchainImage(m_swapchain, nullptr, &swapchain_image_index);
  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrAcquireSwapchainImage: %d", result);
    return {};
  }

  XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait_info.timeout = XR_INFINITE_DURATION;
  result = xrWaitSwapchainImage(m_swapchain, &wait_info);
  if (XR_FAILED(result))
  {
    ERROR_LOG(VIDEO, "OpenXR: xrWaitSwapchainImage: %d", result);
    return {};
  }

  return swapchain_image_index;
}

bool Session::ReleaseSwapchainImage()
{
  const XrResult result = xrReleaseSwapchainImage(m_swapchain, nullptr);
  return XR_SUCCEEDED(result);
}

s64 Session::GetSwapchainFormat() const
{
  return m_swapchain_format;
}

XrExtent2Di Session::GetSwapchainSize() const
{
  return m_swapchain_size;
}

void Session::Set3DScreenSize(float width, float height)
{
  m_3d_screen_width = width;
  m_3d_screen_height = height;
}

void Session::Set3DScreenZ(float z)
{
  m_3d_screen_z = z;
}

Common::Matrix44 Session::GetEyeViewMatrix(int eye_index, float z_near, float z_far)
{
  using Common::Matrix33;
  using Common::Matrix44;

  UpdateValuesIfDirty();

  // TODO: Make this per-game configurable.
  const float units_per_meter = 100;

  auto& view = m_eye_views[eye_index];

  const auto& pos = view.pose.position;
  const auto& rot = view.pose.orientation;

  const auto view_matrix =
      Matrix44::Translate(Common::Vec3{pos.x, pos.y, pos.z} * units_per_meter) *
      Matrix44::FromMatrix33(Matrix33::FromQuaternion(rot.x, rot.y, rot.z, rot.w));

  const auto& fov = view.fov;

  float left = std::tan(fov.angleLeft) * z_near;
  float right = std::tan(fov.angleRight) * z_near;
  float bottom = std::tan(fov.angleDown) * z_near;
  float top = std::tan(fov.angleUp) * z_near;

  return Matrix44::FrustumD3D(left, right, bottom, top, z_near, z_far) * view_matrix.Inverted();
  //return view_matrix;
}

Common::Matrix44 Session::GetHeadMatrix()
{
  using Common::Matrix33;
  using Common::Matrix44;

  UpdateValuesIfDirty();

  // TODO: Make this per-game configurable.
  if (!(m_view_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ||
      !(m_view_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
  {
    return Matrix44::Identity();
  }
  // TODO: Make this per-game configurable.
  const float units_per_meter = 100;

  const auto& pos = m_view_location.pose.position;
  const auto& rot = m_view_location.pose.orientation;

  const auto view_matrix =
      Matrix44::Translate(Common::Vec3{pos.x, pos.y, pos.z} * units_per_meter) *
      Matrix44::FromMatrix33(Matrix33::FromQuaternion(rot.x, rot.y, rot.z, rot.w));

  return view_matrix.Inverted();
}

Common::Matrix44 Session::GetEyeViewMatrixMove2DObjects(int eye_index, float z_near, float z_far)
{
  using Common::Matrix33;
  using Common::Matrix44;

  UpdateValuesIfDirty();

  // TODO: Make this per-game configurable.
  const float units_per_meter = 100;

  auto& view = m_eye_views[eye_index];

  const auto& pos = view.pose.position;
  const auto& rot = view.pose.orientation;

  const auto view_matrix =
      Matrix44::FromMatrix33(Matrix33::FromQuaternion(rot.x, rot.y, rot.z, rot.w)) *
      Matrix44::Translate(Common::Vec3{pos.x, pos.y, pos.z} * units_per_meter) *
      Matrix44::Translate(Common::Vec3{0.0f, 0.0f, -z_far + z_near - 1.0f});

  const auto& fov = view.fov;

  float left = std::tan(fov.angleLeft) * z_near;
  float right = std::tan(fov.angleRight) * z_near;
  float bottom = std::tan(fov.angleDown) * z_near;
  float top = std::tan(fov.angleUp) * z_near;

  return Matrix44::Frustum(left, right, bottom, top, z_near, z_far) * view_matrix.Inverted();
}

Common::Matrix44 Session::GetEyeViewOnlyMatrix(int eye_index)
{
  using Common::Matrix33;
  using Common::Matrix44;

  UpdateValuesIfDirty();

  // TODO: Make this per-game configurable.
  const float units_per_meter = 1000.0f;

  auto& view = m_eye_views[eye_index];

  const auto& pos = view.pose.position;
  const auto& rot = view.pose.orientation;

  const auto view_matrix =
      Matrix44::Translate(Common::Vec3{pos.x, pos.y, pos.z} * units_per_meter) *
      Matrix44::FromMatrix33(Matrix33::FromQuaternion(rot.x, rot.y, rot.z, rot.w));

  return  view_matrix.Inverted();
}

Common::Matrix44 Session::GetProjectionOnlyMatrix(int eye_index, float z_near, float z_far)
{
  using Common::Matrix33;
  using Common::Matrix44;

  UpdateValuesIfDirty();


  auto& view = m_eye_views[eye_index];


  const auto& fov = view.fov;

  float left = std::tan(fov.angleLeft) * z_near;
  float right = std::tan(fov.angleRight) * z_near;
  float bottom = std::tan(fov.angleDown) * z_near;
  float top = std::tan(fov.angleUp) * z_near;

  return Matrix44::FrustumD3D(left, right, bottom, top, z_near, z_far);
}

float sign(float x)
{
  if (x < 0.0f)
    return -1.0f;
  else
    return 1.0f;
}

void Session::GetProjectionBounds(int eye_index, float *angleLeft, float *angleRight, float *angleUp,
                                  float *angleDown)
{
  UpdateValuesIfDirty();

  const auto& fov = m_eye_views[eye_index].fov;
  *angleLeft = fov.angleLeft;
  *angleRight = fov.angleRight;
  *angleUp = fov.angleUp;
  *angleDown = fov.angleDown;
}

void Session::ModifyProjectionMatrix(u32 projtype, Common::Matrix44 *proj, int eye_index)
{
  using Common::Matrix33;
  using Common::Matrix44;

  UpdateValuesIfDirty();

  auto& view = m_eye_views[eye_index];

  const auto& fov = view.fov;

  float left = std::tan(fov.angleLeft);
  float right = std::tan(fov.angleRight);
  float bottom = std::tan(fov.angleDown);
  float top = std::tan(fov.angleUp);
  float width = right - left;
  float height = top - bottom;

  if (projtype == GX_PERSPECTIVE)
  {
    proj->data[0] = 2.0f / width;
    proj->data[2] = (right + left) / width;
    proj->data[5] =  2.0f / height;
    proj->data[6] = (top + bottom) / height;
  }
  else
  {
    *proj = 
            Matrix44::Translate(Common::Vec3{-(right + left) / (right - left),
                                             -(top + bottom) / (top - bottom), 0.0}) *
            Matrix44::Scale(Common::Vec3{2.0f / (right - left), 2.0f / (top - bottom), 1.0f}) *
            *proj;
  }

  //Matrix44 shiftmatrix = Matrix44::Identity();
  //shiftmatrix.data[0] = 0.5f;
  //shiftmatrix.data[3] = eye_index == 0 ? -0.5f : 0.5f;
  //*proj = shiftmatrix * *proj;
}

Common::Matrix44 Session::GetTextureShiftMatrix(int eye_index)
{
  using Common::Matrix33;
  using Common::Matrix44;

  UpdateValuesIfDirty();

  auto& view = m_eye_views[eye_index];

  const auto& fov = view.fov;

  float left = std::tan(fov.angleLeft);
  float right = std::tan(fov.angleRight);
  float bottom = std::tan(fov.angleDown);
  float top = std::tan(fov.angleUp);

  //Matrix44 result = Matrix44::Scale(Common::Vec3{2.0f/(right - left), 2.0f/(top - bottom), 1.0f}) * Matrix44::Translate(Common::Vec3{-(right + left) / (right - left), -(top + bottom)/(top - bottom), 0.0});
  Matrix44 result = Matrix44::Translate( Common::Vec3{-(right + left) / (right - left), -(top + bottom) / (top - bottom), 0.0});
  return result;
}

bool Session::AreValuesDirty() const
{
  return m_eye_views[0].type != XR_TYPE_VIEW;
}

void Session::MarkValuesDirty()
{
  m_eye_views[0].type = XR_TYPE_UNKNOWN;
}

void Session::UpdateValuesIfDirty()
{
  if (!AreValuesDirty())
    return;

  // Update our display time with the most up to date prediction
  m_display_time = m_predicted_display_time;

  // If WaitFrame has yet to complete we will still not have a usable time.
  if (m_display_time)
  {
    m_eye_views.fill({XR_TYPE_VIEW});

    XrViewState view_state{XR_TYPE_VIEW_STATE};

    XrViewLocateInfo view_locate_info{XR_TYPE_VIEW_LOCATE_INFO};
    view_locate_info.viewConfigurationType = VIEW_CONFIG_TYPE;
    view_locate_info.displayTime = m_display_time;
    view_locate_info.space = m_local_space;

    uint32_t view_count = uint32_t(m_eye_views.size());

    XrResult result = xrLocateViews(m_session, &view_locate_info, &view_state, view_count,
                                    &view_count, m_eye_views.data());
    if (XR_FAILED(result))
    {
      ERROR_LOG(VIDEO, "OpenXR: xrLocateViews: %d", result);
    }

    // Sanitize the eye views if the relevant flags aren't set.
    if (!(view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
    {
      WARN_LOG(VIDEO, "OpenXR: xrLocateViews: Orientation not available.");

      for (auto& view : m_eye_views)
        view.pose.orientation = IDENTITY_ORIENTATION;
    }

    if (!(view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
    {
      WARN_LOG(VIDEO, "OpenXR: xrLocateViews: Position not available.");

      for (auto& view : m_eye_views)
        view.pose.position = IDENTITY_POSITION;
    }

    result = xrLocateSpace(m_view_space, m_local_space, m_display_time, &m_view_location);
    if (XR_FAILED(result))
    {
      ERROR_LOG(VIDEO, "OpenXR: xrLocateSpace: %d", result);
    }
  }
  else
  {
    // This would only happen on start.
    WARN_LOG(VIDEO, "OpenXR: No display time available to locate views, using fallback.");

    XrView fallback_view{XR_TYPE_VIEW};
    fallback_view.pose = IDENTITY_POSE;
    // 45 degrees.
    constexpr auto fov = float(MathUtil::TAU / 8);
    fallback_view.fov = {-fov, fov, fov, -fov};

    m_eye_views.fill(fallback_view);
  }
}

XrSessionState Session::GetState() const
{
  return m_session_state;
}

void Session::OnChangeState(XrSessionState new_state)
{
  m_session_state = new_state;

  // TODO: Perhaps this logic should take place in the render loop thread?
  switch (new_state)
  {
  case XR_SESSION_STATE_READY:
    Begin();
    break;

  case XR_SESSION_STATE_STOPPING:
    End();
    break;

  case XR_SESSION_STATE_LOSS_PENDING:
  case XR_SESSION_STATE_EXITING:
    Destroy();
    break;

  default:
    break;
  }
}

}  // namespace OpenXR
