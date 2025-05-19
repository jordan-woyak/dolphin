// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/Assets/CustomAssetLoader.h"

#include "Common/Thread.h"

namespace VideoCommon
{
void CustomAssetLoader::Initialize()
{
  ResizeWorkerThreads(2);
}

void CustomAssetLoader::Shutdown()
{
  Reset(false);
}

bool CustomAssetLoader::StartWorkerThreads(u32 num_worker_threads)
{
  for (u32 i = 0; i < num_worker_threads; i++)
  {
    m_worker_threads.emplace_back(&CustomAssetLoader::WorkerThreadRun, this);
  }

  return HasWorkerThreads();
}

bool CustomAssetLoader::ResizeWorkerThreads(u32 num_worker_threads)
{
  if (m_worker_threads.size() == num_worker_threads)
    return true;

  StopWorkerThreads();
  return StartWorkerThreads(num_worker_threads);
}

bool CustomAssetLoader::HasWorkerThreads() const
{
  return !m_worker_threads.empty();
}

void CustomAssetLoader::StopWorkerThreads()
{
  if (!HasWorkerThreads())
    return;

  // Signal worker threads to stop, and wake all of them.
  {
    std::lock_guard guard(m_assets_to_load_lock);
    m_exit_flag.Set();
    m_worker_thread_wake.notify_all();
  }

  // Wait for worker threads to exit.
  for (std::thread& thr : m_worker_threads)
    thr.join();
  m_worker_threads.clear();
  m_exit_flag.Clear();
}

void CustomAssetLoader::WorkerThreadRun()
{
  Common::SetCurrentThreadName("Asset Loader Worker");

  std::unique_lock load_lock(m_assets_to_load_lock);
  while (true)
  {
    m_worker_thread_wake.wait(load_lock,
                              [&] { return !m_assets_to_load.empty() || m_exit_flag.IsSet(); });

    if (m_exit_flag.IsSet())
      return;

    const auto iter = m_assets_to_load.begin();
    const auto item = *iter;
    m_assets_to_load.erase(iter);
    const auto [it, inserted] = m_handles_in_progress.insert(item->GetHandle());
    const auto last_request_time = m_last_request_time;

    // Was the asset added by another call to 'ScheduleAssetsToLoad'
    // while a load for that asset is still in progress on a worker?
    if (!inserted)
      continue;

    if (m_used_memory > m_allowed_memory)
      break;

    load_lock.unlock();

    // Prevent a second load from occurring when a load finishes after
    // a new asset request is triggered by 'ScheduleAssetsToLoad'
    if (last_request_time > item->GetLastLoadedTime() && item->Load())
    {
      std::lock_guard guard(m_assets_loaded_lock);
      m_used_memory += item->GetByteSizeInMemory();
      m_asset_handles_loaded.push_back(item->GetHandle());
    }

    load_lock.lock();
    m_handles_in_progress.erase(item->GetHandle());
  }
}

std::vector<std::size_t> CustomAssetLoader::TakeLoadedAssetHandles()
{
  std::vector<std::size_t> completed_asset_handles;
  {
    std::lock_guard guard(m_assets_loaded_lock);
    m_asset_handles_loaded.swap(completed_asset_handles);
    m_used_memory = 0;
  }

  return completed_asset_handles;
}

void CustomAssetLoader::ScheduleAssetsToLoad(const std::list<CustomAsset*>& assets_to_load,
                                             u64 allowed_memory)
{
  if (assets_to_load.empty()) [[unlikely]]
    return;

  // There's new assets to process, notify worker threads
  {
    std::lock_guard guard(m_assets_to_load_lock);
    m_allowed_memory = allowed_memory;
    m_assets_to_load = assets_to_load;
    m_last_request_time = CustomAssetLibrary::ClockType::now();
    m_worker_thread_wake.notify_all();
  }
}

void CustomAssetLoader::Reset(bool restart_worker_threads)
{
  const std::size_t worker_thread_count = m_worker_threads.size();
  StopWorkerThreads();

  m_assets_to_load.clear();
  m_allowed_memory = 0;
  m_asset_handles_loaded.clear();
  m_used_memory = 0;

  if (restart_worker_threads)
  {
    StartWorkerThreads(static_cast<u32>(worker_thread_count));
  }
}

}  // namespace VideoCommon
