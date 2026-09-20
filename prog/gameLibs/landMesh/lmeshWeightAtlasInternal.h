// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

// Shared by the atlas objects: the driver-free packing (lmeshWeightAtlas.cpp),
// the GPU residency (lmeshWeightAtlasGpu.cpp) and the legacy conversions.
// Nothing here may pull a shader or driver library symbol into a link;
// driver headers and inline virtual calls are fine, library calls are not.

#include <landMesh/lmeshWeightAtlas.h>
#include <drv/3d/dag_buffers.h>
#include <math/dag_mathBase.h>
#include <math/dag_mathUtils.h> // safeinv
#include <startup/dag_globalSettings.h>
#include <ioSys/dag_dataBlock.h>

// the explicit opt-in that routes desktops onto the GPU conversion (WTM ships
// it; the desktop check uses it)
inline bool land_weight_atlas_force_gpu()
{
  return dgs_get_settings()->getBlockByNameEx("graphics")->getBool("landWeightAtlasForceGpu", false);
}

// what the exported pair of weight textures holds: four channels in the first,
// two of the second's; any channel a cell blends past those is not stored
static constexpr int LEGACY_TEX1_CHANNELS = 4, LEGACY_TEX2_CHANNELS = 2;
G_STATIC_ASSERT(LEGACY_TEX1_CHANNELS + LEGACY_TEX2_CHANNELS < LandWeightAtlas::DET_NUM);
// bit position of each first-texture channel's nibble in the argb4 texel
inline constexpr int LEGACY_TEX1_NIBBLE[LEGACY_TEX1_CHANNELS] = {8, 4, 0, 12};
// a legacy cell record: the landclass ids, the ddsx bytes' length, the second texture's offset in them
static constexpr int LEGACY_CELL_HDR = LandWeightAtlas::DET_NUM + 8;

// the row length that packs n pages of page_w tightest under the texture caps
int land_weight_choose_pages_per_row(int n, int page_w, int max_w, int max_h);

// The derivation the legacy per-cell weight textures were decoded with, so the
// packed weights are complete and consumers never repeat this chain. Which
// channel the export leaves for us to derive is NOT the last one: the argb4/rg8
// pair holds the others, so it is the even channel below the count (0,0,2,2,4,4,6
// for one to seven textures). Everything above the count is unused.
inline void land_weight_derive(float w[LandWeightAtlas::DET_NUM], int num_tex)
{
  num_tex = clamp(num_tex, 1, (int)LandWeightAtlas::DET_NUM);
  const int derived = (num_tex - 1) & ~1;
  for (int i = num_tex; i < LandWeightAtlas::DET_NUM; i++)
    w[i] = 0;
  w[derived] = 0;
  float sum = 0;
  for (int i = 0; i < num_tex; i++)
    sum += w[i];
  w[derived] = saturate(1 - sum);
}

// A device reset recreates the buffer empty. The records are the one thing we
// always keep on the CPU (the pages are not), so this is free; the atlas
// texture comes back from its system copy or from a reload of the level.
// The atlas owns the callback: a driver that takes it only calls destroySelf()
// when a second one replaces it, so handing ownership over would leak it.
struct LandWeightAtlas::CellsReload final : public Sbuffer::IReloadData
{
  const LandWeightAtlas &atlas;
  CellsReload(const LandWeightAtlas &a) : atlas(a) {}
  void reloadD3dRes(Sbuffer *sb) override { sb->updateData(0, data_size(atlas.records), atlas.records.data(), VBLOCK_WRITEONLY); }
  void destroySelf() override {}
};
