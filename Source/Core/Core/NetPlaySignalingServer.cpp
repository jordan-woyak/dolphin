// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/NetPlaySignalingServer.h"

#include "Common/BitUtils.h"
#include "Common/Logging/Log.h"

namespace NetPlay
{

SignalingServer::SignalingServer(const std::string& ip_address, u16 port)
{
  m_server.ServePath("/signaling",
                     [](const Common::WebSocketRequest&, Common::WebSocket& web_socket) {
                       INFO_LOG_FMT(NETPLAY, "SignalingServer got connection !");

                       // FYI: These requests each happen on their own thread.
                       // Common::SetCurrentThreadName();

                       while (web_socket.IsOpen())
                       {
                         // TODO: remove hax
                         web_socket.Send(Common::AsU8Span(web_socket.Read()));
                       }
                     });

  m_server.Start(ip_address, port);
}

}  // namespace NetPlay
