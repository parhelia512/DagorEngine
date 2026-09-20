//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <math/dag_Point3.h>
#include <vecmath/dag_vecMath.h> // v_test_triangle_triangle_intersection lives there now

// Scalar Moller formulation. Prefer v_test_triangle_triangle_intersection; this one is kept as
// the independent reference the collisionResource tests check that kernel against.
bool test_triangle_triangle_intersection_mueller(const Point3 &p1, const Point3 &q1, const Point3 &r1, const Point3 &p2,
  const Point3 &q2, const Point3 &r2);
