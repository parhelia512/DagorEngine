// Copyright (C) Gaijin Games KFT.  All rights reserved.

// Non-destructive spline corner fillet/chamfer.
//
// A point with props.filletR > 0 is a fillet source: the curve does not pass through it.
// Two generated points (V0 before, V1 after) cut the base curve and the piece between them is replaced by a blend,
// while the source point's knot moves onto the blend middle (getKnotPos) to keep the point<->segment correspondence roads,
// lofts and object generation rely on. Outside the cut the base curve shape is kept exactly (de Casteljau subdivision).
//
// Generated points are derived state: recomputed here on every curve rebuild,
// never saved and never registered in ObjectEditor (thus not selectable).

#include "hmlSplineObject.h"
#include "hmlSplinePoint.h"

#include <EditorCore/ec_ObjectEditor.h>
#include <de3_interface.h>
#include <math/dag_mathBase.h>

static inline Point3 lerp_p3(const Point3 &a, const Point3 &b, float t) { return a + (b - a) * t; }

static inline bool same_p3(const Point3 &a, const Point3 &b) { return lengthSq(a - b) < 1e-10f; }

// control points of sub-curve [t0..t1] of cubic (V[0]..V[3]), out[0..3] absolute
static void bezier_subcurve(const Point3 V[4], float t0, float t1, Point3 out[4])
{
  Point3 L[4];
  {
    Point3 a = lerp_p3(V[0], V[1], t1), b = lerp_p3(V[1], V[2], t1), c = lerp_p3(V[2], V[3], t1);
    Point3 d = lerp_p3(a, b, t1), e = lerp_p3(b, c, t1);
    L[0] = V[0];
    L[1] = a;
    L[2] = d;
    L[3] = lerp_p3(d, e, t1);
  }
  float t = t1 > 1e-6f ? t0 / t1 : 0.0f;
  {
    Point3 a = lerp_p3(L[0], L[1], t), b = lerp_p3(L[1], L[2], t), c = lerp_p3(L[2], L[3], t);
    Point3 d = lerp_p3(a, b, t), e = lerp_p3(b, c, t);
    out[0] = lerp_p3(d, e, t);
    out[1] = e;
    out[2] = c;
    out[3] = L[3];
  }
}

static splineclass::Attr lerp_attr(const splineclass::Attr &a, const splineclass::Attr &b, float t, const splineclass::Attr &ovr)
{
  splineclass::Attr r = ovr;
  r.scale_h = a.scale_h + (b.scale_h - a.scale_h) * t;
  r.scale_w = a.scale_w + (b.scale_w - a.scale_w) * t;
  r.opacity = a.opacity + (b.opacity - a.opacity) * t;
  r.tc3u = a.tc3u + (b.tc3u - a.tc3u) * t;
  r.tc3v = a.tc3v + (b.tc3v - a.tc3v) * t;
  return r;
}

namespace
{
struct FilletCtx
{
  int inSeg = -1, outSeg = -1;
  bool wants = false;      // filletR set and the point can carry a corner
  bool active = false;     // and the room left it something to cut
  float s = 0;             // cut distance actually used, after the room limit
  float tIn = 0, tOut = 0; // cut params on inSeg/outSeg
  Point3 v0pos, v1pos;
  Point3 v0relIn, v0relOut, v0adj;            // v0adj: preserved out-handle of prev knot (abs)
  Point3 v1relIn, v1relOut, v1adj;            // v1adj: preserved in-handle of next knot (abs)
  Point3 knotPt, knotIn, knotOut;             // source knot on blend middle (abs handles)
  splineclass::Attr attrV0 = {}, attrV1 = {}; // interpolated point attributes at the cuts
};
} // namespace

// source points at both ends of base segment k, in the knot layout the base curve is built with
static void seg_ends(int k, int n, bool poly, bool closed, int &left, int &right)
{
  left = k;
  if (poly)
    right = (k + 1) % n;
  else
    right = (k + 1 == n - 1 && closed) ? 0 : k + 1;
}

void SplineObject::gatherSourcePoints(PtrTab<SplinePointObject> &out_pts) const
{
  int pnum = isClosed() ? points.size() - 1 : points.size();
  out_pts.reserve(pnum);
  for (int i = 0; i < pnum; i++)
    if (!points[i]->isFilletGen)
      out_pts.push_back(points[i]);
}

SplinePointObject::Fillet &SplinePointObject::ensureFillet()
{
  if (!fillet)
    fillet.reset(new Fillet);
  return *fillet;
}

void SplineObject::beforeStructuralEdit(SplineObject *s1, SplineObject *s2)
{
  if (s1)
    s1->removeFilletPoints();
  if (s2)
    s2->removeFilletPoints();
}

SplinePointObject *SplineObject::nextRealPoint(int from, int dir) const
{
  for (int i = from + dir; i >= 0 && i < points.size(); i += dir)
    if (!points[i]->isFilletGen)
      return points[i];
  return nullptr;
}

int SplineObject::sourcePointOrdinal(const SplinePointObject *pt) const
{
  int n = 0;
  // exclude last point on closed spline from counting since points.front()==points.back() there
  for (int i = 0, pnum = isClosed() ? points.size() - 1 : points.size(); i < pnum; i++)
  {
    if (points[i] == pt)
      return n;
    if (!points[i]->isFilletGen)
      n++;
  }
  return -1; // not in the array, nothing to count from
}

int SplineObject::sourcePointCount() const
{
  int n = 0;
  for (int i = 0, pnum = isClosed() ? points.size() - 1 : points.size(); i < pnum; i++)
    if (!points[i]->isFilletGen)
      n++;
  return n;
}

int SplineObject::sourcePointIndex(int ordinal) const
{
  if (ordinal < 0)
    return points.size();
  int n = 0;
  // the ordinal past the last user point resolves onto the duplicate's slot, where a re-added trailing point goes
  for (int i = 0; i < points.size(); i++)
  {
    if (points[i]->isFilletGen)
      continue;
    if (n == ordinal)
      return i;
    n++;
  }
  return points.size();
}

bool SplineObject::hasFilletPoints() const
{
  for (int i = 0; i < points.size(); i++)
    if (points[i]->isFilletGen)
      return true;
  return false;
}

void SplineObject::removeFilletPoints()
{
  // strong refs: a point destructor walks points[] again (clearSegment -> updateRoadBox),
  // so it must not run while the array is half erased
  PtrTab<SplinePointObject> gone(tmpmem);
  bool removed = false;
  for (int i = points.size() - 1; i >= 0; i--)
    if (points[i]->isFilletGen)
    {
      points[i]->clearSegment();
      gone.push_back(points[i]);
      erase_items(points, i, 1);
      removed = true;
    }
  if (!removed)
    return;
  for (int i = points.size() - 1; i >= 0; i--)
    points[i]->arrId = i;
  for (int i = 0; i < points.size(); i++)
    points[i]->filletApplied = false;
}

void SplineObject::updateFilletPoints()
{
  if (updatingFillets)
    return;

  bool anyGen = false, anyFillet = false;
  for (int i = 0; i < points.size(); i++)
  {
    anyGen |= (bool)points[i]->isFilletGen;
    anyFillet |= !points[i]->isFilletGen && points[i]->getProps().filletR > 0;
  }
  if (!anyGen && !anyFillet)
    return;

  updatingFillets = true;
  bool structureChanged = false;

  // strip current generated points. This runs on every rebuild, so matching ones are reused below:
  // a fresh point would have to re-resolve its spline class and rebuild the road and loft geometry the old one still holds
  PtrTab<SplinePointObject> stash(tmpmem);
  for (int i = points.size() - 1; i >= 0; i--)
    if (points[i]->isFilletGen)
    {
      stash.push_back(points[i]);
      erase_items(points, i, 1);
    }
  for (int i = points.size() - 1; i >= 0; i--)
    points[i]->arrId = i;

  int n = points.size();
  bool closed = isClosed();
  int realCnt = closed ? n - 1 : n;
  bool allowFillet = anyFillet && realCnt >= 3;

  Tab<FilletCtx> ctx(tmpmem);
  BezierSpline3d base;

  if (allowFillet)
  {
    int pts_num = knotCount();
    SmallTab<Point3, TmpmemAlloc> pts;
    clear_and_resize(pts, pts_num * 3);
    for (int pi = 0; pi < pts_num; ++pi)
      getKnotControls(points[pi % n], KNOT_SOURCE, pts[pi * 3], pts[pi * 3 + 1], pts[pi * 3 + 2]);
    if (!base.calculate(pts.data(), pts.size(), false))
      allowFillet = false;
    else
    {
      ctx.resize(realCnt);
      // which corners round, and which segments they cut into
      for (int i = 0; i < realCnt; i++)
      {
        SplinePointObject *p = points[i];
        // a junction collapses its handles anyway; a mere touch of two splines is not one and still rounds
        if (p->getProps().filletR <= 0 || (p->isCross && p->isRealCross))
          continue;
        if (!poly && !closed && (i == 0 || i == realCnt - 1))
          continue; // open spline endpoints have no corner

        FilletCtx &c = ctx[i];
        if (poly)
          c.inSeg = i == 0 ? n - 1 : i - 1;
        else if (closed)
          c.inSeg = i == 0 ? n - 2 : i - 1;
        else
          c.inSeg = i - 1;
        c.outSeg = i;
        c.wants = c.active = true;
      }

      // a tenth of every segment stays outside the blends, so a cut never lands on a knot;
      // the rest is shared with the corner at the other end
      auto side_room = [&](int seg, int corner) {
        int left, right;
        seg_ends(seg, n, poly, closed, left, right);
        int other = left == corner ? right : left;
        bool shared = ctx[other].active; // seg_ends yields source indices, so other is always in range
        return (shared ? 0.45f : 0.9f) * base.segs[seg].len;
      };

      // room depends on the neighbour cutting, which depends on room: two rounds settle it
      for (int round = 0; round < 2; round++)
        for (int i = 0; i < realCnt; i++)
        {
          FilletCtx &c = ctx[i];
          if (!c.wants)
            continue;
          // symmetric cut: an arc needs the same tangent distance on both sides
          c.s = min(points[i]->getProps().filletR, min(side_room(c.inSeg, i), side_room(c.outSeg, i)));
          c.active = c.s >= 1e-3f;
        }

      for (int i = 0; i < realCnt; i++)
      {
        FilletCtx &c = ctx[i];
        if (!c.active)
          continue;

        SplinePointObject *p = points[i];
        const float s = c.s;
        const BezierSplineInt3d &segIn = base.segs[c.inSeg], &segOut = base.segs[c.outSeg];

        c.tIn = segIn.getTFromS(segIn.len - s);
        c.tOut = segOut.getTFromS(s);
        // the array holds source points only here, so i +- 1 are the neighbours
        const splineclass::Attr &prevAttr = points[i > 0 ? i - 1 : realCnt - 1]->getProps().attr;
        const splineclass::Attr &nextAttr = points[(i + 1) % realCnt]->getProps().attr;
        float fracIn = segIn.len > 0 ? 1.0f - s / segIn.len : 1.0f;
        float fracOut = segOut.len > 0 ? s / segOut.len : 0.0f;
        c.attrV0 = lerp_attr(prevAttr, p->getProps().attr, fracIn, p->getProps().attr);
        c.attrV1 = lerp_attr(p->getProps().attr, nextAttr, fracOut, p->getProps().attr);
        c.v0pos = segIn.point(c.tIn);
        c.v1pos = segOut.point(c.tOut);
        Point3 t0dir = normalize(segIn.tang(c.tIn));
        Point3 t1dir = normalize(segOut.tang(c.tOut));

        Point3 B[4];
        B[0] = c.v0pos;
        B[3] = c.v1pos;
        if (p->getProps().filletType == 1) // chamfer: straight cut
        {
          Point3 ch = (c.v1pos - c.v0pos) / 3.0f;
          B[1] = c.v0pos + ch;
          B[2] = c.v1pos - ch;
        }
        else
        {
          // arc approximation: handles toward the tangent lines intersection
          bool ok = false;
          float dt = t0dir * t1dir;
          float denom = 1.0f - dt * dt;
          if (denom > 1e-6f)
          {
            Point3 d = c.v1pos - c.v0pos;
            float a = (d * t0dir - dt * (d * t1dir)) / denom;
            float b = (dt * (d * t0dir) - d * t1dir) / denom;
            if (a > 0 && b < 0)
            {
              Point3 q = ((c.v0pos + t0dir * a) + (c.v1pos + t1dir * b)) * 0.5f;
              // no clamp: the denom guard above already keeps |dt| below 1, and a clamped delta would size the handles for
              // a gentler corner than the q they are measured to, overshooting the arc at a hairpin
              float delta = acosf(dt);
              float k = delta > 1e-4f ? (4.0f / 3.0f) * tanf(delta * 0.25f) / tanf(delta * 0.5f) : 2.0f / 3.0f;
              B[1] = c.v0pos + t0dir * (k * length(q - c.v0pos));
              B[2] = c.v1pos - t1dir * (k * length(c.v1pos - q));
              ok = true;
            }
          }
          if (!ok) // nearly straight or degenerate corner
          {
            float h = length(c.v1pos - c.v0pos) / 3.0f;
            B[1] = c.v0pos + t0dir * h;
            B[2] = c.v1pos - t1dir * h;
          }
        }

        // source knot at the blend middle
        Point3 a1 = lerp_p3(B[0], B[1], 0.5f), m1 = lerp_p3(B[1], B[2], 0.5f), c1 = lerp_p3(B[2], B[3], 0.5f);
        Point3 a2 = lerp_p3(a1, m1, 0.5f), c2 = lerp_p3(m1, c1, 0.5f);
        c.knotPt = lerp_p3(a2, c2, 0.5f);
        c.knotIn = a2;
        c.knotOut = c2;
        c.v0relOut = a1 - c.v0pos;
        c.v1relIn = c1 - c.v1pos;
      }

      // preserved base shape on cut segments: outer handles come from subdivision
      for (int k = 0; k < base.segs.size(); k++)
      {
        int left, right;
        seg_ends(k, n, poly, closed, left, right);
        bool lf = left < realCnt && ctx[left].active && ctx[left].outSeg == k;
        bool rf = right < realCnt && ctx[right].active && ctx[right].inSeg == k;
        if (!lf && !rf)
          continue;

        float ta = lf ? ctx[left].tOut : 0.0f;
        float tb = rf ? ctx[right].tIn : 1.0f;
        Point3 V[4], sub[4];
        base.segs[k].calculateBack(V);
        bezier_subcurve(V, ta, tb, sub);
        if (lf)
        {
          ctx[left].v1relOut = sub[1] - sub[0];
          ctx[left].v1adj = sub[2];
        }
        if (rf)
        {
          ctx[right].v0relIn = sub[2] - sub[3];
          ctx[right].v0adj = sub[1];
        }
      }
    }
  }

  // insert generated points, descending so indices stay valid
  Tab<SplinePointObject *> changedGens(tmpmem);
  for (int i = realCnt - 1; i >= 0; i--)
  {
    SplinePointObject *p = points[i];
    if (!allowFillet || !ctx[i].active)
    {
      // the corner is sharp again, and only segChanged makes updateChangedSegmentsLoftGeom rebuild the run it sits in
      if (p->hasActiveFillet())
      {
        p->segChanged = true;
        if (i > 0)
          points[i - 1]->segChanged = true;
        if (i + 1 < points.size())
          points[i + 1]->segChanged = true;
      }
      p->filletApplied = false;
      p->fillet.reset();
      continue;
    }
    const FilletCtx &c = ctx[i];

    // strong refs: the stash may hold the last reference to a reused point
    Ptr<SplinePointObject> gen[2];
    for (int gi = 0; gi < stash.size(); gi++)
      if (stash[gi] && stash[gi]->fillet->src == p)
      {
        gen[stash[gi]->fillet->role ? 1 : 0] = stash[gi];
        stash[gi] = nullptr;
      }
    for (int role = 0; role < 2; role++)
      if (!gen[role])
      {
        gen[role] = new SplinePointObject;
        gen[role]->isFilletGen = true;
        SplinePointObject::Fillet &gf = gen[role]->ensureFillet();
        gf.src = p;
        gf.role = role;
        gen[role]->spline = this;
        structureChanged = true;
      }

    SplinePointObject::Props np = *SplinePointObject::defaultProps;
    np.cornerType = 0; // explicit handles
    np.attr = c.attrV0;
    np.pt = c.v0pos;
    np.relIn = c.v0relIn;
    np.relOut = c.v0relOut;
    bool ch0 = !same_p3(gen[0]->getProps().pt, np.pt) || !same_p3(gen[0]->getProps().relIn, np.relIn) ||
               !same_p3(gen[0]->getProps().relOut, np.relOut) || !same_p3(gen[0]->fillet->adjHandle, c.v0adj);
    gen[0]->setProps(np);
    gen[0]->fillet->adjHandle = c.v0adj;

    np.attr = c.attrV1;
    np.pt = c.v1pos;
    np.relIn = c.v1relIn;
    np.relOut = c.v1relOut;
    bool ch1 = !same_p3(gen[1]->getProps().pt, np.pt) || !same_p3(gen[1]->getProps().relIn, np.relIn) ||
               !same_p3(gen[1]->getProps().relOut, np.relOut) || !same_p3(gen[1]->fillet->adjHandle, c.v1adj);
    gen[1]->setProps(np);
    gen[1]->fillet->adjHandle = c.v1adj;

    bool knotCh = !p->hasActiveFillet() || !same_p3(p->fillet->knotPt, c.knotPt) || !same_p3(p->fillet->knotIn, c.knotIn) ||
                  !same_p3(p->fillet->knotOut, c.knotOut);
    SplinePointObject::Fillet &pf = p->ensureFillet();
    pf.knotPt = c.knotPt;
    pf.knotIn = c.knotIn;
    pf.knotOut = c.knotOut;
    p->filletApplied = true;
    pf.cutS = c.s;

    // a cut that did not move leaves the geometry alone: segChanged is what makes updateChangedSegmentsLoftGeom rebuild a run
    if (ch0 || ch1 || knotCh)
    {
      gen[0]->segChanged = true;
      p->segChanged = true;
      gen[1]->segChanged = true;
      changedGens.push_back(gen[0]);
      changedGens.push_back(gen[1]);
    }

    // order around source: ..., prev, V0, src, V1, next, ...
    SplinePointObject *ins;
    ins = gen[1];
    insert_items(points, i + 1, 1, &ins);
    if (i > 0)
    {
      ins = gen[0];
      insert_items(points, i, 1, &ins);
    }
    else
    {
      // V0 lies on the last segment. A closed spline keeps points[0] == points.back(), which isClosed()/save rely on,
      // so insert before that duplicate; a polygon has none and takes V0 at the very end
      ins = gen[0];
      if (isClosed())
        insert_items(points, points.size() - 1, 1, &ins);
      else
        insert_items(points, points.size(), 1, &ins);
    }
  }

  // unused stash entries mean those fillets are gone
  for (int gi = 0; gi < stash.size(); gi++)
    if (stash[gi])
    {
      stash[gi]->clearSegment();
      structureChanged = true;
    }
  clear_and_shrink(stash);

  for (int i = points.size() - 1; i >= 0; i--)
    points[i]->arrId = i;

  // segments adjacent to a changed cut are owned by the outer neighbors
  for (int i = 0; i < changedGens.size(); i++)
  {
    int id = changedGens[i]->arrId;
    if (id > 0)
      points[id - 1]->segChanged = true;
    if (id + 1 < points.size())
      points[id + 1]->segChanged = true;
  }

  // only new points need a splineclass resolved: reused ones keep a valid asset chain,
  // and moved ones are refreshed through the segChanged flags above
  if (structureChanged && !SplineObject::isSplineObjectsAreLoading)
    prepareSplineClassInPoints();

  updatingFillets = false;

  if (!SplineObject::isSplineObjectsAreLoading)
    for (int i = 0; i < points.size(); i++)
      if (!points[i]->isFilletGen)
        points[i]->updateFilletHint();
}

void SplineObject::bakeFilletPoint(SplinePointObject *p)
{
  if (!p || p->isFilletGen || p->spline != this)
    return;

  updateFilletPoints(); // make sure derived state matches current props

  if (!p->hasActiveFillet())
    return; // filletR stays: it is a request, and the panel hint reports that it does not apply here

  // a polygon builds real points as plain knots, so a baked arc degrades to the polyline through them;
  // a straight chamfer is unaffected
  if (poly && !props.poly.smooth && p->getProps().filletType == 0)
    DAEDITOR3.conWarning("%s: polygon corner rounding baked to a polyline through its knots", getName());

  int id = p->arrId;
  int n = points.size();
  int i0 = id > 0 ? id - 1 : (poly ? n - 1 : n - 2);
  // strong refs: points[] may hold the last reference and they are erased below
  Ptr<SplinePointObject> g0 = points[i0], g1 = points[id + 1];
  if (!g0->isFilletGen || g0->fillet->src != p || !g1->isFilletGen || g1->fillet->src != p)
    return;

  SplinePointObject::Props gp0 = g0->getProps(), gp1 = g1->getProps();
  Point3 adj0 = g0->fillet->adjHandle, adj1 = g1->fillet->adjHandle;
  Point3 kPt = p->fillet->knotPt, kIn = p->fillet->knotIn, kOut = p->fillet->knotOut;

  // setPos/addObject below each rebuild the curve;
  // reconciling on every one would redo the whole strip and reinsert, so suppress until the last
  updatingFillets = true;

  // drop derived points of this fillet; they are replaced with real ones below
  g0->clearSegment();
  g1->clearSegment();
  for (int i = n - 1; i >= 0; i--)
    if (points[i] == g0 || points[i] == g1)
      erase_items(points, i, 1);
  for (int i = points.size() - 1; i >= 0; i--)
    points[i]->arrId = i;

  getObjEditor()->getUndoSystem()->put(p->makePropsUndoObj());
  SplinePointObject::Props np = p->getProps();
  np.filletR = 0;
  np.pt = kPt;
  np.relIn = kIn - kPt;
  np.relOut = kOut - kPt;
  np.cornerType = 0;
  p->setProps(np);
  // the knot is real from here on: with no fillet left anywhere the reconcile early-returns,
  // so it would never clear this and getKnotPos() would keep serving the blend middle
  p->filletApplied = false;
  p->fillet.reset();
  p->setPos(kPt);

  Ptr<SplinePointObject> n0 = new SplinePointObject;
  n0->setProps(gp0);
  n0->spline = this;
  n0->arrId = p->arrId > 0 ? p->arrId : (poly ? points.size() : points.size() - 1);
  getObjEditor()->addObject(n0);
  n0->setPos(gp0.pt);

  Ptr<SplinePointObject> n1 = new SplinePointObject;
  n1->setProps(gp1);
  n1->spline = this;
  n1->arrId = p->arrId + 1;
  getObjEditor()->addObject(n1);
  n1->setPos(gp1.pt);

  // outer neighbours keep the shape by adopting the subdivided handles, which only explicit smooth-tangent knots can hold.
  // A smooth curvature knot mirrors its handles, so it would have to become explicit to hold one: left alone instead,
  // since the deviation is the handle it does not shorten, which grows with the share of the segment the cut takes.
  // Polyline corners are straight there and need no fixup
  int effCornerDef = props.cornerType;
  SplinePointObject *prevR = points[n0->arrId - 1];
  int prevCt = prevR->getProps().cornerType == -2 ? effCornerDef : prevR->getProps().cornerType;
  if (!prevR->isFilletGen && !(prevR->isCross && prevR->isRealCross) && prevCt == 0)
  {
    getObjEditor()->getUndoSystem()->put(prevR->makePropsUndoObj());
    SplinePointObject::Props pp = prevR->getProps();
    pp.relOut = adj0 - pp.pt;
    prevR->setProps(pp);
    prevR->markChanged();
  }
  int nextId = n1->arrId + 1 < points.size() ? n1->arrId + 1 : (poly ? 0 : -1);
  if (nextId >= 0)
  {
    SplinePointObject *nextR = points[nextId];
    int nextCt = nextR->getProps().cornerType == -2 ? effCornerDef : nextR->getProps().cornerType;
    if (!nextR->isFilletGen && !(nextR->isCross && nextR->isRealCross) && nextCt == 0)
    {
      getObjEditor()->getUndoSystem()->put(nextR->makePropsUndoObj());
      SplinePointObject::Props pp = nextR->getProps();
      pp.relIn = adj1 - pp.pt;
      nextR->setProps(pp);
      nextR->markChanged();
    }
  }

  updatingFillets = false;
  pointChanged(-1);
  markModifChangedWhenUsed();
  getSpline();
}

// A blend cannot hold the shortened handles a shape preserving split needs, the next reconcile derives them again;
// baking the corner first makes the knots real and keeps the shape. The segments flanking a cut are baked too:
// refine() splits on the raw getBezierIn/Out, not the subdivided handles the curve uses there, so an unbaked split moves the shape.
void SplineObject::bakeFilletsAtSegment(int seg_id)
{
  if (seg_id < 0 || seg_id >= points.size())
    return;

  SplinePointObject *segEnd[2] = {points[seg_id], points[(seg_id + 1) % points.size()]};
  Ptr<SplinePointObject> corner[2];
  for (int i = 0; i < 2; i++)
  {
    if (segEnd[i]->isFilletGen)
      corner[i] = segEnd[i]->fillet->src;
    else if (segEnd[i]->hasActiveFillet())
      corner[i] = segEnd[i];
  }
  if (corner[0] == corner[1]) // both ends belong to the same blend
    corner[1] = nullptr;

  // strong refs: baking reshuffles points[], the corners themselves are kept
  for (int i = 0; i < 2; i++)
    if (corner[i])
      bakeFilletPoint(corner[i]);
}
