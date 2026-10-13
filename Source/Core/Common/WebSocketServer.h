// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>

#include "Common/CommonTypes.h"

// TODO: Rename this file to just WebSocket
// TODO: Put client before server ?

namespace Common
{

class WebSocket
{
public:
  virtual ~WebSocket() = 0;

  virtual bool IsOpen() const = 0;

  virtual bool Send(std::span<const u8> message) = 0;

  // TODO: Better interface.
  virtual std::string Read() = 0;

  virtual void Close() = 0;
};

class WebSocketClient : public WebSocket
{
public:
  WebSocketClient(const std::string& address);
  ~WebSocketClient() override;

  bool Send(std::span<const u8> message) override;

  std::string Read() override;

  bool IsOpen() const override;

  void Close() override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

class WebSocketServer;

// TODO: Better name
class WebSocketRequest : public WebSocket
{
  friend WebSocketServer;

public:
  WebSocketRequest(std::string_view path);
  ~WebSocketRequest() override;

  std::string_view GetPath() const { return m_path; }

  bool Send(std::span<const u8> message) override;

  std::string Read() override;

  bool IsOpen() const override;

  void Close() override;

private:
  std::string_view m_path;

  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

class WebSocketServer
{
public:
  WebSocketServer();
  ~WebSocketServer();

  using HandlerType = std::function<void(WebSocketRequest&)>;

  // TODO: Rename
  void ServePath(const std::string& path, HandlerType handler);

  void Start(std::string_view ip, u16 port);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Common
