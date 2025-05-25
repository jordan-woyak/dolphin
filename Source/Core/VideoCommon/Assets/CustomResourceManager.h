// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/HookableEvent.h"

#include "VideoCommon/Assets/CustomAsset.h"
#include "VideoCommon/Assets/CustomAssetLibrary.h"
#include "VideoCommon/Assets/CustomAssetLoader.h"
#include "VideoCommon/Assets/CustomTextureData.h"

namespace VideoCommon
{
class TextureAsset;

// The resource manager manages custom resources (textures, shaders, meshes)
// called assets.  These assets are loaded using a priority system,
// where assets requested more often gets loaded first.  This system
// also tracks memory usage and if memory usage goes over a calculated limit,
// then assets will be purged with older assets being targeted first.
class CustomResourceManager
{
public:
  void Initialize();
  void Shutdown();

  void Reset();

  // Request that an asset be reloaded
  void MarkAssetDirty(const CustomAssetLibrary::AssetID& asset_id);

  void XFBTriggered();

  using TextureTimePair = std::pair<std::shared_ptr<CustomTextureData>, CustomAsset::TimeType>;

  // Returns a pair with the custom texture data and the time it was last loaded
  // Callees are not expected to hold onto the shared_ptr as that will prevent
  // the resource manager from being able to properly release data
  TextureTimePair GetTextureDataFromAsset(const CustomAssetLibrary::AssetID& asset_id,
                                          std::shared_ptr<VideoCommon::CustomAssetLibrary> library);

private:
  struct AssetData;

  struct LoadRequest
  {
    AssetData* asset_data = nullptr;
    CustomAsset::TimeType load_request_time = {};
  };

  // Assets that are currently active in memory, in order of most recently used by the game.
  std::list<AssetData*> m_active_assets;

  // Assets that need to be loaded.
  // e.g. Because the game tried to use them or because they changed on disk.
  // Ordered by most recently used.
  std::list<LoadRequest> m_pending_assets;

  // A generic interface to describe an assets' type
  // and load state
  struct AssetData
  {
    std::unique_ptr<CustomAsset> asset;

    // Used to prevent addition load attempts until marked as dirty.
    bool has_load_error = false;

    bool IsLoaded() const { return asset->GetByteSizeInMemory() != 0; }

    CustomAsset::AssetType type;

    decltype(m_active_assets)::iterator active_iter;
    decltype(m_pending_assets)::iterator pending_iter;
  };

  void ProcessDirtyAssets();
  void ProcessLoadedAssets();
  void RemoveAssetsUntilBelowMemoryLimit();

  template <typename T>
  T* GetAsset(const CustomAssetLibrary::AssetID& asset_id,
              std::shared_ptr<VideoCommon::CustomAssetLibrary> library)
  {
    auto& asset_data = m_asset_id_to_data[asset_id];

    // Create a new asset if it doesn't exist.
    if (asset_data == nullptr)
    {
      const auto new_handle = m_asset_handle_to_data.size();

      AssetData new_data;
      new_data.asset = std::make_unique<T>(library, asset_id, new_handle);
      new_data.type = T::ASSET_TYPE;
      new_data.active_iter = m_active_assets.end();
      new_data.pending_iter = m_pending_assets.end();

      asset_data =
          m_asset_handle_to_data.emplace_back(std::make_unique<AssetData>(std::move(new_data)))
              .get();
    }

    MakeAssetHighestPriority(asset_data);

    return static_cast<T*>(asset_data->asset.get());
  }

  // This is used when a game accesses an asset.
  // It marks an unloaded asset as highest priority for loading.
  // and marks a loaded asset as recently used (unlikely to be unloaded).
  void MakeAssetHighestPriority(AssetData*);

  std::vector<std::unique_ptr<AssetData>> m_asset_handle_to_data;
  std::map<CustomAssetLibrary::AssetID, AssetData*> m_asset_id_to_data;

  // Memory used by currently "loaded" assets.
  u64 m_ram_used = 0;

  // A calculated amount of memory to avoid exceeding.
  u64 m_max_ram_available = 0;

  std::mutex m_dirty_mutex;
  std::set<CustomAssetLibrary::AssetID> m_dirty_assets;

  CustomAssetLoader m_asset_loader;

  Common::EventHook m_xfb_event;
};

}  // namespace VideoCommon
