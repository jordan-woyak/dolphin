// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/Resources/Resource.h"

namespace VideoCommon
{
Resource::Resource(ResourceContext resource_context)
    : m_resource_context(std::move(resource_context)), m_reload_data_task{ReloadData()}
{
}

void Resource::Process()
{
  MarkAsActive();

  if (!m_reload_data_task.IsDone())
    m_reload_data_task.Resume();
}

Common::ResumableTask Resource::ReloadData()
{
  // Wait for a .Resume to actually do anything.
  co_await std::suspend_always{};

  ResetData();

  MarkAsPending();
  for (auto primary = CollectPrimaryData(); !primary.IsDone(); primary.Resume())
  {
    MarkAsPending();
    co_await std::suspend_always{};
  }

  for (auto deps = CollectDependencyData(); !deps.IsDone(); deps.Resume())
    co_await std::suspend_always{};

  for (auto process = ProcessData(); !process.IsDone(); process.Resume())
    co_await std::suspend_always{};

  m_data_processed = true;
}

void Resource::NotifyAssetChanged(bool has_error)
{
  // TODO: this is supposed to do nothing if there was an error previously?
  m_data_processed = false;
  m_reload_data_task = ReloadData();

  for (Resource* reference : m_references)
  {
    reference->NotifyAssetChanged(has_error);
  }
}

void Resource::NotifyAssetUnloaded()
{
  OnUnloadRequested();

  for (Resource* reference : m_references)
  {
    reference->NotifyAssetUnloaded();
  }
}

void Resource::AddReference(Resource* reference)
{
  m_references.insert(reference);
}

void Resource::RemoveReference(Resource* reference)
{
  m_references.erase(reference);
}

void Resource::AssetLoaded(bool has_error, bool triggered_by_reload)
{
  if (triggered_by_reload)
    NotifyAssetChanged(has_error);
}

void Resource::AssetUnloaded()
{
  NotifyAssetUnloaded();
}

void Resource::OnUnloadRequested()
{
}

void Resource::ResetData()
{
}

Common::ResumableTask Resource::CollectPrimaryData()
{
  co_return;
}

Common::ResumableTask Resource::CollectDependencyData()
{
  co_return;
}

Common::ResumableTask Resource::ProcessData()
{
  co_return;
}
}  // namespace VideoCommon
