// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <vecmath/dag_vecMath.h>

#include "shatterDriver.h"
#include "meshSliceImpl.h"


namespace frx
{

struct RoughShatterHmap
{
  const RoughShatterFlavorSettings &par;
  float maxHeight = 0.5f;
  float cellSize = 0.15f;           // L: grid pitch in plane UV
  Point2 gridOrigin = Point2::ZERO; // lattice anchor in plane UV, read by both the cutter and the fill
  uint32_t seed = 0;                // per-cut decorrelation, mixed into the hashes
  bool creaseActive = false;        // set when this cut branches off an inherited concave crease
  float creaseU = 0.f;              // u of that crease (a constant-u line); h blends to neutral 0.5 near it

  explicit RoughShatterHmap(const RoughShatterFlavorSettings &p) : par(p) {}

  static __forceinline uint32_t mix(uint32_t h)
  {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
  }
  __forceinline float hash1(int x) const
  {
    return float(mix(uint32_t(x) * 0x9e3779b1u + seed) & 0xffffffu) * (1.f / float(0x1000000));
  }
  __forceinline float hash2(int x, int y) const
  {
    return float(mix((uint32_t(x) * 0x8da6b343u) ^ (uint32_t(y) * 0xd8163841u) ^ seed) & 0xffffffu) * (1.f / float(0x1000000));
  }
  __forceinline float valueNoise(float x, float y) const
  {
    const int ix = int(floorf(x)), iy = int(floorf(y));
    const float fx = x - float(ix), fy = y - float(iy);
    const float a = lerp(hash2(ix, iy), hash2(ix + 1, iy), fx);
    const float b = lerp(hash2(ix, iy + 1), hash2(ix + 1, iy + 1), fx);
    return lerp(a, b, fy);
  }
  __forceinline float detailNoise(float x, float y) const
  {
    return valueNoise(x, y) * 0.67f + valueNoise(x * 2.f + 19.7f, y * 2.f + 4.3f) * 0.33f;
  }
  __forceinline float ridgeKnot(int i) const
  {
    const float amt = hash1(i) * par.ridgeJitter * (par.ridgeRange.y - par.ridgeRange.x);
    return (i & 1) ? (par.ridgeRange.y - amt) : (par.ridgeRange.x + amt);
  }
  __forceinline float sample(Point2 uv) const
  {
    const float u = uv.x - gridOrigin.x, v = uv.y - gridOrigin.y;
    // break-line ridge: triangle-wave profile along u, kinks on grid lines (lattice pitch = ridgeCells * cell)
    const float rt = u / (cellSize * float(par.ridgeCells));
    const int ri = int(floorf(rt));
    const float rf = rt - float(ri);
    const float ridge = lerp(ridgeKnot(ri), ridgeKnot(ri + 1), rf);
    // secondary detail, faded to 0 near each kink so the creases stay clean
    const float inv = 1.f / (cellSize * par.detailCells);
    const float detail = detailNoise(u * inv, v * inv) * 2.f - 1.f;
    const float fade = clamp(min(rf, 1.f - rf) * float(par.ridgeCells) / par.fadeCells, 0.f, 1.f);
    float hNorm = clamp(ridge + detail * par.detailFrac * fade, 0.f, 1.f);
    if (creaseActive) // flatten to neutral (0.5) near the inherited crease so the branch meets it flush
    {
      const float w = clamp(fabsf(u - creaseU) / (cellSize * par.creaseBlendCells), 0.f, 1.f);
      hNorm = lerp(0.5f, hNorm, w);
    }
    return hNorm * maxHeight;
  }
};


struct RoughShatterFlavor
{
  const RoughShatterFlavorSettings &params;

  struct Crease
  {
    Point3 p, dir, into;
  }; // world-space
  struct PerPiece
  {
    int n = 0;
    Crease creases[4];
  };

  explicit RoughShatterFlavor(const RoughShatterFlavorSettings &p) : params(p) {}

  void begin(ShatterDriver<RoughShatterFlavor> &, int) {}

  bool cut(ShatterDriver<RoughShatterFlavor> &drv, const CutHint &hint, const ShatterPieceInfo &info, const PerPiece &in,
    DestrMesh &&src, int &seed)
  {
    G_ASSERT(!src.faces.empty());

    bool useCrease = false;
    Point3 normal, center, creaseDirW, creasePtW;
    if (in.n > 0 && _frnd(seed) < params.creaseFollowProb)
    {
      const float maxTilt = params.creaseMaxTiltDeg * DEG_TO_RAD;
      float bestDist = 1e30f;
      for (int ci = 0; ci < in.n; ci++)
      {
        const Crease &c = in.creases[ci];
        const Point3 d = hint.center - c.p;
        const Point3 side = normalize(cross(c.dir, c.into));
        float a = clamp(atan2f(dot(side, d), dot(c.into, d)), -maxTilt, maxTilt);
        a = clamp(a + _srnd(seed) * (maxTilt * params.creaseTiltRnd), -maxTilt, maxTilt);
        const Point3 w = c.into * cosf(a) + side * sinf(a);
        const Point3 n = normalize(cross(c.dir, w));
        const float dist = fabsf(dot(n, d));
        if (dist < bestDist)
        {
          bestDist = dist;
          normal = n;
          center = c.p;
          creaseDirW = c.dir;
          creasePtW = c.p;
        }
      }
      useCrease = bestDist <= info.pieceSize * params.creaseMaxOffcenter;
    }
    if (!useCrease)
    {
      normal = hint.normal;
      center = hint.center;
    }

    RoughShatterHmap hmap(params);
    hmap.cellSize = clamp(info.pieceSize * params.relCell, params.minCell, params.maxCell);
    hmap.maxHeight = hmap.cellSize * params.relAmpl;
    hmap.gridOrigin = Point2::ZERO;
    hmap.seed = uint32_t(_rnd(seed));
    center -= normal * (hmap.maxHeight * 0.5f);

    const Plane3 worldPlane(normal, center);
    const plane3f localPlaneV = transform_plane_to_local(v_ldu(&worldPlane.n.x), src.tm);
    PlaneBasis basis(localPlaneV);
    if (useCrease)
    {
      const Point3 lcd = normalize(inverse(src.tm) % creaseDirW);
      const Point3 uu = cross(basis.plane.n, lcd);
      basis.U = lengthSq(uu) > 1e-6f ? normalize(uu) : basis.U;
      basis.V = cross(basis.U, basis.plane.n);
      hmap.creaseActive = true;
      hmap.creaseU = dot(basis.U, inverse(src.tm) * creasePtW);
    }
    else
    {
      float mA = 0.f, mB = 0.f;
      for (const auto &vtx : src.verts)
      {
        mA += dot(basis.U, vtx.pos);
        mB += dot(basis.V, vtx.pos);
      }
      const float invN = safeinv(float(src.verts.size()));
      mA *= invN;
      mB *= invN;
      float sAA = 0.f, sAB = 0.f, sBB = 0.f;
      for (const auto &vtx : src.verts)
      {
        const float a = dot(basis.U, vtx.pos) - mA, b = dot(basis.V, vtx.pos) - mB;
        sAA += a * a;
        sAB += a * b;
        sBB += b * b;
      }
      const float theta = 0.5f * atan2f(2.f * sAB, sAA - sBB);
      basis.U = normalize(basis.U * cosf(theta) + basis.V * sinf(theta));
      basis.V = cross(basis.U, basis.plane.n);
    }

    PlaneBasis worldBasis;
    worldBasis.plane = worldPlane;
    worldBasis.U = normalize(src.tm % basis.U);
    worldBasis.V = normalize(src.tm % basis.V);

    DestrMesh up, down;
    mesh_slice_impl(drv.ctx, src, {.upMesh = &up, .downMesh = &down, .cutPlane = worldBasis, .cutMatId = int16_t(drv.interiorMat)},
      hmap);
    if (up.faces.empty() && down.faces.empty())
      return false;

    PerPiece upMeta, dnMeta;
    {
      Point3 cenLocal = Point3::ZERO;
      float uMin = 1e30f, uMax = -1e30f;
      for (const auto &vtx : src.verts)
      {
        cenLocal += vtx.pos;
        const float uu = dot(basis.U, vtx.pos);
        uMin = min(uMin, uu);
        uMax = max(uMax, uu);
      }
      cenLocal *= safeinv(float(src.verts.size()));
      const float vC = dot(basis.V, cenLocal);
      const Point3 dirW = normalize(src.tm % basis.V);
      const Point3 nW = normalize(src.tm % basis.plane.n);
      const float pitch = hmap.cellSize * float(params.ridgeCells);
      for (int i = int(ceilf(uMin / pitch)); i <= int(floorf(uMax / pitch)); i++)
      {
        if (hmap.creaseActive && fabsf(i * pitch - hmap.creaseU) < params.creaseBlendCells * hmap.cellSize)
          continue;
        const Point3 pW = src.tm * basis.unProject(Point2(i * pitch, vC), hmap.ridgeKnot(i) * hmap.maxHeight);
        if (i & 1)
        {
          if (upMeta.n < 4)
            upMeta.creases[upMeta.n++] = {pW, dirW, nW};
        }
        else
        {
          if (dnMeta.n < 4)
            dnMeta.creases[dnMeta.n++] = {pW, dirW, -nW};
        }
      }
    }

    drv.push(eastl::move(up), eastl::move(upMeta));
    drv.push(eastl::move(down), eastl::move(dnMeta));
    return true;
  }
};

} // namespace frx
