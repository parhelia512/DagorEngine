// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <gameRes/dag_collResDecl.h>

class MeshData;
class TMatrix;

// occluders are traced in world space
void calculatePRT(MeshData &meshData, const TMatrix &toWorld, const CollisionResource &occluders, int rays_per_point,
  float points_per_sq_meter, int maxPointsPerFace, int prt1, int prt2, int prt3); // channels
