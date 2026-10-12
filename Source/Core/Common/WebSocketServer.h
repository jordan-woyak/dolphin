// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>

#include "Common/CommonTypes.h"

namespace Common
{

class WebSocketServer;

class WebSocket
{
  friend WebSocketServer;

public:
  ~WebSocket();

  bool IsOpen() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

class WebSocketRequest
{
public:
  WebSocketRequest(std::string_view path) : m_path{path} {}

  std::string_view GetPath() const { return m_path; }

private:
  std::string_view m_path;
};

class WebSocketServer
{
public:
  WebSocketServer(std::string_view ip, u16 port);
  ~WebSocketServer();

  using HandlerType = std::function<void(const WebSocketRequest&, WebSocket&)>;

  void ServePath(const std::string& path, HandlerType handler);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Common
