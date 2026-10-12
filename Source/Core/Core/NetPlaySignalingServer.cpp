// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/NetPlaySignalingServer.h"

#include <thread>

#include "Common/Logging/Log.h"

namespace NetPlay
{

SignalingServer::SignalingServer(const std::string& ip_address, u16 port)
    : m_server{ip_address, port}
{
  m_server.ServePath("/signaling",
                     [](const Common::WebSocketRequest&, Common::WebSocket& web_socket) {
                       INFO_LOG_FMT(NETPLAY, "SignalingServer got connection !");

                       while (web_socket.IsOpen())
                       {
                         // web_socket.Read();
                         std::this_thread::sleep_for(std::chrono::milliseconds{100});
                       }
                     });
}

}  // namespace NetPlay
