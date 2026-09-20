// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <vecmath/dag_vecMath.h>
#include <util/dag_bitwise_cast.h>
#include <math/dag_mathBase.h>

#include "bvh_context.h"

namespace bvh::voxel_activity
{

// debug toggles from the BVH imgui window
extern bool freeze;
extern bool cull;

// Reset the category's placement statistics before spawning its jobs.
void begin_ri_gen_placement(ContextId context_id);
void begin_ri_extra_placement(ContextId context_id);
void begin_dynrend_placement(ContextId context_id);

// Probe before building the culling sphere, so an inactive context pays nothing on the hot path.
__forceinline bool culling_active(ContextId context_id)
{
  const auto &cpu = context_id->voxelActivity.cpu;
  return cpu.valid && cpu.cull;
}

// One call per source instance with its world bounding sphere: one live voxel anywhere in the
// bounds keeps the instance. When all are dead, a per frame random fraction survives as revive
// probes; revive_probe=false (dynrend) culls outright to keep its skinning and BLAS VRAM,
// accepting that a lone stationary instance cannot revive until it moves.
// Bounds reaching outside the volume touch untracked space and always pass. The counters are
// job local; the caller adds them to the context totals once per job.
__forceinline bool VECTORCALL keep_instance(ContextId context_id, vec4f world_bsph, uint32_t &considered, uint32_t &culled,
  bool revive_probe = true)
{
  const auto &cpu = context_id->voxelActivity.cpu;
  if (!cpu.valid || !cpu.cull)
    return true;

  ++considered;

  const vec4f rad = v_splat_w(world_bsph);
  const vec4i lo = v_subi(v_cvt_floori(v_mul(v_sub(world_bsph, rad), cpu.invVoxelSize)), cpu.originVoxel);
  const vec4i hi = v_subi(v_cvt_floori(v_mul(v_add(world_bsph, rad), cpu.invVoxelSize)), cpu.originVoxel);

  // Both early outs in one mask, the w lanes neutral: lo.w is 0, dimsMinus1.w is INT_MAX and
  // the span product is masked to lane x. A span over megaStructureSize voxels is a
  // megastructure that is almost surely alive somewhere: keep it instead of scanning it.
  constexpr int megaStructureSize = 128;
  const vec4i span = v_addi(v_subi(hi, lo), V_CI_1);
  const vec4i volume = v_muli(v_muli(span, v_roti_1(span)), v_roti_2(span));
  vec4i pass = v_ori(v_cmp_gti(v_zeroi(), lo), v_cmp_gti(hi, cpu.dimsMinus1));
  pass = v_ori(pass, v_andi(v_cmp_gti(volume, v_splatsi(megaStructureSize)), V_CI_MASK1000));
  if (v_check_xyzw_any_true(v_cast_vec4f(pass)))
    return true;

  alignas(16) int lo3[4], hi3[4];
  v_sti(lo3, lo);
  v_sti(hi3, hi);
  const int x0 = lo3[0], y0 = lo3[1], z0 = lo3[2];
  const int x1 = hi3[0], y1 = hi3[1], z1 = hi3[2];

  const uint8_t *ages = cpu.ages.data();
  for (int z = z0; z <= z1; ++z)
    for (int y = y0; y <= y1; ++y)
    {
      const uint8_t *row = ages + x0 + cpu.rowStride * y + cpu.sliceStride * z;
      for (int x = x0; x <= x1; ++x)
        if (*row++ > 0)
          return true;
    }

  if (revive_probe)
  {
    // Randomly keep some instances in anyways
    const vec4i posBits = v_cast_vec4i(world_bsph);
    const uint32_t h = (uint32_t(v_extract_xi(posBits) ^ v_extract_yi(posBits) ^ v_extract_zi(posBits)) ^ cpu.frameSalt) * 0x9E3779B1u;
    if ((h >> 16) < cpu.keepThreshold)
      return true;
  }

  ++culled;
  return false;
}

} // namespace bvh::voxel_activity
