// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <render/lights/lightsPartition.h>
#include <render/lights/lightsPartition.hlsli>
#include <math/dag_mathBase.h>
#include <generic/dag_align.h>
#include <shaders/dag_shaders.h>
#include <shaders/dag_shaderVariableInfo.h>
#include <shaders/dag_shaderVar.h>
#include <perfMon/dag_statDrv.h>
#include <drv/3d/dag_rwResource.h>
#include <drv/3d/dag_barrier.h>
#include <drv/3d/dag_driver.h>
#include <3d/dag_resourceTags.h>

#define LIGHTS_PARTITION_GLOBAL_VARS_LIST              \
  VAR(partition_lights_frustum_planes)                 \
  VAR(partition_lights_cutoff_dist_sq)                 \
  VAR(partition_lights_mark_small_lights_as_far_limit) \
  VAR(partition_lights_omni_require_any_mask)          \
  VAR(partition_lights_spot_require_any_mask)          \
  VAR(partition_lights_camera_position)                \
  VAR(partition_lights_znear_plane)                    \
  VAR(partition_lights_inverse_zfar)                   \
  VAR(scene_omni_managed_lights)                       \
  VAR(scene_omni_render_lights)                        \
  VAR(scene_spot_managed_lights)                       \
  VAR(scene_spot_render_lights)                        \
  VAR(visible_lights_data_buf)                         \
  VAR(visible_lights_counts_buf)

#define VAR(a) static ShaderVariableInfo a##VarId(#a, true);
LIGHTS_PARTITION_GLOBAL_VARS_LIST
#undef VAR
#undef LIGHTS_PARTITION_GLOBAL_VARS_LIST

struct DispatchLightsPartitionBuffers
{
  Sbuffer *countsBuf;
};

struct DispatchLightsPartitionParams
{
  OmniLightsManager &omniLights;
  SpotLightsManager &spotLights;
  ComputeShader &cs;
  DispatchLightsPartitionBuffers bufs;
  int omniRequireAnyMask;
  int spotRequireAnyMask;
  float markSmallLightsAsFarLimit;
  float cutoffDistSq;
};

static void dispatch_lights_partition(const DispatchLightsPartitionParams &p)
{
  TIME_D3D_PROFILE(lightsPartition_gpu_impl);
  G_ASSERTF(p.cs, "dispatch_lights_partition: compute shader not found");

  Sbuffer *sceneOmniManagedBuf = p.omniLights.getSceneManagedLightsBuffer();
  Sbuffer *sceneOmniRenderBuf = p.omniLights.getSceneRenderLightsBuffer();
  Sbuffer *sceneSpotManagedBuf = p.spotLights.getSceneManagedLightsBuffer();
  Sbuffer *sceneSpotRenderBuf = p.spotLights.getSceneRenderLightsBuffer();

  G_ASSERTF(sceneOmniManagedBuf, "dispatch_lights_partition: omni managed lights buf is null");
  G_ASSERTF(sceneOmniRenderBuf, "dispatch_lights_partition: omni render lights buf is null");
  G_ASSERTF(sceneSpotManagedBuf, "dispatch_lights_partition: spot managed lights buf is null");
  G_ASSERTF(sceneSpotRenderBuf, "dispatch_lights_partition: spot render lights buf is null");

  d3d::zero_rwbufi(p.bufs.countsBuf);
  d3d::resource_barrier({p.bufs.countsBuf, RB_FLUSH_UAV | RB_SOURCE_STAGE_COMPUTE | RB_STAGE_COMPUTE});

  ShaderGlobal::set_float(partition_lights_cutoff_dist_sqVarId, p.cutoffDistSq > 0.f ? p.cutoffDistSq : MAX_REAL);
  ShaderGlobal::set_float(partition_lights_mark_small_lights_as_far_limitVarId, p.markSmallLightsAsFarLimit);
  ShaderGlobal::set_int(partition_lights_omni_require_any_maskVarId, p.omniRequireAnyMask);
  ShaderGlobal::set_int(partition_lights_spot_require_any_maskVarId, p.spotRequireAnyMask);

  p.cs.dispatchThreads(MAX_SCENE_OMNI_LIGHTS + MAX_SCENE_SPOT_LIGHTS, 1, 1);
}

LightsPartition::LightsPartition(OmniLightsManager &omni_lights, SpotLightsManager &spot_lights,
  const LightsResourcesManager *lights_res_mgr) :
  omniLights(&omni_lights), spotLights(&spot_lights), lightsResMgr(lights_res_mgr), lightsSorter(omni_lights, spot_lights)
{}

void LightsPartition::init(bool use_gpu_partition)
{
  if (VariableMap::isVariablePresent(VariableMap::getVariableId("spot_lights_flags")))
  {
    static constexpr uint32_t spotMaskSizeInDwords = dag::divide_align_up(MAX_CLUSTERED_SPOT_LIGHTS, 4);
    visibleClusteredSpotLightsMasksSB = dag::buffers::create_one_frame_sr_byte_address(spotMaskSizeInDwords,
      lightsResMgr->getResName("spot_lights_flags"), RESTAG_LIGHTS);
  }

  if (VariableMap::isVariablePresent(VariableMap::getVariableId("omni_lights_flags")))
  {
    static constexpr uint32_t omniMaskSizeInDwords = dag::divide_align_up(MAX_CLUSTERED_OMNI_LIGHTS, 4);
    visibleClusteredOmniLightsMasksSB = dag::buffers::create_one_frame_sr_byte_address(omniMaskSizeInDwords,
      lightsResMgr->getResName("omni_lights_flags"), RESTAG_LIGHTS);
  }

  visibleClusteredOmniLightsCB[0].reallocate(0, MAX_CLUSTERED_OMNI_LIGHTS, lightsResMgr->getResName("clustered_omni_lights"), true);
  visibleClusteredOmniLightsCB[0].update(nullptr, 0);
  visibleClusteredSpotLightsCB[0].reallocate(0, MAX_CLUSTERED_SPOT_LIGHTS, lightsResMgr->getResName("clustered_spot_lights"), true);
  visibleClusteredSpotLightsCB[0].update(nullptr, 0);

  visibleFarOmniLightsCB[0].reallocate(0, MAX_VISIBLE_FAR_LIGHTS, lightsResMgr->getResName("far_omni_lights"), true);
  visibleFarOmniLightsCB[0].update(nullptr, 0);
  visibleFarSpotLightsCB[0].reallocate(0, MAX_VISIBLE_FAR_LIGHTS, lightsResMgr->getResName("far_spot_lights"), true);
  visibleFarSpotLightsCB[0].update(nullptr, 0);

  useGPUPartition = use_gpu_partition;
  if (useGPUPartition)
  {
    partitionCS = ComputeShader("partition_lights_cs", true);

    if (!partitionCS)
    {
      useGPUPartition = false;
    }
  }

  if (useGPUPartition)
  {
    frustumPlanesCB = dag::buffers::create_one_frame_cb(dag::buffers::cb_array_reg_count<Point4>(6),
      lightsResMgr->getResName("partition_lights_frustum_planes"));

    visibleLightsDataBuffer = dag::buffers::create_ua_sr_byte_address(LIGHTS_DATA_BUF_RECORDS_COUNT,
      lightsResMgr->getResName("lights_ids"), d3d::buffers::Init::No, RESTAG_LIGHTS);

    visibleLightsCountsBuffer = dag::buffers::create_ua_sr_structured(sizeof(uint32_t), LIGHTS_COUNTS_BUF_RECORDS_COUNT,
      lightsResMgr->getResName("lights_counts"), d3d::buffers::Init::No, RESTAG_LIGHTS);

    lightsSorter.initGpuMode();
  }
}

bool LightsPartition::isGPU() const { return useGPUPartition; }

bool LightsPartition::isGPUSortAvailable() const { return lightsSorter.isGPUSortAvailable(); }

void LightsPartition::executeLightsCPUPartition(LightType light_type, int max_index, const LightsVisibilityChecker &checker,
  Tab<uint16_t> &clustered_lights, Tab<uint16_t> &far_lights)
{
  TIME_D3D_PROFILE(lightsPartition_cpu_impl);

  clustered_lights.clear();
  far_lights.clear();

  const int reserveSize = (max_index + 1) / 2;
  clustered_lights.reserve(reserveSize);
  far_lights.reserve(reserveSize);

  for (int i = 0; i <= max_index; ++i)
  {
    switch (checker.test(light_type, i))
    {
      case LightsVisibilityChecker::TestResult::Clustered: clustered_lights.push_back(i); break;
      case LightsVisibilityChecker::TestResult::Far: far_lights.push_back(i); break;
      case LightsVisibilityChecker::TestResult::Invisible: break;
      default:
        G_ASSERT_FAIL("LightsPartition::executeLightsCPUPartition - unknow test result for light of type '%d' and id '%d'",
          static_cast<uint32_t>(light_type), i);
        break;
    }
  };
};

template <typename TRenderLight>
static void update_render_lights_const_buffer(int elem_count, int max_count, Tab<TRenderLight> &render_lights,
  ReallocatableLightsConstBuffer<sizeof(TRenderLight) / sizeof(vec4f), true> &cb, bool persistent, const char *name,
  const LightsResourcesManager *lights_res_mgr)
{
  G_ASSERT(elem_count <= render_lights.size());
  G_ASSERT(sizeof(TRenderLight) % sizeof(vec4f) == 0);

  cb.reallocate(elem_count, max_count, lights_res_mgr->getResName(name), persistent);
  cb.update(render_lights.data(), elem_count * sizeof(TRenderLight));
};

template <typename TMaskType>
static void update_masks_buffer(int elem_count, int max_count, Tab<TMaskType> &masks_list, Sbuffer *buf, TMaskType stub_mask_value)
{
  if (buf)
  {
    const TMaskType stubMask[1] = {stub_mask_value};
    G_ASSERT(masks_list.size() <= ((max_count + 3) & ~3));
    dag::Span<const TMaskType> masks = elem_count > 0 ? make_span_const(masks_list) : make_span_const(stubMask);
    // bound & used framemem buffer must be updated every frame
    buf->updateDataWithLock(0, data_size(masks), masks.data(), VBLOCK_DISCARD);
  }
};


void LightsPartition::executeLightsCPUPartition(const ExecuteLightsCPUPartitionParams &params) const
{
  TIME_D3D_PROFILE(lightsPartition_cpu_executePartition);

  executeLightsCPUPartition(LightType::Omni, omniLights->maxIndex(), params.checker, params.omniLightsClustered, params.omniLightsFar);
  executeLightsCPUPartition(LightType::Spot, spotLights->maxIndex(), params.checker, params.spotLightsClustered, params.spotLightsFar);
}

void LightsPartition::prepareClusteredAndFarLightBuffersCPU(const LightsVisibilityChecker &checker)
{
  TIME_D3D_PROFILE(lightsPartition_cpu_prepareClusteredAndFarLightBuffers);

  executeLightsCPUPartition(LightType::Omni, omniLights->maxIndex(), checker, visibleClusteredOmniLightsIds, visibleFarOmniLightsIds);
  executeLightsCPUPartition(LightType::Spot, spotLights->maxIndex(), checker, visibleClusteredSpotLightsIds, visibleFarSpotLightsIds);

  lightsSorter.sortLightsCPU(visibleClusteredOmniLightsIds, visibleClusteredSpotLightsIds, checker.getTestParams().cameraPos);

  handleVisibleLightsIdListsOverflowCPU(visibleClusteredOmniLightsIds, visibleFarOmniLightsIds, MAX_CLUSTERED_OMNI_LIGHTS);
  handleVisibleLightsIdListsOverflowCPU(visibleClusteredSpotLightsIds, visibleFarSpotLightsIds, MAX_CLUSTERED_SPOT_LIGHTS);

  {
    fillDerivativeLightsLists(omniLights, visibleClusteredOmniLightsIds, visibleFarOmniLightsIds, renderOmniLightsClustered,
      renderOmniLightsFar, visibleOmniLightsMasks, visibleClusteredOmniLightsBounds, OmniLightMaskType::OMNI_LIGHT_MASK_NONE);
    fillDerivativeLightsLists(spotLights, visibleClusteredSpotLightsIds, visibleFarSpotLightsIds, renderSpotLightsClustered,
      renderSpotLightsFar, visibleSpotLightsMasks, visibleClusteredSpotLightsBounds, SpotLightMaskType::SPOT_LIGHT_MASK_NONE);
  }
}

void LightsPartition::updatePartitionFrustumUniforms(const Frustum &frustum, vec4f znear_plane, vec3f camera_pos, float zfar)
{
  Point4 planes[6];
  v_stu(&planes[0].x, frustum.plane03X);
  v_stu(&planes[1].x, frustum.plane03Y);
  v_stu(&planes[2].x, frustum.plane03Z);
  v_stu(&planes[3].x, frustum.plane03W);
  v_stu(&planes[4].x, frustum.camPlanes[Frustum::FARPLANE]);
  v_stu(&planes[5].x, frustum.camPlanes[Frustum::NEARPLANE]);

  frustumPlanesCB->updateData(0, sizeof(planes), planes, VBLOCK_WRITEONLY | VBLOCK_DISCARD);
  partition_lights_frustum_planesVarId.set_buffer(frustumPlanesCB.getBuf());

  ShaderGlobal::set_float4(partition_lights_znear_planeVarId, znear_plane);
  ShaderGlobal::set_float4(partition_lights_camera_positionVarId, camera_pos);
  ShaderGlobal::set_float(partition_lights_inverse_zfarVarId, 1.f / zfar);
}

void LightsPartition::bindPartitionBuffers(const Frustum &frustum, vec4f znear_plane, vec3f camera_pos, float zfar)
{
  updatePartitionFrustumUniforms(frustum, znear_plane, camera_pos, zfar);
  scene_omni_managed_lightsVarId.set_buffer(omniLights->getSceneManagedLightsBuffer());
  scene_omni_render_lightsVarId.set_buffer(omniLights->getSceneRenderLightsBuffer());
  scene_spot_managed_lightsVarId.set_buffer(spotLights->getSceneManagedLightsBuffer());
  scene_spot_render_lightsVarId.set_buffer(spotLights->getSceneRenderLightsBuffer());
  visible_lights_data_bufVarId.set_buffer(visibleLightsDataBuffer.getBuf());
  visible_lights_counts_bufVarId.set_buffer(visibleLightsCountsBuffer.getBuf());
}

void LightsPartition::unbindPartitionBuffers()
{
  scene_omni_managed_lightsVarId.set_buffer(nullptr);
  scene_omni_render_lightsVarId.set_buffer(nullptr);
  scene_spot_managed_lightsVarId.set_buffer(nullptr);
  scene_spot_render_lightsVarId.set_buffer(nullptr);
  visible_lights_data_bufVarId.set_buffer(nullptr);
  visible_lights_counts_bufVarId.set_buffer(nullptr);
  partition_lights_frustum_planesVarId.set_buffer(nullptr);
}

void LightsPartition::executeLightsGPUPartition(const LightsVisibilityChecker::TestParameters &test_params, float zfar,
  bool update_variables)
{
  G_ASSERT_RETURN(isGPU(), );
  G_ASSERT(zfar > 0.f);

  TIME_D3D_PROFILE(lightsPartition_gpu_executePartition);

  Sbuffer *countsBuf = visibleLightsCountsBuffer.getBuf();

  if (update_variables)
    bindPartitionBuffers(test_params.frustum, test_params.znearPlane, test_params.cameraPos, zfar);

  dispatch_lights_partition({
    .omniLights = *omniLights,
    .spotLights = *spotLights,
    .cs = partitionCS,
    .bufs = {.countsBuf = countsBuf},
    .omniRequireAnyMask = static_cast<int>(test_params.omniRequireAnyMask),
    .spotRequireAnyMask = static_cast<int>(test_params.spotRequireAnyMask),
    .markSmallLightsAsFarLimit = test_params.markSmallLightsAsFarLimit,
    .cutoffDistSq = test_params.cutoffDistSq,
  });

  if (update_variables)
    unbindPartitionBuffers();
}

void LightsPartition::prepareClusteredAndFarLightBuffersGPU(const LightsVisibilityChecker::TestParameters &test_params, float zfar)
{
  G_ASSERT_RETURN(isGPU(), );
  G_ASSERT_RETURN(lightsSorter.isGPUSortAvailable(), );

  TIME_D3D_PROFILE(lightsPartition_gpu_prepareClusteredAndFarLightBuffers);

  Sbuffer *dataBuf = visibleLightsDataBuffer.getBuf();
  Sbuffer *countsBuf = visibleLightsCountsBuffer.getBuf();

  bindPartitionBuffers(test_params.frustum, test_params.znearPlane, test_params.cameraPos, zfar);
  lightsSorter.bindStageBuffers();

  executeLightsGPUPartition(test_params, zfar, /*update_variables*/ false);

  d3d::resource_barrier({{dataBuf, countsBuf},
    {RB_FLUSH_UAV | RB_SOURCE_STAGE_COMPUTE | RB_STAGE_COMPUTE, RB_FLUSH_UAV | RB_SOURCE_STAGE_COMPUTE | RB_STAGE_COMPUTE}});

  lightsSorter.sortLightsGPU(dataBuf, countsBuf, MAX_VISIBLE_FAR_LIGHTS, /*update_variables*/ false);

  lightsSorter.unbindStageBuffers();
  unbindPartitionBuffers();
}

void LightsPartition::handleVisibleLightsIdListsOverflowCPU(Tab<uint16_t> &clustered_lights, Tab<uint16_t> &far_lights,
  int max_clustered_count)
{
  TIME_D3D_PROFILE(lightsPartition_cpu_handleVisibleLightsIdListsOverflow);

  if (clustered_lights.size() > max_clustered_count)
  {
    auto excessSize = clustered_lights.size() - max_clustered_count;
    append_items(far_lights, excessSize, clustered_lights.begin() + max_clustered_count);
  }
  clustered_lights.resize(min(int(clustered_lights.size()), int(max_clustered_count)));
  far_lights.resize(min<int>(far_lights.size(), MAX_VISIBLE_FAR_LIGHTS));
}

template <typename LightsManager>
void LightsPartition::fillDerivativeLightsLists(const LightsManager *lights_manager, const Tab<uint16_t> &clustered_lights_id,
  const Tab<uint16_t> &far_lights_ids, Tab<typename LightsManager::RenderLight> &clustered_render_lights,
  Tab<typename LightsManager::RenderLight> &far_render_lights, Tab<typename LightsManager::MaskType> &clustered_lights_masks,
  Tab<vec4f> &clustered_lights_bounds, typename LightsManager::MaskType default_mask_type)
{
  TIME_D3D_PROFILE(lightsPartition_cpu_fillDerivativeLightsLists);

  clustered_render_lights.resize(clustered_lights_id.size());
  clustered_lights_masks.resize((clustered_lights_id.size() + 3) & ~3);
  clustered_lights_bounds.resize(clustered_lights_id.size());

  far_render_lights.resize(far_lights_ids.size());
  for (int i = 0, e = far_lights_ids.size(); i < e; ++i)
    far_render_lights[i] = lights_manager->getRenderLight(far_lights_ids[i]);

  for (int i = 0, e = clustered_lights_id.size(); i < e; ++i)
  {
    uint32_t id = clustered_lights_id[i];
    clustered_render_lights[i] = lights_manager->getRenderLight(id);
    clustered_lights_bounds[i] = lights_manager->getBoundingSphere(id);
    clustered_lights_masks[i] = lights_manager->getLightMask(id);
  }

  for (int i = clustered_lights_id.size(), e = (clustered_lights_id.size() + 3) & ~3; i < e; ++i)
    clustered_lights_masks[i] = default_mask_type;
}

void LightsPartition::close()
{
  visibleClusteredSpotLightsMasksSB.close();
  visibleClusteredOmniLightsMasksSB.close();

  for (uint32_t slot = 0; slot < LIGHTS_BUFFER_SLOTS; ++slot)
  {
    visibleClusteredOmniLightsCB[slot].close();
    visibleFarOmniLightsCB[slot].close();
    visibleClusteredSpotLightsCB[slot].close();
    visibleFarSpotLightsCB[slot].close();
  }
}

void LightsPartition::updateBuffersForVisibleFarLights(LightBufferSlot buffer_slot)
{
  const size_t slot = size_t(buffer_slot);
  const bool secondary = buffer_slot != LightBufferSlot::Main;
  update_render_lights_const_buffer(renderSpotLightsFar.size(), MAX_VISIBLE_FAR_LIGHTS, renderSpotLightsFar,
    visibleFarSpotLightsCB[slot], false, secondary ? "far_spot_lights_secondary" : "far_spot_lights", lightsResMgr);
  update_render_lights_const_buffer(renderOmniLightsFar.size(), MAX_VISIBLE_FAR_LIGHTS, renderOmniLightsFar,
    visibleFarOmniLightsCB[slot], false, secondary ? "far_omni_lights_secondary" : "far_omni_lights", lightsResMgr);
}

void LightsPartition::updateBuffersForVisibleClusteredLights(LightBufferSlot buffer_slot, int omni_count, int spot_count)
{
  // FIXME: (workaround) buffers are persistent as it referenced by volume lights and eye caustics when data is not updated in
  // clustered lights
  const size_t slot = size_t(buffer_slot);
  const bool secondary = buffer_slot != LightBufferSlot::Main;
  update_render_lights_const_buffer(spot_count, MAX_CLUSTERED_SPOT_LIGHTS, renderSpotLightsClustered,
    visibleClusteredSpotLightsCB[slot], true, secondary ? "clustered_spot_lights_secondary" : "clustered_spot_lights", lightsResMgr);
  update_render_lights_const_buffer(omni_count, MAX_CLUSTERED_OMNI_LIGHTS, renderOmniLightsClustered,
    visibleClusteredOmniLightsCB[slot], true, secondary ? "clustered_omni_lights_secondary" : "clustered_omni_lights", lightsResMgr);


  update_masks_buffer(omni_count, MAX_CLUSTERED_OMNI_LIGHTS, visibleOmniLightsMasks, visibleClusteredOmniLightsMasksSB.getBuf(),
    OmniLightMaskType::OMNI_LIGHT_MASK_NONE);
  update_masks_buffer(spot_count, MAX_CLUSTERED_SPOT_LIGHTS, visibleSpotLightsMasks, visibleClusteredSpotLightsMasksSB.getBuf(),
    SpotLightMaskType::SPOT_LIGHT_MASK_NONE);
}

const Tab<uint16_t> &LightsPartition::getVisibleClusteredSpotLightsIds() const { return visibleClusteredSpotLightsIds; }
const Tab<uint16_t> &LightsPartition::getVisibleClusteredOmniLightsIds() const { return visibleClusteredOmniLightsIds; }

const Tab<uint16_t> &LightsPartition::getVisibleFarSpotLightsIds() const { return visibleFarSpotLightsIds; }
const Tab<uint16_t> &LightsPartition::getVisibleFarOmniLightsIds() const { return visibleFarOmniLightsIds; }

const Tab<RenderOmniLight> &LightsPartition::getRenderOmniLightsFar() const { return renderOmniLightsFar; }
const Tab<RenderSpotLight> &LightsPartition::getRenderSpotLightsFar() const { return renderSpotLightsFar; }

const Tab<vec4f> &LightsPartition::getVisibleClusteredSpotLightsBounds() const { return visibleClusteredSpotLightsBounds; }
const Tab<vec4f> &LightsPartition::getVisibleClusteredOmniLightsBounds() const { return visibleClusteredOmniLightsBounds; }

const LightsPartition::OmniLightsCB &LightsPartition::getVisibleClusteredOmniLightsCB(LightBufferSlot buffer_slot) const
{
  return visibleClusteredOmniLightsCB[size_t(buffer_slot)];
}
const LightsPartition::OmniLightsCB &LightsPartition::getVisibleFarOmniLightsCB(LightBufferSlot buffer_slot) const
{
  return visibleFarOmniLightsCB[size_t(buffer_slot)];
}
const LightsPartition::SpotLightsCB &LightsPartition::getVisibleClusteredSpotLightsCB(LightBufferSlot buffer_slot) const
{
  return visibleClusteredSpotLightsCB[size_t(buffer_slot)];
}
const LightsPartition::SpotLightsCB &LightsPartition::getVisibleFarSpotLightsCB(LightBufferSlot buffer_slot) const
{
  return visibleFarSpotLightsCB[size_t(buffer_slot)];
}

const UniqueBuf &LightsPartition::getVisibleClusteredSpotLightsMasksSB() const { return visibleClusteredSpotLightsMasksSB; }
const UniqueBuf &LightsPartition::getVisibleClusteredOmniLightsMasksSB() const { return visibleClusteredOmniLightsMasksSB; }

const UniqueBuf &LightsPartition::getVisibleLightsDataBuffer() const
{
  G_ASSERT(isGPU());
  return visibleLightsDataBuffer;
}

const UniqueBuf &LightsPartition::getVisibleLightsCountsBuffer() const
{
  G_ASSERT(isGPU());
  return visibleLightsCountsBuffer;
}
