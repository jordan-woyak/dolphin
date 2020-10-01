// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <vector>

#include "Common/Matrix.h"

class ScreenObjectTracker
{
public:
  void OnFrameAdvance();

  void AddObject(Common::Vec3 position);

private:
  struct Object
  {
    Common::Vec3 position;
  };

  std::vector<Object> m_objects;
};
