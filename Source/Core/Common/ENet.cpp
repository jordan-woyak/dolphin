// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/ENet.h"

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

namespace Common::ENet
{

// void WakeupThread(ENetHost* host);
// int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event);

// void WakeupThread(ENetHost* host)
// {
//   // Send ourselves a spurious message.  This is hackier than it should be.
//   // comex reported this as https://github.com/lsalzman/enet/issues/23, so
//   // hopefully there will be a better way to do it in the future.
//   ENetAddress address;
//   if (host->address.port != 0)
//     address.port = host->address.port;
//   else
//     enet_socket_get_address(host->socket, &address);
//   address.host = 0x0100007f;  // localhost
//   u8 byte = 0;
//   ENetBuffer buf;
//   buf.data = &byte;
//   buf.dataLength = 1;
//   enet_socket_send(host->socket, &address, &buf, 1);
// }

// int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event)
// {
//   // wakeup packet received
//   if (host->receivedDataLength == 1 && host->receivedData[0] == 0)
//   {
//     event->type = static_cast<ENetEventType>(SKIPPABLE_EVENT);
//     return 1;
//   }
//   return 0;
// }

struct Peer::Impl
{
  // ENetPeer* m_enet_peer;
};

bool Peer::SendPacket(const sf::Packet& packet, u8 channel_id)
{
  // if (!m_impl->m_enet_peer)
  // {
  //   ERROR_LOG_FMT(NETPLAY, "Target socket is null.");
  //   return false;
  // }

  // ENetPacket* epac =
  //     enet_packet_create(packet.getData(), packet.getDataSize(), ENET_PACKET_FLAG_RELIABLE);
  // if (!epac)
  // {
  //   ERROR_LOG_FMT(NETPLAY, "Failed to create ENetPacket ({} bytes).", packet.getDataSize());
  //   return false;
  // }

  // const int result = enet_peer_send(m_impl->m_enet_peer, channel_id, epac);
  // if (result != 0)
  // {
  //   ERROR_LOG_FMT(NETPLAY, "Failed to send ENetPacket (error code {}).", result);
  //   return false;
  // }

  return true;
}

struct Session::Impl
{
  static constexpr int CHANNEL_COUNT = 2;
  static constexpr u32 MAX_ENET_MTU = 1392;

  bool Host(u16 port)
  {
    // ENetAddress serverAddr;
    // serverAddr.host = ENET_HOST_ANY;
    // serverAddr.port = port;
    // m_server = enet_host_create(&serverAddr, 10, CHANNEL_COUNT, 0, 0);
    // if (m_server != nullptr)
    // {
    //   m_server->mtu = std::min(m_server->mtu, MAX_ENET_MTU);
    //   m_server->intercept = Common::ENet::InterceptCallback;
    //   return true;
    // }

    return false;
  }

  Peer* Connect(const std::string& address, u16 port)
  {
    // m_client = enet_host_create(nullptr, 1, CHANNEL_COUNT, 0, 0);

    // if (m_client == nullptr)
    // {
    //   // m_dialog->OnConnectionError(_trans("Could not create client."));
    //   return nullptr;
    // }

    // m_client->mtu = std::min(m_client->mtu, MAX_ENET_MTU);

    // ENetAddress addr;
    // enet_address_set_host(&addr, address.c_str());
    // addr.port = port;

    // auto* const server = enet_host_connect(m_client, &addr, CHANNEL_COUNT, 0);

    // if (server == nullptr)
    // {
    //   // m_dialog->OnConnectionError(_trans("Could not create peer."));
    // }

    // // Update time in milliseconds of no acknowledgment of
    // // sent packets before a connection is deemed disconnected
    // enet_peer_timeout(server, 0, PEER_TIMEOUT.count(), PEER_TIMEOUT.count());

    // // return server;
    // return nullptr;
  }

  void Flush()
  {
    // enet_host_flush(m_client);
  }

  // TODO: Fix this nonsense..
  // ENetHost* m_server = nullptr;
  // ENetHost* m_client = nullptr;
};

Session::Session() : m_impl{std::make_unique<Impl>()}
{
}

Session::~Session() = default;

bool Session::Host(u16 port)
{
  return m_impl->Host(port);
}

Peer* Session::Connect(const std::string& address, u16 port)
{
  return m_impl->Connect(address, port);
}

void Session::Flush()
{
  m_impl->Flush();
}

void Session::InterruptPollEvent()
{
  // WakeupThread(m_impl->m_server);
}

}  // namespace Common::ENet
