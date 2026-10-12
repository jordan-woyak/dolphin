// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
#pragma once

#include <memory>

#include <SFML/Network/Packet.hpp>
#include <enet/enet.h>
#include <variant>

#include "Common/Buffer.h"
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

// used for traversal packets and wake-up packets
constexpr int SKIPPABLE_EVENT = 42;

// An arbitrary amount of time of no acknowledgement of sent packets before netplay decides a
// connection is disconnected
static constexpr std::chrono::milliseconds PEER_TIMEOUT{30};

class Peer
{
public:
  std::string GetHostString() const
  {
    // TODO:
    return "IP:Port";
  }

  bool SendPacket(const sf::Packet& packet, u8 channel_id);

  void Disconnect() {}

  // TODO: Remove this
  void Reset() {}

  void* user_data;

private:
  // TDOO: pimpl
  ENetPeer* m_enet_peer;
};

class Session
{
public:
  Session() : m_impl{std::make_unique<Impl>()} {}
  ~Session() = default;

  bool Host(u16 port) { return m_impl->Host(port); }

  Peer* Connect(const std::string& address, u16 port) { return m_impl->Connect(address, port); }

  // Remove this ?
  void Destroy() {}

  u16 GetPort() const { return 999; }

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
    Common::UniqueBuffer<u8> data;
  };

  using Event = std::variant<std::monostate, ConnectEvent, DisconnectEvent, ReceiveEvent>;

  Event PollEvent(DT timeout) { return {}; }

  void InterruptPollEvent() { WakeupThread(m_impl->m_server); }

private:
  struct Impl
  {
    static constexpr int CHANNEL_COUNT = 2;
    static constexpr u32 MAX_ENET_MTU = 1392;

    bool Host(u16 port)
    {
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

    Peer* Connect(const std::string& address, u16 port)
    {
      m_client = enet_host_create(nullptr, 1, CHANNEL_COUNT, 0, 0);

      if (m_client == nullptr)
      {
        // m_dialog->OnConnectionError(_trans("Could not create client."));
        return nullptr;
      }

      m_client->mtu = std::min(m_client->mtu, MAX_ENET_MTU);

      ENetAddress addr;
      enet_address_set_host(&addr, address.c_str());
      addr.port = port;

      auto* const server = enet_host_connect(m_client, &addr, CHANNEL_COUNT, 0);

      if (server == nullptr)
      {
        // m_dialog->OnConnectionError(_trans("Could not create peer."));
      }

      // Update time in milliseconds of no acknowledgment of
      // sent packets before a connection is deemed disconnected
      enet_peer_timeout(server, 0, PEER_TIMEOUT.count(), PEER_TIMEOUT.count());

      if (Config::Get(Config::NETPLAY_ENABLE_QOS))
        new_player.qos_session = Common::QoSSession(new_player.socket);

      return server;
    }

    ENetHost* m_server = nullptr;
    ENetHost* m_client = nullptr;
  };
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Common::ENet
