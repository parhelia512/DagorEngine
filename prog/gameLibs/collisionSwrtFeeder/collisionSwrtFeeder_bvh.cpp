// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <collisionGeometryFeeder/collisionGeometryFeeder.h>

#include <daBVH/dag_bvhBuild.h>
#include <daBVH/dag_bvhSerialization.h> // build_bvh::checkIfIsBox (the analytic-box shortcut)
#include <daBVH/dag_quadBLASBuilder.h>
#include <daBVH/dag_bvhReencode.h>
#include <daBVH/dag_swBLAS_leaf.h>        // RayData::unpackVert21 (chunk fast-path vert reencode)
#include <daBVH/dag_swBLAS_soa4Convert.h> // soa4::buildStackless (chunks store the SoA4 CPU layout)
#include <daSWRT/swBVH.h>
#include <daSWRT/swBLASBoxResemblance.h>
#include <generic/dag_tab.h>
#include <debug/dag_assert.h>
#include <debug/dag_log.h>
#include <gameRes/dag_collisionResource.h>
#include <math/dag_mathBase.h>
#include <vecmath/dag_vecMath.h>
#include <daBVH/swBLASLeafDefs.hlsli>


// Distance under which a BLAS this box-like renders as an analytic box (SWRT LOD knob).
// encoding must match the tree bytes: the splice hands an FP16-reencoded tree; the soup tail a
// still-uint16 writeDoubleQuadBLAS tree (addPreBuiltModel reencodes it internally).
static float scoreDimAsBoxDist(const uint8_t *tree, int tree_bytes, bbox3f_cref box, float dim_min, float dim_max,
  daSWRT::BlasBoxEncoding encoding)
{
  if (dim_max <= dim_min)
    return dim_max;
  const float boxLike = daSWRT::computeBlasBoxResemblanceVoxel(tree, 0, tree_bytes, box, encoding);
  return lerp(dim_max, dim_min, powf(boxLike, 1.5f));
}

// Fast path: collapsed content is one IDENT mesh node in the common case, and its per-node chunk
// is one SoA4 tree over one quantization frame -- the shape the SWRT BLAS wants. The byte math
// lives in buildSwrtChunkSpliceBLAS (collisionSwrtFeeder_splice.cpp, unit-tested); the score and
// the model registration happen here. Gated by the caller on fp16 verts and an IDENT live pose
// (chunk verts are node-local); a material filter carves whole leaves inside the splice.
static int buildSwrtBLAS_chunkFast(RenderSWRT &swrt, const CollisionResource &coll_res, int node_index, float dim_as_box_min,
  float dim_as_box_max, const CollisionGeometryFeeder::PhysMatFilter &mat_pred)
{
  daSWRT::BuiltBLAS built;
  CollisionGeometryFeeder::ChunkSpliceInfo info;
  if (!CollisionGeometryFeeder::buildSwrtChunkSpliceBLAS(coll_res, node_index, built, info, mat_pred))
    return -1;
  // The producer names its byte format and reports the carve, so neither the score decoder nor the
  // box rule can drift from the bytes. A carved model's box is the full quantization frame: 0 (the
  // far cap) keeps it from occluding through removed material; the soup path fits a tight box.
  built.dimAsBoxDist = info.carved ? 0.f
                                   : scoreDimAsBoxDist(built.data.data(), (int)built.treeBytes, built.box, dim_as_box_min,
                                       dim_as_box_max, info.boxEncoding);
  return swrt.addBuiltModel(eastl::move(built));
}

int CollisionGeometryFeeder::buildSwrtBLASFromCollisionResource(RenderSWRT &swrt, const CollisionResource &coll_res,
  const PhysMatFilter &node_filter, float dim_as_box_min, float dim_as_box_max, BuildSwrtBLASScratch &scratch)
{
  // Clear every caller-owned vector up front so both success and failure paths leave scratch in a
  // known-empty state; the header promises this contract.
  scratch.verts.clear();
  scratch.indices.clear();
  scratch.orderedVerts.clear();
  scratch.primBoxes.clear();

  const auto allNodes = coll_res.getAllNodes();

  // The game's physmat filter, asked per FACE (one collision node can carry several materials); an
  // empty filter keeps everything.
  auto faceMaterialPasses = [&node_filter](int phys_mat_id) { return !node_filter || node_filter(phys_mat_id); };

  // One pass classifies every node for the three consumers below (the single-node splice gate,
  // the reserve, the gather): eligibility is the trace's own early-out plus a well-formedness
  // guard (a node with indices but zero verts would desync the gather's vertex base), and the
  // node's material set answers "any face kept" (eligibility) and "all faces kept" (the splice
  // and the gather's unfiltered arm) in the same walk.
  struct EligibleNode
  {
    int nodeIndex, vertCount;
    bool allFacesKept;
  };
  dag::Vector<EligibleNode> eligible;
  eligible.reserve(allNodes.size());
  int totalVxCnt = 0, totalIdxCnt = 0;
  for (int ni = 0, ne = (int)allNodes.size(); ni < ne; ++ni)
  {
    const CollisionNode *node = coll_res.getNode(ni);
    if (!node || !node->checkBehaviorFlags(CollisionNode::TRACEABLE))
      continue;
    // a mirrored/singular live pose hides the node from CPU traces; SWRT must match
    if (!coll_res.getDefaultInstance().isNodeTraceable(node->nodeIndex))
      continue;
    if (node->type != COLLISION_NODE_TYPE_MESH && node->type != COLLISION_NODE_TYPE_CONVEX)
      continue;
    const int vertCount = coll_res.getNodeVertCount(ni);
    const int faceCount = (int)coll_res.getNodeFaceCount(ni);
    if (vertCount == 0 || faceCount == 0)
      continue;
    bool anyKept = !node_filter, allKept = true;
    if (node_filter)
      // The own-authority / material-less dispatch lives on the resource, one home with the
      // per-face enumeration the gather uses.
      coll_res.classifyNodeMaterials(*node, faceMaterialPasses, anyKept, allKept);
    if (!anyKept)
      continue;
    eligible.push_back(EligibleNode{ni, vertCount, allKept});
    totalVxCnt += vertCount;
    totalIdxCnt += faceCount * 3;
  }
  if (eligible.empty())
    return -1;

  // Single-node fast path (the common collapsed shape): splice the node's chunk instead of the
  // gather + SAH build. A material filter carves during the splice (a leaf holds one material,
  // so whole leaf records drop and emptied subtrees prune; boxes stay conservative). The soup
  // remains for multi-node resources, non-IDENT live poses (chunk verts are node-local) and the
  // 12 B float3 GPU vert format.
  // Eligibility already excluded chunkless nodes (zero faces).
  // Tiny nodes (a box is 8 verts) fall through to the soup so checkIfIsBox can still emit the
  // analytic box; their gather + SAH is trivial at that size.
  // An all-kept node passes no filter: a shortcut past the fold and the prune walks, not a scoring
  // rule (the splice reports the carve itself).
  if (swrt.blasVertsFp16 && eligible.size() == 1 && eligible[0].vertCount > 8 && coll_res.isIdentNode(eligible[0].nodeIndex))
    return buildSwrtBLAS_chunkFast(swrt, coll_res, eligible[0].nodeIndex, dim_as_box_min, dim_as_box_max,
      eligible[0].allFacesKept ? CollisionGeometryFeeder::PhysMatFilter{} : node_filter);

  scratch.verts.reserve(totalVxCnt);
  // Upper bound: a partially filtered node books its full face count; the fill emits only the
  // passing faces.
  scratch.indices.reserve(totalIdxCnt);

  uint32_t firstVertex = 0;
  for (const EligibleNode &en : eligible)
  {
    const int ni = en.nodeIndex;
    if (!coll_res.isIdentNode(ni))
    {
      mat44f nodeTm;
      v_mat44_make_from_43cu_unsafe(nodeTm, coll_res.getNodeTm(ni)[0]);
      coll_res.iterateNodeVerts(ni, [&](int, vec4f v) { v_st(&scratch.verts.push_back().x, v_mat44_mul_vec3p(nodeTm, v)); });
    }
    else
    {
      coll_res.iterateNodeVerts(ni, [&](int, vec4f v) { v_st(&scratch.verts.push_back().x, v); });
    }

    const uint32_t vertOffset = firstVertex;
    auto pushFace = [&](int, uint32_t i0, uint32_t i1, uint32_t i2) {
      scratch.indices.push_back(i0 + vertOffset);
      scratch.indices.push_back(i1 + vertOffset);
      scratch.indices.push_back(i2 + vertOffset);
    };
    if (en.allFacesKept)
      coll_res.iterateNodeFaces(ni, pushFace);
    else
      coll_res.iterateNodeFacesByMaterial(ni, faceMaterialPasses, pushFace);
    firstVertex += (uint32_t)en.vertCount;
  }

  // Box fast path on the raw gather, before the SAH reorder: a box resource is emitted as an analytic
  // box, so leafOrderVertexFetch's triangle-box + SAH-order work would be built only to be discarded.
  // checkIfIsBox is vertex-order independent and a genuine box gathers exactly its 8 verts, so the
  // verdict is identical pre- and post-reorder.
  {
    const bbox3f rawBox = build_bvh::calcBox((const vec4f *)scratch.verts.data(), (int)scratch.verts.size());
    if (build_bvh::checkIfIsBox(scratch.indices.data(), (int)scratch.indices.size(), (const vec4f *)scratch.verts.data(),
          (int)scratch.verts.size(), rawBox))
      return swrt.addBoxModel(rawBox.bmin, rawBox.bmax);
  }

  // SAH-leaf-order renumber + shared window-block over-spread dup (build_bvh, see dag_bvhBuild.h).
  // scratch.orderedVerts becomes the BLAS vertex array; scratch.indices is rewritten to index it.
  build_bvh::leafOrderVertexFetch(scratch.indices.data(), (unsigned)scratch.indices.size(), (const vec4f *)scratch.verts.data(),
    (unsigned)scratch.verts.size(), scratch.orderedVerts);

  const vec4f *vertsPtr = scratch.orderedVerts.data();
  const int vertCountTotal = (int)scratch.orderedVerts.size();
  const int idxCountTotal = (int)scratch.indices.size();
  bbox3f box = build_bvh::calcBox(vertsPtr, vertCountTotal);

  const int faceCount = idxCountTotal / 3;
  Tab<build_bvh::QuadPrim> prims;
  int quadCount = 0, singleCount = 0;
  build_bvh::buildQuadPrims(prims, quadCount, singleCount, scratch.indices.data(), faceCount, vertsPtr);
  // buildQuadPrims drops duplicate-index (zero-area) faces; if every gathered face is degenerate it
  // yields no prims. writeQuadBLAS would emit nothing and the treeBytes math below would underflow, so
  // report no model (same as the empty-input guard above).
  if (prims.empty())
    return -1;

  // Pair into double-quad leaves (4 tris/leaf). This soup BLAS feeds GPU RT only (no per-node
  // tri_ref / filtering), so pairing is unconstrained (vert_group = nullptr).
  dag::Vector<build_bvh::DoubleQuadPrim> dqPrims;
  build_bvh::buildDoubleQuadPrims(dqPrims, prims.data(), (int)prims.size(), vertsPtr);

  scratch.primBoxes.clear();
  scratch.primBoxes.resize(dqPrims.size());
  build_bvh::addDoubleQuadPrimitivesAABBList(scratch.primBoxes.data(), dqPrims.data(), (int)dqPrims.size(), vertsPtr);

  Tab<bbox3f> nodes;
  int maxDepth = 0;
  const int root = build_bvh::create_bvh_node_sah(nodes, scratch.primBoxes.data(), (uint32_t)dqPrims.size(), 4, maxDepth);

  dag::Vector<uint8_t> blasBytes;
  const bool builtBlas = build_bvh::writeDoubleQuadBLAS(blasBytes, box, nodes.data(), root, dqPrims.data(), (int)dqPrims.size(),
    reinterpret_cast<const uint8_t *>(vertsPtr), (int)sizeof(vec4f), vertCountTotal);
  if (!builtBlas) // vert span overflowed the unsigned 24-bit leaf base; drop this model's SWRT BLAS
    return -1;

  // Scored on the SWRT BuiltBLAS itself (a genuine box exited above at build_bvh::checkIfIsBox).
  const float dimAsBoxDist = scoreDimAsBoxDist(blasBytes.data(), (int)blasBytes.size() - vertCountTotal * 12, box, dim_as_box_min,
    dim_as_box_max, daSWRT::BlasBoxEncoding::Quantized16);

  return swrt.addPreBuiltModel(box, eastl::move(blasBytes), vertCountTotal, (int)nodes.size(), (int)dqPrims.size(), dimAsBoxDist);
}
