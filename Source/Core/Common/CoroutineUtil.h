// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <coroutine>
#include <future>

template <typename T>
class MyPromise;

template <typename T>
class MyPromiseCommon;

template <typename T>
class ThreadTask
{
  friend MyPromiseCommon<T>;

public:
  T Get() { return m_future.get(); }

  using promise_type = MyPromise<T>;

private:
  explicit ThreadTask(std::future<T>&& f) : m_future{std::move(f)} {}

  std::future<T> m_future;
};

template <typename T>
class MyPromiseCommon
{
public:
  auto get_return_object() { return ThreadTask<T>{m_promise.get_future()}; }
  auto initial_suspend() { return std::suspend_never{}; }
  auto final_suspend() noexcept { return std::suspend_never{}; }
  void unhandled_exception() {}

protected:
  std::promise<T> m_promise;
};

template <typename T>
class MyPromise : public MyPromiseCommon<T>
{
public:
  template <typename TT>
  void return_value(TT&& val)
  {
    this->m_promise.set_value(std::forward<TT>(val));
  }
};

template <>
class MyPromise<void> : public MyPromiseCommon<void>
{
public:
  void return_void() { m_promise.set_value(); }
};
