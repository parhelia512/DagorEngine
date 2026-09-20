// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <math/dag_Point3.h>
#include <math/dag_mathUtils.h>
#include <math/random/dag_random.h>
#include <dag/dag_vectorMap.h>
#include <memory/dag_framemem.h>
#include <debug/dag_assert.h>

#include <daFracture/core/destrMesh.h>
#include <daFracture/core/meshCommon.h>
#include <daFracture/core/meshSlicing.h>


namespace frx
{

struct ShatterPieceInfo
{
  Point3 worldNormal = Point3(0, 1, 0);        // long-axis direction in world -> basis for the preferred normal
  Point3 p1 = Point3::ZERO, p2 = Point3::ZERO; // long-axis endpoints, world
  Point3 centroid = Point3::ZERO;              // vertex centroid, world
  float pieceSize = 0.f;                       // long-axis diameter
  float pickWeight = 0.f;                      // selection weight
};

struct CutHint
{
  int pieceId = -1;
  Point3 center = Point3::ZERO;
  Point3 normal = Point3(0, 1, 0);
  float targetSize = 0.f;
};

template <class Flavor>
struct ShatterDriver
{
  DestrContext &ctx;
  DestrSystem &sys;
  uint16_t interiorMat;
  const ShatterImpactProfile &impact;
  const ShatterMaterialProfile &material;
  int &seed;
  Flavor &flavor;

  struct WorkItem
  {
    ShatterPieceInfo info;
    typename Flavor::PerPiece meta;
  };
  dag::VectorMap<int, WorkItem, eastl::less<int>, framemem_allocator> work;

  ShatterDriver(DestrContext &ctx_, DestrSystem &sys_, uint16_t interior_mat, const ShatterImpactProfile &impact_,
    const ShatterMaterialProfile &material_, int &seed_, Flavor &flavor_) :
    ctx(ctx_), sys(sys_), interiorMat(interior_mat), impact(impact_), material(material_), seed(seed_), flavor(flavor_)
  {}

  static ShatterPieceInfo measure(const DestrMesh &piece)
  {
    G_ASSERT(!piece.verts.empty());
    ShatterPieceInfo r;

    Point3 c = Point3::ZERO;
    for (const auto &v : piece.verts)
      c += v.pos;
    c *= safeinv(float(piece.verts.size()));
    r.centroid = piece.tm * c;

    Point3 p1 = c;
    float bestSq = 0.f;
    for (const auto &v : piece.verts)
      if (const float s = lengthSq(v.pos - c); s > bestSq)
      {
        bestSq = s;
        p1 = v.pos;
      }
    Point3 p2 = p1;
    bestSq = 0.f;
    for (const auto &v : piece.verts)
      if (const float s = lengthSq(v.pos - p1); s > bestSq)
      {
        bestSq = s;
        p2 = v.pos;
      }
    if (bestSq < 1e-8f)
      return r;

    const float diameter = sqrtf(bestSq);
    const Point3 localAxis = (p2 - p1) * (1.f / diameter);
    r.worldNormal = normalize(piece.tm % localAxis);
    r.pieceSize = diameter;
    r.pickWeight = diameter;
    r.p1 = piece.tm * p1;
    r.p2 = piece.tm * p2;
    return r;
  }

  float getPower(const Point3 &p) const
  {
    const float t = cvt(length(p - impact.pos), impact.radiusRange.x, impact.radiusRange.y, 0.f, 1.f);
    return cvt(powf(t, impact.falloffPow), 0.f, 1.f, impact.powerRange.x, impact.powerRange.y);
  }

  float pickTargetPieceSize(const Point3 &p, float upper_limit)
  {
    const float power = getPower(p);
    const auto eligible = [&](const ShatterMaterialProfile::SizeMode &m) {
      return power >= m.powerRange.x && power <= m.powerRange.y && m.sizeRange.x <= upper_limit;
    };
    float total = 0.f;
    for (const auto &m : material.sizeModes)
      if (eligible(m))
        total += m.weight * m.sizeRange.x; // small pieces are compensated to have smaller real weight
    if (total <= 0.f)
      return -1.f;
    float r = _frnd(seed) * total;
    for (const auto &m : material.sizeModes)
      if (eligible(m))
      {
        r -= m.weight * m.sizeRange.x;
        if (r <= 0.f)
          return lerp(m.sizeRange.x, min(m.sizeRange.y, upper_limit), _frnd(seed));
      }
    return -1.f;
  }

  Point3 pickCutNormal(const Point3 &ref)
  {
    if (material.cutPlaneModes.empty())
      return ref;
    float total = 0.f;
    for (const auto &m : material.cutPlaneModes)
      total += m.weight;
    float r = _frnd(seed) * max(total, 1e-6f);
    const ShatterMaterialProfile::CutPlaneMode *chosen = &material.cutPlaneModes[0];
    for (const auto &m : material.cutPlaneModes)
    {
      r -= m.weight;
      if (r <= 0.f)
      {
        chosen = &m;
        break;
      }
    }
    const float cosT = clamp(lerp(chosen->cosineRange.x, chosen->cosineRange.y, _frnd(seed)), -1.f, 1.f);
    const float sinT = sqrtf(max(0.f, 1.f - cosT * cosT));
    const Point3 t = normalize(cross(ref, fabsf(ref.x) < 0.9f ? Point3(1, 0, 0) : Point3(0, 1, 0)));
    const Point3 b = cross(ref, t);
    const float phi = _srnd(seed) * PI;
    const Point3 azim = cosf(phi) * t + sinf(phi) * b;
    return cosT * ref + sinT * azim;
  }

  int push(DestrMesh &&m, typename Flavor::PerPiece &&meta)
  {
    if (m.faces.empty())
      return -1;
    const int id = sys.pieces.back().first + 1; // strictly greater than any live id -> no collision
    mesh_normalize_transform(ctx, m);
    work.emplace(id, WorkItem{measure(m), eastl::move(meta)});
    sys.pieces[id] = eastl::move(m);
    return id;
  }

  int run(int start_piece_idx)
  {
    mesh_normalize_transform(ctx, sys.pieces[start_piece_idx]);
    work.reserve(material.maxPieces);
    work.emplace(start_piece_idx, WorkItem{measure(sys.pieces[start_piece_idx]), {}});
    flavor.begin(*this, start_piece_idx);

    sys.pieces.reserve(sys.pieces.size() + material.maxPieces);
    int cutsDone = 0;
    for (int iterGuard = 0; !work.empty() && int(sys.pieces.size()) < material.maxPieces && iterGuard < 4096; iterGuard++)
    {
      // weighted selection by pickWeight (iteration order is key order, same as the original)
      float total = 0.f;
      for (const auto &[idx, wi] : work)
        total += wi.info.pickWeight;
      float pick = _frnd(seed) * total;
      int selected = int(work.size()) - 1, scan = 0;
      for (const auto &[idx, wi] : work)
      {
        if ((pick -= wi.info.pickWeight) <= 0.f)
        {
          selected = scan;
          break;
        }
        ++scan;
      }
      // COPY out before the cut: push() below can reallocate `work` and invalidate the entry
      const int pieceIdx = work.data()[selected].first;
      const ShatterPieceInfo axisInfo = work.data()[selected].second.info;
      const typename Flavor::PerPiece meta = work.data()[selected].second.meta;

      const float sizeUpperConstraint = axisInfo.pieceSize * material.relativeSizeWindow.y;
      float theta1 = pickTargetPieceSize(lerp(axisInfo.p1, axisInfo.p2, 0.25f), sizeUpperConstraint);
      if (theta1 < 0.f)
        theta1 = axisInfo.pieceSize;
      float theta2 = pickTargetPieceSize(lerp(axisInfo.p1, axisInfo.p2, 0.75f), sizeUpperConstraint);
      if (theta2 < 0.f)
        theta2 = axisInfo.pieceSize;
      if (int(sys.pieces.size()) >= material.minPieces && axisInfo.pieceSize * material.relativeSizeWindow.x <= min(theta1, theta2))
      {
        work.erase(pieceIdx);
        continue;
      }

      const float cutT = lerp(material.minCutRatio, 1.f - material.minCutRatio, theta1 / (theta1 + theta2));
      Point3 cutCenter = lerp(axisInfo.p1, axisInfo.p2, cutT);
      if (cutsDone < material.impactPointCuts)
      {
        const Point3 axis = axisInfo.p2 - axisInfo.p1;
        const float axisLen2 = lengthSq(axis);
        const float tRaw = axisLen2 > 1e-8f ? (impact.pos - axisInfo.p1) * axis / axisLen2 : 0.5f;
        cutCenter = (tRaw >= material.minCutRatio && tRaw <= 1.f - material.minCutRatio)
                      ? impact.pos
                      : lerp(axisInfo.p1, axisInfo.p2, clamp(tRaw, material.minCutRatio, 1.f - material.minCutRatio));
      }
      const Point3 cutNormal = pickCutNormal(axisInfo.worldNormal);

      CutHint hint;
      hint.pieceId = pieceIdx;
      hint.center = cutCenter;
      hint.normal = cutNormal;
      hint.targetSize = min(theta1, theta2);

      [[maybe_unused]] const int piecesBefore = int(sys.pieces.size());
      DestrMesh src = eastl::move(sys.pieces[pieceIdx]);
      const bool cutMade = flavor.cut(*this, hint, axisInfo, meta, eastl::move(src), seed);
      work.erase(pieceIdx);
      if (cutMade)
      {
        sys.pieces.erase(pieceIdx);
        cutsDone++;
      }
      else
      {
        // a failed cut must not have added any piece nor touched the source
        G_ASSERT(int(sys.pieces.size()) == piecesBefore && !src.faces.empty());
        sys.pieces[pieceIdx] = eastl::move(src);
      }
    }
    return cutsDone;
  }
};

} // namespace frx
