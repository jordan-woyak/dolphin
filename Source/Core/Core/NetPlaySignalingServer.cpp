// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/NetPlaySignalingServer.h"

#include <juice/juice.h>

#include "Common/BitUtils.h"
#include "Common/Logging/Log.h"

namespace NetPlay
{

struct SignalingClient::Impl
{
  void OnStateChanged(juice_state_t state) {}
  void OnCandidate(const char*) {}
  void OnGatheringDone() {}

  juice_agent_t* agent;

  template <auto MemFunc>
  static auto WrapCall()
  {
    return [](juice_agent_t*, auto... args) {
      auto* const last_argument = std::get<sizeof...(args) - 1>(std::forward_as_tuple(args...));
      auto* const user_ptr = static_cast<Impl*>(last_argument);
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        std::invoke(MemFunc, user_ptr, std::get<I>(std::forward_as_tuple(args...))...);
      }(std::make_index_sequence<sizeof...(args) - 1>{});
    };
  }

  static constexpr auto test = std::mem_fn(&Impl::OnStateChanged);
};

SignalingClient::SignalingClient(const std::string& address) : m_impl{std::make_unique<Impl>()}
{
  // Agent 1: Create agent
  juice_config_t config{};

  // STUN server example
  // config.stun_server_host = "stun.l.google.com";
  // config.stun_server_port = 19302;

  // TODO: config.concurrency_mode

  // Port range example
  config.local_port_range_begin = 60000;
  config.local_port_range_end = 61000;

  config.cb_state_changed = Impl::WrapCall<&Impl::OnStateChanged>();
  config.cb_candidate = Impl::WrapCall<&Impl::OnCandidate>();
  config.cb_gathering_done = Impl::WrapCall<&Impl::OnGatheringDone>();
  // config.cb_recv = on_recv;
  config.user_ptr = m_impl.get();

  m_impl->agent = juice_create(&config);
}

SignalingClient::~SignalingClient() = default;

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
