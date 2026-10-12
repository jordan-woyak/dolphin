// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/WebSocketServer.h"

#include <httplib.h>

#include "Common/Logging/Log.h"
#include "Common/ScopeGuard.h"
#include "Common/Thread.h"

namespace Common
{

// TODO: Add a name parameter for all the logging.

struct WebSocket::Impl
{
  httplib::ws::WebSocket& m_web_socket;
};

WebSocket::~WebSocket() = default;

bool WebSocket::IsOpen() const
{
  return m_impl->m_web_socket.is_open();
}

struct WebSocketServer::Impl
{
  ~Impl()
  {
    // TODO: Expose this function
    m_server.wait_until_ready();

    INFO_LOG_FMT(COMMON, "Stopping server");
    m_server.stop();

    {
      std::lock_guard lk{m_web_socket_mutex};
      for (auto* ws : m_web_sockets)
        ws->close(httplib::ws::CloseStatus::GoingAway);
    }

    if (m_thread.joinable())
      m_thread.join();
  }

  void ThreadFunc(std::string bind_ip, u16 bind_port)
  {
    Common::SetCurrentThreadName("WebSocket Server");

    Common::ScopeGuard decommission{[this] { m_server.decommission(); }};

    m_server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
      if (res.status == 200)
        DEBUG_LOG_FMT(COMMON, "{} {} -> {}", req.method, req.path, res.status);
      else
        INFO_LOG_FMT(COMMON, "{} {} -> {}", req.method, req.path, res.status);
    });

    const bool bind_successful = m_server.bind_to_port(bind_ip, bind_port);
    if (!bind_successful)
    {
      ERROR_LOG_FMT(COMMON, "Unable to bind port: {}:{}", bind_ip, bind_port);
      return;
    }

    NOTICE_LOG_FMT(COMMON, "Server started: {}:{}", bind_ip, bind_port);

    m_server.listen_after_bind();

    NOTICE_LOG_FMT(COMMON, "Server stopped");
  }

  // Open WebSocket are tracked so destruction can close them all, triggering threads to finish.
  void TrackWebSocketAndRunHandler(HandlerType handler, const httplib::Request& request,
                                   httplib::ws::WebSocket& web_socket)
  {
    {
      std::lock_guard lk{m_web_socket_mutex};
      m_web_sockets.emplace(&web_socket);
    }
    Common::ScopeGuard socket_closed{[&] {
      std::lock_guard lk{m_web_socket_mutex};
      m_web_sockets.erase(&web_socket);
    }};

    WebSocketRequest ws_req{request.path};
    WebSocket ws;
    ws.m_impl = std::make_unique<WebSocket::Impl>(web_socket);

    std::invoke(handler, ws_req, ws);
  }

  httplib::Server m_server;
  std::mutex m_web_socket_mutex;
  std::set<httplib::ws::WebSocket*> m_web_sockets;
  std::thread m_thread;
};

WebSocketServer::WebSocketServer() : m_impl{std::make_unique<Impl>()}
{
  // TODO: Remove
  m_impl->m_server.Get("/", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("Hello HTTP", "text/plain");
  });
}

WebSocketServer::~WebSocketServer() = default;

void WebSocketServer::ServePath(const std::string& path, HandlerType handler)
{
  m_impl->m_server.WebSocket(
      path, std::bind_front(&Impl::TrackWebSocketAndRunHandler, m_impl.get(), std::move(handler)));
}

void WebSocketServer::Start(std::string_view ip, u16 port)
{
  m_impl->m_thread = std::thread{&Impl::ThreadFunc, m_impl.get(), std::string(ip), port};
}

struct WebSocketClient::Impl
{
  Impl(const std::string& address) : m_client{address} { m_client.connect(); }

  httplib::ws::WebSocketClient m_client;
};

WebSocketClient::WebSocketClient(const std::string& address)
    : m_impl{std::make_unique<Impl>(address)}
{
}

WebSocketClient::~WebSocketClient() = default;

bool WebSocketClient::IsOpen() const
{
  return m_impl->m_client.is_open();
}

bool WebSocketClient::Send(std::span<const u8> message)
{
  return m_impl->m_client.send(reinterpret_cast<const char*>(message.data()), message.size());
}

std::string WebSocketClient::Read()
{
  std::string result;
  m_impl->m_client.read(result);
  return result;
}

}  // namespace Common
