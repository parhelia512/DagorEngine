// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <math/dag_Point2.h>

// Codec for the string a curve node property stores:
//   <coefficients joined ", "> " /*" <control points joined ", "> <TYPE letter> "*/"
// EditorCurve reads the coefficients and the kind letter; the editor owns the points and derives
// the coefficients from them, as the JS editor does.
enum class CurveKind
{
  Steps,     // S: 2 coefficients per point (x, y)
  Linear,    // L: 3 per segment (x, y, slope)
  Monotonic, // M: 5 per segment (x, y, c1, c2, c3), Fritsch-Carlson
  Polynom,   // P: one coefficient per point, exact fit of degree n-1
};

// The max bounds interactive adds only: a paste can exceed it, and nothing downstream caps curve
// points. The largest stored curve has 8; the JS editor refuses to delete below 2.
inline constexpr int CURVE_MIN_POINTS = 2;
inline constexpr int CURVE_MAX_POINTS = 32;

enum class CurveParse
{
  Ok,
  Empty,        // nothing stored yet: the caller falls back to the descriptor default
  NoAnnotation, // has coefficients but no point list -- refuse rather than overwrite it with a guess
};

bool curve_kind_for_prop_type(const char *prop_type, CurveKind &out_kind);

// Points come back sorted by x and clamped to the unit square, like the control keeps them.
CurveParse parse_curve_points(const char *stored, eastl::vector<Point2> &out_points);

eastl::string format_curve(CurveKind kind, const eastl::vector<Point2> &points);

// The stored value as this editor would write it back. Empty when it has no usable annotation.
eastl::string canonical_curve(const char *stored, CurveKind kind);
