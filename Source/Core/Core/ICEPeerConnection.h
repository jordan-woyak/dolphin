// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <juice/juice.h>

#include <picojson.h>

#include "Core/NetPlayProto.h"

// TODO: Needs a reliability layer !!

namespace NetPlay
{

// FYI: juice forbids `juice_destroy` within its own callbacks which is majorly inconvenient.
// We have a separate cleanup thread to deal with this.
// TODO: Actually do that.
using UniqueJuiceAgent = std::unique_ptr<juice_agent_t, Common::InvokerOf<juice_destroy>>;

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
  ICEPeerConnection();

  void DestroyAndRecreateAgent()
  {
    juice_config_t config{};

    // TODO:
    // config.stun_server_host = "stun.l.google.com";
    // config.stun_server_port = 19302;

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
  // Common::HookableEvent<const char*> on_candidate;
  Common::HookableEvent<picojson::object> on_sdp_json_message;

  void HandleSDPJsonMessage(const picojson::object& obj);

  void SendDescription();

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

  void OnCandidate(const char* candidate);

  void OnRecv(const char* data, unsigned long size)
  {
    on_receive.Trigger(Common::AsU8Span(std::span{data, size}));
  }

  // void OnGatheringDone() { on_gathering_done.Trigger(); }

  std::atomic_bool m_is_connected = false;
  UniqueJuiceAgent m_agent;
};

}  // namespace NetPlay
