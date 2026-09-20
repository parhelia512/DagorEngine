// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <render/lights/dynamicLightShadows.h>
#include <render/lights/shadowSystem.h>
#include <render/lights/lightsVisibilityChecker.h>
#include <render/lights/renderLights.hlsli>
#include <render/depthUtil.h>
#include <drv/3d/dag_matricesAndPerspective.h>
#include <drv/3d/dag_driver.h>
#include <shaders/dag_overrideStates.h>
#include <shaders/dag_shaderVar.h>
#include <generic/dag_staticTab.h>
#include <perfMon/dag_statDrv.h>
#include <EASTL/type_traits.h>

DynamicLightShadows::DynamicLightShadows(const LightsResourcesManager *lights_res_mgr, OmniLightsManager *omni_lights_manager,
  SpotLightsManager *spot_lights_manager) :
  omniLightsManager(omni_lights_manager), spotLightsManager(spot_lights_manager), lightsResMgr(lights_res_mgr)
{}

void DynamicLightShadows::close()
{
  spotLightSsssShadowDescBuffer.close();
  for (auto &cb : commonLightShadowsBufferCB)
    cb.close();
}

void DynamicLightShadows::setShadowSystem(ShadowSystem *shadow_system) { shadowSystem = shadow_system; }

void DynamicLightShadows::resetShadowVolumeUpdateFlag(uint16_t shadow_id) { dynamicLightsShadowsVolumeSet.reset(shadow_id); }

void DynamicLightShadows::resetShadowVolumeUpdateFlags() { dynamicLightsShadowsVolumeSet.reset(); }

const DynamicLightShadows::CommonLightShadowsCBType &DynamicLightShadows::getInFrustumCommonLightShadowsCB(LightBufferSlot slot) const
{
  return commonLightShadowsBufferCB[size_t(slot)];
}

void DynamicLightShadows::setLightShadowVolume(BaseLightsManager *lights_manager, int light_id)
{
  uint32_t shadowId = lights_manager->getShadowId(light_id);
  if (shadowId == INVALID_SHADOW_VOLUME_ID || dynamicLightsShadowsVolumeSet.test(shadowId))
    return;

  lights_manager->updateShadowVolume(light_id);
  dynamicLightsShadowsVolumeSet.set(shadowId);
}

void DynamicLightShadows::framePrepareShadows(dynamic_shadow_render::FrameVolumeData &volume_data,
  const dynamic_shadow_render::FramePrepareShadowsParams &params)
{
  volume_data.clear();
  if (!shadowSystem || !params.lightVisibilityChecker)
    return;
  TIME_PROFILE(prepare_spot_omni_shadows);

  shadowSystem->startPrepareShadows();

  const auto shadowedLightCallback = [this, &params](BaseLightsManager *lights_manager, LightType light_type, uint32_t light_id) {
    if (params.lightVisibilityChecker->test(light_type, light_id) != LightsVisibilityChecker::TestResult::Clustered)
      return;
    setLightShadowVolume(lights_manager, light_id);
    shadowSystem->useShadowOnFrame(lights_manager->getShadowId(light_id));
  };

  spotLightsManager->forEachLightsWithCloseShadow(params.viewPos, maxShadowDist,
    [&](uint32_t light_id) { shadowedLightCallback(spotLightsManager, LightType::Spot, light_id); });

  omniLightsManager->forEachLightsWithCloseShadow(params.viewPos, maxShadowDist,
    [&](uint32_t light_id) { shadowedLightCallback(omniLightsManager, LightType::Omni, light_id); });

  shadowSystem->setDynamicObjectsContent(params.dynamicBoxes.data(), params.dynamicBoxes.size()); // dynamic content within those boxes

  float maxAreaToUpdate = max((float)maxShadowsToUpdateOnFrame / DEFAULT_MAX_SHADOWS_TO_UPDATE_PER_FRAME, 1.0f) * 0.25f;
  shadowSystem->endPrepareShadows(volume_data, maxShadowsToUpdateOnFrame, maxShadowViewsToUpdateOnFrame, maxAreaToUpdate,
    params.viewPos, params.cameraFocal, params.globtm);

  if (!params.collectUpdates)
    return;

  for (int i = volume_data.volumes.size() - 1; i >= 0; --i)
  {
    auto &vol = volume_data.volumes[i];
    const auto renderFlags = shadowSystem->getVolumeRenderFlags(vol.id);
    if (renderFlags & ShadowSystem::RENDER_STATIC)
    {
      vol.staticUpdateIndex = volume_data.staticUpdates.size();
      shadowSystem->getVolumeUpdateData(vol.id, volume_data.staticUpdates.emplace_back());
    }
    if (renderFlags & ShadowSystem::RENDER_DYNAMIC)
    {
      vol.dynamicUpdateIndex = volume_data.dynamicUpdates.size();
      shadowSystem->getVolumeUpdateData(vol.id, volume_data.dynamicUpdates.emplace_back());
    }
  }
}

void DynamicLightShadows::frameRenderShadows(const dynamic_shadow_render::FrameVolumeData &volume_data,
  StaticRenderCallback renderStatic, DynamicRenderCallback renderDynamic, shaders::OverrideStateId depth_bias_override_id,
  shaders::OverrideStateId depth_bias_two_sided_override_id)
{
  if (!shadowSystem)
    return;

  shadowSystem->setTextureToShader();
  if (volume_data.volumes.empty())
    return;

  shadowSystem->startRenderVolumes(volume_data);
  bool staticOverrideState = false;
  shaders::OverrideStateId originalState = shaders::overrides::get_current();
  for (int i = volume_data.volumes.size() - 1; i >= 0; --i)
  {
    const auto &vol = volume_data.volumes[i];
    shaders::overrides::set(depth_bias_override_id);
    mat44f view, proj, viewItm;
    const int id = vol.id;
    ShadowSystem::RenderFlags renderFlags;
    uint32_t numViews = shadowSystem->startRenderVolume(id, proj, renderFlags);
    if (renderFlags & ShadowSystem::RENDER_STATIC)
    {
      TIME_D3D_PROFILE(staticShadow);
      if (!staticOverrideState)
      {
        shaders::overrides::reset();
        shaders::overrides::set(depth_bias_override_id);
        staticOverrideState = true;
      }

      for (uint32_t viewId = 0; viewId < numViews; ++viewId)
      {
        shadowSystem->startRenderVolumeView(id, viewId, viewItm, view, renderFlags, ShadowSystem::RENDER_STATIC);
        alignas(16) TMatrix viewItmS;
        v_mat_43ca_from_mat44(viewItmS[0], viewItm);

        d3d::settm(TM_VIEW, view);
        d3d::settm(TM_PROJ, proj);
        mat44f globTm;
        v_mat44_mul(globTm, proj, view);

        bool hint_dynamic;
        ShadowCastersFlag casters;
        uint8_t priority, shadow_size_srl;
        uint16_t quality;
        DynamicShadowRenderGPUObjects render_gpu_objects;
        shadowSystem->getShadowProperties(id, casters, hint_dynamic, quality, priority, shadow_size_srl, render_gpu_objects);

        renderStatic(globTm, proj, viewItmS, vol.staticUpdateIndex, viewId, render_gpu_objects);
        shadowSystem->endRenderVolumeView(id, viewId);
      }
      shadowSystem->endRenderStatic(id);
    }
    if (renderFlags & ShadowSystem::RENDER_DYNAMIC)
    {
      TIME_D3D_PROFILE(dynamicShadow);
      staticOverrideState = false;
      shaders::overrides::reset(); // startRenderDynamic uses an other state
      shadowSystem->startRenderDynamic(id);
      shaders::overrides::set(shadowSystem->isShadowTwoSided(id) ? depth_bias_two_sided_override_id : depth_bias_override_id);
      for (uint32_t viewId = 0; viewId < numViews; ++viewId)
      {
        shadowSystem->startRenderVolumeView(id, viewId, viewItm, view, renderFlags, ShadowSystem::RENDER_DYNAMIC);
        alignas(16) TMatrix viewItmS;
        v_mat_43ca_from_mat44(viewItmS[0], viewItm);

        d3d::settm(TM_VIEW, view);
        d3d::settm(TM_PROJ, proj);

        renderDynamic(viewItmS, view, proj, vol.dynamicUpdateIndex, viewId);
        shadowSystem->endRenderVolumeView(id, viewId);
      }
      shaders::overrides::reset();
    }
    shadowSystem->endRenderVolume(id);
    shaders::overrides::reset();
  }
  shaders::overrides::reset();
  shaders::overrides::set(originalState);
  shadowSystem->endRenderVolumes();
}

template <typename LightsManagerT>
void DynamicLightShadows::fillLightShadowData(LightsManagerT *lights_manager, ShadowSystem *shadow_system,
  CommonLightShadowsDataType &common_shadow_data, const Tab<uint16_t> &visible_light_ids, uint32_t base_index)
{
  constexpr bool isSpot = eastl::is_same_v<LightsManagerT, SpotLightsManager>;
  constexpr uint32_t STRIDE = isSpot ? SPOT_LIGHT_SHADOW_DATA_STRIDE : OMNI_LIGHT_SHADOW_DATA_STRIDE;
  for (int i = 0, ie = visible_light_ids.size(); i < ie; ++i)
  {
    const auto shadowId = lights_manager->getShadowId(visible_light_ids[i]);
    const bool isActualShadowData =
      shadowId != INVALID_SHADOW_VOLUME_ID && shadow_system && shadow_system->hasVolumeEverBeenRendered(shadowId);
    if constexpr (isSpot)
    {
      if (isActualShadowData)
      {
        memcpy(&common_shadow_data[base_index + i * STRIDE], &shadow_system->getVolumeTexMatrix(shadowId), 4 * sizeof(Point4));
        common_shadow_data[base_index + i * STRIDE + 4] = shadow_system->getShadowUvMinMax(shadowId);
      }
      else
      {
        memset(&common_shadow_data[base_index + i * STRIDE], 0, 4 * sizeof(Point4));
        common_shadow_data[base_index + i * STRIDE + 4] = Point4(0, 0, 1, 1);
      }
    }
    else
    {
      if (isActualShadowData)
        common_shadow_data[base_index + i * STRIDE] = shadow_system->getOctahedralVolumeTexData(shadowId);
      else
        memset(&common_shadow_data[base_index + i * STRIDE], 0, sizeof(Point4));
    }
  }
}

void DynamicLightShadows::updateCommonLightShadowsCB(CommonLightShadowsCBType &shadow_command_buffer, const char *buffer_name,
  const Tab<uint16_t> &visible_spot_light_ids, const Tab<uint16_t> &visible_omni_light_ids, bool always_write_header)
{
  const uint32_t commonShadowDataSize =
    1 + visible_spot_light_ids.size() * SPOT_LIGHT_SHADOW_DATA_STRIDE + visible_omni_light_ids.size() * OMNI_LIGHT_SHADOW_DATA_STRIDE;

  // FIXME: (workaround) buffer is persistent as it referenced by volume lights when data is not updated in clustered lights
  shadow_command_buffer.reallocate(commonShadowDataSize, COMMON_LIGHT_SHADOWS_DATA_CAPACITY, lightsResMgr->getResName(buffer_name),
    true);

  const bool hasLights = !visible_spot_light_ids.empty() || !visible_omni_light_ids.empty();
  if (!hasLights && !always_write_header)
  {
    shadow_command_buffer.update(nullptr, 0);
    return;
  }

  // Per spot: 4 float4 tex matrix rows + 1 float4 atlas-UV bounds (rectMin.xy, rectMax.xy).
  CommonLightShadowsDataType commonShadowData;
  commonShadowData.resize(commonShadowDataSize);
  commonShadowData[0] = Point4(visible_spot_light_ids.size(), visible_omni_light_ids.size(),
    SPOT_LIGHT_SHADOW_DATA_STRIDE * visible_spot_light_ids.size(), 0);

  fillLightShadowData(spotLightsManager, shadowSystem, commonShadowData, visible_spot_light_ids, 1);
  fillLightShadowData(omniLightsManager, shadowSystem, commonShadowData, visible_omni_light_ids,
    1 + visible_spot_light_ids.size() * SPOT_LIGHT_SHADOW_DATA_STRIDE);

  shadow_command_buffer.update(commonShadowData.data(), data_size(commonShadowData));
}

void DynamicLightShadows::updateOutOfFrustumCommonLightShadowsCB(const Tab<uint16_t> &visible_spot_light_ids,
  const Tab<uint16_t> &visible_omni_light_ids)
{
  const bool hasLights = !visible_spot_light_ids.empty() || !visible_omni_light_ids.empty();
  if (hasLights || !outOfFrustumCommonLightShadowsAreEmpty)
    updateCommonLightShadowsCB(outOfFrustumCommonLightShadowsCB, "out_of_frustum_common_lights_shadow_data", visible_spot_light_ids,
      visible_omni_light_ids, false);
  outOfFrustumCommonLightShadowsAreEmpty = !hasLights;
}

void DynamicLightShadows::setEmptyOutOfFrustumCommonLightShadowsCB()
{
  outOfFrustumCommonLightShadowsCB.reallocate(1, COMMON_LIGHT_SHADOWS_DATA_CAPACITY,
    lightsResMgr->getResName("out_of_frustum_common_lights_shadow_data"), true /*persistent*/);
  if (!outOfFrustumCommonLightShadowsAreEmpty)
    outOfFrustumCommonLightShadowsCB.update(nullptr, 0);
  outOfFrustumCommonLightShadowsAreEmpty = true;
}

void DynamicLightShadows::afterResetDevice()
{
  if (outOfFrustumCommonLightShadowsAreEmpty)
    outOfFrustumCommonLightShadowsCB.update(nullptr, 0);
}

void DynamicLightShadows::updateVisibleLightShadowBuffers(LightBufferSlot current_buffer_slot,
  const Tab<uint16_t> &visible_spot_light_ids, const Tab<uint16_t> &visible_omni_light_ids)
{
  auto &commonShadowsCB = commonLightShadowsBufferCB[size_t(current_buffer_slot)];
  updateCommonLightShadowsCB(commonShadowsCB,
    current_buffer_slot != LightBufferSlot::Main ? "common_lights_shadows_secondary" : "common_lights_shadows", visible_spot_light_ids,
    visible_omni_light_ids, true);

  // Secondary views keep the main view SSSS data.
  const int numSpotShadows = min<int>(visible_spot_light_ids.size(), MAX_CLUSTERED_SPOT_LIGHTS);
  if (current_buffer_slot == LightBufferSlot::Main && spotLightSsssShadowDescBuffer && numSpotShadows > 0 && shadowSystem)
  {
    StaticTab<SpotlightShadowDescriptor, MAX_CLUSTERED_SPOT_LIGHTS> spotLightSsssShadowDesc;
    spotLightSsssShadowDesc.resize(numSpotShadows);
    for (int i = 0; i < visible_spot_light_ids.size(); ++i)
    {
      uint16_t shadowId = spotLightsManager->getShadowId(visible_spot_light_ids[i]);
      if (shadowId != INVALID_SHADOW_VOLUME_ID)
      {
        SpotlightShadowDescriptor &shadowDesc = spotLightSsssShadowDesc[i];

        float wk;
        Point2 zn_zfar;
        shadowSystem->getVolumeInfo(shadowId, wk, zn_zfar.x, zn_zfar.y);
        shadowDesc.decodeDepth = get_decode_depth(zn_zfar);

        Point2 shadowUvSize = shadowSystem->getShadowUvSize(shadowId);
        shadowDesc.meterToUvAtZfar = max(shadowUvSize.x, shadowUvSize.y) / (2 * wk);
        Point4 shadowUvMinMax = shadowSystem->getShadowUvMinMax(shadowId);
        shadowDesc.uvMinMax = shadowUvMinMax;

        bool hintDynamic;
        ShadowCastersFlag casters;
        uint16_t quality;
        uint8_t priority, size;
        DynamicShadowRenderGPUObjects renderGPUObjects;
        shadowSystem->getShadowProperties(shadowId, casters, hintDynamic, quality, priority, size, renderGPUObjects);
        shadowDesc.hasDynamic = static_cast<float>((casters & ShadowCastersFlag::Dynamic) != ShadowCastersFlag::None);
      }
      else
      {
        spotLightSsssShadowDesc[i] = {};
      }
    }

    spotLightSsssShadowDescBuffer.getBuf()->updateData(0, numSpotShadows * sizeof(SpotlightShadowDescriptor),
      static_cast<const void *>(spotLightSsssShadowDesc.data()), VBLOCK_WRITEONLY | VBLOCK_DISCARD);
  }
}

dynamic_shadow_render::QualityParams DynamicLightShadows::getQualityParams() const
{
  dynamic_shadow_render::QualityParams result;
  result.maxShadowsToUpdateOnFrame = maxShadowsToUpdateOnFrame;
  result.maxShadowViewsToUpdateOnFrame = maxShadowViewsToUpdateOnFrame;
  result.maxShadowDist = maxShadowDist;
  return result;
}

void DynamicLightShadows::setNeedSsss(bool need_ssss)
{
  spotLightSsssShadowDescBuffer.close();
  if (need_ssss)
    spotLightSsssShadowDescBuffer = dag::create_sbuffer(sizeof(SpotlightShadowDescriptor), MAX_CLUSTERED_SPOT_LIGHTS,
      SBCF_DYNAMIC | SBCF_CPU_ACCESS_WRITE | SBCF_BIND_SHADER_RES | SBCF_MISC_STRUCTURED, 0, "spot_lights_ssss_shadow_desc",
      RESTAG_LIGHTS);
}
