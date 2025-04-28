// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <coroutine>
#include <future>

struct Task
{
  struct promise_type
  {
    static auto get_return_object() { return Task{}; }
    static auto initial_suspend() { return std::suspend_never{}; }
    static auto final_suspend() noexcept { return std::suspend_never{}; }
    static void return_void() {}
    static void unhandled_exception() {}
  };
};

template <typename T>
class AsyncTask;

template <typename T>
class MyPromiseCommon
{
public:
  auto get_return_object() { return AsyncTask<T>{m_promise.get_future()}; }
  static auto initial_suspend() { return std::suspend_never{}; }
  static auto final_suspend() noexcept { return std::suspend_never{}; }
  static void unhandled_exception() {}

protected:
  std::promise<T> m_promise;
};

template <typename T>
class MyPromise : public MyPromiseCommon<T>
{
public:
  template <typename Arg>
  void return_value(Arg&& val)
  {
    this->m_promise.set_value(std::forward<Arg>(val));
  }
};

template <>
class MyPromise<void> : public MyPromiseCommon<void>
{
public:
  void return_void() { m_promise.set_value(); }
};

template <typename T>
class AsyncTask
{
  friend MyPromiseCommon<T>;

public:
  auto Wait() { return m_future.wait(); }
  auto Get() { return m_future.get(); }

  using promise_type = MyPromise<T>;

private:
  explicit AsyncTask(std::future<T>&& f) : m_future{std::move(f)} {}

  std::future<T> m_future;
};

template <typename Func>
auto SwitchToFunctor(Func&& func)
{
  struct Awaiter
  {
    Func func;

    void await_suspend(std::coroutine_handle<> h)
    {
      func([h]() { h.resume(); });
    }

    static constexpr bool await_ready() { return false; }
    static constexpr void await_resume() {}
  };

  return Awaiter{std::forward<Func>(func)};
}
