// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerEmu/ControlGroup/RawIR.h"

#include "Common/Common.h"
#include "Common/MathUtil.h"

namespace ControllerEmu
{
// i18n: "IR" refers to the infrared camera of a Wii Remote.
RawIR::RawIR() : ControlGroup("RawIR", _trans("IR"), GroupType::RawIR)
{
  AddInput(Translatability::Translate, _trans("Up"));
  AddInput(Translatability::Translate, _trans("Down"));
  AddInput(Translatability::Translate, _trans("Left"));
  AddInput(Translatability::Translate, _trans("Right"));
  AddInput(Translatability::Translate, _trans("Roll Left"));
  AddInput(Translatability::Translate, _trans("Roll Right"));

  AddSetting(&m_distance_setting,
             {_trans("Distance"),
              // i18n: The symbol/abbreviation for meters.
              _trans("m"),
              //
              _trans("Distance between Wii Remote and Sensor Bar.")},
             2, -1, 100);

  AddSetting(&m_object_count_setting,
             {_trans("Object Count"), _trans(" "),
              _trans("Number of tracked infrared objects (normally two).")},
             2, 0, 4);
}

double RawIR::GetPitch() const
{
  return controls[1]->GetState() - controls[0]->GetState();
}

double RawIR::GetYaw() const
{
  return controls[3]->GetState() - controls[2]->GetState();
}

double RawIR::GetRoll() const
{
  return controls[5]->GetState() - controls[4]->GetState();
}

double RawIR::GetDistance() const
{
  return m_distance_setting.GetValue();
}

int RawIR::GetObjectCount() const
{
  return m_object_count_setting.GetValue();
}

}  // namespace ControllerEmu
