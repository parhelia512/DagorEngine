//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <EASTL/initializer_list.h>
#include <math/dag_Point3.h>

class Point4;

inline constexpr Point3 VEC_UNIT_UP(0.f, 1.f, 0.f);
inline constexpr Point3 VEC_UNIT_FWD(1.f, 0.f, 0.f);

bool p3_nonzero(const Point3 &p);
float distance_to_triangle(const Point3 &p, const Point3 &a, const Point3 &b, const Point3 &c, Point3 &out_contact,
  Point3 &out_normal);
void get_random_point_on_sphere(float min_x, float max_x, int &rand_seed, Point3 &out_pnt);

float cvt_p4(float val, const Point4 &m);
