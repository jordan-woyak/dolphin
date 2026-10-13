// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <future>

#include "Common/WebSocketServer.h"
#include "Core/NetPlayProto.h"

namespace NetPlay
{

// TODO: Maybe put SDP in the name?

class SignalingClient
{
public:
  SignalingClient(const std::string& address);
  ~SignalingClient();

  std::future<std::unique_ptr<NetPlay::PeerConnection>> GetFuturePeerConnection();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;

  // TODO: Put inside Impl ?
  Common::WebSocketClient m_client;
};

// TODO: Change this API
std::unique_ptr<NetPlay::PeerConnection> MakeICEPeerConnection(const std::string& address);

class SignalingServer
{
public:
  SignalingServer(const std::string& ip_address, u16 port);

  // TODO: Do this differently...
  Common::HookableEvent<std::unique_ptr<NetPlay::PeerConnection>*> on_peer_connection;

private:
  void Handler(const Common::WebSocketRequest&, Common::WebSocket& web_socket);

  Common::WebSocketServer m_server;
};

}  // namespace NetPlay
