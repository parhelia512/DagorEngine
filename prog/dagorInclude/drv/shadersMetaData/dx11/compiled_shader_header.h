//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <util/dag_baseDef.h>
#include <util/dag_stdint.h>

namespace dx11
{

constexpr uint32_t SIMPLE_METADATA_SIZE = 12;

constexpr uint32_t COMBINED_SHADERS_IDENT = _MAKE4C('DX11');
constexpr uint32_t COMBINED_HEADER_OFFSET = SIMPLE_METADATA_SIZE;
constexpr uint32_t COMBINED_METADATA_SIZE = 32;

constexpr uint32_t HS_TOPOLOGY_SHIFT = 24;
constexpr uint32_t COMBINED_LEN_MASK = (1u << HS_TOPOLOGY_SHIFT) - 1;

struct SimpleHeader
{
  uint32_t bytecodeByteSize;
  int32_t maxConstantRegUsed; // -1 means none
  int32_t maxRtvUsed;         // PS only, -1 means none
};

static_assert(sizeof(SimpleHeader) == SIMPLE_METADATA_SIZE);

struct CombinedHeader
{
  uint32_t magic;
  uint32_t vsLenDwords;
  uint32_t hsLenDwordsAndTopology;
  uint32_t dsLenDwords;
  uint32_t gsLenDwords;
};

static_assert(sizeof(SimpleHeader) + sizeof(CombinedHeader) == COMBINED_METADATA_SIZE);

} // namespace dx11
