// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/ICEPeerConnection.h"

#include "Common/JsonUtil.h"

namespace NetPlay
{

ICEPeerConnection::ICEPeerConnection()
{
  DestroyAndRecreateAgent();

  // TODO: Eliminate the on_candidate hook
  //   auto hook = on_candidate.Register([&](const char* candidate) mutable {

  //   });
}

void ICEPeerConnection::HandleSDPJsonMessage(const picojson::object& msg_obj)
{
  const auto msg_type = ReadStringFromJson(msg_obj, "type");
  if (!msg_type)
    return;

  if (*msg_type == "description")
  {
    if (const auto remote_sdp = ReadStringFromJson(msg_obj, "description"))
    {
      INFO_LOG_FMT(NETPLAY, "remote sdp: {}", *remote_sdp);

      const auto state = juice_get_state(GetAgent());

      if (state == JUICE_STATE_FAILED)
        DestroyAndRecreateAgent();

      juice_set_remote_description(GetAgent(), remote_sdp->c_str());

      if (state == JUICE_STATE_DISCONNECTED)
        SendDescription();
    }
  }
  else if (*msg_type == "candidate")
  {
    if (const auto candidate = ReadStringFromJson(msg_obj, "candidate"))
    {
      INFO_LOG_FMT(NETPLAY, "remote candidate: {}", *candidate);
      juice_add_remote_candidate(GetAgent(), candidate->c_str());
    }
  }
  else
  {
    WARN_LOG_FMT(NETPLAY, "HandleSDPJsonMessage: unknown message type: {}", *msg_type);
  }
}

void ICEPeerConnection::SendDescription()
{
  // TODO: build a std::string in place.
  char sdp[JUICE_MAX_SDP_STRING_LEN];
  juice_get_local_description(GetAgent(), sdp, JUICE_MAX_SDP_STRING_LEN);

  picojson::object obj;
  obj.emplace("type", "description");
  obj.emplace("description", sdp);
  on_sdp_json_message.Trigger(std::move(obj));

  // TODO: Do this ASAP
  GatherCandidates();
}

void ICEPeerConnection::OnCandidate(const char* candidate)
{
  // on_candidate.Trigger(candidate);

  INFO_LOG_FMT(NETPLAY, "local candidate: {}", candidate);
  picojson::object obj;
  obj.emplace("type", "candidate");
  obj.emplace("candidate", candidate);
  on_sdp_json_message.Trigger(std::move(obj));
}

}  // namespace NetPlay
