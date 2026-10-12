// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "Common/WebSocketServer.h"

namespace NetPlay
{

class SignalingServer
{
public:
private:
  Common::WebSocketServer m_server;
};

}  // namespace NetPlay
