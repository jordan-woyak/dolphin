// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/Matrix.h"
#include "Core/HW/WiimoteCommon/WiimoteReport.h"
#include "Core/HW/WiimoteEmu/Extension/Extension.h"

namespace ControllerEmu
{
class AnalogStick;
class Buttons;
class ControlGroup;
class MixedTriggers;
}  // namespace ControllerEmu

namespace WiimoteEmu
{
enum class ClassicGroup
{
  Buttons,
  Triggers,
  DPad,
  LeftStick,
  RightStick
};

class Classic : public Extension1stParty
{
public:
  union ButtonFormat
  {
    u16 hex;

    struct
    {
      u8 : 1;
      u8 rt : 1;  // right trigger
      u8 plus : 1;
      u8 home : 1;
      u8 minus : 1;
      u8 lt : 1;  // left trigger
      u8 dpad_down : 1;
      u8 dpad_right : 1;

      u8 dpad_up : 1;
      u8 dpad_left : 1;
      u8 zr : 1;
      u8 x : 1;
      u8 a : 1;
      u8 y : 1;
      u8 b : 1;
      u8 zl : 1;  // left z button
    };
  };
  static_assert(sizeof(ButtonFormat) == 2, "Wrong size");

  struct DataFormat
  {
    using StickValue = Common::TVec2<u8>;

    // 6-bit X and Y values (0-63)
    auto GetLeftStick() const { return StickValue(lx, ly); };

    // 5-bit X and Y values (0-31)
    auto GetRightStick() const { return StickValue(rx1 | rx2 << 1 | rx3 << 3, ry); };

    // 5-bit values (0-31)
    u8 GetLeftTrigger() const { return lt1 | lt2 << 3; }
    u8 GetRightTrigger() const { return rt; }

    // TODO: naming?
    auto GetLeftStickValue() const
    {
      return ControllerEmu::RawValue<StickValue, LEFT_STICK_BITS>(GetLeftStick());
    }
    auto GetRightStickValue() const
    {
      return ControllerEmu::RawValue<StickValue, RIGHT_STICK_BITS>(GetRightStick());
    }

    // TODO: magic numbers
    auto GetLeftTriggerValue() const { return ControllerEmu::RawValue<u8, 5>(GetLeftTrigger()); }
    auto GetRightTriggerValue() const { return ControllerEmu::RawValue<u8, 5>(GetRightTrigger()); }

    u8 GetButtons() const
    {
      // 0 == pressed.
      return ~bt.hex;
    }

    u8 lx : 6;  // byte 0
    u8 rx3 : 2;

    u8 ly : 6;  // byte 1
    u8 rx2 : 2;

    u8 ry : 5;
    u8 lt2 : 2;
    u8 rx1 : 1;

    u8 rt : 5;
    u8 lt1 : 3;

    ButtonFormat bt;  // byte 4, 5
  };
  static_assert(sizeof(DataFormat) == 6, "Wrong size");

  struct CalibrationData
  {
    using StickValue = DataFormat::StickValue;

    struct Stick
    {
      u8 max;
      u8 min;
      u8 center;
    };

    auto GetLeftStick() const
    {
      return ControllerEmu::ThreePointCalibration<StickValue, 8>(
          StickValue{left_stick_x.min, left_stick_y.min},
          StickValue{left_stick_x.center, left_stick_y.center},
          StickValue{left_stick_x.max, left_stick_y.max});
    }

    auto GetRightStick() const
    {
      return ControllerEmu::ThreePointCalibration<StickValue, 8>(
          StickValue{right_stick_x.min, right_stick_y.min},
          StickValue{right_stick_x.center, right_stick_y.center},
          StickValue{right_stick_x.max, right_stick_y.max});
    }

    auto GetLeftTrigger() const
    {
      // TODO: magic numbers
      return ControllerEmu::TwoPointCalibration<u8, 8>(left_trigger_zero, 0xff);
    }

    auto GetRightTrigger() const
    {
      // TODO: magic numbers
      return ControllerEmu::TwoPointCalibration<u8, 8>(right_trigger_zero, 0xff);
    }

    Stick left_stick_x;
    Stick left_stick_y;
    Stick right_stick_x;
    Stick right_stick_y;

    u8 left_trigger_zero;
    u8 right_trigger_zero;

    std::array<u8, 2> checksum;
  };
  static_assert(sizeof(CalibrationData) == 16, "Wrong size");

  Classic();

  void Update() override;
  bool IsButtonPressed() const override;
  void Reset() override;

  ControllerEmu::ControlGroup* GetGroup(ClassicGroup group);

  static constexpr u16 PAD_RIGHT = 0x80;
  static constexpr u16 PAD_DOWN = 0x40;
  static constexpr u16 TRIGGER_L = 0x20;
  static constexpr u16 BUTTON_MINUS = 0x10;
  static constexpr u16 BUTTON_HOME = 0x08;
  static constexpr u16 BUTTON_PLUS = 0x04;
  static constexpr u16 TRIGGER_R = 0x02;
  static constexpr u16 NOTHING = 0x01;
  static constexpr u16 BUTTON_ZL = 0x8000;
  static constexpr u16 BUTTON_B = 0x4000;
  static constexpr u16 BUTTON_Y = 0x2000;
  static constexpr u16 BUTTON_A = 0x1000;
  static constexpr u16 BUTTON_X = 0x0800;
  static constexpr u16 BUTTON_ZR = 0x0400;
  static constexpr u16 PAD_LEFT = 0x0200;
  static constexpr u16 PAD_UP = 0x0100;

  static constexpr u8 CAL_STICK_CENTER = 0x80;
  static constexpr u8 CAL_STICK_RANGE = 0x7f;
  static constexpr int CAL_STICK_BITS = 8;

  static constexpr int LEFT_STICK_BITS = 6;
  static constexpr u8 LEFT_STICK_CENTER_X = CAL_STICK_CENTER >> (CAL_STICK_BITS - LEFT_STICK_BITS);
  static constexpr u8 LEFT_STICK_CENTER_Y = CAL_STICK_CENTER >> (CAL_STICK_BITS - LEFT_STICK_BITS);
  static constexpr u8 LEFT_STICK_RADIUS = CAL_STICK_RANGE >> (CAL_STICK_BITS - LEFT_STICK_BITS);

  static constexpr int RIGHT_STICK_BITS = 5;
  static constexpr u8 RIGHT_STICK_CENTER_X = CAL_STICK_CENTER >>
                                             (CAL_STICK_BITS - RIGHT_STICK_BITS);
  static constexpr u8 RIGHT_STICK_CENTER_Y = CAL_STICK_CENTER >>
                                             (CAL_STICK_BITS - RIGHT_STICK_BITS);
  static constexpr u8 RIGHT_STICK_RADIUS = CAL_STICK_RANGE >> (CAL_STICK_BITS - RIGHT_STICK_BITS);

  static constexpr u8 LEFT_TRIGGER_RANGE = 0x1F;
  static constexpr u8 RIGHT_TRIGGER_RANGE = 0x1F;

  static constexpr u8 STICK_GATE_RADIUS = 0x16;

private:
  ControllerEmu::Buttons* m_buttons;
  ControllerEmu::MixedTriggers* m_triggers;
  ControllerEmu::Buttons* m_dpad;
  ControllerEmu::AnalogStick* m_left_stick;
  ControllerEmu::AnalogStick* m_right_stick;
};
}  // namespace WiimoteEmu
