//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <render/lights/lightsBase.h>
#include <render/lights/omniLightsManager.h>
#include <render/lights/spotLightsManager.h>
#include <render/lights/reallocatableLightsConstBuffer.h>
#include <render/lights/lightsResources.h>
#include <render/lights/lightsSorter.h>
#include <render/lights/lightsVisibilityChecker.h>
#include <drv/3d/dag_buffers.h>
#include <math/dag_frustum.h>
#include <shaders/dag_shaders.h>
#include <shaders/dag_computeShaders.h>
#include <shaders/dag_shaderVariableInfo.h>
#include <3d/dag_resourceTags.h>
#include <EASTL/array.h>

class LightsPartition
{
public:
  using OmniLightsCB = ReallocatableLightsConstBuffer<sizeof(RenderOmniLight) / 16, true>;
  using SpotLightsCB = ReallocatableLightsConstBuffer<sizeof(RenderSpotLight) / 16, true>;

  // At least on win7 we have a limit for 64k of cb buffer size
  // But drivers requires to keep cb buffer size under 64k on all platforms.
  // So we limit it for all platforms.
  // Reserve one Point4 for the element count constant that ReallocatableLightsConstBuffer
  // prepends when store_elems_count is true (see reallocate()).
  static constexpr int MAX_VISIBLE_FAR_LIGHTS = (65536 - sizeof(Point4)) / max(sizeof(RenderSpotLight), sizeof(RenderOmniLight));


  LightsPartition(OmniLightsManager &omni_lights, SpotLightsManager &spot_lights, const LightsResourcesManager *lights_res_mgr);

  void init(bool use_gpu_partition);
  bool isGPU() const;
  bool isGPUSortAvailable() const;
  struct ExecuteLightsCPUPartitionParams
  {
    const LightsVisibilityChecker &checker;
    Tab<uint16_t> &omniLightsFar;
    Tab<uint16_t> &omniLightsClustered;
    Tab<uint16_t> &spotLightsFar;
    Tab<uint16_t> &spotLightsClustered;
  };
  void executeLightsCPUPartition(const ExecuteLightsCPUPartitionParams &params) const;

  void prepareClusteredAndFarLightBuffersCPU(const LightsVisibilityChecker &checker);
  void prepareClusteredAndFarLightBuffersGPU(const LightsVisibilityChecker::TestParameters &test_params, float zfar);

  void executeLightsGPUPartition(const LightsVisibilityChecker::TestParameters &test_params, float zfar, bool update_variables = true);

  void close();

  void updateBuffersForVisibleFarLights(LightBufferSlot buffer_slot);
  void updateBuffersForVisibleClusteredLights(LightBufferSlot buffer_slot, int omni_count, int spot_count);

  const Tab<uint16_t> &getVisibleClusteredSpotLightsIds() const;
  const Tab<uint16_t> &getVisibleClusteredOmniLightsIds() const;

  const Tab<uint16_t> &getVisibleFarSpotLightsIds() const;
  const Tab<uint16_t> &getVisibleFarOmniLightsIds() const;

  const Tab<RenderOmniLight> &getRenderOmniLightsFar() const;
  const Tab<RenderSpotLight> &getRenderSpotLightsFar() const;

  const Tab<vec4f> &getVisibleClusteredSpotLightsBounds() const;
  const Tab<vec4f> &getVisibleClusteredOmniLightsBounds() const;

  const OmniLightsCB &getVisibleClusteredOmniLightsCB(LightBufferSlot buffer_slot) const;
  const OmniLightsCB &getVisibleFarOmniLightsCB(LightBufferSlot buffer_slot) const;
  const SpotLightsCB &getVisibleClusteredSpotLightsCB(LightBufferSlot buffer_slot) const;
  const SpotLightsCB &getVisibleFarSpotLightsCB(LightBufferSlot buffer_slot) const;

  const UniqueBuf &getVisibleClusteredSpotLightsMasksSB() const;
  const UniqueBuf &getVisibleClusteredOmniLightsMasksSB() const;

  const UniqueBuf &getVisibleLightsDataBuffer() const;
  const UniqueBuf &getVisibleLightsCountsBuffer() const;

private:
  static void executeLightsCPUPartition(LightType light_type, int max_index, const LightsVisibilityChecker &checker,
    Tab<uint16_t> &clustered_lights, Tab<uint16_t> &far_lights);

  template <typename LightsManager>
  static void fillDerivativeLightsLists(const LightsManager *lights_manager, const Tab<uint16_t> &clustered_lights_ids,
    const Tab<uint16_t> &far_lights_ids, Tab<typename LightsManager::RenderLight> &clustered_render_lights,
    Tab<typename LightsManager::RenderLight> &far_render_lights, Tab<typename LightsManager::MaskType> &clustered_lights_masks,
    Tab<vec4f> &clustered_lights_bounds, typename LightsManager::MaskType default_mask_type);

  void updatePartitionFrustumUniforms(const Frustum &frustum, vec4f znear_plane, vec3f camera_pos, float zfar);

  void bindPartitionBuffers(const Frustum &frustum, vec4f znear_plane, vec3f camera_pos, float zfar);
  void unbindPartitionBuffers();

  void handleVisibleLightsIdListsOverflowCPU(Tab<uint16_t> &clustered_lights, Tab<uint16_t> &far_lights, int max_clustered_count);

  OmniLightsManager *omniLights;
  SpotLightsManager *spotLights;

  const LightsResourcesManager *lightsResMgr;

  LightsSorter lightsSorter;
  bool useGPUPartition = false;

  Tab<uint16_t> visibleClusteredSpotLightsIds;
  Tab<uint16_t> visibleClusteredOmniLightsIds;

  Tab<uint16_t> visibleFarSpotLightsIds;
  Tab<uint16_t> visibleFarOmniLightsIds;

  Tab<RenderOmniLight> renderOmniLightsClustered, renderOmniLightsFar;
  Tab<RenderSpotLight> renderSpotLightsClustered, renderSpotLightsFar;

  Tab<SpotLightMaskType> visibleSpotLightsMasks;
  Tab<OmniLightMaskType> visibleOmniLightsMasks;

  Tab<vec4f> visibleClusteredSpotLightsBounds;
  Tab<vec4f> visibleClusteredOmniLightsBounds;

  eastl::array<OmniLightsCB, LIGHTS_BUFFER_SLOTS> visibleClusteredOmniLightsCB, visibleFarOmniLightsCB;
  eastl::array<SpotLightsCB, LIGHTS_BUFFER_SLOTS> visibleClusteredSpotLightsCB, visibleFarSpotLightsCB;

  UniqueBuf visibleClusteredSpotLightsMasksSB;
  UniqueBuf visibleClusteredOmniLightsMasksSB;

  ComputeShader partitionCS;

  UniqueBuf frustumPlanesCB;

  UniqueBuf visibleLightsDataBuffer;
  UniqueBuf visibleLightsCountsBuffer;
};
