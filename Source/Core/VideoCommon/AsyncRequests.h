// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

#include "Common/Flag.h"

struct EfbPokeData;
class PointerWrap;

class AsyncRequests
{
public:
  AsyncRequests();

  void PullEvents()
  {
    if (!m_empty.IsSet())
      PullEventsInternal();
  }
  void WaitForEmptyQueue();
  void SetEnable(bool enable);
  void SetPassthrough(bool enable);

  enum class ExecType : bool
  {
    NonBlocking,
    Blocking,
  };

  template <typename F>
  void PushEvent(F&& callback, ExecType exec = ExecType::NonBlocking)
  {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_passthrough)
    {
      std::invoke(callback);
      return;
    }

    QueueEvent(std::forward<F>(callback), exec, lock);
  }

  static AsyncRequests* GetInstance() { return &s_singleton; }

private:
  using Event = std::function<void()>;

  void QueueEvent(const Event& event, ExecType exec, std::unique_lock<std::mutex>&);

  void PullEventsInternal();

  static AsyncRequests s_singleton;

  Common::Flag m_empty;
  std::queue<Event> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_cond;

  bool m_wake_me_up_again = false;
  bool m_enable = false;
  bool m_passthrough = true;
};
