// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/NetPlaySignalingServer.h"

#include <thread>

#include <juice/juice.h>

#include <picojson.h>

#include "Common/BitUtils.h"
#include "Common/JsonUtil.h"
#include "Common/Logging/Log.h"
#include "Core/Config/NetplaySettings.h"
#include "Core/ICEPeerConnection.h"

namespace
{

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

// TODO:..
class IcePeerManager
{
public:
  IcePeerManager() { m_thread = std::jthread(&IcePeerManager::ThreadFunc, this); }

  std::shared_ptr<NetPlay::ICEPeerConnection> GetOrCreatePeer(const std::string& key)
  {
    std::lock_guard lk{m_mutex};
    return m_peers[key];
  }

private:
  void ThreadFunc()
  {
    while (!m_thread.get_stop_token().stop_requested())
    {
      // for (auto&& [key, peer] : m_peers)

      //   while (peer && peer.use_count() > 1)

      //     // TODO: Janky..
      //     std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
  }

  std::mutex m_mutex;
  std::jthread m_thread;
  std::map<std::string, std::shared_ptr<NetPlay::ICEPeerConnection>> m_peers;
};

struct ICEJsonMessageHandler
{
  Common::WebSocket& web_socket;

  std::map<std::string, Common::MoveOnlyFunction<void(const picojson::object&)>> message_handlers;

  void HandleMessage(const std::string& message)
  {
    // TODO: Jank
    const auto msg_obj = ParseJsonObject(message);
    if (!msg_obj)
      return;

    const auto msg_type = ReadStringFromJson(*msg_obj, "type");
    if (!msg_type)
      return;

    const auto it = message_handlers.find(*msg_type);
    if (it == message_handlers.end())
      return;

    std::invoke(it->second, *msg_obj);
  }
};

struct WebSocketJsonReadLoop
{
  Common::WebSocket& web_socket;

  std::map<std::string, Common::MoveOnlyFunction<void(const picojson::object&)>> message_handlers;

  void Run()
  {
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
      if (!msg_type)
        continue;

      const auto it = message_handlers.find(*msg_type);
      if (it == message_handlers.end())
        continue;

      std::invoke(it->second, *msg_obj);
    }
  }
};

// Allow exchange of candidates in both directions.
void InstallCanidateExchangeHooks(WebSocketJsonReadLoop& handler,
                                  std::shared_ptr<NetPlay::ICEPeerConnection> peer)
{
  auto hook = peer->on_candidate.Register([&](const char* candidate) {
    INFO_LOG_FMT(NETPLAY, "local candidate: {}", candidate);
    picojson::object obj;
    obj.emplace("type", "candidate");
    obj.emplace("candidate", candidate);
    handler.web_socket.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
  });

  handler.message_handlers["candidate"] = [hook = std::move(hook),
                                           peer](const picojson::object& msg_obj) {
    if (const auto candidate = ReadStringFromJson(msg_obj, "candidate"))
    {
      INFO_LOG_FMT(NETPLAY, "remote candidate: {}", *candidate);
      juice_add_remote_candidate(peer->GetAgent(), candidate->c_str());
    }
  };
}

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

    // TODO: Ensure address is resolved.
    // TODO: Verify this is correct. It is working, though.
    const auto manual_candidate =
        fmt::format("a=candidate:999 1 UDP 2130706431 {} {} typ host", address, port);
    juice_add_remote_candidate(peer->GetAgent(), manual_candidate.c_str());

    WebSocketJsonReadLoop read_loop{web_socket};
    InstallCanidateExchangeHooks(read_loop, peer);

    // TODO: This happens before the answer. Probably fine ?
    peer->GatherCandidates();

    web_socket.SetReadTimeout(std::chrono::milliseconds{5000});

    read_loop.message_handlers["answer"] = [&](const picojson::object& msg_obj) {
      INFO_LOG_FMT(NETPLAY, "remote answer !!");

      if (const auto sdp = ReadStringFromJson(msg_obj, "sdp"))
        juice_set_remote_description(peer->GetAgent(), sdp->c_str());
    };

    read_loop.Run();

    INFO_LOG_FMT(NETPLAY, "WS client done");

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

void SignalingServer::ConnectToNetPlayIndex(std::string secret)
{
  m_thread = std::jthread(&SignalingServer::IndexThreadFunc, this, std::move(secret));
}

void SignalingServer::Handler(Common::WebSocketRequest& web_socket)
{
  INFO_LOG_FMT(NETPLAY, "SignalingServer got connection !");

  // FYI: These requests each happen on their own thread.
  // Common::SetCurrentThreadName();

  // TODO: Disconnect after some seconds

  auto peer = std::make_shared<ICEPeerConnection>();

  Common::EventHook recv_hook = peer->on_receive.Register([&](std::span<const u8> message) {
    // We received the initial message from the client.
    NOTICE_LOG_FMT(NETPLAY, "recv_hook");
    recv_hook.reset();
    m_new_peer_connection_callback(peer, message);
  });

  const auto fail_hook = peer->on_state_changed.Register([&](juice_state_t state) {
    if (state == JUICE_STATE_FAILED || state == JUICE_STATE_DISCONNECTED)
    {
      web_socket.Close();
    }
  });

  web_socket.SetReadTimeout(std::chrono::milliseconds{5000});

  // TODO: We can close this socket when gathering is complete.
  // TODO: We need to stay in this loop long enough for the recv hook to trigger.

  WebSocketJsonReadLoop read_loop{web_socket};

  read_loop.message_handlers["offer"] = [&](const picojson::object& msg_obj) {
    INFO_LOG_FMT(NETPLAY, "remote offer !!");

    if (const auto remote_sdp = ReadStringFromJson(msg_obj, "sdp"))
    {
      juice_set_remote_description(peer->GetAgent(), remote_sdp->c_str());

      // TODO: build the string in place.
      char local_sdp[JUICE_MAX_SDP_STRING_LEN];
      juice_get_local_description(peer->GetAgent(), local_sdp, JUICE_MAX_SDP_STRING_LEN);

      picojson::object answer_obj;
      answer_obj.emplace("type", "answer");
      answer_obj.emplace("sdp", local_sdp);
      web_socket.Send(Common::AsU8Span(picojson::value(std::move(answer_obj)).serialize()));

      InstallCanidateExchangeHooks(read_loop, peer);

      peer->GatherCandidates();
    }
  };

  read_loop.Run();

  INFO_LOG_FMT(NETPLAY, "SignalingServer leaving handler !");
}

void SignalingServer::IndexThreadFunc(std::string secret)
{
  // TODO: Jank for http -> ws
  const auto index_url =
      fmt::format("ws{}", std::string_view{Config::Get(Config::NETPLAY_INDEX_URL).substr(4)});

  const auto path = fmt::format("{}/ws/v0/session/listen?secret={}", index_url, secret);
  Common::WebSocketClient web_socket{path};

  struct TraversalPeer
  {
    std::shared_ptr<NetPlay::ICEPeerConnection> peer;
  };

  using ClientUID = std::string;
  std::map<ClientUID, TraversalPeer> m_peers;

  WebSocketJsonReadLoop read_loop{web_socket};

  read_loop.message_handlers["offer"] = [&](const picojson::object& msg_obj) {
    INFO_LOG_FMT(NETPLAY, "remote traversal offer !!");

    if (const auto remote_sdp = ReadStringFromJson(msg_obj, "sdp"))
    {
      // TODO: Add this peer somewhere to be referenced later..
      auto peer = std::make_shared<ICEPeerConnection>();

      juice_set_remote_description(peer->GetAgent(), remote_sdp->c_str());

      // TODO: build the string in place.
      char local_sdp[JUICE_MAX_SDP_STRING_LEN];
      juice_get_local_description(peer->GetAgent(), local_sdp, JUICE_MAX_SDP_STRING_LEN);

      picojson::object answer_obj;
      answer_obj.emplace("type", "answer");
      answer_obj.emplace("sdp", local_sdp);
      web_socket.Send(Common::AsU8Span(picojson::value(std::move(answer_obj)).serialize()));

      // InstallCanidateExchangeHooks(read_loop, peer);

      peer->GatherCandidates();
    }
  };

  // TODO: Instead close the web socket from outside on stop
  web_socket.SetReadTimeout(std::chrono::milliseconds{500});

  while (!m_thread.get_stop_token().stop_requested())
  {
    if (!web_socket.IsOpen())
    {
      // TODO: Reconnect after some random time
      WARN_LOG_FMT(NETPLAY, "IndexThreadFunc: WebSocket closed.");
      break;
    }
  }
}

}  // namespace NetPlay
