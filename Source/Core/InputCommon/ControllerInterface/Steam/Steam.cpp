// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/Steam/Steam.h"

#pragma warning(push)
// Disable warnings for illegal characters in source code.
#pragma warning(disable : 4828)
#include "../../Externals/SteamworksSDK/public/steam/steam_api.h"
#pragma warning(pop)

#pragma comment(lib, "../../../Externals/SteamworksSDK/redistributable_bin/win64/steam_api64.lib")

#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ciface::Steam
{
static bool g_is_api_init;
static bool g_is_input_init;

static constexpr auto SOURCE_NAME = "Steam";

class GyroInput : public Core::Device::Input
{
public:
  GyroInput(const char* name, const float* state, float scale)
      : m_name{name}, m_state{*state}, m_scale(scale)
  {
  }

  ControlState GetState() const override { return m_scale * m_state; }
  std::string GetName() const override { return m_name; }

private:
  const char* m_name;
  const float& m_state;
  const float m_scale;
};

class Controller;

class VibrationOutput : public Core::Device::Output
{
public:
  VibrationOutput(const char* name, Controller* parent, unsigned short* state)
      : m_parent{*parent}, m_name{name}, m_state{*state}
  {
  }

  void SetState(ControlState value) override;

  std::string GetName() const override { return m_name; }

private:
  Controller& m_parent;
  const char* m_name;
  unsigned short& m_state;
};

class Controller : public Core::Device
{
public:
  Controller(InputHandle_t handle) : m_handle{handle}
  {
    // TODO: Apparently these values might be different depending on the controller?
    constexpr auto accel_scale = float(MathUtil::GRAVITY_ACCELERATION / 16384.f);
    constexpr auto gyro_scale = float(MathUtil::TAU / 360.f / 16.f);

    // Add motion data inputs.
    // TODO: do this conditionally.
    AddInput(new GyroInput{"Accel Up", &m_motion_data.posAccelZ, accel_scale});
    AddInput(new GyroInput{"Accel Down", &m_motion_data.posAccelZ, -accel_scale});
    AddInput(new GyroInput{"Accel Left", &m_motion_data.posAccelX, -accel_scale});
    AddInput(new GyroInput{"Accel Right", &m_motion_data.posAccelX, accel_scale});
    AddInput(new GyroInput{"Accel Forward", &m_motion_data.posAccelY, accel_scale});
    AddInput(new GyroInput{"Accel Backward", &m_motion_data.posAccelY, -accel_scale});

    AddInput(new GyroInput{"Gyro Pitch Up", &m_motion_data.rotVelX, gyro_scale});
    AddInput(new GyroInput{"Gyro Pitch Down", &m_motion_data.rotVelX, -gyro_scale});
    AddInput(new GyroInput{"Gyro Roll Left", &m_motion_data.rotVelY, gyro_scale});
    AddInput(new GyroInput{"Gyro Roll Right", &m_motion_data.rotVelY, -gyro_scale});
    AddInput(new GyroInput{"Gyro Yaw Left", &m_motion_data.rotVelZ, gyro_scale});
    AddInput(new GyroInput{"Gyro Yaw Right", &m_motion_data.rotVelZ, -gyro_scale});

    AddOutput(new VibrationOutput("Rumble Left", this, &m_rumble_left));
    AddOutput(new VibrationOutput("Rumble Right", this, &m_rumble_right));
  }

  void UpdateInput() override
  {
    // Ideally this wouldn't be called by each individual Device.
    SteamInput()->RunFrame();

    m_motion_data = SteamInput()->GetMotionData(m_handle);
  }

  void UpdateOutput() { SteamInput()->TriggerVibration(m_handle, m_rumble_left, m_rumble_right); }

  std::string GetSource() const override { return SOURCE_NAME; }
  std::string GetName() const override
  {
    switch (SteamInput()->GetInputTypeForHandle(m_handle))
    {
    case k_ESteamInputType_SteamController:
      return "Steam Controller";
    case k_ESteamInputType_XBox360Controller:
      return "XBox 360 Controller";
    case k_ESteamInputType_XBoxOneController:
      return "XBox One Controller";
    case k_ESteamInputType_GenericGamepad:
      return "Generic Gamepad";
    case k_ESteamInputType_PS3Controller:
      return "PS3 Controller";
    case k_ESteamInputType_PS4Controller:
      return "PS4 Controller";
    case k_ESteamInputType_PS5Controller:
      return "PS5 Controller";
    case k_ESteamInputType_SteamDeckController:
      return "Steam Deck Controller";
    case k_ESteamInputType_SwitchProController:
      return "Switch Pro Controller";
    default:
      return "Controller";
    }
  }
  int GetSortPriority() const override { return 0; }

private:
  unsigned short m_rumble_left{};
  unsigned short m_rumble_right{};
  InputMotionData_t m_motion_data{};
  const InputHandle_t m_handle;
};

void Init()
{
  if (!SteamAPI_Init())
  {
    ERROR_LOG(CONTROLLERINTERFACE, "SteamAPI_Init");
    return;
  }

  g_is_api_init = true;

  if (!SteamInput()->Init(true))
  {
    ERROR_LOG(CONTROLLERINTERFACE, "SteamInput()->Init");
    return;
  }

  // Failed attempts to get steam input to work before starting a game.
  SteamUtils()->SetGameLauncherMode(false);

  g_is_input_init = true;
}

void DeInit()
{
  if (g_is_input_init)
    SteamInput()->Shutdown();

  if (g_is_api_init)
    SteamAPI_Shutdown();

  g_is_input_init = false;
  g_is_api_init = false;
}

void PopulateDevices()
{
  if (!g_is_input_init)
    return;

  SteamInput()->RunFrame();

  g_controller_interface.RemoveDevice(
      [](const auto* dev) { return dev->GetSource() == SOURCE_NAME; });

  InputHandle_t controllers[STEAM_INPUT_MAX_COUNT];
  const int controllers_num = SteamInput()->GetConnectedControllers(controllers);

  for (int i = 0; i != controllers_num; ++i)
  {
    g_controller_interface.AddDevice(std::make_shared<Controller>(controllers[i]));
  }

  ERROR_LOG_FMT(CONTROLLERINTERFACE, "controller count: {}", controllers_num);
}

void VibrationOutput::SetState(ControlState value)
{
  const unsigned short new_state = std::lround(value * std::numeric_limits<unsigned short>::max());

  if (new_state == m_state)
    return;

  m_state = new_state;
  m_parent.UpdateOutput();
}

}  // namespace ciface::Steam
