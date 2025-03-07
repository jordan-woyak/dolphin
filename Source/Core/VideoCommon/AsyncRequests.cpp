// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/AsyncRequests.h"

#include <mutex>

#include "Common/EnumUtils.h"

#include "Core/System.h"

#include "VideoCommon/Fifo.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoEvents.h"

AsyncRequests AsyncRequests::s_singleton;

AsyncRequests::AsyncRequests() = default;

void AsyncRequests::PullEventsInternal()
{
  // This is only called if the queue isn't empty.
  // So just flush the pipeline to get accurate results.
  g_vertex_manager->Flush();

  std::unique_lock<std::mutex> lock(m_mutex);
  m_empty.Set();

  while (!m_queue.empty())
  {
    Event e = m_queue.front();
    lock.unlock();
    std::invoke(e);
    lock.lock();

    m_queue.pop();
  }

  if (m_wake_me_up_again)
  {
    m_wake_me_up_again = false;
    m_cond.notify_all();
  }
}

void AsyncRequests::QueueEvent(const AsyncRequests::Event& event, ExecType exec,
                               std::unique_lock<std::mutex>& lock)
{
  m_empty.Clear();
  m_wake_me_up_again |= Common::ToUnderlying(exec);

  if (!m_enable)
    return;

  m_queue.push(event);

  auto& system = Core::System::GetInstance();
  system.GetFifo().RunGpu();
  if (exec == ExecType::Blocking)
  {
    m_cond.wait(lock, [this] { return m_queue.empty(); });
  }
}

void AsyncRequests::WaitForEmptyQueue()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_cond.wait(lock, [this] { return m_queue.empty(); });
}

void AsyncRequests::SetEnable(bool enable)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_enable = enable;

  if (!enable)
  {
    // flush the queue on disabling
    while (!m_queue.empty())
      m_queue.pop();
    if (m_wake_me_up_again)
      m_cond.notify_all();
  }
}

void AsyncRequests::SetPassthrough(bool enable)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_passthrough = enable;
}
