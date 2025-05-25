// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/Assets/CustomResourceManager.h"

#include "Common/Logging/Log.h"
#include "Common/MemoryUtil.h"

#include "UICommon/UICommon.h"

#include "VideoCommon/Assets/CustomAsset.h"
#include "VideoCommon/Assets/TextureAsset.h"
#include "VideoCommon/VideoEvents.h"

namespace VideoCommon
{
void CustomResourceManager::Initialize()
{
  // Use half of available system memory but leave at least 2GiB unused for system stability.
  constexpr size_t must_keep_unused = 2 * size_t(1024 * 1024 * 1024);

  const size_t sys_mem = Common::MemPhysical();
  const size_t keep_unused_mem = std::max(sys_mem / 2, std::min(sys_mem, must_keep_unused));

  m_max_ram_available = sys_mem - keep_unused_mem;

  if (m_max_ram_available == 0)
    ERROR_LOG_FMT(VIDEO, "Not enough system memory for custom resources.");

  m_asset_loader.Initialize();

  m_xfb_event =
      AfterFrameEvent::Register([this](Core::System&) { XFBTriggered(); }, "CustomResourceManager");
}

void CustomResourceManager::Shutdown()
{
  Reset();

  m_asset_loader.Shutdown();
}

void CustomResourceManager::Reset()
{
  m_asset_loader.Reset(true);

  m_active_assets = {};
  m_pending_assets = {};
  m_asset_handle_to_data.clear();
  m_asset_id_to_data.clear();
  m_dirty_assets.clear();
  m_ram_used = 0;
}

void CustomResourceManager::MarkAssetDirty(const CustomAssetLibrary::AssetID& asset_id)
{
  std::lock_guard guard(m_dirty_mutex);
  m_dirty_assets.insert(asset_id);
}

CustomResourceManager::TextureTimePair CustomResourceManager::GetTextureDataFromAsset(
    const CustomAssetLibrary::AssetID& asset_id,
    std::shared_ptr<VideoCommon::CustomAssetLibrary> library)
{
  auto* const asset = GetAsset<TextureAsset>(asset_id, std::move(library));
  return {asset->GetData(), asset->GetLastLoadedTime()};
}

void CustomResourceManager::MakeAssetHighestPriority(AssetData* data)
{
  // Update active state.
  if (data->active_iter == m_active_assets.end())
  {  // Place newly active asset at the front of the list.
    data->active_iter = m_active_assets.insert(m_active_assets.begin(), data);
  }
  else
  {  // Bump already active asset to the front of the list.
    m_active_assets.splice(m_active_assets.begin(), m_active_assets, data->active_iter);
  }

  if (data->IsLoaded() || data->has_load_error)
    return;

  // Update pending state.
  if (data->pending_iter == m_pending_assets.end())
  {  // Create a new load request.
    data->pending_iter =
        m_pending_assets.insert(m_pending_assets.begin(), LoadRequest{data, Clock::now()});
  }
  else
  {  // Bump the existing load request to the front of the list.
    m_pending_assets.splice(m_pending_assets.begin(), m_pending_assets, data->pending_iter);
  }
}

void CustomResourceManager::XFBTriggered()
{
  ProcessDirtyAssets();
  ProcessLoadedAssets();

  if (m_ram_used > m_max_ram_available)
  {
    RemoveAssetsUntilBelowMemoryLimit();
  }

  if (m_pending_assets.empty())
    return;

  if (m_ram_used > m_max_ram_available)
    return;

  const u64 allowed_memory = m_max_ram_available - m_ram_used;

  std::list<CustomAsset*> assets_to_load;
  for (const auto& request : m_pending_assets)
    assets_to_load.emplace_back(request.asset_data->asset.get());

  m_asset_loader.ScheduleAssetsToLoad(assets_to_load, allowed_memory);
}

void CustomResourceManager::ProcessDirtyAssets()
{
  decltype(m_dirty_assets) dirty_assets;

  if (const auto lk = std::unique_lock{m_dirty_mutex, std::try_to_lock})
    std::swap(dirty_assets, m_dirty_assets);

  const auto now = CustomAsset::ClockType::now();
  for (const auto& asset_id : dirty_assets)
  {
    const auto it = m_asset_id_to_data.find(asset_id);

    if (it == m_asset_id_to_data.end())
      continue;

    AssetData& asset_data = *it->second;

    // Clear error state.
    asset_data.has_load_error = false;

    // Ignore the change if the asset isn't active.
    if (asset_data.active_iter == m_active_assets.end())
      continue;

    if (asset_data.pending_iter == m_pending_assets.end())
    {  // Create a new load request.
      asset_data.pending_iter =
          m_pending_assets.insert(m_pending_assets.end(), LoadRequest{&asset_data, now});
    }
    else
    {  // Update the existing load request.
      asset_data.pending_iter->load_request_time = now;
    }

    INFO_LOG_FMT(VIDEO, "Dirty asset pending reload: {}", asset_data.asset->GetAssetId());
  }
}

void CustomResourceManager::ProcessLoadedAssets()
{
  const auto load_results = m_asset_loader.TakeLoadResults();
  m_ram_used += load_results.change_in_memory;

  for (const auto& [handle, load_successful] : load_results.asset_handles)
  {
    AssetData& asset_data = *m_asset_handle_to_data[handle];

    // Ensure this loaded asset is in the active list.
    if (asset_data.active_iter == m_active_assets.end())
      asset_data.active_iter = m_active_assets.insert(m_active_assets.end(), &asset_data);

    // Did we still even want this asset loaded?
    if (asset_data.pending_iter == m_pending_assets.end())
      continue;

    // If we have a reload request that is newer than our loaded time
    // we need to wait for another reload.
    if (asset_data.pending_iter->load_request_time > asset_data.asset->GetLastLoadedTime())
      continue;

    // Remove from pending list.
    if (asset_data.pending_iter != m_pending_assets.end())
      m_pending_assets.erase(std::exchange(asset_data.pending_iter, m_pending_assets.end()));

    if (!load_successful)
      asset_data.has_load_error = true;
  }
}

void CustomResourceManager::RemoveAssetsUntilBelowMemoryLimit()
{
  const u64 threshold_ram = m_max_ram_available * 8 / 10;

  if (m_ram_used > threshold_ram)
  {
    INFO_LOG_FMT(VIDEO, "Memory usage over threshold: {}", UICommon::FormatSize(m_ram_used));
  }

  // Clear out least recently used resources until
  // we get safely in our threshold
  while (m_ram_used > threshold_ram && !m_active_assets.empty())
  {
    auto* const data = m_active_assets.back();
    m_active_assets.pop_back();
    data->active_iter = m_active_assets.end();

    // Also remove from pending list.
    if (data->pending_iter != m_pending_assets.end())
      m_pending_assets.erase(std::exchange(data->pending_iter, m_pending_assets.end()));

    auto* const asset = data->asset.get();

    m_ram_used -= asset->GetByteSizeInMemory();

    INFO_LOG_FMT(VIDEO, "Unloading asset: {} ({})", asset->GetAssetId(),
                 UICommon::FormatSize(asset->GetByteSizeInMemory()));

    asset->Unload();
  }
}

}  // namespace VideoCommon
