// Copyright 2019 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/WiimoteEmu/Dynamics.h"

#include <algorithm>
#include <cmath>

#include "Common/MathUtil.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"
#include "InputCommon/ControllerEmu/ControlGroup/Cursor.h"
#include "InputCommon/ControllerEmu/ControlGroup/Force.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUAccelerometer.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUCursor.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUGyroscope.h"
#include "InputCommon/ControllerEmu/ControlGroup/Tilt.h"

namespace
{
template <typename T>
constexpr T SmoothTransition(T x)
{
  // return 1 / (1 + std::exp(1 / (-1 + x) + 1 / x));
  // return x - std::sin(MathUtil::TAU * x) / MathUtil::TAU;
  // return (1 - std::cos(MathUtil::PI * x)) / 2;
  return (1 - std::cos(T(MathUtil::PI) * x)) / 4 + x / 2;
}
}  // namespace

namespace WiimoteEmu
{
void MotionProcessor::Step(Common::Vec3* state, float time_elapsed)
{
  // Advance times, remove if expired.
  // Combine all the smooth transitions.
  Common::Vec3 current_transitions{};
  for (auto it = m_targets.begin(); it != m_targets.end();)
  {
    if ((it->current_time += time_elapsed / it->duration) >= 1)
    {
      m_base += it->offset;
      it = m_targets.erase(it);
    }
    else
    {
      current_transitions += it->offset * SmoothTransition(it->current_time);
      ++it;
    }
  }

  // TODO: adjust m_target.. when no targets exist?

  *state = m_base + current_transitions;
}

// TODO: support different curves
void MotionProcessor::AddTarget(const Common::Vec3& target, float speed)
{
  const auto offset = target - m_target;
  m_target = target;

  const auto length = offset.Length();
  if (!length)
    return;

  MotionTarget st;
  st.offset = offset;
  st.duration = length / speed;
  st.current_time = 0;

  m_targets.emplace_back(st);
}

bool MotionProcessor::IsActive() const
{
  return !m_targets.empty();
}

Common::Quaternion ComplementaryFilter(const Common::Quaternion& gyroscope,
                                       const Common::Vec3& accelerometer, float accel_weight,
                                       const Common::Vec3& accelerometer_normal)
{
  const auto gyro_vec = gyroscope * accelerometer_normal;
  const auto normalized_accel = accelerometer.Normalized();

  const auto cos_angle = normalized_accel.Dot(gyro_vec);

  // If gyro to accel angle difference is betwen 0 and 180 degrees we make an adjustment.
  const auto abs_cos_angle = std::abs(cos_angle);
  if (abs_cos_angle > 0 && abs_cos_angle < 1)
  {
    const auto axis = gyro_vec.Cross(normalized_accel).Normalized();
    return Common::Quaternion::Rotate(std::acos(cos_angle) * accel_weight, axis) * gyroscope;
  }
  else
  {
    return gyroscope;
  }
}

void EmulateShake(PositionalState* state, ControllerEmu::Shake* const shake_group,
                  float time_elapsed)
{
  const auto intensity = float(shake_group->GetIntensity());
  const auto speed = intensity * shake_group->GetFrequency();
  const auto extent = shake_group->GetState() * intensity / 2;

  // for (std::size_t c = 0; c != extent.data.size(); ++c)
  // {
  //   if (state->position.data[c] == -extent.data[])
  // }

  // TODO: fix individual axis speed.
  // state->motion_processor.AddTarget(target_position, speed);

  state->motion_processor.Step(&state->position, time_elapsed);
}

void EmulateTilt(RotationalState* state, ControllerEmu::Tilt* const tilt_group, float time_elapsed)
{
  const auto target = tilt_group->GetState();

  // 180 degrees is currently the max tilt value.
  const ControlState roll = target.x * MathUtil::PI;
  const ControlState pitch = target.y * MathUtil::PI;

  const auto target_angle = Common::Vec3(pitch, -roll, 0);

  // For each axis, wrap around current angle if target is farther than 180 degrees.
  for (std::size_t i = 0; i != target_angle.data.size(); ++i)
  {
    auto& angle = state->angle.data[i];
    if (std::abs(angle - target_angle.data[i]) > float(MathUtil::PI))
      angle -= std::copysign(MathUtil::TAU, angle);
  }

  state->motion_processor.AddTarget(Common::Vec3(pitch, roll, 0),
                                    tilt_group->GetMaxRotationalVelocity());
  state->motion_processor.Step(&state->angle, time_elapsed);
}

void EmulateSwing(MotionState* state, ControllerEmu::Force* swing_group, float time_elapsed)
{
  const auto input_state = swing_group->GetState();
  const float max_distance = swing_group->GetMaxDistance();
  const float max_angle = swing_group->GetTwistAngle();

  // Note: Y/Z swapped because X/Y axis to the swing_group is X/Z to the wiimote.
  // X is negated because Wiimote X+ is to the left.
  const auto target_position = Common::Vec3{-input_state.x, -input_state.z, input_state.y};

  const auto xz_target_dist = Common::Vec2{target_position.x, target_position.z}.Length();
  const auto y_target_dist = std::abs(target_position.y);
  const auto target_dist = Common::Vec3{xz_target_dist, y_target_dist, xz_target_dist};

  const auto target_angle =
      Common::Vec3{-target_position.z, 0, target_position.x} / max_distance * max_angle;

  const auto speed = MathUtil::Lerp(float(swing_group->GetReturnSpeed()),
                                    float(swing_group->GetSpeed()), target_dist.Length());

  const auto angular_velocity = speed * max_angle / max_distance;

  state->motion_processor.AddTarget(target_angle, angular_velocity);
  state->motion_processor.Step(&state->angle, time_elapsed);

  Common::Vec3 new_position;
  new_position.x = std::sin(state->angle.z) * max_distance;
  new_position.z = std::sin(state->angle.x) * -max_distance;
  // TODO: fix
  new_position.y = (1 - std::cos(std::abs(state->angle.z))) * max_distance;

  state->position = new_position;
}

WiimoteCommon::AccelData ConvertAccelData(const Common::Vec3& accel, u16 zero_g, u16 one_g)
{
  const auto scaled_accel = accel * (one_g - zero_g) / float(GRAVITY_ACCELERATION);

  // 10-bit integers.
  constexpr long MAX_VALUE = (1 << 10) - 1;

  return WiimoteCommon::AccelData(
      {u16(std::clamp(std::lround(scaled_accel.x + zero_g), 0l, MAX_VALUE)),
       u16(std::clamp(std::lround(scaled_accel.y + zero_g), 0l, MAX_VALUE)),
       u16(std::clamp(std::lround(scaled_accel.z + zero_g), 0l, MAX_VALUE))});
}

void EmulatePoint(MotionState* state, ControllerEmu::Cursor* ir_group, float time_elapsed)
{
  const auto cursor = ir_group->GetState(true);

  // Nintendo recommends a distance of 1-3 meters.
  constexpr float NEUTRAL_DISTANCE = 2.f;

  // Move the wiimote a kilometer back. Camera logic will calculate tiny/invisible IR points.
  state->position.y = ir_group->IsVisible() ? NEUTRAL_DISTANCE : 1000.f;

  // When the sensor bar position is on bottom, apply the "offset" setting negatively.
  // This is kinda odd but it does seem to maintain consistent cursor behavior.
  const bool sensor_bar_on_top = Config::Get(Config::SYSCONF_SENSOR_BAR_POSITION) != 0;
  const float height = ir_group->GetVerticalOffset() * (sensor_bar_on_top ? 1 : -1);
  state->position.z = -height;

  const float yaw_scale = ir_group->GetTotalYaw() / 2;
  const float pitch_scale = ir_group->GetTotalPitch() / 2;
  const auto target_angle = Common::Vec3(pitch_scale * -cursor.y, 0, yaw_scale * -cursor.x);

  // TODO: tweak this value.
  // Higher values will be more responsive but increase rate of M+ "desync".
  // I'd rather not expose this value in the UI if not needed.
  // At this value, sync is very good and responsiveness still appears instant.
  constexpr auto angular_velocity = float(MathUtil::TAU / 2);

  state->motion_processor.AddTarget(target_angle, angular_velocity);
  state->motion_processor.Step(&state->angle, time_elapsed);
}

void EmulateIMUCursor(IMUCursorState* state, ControllerEmu::IMUCursor* imu_ir_group,
                      ControllerEmu::IMUAccelerometer* imu_accelerometer_group,
                      ControllerEmu::IMUGyroscope* imu_gyroscope_group, float time_elapsed)
{
  const auto ang_vel = imu_gyroscope_group->GetState();

  // Reset if pointing is disabled or we have no gyro data.
  if (!imu_ir_group->enabled || !ang_vel.has_value())
  {
    *state = {};
    return;
  }

  // Apply rotation from gyro data.
  const auto gyro_rotation = GetRotationFromGyroscope(*ang_vel * -1 * time_elapsed);
  state->rotation = gyro_rotation * state->rotation;

  // If we have some non-zero accel data use it to adjust gyro drift.
  constexpr auto ACCEL_WEIGHT = 0.02f;
  auto const accel = imu_accelerometer_group->GetState().value_or(Common::Vec3{});
  if (accel.LengthSquared())
    state->rotation = ComplementaryFilter(state->rotation, accel, ACCEL_WEIGHT);

  // Clamp yaw within configured bounds.
  const auto yaw = GetYaw(state->rotation);
  const auto max_yaw = float(imu_ir_group->GetTotalYaw() / 2);
  auto target_yaw = std::clamp(yaw, -max_yaw, max_yaw);

  // Handle the "Recenter" button being pressed.
  if (imu_ir_group->controls[0]->GetState<bool>())
  {
    state->recentered_pitch = GetPitch(state->rotation);
    target_yaw = 0;
  }

  // Adjust yaw as needed.
  if (yaw != target_yaw)
    state->rotation *= Common::Quaternion::RotateZ(target_yaw - yaw);

  // Normalize for floating point inaccuracies.
  state->rotation = state->rotation.Normalized();
}

Common::Quaternion GetRotationFromAcceleration(const Common::Vec3& accel)
{
  const auto normalized_accel = accel.Normalized();

  const auto angle = std::acos(normalized_accel.Dot({0, 0, 1}));
  const auto axis = normalized_accel.Cross({0, 0, 1});

  // Check that axis is non-zero to handle perfect up/down orientations.
  return Common::Quaternion::Rotate(angle, axis.LengthSquared() ? axis.Normalized() :
                                                                  Common::Vec3{0, 1, 0});
}

Common::Quaternion GetRotationFromGyroscope(const Common::Vec3& gyro)
{
  const auto length = gyro.Length();
  return length ? Common::Quaternion::Rotate(length, gyro / length) :
                  Common::Quaternion::Identity();
}

Common::Vec3 GetGyroscopeFromRotation(const Common::Quaternion& q)
{
  // Prevent division by zero or nan when angle is zero.
  if (q.data.w >= 1)
    return {};

  const auto angle = 2 * std::acos(q.data.w);
  const auto axis = Common::Vec3(q.data.x, q.data.y, q.data.z) / std::sqrt(1 - q.data.w * q.data.w);
  return axis * angle;
}

float GetPitch(const Common::Quaternion& world_rotation)
{
  const auto vec = world_rotation * Common::Vec3{0, 0, 1};
  return std::atan2(vec.y, Common::Vec2(vec.x, vec.z).Length());
}

float GetRoll(const Common::Quaternion& world_rotation)
{
  const auto vec = world_rotation * Common::Vec3{0, 0, 1};
  return std::atan2(vec.x, vec.z);
}

float GetYaw(const Common::Quaternion& world_rotation)
{
  const auto vec = world_rotation.Inverted() * Common::Vec3{0, 1, 0};
  return std::atan2(vec.x, vec.y);
}

}  // namespace WiimoteEmu
