// Copyright (C) Gaijin Games KFT.  All rights reserved.

// The RenderSWRT-free half of the single-node chunk splice, so the collision unit suite can pin
// the byte math (frame box, conversion window, reencode) without linking the SWRT runtime; the
// feeder adds the box-resemblance score and registers the result.

#include <collisionGeometryFeeder/collisionGeometryFeeder.h>

#include <daBVH/dag_bvhReencode.h>
#include <daBVH/dag_swBLAS_leaf.h>        // RayData::unpackVert21 (vert reencode)
#include <daBVH/dag_swBLAS_soa4Convert.h> // soa4::buildStackless (chunks store the SoA4 CPU layout)
#include <daSWRT/swBVH.h>
#include <daSWRT/swBLASBoxResemblance.h> // BlasBoxEncoding (the producer names its own byte format)
#include <debug/dag_assert.h>
#include <debug/dag_log.h>
#include <gameRes/dag_collisionResource.h>
#include <vecmath/dag_vecMath.h>
#include <daBVH/swBLASLeafDefs.hlsli>

bool CollisionGeometryFeeder::buildSwrtChunkSpliceBLAS(const CollisionResource &coll_res, int node_index, daSWRT::BuiltBLAS &built,
  ChunkSpliceInfo &out_info, const PhysMatFilter &mat_pred)
{
  // This producer reencodes the tree boxes below, so IT names the score decoder.
  out_info.boxEncoding = daSWRT::BlasBoxEncoding::Fp16;
  // Resolved HERE so a node of another resource is unrepresentable (the chunk offsets index
  // coll_res's own storage).
  const CollisionNode *node = coll_res.getNode(node_index);
  // IDENT gate: the chunk bytes are bind-frame; splicing a posed node would upload a stale pose.
  if (!node || !coll_res.hasNodeBlas(node_index) || !coll_res.isIdentNode(node_index))
    return false;
  const CollisionResource::NodeOccluderBlas chunk = coll_res.getNodeOccluderBlas(*node);
  // Verts/tree were quantized on the chunk's own frame: [bmin, bmin + 65535 * invScale] is the
  // exact box the [0,65535] -> [-1,1] remap below maps onto, so the SWRT model box must be it.
  built.box.bmin = chunk.bmin;
  built.box.bmax = v_madd(chunk.invScale, v_splats(65535.f), chunk.bmin);
  // built.vertsFp16 stays at the BuiltBLAS default (true): the in-place reencode emits 8 B fp16
  // verts, and the feeder gates the splice on blasVertsFp16.

  const int vertCount = coll_res.getNodeVertCount(node_index);
  // The filter crosses the conversion as a keep mask over the 6-bit leaf user values (the OWNING
  // node's palette; one material per leaf): folded once, so the conversion's two passes agree.
  // Every value resolves through getNodePhysMatId, the one home of the past-palette rule (it
  // answers PHYSMAT_INVALID, as at trace time); each distinct id is asked once, PHYSMAT_INVALID included.
  static_assert(CollisionResource::MAX_NODE_PHYS_MATS <= 64, "the leaf user-value domain must fit the keep mask");
  uint64_t keepMask = ~0ull;
  if (mat_pred)
  {
    keepMask = 0;
    int invalidKept = -1; // PHYSMAT_INVALID's verdict, asked on first use
    for (uint32_t v = 0; v < CollisionResource::MAX_NODE_PHYS_MATS; ++v)
    {
      const int id = coll_res.getNodePhysMatId(node_index, (int)v);
      if (id == PHYSMAT_INVALID && invalidKept < 0)
        invalidKept = mat_pred((int16_t)PHYSMAT_INVALID) ? 1 : 0;
      const bool kept = id == PHYSMAT_INVALID ? invalidKept != 0 : mat_pred((int16_t)id);
      keepMask |= kept ? 1ull << v : 0ull;
    }
  }
  const soa4::StacklessResult rt =
    soa4::buildStackless(chunk.blasData, chunk.rootRef, (int)chunk.vertOffset, vertCount * 8, built.data, keepMask);
  if (!rt.valid())
  {
    if (rt.status == soa4::StacklessResult::Status::AllCarved)
      return false; // the filter carved every leaf: legitimately no SWRT model
    // Structurally impossible for a validly built chunk (the conversion round-trips 1:1); degrade
    // loudly to "no SWRT model for this resource", never a corrupt upload -- filter or not.
    logerr("swrtFeeder: SoA4->stackless conversion failed for the node chunk; no SWRT model");
    return false;
  }
  out_info.carved = rt.status == soa4::StacklessResult::Status::EmittedCarved;
  const int treeBytes = rt.treeBytes;
  const int vertsOfs = rt.vertsOfs; // == align8(treeBytes), the conversion's documented layout
  G_ASSERT(treeBytes > 0 && vertCount > 0);
  built.treeBytes = (uint32_t)treeBytes;

  uint8_t *dst = built.data.data();
  for (int ofs = 0; ofs < treeBytes;)
  {
    uint32_t encWord;
    memcpy(&encWord, dst + ofs + 12, sizeof(uint32_t)); // QUAD_LEAF_FLAG + quad/single encoding live at +12
    build_bvh::reencodeBoxNodeToFP16(dst + ofs);        // box uint16 [0,65535] -> FP16 [-1,1]; preserves +12
    ofs += (encWord & QUAD_LEAF_FLAG) ? BVH_BLAS_LEAF_SIZE : BVH_BLAS_NODE_SIZE;
  }

  // vert21 -> fp16 in place: unpack to box space, map f/32767.5 - 1 to [-1,1], repack. Same 8 B
  // slot, so the read completes (into a register) before the write -- no aliasing.
  const vec4f vertToNorm = v_splats(1.0f / 32767.5f);
  const vec4f vertBias = v_splats(-1.0f);
  uint8_t *verts = dst + vertsOfs;
  for (int v = 0; v < vertCount; ++v)
  {
    vec3f n = v_madd(RayData::unpackVert21(verts + (size_t)v * 8), vertToNorm, vertBias);
    build_bvh::writeGpuBlasVert(verts + (size_t)v * 8, n, /*fp16*/ true);
  }
  return true;
}
