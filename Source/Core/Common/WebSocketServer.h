// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "Common/CommonTypes.h"
#include "Common/Functional.h"

namespace Common
{

class WebSocketServer;

class WebSocket
{
  friend WebSocketServer;

public:
  bool IsOpen() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

class WebSocketRequest
{
};

class WebSocketServer
{
public:
  WebSocketServer(std::string_view ip, u16 port);

  using HandlerType = Common::MoveOnlyFunction<void(const WebSocketRequest&, WebSocket&)>;

  void ServePath(const std::string& path, HandlerType handler);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Common
