// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/NetPlaySignalingServer.h"

#include <thread>

#include <juice/juice.h>

#include <picojson.h>

#include "Common/BitUtils.h"
#include "Common/JsonUtil.h"
#include "Common/Logging/Log.h"

namespace
{

// FYI: juice forbids `juice_destroy` within its own callbacks which is majorly inconvenient.
// We have a separate cleanup thread to deal with this.
// TODO: Actually do that.
using UniqueJuiceAgent = std::unique_ptr<juice_agent_t, Common::InvokerOf<juice_destroy>>;

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
  template <auto MemFunc>
  static auto WrapCall()
  {
    return [](juice_agent_t*, auto... args) {
      auto* const last_argument = std::get<sizeof...(args) - 1>(std::forward_as_tuple(args...));
      auto* const user_ptr = static_cast<ICEPeerConnection*>(last_argument);
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        std::invoke(MemFunc, user_ptr, std::get<I>(std::forward_as_tuple(args...))...);
      }(std::make_index_sequence<sizeof...(args) - 1>{});
    };
  }

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

    config.cb_state_changed = WrapCall<&ICEPeerConnection::OnStateChanged>();
    config.cb_candidate = WrapCall<&ICEPeerConnection::OnCandidate>();
    config.cb_recv = WrapCall<&ICEPeerConnection::OnRecv>();
    // config.cb_gathering_done = WrapCall<&ICEPeerConnection::OnGatheringDone>();
    config.user_ptr = this;

    m_agent.reset(juice_create(&config));
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

  void Disconnect() override
  {
    m_is_connected = false;
    // TODO: Trigger on_disconnect ?
  }

  bool IsConnected() const override { return m_is_connected; }

  // void AddRemoteCandidate(const char* sdp) { juice_add_remote_candidate(m_agent.get(), sdp); }

  Common::HookableEvent<juice_state_t> on_state_changed;
  Common::HookableEvent<const char*> on_candidate;
  // Common::HookableEvent<> on_gathering_done;

private:
  void OnStateChanged(juice_state_t state)
  {
    INFO_LOG_FMT(NETPLAY, "ICEPeerConnection: OnStateChanged: {}", juice_state_to_string(state));

    const bool is_connected = state == JUICE_STATE_CONNECTED || state == JUICE_STATE_COMPLETED;
    const bool was_connected = m_is_connected.exchange(is_connected);

    // TODO: Prevent connection after external Disconnect call ?
    on_state_changed.Trigger(state);

    if (was_connected && !is_connected)
      on_disconnect.Trigger();
  }

  void OnCandidate(const char* candidate) { on_candidate.Trigger(candidate); }

  void OnRecv(const char* data, unsigned long size)
  {
    on_receive.Trigger(Common::AsU8Span(std::span{data, size}));
  }

  // void OnGatheringDone() { on_gathering_done.Trigger(); }

  std::atomic_bool m_is_connected = false;
  UniqueJuiceAgent m_agent;
};

class IcePeerManager
{
public:
  IcePeerManager();

private:
  void ThreadFunc()
  {
    while (!m_thread.get_stop_token().stop_requested())
    {
    }
  }

  std::jthread m_thread;
};

}  // namespace

namespace NetPlay
{

struct SignalingClient::Impl
{
  std::promise<std::shared_ptr<NetPlay::PeerConnection>> m_peer;
  std::thread m_thread;

  void ThreadFunc(std::string address, u16 port)
  {
    // TODO: Close this socket when trying to join the thread.
    Common::WebSocketClient web_socket{
        fmt::format("ws://{}:{}{}", address, port, SIGNALING_ENDPOINT)};

    auto peer = std::make_shared<ICEPeerConnection>();

    const auto candidate_hook = peer->on_candidate.Register([&](const char* candidate) {
      picojson::object obj;
      obj.emplace("type", "candidate");
      obj.emplace("candidate", candidate);
      web_socket.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
    });

    const auto fail_hook = peer->on_state_changed.Register([&](juice_state_t state) {
      if (state == JUICE_STATE_FAILED || state == JUICE_STATE_DISCONNECTED)
      {
        web_socket.Close();
      }
    });

    Common::EventHook success_hook = peer->on_state_changed.Register([&](juice_state_t) {
      if (peer->IsConnected())
      {
        success_hook.reset();
        INFO_LOG_FMT(NETPLAY, "connected !!");
        m_peer.set_value(peer);
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

    // TODO: Ensure address is resolved.
    // TODO: Verify this is correct. It is working, though.
    const auto manual_candidate =
        fmt::format("a=candidate:999 1 UDP 2130706431 {} {} typ host", address, port);
    juice_add_remote_candidate(peer->GetAgent(), manual_candidate.c_str());

    web_socket.SetReadTimeout(std::chrono::milliseconds{5000});

    while (web_socket.IsOpen())
    {
      const auto message = web_socket.Read();
      if (!message)
        break;

      const auto obj = ParseJsonObject(*message);
      if (!obj)
        break;

      const auto msg_type = ReadStringFromJson(*obj, "type");
      if (msg_type == "answer")
      {
        INFO_LOG_FMT(NETPLAY, "remote answer !!");

        if (const auto sdp = ReadStringFromJson(*obj, "sdp"))
          juice_set_remote_description(peer->GetAgent(), sdp->c_str());
      }
      else if (msg_type == "candidate")
      {
        if (const auto candidate = ReadStringFromJson(*obj, "candidate"))
        {
          INFO_LOG_FMT(NETPLAY, "remote candidate: {}", *candidate);
          juice_add_remote_candidate(peer->GetAgent(), candidate->c_str());
        }
      }
    }

    INFO_LOG_FMT(NETPLAY, "WS client done");
    web_socket.Close();

    // TODO: Janky..
    while (peer && peer.use_count() > 1)
      std::this_thread::sleep_for(std::chrono::milliseconds{100});

    INFO_LOG_FMT(NETPLAY, "SignalingClient done");
  }
};

SignalingClient::SignalingClient() : m_impl{std::make_unique<Impl>()}
{
}

SignalingClient::~SignalingClient()
{
  // TODO: Close the websocket so it stops faster
  if (m_impl->m_thread.joinable())
    m_impl->m_thread.join();
}

std::future<std::shared_ptr<NetPlay::PeerConnection>>
SignalingClient::MakePeerConnection(const std::string& address, u16 port)
{
  m_impl->m_thread = std::thread(&Impl::ThreadFunc, m_impl.get(), address, port);
  return m_impl->m_peer.get_future();
}

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

  const auto candidate_hook = peer->on_candidate.Register([&](const char* candidate) {
    picojson::object obj;
    obj.emplace("type", "candidate");
    obj.emplace("candidate", candidate);
    web_socket.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
  });

  Common::EventHook recv_hook = peer->on_receive.Register([&](std::span<const u8> message) {
    NOTICE_LOG_FMT(NETPLAY, "recv_hook");
    recv_hook.reset();
    m_new_peer_connection_callback(peer, message);
  });

  // std::atomic_bool local_gathering_done = false;
  // bool remote_gathering_done = false;

  // const auto done_hook = peer->on_gathering_done.Register([&]() {
  //   picojson::object obj;
  //   obj.emplace("type", "candidate");
  //   obj.emplace("candidate", "");
  //   web_socket.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));

  //   local_gathering_done = true;
  // });

  const auto fail_hook = peer->on_state_changed.Register([&](juice_state_t state) {
    if (state == JUICE_STATE_FAILED || state == JUICE_STATE_DISCONNECTED)
    {
      web_socket.Close();
    }
  });

  web_socket.SetReadTimeout(std::chrono::milliseconds{5000});

  // TODO: We can close this socket when gathering is complete.

  // TODO: We need to stay in this loop long enough for the recv hook to trigger.

  while (web_socket.IsOpen())
  {
    // TODO: Jank
    const auto message = web_socket.Read();
    if (!message)
      break;

    const auto msg_obj = ParseJsonObject(*message);
    if (!msg_obj)
      break;

    const auto msg_type = ReadStringFromJson(*msg_obj, "type");
    if (msg_type == "offer")
    {
      if (const auto remote_sdp = ReadStringFromJson(*msg_obj, "sdp"))
      {
        juice_set_remote_description(peer->GetAgent(), remote_sdp->c_str());

        // TODO: build the string in place.
        char local_sdp[JUICE_MAX_SDP_STRING_LEN];
        juice_get_local_description(peer->GetAgent(), local_sdp, JUICE_MAX_SDP_STRING_LEN);

        picojson::object answer_obj;
        answer_obj.emplace("type", "answer");
        answer_obj.emplace("sdp", local_sdp);
        web_socket.Send(Common::AsU8Span(picojson::value(std::move(answer_obj)).serialize()));

        peer->GatherCandidates();
      }
    }
    else if (msg_type == "candidate")
    {
      if (const auto candidate = ReadStringFromJson(*msg_obj, "candidate"))
      {
        INFO_LOG_FMT(NETPLAY, "remote candidate: {}", *candidate);
        juice_add_remote_candidate(peer->GetAgent(), candidate->c_str());
      }
    }
  }

  INFO_LOG_FMT(NETPLAY, "SignalingServer leaving handler !");
}

}  // namespace NetPlay
