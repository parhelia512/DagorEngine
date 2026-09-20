//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <util/dag_stdint.h>

enum class LightType
{
  Spot,
  Omni,
  Invalid
};

enum class LightBufferSlot : uint8_t
{
  Main = 0,
  Secondary = 1
};

static constexpr uint32_t LIGHTS_BUFFER_SLOTS = 2;
static_assert(uint32_t(LightBufferSlot::Secondary) + 1 == LIGHTS_BUFFER_SLOTS);
