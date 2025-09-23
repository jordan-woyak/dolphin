// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>

namespace Common
{

// This heavily encourages always holding a mutex when accessing some object.
// It's still possible to create a reference/pointer to fields that outlast the lock,
//  but that would mostly require intentional misuse.
// This template at least ensures that a lock is acquired in the first place.
template <typename T, typename MutexType>
class LockProtected
{
public:
  // Returns a smart-pointer-like interface for the object which also holds the mutex.
  [[nodiscard]] auto Lock()
  {
    class Value
    {
    public:
      T* operator->() const { return &m_ref; }

    private:
      friend LockProtected;
      Value(T& ref, MutexType& mtx) : m_ref{ref}, m_lock{mtx} {}

      T& m_ref;
      std::lock_guard<MutexType> m_lock;
    };
    return Value(m_value, m_mutex);
  }

  // TODO C++23: const-redundancy can be eliminated with "deducing this" feature.
  [[nodiscard]] auto Lock() const
  {
    class Value
    {
    public:
      const T* operator->() const { return &m_ref; }

    private:
      friend LockProtected;
      Value(const T& ref, MutexType& mtx) : m_ref{ref}, m_lock{mtx} {}

      const T& m_ref;
      std::lock_guard<MutexType> m_lock;
    };
    return Value(m_value, m_mutex);
  }

private:
  T m_value;
  mutable MutexType m_mutex;
};

}  // namespace Common
