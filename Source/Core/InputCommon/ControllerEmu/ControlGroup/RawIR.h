// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"

#include "Common/Matrix.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"

namespace ControllerEmu
{
class RawIR : public ControlGroup
{
public:
  RawIR();

  double GetPitch() const;
  double GetYaw() const;
  double GetRoll() const;

  double GetDistance() const;
  int GetObjectCount() const;

private:
  SettingValue<double> m_distance_setting;
  SettingValue<int> m_object_count_setting;
};
}  // namespace ControllerEmu
