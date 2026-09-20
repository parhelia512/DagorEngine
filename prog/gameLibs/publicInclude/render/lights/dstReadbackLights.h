//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <render/lights/shadowSystem.h>
#include <render/lights/spotLightsManager.h>
#include <generic/dag_functionRef.h>
#include <EASTL/unique_ptr.h>

#include <shaders/dag_computeShaders.h>
#include <3d/dag_ringCPUQueryLock.h>
#include <dag/dag_vectorMap.h>

class DistanceReadbackLights
{
  ShadowSystem *lightShadows;
  SpotLightsManager *spotLights;
  eastl::unique_ptr<ComputeShaderElement> findMaxDepth2D;
  eastl::unique_ptr<Sbuffer, DestroyDeleter<Sbuffer>> maxValueBuffer;
  RingCPUBufferLock resultRingBuffer;
  dag::VectorMap<int, int> lightLogCount;

  int lastNonOptId;
  bool processing;

  using RenderStaticCallback = dag::FunctionRef<void(mat44f_cref globTm, mat44f_cref projTm, const TMatrix &viewItm, int updateIndex,
    int frustumIndex, DynamicShadowRenderGPUObjects render_gpu_objects) const>;
  void dispatchQuery(RenderStaticCallback render_static);
  void completeQuery();

public:
  DistanceReadbackLights(ShadowSystem *shadowSystem, SpotLightsManager *spotLights, const LightsResourcesManager *lights_res_mgr);
  void update(RenderStaticCallback render_static);
  void afterResetDevice();
};
