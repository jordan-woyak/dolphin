// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/NetPlaySignalingServer.h"

#include <juice/juice.h>

#include <picojson.h>

#include "Common/BitUtils.h"
#include "Common/JsonUtil.h"
#include "Common/Logging/Log.h"

namespace
{

// TODO: All this nonsense can be relocated if the PeerConnection itself is shared.
struct DynamicJuiceCallbacks
{
  Common::HookableEvent<juice_state_t> state_changed;
  Common::HookableEvent<const char*> candidate;
  Common::HookableEvent<const char*, unsigned long> recv;
};

template <auto MemFunc>
static auto WrapCall()
{
  return [](juice_agent_t*, auto... args) {
    auto* const last_argument = std::get<sizeof...(args) - 1>(std::forward_as_tuple(args...));
    auto* const user_ptr = static_cast<DynamicJuiceCallbacks*>(last_argument);
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      std::invoke(MemFunc, user_ptr).Trigger(std::get<I>(std::forward_as_tuple(args...))...);
    }(std::make_index_sequence<sizeof...(args) - 1>{});
  };
}

// FYI: juice forbids `juice_destroy` within its own callbacks which is majorly inconvenient.
// We have a separate cleanup thread to deal with this.
// TODO: Actually do that.
using SharedJuiceAgent = std::shared_ptr<juice_agent_t>;

SharedJuiceAgent MakeSharedJuiceAgent(juice_config_t* config,
                                      std::shared_ptr<DynamicJuiceCallbacks> callbacks)
{
  config->cb_state_changed = WrapCall<&DynamicJuiceCallbacks::state_changed>();
  config->cb_candidate = WrapCall<&DynamicJuiceCallbacks::candidate>();
  config->cb_recv = WrapCall<&DynamicJuiceCallbacks::recv>();

  config->user_ptr = callbacks.get();

  return SharedJuiceAgent{juice_create(config),
                          [callbacks](juice_agent_t* agent) { juice_destroy(agent); }};
}

// TODO: /ws maybe ? or /ice or /traversal
constexpr auto SIGNALING_ENDPOINT = "/signaling";

std::optional<picojson::object> ParseJsonObject(const std::string& message)
{
  std::optional<picojson::object> obj;

  picojson::value json;
  const std::string err = picojson::parse(json, message);

  if (!err.empty())
  {
    // throw std::invalid_argument{fmt::format("JSON: {}", err)};
    return obj;
  }

  if (!json.is<picojson::object>())
  {
    // throw std::invalid_argument{"JSON: Not an object"};
    return obj;
  }

  obj.emplace(json.get<picojson::object>());
  return obj;
}

// TODO: Needs a reliability layer !!
class ICEPeerConnection : public NetPlay::PeerConnection
{
private:
  // template <auto MemFunc>
  // static auto WrapCall()
  // {
  //   return [](juice_agent_t*, auto... args) {
  //     auto* const last_argument = std::get<sizeof...(args) - 1>(std::forward_as_tuple(args...));
  //     auto* const user_ptr = static_cast<ICEPeerConnection*>(last_argument);
  //     [&]<std::size_t... I>(std::index_sequence<I...>) {
  //       std::invoke(MemFunc, user_ptr, std::get<I>(std::forward_as_tuple(args...))...);
  //     }(std::make_index_sequence<sizeof...(args) - 1>{});
  //   };
  // }

public:
  ICEPeerConnection()
  {
    juice_config_t config{};

    // TODO:
    // STUN server example
    // config.stun_server_host = "stun.l.google.com";
    // config.stun_server_port = 19302;

    // TODO:
    config.concurrency_mode = JUICE_CONCURRENCY_MODE_POLL;

    // TODO:
    // config.local_port_range_begin = 60000;
    // config.local_port_range_end = 61000;

    // config.cb_state_changed = WrapCall<&ICEPeerConnection::OnStateChanged>();
    // config.cb_candidate = WrapCall<&ICEPeerConnection::OnCandidate>();
    // config.cb_recv = WrapCall<&ICEPeerConnection::OnRecv>();
    // config.user_ptr = this;

    m_callbacks = std::make_shared<DynamicJuiceCallbacks>();
    m_agent = MakeSharedJuiceAgent(&config, m_callbacks);

    // TODO: Janky
    m_recv_hook = m_callbacks->recv.Register([this](const char* data, unsigned long size) {
      on_receive.Trigger(Common::AsU8Span(std::span{data, size}));
    });
  }

  void GatherCandidates() { juice_gather_candidates(m_agent.get()); }

  // TODO: Eliminate
  // Incoming connection attempt.
  // void HandleOffer(const std::string& remote_sdp)
  // {
  //   GatherCandidates();

  //   juice_set_remote_description(m_agent.get(), remote_sdp.c_str());
  // }

  juice_agent_t* GetAgent() { return m_agent.get(); }

  void Send(std::span<const u8> data, u8 channel_id) override
  {
    juice_send(m_agent.get(), reinterpret_cast<const char*>(data.data()), data.size());
  }

  void Disconnect() override { m_agent.reset(); }

  bool IsConnected() const override
  {
    // TODO: This needs to operate differently if we can't call destroy when we want..
    const auto state = juice_get_state(m_agent.get());
    return state == JUICE_STATE_CONNECTED || state == JUICE_STATE_COMPLETED;
  }

  auto& GetCandidateEvent() { return m_callbacks->candidate; }
  auto& GetStateChangeEvent() { return m_callbacks->state_changed; }
  auto& GetRecvEvent() { return m_callbacks->recv; }

  // void AddRemoteCandidate(const char* sdp) { juice_add_remote_candidate(m_agent.get(), sdp); }

private:
  // void OnStateChanged(juice_state_t state)
  // {
  //   INFO_LOG_FMT(NETPLAY, "ICEPeerConnection: OnStateChanged: {}", juice_state_to_string(state));

  //   // TODO: Handle other states ?
  //   if (state == JUICE_STATE_CONNECTED || state == JUICE_STATE_FAILED)
  //   {
  //     // TODO: Is this appropriate, there may be more candidates coming in..
  //     m_signaling_web_socket->Close();
  //   }
  // }

  // void OnCandidate(const char* sdp)
  // {
  //   picojson::object obj;
  //   obj.emplace("type", "candidate");
  //   obj.emplace("candidate", sdp);
  //   m_signaling_web_socket->Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
  // }

  void OnRecv(const char* data, unsigned long size)
  {
    on_receive.Trigger(Common::AsU8Span(std::span{data, size}));
  }

  std::shared_ptr<DynamicJuiceCallbacks> m_callbacks;
  SharedJuiceAgent m_agent;
  Common::EventHook m_recv_hook;
};
}  // namespace

namespace NetPlay
{

struct SignalingClient::Impl
{
};

std::shared_ptr<NetPlay::PeerConnection> MakeICEPeerConnection(const std::string& address)
{
  Common::WebSocketClient web_socket{fmt::format("ws://{}{}", address, SIGNALING_ENDPOINT)};
  auto peer = std::make_shared<ICEPeerConnection>();

  // TODO: Allow this WebSocket to keep living and establish candidates.
  const auto candidate_hook = peer->GetCandidateEvent().Register([&](const char* candidate) {
    picojson::object obj;
    obj.emplace("type", "candidate");
    obj.emplace("candidate", candidate);
    web_socket.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
  });

  const auto state_change_hook = peer->GetStateChangeEvent().Register([&](juice_state_t state) {
    INFO_LOG_FMT(NETPLAY, "juice state_change: {}", juice_state_to_string(state));

    if (state == JUICE_STATE_CONNECTED || state == JUICE_STATE_FAILED)
    {
      INFO_LOG_FMT(NETPLAY, "connected !!");

      // TODO: Is this appropriate, there may be more candidates coming in..
      web_socket.Close();
    }
  });

  {
    // TODO: build a std::string in place.
    char sdp[JUICE_MAX_SDP_STRING_LEN];
    juice_get_local_description(peer->GetAgent(), sdp, JUICE_MAX_SDP_STRING_LEN);

    picojson::object obj;
    obj.emplace("type", "offer");
    obj.emplace("sdp", sdp);
    web_socket.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
  }

  peer->GatherCandidates();

  // TODO: Manually add a remote candidate based on the IP and port.
  // Verify this is correct and working..
  // const auto manual_candidate =
  //     fmt::format("a=candidate:999 1 UDP 2130706431 {} {} typ host", ip_address, port);
  // juice_add_remote_candidate(m_agent.get(), manual_candidate.c_str());

  while (web_socket.IsOpen())
  {
    // TODO: Jank
    const auto message = web_socket.Read();
    if (message.empty())
      continue;

    const auto obj = ParseJsonObject(message);
    if (!obj)
      continue;

    const auto msg_type = ReadStringFromJson(*obj, "type");
    if (msg_type == "answer")
    {
      INFO_LOG_FMT(NETPLAY, "remote answer !!");

      if (const auto sdp = ReadStringFromJson(*obj, "sdp"))
        juice_set_remote_description(peer->GetAgent(), sdp->c_str());
    }
    else if (msg_type == "candidate")
    {
      INFO_LOG_FMT(NETPLAY, "remote candidate !!");

      if (const auto candidate = ReadStringFromJson(*obj, "candidate"))
        juice_add_remote_candidate(peer->GetAgent(), candidate->c_str());
    }
  }

  return peer;
}

SignalingClient::SignalingClient(const std::string& address)
    : m_impl{std::make_unique<Impl>()},
      m_client{fmt::format("ws://{}{}", address, SIGNALING_ENDPOINT)}
{
}

SignalingClient::~SignalingClient() = default;

void SignalingServer::Start(const std::string& ip_address, u16 port)
{
  m_server.ServePath(SIGNALING_ENDPOINT, std::bind_front(&SignalingServer::Handler, this));

  m_server.Start(ip_address, port);
}

void SignalingServer::Handler(Common::WebSocketRequest& web_socket)
{
  INFO_LOG_FMT(NETPLAY, "SignalingServer got connection !");

  // FYI: These requests each happen on their own thread.
  // Common::SetCurrentThreadName();

  // TODO: Disconnect after some seconds

  std::shared_ptr<ICEPeerConnection> peer = std::make_shared<ICEPeerConnection>();

  const auto candidate_hook = peer->GetCandidateEvent().Register([&](const char* candidate) {
    picojson::object obj;
    obj.emplace("type", "candidate");
    obj.emplace("candidate", candidate);
    web_socket.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
  });

  Common::EventHook recv_hook =
      peer->GetRecvEvent().Register([&](const char* data, unsigned long size) {
        INFO_LOG_FMT(NETPLAY, "recv_hook");

        // TODO: Is the test necessary ?
        if (peer->IsConnected())
        {
          recv_hook.reset();
          m_new_peer_connection_callback(peer, Common::AsU8Span(std::span{data, size}));
        }
      });

  const auto state_change_hook = peer->GetStateChangeEvent().Register([&](juice_state_t state) {
    INFO_LOG_FMT(NETPLAY, "juice state_change: {}", juice_state_to_string(state));
  });

  // TODO: We can close this socket when gathering is complete.

  // TODO: We need to stay in this loop long enough for the recv hook to trigger.
  while (web_socket.IsOpen() || peer->IsConnected())
  {
    // TODO: remove hax
    const auto message = web_socket.Read();
    if (message.empty())
      continue;

    INFO_LOG_FMT(NETPLAY, "WS: {}", message);

    const auto obj = ParseJsonObject(message);
    if (!obj)
      continue;

    const auto msg_type = ReadStringFromJson(*obj, "type");
    if (msg_type == "offer")
    {
      if (const auto remote_sdp = ReadStringFromJson(*obj, "sdp"))
      {
        juice_set_remote_description(peer->GetAgent(), remote_sdp->c_str());

        // TODO: build the string in place.
        char local_sdp[JUICE_MAX_SDP_STRING_LEN];
        juice_get_local_description(peer->GetAgent(), local_sdp, JUICE_MAX_SDP_STRING_LEN);

        picojson::object obj;
        obj.emplace("type", "answer");
        obj.emplace("sdp", local_sdp);
        web_socket.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));

        peer->GatherCandidates();
      }
    }
    else if (msg_type == "candidate")
    {
      if (const auto candidate = ReadStringFromJson(*obj, "candidate"))
        juice_add_remote_candidate(peer->GetAgent(), candidate->c_str());
    }
  }

  INFO_LOG_FMT(NETPLAY, "SignalingServer leaving handler !");
}

}  // namespace NetPlay
