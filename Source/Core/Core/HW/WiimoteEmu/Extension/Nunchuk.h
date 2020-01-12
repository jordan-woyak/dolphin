// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <array>

#include "Core/HW/WiimoteCommon/WiimoteReport.h"
#include "Core/HW/WiimoteEmu/Dynamics.h"
#include "Core/HW/WiimoteEmu/Extension/Extension.h"

namespace ControllerEmu
{
class AnalogStick;
class Buttons;
class ControlGroup;
class Force;
class Tilt;
}  // namespace ControllerEmu

namespace WiimoteEmu
{
enum class NunchukGroup
{
  Buttons,
  Stick,
  Tilt,
  Swing,
  Shake,
  IMUAccelerometer,
};

class Nunchuk : public Extension1stParty
{
public:
  union ButtonFormat
  {
    u8 hex;

    struct
    {
      u8 z : 1;
      u8 c : 1;

      // TODO: verify order of these.
      // LSBs of accelerometer
      u8 acc_x_lsb : 2;
      u8 acc_y_lsb : 2;
      u8 acc_z_lsb : 2;
    };
  };
  static_assert(sizeof(ButtonFormat) == 1, "Wrong size");

  struct DataFormat
  {
    using AccelData = Common::TVec3<u16>;
    using StickValue = Common::TVec2<u8>;

    auto GetStick() const { return ControllerEmu::RawValue<StickValue, 8>({jx, jy}); }

    // Components have 10 bits of precision.
    u16 GetAccelX() const { return ax << 2 | bt.acc_x_lsb; }
    u16 GetAccelY() const { return ay << 2 | bt.acc_y_lsb; }
    u16 GetAccelZ() const { return az << 2 | bt.acc_z_lsb; }

    auto GetAccelData() const
    {
      return ControllerEmu::RawValue<AccelData, 10>{
          AccelData{GetAccelX(), GetAccelY(), GetAccelZ()}};
    }

    u8 GetButtons() const
    {
      // 0 == pressed.
      return ~bt.hex;
    }

    // joystick x, y
    u8 jx;
    u8 jy;

    // accelerometer
    u8 ax;
    u8 ay;
    u8 az;

    // buttons + accelerometer LSBs
    ButtonFormat bt;
  };
  static_assert(sizeof(DataFormat) == 6, "Wrong size");

  struct CalibrationData
  {
    using AccelData = DataFormat::AccelData;
    using StickValue = DataFormat::StickValue;

    struct Accel
    {
      // Note these are 10 bit values.
      u16 GetX() const { return x2 << 2 | x1; }
      u16 GetY() const { return y2 << 2 | y1; }
      u16 GetZ() const { return z2 << 2 | z1; }

      auto GetData() const { return AccelData{GetX(), GetY(), GetZ()}; }

      u8 x2;
      u8 y2;
      u8 z2;

      // Note: LSBs are assumed based on Wii Remote accelerometer calibration.
      // TODO: verify order of these.
      u8 z1 : 2;
      u8 y1 : 2;
      u8 x1 : 2;
      u8 : 2;
    };

    struct Stick
    {
      u8 max;
      u8 min;
      u8 center;
    };

    auto GetStick() const
    {
      return ControllerEmu::ThreePointCalibration<StickValue, 8>(
          StickValue{stick_x.min, stick_y.min}, StickValue{stick_x.center, stick_y.center},
          StickValue{stick_x.max, stick_y.max});
    }

    auto GetAcceleration() const
    {
      return ControllerEmu::TwoPointCalibration<AccelData, 10>(accel_zero_g.GetData(),
                                                               accel_one_g.GetData());
    }

    Accel accel_zero_g;
    Accel accel_one_g;
    Stick stick_x;
    Stick stick_y;

    std::array<u8, 2> checksum;
  };
  static_assert(sizeof(CalibrationData) == 16, "Wrong size");

  Nunchuk();

  void Update() override;
  bool IsButtonPressed() const override;
  void Reset() override;
  void DoState(PointerWrap& p) override;

  ControllerEmu::ControlGroup* GetGroup(NunchukGroup group);

  static constexpr u8 BUTTON_C = 0x02;
  static constexpr u8 BUTTON_Z = 0x01;

  static constexpr u8 ACCEL_ZERO_G = 0x80;
  static constexpr u8 ACCEL_ONE_G = 0xB3;

  static constexpr u8 STICK_CENTER = 0x80;
  static constexpr u8 STICK_RADIUS = 0x7F;
  static constexpr u8 STICK_GATE_RADIUS = 0x52;

  void LoadDefaults(const ControllerInterface& ciface) override;

private:
  ControllerEmu::Tilt* m_tilt;
  ControllerEmu::Force* m_swing;
  ControllerEmu::Shake* m_shake;
  ControllerEmu::Buttons* m_buttons;
  ControllerEmu::AnalogStick* m_stick;
  ControllerEmu::IMUAccelerometer* m_imu_accelerometer;

  // Dynamics:
  MotionState m_swing_state;
  RotationalState m_tilt_state;
  PositionalState m_shake_state;
};
}  // namespace WiimoteEmu
