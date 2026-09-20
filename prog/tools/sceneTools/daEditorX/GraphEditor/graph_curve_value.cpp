// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "graph_curve_value.h"
#include "graph_value_format.h"

#include <EASTL/sort.h>
#include <EASTL/utility.h>

#include <math/dag_mathBase.h>

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// The coefficient builders are ports of find_*_coefficients in curveEditor.js, down to its 1e-9
// guards: both editors write this field, so a value must read back the same whichever wrote it.
namespace
{
char curve_kind_letter(CurveKind kind)
{
  switch (kind)
  {
    case CurveKind::Steps: return 'S';
    case CurveKind::Linear: return 'L';
    case CurveKind::Monotonic: return 'M';
    case CurveKind::Polynom: return 'P';
  }
  return 'S';
}

eastl::vector<Point2> sorted_by_x(const eastl::vector<Point2> &points)
{
  eastl::vector<Point2> sorted = points;
  eastl::stable_sort(sorted.begin(), sorted.end(), [](const Point2 &l, const Point2 &r) { return l.x < r.x; });
  return sorted;
}

void build_steps(const eastl::vector<Point2> &points, eastl::vector<double> &out)
{
  const eastl::vector<Point2> p = sorted_by_x(points);
  for (const Point2 &pt : p)
  {
    out.push_back(pt.x);
    out.push_back(pt.y);
  }
}

void build_linear(const eastl::vector<Point2> &points, eastl::vector<double> &out)
{
  const eastl::vector<Point2> p = sorted_by_x(points);
  for (int i = 0; i + 1 < static_cast<int>(p.size()); ++i)
  {
    const double k = (p[i + 1].y - p[i].y) / (p[i + 1].x - p[i].x + 1e-9);
    out.push_back(p[i].x);
    out.push_back(p[i].y);
    out.push_back(k);
  }
}

void build_monotonic(const eastl::vector<Point2> &points, eastl::vector<double> &out)
{
  const eastl::vector<Point2> p = sorted_by_x(points);
  const int n = static_cast<int>(p.size());
  if (n < 2)
  {
    return;
  }

  eastl::vector<double> dxs, ms;
  for (int i = 0; i + 1 < n; ++i)
  {
    const double dx = p[i + 1].x - p[i].x;
    const double dy = p[i + 1].y - p[i].y;
    dxs.push_back(dx);
    ms.push_back(dy / (dx + 1e-9));
  }

  eastl::vector<double> c1s;
  c1s.push_back(ms[0]);
  for (int i = 0; i + 1 < static_cast<int>(dxs.size()); ++i)
  {
    const double m = ms[i];
    const double mNext = ms[i + 1];
    if (m * mNext <= 0.0)
    {
      c1s.push_back(0.0);
    }
    else
    {
      const double dx = dxs[i];
      const double dxNext = dxs[i + 1];
      const double common = dx + dxNext;
      c1s.push_back(3.0 * common / ((common + dxNext + 1e-9) / (m + 1e-9) + (common + dx) / (mNext + 1e-9)));
    }
  }
  c1s.push_back(ms.back());

  for (int i = 0; i + 1 < static_cast<int>(c1s.size()); ++i)
  {
    const double c1 = c1s[i];
    const double m = ms[i];
    const double invDx = 1.0 / (dxs[i] + 1e-9);
    const double common = c1 + c1s[i + 1] - m - m;
    out.push_back(p[i].x);
    out.push_back(p[i].y);
    out.push_back(c1);
    out.push_back((m - c1 - common) * invDx);
    out.push_back(common * invDx * invDx);
  }
}

// Exact fit through every point: the Vandermonde system solved with partial pivoting.
void build_polynom(const eastl::vector<Point2> &points, eastl::vector<double> &out)
{
  const int n = static_cast<int>(points.size());
  if (n < 1)
  {
    return;
  }

  const int cols = n + 1; // the augmented column holds y
  eastl::vector<double> m(static_cast<size_t>(n) * cols, 0.0);
  for (int i = 0; i < n; ++i)
  {
    double xp = 1.0;
    for (int j = 0; j < n; ++j)
    {
      m[i * cols + j] = xp;
      xp *= points[i].x;
    }
    m[i * cols + n] = points[i].y;
  }

  for (int i = 0; i < n; ++i)
  {
    int maxRow = i;
    double maxEl = fabs(m[i * cols + i]);
    for (int k = i + 1; k < n; ++k)
    {
      if (fabs(m[k * cols + i]) > maxEl)
      {
        maxEl = fabs(m[k * cols + i]);
        maxRow = k;
      }
    }
    for (int k = i; k < cols; ++k)
    {
      eastl::swap(m[maxRow * cols + k], m[i * cols + k]);
    }
    for (int k = i + 1; k < n; ++k)
    {
      const double c = fabs(m[i * cols + i]) > 1e-9 ? -m[k * cols + i] / m[i * cols + i] : 0.0;
      for (int j = i; j < cols; ++j)
      {
        m[k * cols + j] = (i == j) ? 0.0 : m[k * cols + j] + c * m[i * cols + j];
      }
    }
  }

  const size_t base = out.size();
  out.resize(base + n, 0.0);
  double *x = out.data() + base;
  for (int i = n - 1; i >= 0; --i)
  {
    x[i] = fabs(m[i * cols + i]) > 1e-9 ? m[i * cols + n] / m[i * cols + i] : 0.0;
    for (int k = i - 1; k >= 0; --k)
    {
      m[k * cols + n] -= m[k * cols + i] * x[i];
    }
  }
}
} // namespace

bool curve_kind_for_prop_type(const char *prop_type, CurveKind &out_kind)
{
  if (!prop_type)
  {
    return false;
  }
  if (!strcmp(prop_type, "steps_curve"))
  {
    out_kind = CurveKind::Steps;
    return true;
  }
  if (!strcmp(prop_type, "linear_curve"))
  {
    out_kind = CurveKind::Linear;
    return true;
  }
  if (!strcmp(prop_type, "monotonic_curve"))
  {
    out_kind = CurveKind::Monotonic;
    return true;
  }
  if (!strcmp(prop_type, "polynom_curve"))
  {
    out_kind = CurveKind::Polynom;
    return true;
  }
  return false;
}

CurveParse parse_curve_points(const char *stored, eastl::vector<Point2> &out_points)
{
  out_points.clear();
  if (!stored || !*stored)
  {
    return CurveParse::Empty;
  }

  const char *begin = strstr(stored, "/*");
  if (!begin)
  {
    return CurveParse::NoAnnotation;
  }
  begin += 2;
  const char *end = strstr(begin, "*/");
  if (!end)
  {
    end = begin + strlen(begin);
  }

  eastl::vector<float> nums;
  const char *p = begin;
  while (p < end)
  {
    // Skips separators and the kind letter, which is glued to the last number as "1L*/".
    while (p < end && !isdigit(static_cast<unsigned char>(*p)) && *p != '-' && *p != '+' && *p != '.')
    {
      ++p;
    }
    if (p >= end)
    {
      break;
    }
    char *next = nullptr;
    const double v = strtod(p, &next);
    if (next == p)
    {
      ++p;
      continue;
    }
    nums.push_back(static_cast<float>(v));
    p = next;
  }

  const int pointCount = static_cast<int>(nums.size()) / 2;
  for (int i = 0; i < pointCount; ++i)
  {
    out_points.push_back(Point2(clamp(nums[i * 2], 0.f, 1.f), clamp(nums[i * 2 + 1], 0.f, 1.f)));
  }
  if (out_points.size() < CURVE_MIN_POINTS)
  {
    out_points.clear();
    return CurveParse::NoAnnotation;
  }
  // Sorted here so the stored order, the drawn polyline and canonical_curve agree.
  out_points = sorted_by_x(out_points);
  return CurveParse::Ok;
}

eastl::string format_curve(CurveKind kind, const eastl::vector<Point2> &points)
{
  eastl::vector<double> coeffs;
  switch (kind)
  {
    case CurveKind::Steps: build_steps(points, coeffs); break;
    case CurveKind::Linear: build_linear(points, coeffs); break;
    case CurveKind::Monotonic: build_monotonic(points, coeffs); break;
    case CurveKind::Polynom: build_polynom(points, coeffs); break;
  }

  eastl::string out;
  for (int i = 0; i < static_cast<int>(coeffs.size()); ++i)
  {
    if (i > 0)
    {
      out += ", ";
    }
    if (kind == CurveKind::Polynom) // the one kind the JS editor writes unrounded
    {
      append_shortest(out, static_cast<float>(coeffs[i]));
    }
    else
    {
      append_rounded(out, coeffs[i], 5);
    }
  }

  out += " /*";
  for (int i = 0; i < static_cast<int>(points.size()); ++i)
  {
    if (i > 0)
    {
      out += ", ";
    }
    append_rounded(out, points[i].x, 5);
    out += ", ";
    append_rounded(out, points[i].y, 5);
  }
  out += curve_kind_letter(kind);
  out += "*/";
  return out;
}

eastl::string canonical_curve(const char *stored, CurveKind kind)
{
  eastl::vector<Point2> points;
  if (parse_curve_points(stored, points) != CurveParse::Ok)
  {
    return eastl::string();
  }
  return format_curve(kind, points);
}
