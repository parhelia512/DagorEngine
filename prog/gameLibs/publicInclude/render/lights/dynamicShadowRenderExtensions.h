//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <EASTL/array.h>
#include <vecmath/dag_vecMathDecl.h>
#include <generic/dag_relocatableFixedVector.h>
#include <generic/dag_span.h>
#include <math/dag_Point3.h>
#include <render/dynamicShadowRender.h>

class LightsVisibilityChecker;

// TODO: should also be moved to the namespace.
enum class DynamicShadowRenderGPUObjects
{
  NO,
  YES
};

namespace dynamic_shadow_render
{

struct FrameUpdate
{
  struct View
  {
    mat44f invView;
    mat44f view;
  };

  eastl::array<View, 6> views;
  mat44f proj;
  int numViews;
  float maxDrawDistance;
  DynamicShadowRenderGPUObjects renderGPUObjects;
};

struct QualityParams
{
  int maxShadowsToUpdateOnFrame;
  int maxShadowViewsToUpdateOnFrame;
  float maxShadowDist;
};

struct FramePrepareShadowsParams
{
  const LightsVisibilityChecker *lightVisibilityChecker;
  const Point3 &viewPos;
  mat44f_cref globtm;
  float cameraFocal;
  dag::ConstSpan<bbox3f> dynamicBoxes;
  bool collectUpdates;
};

static constexpr int ESTIMATED_MAX_SHADOWS_TO_UPDATE_PER_FRAME = 5; // See dynamicShadowsMaxUpdatePerFrame.

template <typename T>
using FrameVector = dag::RelocatableFixedVector<T, ESTIMATED_MAX_SHADOWS_TO_UPDATE_PER_FRAME, true>;

using FrameUpdates = FrameVector<FrameUpdate>;

struct FrameVolumeData
{
  struct Volume
  {
    uint16_t id = 0;
    // -1 when no entry was collected:
    // the volume renders no such pass this frame, or framePrepareShadows was called with collect_updates false
    int16_t staticUpdateIndex = -1;
    int16_t dynamicUpdateIndex = -1;
  };

  FrameVector<Volume> volumes;
  FrameUpdates staticUpdates;
  FrameUpdates dynamicUpdates;

  void clear()
  {
    volumes.clear();
    staticUpdates.clear();
    dynamicUpdates.clear();
  }
};

} // namespace dynamic_shadow_render
