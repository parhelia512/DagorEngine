//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <stdint.h>

// These values are baked into level binaries. Don't delete or insert, only append.
enum class NavmeshExportType : uint32_t
{
  WATER = 0,
  SPLINES = 1,
  HEIGHT_FROM_ABOVE = 2,
  GEOMETRY = 3,
  WATER_AND_GEOMETRY = 4,
  INVALID = 5,
  COUNT = 6
};
