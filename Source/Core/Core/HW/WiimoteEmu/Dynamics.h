// Copyright 2019 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "Common/MathUtil.h"
#include "Common/Matrix.h"
#include "Core/HW/WiimoteCommon/DataReport.h"
#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"
#include "InputCommon/ControllerEmu/ControlGroup/Cursor.h"
#include "InputCommon/ControllerEmu/ControlGroup/Force.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUAccelerometer.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUCursor.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUGyroscope.h"
#include "InputCommon/ControllerEmu/ControlGroup/Tilt.h"

namespace WiimoteEmu
{
using MathUtil::GRAVITY_ACCELERATION;

// TODO: naming?
class MotionProcessor
{
public:
  void AddTarget(const Common::Vec3& target, float speed);
  void Step(Common::Vec3* state, float time_elapsed);

  // TODO: kill
  bool IsActive() const;

private:
  struct MotionTarget
  {
    Common::Vec3 offset;
    float current_time;
    float duration;
  };

  std::vector<MotionTarget> m_targets;
  Common::Vec3 m_base;
  Common::Vec3 m_target;
};

class PositionalState
{
public:
  Common::Vec3 position;

  MotionProcessor motion_processor;
};

class RotationalState
{
public:
  Common::Vec3 angle;

  MotionProcessor motion_processor;
};

struct IMUCursorState
{
  // Rotation of world around device.
  Common::Quaternion rotation = Common::Quaternion::Identity();

  float recentered_pitch = {};
};

// Contains both positional and rotational state.
struct MotionState
{
  Common::Vec3 position;
  Common::Vec3 angle;

  MotionProcessor motion_processor;
};

// Note that 'gyroscope' is rotation of world around device.
// Alternative accelerometer_normal can be supplied to correct from non-accelerometer data.
// e.g. Used for yaw/pitch correction with IR data.
Common::Quaternion ComplementaryFilter(const Common::Quaternion& gyroscope,
                                       const Common::Vec3& accelerometer, float accel_weight,
                                       const Common::Vec3& accelerometer_normal = {0, 0, 1});

// Estimate orientation from accelerometer data.
Common::Quaternion GetRotationFromAcceleration(const Common::Vec3& accel);

// Get a quaternion from current gyro data.
Common::Quaternion GetRotationFromGyroscope(const Common::Vec3& gyro);

// Produce gyroscope readings given a quaternion representing angular velocity.
Common::Vec3 GetGyroscopeFromRotation(const Common::Quaternion& rotation);

float GetPitch(const Common::Quaternion& world_rotation);
float GetRoll(const Common::Quaternion& world_rotation);
float GetYaw(const Common::Quaternion& world_rotation);

void EmulateShake(PositionalState* state, ControllerEmu::Shake* shake_group, float time_elapsed);
void EmulateTilt(RotationalState* state, ControllerEmu::Tilt* tilt_group, float time_elapsed);
void EmulateSwing(MotionState* state, ControllerEmu::Force* swing_group, float time_elapsed);
void EmulatePoint(MotionState* state, ControllerEmu::Cursor* ir_group, float time_elapsed);
void EmulateIMUCursor(IMUCursorState* state, ControllerEmu::IMUCursor* imu_ir_group,
                      ControllerEmu::IMUAccelerometer* imu_accelerometer_group,
                      ControllerEmu::IMUGyroscope* imu_gyroscope_group, float time_elapsed);

// Convert m/s/s acceleration data to the format used by Wiimote/Nunchuk (10-bit unsigned integers).
WiimoteCommon::AccelData ConvertAccelData(const Common::Vec3& accel, u16 zero_g, u16 one_g);

}  // namespace WiimoteEmu
