// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <cstdint>

enum class DebugLevel : uint8_t
{
  NONE,
  BASIC,
  FULL_DEBUG_INFO,
  AFTERMATH,
};

enum class DebugParts : uint8_t
{
  STRIP,
  KEEP,
  EMBED_SOURCE,
};
