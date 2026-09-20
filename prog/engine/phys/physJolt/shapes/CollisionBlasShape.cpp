// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "CollisionBlasShape.h"
#include <Jolt/Core/Profiler.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/Shape/ScaleHelpers.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/CastConvexVsTriangles.h>
#include <Jolt/Physics/Collision/CastSphereVsTriangles.h>
#include <Jolt/Physics/Collision/CollideConvexVsTriangles.h>
#include <Jolt/Physics/Collision/CollideSphereVsTriangles.h>
#include <Jolt/Physics/Collision/CollideSoftBodyVerticesVsTriangles.h>
#include <Jolt/Physics/Collision/SortReverseAndStore.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Geometry/AABox4.h>
#include <Jolt/Geometry/RayAABox.h>
#include <Jolt/Geometry/RayTriangle.h>
#include <Jolt/Geometry/OrientedBox.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRenderer.h>
#endif
#include <daBVH/swBVHDefine.hlsli>
#include <float.h>

JPH_NAMESPACE_BEGIN

namespace
{
// 4 stack slots per tree level (a 4-wide node pushes at most 3 more than it pops), like the SoA4 walkers.
constexpr int cStackSize = BVH_MAX_BLAS_DEPTH * 4 + 8;
// bits that index `count` items: none for one
inline uint32 indexBits(uint32 count) { return count > 1 ? 32 - CountLeadingZeros(count - 1) : 0; }
} // namespace

// Jolt's MeshShape walk over the SoA4 tree: a stack of node refs (bit 31 clear, N and short mask in
// the tag bits) and LeafRefs (bit 31 set). A node hands its N lane boxes to the visitor 4 wide, lanes
// past N inverted so every test drops them; a leaf entry expands to its 1..4 triangles.
class CollisionBlasShape::DecodingContext
{
public:
  // A one-leaf BLAS has a raw root: it enters the stack as the leaf entry Walk's leaf branch takes.
  JPH_INLINE explicit DecodingContext(const CollisionBlasShape *inShape) : mShape(inShape)
  {
    const uint32 root = (uint32)inShape->mRoot.v;
    mStack[0] = (root & soa4::TAG_MASK) ? root : (uint32)soa4::makeRootLeafRef(root & soa4::PTR_OFS_MASK);
  }

  template <class Visitor>
  JPH_INLINE void Walk(Visitor &ioVisitor)
  {
    const uint8_t *tree = mShape->mTree;
    const vec4f sx = v_splat_x(mShape->mInvScale), sy = v_splat_y(mShape->mInvScale), sz = v_splat_z(mShape->mInvScale);
    const vec4f bx = v_splat_x(mShape->mBmin), by = v_splat_y(mShape->mBmin), bz = v_splat_z(mShape->mBmin);
    const vec4f bigPos = v_splats(FLT_MAX), bigNeg = v_splats(-FLT_MAX);
    do
    {
      const uint32 e = mStack[mTop];
      if (e & soa4::LEAF_ENTRY_FLAG)
        mShape->VisitLeaf(ioVisitor, (soa4::LeafRef)e);
      else
      {
        const soa4::NodeSoA nd(tree, e);
        const vec4f kill = v_ld((const float *)soa4::LANE_KILL[nd.N - 2]);
        const Vec4 minx(v_btsel(v_madd(nd.mnx, sx, bx), bigPos, kill));
        const Vec4 miny(v_btsel(v_madd(nd.mny, sy, by), bigPos, kill));
        const Vec4 minz(v_btsel(v_madd(nd.mnz, sz, bz), bigPos, kill));
        const Vec4 maxx(v_btsel(v_madd(nd.mxx, sx, bx), bigNeg, kill));
        const Vec4 maxy(v_btsel(v_madd(nd.mxy, sy, by), bigNeg, kill));
        const Vec4 maxz(v_btsel(v_madd(nd.mxz, sz, bz), bigNeg, kill));
        // child entries: a leaf lane (bit 31 set) becomes its LeafRef (makeLeafRef, vectorized: the
        // parent part is shared, the lane is the low 2 bits), an internal lane keeps its child ref
        const uint32 leafBase = soa4::LEAF_ENTRY_FLAG | (((e >> soa4::PTR_SHORT_SHIFT) & 15u) << soa4::LEAF_ENTRY_SHORT_SHIFT) |
                                ((e & soa4::TAG_MASK) << soa4::LEAF_ENTRY_N_SHIFT) | (e & soa4::LEAF_ENTRY_OFS_MASK);
        const vec4i leafRefs = v_addi(v_splatsi((int)leafBase), v_ldui(soa4::LANE_IDX));
        const vec4f isLeaf = v_cast_vec4f(v_srai(nd.wv, 31));
        UVec4 p(v_cast_vec4i(v_btsel(v_cast_vec4f(nd.wv), v_cast_vec4f(leafRefs), isLeaf)));
        const int n = ioVisitor.VisitNodes(minx, miny, minz, maxx, maxy, maxz, p, mTop);
        JPH_ASSERT(mTop + 4 < cStackSize);
        p.StoreInt4(&mStack[mTop]);
        mTop += n;
      }
      if (ioVisitor.ShouldAbort())
        return;
      do
        --mTop;
      while (mTop >= 0 && !ioVisitor.ShouldVisitNode(mTop));
    } while (mTop >= 0);
  }
  JPH_INLINE bool IsDoneWalking() const { return mTop < 0; }

private:
  const CollisionBlasShape *mShape;
  int mTop = 0;
  uint32 mStack[cStackSize];
};

template <class Visitor>
void CollisionBlasShape::WalkTree(Visitor &ioVisitor) const
{
  DecodingContext ctx(this);
  ctx.Walk(ioVisitor);
}

// A visitor with VisitLeaf tests the whole leaf itself (the 4-wide ray test); the others get the
// triangles one by one.
template <class Visitor>
JPH_INLINE void CollisionBlasShape::VisitLeaf(Visitor &ioVisitor, soa4::LeafRef ref) const
{
  if constexpr (requires { ioVisitor.VisitLeaf(*this, ref); })
    ioVisitor.VisitLeaf(*this, ref);
  else
    VisitLeafTriangles(ioVisitor, ref);
}

// The leaf's 1..4 triangles in chunk order (expandQuadLeafTris' winding: the strip's second triangle
// is (v1, v2, v3) with the first two corners swapped when flipped).
template <class Visitor>
JPH_INLINE void CollisionBlasShape::VisitLeafTriangles(Visitor &ioVisitor, soa4::LeafRef ref) const
{
  const soa4::LeafLoc l = soa4::decodeLeafRef(mTree, ref);
  const QuadLeafFields f = soa4::leafFields(mTree, l);
  const int baseA = l.bodyOfs + (int)f.relBaseBytes;
  LeafHit id{this, ref};
  uint32 k = 0;
  // the leaf's triangle order is the flags tail's and the sub shape id's `tri`: one home, expandQuadLeafTris
  expandQuadLeafTris(f, 0u, [&](uint32_t a, uint32_t b, uint32_t c) {
    ioVisitor.VisitTriangle(id, k++, DecodeVert(baseA, (int)a), DecodeVert(baseA, (int)b), DecodeVert(baseA, (int)c));
  });
}

// Closest hit of one leaf with Jolt's RayTriangle4 over the 4 strip lanes (0-1 quad A, 2-3 quad B;
// a lane without a triangle is masked). Returns the fraction below inClosest, else inClosest.
JPH_INLINE float CollisionBlasShape::RayLeaf(soa4::LeafRef ref, Vec3Arg inOrigin, Vec3Arg inDirection, float inClosest,
  uint32 &outTri) const
{
  const soa4::LeafLoc l = soa4::decodeLeafRef(mTree, ref);
  const QuadLeafFields f = soa4::leafFields(mTree, l);
  const int baseA = l.bodyOfs + (int)f.relBaseBytes;
  // box space -> node local per axis with the madd DecodeVert uses, so both paths see identical corners
  const vec4f sx = v_splat_x(mInvScale), sy = v_splat_y(mInvScale), sz = v_splat_z(mInvScale);
  const vec4f bx = v_splat_x(mBmin), by = v_splat_y(mBmin), bz = v_splat_z(mBmin);
  vec4f xs, ys, zs;
  swblas_unpack4SoA<BVH_BLAS_VERT21_STRIDE>(mTree, baseA, f.o1, f.o2, f.o3, xs, ys, zs);
  vec4f a0x, a0y, a0z, a1x, a1y, a1z, a2x, a2y, a2z;
  swblas_quadCorners2(f.flipSecond, v_madd(xs, sx, bx), v_madd(ys, sy, by), v_madd(zs, sz, bz), a0x, a0y, a0z, a1x, a1y, a1z, a2x, a2y,
    a2z);
  vec4f b0x = a0x, b0y = a0y, b0z = a0z, b1x = a1x, b1y = a1y, b1z = a1z, b2x = a2x, b2y = a2y, b2z = a2z;
  if (f.hasB)
  {
    swblas_unpack4SoA<BVH_BLAS_VERT21_STRIDE>(mTree, baseA + (int)f.deltaB * (int)BVH_BLAS_VERT21_STRIDE, f.o1b, f.o2b, f.o3b, xs, ys,
      zs);
    swblas_quadCorners2(f.flipSecondB, v_madd(xs, sx, bx), v_madd(ys, sy, by), v_madd(zs, sz, bz), b0x, b0y, b0z, b1x, b1y, b1z, b2x,
      b2y, b2z);
  }
  const Vec4 fraction = RayTriangle4(inOrigin, inDirection, Vec4(v_perm_xyab(a0x, b0x)), Vec4(v_perm_xyab(a0y, b0y)),
    Vec4(v_perm_xyab(a0z, b0z)), Vec4(v_perm_xyab(a1x, b1x)), Vec4(v_perm_xyab(a1y, b1y)), Vec4(v_perm_xyab(a1z, b1z)),
    Vec4(v_perm_xyab(a2x, b2x)), Vec4(v_perm_xyab(a2y, b2y)), Vec4(v_perm_xyab(a2z, b2z)));
  // lane 1 needs a 2-triangle quad A, lanes 2-3 a quad B, lane 3 a 2-triangle quad B
  alignas(16) static const uint32 cLaneValid[8][4] = {{~0u, ~0u, 0, 0}, {~0u, 0, 0, 0}, {~0u, ~0u, ~0u, ~0u}, {~0u, 0, ~0u, ~0u},
    {~0u, ~0u, 0, 0}, {~0u, 0, 0, 0}, {~0u, ~0u, ~0u, 0}, {~0u, 0, ~0u, 0}};
  const int lanes = (f.isSingle ? 1 : 0) | (f.hasB ? 2 : 0) | (f.isSingleB ? 4 : 0);
  const Vec4 valid = Vec4::sSelect(Vec4::sReplicate(FLT_MAX), fraction, UVec4::sLoadInt4Aligned(cLaneValid[lanes]));
  const float best = valid.ReduceMin();
  if (best >= inClosest)
    return inClosest;
  const int lane = CountTrailingZeros(Vec4::sEquals(valid, Vec4::sReplicate(best)).GetTrues()); // ties: the first lane, as a scan
  outTri = (uint32)lane - ((lane >= 2 && f.isSingle) ? 1u : 0u);
  return best;
}

CollisionBlasShape::CollisionBlasShape(const soa4::ChunkRef &chunk, uint32 compound_children, bool force_descent_ids,
  ShapeResult &out_result) :
  Shape(EShapeType::User1, EShapeSubType::User1)
{
  mTree = chunk.tree;
  mRoot = chunk.root;
  mBmin = chunk.bmin;
  mInvScale = chunk.invScale;
  mTriCount = chunk.triCount;
  // The id: parent dword offset sized to the tree, lane, triangle; the parent's layout bits (child
  // count, short mask) ride along when the compound leaves room.
  const uint32 ofsBits = indexBits(chunk.treeBytes / 4), compoundBits = indexBits(compound_children);
  mDescentIds = force_descent_ids || compoundBits + ofsBits + 10 > SubShapeID::MaxBits;
  mSubShapeBits = ofsBits + (mDescentIds ? 4 : 10);
  if (compoundBits + mSubShapeBits > SubShapeID::MaxBits) // not even the descent format fits this compound
  {
    out_result.SetError("sub shape ids do not fit: the compound is too deep for this tree");
    return;
  }
  // Bounds: the root's lane boxes (a node's lane box is its child subtree's box, so their union is
  // the leaf union), or the leaf's own box when the whole BLAS is one leaf block.
  vec3f lo, hi;
  const uint32 root = (uint32)mRoot.v;
  if (!(root & soa4::TAG_MASK))
  {
    const soa4::LeafLoc l = soa4::decodeLeafRef(mTree, soa4::makeRootLeafRef(root & soa4::PTR_OFS_MASK));
    soa4::rootLeafBox(mTree, l.bodyOfs, soa4::leafFields(mTree, l), lo, hi);
  }
  else
  {
    const soa4::NodeSoA nd(mTree, root);
    const vec4f kill = v_ld((const float *)soa4::LANE_KILL[nd.N - 2]);
    const vec4f bigPos = v_splats(FLT_MAX), bigNeg = v_splats(-FLT_MAX);
    lo = v_make_vec4f(Vec4(v_btsel(nd.mnx, bigPos, kill)).ReduceMin(), Vec4(v_btsel(nd.mny, bigPos, kill)).ReduceMin(),
      Vec4(v_btsel(nd.mnz, bigPos, kill)).ReduceMin(), 0.f);
    hi = v_make_vec4f(Vec4(v_btsel(nd.mxx, bigNeg, kill)).ReduceMax(), Vec4(v_btsel(nd.mxy, bigNeg, kill)).ReduceMax(),
      Vec4(v_btsel(nd.mxz, bigNeg, kill)).ReduceMax(), 0.f);
  }
  mLocalBounds = AABox(Vec3(v_perm_xyzz(v_madd(lo, mInvScale, mBmin))), Vec3(v_perm_xyzz(v_madd(hi, mInvScale, mBmin))));
  out_result.Set(this);
}

Shape::ShapeResult CollisionBlasShape::sCreate(const soa4::ChunkRef &chunk, uint32 compound_children, bool force_descent_ids)
{
  ShapeResult result;
  if (compound_children < 1) // only the resource overload knows a default to substitute
  {
    result.SetError("compound_children must be at least 1");
    return result;
  }
  Ref<CollisionBlasShape> shape = new CollisionBlasShape(chunk, compound_children, force_descent_ids, result);
  return result;
}

// The converter writes a node's child subtrees in lane order right behind the node, so from the root
// the internal child at or before node_ofs with the highest offset leads to the target; its child
// word carries the child count and short mask a LeafRef needs.
soa4::LeafRef CollisionBlasShape::LeafRefByDescent(uint32 node_ofs, uint32 lane) const
{
  uint32 cur = (uint32)mRoot.v;
  if (!(cur & soa4::TAG_MASK)) // the whole BLAS is one leaf block
    return soa4::makeRootLeafRef(node_ofs);
  for (int depth = 0; (cur & soa4::PTR_OFS_MASK) != node_ofs && depth < BVH_MAX_BLAS_DEPTH; ++depth)
  {
    const soa4::NodeRef nd(mTree, cur);
    uint32 best = 0;
    for (int i = 0; i < nd.N; ++i)
    {
      const uint32 w = nd.w()[i];
      if (!(w & soa4::LEAF_ENTRY_FLAG) && (w & soa4::PTR_OFS_MASK) <= node_ofs &&
          (w & soa4::PTR_OFS_MASK) >= (best & soa4::PTR_OFS_MASK))
        best = w;
    }
    JPH_ASSERT(best != 0);
    cur = best;
  }
  JPH_ASSERT((cur & soa4::PTR_OFS_MASK) == node_ofs);
  return soa4::makeLeafRef(cur, (int)lane);
}

bool CollisionBlasShape::SelfTestSubShapeIDs() const
{
  bool ok = true;
  soa4::iterateLeafRefs(
    mTree, mRoot, [](vec3f, vec3f) { return true; },
    [&](vec3f, vec3f, soa4::LeafRef ref, const soa4::LeafLoc &l) {
      const uint32 tris = quadLeafTriCount(soa4::leafFields(mTree, l));
      for (uint32 tri = 0; tri < tris && ok; ++tri)
      {
        soa4::LeafRef back;
        uint32 triBack;
        DecodeSubShapeID(EncodeSubShapeID(SubShapeIDCreator(), ref, tri), back, triBack);
        ok = back == ref && triBack == tri;
      }
      return !ok;
    });
  return ok;
}

void CollisionBlasShape::LeafTriangle(soa4::LeafRef ref, uint32 tri, Vec3 &outV0, Vec3 &outV1, Vec3 &outV2) const
{
  struct Visitor
  {
    uint32 wanted;
    Vec3 *v0, *v1, *v2;
    JPH_INLINE void VisitTriangle(LeafHit &, uint32 k, Vec3Arg a, Vec3Arg b, Vec3Arg c)
    {
      if (k == wanted)
        *v0 = a, *v1 = b, *v2 = c;
    }
  };
  Visitor visitor{tri, &outV0, &outV1, &outV2};
  VisitLeaf(visitor, ref);
}

void CollisionBlasShape::GetTriangle(const SubShapeID &inSubShapeID, Vec3 &outV0, Vec3 &outV1, Vec3 &outV2) const
{
  soa4::LeafRef ref;
  uint32 tri;
  DecodeSubShapeID(inSubShapeID, ref, tri);
  LeafTriangle(ref, tri, outV0, outV1, outV2);
}

Vec3 CollisionBlasShape::GetSurfaceNormal(const SubShapeID &inSubShapeID, Vec3Arg) const
{
  Vec3 v0, v1, v2;
  GetTriangle(inSubShapeID, v0, v1, v2);
  return (v2 - v1).Cross(v0 - v1).Normalized();
}

void CollisionBlasShape::GetSupportingFace(const SubShapeID &inSubShapeID, Vec3Arg, Vec3Arg inScale, Mat44Arg inCenterOfMassTransform,
  SupportingFace &outVertices) const
{
  outVertices.resize(3);
  GetTriangle(inSubShapeID, outVertices[0], outVertices[1], outVertices[2]);
  if (ScaleHelpers::IsInsideOut(inScale))
    std::swap(outVertices[1], outVertices[2]);
  const Mat44 transform = inCenterOfMassTransform.PreScaled(inScale);
  for (Vec3 &v : outVertices)
    v = transform * v;
}

bool CollisionBlasShape::CastRay(const RayCast &inRay, const SubShapeIDCreator &inSubShapeIDCreator, RayCastResult &ioHit) const
{
  JPH_PROFILE_FUNCTION();
  struct Visitor
  {
    JPH_INLINE explicit Visitor(const RayCast &inRay, const SubShapeIDCreator &inSubShapeIDCreator, RayCastResult &ioHit) :
      mHit(ioHit),
      mRayOrigin(inRay.mOrigin),
      mRayDirection(inRay.mDirection),
      mRayInvDirection(inRay.mDirection),
      mSubShapeIDCreator(inSubShapeIDCreator)
    {}
    JPH_INLINE bool ShouldAbort() const { return mHit.mFraction <= 0.0f; }
    JPH_INLINE bool ShouldVisitNode(int inStackTop) const { return mDistanceStack[inStackTop] < mHit.mFraction; }
    JPH_INLINE int VisitNodes(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX,
      Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop)
    {
      Vec4 distance =
        RayAABox4(mRayOrigin, mRayInvDirection, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ);
      distance = Vec4::sSelect(Vec4::sReplicate(FLT_MAX), distance, Vec4::sLessOrEqual(inBoundsMinY, inBoundsMaxY));
      return SortReverseAndStore(distance, mHit.mFraction, ioProperties, &mDistanceStack[inStackTop]);
    }
    JPH_INLINE void VisitLeaf(const CollisionBlasShape &inShape, soa4::LeafRef inRef)
    {
      uint32 tri;
      const float fraction = inShape.RayLeaf(inRef, mRayOrigin, mRayDirection, mHit.mFraction, tri);
      if (fraction < mHit.mFraction)
      {
        mHit.mFraction = fraction;
        mHit.mSubShapeID2 = inShape.EncodeSubShapeID(mSubShapeIDCreator, inRef, tri);
        mReturnValue = true;
      }
    }
    RayCastResult &mHit;
    Vec3 mRayOrigin;
    Vec3 mRayDirection;
    RayInvDirection mRayInvDirection;
    SubShapeIDCreator mSubShapeIDCreator;
    bool mReturnValue = false;
    float mDistanceStack[cStackSize]; //-V730_NOINIT
  };
  Visitor visitor(inRay, inSubShapeIDCreator, ioHit);
  WalkTree(visitor);
  return visitor.mReturnValue;
}

void CollisionBlasShape::CastRay(const RayCast &inRay, const RayCastSettings &inRayCastSettings,
  const SubShapeIDCreator &inSubShapeIDCreator, CastRayCollector &ioCollector, const ShapeFilter &inShapeFilter) const
{
  JPH_PROFILE_FUNCTION();
  if (!inShapeFilter.ShouldCollide(this, inSubShapeIDCreator.GetID()))
    return;
  struct Visitor
  {
    JPH_INLINE explicit Visitor(const CollisionBlasShape *inShape, const RayCast &inRay, const RayCastSettings &inRayCastSettings,
      const SubShapeIDCreator &inSubShapeIDCreator, CastRayCollector &ioCollector) :
      mCollector(ioCollector),
      mRayOrigin(inRay.mOrigin),
      mRayDirection(inRay.mDirection),
      mRayInvDirection(inRay.mDirection),
      mBackFaceMode(inRayCastSettings.mBackFaceModeTriangles),
      mShape(inShape),
      mSubShapeIDCreator(inSubShapeIDCreator)
    {}
    JPH_INLINE bool ShouldAbort() const { return mCollector.ShouldEarlyOut(); }
    JPH_INLINE bool ShouldVisitNode(int inStackTop) const { return mDistanceStack[inStackTop] < mCollector.GetEarlyOutFraction(); }
    JPH_INLINE int VisitNodes(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX,
      Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop)
    {
      Vec4 distance =
        RayAABox4(mRayOrigin, mRayInvDirection, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ);
      distance = Vec4::sSelect(Vec4::sReplicate(FLT_MAX), distance, Vec4::sLessOrEqual(inBoundsMinY, inBoundsMaxY));
      return SortReverseAndStore(distance, mCollector.GetEarlyOutFraction(), ioProperties, &mDistanceStack[inStackTop]);
    }
    JPH_INLINE void VisitTriangle(LeafHit &inLeaf, uint32 inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2) const
    {
      if (mBackFaceMode == EBackFaceMode::IgnoreBackFaces && (inV2 - inV0).Cross(inV1 - inV0).Dot(mRayDirection) < 0)
        return;
      const float fraction = RayTriangle(mRayOrigin, mRayDirection, inV0, inV1, inV2);
      if (fraction < mCollector.GetEarlyOutFraction())
      {
        RayCastResult hit;
        hit.mBodyID = TransformedShape::sGetBodyID(mCollector.GetContext());
        hit.mFraction = fraction;
        hit.mSubShapeID2 = mShape->EncodeSubShapeID(mSubShapeIDCreator, inLeaf.ref, inTriangle);
        mCollector.AddHit(hit);
      }
    }
    CastRayCollector &mCollector;
    Vec3 mRayOrigin;
    Vec3 mRayDirection;
    RayInvDirection mRayInvDirection;
    EBackFaceMode mBackFaceMode;
    const CollisionBlasShape *mShape;
    SubShapeIDCreator mSubShapeIDCreator;
    float mDistanceStack[cStackSize]; //-V730_NOINIT
  };
  Visitor visitor(this, inRay, inRayCastSettings, inSubShapeIDCreator, ioCollector);
  WalkTree(visitor);
}

void CollisionBlasShape::CollidePoint(Vec3Arg inPoint, const SubShapeIDCreator &inSubShapeIDCreator,
  CollidePointCollector &ioCollector, const ShapeFilter &inShapeFilter) const
{
  sCollidePointUsingRayCast(*this, inPoint, inSubShapeIDCreator, ioCollector, inShapeFilter);
}

void CollisionBlasShape::CollideSoftBodyVertices(Mat44Arg inCenterOfMassTransform, Vec3Arg inScale,
  const CollideSoftBodyVertexIterator &inVertices, uint inNumVertices, int inCollidingShapeIndex) const
{
  JPH_PROFILE_FUNCTION();
  struct Visitor : public CollideSoftBodyVerticesVsTriangles
  {
    using CollideSoftBodyVerticesVsTriangles::CollideSoftBodyVerticesVsTriangles;
    JPH_INLINE bool ShouldAbort() const { return false; }
    JPH_INLINE bool ShouldVisitNode(int inStackTop) const { return mDistanceStack[inStackTop] < mClosestDistanceSq; }
    JPH_INLINE int VisitNodes(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX,
      Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop)
    {
      Vec4 bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z;
      AABox4Scale(mScale, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ, bounds_min_x,
        bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
      Vec4 dist_sq =
        AABox4DistanceSqToPoint(mLocalPosition, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
      dist_sq = Vec4::sSelect(Vec4::sReplicate(FLT_MAX), dist_sq, Vec4::sLessOrEqual(inBoundsMinY, inBoundsMaxY));
      return SortReverseAndStore(dist_sq, mClosestDistanceSq, ioProperties, &mDistanceStack[inStackTop]);
    }
    JPH_INLINE void VisitTriangle(LeafHit &, uint32, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2) { ProcessTriangle(inV0, inV1, inV2); }
    float mDistanceStack[cStackSize];
  };
  Visitor visitor(inCenterOfMassTransform, inScale);
  for (CollideSoftBodyVertexIterator v = inVertices, sbv_end = inVertices + inNumVertices; v != sbv_end; ++v)
    if (v.GetInvMass() > 0.0f)
    {
      visitor.StartVertex(v);
      WalkTree(visitor);
      visitor.FinishVertex(v, inCollidingShapeIndex);
    }
}

// A leaf's 1..4 triangles go to the caller's buffer whole. A leaf that does not fit ends the call
// with the walk still on it and the next call retries it, MeshShape's rule; a call always has room
// for one leaf (cGetTrianglesMinTrianglesRequested >= 4).
struct CollisionBlasShape::GetTrianglesCtx
{
  GetTrianglesCtx(const CollisionBlasShape *inShape, const AABox &inBox, Vec3Arg inPositionCOM, QuatArg inRotation, Vec3Arg inScale) :
    mDecodeCtx(inShape),
    mLocalBox(Mat44::sInverseRotationTranslation(inRotation, inPositionCOM), inBox),
    mScale(inScale),
    mLocalToWorld(Mat44::sRotationTranslation(inRotation, inPositionCOM) * Mat44::sScale(inScale)),
    mIsInsideOut(ScaleHelpers::IsInsideOut(inScale))
  {}
  JPH_INLINE bool ShouldAbort() const { return mAbort; }
  JPH_INLINE bool ShouldVisitNode(int) const { return true; }
  JPH_INLINE int VisitNodes(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX,
    Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int) const
  {
    Vec4 bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z;
    AABox4Scale(mScale, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ, bounds_min_x, bounds_min_y,
      bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
    UVec4 collides = AABox4VsBox(mLocalBox, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
    collides = UVec4::sAnd(collides, Vec4::sLessOrEqual(inBoundsMinY, inBoundsMaxY));
    return CountAndSortTrues(collides, ioProperties);
  }
  JPH_INLINE void VisitLeaf(const CollisionBlasShape &inShape, soa4::LeafRef inRef)
  {
    const soa4::LeafLoc l = soa4::decodeLeafRef(inShape.mTree, inRef);
    if (mNumTrianglesFound + (int)quadLeafTriCount(soa4::leafFields(inShape.mTree, l)) > mMaxTrianglesRequested)
    {
      mAbort = true; // the walk stays on this leaf
      return;
    }
    inShape.VisitLeafTriangles(*this, inRef);
  }
  JPH_INLINE void VisitTriangle(LeafHit &, uint32, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2) { Store(inV0, inV1, inV2); }
  JPH_INLINE void Store(Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2)
  {
    (mLocalToWorld * inV0).StoreFloat3(mTriangleVertices++);
    (mLocalToWorld * (mIsInsideOut ? inV2 : inV1)).StoreFloat3(mTriangleVertices++);
    (mLocalToWorld * (mIsInsideOut ? inV1 : inV2)).StoreFloat3(mTriangleVertices++);
    if (mMaterials != nullptr)
      *mMaterials++ = PhysicsMaterial::sDefault;
    mNumTrianglesFound++;
  }
  DecodingContext mDecodeCtx;
  OrientedBox mLocalBox;
  Vec3 mScale;
  Mat44 mLocalToWorld;
  int mMaxTrianglesRequested = 0;
  Float3 *mTriangleVertices = nullptr;
  int mNumTrianglesFound = 0;
  const PhysicsMaterial **mMaterials = nullptr;
  bool mAbort = false;
  bool mIsInsideOut;
};

void CollisionBlasShape::GetTrianglesStart(GetTrianglesContext &ioContext, const AABox &inBox, Vec3Arg inPositionCOM,
  QuatArg inRotation, Vec3Arg inScale) const
{
  static_assert(sizeof(GetTrianglesCtx) <= sizeof(GetTrianglesContext), "GetTrianglesContext too small");
  JPH_ASSERT(IsAligned(&ioContext, alignof(GetTrianglesCtx)));
  new (&ioContext) GetTrianglesCtx(this, inBox, inPositionCOM, inRotation, inScale);
}

int CollisionBlasShape::GetTrianglesNext(GetTrianglesContext &ioContext, int inMaxTrianglesRequested, Float3 *outTriangleVertices,
  const PhysicsMaterial **outMaterials) const
{
  static_assert(cGetTrianglesMinTrianglesRequested >= 4, "a leaf's triangles must fit one call");
  JPH_ASSERT(inMaxTrianglesRequested >= cGetTrianglesMinTrianglesRequested);
  GetTrianglesCtx &context = (GetTrianglesCtx &)ioContext; //-V1027 Jolt hands an opaque context block, like MeshShape
  if (context.mDecodeCtx.IsDoneWalking())
    return 0;
  context.mMaxTrianglesRequested = inMaxTrianglesRequested;
  context.mTriangleVertices = outTriangleVertices;
  context.mMaterials = outMaterials;
  context.mNumTrianglesFound = 0;
  context.mAbort = false;
  context.mDecodeCtx.Walk(context);
  return context.mNumTrianglesFound;
}

Shape::Stats CollisionBlasShape::GetStats() const { return Stats(sizeof(*this), mTriCount); }

#ifdef JPH_DEBUG_RENDERER
void CollisionBlasShape::Draw(DebugRenderer *inRenderer, RMat44Arg inCenterOfMassTransform, Vec3Arg inScale, ColorArg inColor, bool,
  bool inDrawWireframe) const
{
  struct Visitor
  {
    JPH_INLINE bool ShouldAbort() const { return false; }
    JPH_INLINE bool ShouldVisitNode(int) const { return true; }
    JPH_INLINE int VisitNodes(Vec4Arg, Vec4Arg inBoundsMinY, Vec4Arg, Vec4Arg, Vec4Arg inBoundsMaxY, Vec4Arg, UVec4 &ioProperties,
      int) const
    {
      return CountAndSortTrues(Vec4::sLessOrEqual(inBoundsMinY, inBoundsMaxY), ioProperties);
    }
    JPH_INLINE void VisitTriangle(LeafHit &, uint32, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2) const
    {
      const RVec3 a = mTransform * inV0, b = mTransform * inV1, c = mTransform * inV2;
      if (mWireframe)
        mRenderer->DrawWireTriangle(a, b, c, mColor);
      else
        mRenderer->DrawTriangle(a, b, c, mColor, DebugRenderer::ECastShadow::On);
    }
    DebugRenderer *mRenderer;
    RMat44 mTransform;
    Color mColor;
    bool mWireframe;
  };
  Visitor visitor{inRenderer, inCenterOfMassTransform.PreScaled(inScale), inColor, inDrawWireframe};
  WalkTree(visitor);
}
#endif

void CollisionBlasShape::sCollideConvexVsBlas(const Shape *inShape1, const Shape *inShape2, Vec3Arg inScale1, Vec3Arg inScale2,
  Mat44Arg inCenterOfMassTransform1, Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator &inSubShapeIDCreator1,
  const SubShapeIDCreator &inSubShapeIDCreator2, const CollideShapeSettings &inCollideShapeSettings,
  CollideShapeCollector &ioCollector, [[maybe_unused]] const ShapeFilter &inShapeFilter)
{
  JPH_PROFILE_FUNCTION();
  JPH_ASSERT(inShape1->GetType() == EShapeType::Convex);
  JPH_ASSERT(inShape2->GetSubType() == EShapeSubType::User1);
  const ConvexShape *shape1 = static_cast<const ConvexShape *>(inShape1);
  const CollisionBlasShape *shape2 = static_cast<const CollisionBlasShape *>(inShape2);
  struct Visitor : public CollideConvexVsTriangles
  {
    using CollideConvexVsTriangles::CollideConvexVsTriangles;
    JPH_INLINE bool ShouldAbort() const { return mCollector.ShouldEarlyOut(); }
    JPH_INLINE bool ShouldVisitNode(int) const { return true; }
    JPH_INLINE int VisitNodes(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX,
      Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int) const
    {
      Vec4 bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z;
      AABox4Scale(mScale2, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ, bounds_min_x,
        bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
      UVec4 collides =
        AABox4VsBox(mBoundsOf1InSpaceOf2, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
      collides = UVec4::sAnd(collides, Vec4::sLessOrEqual(inBoundsMinY, inBoundsMaxY));
      return CountAndSortTrues(collides, ioProperties);
    }
    JPH_INLINE void VisitTriangle(LeafHit &inLeaf, uint32 inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2)
    {
      Collide(inV0, inV1, inV2, mShape2->EdgeFlags(inLeaf, inTriangle),
        mShape2->EncodeSubShapeID(mSubShapeIDCreator2, inLeaf.ref, inTriangle));
    }
    const CollisionBlasShape *mShape2;
    SubShapeIDCreator mSubShapeIDCreator2;
  };
  Visitor visitor(shape1, inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1.GetID(),
    inCollideShapeSettings, ioCollector);
  visitor.mShape2 = shape2;
  visitor.mSubShapeIDCreator2 = inSubShapeIDCreator2;
  shape2->WalkTree(visitor);
}

void CollisionBlasShape::sCollideSphereVsBlas(const Shape *inShape1, const Shape *inShape2, Vec3Arg inScale1, Vec3Arg inScale2,
  Mat44Arg inCenterOfMassTransform1, Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator &inSubShapeIDCreator1,
  const SubShapeIDCreator &inSubShapeIDCreator2, const CollideShapeSettings &inCollideShapeSettings,
  CollideShapeCollector &ioCollector, [[maybe_unused]] const ShapeFilter &inShapeFilter)
{
  JPH_PROFILE_FUNCTION();
  JPH_ASSERT(inShape1->GetSubType() == EShapeSubType::Sphere);
  JPH_ASSERT(inShape2->GetSubType() == EShapeSubType::User1);
  const SphereShape *shape1 = static_cast<const SphereShape *>(inShape1);
  const CollisionBlasShape *shape2 = static_cast<const CollisionBlasShape *>(inShape2);
  struct Visitor : public CollideSphereVsTriangles
  {
    using CollideSphereVsTriangles::CollideSphereVsTriangles;
    JPH_INLINE bool ShouldAbort() const { return mCollector.ShouldEarlyOut(); }
    JPH_INLINE bool ShouldVisitNode(int) const { return true; }
    JPH_INLINE int VisitNodes(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX,
      Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int) const
    {
      Vec4 bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z;
      AABox4Scale(mScale2, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ, bounds_min_x,
        bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
      UVec4 collides = AABox4VsSphere(mSphereCenterIn2, mRadiusPlusMaxSeparationSq, bounds_min_x, bounds_min_y, bounds_min_z,
        bounds_max_x, bounds_max_y, bounds_max_z);
      collides = UVec4::sAnd(collides, Vec4::sLessOrEqual(inBoundsMinY, inBoundsMaxY));
      return CountAndSortTrues(collides, ioProperties);
    }
    JPH_INLINE void VisitTriangle(LeafHit &inLeaf, uint32 inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2)
    {
      Collide(inV0, inV1, inV2, mShape2->EdgeFlags(inLeaf, inTriangle),
        mShape2->EncodeSubShapeID(mSubShapeIDCreator2, inLeaf.ref, inTriangle));
    }
    const CollisionBlasShape *mShape2;
    SubShapeIDCreator mSubShapeIDCreator2;
  };
  Visitor visitor(shape1, inScale1, inScale2, inCenterOfMassTransform1, inCenterOfMassTransform2, inSubShapeIDCreator1.GetID(),
    inCollideShapeSettings, ioCollector);
  visitor.mShape2 = shape2;
  visitor.mSubShapeIDCreator2 = inSubShapeIDCreator2;
  shape2->WalkTree(visitor);
}

void CollisionBlasShape::sCastConvexVsBlas(const ShapeCast &inShapeCast, const ShapeCastSettings &inShapeCastSettings,
  const Shape *inShape, Vec3Arg inScale, [[maybe_unused]] const ShapeFilter &inShapeFilter, Mat44Arg inCenterOfMassTransform2,
  const SubShapeIDCreator &inSubShapeIDCreator1, const SubShapeIDCreator &inSubShapeIDCreator2, CastShapeCollector &ioCollector)
{
  JPH_PROFILE_FUNCTION();
  JPH_ASSERT(inShape->GetSubType() == EShapeSubType::User1);
  const CollisionBlasShape *shape = static_cast<const CollisionBlasShape *>(inShape);
  struct Visitor : public CastConvexVsTriangles
  {
    using CastConvexVsTriangles::CastConvexVsTriangles;
    JPH_INLINE bool ShouldAbort() const { return mCollector.ShouldEarlyOut(); }
    JPH_INLINE bool ShouldVisitNode(int inStackTop) const
    {
      return mDistanceStack[inStackTop] < mCollector.GetPositiveEarlyOutFraction();
    }
    JPH_INLINE int VisitNodes(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX,
      Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop)
    {
      Vec4 bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z;
      AABox4Scale(mScale, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ, bounds_min_x,
        bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
      AABox4EnlargeWithExtent(mBoxExtent, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
      Vec4 distance =
        RayAABox4(mBoxCenter, mInvDirection, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
      distance = Vec4::sSelect(Vec4::sReplicate(FLT_MAX), distance, Vec4::sLessOrEqual(inBoundsMinY, inBoundsMaxY));
      return SortReverseAndStore(distance, mCollector.GetPositiveEarlyOutFraction(), ioProperties, &mDistanceStack[inStackTop]);
    }
    JPH_INLINE void VisitTriangle(LeafHit &inLeaf, uint32 inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2)
    {
      Cast(inV0, inV1, inV2, mShape2->EdgeFlags(inLeaf, inTriangle),
        mShape2->EncodeSubShapeID(mSubShapeIDCreator2, inLeaf.ref, inTriangle));
    }
    const CollisionBlasShape *mShape2;
    RayInvDirection mInvDirection;
    Vec3 mBoxCenter;
    Vec3 mBoxExtent;
    SubShapeIDCreator mSubShapeIDCreator2;
    float mDistanceStack[cStackSize]; //-V730_NOINIT
  };
  Visitor visitor(inShapeCast, inShapeCastSettings, inScale, inCenterOfMassTransform2, inSubShapeIDCreator1, ioCollector);
  visitor.mShape2 = shape;
  visitor.mInvDirection.Set(inShapeCast.mDirection);
  visitor.mBoxCenter = inShapeCast.mShapeWorldBounds.GetCenter();
  visitor.mBoxExtent = inShapeCast.mShapeWorldBounds.GetExtent();
  visitor.mSubShapeIDCreator2 = inSubShapeIDCreator2;
  shape->WalkTree(visitor);
}

void CollisionBlasShape::sCastSphereVsBlas(const ShapeCast &inShapeCast, const ShapeCastSettings &inShapeCastSettings,
  const Shape *inShape, Vec3Arg inScale, [[maybe_unused]] const ShapeFilter &inShapeFilter, Mat44Arg inCenterOfMassTransform2,
  const SubShapeIDCreator &inSubShapeIDCreator1, const SubShapeIDCreator &inSubShapeIDCreator2, CastShapeCollector &ioCollector)
{
  JPH_PROFILE_FUNCTION();
  JPH_ASSERT(inShape->GetSubType() == EShapeSubType::User1);
  const CollisionBlasShape *shape = static_cast<const CollisionBlasShape *>(inShape);
  struct Visitor : public CastSphereVsTriangles
  {
    using CastSphereVsTriangles::CastSphereVsTriangles;
    JPH_INLINE bool ShouldAbort() const { return mCollector.ShouldEarlyOut(); }
    JPH_INLINE bool ShouldVisitNode(int inStackTop) const
    {
      return mDistanceStack[inStackTop] < mCollector.GetPositiveEarlyOutFraction();
    }
    JPH_INLINE int VisitNodes(Vec4Arg inBoundsMinX, Vec4Arg inBoundsMinY, Vec4Arg inBoundsMinZ, Vec4Arg inBoundsMaxX,
      Vec4Arg inBoundsMaxY, Vec4Arg inBoundsMaxZ, UVec4 &ioProperties, int inStackTop)
    {
      Vec4 bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z;
      AABox4Scale(mScale, inBoundsMinX, inBoundsMinY, inBoundsMinZ, inBoundsMaxX, inBoundsMaxY, inBoundsMaxZ, bounds_min_x,
        bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
      AABox4EnlargeWithExtent(Vec3::sReplicate(mRadius), bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y,
        bounds_max_z);
      Vec4 distance =
        RayAABox4(mStart, mInvDirection, bounds_min_x, bounds_min_y, bounds_min_z, bounds_max_x, bounds_max_y, bounds_max_z);
      distance = Vec4::sSelect(Vec4::sReplicate(FLT_MAX), distance, Vec4::sLessOrEqual(inBoundsMinY, inBoundsMaxY));
      return SortReverseAndStore(distance, mCollector.GetPositiveEarlyOutFraction(), ioProperties, &mDistanceStack[inStackTop]);
    }
    JPH_INLINE void VisitTriangle(LeafHit &inLeaf, uint32 inTriangle, Vec3Arg inV0, Vec3Arg inV1, Vec3Arg inV2)
    {
      Cast(inV0, inV1, inV2, mShape2->EdgeFlags(inLeaf, inTriangle),
        mShape2->EncodeSubShapeID(mSubShapeIDCreator2, inLeaf.ref, inTriangle));
    }
    const CollisionBlasShape *mShape2;
    RayInvDirection mInvDirection;
    SubShapeIDCreator mSubShapeIDCreator2;
    float mDistanceStack[cStackSize]; //-V730_NOINIT
  };
  Visitor visitor(inShapeCast, inShapeCastSettings, inScale, inCenterOfMassTransform2, inSubShapeIDCreator1, ioCollector);
  visitor.mShape2 = shape;
  visitor.mInvDirection.Set(inShapeCast.mDirection);
  visitor.mSubShapeIDCreator2 = inSubShapeIDCreator2;
  shape->WalkTree(visitor);
}

void CollisionBlasShape::sRegister()
{
  ShapeFunctions &f = ShapeFunctions::sGet(EShapeSubType::User1);
  f.mColor = Color::sOrange;
  for (EShapeSubType s : sConvexSubShapeTypes)
  {
    if (s != EShapeSubType::Sphere) // the sphere pair has its own kernels, registered below
    {
      CollisionDispatch::sRegisterCollideShape(s, EShapeSubType::User1, sCollideConvexVsBlas);
      CollisionDispatch::sRegisterCastShape(s, EShapeSubType::User1, sCastConvexVsBlas);
    }
    CollisionDispatch::sRegisterCastShape(EShapeSubType::User1, s, CollisionDispatch::sReversedCastShape);
    CollisionDispatch::sRegisterCollideShape(EShapeSubType::User1, s, CollisionDispatch::sReversedCollideShape);
  }
  CollisionDispatch::sRegisterCollideShape(EShapeSubType::Sphere, EShapeSubType::User1, sCollideSphereVsBlas);
  CollisionDispatch::sRegisterCastShape(EShapeSubType::Sphere, EShapeSubType::User1, sCastSphereVsBlas);
}

JPH_NAMESPACE_END
