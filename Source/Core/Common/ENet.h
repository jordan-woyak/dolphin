// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
#pragma once

#include <memory>

#include <SFML/Network/Packet.hpp>
#include <enet/enet.h>
#include <variant>

#include "Common/CommonTypes.h"

namespace Common::ENet
{
struct ENetHostDeleter
{
  void operator()(ENetHost* host) const noexcept { enet_host_destroy(host); }
};
using ENetHostPtr = std::unique_ptr<ENetHost, ENetHostDeleter>;

void WakeupThread(ENetHost* host);
int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event);
bool SendPacket(ENetPeer* socket, const sf::Packet& packet, u8 channel_id);

// used for traversal packets and wake-up packets
constexpr int SKIPPABLE_EVENT = 42;

class Peer
{
};

class Session
{
public:
  Session() : m_impl{std::make_unique<Impl>()} {}
  ~Session() = default;

  bool Host(u16 port) { return m_impl->Host(port); }

  struct ConnectEvent
  {
    Peer* peer;
  };

  struct DisconnectEvent
  {
    Peer* peer;
  };

  struct ReceiveEvent
  {
    Peer* peer;
  };

  using Event = std::variant<std::monostate, ConnectEvent, DisconnectEvent, ReceiveEvent>;

  Event PollEvent(DT timeout) { return {}; }

private:
  struct Impl
  {
    bool Host(u16 port)
    {
      static constexpr int CHANNEL_COUNT = 2;
      constexpr u32 MAX_ENET_MTU = 1392;

      ENetAddress serverAddr;
      serverAddr.host = ENET_HOST_ANY;
      serverAddr.port = port;
      m_server = enet_host_create(&serverAddr, 10, CHANNEL_COUNT, 0, 0);
      if (m_server != nullptr)
      {
        m_server->mtu = std::min(m_server->mtu, MAX_ENET_MTU);
        m_server->intercept = Common::ENet::InterceptCallback;
        return true;
      }

      return false;
    }

    ENetHost* m_server = nullptr;
  };
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Common::ENet
