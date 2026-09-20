//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <daECS/core/event.h>

class DataBlock;


struct EventRenderSceneLoaded : public ecs::Event
{
  const DataBlock &level_blk;
  ECS_BROADCAST_EVENT_DECL(EventRenderSceneLoaded)
  EventRenderSceneLoaded(const DataBlock &lev_blk) : ECS_EVENT_CONSTRUCTOR(EventRenderSceneLoaded), level_blk(lev_blk) {}
};

ECS_BROADCAST_EVENT_TYPE(EventRenderSceneUnload)
ECS_BROADCAST_EVENT_TYPE(EventAfterDeviceReset, bool /*force_reset*/)
