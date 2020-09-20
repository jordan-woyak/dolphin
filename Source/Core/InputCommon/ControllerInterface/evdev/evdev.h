// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <libevdev/libevdev.h>
#include <string>
#include <vector>

#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ciface::evdev
{
void Init();
void PopulateDevices();
void Shutdown();

class evdevDevice : public Core::Device
{
public:
  void UpdateInput() override;
  bool IsValid() const override;

  ~evdevDevice();

  // Return true if node was "interesting".
  bool AddNode(std::string devnode, int fd, libevdev* dev);

  const char* GetUniqueID() const;

  std::string GetName() const override { return m_name; }
  std::string GetSource() const override { return "evdev"; }

private:
  std::string m_name;

  struct Node
  {
    std::string devnode;
    int fd;
    libevdev* device;
  };

  std::vector<Node> m_nodes;
};
}  // namespace ciface::evdev
