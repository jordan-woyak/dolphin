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

// TODO: ICEPeerConnection should attempt to use signal client itself if connection is lost ?

// TODO: Is it safe to just `juice_destroy` when callbacks are connected ?
using UniqueJuiceAgent = std::unique_ptr<juice_agent_t, Common::InvokerOf<juice_destroy>>;

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
  ICEPeerConnection(const std::string& address)
      : m_signaling_client{fmt::format("ws://{}{}", address, SIGNALING_ENDPOINT)}
  {
    // TODO: Adjust the callbacks !
  }

  void Config()
  {
    juice_config_t config{};

    // STUN server example
    // config.stun_server_host = "stun.l.google.com";
    // config.stun_server_port = 19302;

    // TODO: config.concurrency_mode

    // Port range example
    config.local_port_range_begin = 60000;
    config.local_port_range_end = 61000;

    config.cb_state_changed = WrapCall<&ICEPeerConnection::OnStateChanged>();
    config.cb_candidate = WrapCall<&ICEPeerConnection::OnCandidate>();
    config.cb_gathering_done = WrapCall<&ICEPeerConnection::OnGatheringDone>();
    config.cb_recv = WrapCall<&ICEPeerConnection::OnRecv>();
    config.user_ptr = this;

    m_agent.reset(juice_create(&config));
  }

  // Returns the local description.
  std::string Offer()
  {
    Config();

    // TODO: build the string in place.
    char desc[JUICE_MAX_SDP_STRING_LEN];
    juice_get_local_description(m_agent.get(), desc, JUICE_MAX_SDP_STRING_LEN);

    return desc;
  }

  void Connect()
  {
    Config();

    // TODO: Ensure juice produces no traffic when we exit here.

    if (!m_signaling_client.IsOpen())
      return;

    char desc[JUICE_MAX_SDP_STRING_LEN];
    juice_get_local_description(m_agent.get(), desc, JUICE_MAX_SDP_STRING_LEN);

    {
      picojson::object obj;
      obj.emplace("type", "offer");
      obj.emplace("sdp", desc);
      m_signaling_client.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
    }

    juice_gather_candidates(m_agent.get());

    while (m_signaling_client.IsOpen())
    {
      // TODO: Jank
      const auto message = m_signaling_client.Read();
      if (message.empty())
        continue;

      const auto obj = ParseJsonObject(message);
      if (!obj)
        continue;

      const auto msg_type = ReadStringFromJson(*obj, "type");
      if (msg_type == "answer")
      {
        const auto sdp = ReadStringFromJson(*obj, "sdp");
        if (sdp)
          juice_set_remote_description(m_agent.get(), sdp->c_str());
      }
      else if (msg_type == "candidate")
      {
        const auto candidate = ReadStringFromJson(*obj, "candidate");
        if (candidate)
          juice_add_remote_candidate(m_agent.get(), candidate->c_str());
      }
    }
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
  void OnStateChanged(juice_state_t state)
  {
    if (state == JUICE_STATE_CONNECTED || state == JUICE_STATE_FAILED)
    {
      m_signaling_client.Close();
    }
  }

  void OnCandidate(const char* candidate)
  {
    picojson::object obj;
    obj.emplace("type", "candidate");
    obj.emplace("candidate", candidate);
    m_signaling_client.Send(Common::AsU8Span(picojson::value(std::move(obj)).serialize()));
  }

  void OnGatheringDone() {}

  void OnRecv(const char* data, unsigned long size)
  {
    on_receive.Trigger(Common::AsU8Span(std::span{data, size}));
  }

  // TODO: This needs to support using a WebSocket from the SignalingServer...
  Common::WebSocketClient m_signaling_client;
  UniqueJuiceAgent m_agent;
};
}  // namespace

namespace NetPlay
{

struct SignalingClient::Impl
{
  std::promise<std::unique_ptr<NetPlay::PeerConnection>> m_promise;
};

std::unique_ptr<NetPlay::PeerConnection> MakeICEPeerConnection(const std::string& address)
{
  auto result = std::make_unique<ICEPeerConnection>(address);
  result->Connect();
  return result;
}

SignalingClient::SignalingClient(const std::string& address)
    : m_impl{std::make_unique<Impl>()},
      m_client{fmt::format("ws://{}{}", address, SIGNALING_ENDPOINT)}
{
}

SignalingClient::~SignalingClient() = default;

std::future<std::unique_ptr<NetPlay::PeerConnection>> SignalingClient::GetFuturePeerConnection()
{
  return m_impl->m_promise.get_future();
}

SignalingServer::SignalingServer(const std::string& ip_address, u16 port)
{
  m_server.ServePath(SIGNALING_ENDPOINT, std::bind_front(&SignalingServer::Handler, this));

  m_server.Start(ip_address, port);
}

void SignalingServer::Handler(const Common::WebSocketRequest&, Common::WebSocket& web_socket)
{
  INFO_LOG_FMT(NETPLAY, "SignalingServer got connection !");

  // FYI: These requests each happen on their own thread.
  // Common::SetCurrentThreadName();

  // TODO: Disconnect after some seconds

  std::unique_ptr<ICEPeerConnection> peer;

  while (web_socket.IsOpen())
  {
    // TODO: remove hax
    const auto message = web_socket.Read();
    INFO_LOG_FMT(NETPLAY, "WS: {}", message);

    const auto obj = ParseJsonObject(message);
    if (!obj)
      continue;

    const auto msg_type = ReadStringFromJson(*obj, "type");
    if (msg_type == "offer")
    {
      const auto sdp = ReadStringFromJson(*obj, "sdp");
      if (sdp)
        juice_set_remote_description(m_agent.get(), sdp->c_str());
    }
    else if (msg_type == "candidate")
    {
      const auto candidate = ReadStringFromJson(*obj, "candidate");
      if (candidate)
        juice_add_remote_candidate(m_agent.get(), candidate->c_str());
    }
  }
}

}  // namespace NetPlay
