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
  SignalingClient();
  ~SignalingClient();

  // TODO: Allow this to work with NetPlay index.
  std::future<std::shared_ptr<NetPlay::PeerConnection>>
  MakePeerConnection(const std::string& address, u16 port);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

// TODO: Allow this to use NetPlayIndex to communicate new connections.
class SignalingServer
{
public:
  using ConnectionCallbackType = std::function<void(
      std::shared_ptr<NetPlay::PeerConnection> new_peer, std::span<const u8> first_message)>;

  void SetNewPeerConnectionCallback(ConnectionCallbackType callback)
  {
    m_new_peer_connection_callback = std::move(callback);
  }

  void Start(const std::string& ip_address, u16 port);

private:
  void Handler(Common::WebSocketRequest&);

  ConnectionCallbackType m_new_peer_connection_callback;
  Common::WebSocketServer m_server;
};

}  // namespace NetPlay
