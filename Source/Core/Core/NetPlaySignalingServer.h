// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/WebSocketServer.h"

namespace NetPlay
{

class SignalingServer
{
public:
  SignalingServer(const std::string& ip_address, u16 port);

private:
  Common::WebSocketServer m_server;
};

}  // namespace NetPlay
