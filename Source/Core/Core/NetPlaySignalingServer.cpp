// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/NetPlaySignalingServer.h"

#include <juice/juice.h>

#include <picojson.h>

#include "Common/BitUtils.h"
#include "Common/Logging/Log.h"

namespace
{

// TODO: Is it safe to just `juice_destroy` when callbacks are connected ?
using UniqueJuiceAgent = std::unique_ptr<juice_agent_t, Common::InvokerOf<juice_destroy>>;

// TODO: Needs a reliability layer !!
class ICEPeerConnection : public NetPlay::PeerConnection
{
public:
  ICEPeerConnection(UniqueJuiceAgent agent) : m_agent{std::move(agent)}
  {
    // TODO: Adjust the callbacks !
  }

  void Send(std::span<const u8> data, u8 channel_id) override
  {
    juice_send(m_agent.get(), reinterpret_cast<const char*>(data.data()), data.size());
  }

  void Disconnect() override
  {
    // FYI: This will apparently not be safe from within a callback !
    m_agent.reset();
  }

  bool IsConnected() const override
  {
    return juice_get_state(m_agent.get()) == JUICE_STATE_CONNECTED;
  }

private:
  UniqueJuiceAgent m_agent;
};
}  // namespace

namespace NetPlay
{

struct SignalingClient::Impl
{
  void OnStateChanged(juice_state_t state)
  {
    if (state == JUICE_STATE_CONNECTED)
    {
      m_promise.set_value(std::make_unique<ICEPeerConnection>(std::move(m_agent)));
    }
  }
  void OnCandidate(const char*) {}
  void OnGatheringDone() {}

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

  UniqueJuiceAgent m_agent;
  std::promise<std::unique_ptr<NetPlay::PeerConnection>> m_promise;
};

SignalingClient::SignalingClient(const std::string& address)
    : m_impl{std::make_unique<Impl>()}, m_client{fmt::format("ws://{}/signaling", address)}
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

  m_impl->m_agent.reset(juice_create(&config));

  char sdp[JUICE_MAX_SDP_STRING_LEN];
  juice_get_local_description(m_impl->m_agent.get(), sdp, JUICE_MAX_SDP_STRING_LEN);

  picojson::object obj;
  obj.emplace("type", "offer");
  obj.emplace("sdp", sdp);
  m_client.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
}

SignalingClient::~SignalingClient() = default;

std::future<std::unique_ptr<NetPlay::PeerConnection>> SignalingClient::GetFuturePeerConnection()
{
  return m_impl->m_promise.get_future();
}

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
                         const auto message = web_socket.Read();
                         INFO_LOG_FMT(NETPLAY, "WS: {}", message);
                         web_socket.Send(Common::AsU8Span(message));
                       }
                     });

  m_server.Start(ip_address, port);
}

}  // namespace NetPlay
