// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <daSDF/generate_sdf.h>
#include <daBVH/dag_swBLAS_ray.h>
#include <math.h>

using SDFBlasWalk = BLASTraverse<false, 8>;

inline bool blas_closest_distance(const MeshBLAS &blas, vec3f pos, float max_dist, float &out_dist)
{
  DistData d;
  d.data = blas.data.data();
  d.invScale = blas.invScale;
  d.pos = v_add(pos, v_mul(blas.ofs, d.invScale));
  d.bestDist2 = max_dist * max_dist;
  d.bestTriOffset = -1;
  if (!SDFBlasWalk::distBLAS(d, 0, blas.blasSize))
    return false;
  out_dist = sqrtf(d.bestDist2);
  return true;
}

inline bool blas_ray_hit(const MeshBLAS &blas, vec3f from, vec3f dir, float max_t, bool &normal_opposes)
{
  RayData r;
  r.data = blas.data.data();
  r.rayOrigin = v_madd(from, blas.scale, blas.ofs);
  r.rayDir = v_mul(dir, blas.scale);
  r.t = max_t;
  r.calc();
  if (!rayBLAS_Free<false, 8>(r, 0, blas.blasSize))
    return false;

  const uint32_t skip = ((const uint32_t *)(r.data + r.bestTriOffset))[-1];
  SDFBlasWalk::QuadLeafVerts q;
  q.decodeTri(r.data, r.bestTriOffset, skip, r.bestSubTri, SDFBlasWalk::RDVertexLoader{});
  normal_opposes = v_test_vec_x_lt_0(v_dot3_x(r.rayDir, v_cross3(v_sub(q.v1, q.v0), v_sub(q.v2, q.v0))));
  return true;
}
