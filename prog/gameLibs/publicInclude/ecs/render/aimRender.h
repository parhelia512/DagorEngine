//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <daECS/core/entityId.h>

class DynamicRenderableSceneInstance;
class CollisionResource;

namespace dafg
{
class NodeHandle;
}

struct AimRenderingData
{
  bool farDofEnabled = false;
  bool lensRenderEnabled = false;
  bool isAiming = false;
  ecs::EntityId entityWithScopeLensEid;
  int lensNodeId = -1;
  int lensCollisionNodeId = -1;
  float lensBoundingSphereRadius = 1.0f;
  float aimingTime = 0.0f;
};

AimRenderingData get_aim_rendering_data();
const DynamicRenderableSceneInstance *get_scope_lens(const AimRenderingData &aim_data);
float get_scope_lens_bs_radius(const CollisionResource *collres, const int node_id);
dafg::NodeHandle makeGenAimRenderingDataNode();
