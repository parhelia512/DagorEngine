// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <gameRes/dag_collisionResource.h>
#include <math/dag_math3d.h>
#include <math/dag_mathUtils.h>
#include <vecmath/dag_vecMath.h>
#include <debug/dag_debug.h>
#include <float.h>

// Helpers shared by collisionGameRes.cpp and collisionResourceInstance.cpp - the instance file
// holds the CollisionResourceInstance method definitions, the resource file keeps the shared
// geometry, loading and trace dispatch.

// The temp-to-block landing of a staged array: mem_copy_from over the block's (rvalue) span,
// guarded for the empty stage whose source pointer may be null.
template <typename T>
static inline void collres_copy_into(dag::Span<T> dst, const T *src)
{
  if (!dst.empty())
    mem_copy_from(dst, src);
}

// The ONE name-packing rule of the node name pool (the builder's node appends, the legacy readers): an
// empty name reads as offset 0 and lands nothing; the first real name lands at offset 1, past byte 0 that
// the growth zero-fills as the empty name (the builder's vector value-initializes); a name lands at the
// pool end with its terminator (every reader empty-guards the pool, see getNodeNameStr).
struct CollresNamePlan
{
  uint32_t nameOfs = 0; // 0: the empty name, nothing lands
  uint32_t grownPoolSize = 0;
};
static inline CollresNamePlan collres_plan_name(uint32_t pool_size, const char *name)
{
  if (!name || !*name)
    return {0u, pool_size};
  const uint32_t ofs = pool_size == 0 ? 1u : pool_size;
  return {ofs, ofs + (uint32_t)strlen(name) + 1u};
}
static inline void collres_land_name(char *pool, const CollresNamePlan &plan, const char *name)
{
  if (plan.nameOfs == 0)
    return;
  memcpy(pool + plan.nameOfs, name, plan.grownPoolSize - plan.nameOfs); // the name and its terminator, as planned
}
// A builder's id is always one INLINE material: a value below PHYSMAT_INVALID would read back as a pool reference (see
// setNodePhysMats), so refuse it here the same way and store "no material".
static inline int16_t sanitize_builder_phys_mat(int16_t id, const char *name)
{
  if (DAGOR_LIKELY(id >= PHYSMAT_INVALID))
    return id;
  logerr("collision node '%s': physmat id %d is not storable inline (needs PHYSMAT_INVALID or a non-negative id)", name, (int)id);
  return PHYSMAT_INVALID;
}

// The SoA4 LeafRef encodes parent node offsets in 25 bits (32 MB) -- tighter than the 24-bit
// leaf-base gates, so a stackless tree in the 32..64 MB gap would pass those yet fail the SoA4
// conversion. The gate reads stackless treeBytes; the flags words can grow the converted tree a
// few bytes per node past it, and the converter's emit and base guards refuse an overshoot (the
// builder then drops the node).
static inline bool soa4_tree_fits_leaf_refs(int tree_bytes)
{
  return (int64_t)tree_bytes + (int64_t)soa4::LEAF_BYTES <= (int64_t)soa4::LEAF_ENTRY_OFS_MASK;
}

// Signed-permutation bases with one exact scale per axis: the only bases whose column max IS
// the spectral norm bit-exactly, so no conservative restamp is due.
static inline bool is_exact_axis_aligned_basis(const TMatrix &tm)
{
  for (int a = 0; a < 3; a++)
  {
    const Point3 c = tm.getcol(a);
    if (((c.x != 0.f) + (c.y != 0.f) + (c.z != 0.f)) != 1)
      return false;
  }
  return true;
}

// Largest singular value, used where column lengths under-bound shear. Rounded up a few ULPs
// only: an upper bound where the closed form is exact (uniform and axis-aligned bases), about
// 1e-4 under near coincident singular values, which the shear-class stamps cover with their own
// margin. Defined in collisionGameRes.cpp: its degenerate-scale diagnostic must latch once per
// process, not once per translation unit.
float mat33_spectral_norm(mat44f_cref m_in);

// Affine inverse with the determinant taken in double. The float determinants cannot represent
// what their own matrices can: the legacy inverse() asserts below 1e-12 and v_mat33_inverse
// substitutes an all-zero inverse below its reciprocal floor, while the determinant of any finite
// float basis fits in double with room to spare. So this inverts whenever the result is
// representable, and overflows to inf when it is not, which is what the finiteness probes expect.
static inline TMatrix collres_inverse(const TMatrix &a)
{
  double m[3][3];
  for (int c = 0; c < 3; c++)
    for (int r = 0; r < 3; r++)
      m[c][r] = a.m[c][r];
  const double adj[3][3] = {
    {m[1][1] * m[2][2] - m[2][1] * m[1][2], m[2][1] * m[0][2] - m[0][1] * m[2][2], m[0][1] * m[1][2] - m[1][1] * m[0][2]},
    {m[2][0] * m[1][2] - m[1][0] * m[2][2], m[0][0] * m[2][2] - m[2][0] * m[0][2], m[1][0] * m[0][2] - m[0][0] * m[1][2]},
    {m[1][0] * m[2][1] - m[2][0] * m[1][1], m[2][0] * m[0][1] - m[0][0] * m[2][1], m[0][0] * m[1][1] - m[1][0] * m[0][1]}};
  const double invD = 1.0 / (m[0][0] * adj[0][0] + m[1][0] * adj[0][1] + m[2][0] * adj[0][2]);
  TMatrix r;
  for (int c = 0; c < 3; c++)
    for (int w = 0; w < 3; w++)
      r.m[c][w] = float(adj[c][w] * invD);
  for (int w = 0; w < 3; w++)
    r.m[3][w] = float(-(adj[0][w] * a.m[3][0] + adj[1][w] * a.m[3][1] + adj[2][w] * a.m[3][2]) * invD);
  return r;
}

// Shared ownership/freshness gate for updates and dispatch. Defined in collisionGameRes.cpp
// for the same single-latch reason.
bool check_instance_owned_and_fresh(const CollisionResource *res, const CollisionResourceInstance &instance, const char *site);

// Conditioning of one geometry matrix for the TLAS refit budget, spectral-norm-bounded
// (column lengths under-read shear).
static inline float collres_tlas_tm_conditioning(mat44f_cref gt)
{
  const float det = fabsf(v_extract_x(v_dot3_x(gt.col0, v_cross3(gt.col1, gt.col2))));
  return collres_tlas_conditioning(mat33_spectral_norm(gt), det);
}

// Max basis deviation from orthonormality, |c_i . c_j - delta_ij| over the six pairs: length
// AND pairwise orthogonality (a unit-length shear passes any length-only test).
static inline float collres_mat33_ortho_dev(mat44f_cref m)
{
  const float e00 = fabsf(v_extract_x(v_dot3_x(m.col0, m.col0)) - 1.f);
  const float e11 = fabsf(v_extract_x(v_dot3_x(m.col1, m.col1)) - 1.f);
  const float e22 = fabsf(v_extract_x(v_dot3_x(m.col2, m.col2)) - 1.f);
  const float e01 = fabsf(v_extract_x(v_dot3_x(m.col0, m.col1)));
  const float e02 = fabsf(v_extract_x(v_dot3_x(m.col0, m.col2)));
  const float e12 = fabsf(v_extract_x(v_dot3_x(m.col1, m.col2)));
  return max(max(max(e00, e11), max(e22, e01)), max(e02, e12));
}

// Bit-exact signed-permutation bases (identity, axis-aligned rotations and reflections) are the
// only rigid bases floats represent exactly; every other member of an epsilon class owes the
// conservative pad. Within a near-orthogonal class three unit-axis columns are necessarily
// distinct axes, so no orthogonality check is needed here.
static inline bool is_exact_unit_axis_col(const Point3 &c)
{
  const float ax = fabsf(c.x), ay = fabsf(c.y), az = fabsf(c.z);
  return (ax == 1.f && ay == 0.f && az == 0.f) || (ax == 0.f && ay == 1.f && az == 0.f) || (ax == 0.f && ay == 0.f && az == 1.f);
}

static inline bool is_exact_rigid_basis(const TMatrix &tm)
{
  return is_exact_unit_axis_col(tm.getcol(0)) && is_exact_unit_axis_col(tm.getcol(1)) && is_exact_unit_axis_col(tm.getcol(2));
}

// Row norm without squaring raw elements: a finite 2e20 component would overflow into an
// infinite extent. Outside the normalizable band the max element times sqrt(3) is a
// conservative bound (exact 0 for a zero row; NaN propagates).
static inline float conservative_row_norm(float x, float y, float z)
{
  const float m = max(fabsf(x), max(fabsf(y), fabsf(z)));
  if (DAGOR_UNLIKELY(!(m >= FLT_MIN && m <= 1.f / FLT_MIN)))
    return m == 0.f ? 0.f : m * 1.7320509f;
  const float ix = x / m, iy = y / m, iz = z / m;
  return m * sqrtf(ix * ix + iy * iy + iz * iz);
}


static inline bool is_exact_rigid_basis_v(mat44f_cref m)
{
  TMatrix t;
  v_mat_43cu_from_mat44(t.array, m);
  return is_exact_rigid_basis(t);
}

// Analytic AABB avoids the rotation inflation of corner-mapping a sphere.
// TMatrix is column-major (m[i] IS column i), so conservative_row_norm(tm[0][k], tm[1][k],
// tm[2][k]) is ROW k's norm -- r * |row_k| is the tight Cauchy-Schwarz extent along axis k.
static inline BBox3 composed_sphere_box(const TMatrix &tm, const Point3 &c, float r)
{
  const Point3 center = tm * c;
  const Point3 ext(r * conservative_row_norm(tm[0][0], tm[1][0], tm[2][0]), r * conservative_row_norm(tm[0][1], tm[1][1], tm[2][1]),
    r * conservative_row_norm(tm[0][2], tm[1][2], tm[2][2]));
  return BBox3(center - ext, center + ext);
}


// NaN (a poisoned compose) never joins; a merely overflowed (Inf) lane saturates to the float
// range so a node whose reachable part is finite still bounds instead of leaving the root
// boxes short while the node stays traceable.
static inline void join_saturated_finite(bbox3f &dst, bbox3f b)
{
  if (!v_check_xyz_all_true(v_and(v_cmp_eq(b.bmin, b.bmin), v_cmp_eq(b.bmax, b.bmax))))
    return;
  const vec4f lim = v_splats(FLT_MAX);
  b.bmin = v_min(v_max(b.bmin, v_neg(lim)), lim);
  b.bmax = v_min(v_max(b.bmax, v_neg(lim)), lim);
  v_bbox3_add_box(dst, b);
}
