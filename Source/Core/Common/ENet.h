// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
#pragma once

#include <SFML/Network/Packet.hpp>

#include <memory>
#include <variant>

#include "Common/Buffer.h"
#include "Common/CommonTypes.h"

namespace Common::ENet
{
// struct ENetHostDeleter
// {
//   void operator()(ENetHost* host) const noexcept { enet_host_destroy(host); }
// };
// using ENetHostPtr = std::unique_ptr<ENetHost, ENetHostDeleter>;

// used for wake-up packets
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
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

class Session
{
public:
  Session();
  ~Session();

  bool Host(u16 port);

  Peer* Connect(const std::string& address, u16 port);

  // Remove this ?
  void Destroy() {}

  u16 GetPort() const { return 999; }

  void Flush();

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

  void InterruptPollEvent();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Common::ENet
