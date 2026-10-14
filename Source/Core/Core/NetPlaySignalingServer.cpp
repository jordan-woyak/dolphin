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
// class IcePeerManager
// {
// public:
//   IcePeerManager() { m_thread = std::jthread(&IcePeerManager::ThreadFunc, this); }

//   std::shared_ptr<NetPlay::ICEPeerConnection> GetOrCreatePeer(const std::string& key)
//   {
//     std::lock_guard lk{m_mutex};
//     return m_peers[key];
//   }

// private:
//   void ThreadFunc()
//   {
//     while (!m_thread.get_stop_token().stop_requested())
//     {
//       // for (auto&& [key, peer] : m_peers)

//       //   while (peer && peer.use_count() > 1)

//       //     // TODO: Janky..
//       //     std::this_thread::sleep_for(std::chrono::milliseconds{100});
//     }
//   }

//   std::mutex m_mutex;
//   std::jthread m_thread;
//   std::map<std::string, std::shared_ptr<NetPlay::ICEPeerConnection>> m_peers;
// };

}  // namespace

namespace NetPlay
{

struct SignalingClient::Impl
{
  std::promise<std::shared_ptr<NetPlay::PeerConnection>> m_peer;
  std::jthread m_thread;

  void ThreadFunc(const std::stop_token& stoken, std::string address, u16 port)
  {
    // TODO: Close this socket when trying to join the thread.
    Common::WebSocketClient web_socket{
        fmt::format("ws://{}:{}{}", address, port, SIGNALING_ENDPOINT)};

    // SDPJsonHandlingICEPeerConnection sdp_peer;
    auto peer = std::make_shared<NetPlay::ICEPeerConnection>();
    // auto peer = sdp_peer.peer;

    const auto sdp_hook = peer->on_sdp_json_message.Register([&](picojson::object obj) {
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

    peer->SendDescription();

    // TODO: Ensure address is resolved.
    // TODO: Is it okay to add this before the remote description ?
    const auto manual_candidate =
        fmt::format("a=candidate:999 1 UDP 2130706431 {} {} typ host", address, port);
    juice_add_remote_candidate(peer->GetAgent(), manual_candidate.c_str());

    web_socket.SetReadTimeout(std::chrono::milliseconds{5000});

    while (!stoken.stop_requested() && web_socket.IsOpen())
    {
      auto obj = web_socket.Read().and_then(ParseJsonObject);
      if (!obj)
        break;

      peer->HandleSDPJsonMessage(*obj);
    }

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
  m_impl->m_thread = std::jthread(std::bind_front(&Impl::ThreadFunc, m_impl.get()), address, port);
  return m_impl->m_peer.get_future();
}

void SignalingServer::Start(const std::string& ip_address, u16 port)
{
  m_server.ServePath(SIGNALING_ENDPOINT, std::bind_front(&SignalingServer::Handler, this));

  m_server.Start(ip_address, port);
}

void SignalingServer::ConnectToNetPlayIndex(std::string secret)
{
  m_thread =
      std::jthread(std::bind_front(&SignalingServer::IndexThreadFunc, this), std::move(secret));
}

void SignalingServer::Handler(Common::WebSocketRequest& web_socket)
{
  INFO_LOG_FMT(NETPLAY, "SignalingServer got connection !");

  // FYI: These requests each happen on their own thread.
  // Common::SetCurrentThreadName();

  // TODO: Disconnect after some seconds

  // SDPJsonHandlingICEPeerConnection sdp_peer;
  auto peer = std::make_shared<ICEPeerConnection>();
  // sdp_peer.peer = peer;

  const auto sdp_hook = peer->on_sdp_json_message.Register([&](picojson::object obj) {
    web_socket.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
  });

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

  while (true)
  {
    if (!web_socket.IsOpen())
      break;

    const auto obj = web_socket.Read().and_then(ParseJsonObject);
    if (!obj)
      break;

    peer->HandleSDPJsonMessage(*obj);
  }

  INFO_LOG_FMT(NETPLAY, "SignalingServer leaving handler !");
}

void SignalingServer::IndexThreadFunc(std::string secret)
{
  // TODO: Jank for http -> ws
  const auto index_url =
      fmt::format("ws{}", std::string_view{Config::Get(Config::NETPLAY_INDEX_URL).substr(4)});

  const auto path = fmt::format("{}/ws/v0/session/listen?secret={}", index_url, secret);
  Common::WebSocketClient web_socket{path};

  // struct TraversalPeer
  // {
  //   std::shared_ptr<NetPlay::ICEPeerConnection> peer;
  //   ICEJsonMessageHandler msg_handler;
  // };

  using ClientUID = std::string;
  std::map<ClientUID, ICEPeerConnection> m_peers;

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

    const auto obj = web_socket.Read().and_then(ParseJsonObject);
    if (!obj)
      continue;

    const auto src = ReadStringFromJson(*obj, "src");
    if (!src)
      continue;

    m_peers[*src].HandleSDPJsonMessage(*obj);
  }
}

}  // namespace NetPlay
