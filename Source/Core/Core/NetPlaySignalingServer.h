// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/WebSocketServer.h"

namespace NetPlay
{

// TODO: Maybe put SDP in the name?

class SignalingClient
{
public:
  SignalingClient(const std::string& address);
  ~SignalingClient();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

class SignalingServer
{
public:
  SignalingServer(const std::string& ip_address, u16 port);

private:
  Common::WebSocketServer m_server;
};

}  // namespace NetPlay
