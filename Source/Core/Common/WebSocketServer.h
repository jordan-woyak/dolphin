// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <span>

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

  void Send(std::span<const u8> message);

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
  WebSocketServer();
  ~WebSocketServer();

  using HandlerType = std::function<void(const WebSocketRequest&, WebSocket&)>;

  // TODO: Rename
  void ServePath(const std::string& path, HandlerType handler);

  void Start(std::string_view ip, u16 port);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Common
