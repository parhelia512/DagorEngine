// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include "levelProfilerInterface.h"
#include <ska_hash_map/flat_hash_map2.hpp>
#include <util/dag_hash.h>
#include <cstddef>

class CollisionResource;
struct RendInstGenData;

extern bool resolve_game_resource_name(String &out_name, const RenderableInstanceLodsResource *res);

namespace levelprofiler
{

struct AssetInfo
{
  ProfilerString name;
  eastl::vector<ProfilerString> textureNames;
};

struct LodInfo
{
  int drawCalls = 0;
  int totalFaces = 0;
  float lodDistance = 0.0f;
  float screenPercent = 0.0f;
  struct HeavyShaderEntry
  {
    ProfilerString name;
    int count = 1;
  };
  eastl::vector<HeavyShaderEntry> heavyShaders;
};

struct CollisionInfo
{
  int physTriangles = 0;
  int traceTriangles = 0;
};

struct RiData
{
  ProfilerString name;
  int countOnMap = 0;
  float bSphereRadius = 0.0f;
  float bBoxRadius = 0.0f;
  eastl::vector<LodInfo> lods;
  CollisionInfo collision;
  const RenderableInstanceLodsResource *resource = nullptr;

  RiData() = default;
  RiData(const ProfilerString &asset_name) : name(asset_name) {}
};

// RI module - collects and manages LOD asset data
class RIModule : public IDataCollector, public IProfilerModule
{
public:
  RIModule();
  virtual ~RIModule();

  void collect() override;
  void clear() override;

  void init() override;
  void shutdown() override;
  void drawUI() override;

  const eastl::vector<AssetInfo> &getAssets() const { return assets; }
  const eastl::hash_map<ProfilerString, eastl::vector<ProfilerString>> &getTextureToAssetsMap() const { return textureToAssetsMap; }
  const eastl::hash_map<ProfilerString, TextureUsage> &getTextureUsage() const { return textureUsage; }

  const eastl::vector<RiData> &getRiData() const { return riData; }
  const RiData *getRiDataByName(const ProfilerString &name) const;
  const eastl::hash_map<ProfilerString, int> &getRiInstanceCounts() const { return riInstanceCounts; }

  void continueCollect() override;
  bool isCollecting() const override;
  void resumeCollection() override;
  bool isPaused() const override { return collectPhase == CollectPhase::Paused; }
  float getCollectProgress() const override;
  void pauseCollection() override;

  int getCollectLayerCount() const { return static_cast<int>(layerCollectStates.size()); }
  int getCollectCompletedLayers() const { return currentLayerIndex; }
  size_t getCollectProcessedCells() const { return processedCells; }
  size_t getCollectTotalCells() const { return totalCellsToProcess; }

  int getMaxUniqueTextureUsageCount() const { return maxUniqueTextureUsageCount; }
  int getMaxAssetInstanceCount() const { return maxAssetInstanceCount; }
  bool hasProvisionalRiData() const { return riDataProvisional; }
  bool wasCollectCancelled() const { return collectCancelled; }
  unsigned getRiDataGeneration() const { return riDataGeneration; }

  void buildTextureToAssetMap();
  void computeTextureUsageStatistics();
  void collectRiDataForProfiling();

  eastl::vector<const RenderableInstanceLodsResource *> allRenderableInstances;

protected:
  void shutdownImpl();
  void clearImpl();

private:
  eastl::vector<AssetInfo> assets;
  eastl::hash_map<ProfilerString, eastl::vector<ProfilerString>> textureToAssetsMap;
  eastl::hash_map<ProfilerString, TextureUsage> textureUsage;

  eastl::vector<RiData> riData;
  eastl::hash_map<ProfilerString, int> riInstanceCounts;
  // Flat: rebuilt whole on every collect, and its values point into riData, so moving entries
  // on growth does not invalidate them. riInstanceCounts must stay node-based instead, its
  // mapped values are pointed at by LayerCollectState::PoolCountSlot.
  ska::flat_hash_map<ProfilerString, RiData *, HashFNV1A<ProfilerString>> riDataLookup;

  int maxUniqueTextureUsageCount = 0;
  int maxAssetInstanceCount = 0;
  bool riDataProvisional = false;
  bool collectCancelled = false;
  unsigned riDataGeneration = 0;

  void collectRenderableInstances();
  eastl::vector<AssetInfo> getUniqueAssets();

  struct LayerCollectState;

  void collectInstanceCounts();
  void collectRiGenInstanceCounts();
  int countRiGenCell(LayerCollectState &state, RendInstGenData *layer, int cell_x, int cell_y);
  void prepareLayerCollection();
  RendInstGenData *resolveCollectLayer(const LayerCollectState &state) const;
  bool processLayerCell(LayerCollectState &state, RendInstGenData *layer, int &out_instances);
  void finalizeCollection();
  void cancelCollection();
  void resetCollectionState();
  void updateRiDataCount(const ProfilerString &asset_name, int new_count);
  template <typename LodType>
  LodInfo analyzeLodData(const LodType &lod, float bsphere_radius, float bbox_radius) const;
  CollisionInfo analyzeCollision(const CollisionResource *collision_resource) const;

  enum class CollectPhase
  {
    Idle = 0,
    Collecting,
    Paused,
    Finalizing
  };

  struct LayerCollectState
  {
    struct PoolCountSlot
    {
      int *count = nullptr;
      RiData *data = nullptr;
    };

    // The walk spans many frames and a level unload deletes the riGen layers, so the layer is
    // addressed by index and re-validated against these snapshots before each use.
    int layerIndex = -1;
    const RendInstGenData *expectedLayer = nullptr;
    const void *expectedRtData = nullptr;
    int cellCountW = 0;
    int cellCountH = 0;
    int nextCellIndex = 0;
    eastl::vector<PoolCountSlot> poolSlots;
  };

  CollectPhase collectPhase = CollectPhase::Idle;
  eastl::vector<LayerCollectState> layerCollectStates;
  int currentLayerIndex = 0;
  size_t totalCellsToProcess = 0;
  size_t processedCells = 0;
};

} // namespace levelprofiler
