// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <coroutine>

namespace Common
{
class ResumableTask
{
public:
  ResumableTask() = default;
  ~ResumableTask() { m_handle.destroy(); }

  ResumableTask(ResumableTask&& other) { Swap(other); }
  ResumableTask& operator=(ResumableTask&& other)
  {
    ResumableTask().Swap(*this);
    Swap(other);
    return *this;
  }

  ResumableTask(const ResumableTask&) = default;
  ResumableTask& operator=(const ResumableTask&) = default;

  bool IsValid() const { return m_handle != std::noop_coroutine(); }
  bool IsDone() const { return m_handle.done(); }
  void Resume() { m_handle.resume(); }

  struct promise_type
  {
    // Currently does not start suspended. It might be useful to be able to configure that..
    static constexpr auto initial_suspend() { return std::suspend_never{}; }

    static constexpr auto final_suspend() noexcept { return std::suspend_always{}; }

    // TODO: should probably save the exception or whatever..
    static constexpr void unhandled_exception() {}

    auto get_return_object()
    {
      return ResumableTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    static constexpr void return_void() {}
  };

private:
  void Swap(ResumableTask& other) { std::swap(m_handle, other.m_handle); }

  using HandleType = std::coroutine_handle<>;

  explicit ResumableTask(HandleType handle) : m_handle{handle} {}

  HandleType m_handle{std::noop_coroutine()};
};

}  // namespace Common
