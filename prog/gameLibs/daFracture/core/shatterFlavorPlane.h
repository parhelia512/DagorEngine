// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include "shatterDriver.h"
#include "meshSliceImpl.h"


namespace frx
{

struct PlaneSliceFlavor
{
  struct PerPiece
  {};

  const PlaneShatterFlavorSettings &params;

  explicit PlaneSliceFlavor(const PlaneShatterFlavorSettings &p) : params(p) {}

  void begin(ShatterDriver<PlaneSliceFlavor> &, int) {}

  bool cut(ShatterDriver<PlaneSliceFlavor> &drv, const CutHint &hint, const ShatterPieceInfo &, const PerPiece &, DestrMesh &&src,
    int &)
  {
    G_ASSERT(!src.faces.empty());
    DestrMesh upMesh, downMesh;
    mesh_slice(drv.ctx, src,
      {.upMesh = &upMesh,
        .downMesh = &downMesh,
        .cutPlane = PlaneBasis(Plane3(hint.normal, hint.center)),
        .cutMatId = int16_t(drv.interiorMat)});
    drv.push(eastl::move(upMesh), {});
    drv.push(eastl::move(downMesh), {});
    return true;
  }
};

} // namespace frx
