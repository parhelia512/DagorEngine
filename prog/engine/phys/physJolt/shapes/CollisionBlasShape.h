// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Core/Array.h>
#include <vecmath/dag_vecMath.h> // the daBVH headers use vecmath without including it
#include <daBVH/dag_swBLAS_soa4.h>

JPH_NAMESPACE_BEGIN

class CollideShapeSettings;

/// A Jolt shape over a collision mesh node's SoA4 chunk as stored: no conversion, no index of its
/// own. Queries walk the chunk's 4-wide tree with Jolt's visitor pattern and hand the decoded
/// triangles to Jolt's triangle collide/cast code with the chunk's active-edge flags. The chunk's
/// owner keeps the bytes alive and immutable for the shape's lifetime.
class JPH_EXPORT CollisionBlasShape : public Shape
{
public:
  JPH_OVERRIDE_NEW_DELETE

  // The shape over a chunk its owner keeps alive. compound_children >= 1 sizes the sub shape id for
  // the compound the shape will sit in (1: alone in its body); force_descent_ids: the descent id
  // format regardless (tests).
  static ShapeResult sCreate(const soa4::ChunkRef &chunk, uint32 compound_children = 1, bool force_descent_ids = false);

  bool UsesDescentIDs() const { return mDescentIds; }
  // The triangle a sub shape id of this shape names, node local, in the emitted winding.
  void GetTriangle(const SubShapeID &inSubShapeID, Vec3 &outV0, Vec3 &outV1, Vec3 &outV2) const;
  // Every leaf triangle's id decodes back to it (the descent relies on the converter's layout).
  bool SelfTestSubShapeIDs() const;

  virtual AABox GetLocalBounds() const override { return mLocalBounds; }
  virtual uint GetSubShapeIDBitsRecursive() const override { return mSubShapeBits; }
  virtual bool MustBeStatic() const override { return true; } // like MeshShape: no volume, no mesh vs mesh
  virtual float GetInnerRadius() const override { return 0.0f; }
  virtual MassProperties GetMassProperties() const override { return MassProperties(); } // a mesh has no volume, like MeshShape
  virtual const PhysicsMaterial *GetMaterial(const SubShapeID &) const override { return PhysicsMaterial::sDefault; }
  virtual Vec3 GetSurfaceNormal(const SubShapeID &inSubShapeID, Vec3Arg inLocalSurfacePosition) const override;
  virtual void GetSupportingFace(const SubShapeID &inSubShapeID, Vec3Arg inDirection, Vec3Arg inScale,
    Mat44Arg inCenterOfMassTransform, SupportingFace &outVertices) const override;
  virtual void GetSubmergedVolume(Mat44Arg, Vec3Arg, const Plane &, float &outTotalVolume, float &outSubmergedVolume,
    Vec3 &outCenterOfBuoyancy JPH_IF_DEBUG_RENDERER(, RVec3Arg)) const override
  {
    outTotalVolume = 0.0f;
    outSubmergedVolume = 0.0f;
    outCenterOfBuoyancy = Vec3::sZero();
  }
#ifdef JPH_DEBUG_RENDERER
  virtual void Draw(DebugRenderer *inRenderer, RMat44Arg inCenterOfMassTransform, Vec3Arg inScale, ColorArg inColor,
    bool inUseMaterialColors, bool inDrawWireframe) const override;
#endif
  virtual bool CastRay(const RayCast &inRay, const SubShapeIDCreator &inSubShapeIDCreator, RayCastResult &ioHit) const override;
  virtual void CastRay(const RayCast &inRay, const RayCastSettings &inRayCastSettings, const SubShapeIDCreator &inSubShapeIDCreator,
    CastRayCollector &ioCollector, const ShapeFilter &inShapeFilter = {}) const override;
  virtual void CollidePoint(Vec3Arg inPoint, const SubShapeIDCreator &inSubShapeIDCreator, CollidePointCollector &ioCollector,
    const ShapeFilter &inShapeFilter = {}) const override;
  virtual void CollideSoftBodyVertices(Mat44Arg inCenterOfMassTransform, Vec3Arg inScale,
    const CollideSoftBodyVertexIterator &inVertices, uint inNumVertices, int inCollidingShapeIndex) const override;
  virtual void GetTrianglesStart(GetTrianglesContext &ioContext, const AABox &inBox, Vec3Arg inPositionCOM, QuatArg inRotation,
    Vec3Arg inScale) const override;
  virtual int GetTrianglesNext(GetTrianglesContext &ioContext, int inMaxTrianglesRequested, Float3 *outTriangleVertices,
    const PhysicsMaterial **outMaterials = nullptr) const override;
  virtual Stats GetStats() const override;
  virtual float GetVolume() const override { return 0; }

  // Registers the shape functions and the collide/cast pairs; call after RegisterTypes().
  static void sRegister();

protected:
  CollisionBlasShape(const soa4::ChunkRef &chunk, uint32 compound_children, bool force_descent_ids, ShapeResult &out_result);

private:
  class DecodingContext;
  struct GetTrianglesCtx;
  // A leaf handed to a visitor: its ref (the sub shape id source) and its edge flags word, read only
  // for a triangle that needs it.
  struct LeafHit
  {
    const CollisionBlasShape *shape = nullptr;
    soa4::LeafRef ref = 0;
    int flags = -1;
    JPH_INLINE uint32 Flags()
    {
      if (flags < 0)
        flags = soa4::leafEdgeFlags(shape->mTree, ref);
      return (uint32)flags;
    }
  };
  template <class Visitor>
  void WalkTree(Visitor &ioVisitor) const;
  template <class Visitor>
  void VisitLeaf(Visitor &ioVisitor, soa4::LeafRef ref) const;
  template <class Visitor>
  void VisitLeafTriangles(Visitor &ioVisitor, soa4::LeafRef ref) const;
  void LeafTriangle(soa4::LeafRef ref, uint32 tri, Vec3 &outV0, Vec3 &outV1, Vec3 &outV2) const;
  JPH_INLINE float RayLeaf(soa4::LeafRef ref, Vec3Arg inOrigin, Vec3Arg inDirection, float inClosest, uint32 &outTri) const;
  JPH_INLINE Vec3 DecodeVert(int base, int ofs) const
  {
    return Vec3(v_perm_xyzz(v_madd(RayData::unpackVert21(mTree + base + ofs * (int)BVH_BLAS_VERT21_STRIDE), mInvScale, mBmin)));
  }
  // The LeafRef of lane `lane` of the node at byte offset node_ofs, found by the root descent.
  soa4::LeafRef LeafRefByDescent(uint32 node_ofs, uint32 lane) const;
  JPH_INLINE SubShapeID EncodeSubShapeID(const SubShapeIDCreator &inCreator, soa4::LeafRef ref, uint32 tri) const
  {
    const uint32 ofs = (ref & soa4::LEAF_ENTRY_OFS_MASK) >> 2, lane = ref & soa4::TAG_MASK;
    const uint32 id = mDescentIds ? (ofs << 4) | (lane << 2) | tri
                                  : (ofs << 10) | (((ref >> soa4::LEAF_ENTRY_SHORT_SHIFT) & 15u) << 6) |
                                      (((ref >> soa4::LEAF_ENTRY_N_SHIFT) & 3u) << 4) | (lane << 2) | tri;
    return inCreator.PushID(id, mSubShapeBits).GetID();
  }
  JPH_INLINE void DecodeSubShapeID(const SubShapeID &inSubShapeID, soa4::LeafRef &outRef, uint32 &outTri) const
  {
    SubShapeID remainder;
    const uint32 id = inSubShapeID.PopID(mSubShapeBits, remainder);
    outTri = id & 3u;
    if (mDescentIds)
      outRef = LeafRefByDescent((id >> 4) << 2, (id >> 2) & 3u);
    else
      outRef = soa4::LEAF_ENTRY_FLAG | (((id >> 6) & 15u) << soa4::LEAF_ENTRY_SHORT_SHIFT) |
               (((id >> 4) & 3u) << soa4::LEAF_ENTRY_N_SHIFT) | ((id >> 10) << 2) | ((id >> 2) & 3u);
  }
  // Active-edge bits of triangle `tri` of a leaf (the tree's flags words).
  JPH_INLINE uint8 EdgeFlags(LeafHit &inLeaf, uint32 tri) const { return (inLeaf.Flags() >> (3 * tri)) & 7; }

  static void sCollideConvexVsBlas(const Shape *inShape1, const Shape *inShape2, Vec3Arg inScale1, Vec3Arg inScale2,
    Mat44Arg inCenterOfMassTransform1, Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator &inSubShapeIDCreator1,
    const SubShapeIDCreator &inSubShapeIDCreator2, const CollideShapeSettings &inCollideShapeSettings,
    CollideShapeCollector &ioCollector, const ShapeFilter &inShapeFilter);
  static void sCollideSphereVsBlas(const Shape *inShape1, const Shape *inShape2, Vec3Arg inScale1, Vec3Arg inScale2,
    Mat44Arg inCenterOfMassTransform1, Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator &inSubShapeIDCreator1,
    const SubShapeIDCreator &inSubShapeIDCreator2, const CollideShapeSettings &inCollideShapeSettings,
    CollideShapeCollector &ioCollector, const ShapeFilter &inShapeFilter);
  static void sCastConvexVsBlas(const ShapeCast &inShapeCast, const ShapeCastSettings &inShapeCastSettings, const Shape *inShape,
    Vec3Arg inScale, const ShapeFilter &inShapeFilter, Mat44Arg inCenterOfMassTransform2,
    const SubShapeIDCreator &inSubShapeIDCreator1, const SubShapeIDCreator &inSubShapeIDCreator2, CastShapeCollector &ioCollector);
  static void sCastSphereVsBlas(const ShapeCast &inShapeCast, const ShapeCastSettings &inShapeCastSettings, const Shape *inShape,
    Vec3Arg inScale, const ShapeFilter &inShapeFilter, Mat44Arg inCenterOfMassTransform2,
    const SubShapeIDCreator &inSubShapeIDCreator1, const SubShapeIDCreator &inSubShapeIDCreator2, CastShapeCollector &ioCollector);

  // the chunk (soa4::ChunkRef) cached in the walkers' fields; its owner keeps the bytes alive
  const uint8_t *mTree = nullptr;
  soa4::RootRef mRoot;
  vec3f mBmin = v_zero(), mInvScale = v_zero(); // box space -> node local
  uint32 mTriCount = 0, mSubShapeBits = 4;
  bool mDescentIds = false;
  AABox mLocalBounds;
};

JPH_NAMESPACE_END
