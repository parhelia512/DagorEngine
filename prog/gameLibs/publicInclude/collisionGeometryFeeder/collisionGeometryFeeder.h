//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <EASTL/fixed_function.h>
#include <EASTL/vector.h>
#include <dag/dag_vector.h>
#include <math/dag_Point3.h>
#include <render/occlusion/parallelOcclusionRasterizer.h>
#include <vecmath/dag_vecMath.h>

class CollisionResource;
struct CollisionNode;
struct RenderSWRT;
namespace daSWRT
{
struct BuiltBLAS;
enum class BlasBoxEncoding : uint8_t;
} // namespace daSWRT

// Shared friend-access point for consuming CollisionResource mesh/convex node geometry
// from external rasterizer/BVH feeders. Static methods are split across gameLibs:
//   collisionOccluders     -- implements addRasterizationTasks
//   collisionSwrtFeeder    -- implements buildSwrtBLASFromCollisionResource
class CollisionGeometryFeeder
{
public:
  // Skips nodes with any PhysMat::lightTransparent material (glass must not occlude).
  static void addRasterizationTasks(const CollisionResource &coll_res, mat44f_cref worldviewproj,
    eastl::vector<ParallelOcclusionRasterizer::RasterizationTaskData> &out_tasks, uint32_t triangles_partition, bool allow_convex);

  // node_filter: optional predicate on a phys_mat_id, routed through the resource's material-filtered
  // face enumeration -- so its granularity is that pair's: all-or-nothing per node for a
  // single-material node, per face for a fused node.
  //              When unbound, every TRACEABLE mesh/convex node with non-empty indices is emitted.
  using PhysMatFilter = eastl::fixed_function<sizeof(void *) * 2, bool(int16_t phys_mat_id)>;

  // Xray GPU upload helper. Invokes cb with raw pointers valid only during the call.
  // In future BVH impl, verts_ptr may reference transient decoded scratch.
  using NodeMeshConsumer = eastl::fixed_function<sizeof(void *) * 4,
    void(const void *verts_ptr, int vert_count, int vert_elem_size, const void *indices_ptr, int index_count, int index_elem_size)>;
  static void withNodeMeshData(const CollisionResource &coll_res, int node_id, const NodeMeshConsumer &cb);

  // What the chunk splice says about the bytes it produced (filled on success).
  struct ChunkSpliceInfo
  {
    daSWRT::BlasBoxEncoding boxEncoding{}; // the byte format of built's tree boxes: the producer names it
    // The filter dropped at least one leaf. The box stays the full quantization frame (the verts
    // decode against it), so a carved model must never stand in as its box: the caller gives it
    // dimAsBoxDist = 0 (the far cap), not a box-resemblance score.
    bool carved = false;
  };
  // The RenderSWRT-free half of the single-node chunk splice: fills built (box = the chunk's own
  // quantization frame, data = stackless tree + fp16 verts at align8(treeBytes)) and out_info. The
  // caller scores dimAsBoxDist (far cap when out_info.carved) and registers the result. Split out
  // so the collision unit suite pins the byte math without the SWRT runtime.
  // Precondition (checked inside): the node is IDENT -- the bytes are bind-frame chunk data, so a
  // posed node would splice a stale pose. The caller still owns the fp16 requirement.
  // mat_pred (the same PhysMatFilter shape as the gather; unbound = keep everything) carves the
  // model: a chunk leaf holds ONE material, so the filter drops whole leaf records and prunes
  // emptied subtrees; an all-carved model answers false (no model). The predicate is asked up
  // front (folded into a keep mask), never during the conversion: once per palette material, and
  // once for PHYSMAT_INVALID when any of the 64 leaf values resolves to it (a palette member, or
  // every value past the palette; the trace-time gate reads the same). A material-less node is
  // decided by that one answer.
  static bool buildSwrtChunkSpliceBLAS(const CollisionResource &coll_res, int node_index, daSWRT::BuiltBLAS &built,
    ChunkSpliceInfo &out_info, const PhysMatFilter &mat_pred = {});

  // Caller-owned scratch for buildSwrtBLASFromCollisionResource. Reuse across calls to avoid
  // reallocation. All vectors are cleared at the start of each build; bring your own thread-local
  // instance if running builds in parallel.
  // TODO: drop this (locals suffice) -- the single-node chunk splice serves most models, so the
  // soup path that needed the reuse runs too rarely to justify the API surface.
  struct BuildSwrtBLASScratch
  {
    dag::Vector<Point3_vec4> verts;
    dag::Vector<uint32_t> indices;   // uint32 to support CollisionResources with > 65536 verts
    dag::Vector<vec4f> orderedVerts; // leafOrderVertexFetch output: SAH-leaf-ordered + window-dedup'd BLAS verts
    dag::Vector<bbox3f> primBoxes;
  };

  // Build a SWRT BLAS directly from a CollisionResource and hand it to swrt via addPreBuiltModel
  // (or addBoxModel if the flattened mesh collapses to a pure box).
  // Avoids the flat-indices+verts round-trip through RenderSWRT::addModel by running the daBVH
  // SAH builder inside this function and moving the resulting tree/prims into SWRT.
  // Returns the SWRT model id (>= 0) on success, -1 on empty/invalid input. Re-entrant for the
  // BLAS build phase (scratch is caller-owned); the final hand-off to swrt (either addBoxModel or
  // addPreBuiltModel) mutates shared RenderSWRT state and is not thread-safe, so concurrent
  // callers must serialize that step.
  // dim_as_box_min/max: distance range (metres) for the runtime "treat-as-box" shadow
  // optimization. The actual per-BLAS distance is interpolated from a voxel-rasterized box-
  // resemblance score: a near-box mesh gets `dim_as_box_min` (aggressive: collapses to AABB
  // even up close), a non-box mesh gets `dim_as_box_max` (conservative: only collapses far
  // away). Pass max==min to disable scoring and pin every mesh to a single distance. Pure
  // axis-aligned boxes (checkIfIsBox) bypass this entirely via addBoxModel.
  static int buildSwrtBLASFromCollisionResource(RenderSWRT &swrt, const CollisionResource &coll_res, const PhysMatFilter &node_filter,
    float dim_as_box_min, float dim_as_box_max, BuildSwrtBLASScratch &scratch);
};
