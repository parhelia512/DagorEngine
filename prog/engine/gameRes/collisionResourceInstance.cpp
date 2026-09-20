// Copyright (C) Gaijin Games KFT.  All rights reserved.

// CollisionResourceInstance method definitions; the shared file-local helpers live in
// collisionGameResInternal.h.

#include <gameRes/dag_collisionResource.h>
#include <math/dag_geomTree.h>
#include <math/dag_mathUtils.h>
#include <math/dag_math3d.h>
#include <osApiWrappers/dag_atomic.h>
#include <osApiWrappers/dag_miscApi.h>
#include <debug/dag_debug.h>
#include <daBVH/dag_swBLAS_soa4.h>
#include "collisionGameResInternal.h"

const CollisionResourceInstance::PoseMeta CollisionResourceInstance::PoseMeta::absent_slot = {
  1.f, uint8_t(CollisionNode::IDENT | CollisionNode::ORTHONORMALIZED), uint8_t(0), uint16_t(0)};

bool CollisionResourceInstance::isDefault() const { return res && this == &res->defaultInstance; }

BBox3 CollisionResourceInstance::getNodeResourceBBox(int node_index) const
{
  if (!res)
    return BBox3();
  const CollisionNode *n = res->getNode(node_index);
  if (!n)
    return BBox3(); // out-of-range or absent: empty, the checked-accessor convention
  // Untraceable (NaN/singular) poses report empty by contract: the pose is unrealizable.
  if (!isNodeTraceable(node_index))
    return BBox3();
  // Nodes with no traceable geometry report empty: the exporter's zero-vert r < 0 marker (any node
  // type; report empty, as getNodeBSphere does), a degenerate-dropped mesh/convex whose stale
  // modelBBox has no geometry behind it (the recomputeRootBBox predicate), and POINTS -- never a
  // trace target, and its stored bbox is already resource-space, so composing would double-apply.
  if (n->radiusAroundBoxCenter < 0 || n->type == COLLISION_NODE_TYPE_POINTS ||
      ((n->type == COLLISION_NODE_TYPE_MESH || n->type == COLLISION_NODE_TYPE_CONVEX) && !n->hasGeometry()))
    return BBox3();
  if (n->type == COLLISION_NODE_TYPE_SPHERE)
  {
    TMatrix tm;
    v_mat_43cu_from_mat44(tm.array, getNodeGeometryTm(node_index));
    // Analytic AABB matches the dispatch's reach; corner-mapping a sphere inflates under rotation.
    return composed_sphere_box(tm, n->bsphereCenter(), n->radiusAroundBoxCenter);
  }
  bbox3f box;
  v_bbox3_init(box, getNodeGeometryTm(node_index), res->getNodeGeometryBBox(*n));
  BBox3 out;
  v_stu_bbox3(out, box);
  return out;
}

DAGOR_NOINLINE const TMatrix &CollisionResourceInstance::missingOwnedPose(int node_index) const
{
  G_ASSERTF(false, "collres instance %p of res %p: owned-pose accessor on node %d %s", this, res, node_index,
    tree ? "of a tree-backed instance" : "is out of range");
  LOGERR_ONCE("collres instance %p of res %p: owned-pose accessor on node %d %s; bind identity is used", this, res, node_index,
    tree ? "of a tree-backed instance" : "is out of range");
  return TMatrix::IDENT;
}

mat44f CollisionResourceInstance::getNodeGeometryTm(int node_index) const
{
  mat44f posed;
  if (DAGOR_UNLIKELY((uint32_t)node_index >= nodeTm.size()))
  {
    v_mat44_make_from_43cu_unsafe(posed, missingOwnedPose(node_index).array);
    return posed;
  }
  v_mat44_make_from_43cu_unsafe(posed, nodeTm[node_index].array);
  return res->geometryTmFromPosed(node_index, posed, poseMeta[node_index]);
}

// An epsilon-IDENT mesh may be traced in either frame (the chunk walk keeps stored geometry
// while the scalar path composes T), so every derived bound must enclose the raw box too.
static inline bool eps_ident_needs_raw_box(const CollisionNode &node, const CollisionResourceInstance::PoseMeta &pm)
{
  return node.type == COLLISION_NODE_TYPE_MESH && (pm.flags & CollisionNode::IDENT);
}
static inline void add_eps_ident_raw_box(bbox3f &box, const CollisionNode &node, const CollisionResourceInstance::PoseMeta &pm,
  bbox3f_cref raw_box)
{
  if (eps_ident_needs_raw_box(node, pm))
    v_bbox3_add_box(box, raw_box);
}

// An exact-identity frame keeps the stored box verbatim: v_bbox3_init reconstructs through
// absolute center/half-extent and can collapse a one-ULP-wide far-origin box.
static bbox3f composed_or_raw_box(mat44f_cref gtm, bbox3f_cref raw_box)
{
  if (collres_is_exact_identity_43(gtm))
    return raw_box;
  bbox3f out;
  v_bbox3_init(out, gtm, raw_box);
  return out;
}

void CollisionResourceInstance::mirrorLiveFlagsIfStale() const
{
  if (isDefault()) // the default IS the source; also the hot path of the default-form queries
    return;
  const uint32_t cur = interlocked_acquire_load(res->liveFlagsGen);
  if (DAGOR_LIKELY(interlocked_acquire_load(liveFlagsGenSeen) == cur))
    return;
  while (DAGOR_UNLIKELY(interlocked_compare_exchange(refreshLock, 1, 0) != 0))
    cpu_yield();
  if (liveFlagsGenSeen != cur)
  {
    const dag::Vector<PoseMeta> &def = res->defaultInstance.poseMeta;
    for (uint32_t i = 0, e = min<uint32_t>(def.size(), poseMeta.size()); i < e; ++i)
      poseMeta[i].behaviorFlags = def[i].behaviorFlags;
    interlocked_release_store(liveFlagsGenSeen, cur);
  }
  interlocked_release_store(refreshLock, 0);
}

void CollisionResourceInstance::refreshIfStale(mat44f_cref entity_tm) const
{
  // The tree generation is never 0, so 0 stays "never refreshed". The default-pose stamp
  // catches unbound-node writes, which move matrices without touching the tree generation;
  // its acquire pairs with the refresh's release so a default-pose-only re-derive (the tree
  // stamp re-publishes an unchanged value) still orders the derived-state reads.
  // Derived state anchors to the refreshing trace's entity basis: callers keep that basis
  // stable within a pose generation (re-orienting an entity re-poses its tree).
  if (DAGOR_UNLIKELY(interlocked_acquire_load(poseGeneration) != tree->getPoseGeneration() ||
                     interlocked_acquire_load(defaultPoseGenAtRefresh) != res->defaultPoseGen))
    refreshFromTree(entity_tm);
  mirrorLiveFlagsIfStale();
}

DAGOR_NOINLINE void CollisionResourceInstance::refreshFromTree(mat44f_cref entity_tm) const
{
  // Once-per-generation rebuild; concurrent traces of the same instance wait here.
  while (DAGOR_UNLIKELY(interlocked_compare_exchange(refreshLock, 1, 0) != 0))
    cpu_yield();
  const uint32_t cur = tree->getPoseGeneration();
  const uint32_t curDef = res->defaultPoseGen;
  if (interlocked_acquire_load(poseGeneration) != cur || defaultPoseGenAtRefresh != curDef)
  {
    if (DAGOR_UNLIKELY((uint32_t)tree->nodeCount() != treeNodeCountAtBind))
    {
      // Layout drifted under a live binding: disable the posed reject rather than trace stale bounds.
      v_bbox3_init(rootBBox, v_make_vec4f(-FLT_MAX / 4, -FLT_MAX / 4, -FLT_MAX / 4, 0));
      v_bbox3_add_pt(rootBBox, v_make_vec4f(FLT_MAX / 4, FLT_MAX / 4, FLT_MAX / 4, 0));
      hasBsphereCenterLocal = false;
      interlocked_release_store(defaultPoseGenAtRefresh, curDef);
      // Pre-drift leaf boxes would make the prefilter skip nodes the new layout reaches; the
      // release below publishes the clear to lock-skipping readers.
      tlas.data.clear();
      interlocked_release_store(tlasPoseGeneration, 0u);
      interlocked_release_store(poseGeneration, cur);
      const bool firstDrift = !layoutDriftAsserted; // latched: one assert per binding, not per generation
      layoutDriftAsserted = true;
      interlocked_release_store(refreshLock, 0);
      // Diagnostics AFTER the release: an assert stop or log emit under the spinlock would
      // stall every concurrent trace of this instance.
      if (DAGOR_UNLIKELY(firstDrift))
      {
        G_ASSERTF(false, "collres instance %p of res %p: bound tree layout changed (%d nodes vs %d); rebind the instance", this, res,
          (int)tree->nodeCount(), (int)treeNodeCountAtBind);
        LOGERR_ONCE("collres instance %p of res %p: bound tree layout changed (%d nodes vs %d)", this, res, (int)tree->nodeCount(),
          (int)treeNodeCountAtBind);
      }
      return;
    }
    // Compose translation entity-relative to preserve large-world precision.
    const mat44f entityRot = {entity_tm.col0, entity_tm.col1, entity_tm.col2, v_zero()};
    mat44f invEntity;
    v_mat44_inverse43(invEntity, entityRot);
    const vec3f relOfs = v_sub(tree->getWtmOfs(), entity_tm.col3);
    hasBsphereCenterLocal = res->bsphereCenterNode && res->bsphereCenterNode.index() < tree->nodeCount();
    if (hasBsphereCenterLocal)
    {
      mat44f centerTm = tree->getNodeWtmRel(res->bsphereCenterNode);
      centerTm.col3 = v_add(centerTm.col3, relOfs);
      bsphereCenterLocal = v_mat44_mul_vec3p(invEntity, centerTm.col3);
    }
    // Diagnostics collected under the lock, emitted after the release (an assert stop or a
    // log emit under the spinlock stalls every concurrent trace of this instance).
    int canaryNode = -1, hiddenNode = -1;
    float canaryErr = 0.f;
    G_UNUSED(canaryNode);
    G_UNUSED(canaryErr);
    // No per-node classification of scale here: bind meta stays valid under the no-scale
    // contract (checked by the dev canary in treePosedNodeTm); the refresh re-derives only
    // what live state can change: the driven det gate and the unbound meta.
    // The TLAS refit deliberately does NOT run here: refreshTlasFromTree rebuilds it lazily for
    // the first candidate-eligible query, so every other first-touch trace stays lean.
    // The entity's own handedness must cancel out of the driven gate: comparing SIGNS (not a
    // product, whose det(invEntity)^2 factor underflows at extreme entity scales) leaves only
    // the wtm * relTm sign deciding; det != 0 keeps a singular driven wtm hidden.
    const float invEntityDet = v_extract_x(v_dot3_x(invEntity.col0, v_cross3(invEntity.col1, invEntity.col2)));
    bbox3f box;
    v_bbox3_init_empty(box);
    dag::ConstSpan<CollisionNode> nodes = res->getAllNodes();
    for (uint32_t i = 0, e = min<uint32_t>(nodes.size(), (uint32_t)poseMeta.size()); i < e; ++i)
    {
      const CollisionNode &node = nodes[i];
      PoseMeta &pm = poseMeta[i];
      const mat44f posed = treePosedNodeTm(i, invEntity, relOfs, canaryNode, canaryErr);
      if (node.geomNodeId.index() < tree->nodeCount())
      {
        // A mirrored or degenerate driven wtm breaks the no-scale contract: hide the node
        // like the owned form hides a mirrored pose; it returns when the wtm recovers.
        // All four columns, tested SEPARATELY: a NaN translation passes a basis-only det
        // gate, and a max-abs chain eats a basis NaN (v_max lets the other operand win).
        // Normalized det floor, not a raw det != 0: a finite near-singular wtm would pass the
        // raw compare and the trace would invert an ill-conditioned matrix unguarded (the
        // owned form hides the same class through the identical floor).
        float ndet;
        const bool finitePose = v_test_xyz_finite(posed.col0) && v_test_xyz_finite(posed.col1) && v_test_xyz_finite(posed.col2) &&
                                v_test_xyz_finite(posed.col3);
        const bool ok = // a NaN basis is excluded by finitePose before the floor and sign compare
          pm.isBindTraceable() && finitePose && CollisionResource::relativeDetAboveFloor(posed, ndet) &&
          (ndet < 0.f) == (invEntityDet < 0.f);
        if (DAGOR_UNLIKELY(!ok && pm.isBindTraceable()) && hiddenNode < 0)
          hiddenNode = (int)i; // warned after the refresh releases the spinlock
        pm.setTraceable(ok);
      }
      else
      {
        // Unbound nodes read the default pose live, so its reclassification must land here
        // too; a structural hide stays this instance's own.
        PoseMeta dm = res->defaultInstance.poseMeta[i];
        dm.setDisabled(pm.isDisabled());
        pm = dm;
      }
      // hidden or unrealizable poses never trace, so they must not bound
      if (pm.isDisabled() || !pm.isTraceable())
        continue;
      if (node.type == COLLISION_NODE_TYPE_POINTS)
        continue; // never a trace target
      if (node.radiusAroundBoxCenter < 0.f)
        continue; // zero-vert marker: corner-mapping the inverted box would union a phantom
      if ((node.type == COLLISION_NODE_TYPE_MESH || node.type == COLLISION_NODE_TYPE_CONVEX) && !node.hasGeometry())
        continue;
      const mat44f gtm = res->geometryTmFromPosed((int)i, posed, pm);
      bbox3f nodeBox = res->getNodeGeometryBBox(node);
      bbox3f composedBox = composed_or_raw_box(gtm, nodeBox);
      // NaN never joins and Inf saturates, like every other rootBBox writer: a poisoned box
      // would false-reject every trace of the generation.
      join_saturated_finite(box, composedBox);
      // same predicate as the leaf encoders; the join keeps this arm's poison guard
      if (eps_ident_needs_raw_box(node, pm))
        join_saturated_finite(box, nodeBox);
    }
    rootBBox = box;
    // The default stamp is the release publication a default-pose-only re-derive pairs with
    // (poseGeneration may re-publish an unchanged value and give the reader no edge).
    interlocked_release_store(defaultPoseGenAtRefresh, curDef);
    interlocked_release_store(poseGeneration, cur);
    interlocked_release_store(refreshLock, 0);
#if DAGOR_DBGLEVEL > 0 // the canary collects only in dev; release must not carry an always-false compare
    if (DAGOR_UNLIKELY(canaryNode >= 0))
    {
      G_ASSERTF(false,
        "collres instance %p of res %p: scaled/sheared driven wtm for node %d (err %g) breaks the no-scale "
        "pose contract",
        this, res, canaryNode, canaryErr);
      LOGERR_ONCE("collres instance %p of res %p: scaled/sheared driven wtm for node %d (err %g)", this, res, canaryNode, canaryErr);
    }
#endif
    if (DAGOR_UNLIKELY(hiddenNode >= 0))
      LOGWARN_ONCE("collres instance %p of res %p: mirrored/degenerate driven wtm hides node %d", this, res, hiddenNode);
    return;
  }
  interlocked_release_store(refreshLock, 0);
}

mat44f CollisionResourceInstance::treePosedNodeTm(uint32_t i, mat44f_cref inv_entity, vec3f rel_ofs, int &canary_node,
  float &canary_err) const
{
  const CollisionNode &node = res->getAllNodes()[i];
  auto gnId = node.geomNodeId.index() < tree->nodeCount() ? node.geomNodeId : dag::Index16();
  if (!gnId)
    return res->defaultInstance.getNodeTm((int)i);
  mat44f m = tree->getNodeWtmRel(gnId);
#if DAGOR_DBGLEVEL > 0
  // No-scale contract canary on the raw wtm basis, before the entity composition. 2e-3 admits
  // chained-bone rounding (~1e-5) with headroom; release enforces nothing.
  const float maxErr = collres_mat33_ortho_dev(m);
  if (DAGOR_UNLIKELY(maxErr > 2e-3f) && canary_node < 0)
  {
    canary_node = (int)i; // both refreshers run under refreshLock: emitted after their release
    canary_err = maxErr;
  }
#else
  G_UNUSED(canary_node);
  G_UNUSED(canary_err);
#endif
  m.col3 = v_add(m.col3, rel_ofs);
  v_mat44_mul43(m, inv_entity, m);
  // Missing rel-tm slots on appended nodes mean identity.
  if ((res->collisionFlags & COLLISION_RES_FLAG_HAS_REL_GEOM_NODE_ID) && i < (uint32_t)res->data->relGeomNodeTms().size())
  {
    mat44f relGeomNodeTm;
    v_mat44_make_from_43cu_unsafe(relGeomNodeTm, res->data->relGeomNodeTms()[i].array);
    v_mat44_mul43(m, m, relGeomNodeTm);
  }
  return m;
}

void CollisionResourceInstance::refreshTlasIfStale(mat44f_cref entity_tm) const
{
  if (!tree || !res || !res->hasAllNodesTLAS())
    return;
  // Acquire pairs with the refit's release: a trace that skips the lock on equal stamps sees
  // the fully refit clone and frame. A blueprint-only rebuild leaves the pose stamp unchanged,
  // so the blueprint stamp carries its own publication edge. The resource stamps read plain:
  // rebuilds and default-pose writes are serialized against traces.
  if (DAGOR_UNLIKELY(interlocked_acquire_load(tlasPoseGeneration) != interlocked_acquire_load(poseGeneration) ||
                     interlocked_acquire_load(tlasBlueprintGenSeen) != res->data->tlasGeneration ||
                     interlocked_acquire_load(tlasDefaultPoseGenSeen) != res->defaultPoseGen))
    refreshTlasFromTree(entity_tm);
}

DAGOR_NOINLINE void CollisionResourceInstance::refreshTlasFromTree(mat44f_cref entity_tm) const
{
  // Serialized like the pose refresh; only candidate-eligible traces of a stale generation enter.
  while (DAGOR_UNLIKELY(interlocked_compare_exchange(refreshLock, 1, 0) != 0))
    cpu_yield();
  const uint32_t cur = interlocked_acquire_load(poseGeneration);
  const uint32_t curDef = res->defaultPoseGen; // unbound leaves compose from the default pose
  const auto publishStamps = [&] {
    interlocked_release_store(tlasDefaultPoseGenSeen, curDef);
    interlocked_release_store(tlasBlueprintGenSeen, res->data->tlasGeneration);
    interlocked_release_store(tlasPoseGeneration, cur);
  };
  if (tlasPoseGeneration != cur || tlasBlueprintGenSeen != res->data->tlasGeneration || tlasDefaultPoseGenSeen != curDef)
  {
    // A drift landing between one trace's pose refresh and this refit would otherwise stamp
    // the pre-drift clone fresh (declineTlas's comment covers why the clear keeps the bytes).
    if (DAGOR_UNLIKELY((uint32_t)tree->nodeCount() != treeNodeCountAtBind))
    {
      tlas.data.clear();
      publishStamps();
      interlocked_release_store(refreshLock, 0);
      return;
    }
    const mat44f entityRot = {entity_tm.col0, entity_tm.col1, entity_tm.col2, v_zero()};
    mat44f invEntity;
    v_mat44_inverse43(invEntity, entityRot);
    const vec3f relOfs = v_sub(tree->getWtmOfs(), entity_tm.col3);
    dag::ConstSpan<CollisionNode> nodes = res->getAllNodes();
    const dag::ConstSpan<uint16_t> leafNodes = res->tlasLeafNodes();
    // Leaf-indexed compose; an entry left empty contributes no leaf.
    dag::Vector<bbox3f, framemem_allocator> leafBoxes(leafNodes.size());
    // Refresh-wide scale floor and conditioning max for the refit budget: only this loop sees
    // the composed frames, which are not guaranteed R+T.
    float treeScale = 0.f, treeAmpl = 1.f;
    int canaryNode = -1;
    float canaryErr = 0.f;
    G_UNUSED(canaryErr);
    for (uint32_t k = 0, e = (uint32_t)leafNodes.size(); k < e; ++k)
    {
      v_bbox3_init_empty(leafBoxes[k]);
      const uint32_t i = leafNodes[k];
      if (i >= (uint32_t)poseMeta.size() || poseMeta[i].isDisabled() || !poseMeta[i].isTraceable())
        continue;
      const CollisionNode &node = nodes[i];
      const mat44f posed = treePosedNodeTm(i, invEntity, relOfs, canaryNode, canaryErr);
      const mat44f gtm = res->geometryTmFromPosed((int)i, posed, poseMeta[i]);
      treeScale = max(treeScale, mat33_spectral_norm(gtm));
      treeAmpl = max(treeAmpl, collres_tlas_tm_conditioning(gtm));
      const bbox3f rawBox = res->getNodeGeometryBBox(node);
      leafBoxes[k] = composed_or_raw_box(gtm, rawBox);
      add_eps_ident_raw_box(leafBoxes[k], node, poseMeta[i], rawBox);
    }
    refitAllTlasLeaves(leafBoxes.data(), treeScale, treeAmpl);
    tlasEntityBasisSeen = {entity_tm.col0, entity_tm.col1, entity_tm.col2, entity_tm.col3};
    publishStamps();
    interlocked_release_store(refreshLock, 0);
    // No canary emit here: the pose refresh walks a superset of these nodes and reported this
    // generation's violations already (it always runs first - see refreshTlasIfStale).
    G_UNUSED(canaryNode);
    return;
  }
  interlocked_release_store(refreshLock, 0);
}

// A leafless node the current pose makes traceable has no place in the cloned topology, so
// candidate mode would never visit it; both the pose write and the full refit check this.
static inline bool tlas_leafless_recovery(const CollisionNode &n, const CollisionResourceInstance::PoseMeta &pm, float bsph_r)
{
  return !pm.isDisabled() && pm.isTraceable() && collres_tlas_recovery_capable(n, bsph_r);
}

void CollisionResourceInstance::declineTlas() const
{
  tlas.data.clear();
  tlasDeclined = true;
  tlasDeclinedGen = res->data->tlasGeneration;
}

bool CollisionResourceInstance::ownedPosedNodeBox(int node_index, bbox3f &out_box, bbox3f *out_raw_box) const
{
  if ((uint32_t)node_index >= poseMeta.size() || (uint32_t)node_index >= nodeTm.size())
    return false;
  const PoseMeta &pm = poseMeta[node_index];
  // hidden or unrealizable poses never trace, so they must not bound
  if (pm.isDisabled() || !pm.isTraceable())
    return false;
  const CollisionNode &node = res->getAllNodes()[node_index];
  if (node.type == COLLISION_NODE_TYPE_POINTS)
    return false;
  if ((node.type == COLLISION_NODE_TYPE_MESH || node.type == COLLISION_NODE_TYPE_CONVEX) && !node.hasGeometry())
    return false;
  mat44f posed;
  v_mat44_make_from_43cu_unsafe(posed, nodeTm[node_index].array);
  const bbox3f rawBox = res->getNodeGeometryBBox(node);
  if (out_raw_box)
    *out_raw_box = rawBox;
  out_box = composed_or_raw_box(res->geometryTmFromPosed(node_index, posed, pm), rawBox);
  add_eps_ident_raw_box(out_box, node, pm, rawBox);
  return true;
}

// An empty lane (min 65535 / max 0) is neutral under this union but NOT culled by the slab
// test, so a hidden node still reaches the passes: their traceability checks are load-bearing.
static inline void tlas_node_union(const uint16_t *a, int n, uint16_t mn[3], uint16_t mx[3])
{
  for (int k = 0; k < 3; ++k)
  {
    uint16_t lo = 65535, hi = 0;
    for (int l = 0; l < n; ++l)
    {
      const uint16_t v0 = a[k * n + l], v1 = a[(3 + k) * n + l];
      lo = v0 < lo ? v0 : lo;
      hi = v1 > hi ? v1 : hi;
    }
    mn[k] = lo;
    mx[k] = hi;
  }
}

void CollisionResourceInstance::encodeTlasLeafBox(int node_index, uint32_t leaf_loc, const bbox3f *box) const
{
  const uint32_t ni = leaf_loc >> 2;
  const int lane = (int)(leaf_loc & 3u);
  uint16_t *a = (uint16_t *)(tlas.data.data() + res->tlasNodeOfs()[ni]);
  bbox3f nodeBox;
  if (box)
    nodeBox = *box;
  else if (!ownedPosedNodeBox(node_index, nodeBox))
    v_bbox3_init_empty(nodeBox);
  uint16_t mn[3], mx[3];
  if (v_bbox3_is_empty(nodeBox))
  {
    mn[0] = mn[1] = mn[2] = 65535;
    mx[0] = mx[1] = mx[2] = 0;
  }
  else
    collres_tlas_quant_lane(nodeBox, tlas.scale, tlas.origin, tlas.fpErr, mn, mx);
  soa4::storeLaneBoxU16((uint8_t *)a, CollisionResource::tlasChildCountOf(res->tlasNodeParentPacked()[ni]), lane, mn, mx);
}

void CollisionResourceInstance::refitAllTlasLeaves(const bbox3f *posed_boxes, float posed_scale, float posed_ampl,
  float frame_headroom) const
{
  if (!res || !res->hasAllNodesTLAS())
    return;
  if (!posed_boxes && nodeTm.empty())
    return; // owned-form refit has no poses to read
  // The clone materializes only AFTER the envelope gate passes: a declined pose must not
  // reallocate and copy the blueprint just to decline and free it again.
  // The frame derives from the FRESH leaf union, not the grow-only rootBBox: a rootBBox frame
  // would stay coarse forever after a distant excursion returns.
  dag::ConstSpan<CollisionNode> nodes = res->getAllNodes();

  // Leafless recovery declines this instance (see tlas_leafless_recovery); the precomputed
  // watchlist keeps the scan per leaf, never per node.
  for (uint16_t wi : res->tlasWatchNodes())
    if (DAGOR_UNLIKELY(wi < (uint32_t)poseMeta.size() && !poseMeta[wi].isDisabled() && poseMeta[wi].isTraceable()))
    {
      declineTlas();
      return;
    }

  const dag::ConstSpan<uint16_t> leafNodes = res->tlasLeafNodes();
  // Leaf-indexed throughout; only the owned path composes its scratch here.
  dag::Vector<bbox3f, framemem_allocator> ownedBoxes(posed_boxes ? 0 : leafNodes.size());
  bbox3f leafUnion;
  v_bbox3_init_empty(leafUnion);
  // Budget from pre-compose RAW operands (products cancel in composed AABBs), over
  // contributing leaves only (an untraceable pose can carry NaN and its box stays empty).
  float maxNodeOp = 0.f, amplMax = posed_boxes ? posed_ampl : 1.f;
  for (uint32_t k = 0, e = (uint32_t)leafNodes.size(); k < e; ++k)
  {
    const uint32_t i = leafNodes[k];
    bbox3f rawBox;
    v_bbox3_init_empty(rawBox); // MSVC C4701: the empty-bi continue guards the unwritten arm, but flow analysis cannot see it
    if (!posed_boxes)
    {
      v_bbox3_init_empty(ownedBoxes[k]);
      if (!ownedPosedNodeBox((int)i, ownedBoxes[k], &rawBox))
        v_bbox3_init_empty(ownedBoxes[k]);
    }
    const bbox3f &bi = posed_boxes ? posed_boxes[k] : ownedBoxes[k];
    if (v_bbox3_is_empty(bi))
      continue;
    v_bbox3_add_box(leafUnion, bi);
    if (posed_boxes)
      rawBox = res->getNodeGeometryBBox(nodes[i]);
    const float rawMag = v_extract_x(v_hmax3(v_max(v_abs(rawBox.bmin), v_abs(rawBox.bmax))));
    const float boxMag = v_extract_x(v_hmax3(v_max(v_abs(bi.bmin), v_abs(bi.bmax))));
    float scale = i < (uint32_t)poseMeta.size() ? poseMeta[i].maxTmScale : 1.f;
    // The node test inverts the GEOMETRY matrix, so its conditioning amplifies the budget until
    // the envelope declines; per node here, the tree path supplied refresh-wide maxima.
    float ampl = posed_ampl;
    if (!posed_boxes)
    {
      mat44f pt;
      v_mat44_make_from_43cu_unsafe(pt, nodeTm[i].array);
      ampl = collres_tlas_tm_conditioning(res->geometryTmFromPosed((int)i, pt, poseMeta[i]));
    }
    else
      scale = max(scale, posed_scale);
    amplMax = max(amplMax, ampl);
    maxNodeOp = max(maxNodeOp, collres_tlas_node_op(scale, rawMag, boxMag, ampl));
  }
  const bool allHidden = v_bbox3_is_empty(leafUnion);
  // All-hidden with the latch, or with no CURRENT clone: stay clone-less - no frame derives
  // from an empty union, and tree bytes beside a stale frame would mis-encode the next leaf.
  if (allHidden && ((tlasDeclined && tlasDeclinedGen == res->data->tlasGeneration) || tlas.data.empty() ||
                     tlasCloneGen != res->data->tlasGeneration))
  {
    tlas.data.clear();
    return;
  }
  if (!allHidden)
  {
    tlasFrameDim = v_extract_x(v_hmax3(v_sub(leafUnion.bmax, leafUnion.bmin)));
    // Shared helper, in lockstep with the load-time decision - except the tree-backed arm
    // budgets every leaf at the refresh-wide scale and conditioning maxima, so heterogeneous
    // content can decline an envelope the per-node build model passes; the decline is the
    // conservative direction, and the linear walk stays correct.
    const float opReserve = 1.25f * maxNodeOp;
    const float fpErrS = collres_tlas_fp_budget(maxNodeOp, tlasFrameDim, amplMax);
    tlas.fpErr = v_splats(fpErrS); // axis-independent: rotation mixes source axes into every output
    const vec3f pMin = v_sub(leafUnion.bmin, tlas.fpErr), pMax = v_add(leafUnion.bmax, tlas.fpErr);
    const vec3f pSize = v_sub(pMax, pMin);
    // Outside the provable envelope the prefilter must not guess: LATCH the decline, or every
    // later pose write would recopy, walk all nodes and decline again. Judged on the dominant
    // dimension: thin cross-axes span mostly pad harmlessly.
    const float maxRatio = fpErrS / max(v_extract_x(v_hmax3(pSize)), 1e-30f);
    if (!(fpErrS < 1e30f) || !v_test_xyz_finite(v_add(pMin, pMax)) || maxRatio > COLLRES_TLAS_BUDGET_MAX_RATIO)
    {
      declineTlas();
      return;
    }
    tlasDeclined = false; // the envelope passed: a previously declined pose has returned
    // Geometry-only operand (with reserve) and conditioning maxima: the single-leaf guard
    // compares against these, so a later pose cannot consume the query tranche unnoticed.
    tlasFrameNodeOp = opReserve;
    tlasFrameAmpl = amplMax;
    // An escape refit inflates the frame GEOMETRICALLY: a monotonic one-by-one drift then
    // amortizes to O(log) full refits. Coarser bins, still conservative.
    const vec3f headGrow = v_mul(pSize, v_splats(frame_headroom));
    const vec3f hMin = v_sub(pMin, headGrow), hMax = v_add(pMax, headGrow);
    const vec3f hSize = v_sub(hMax, hMin);
    const vec3f safeSize = v_max(hSize, v_splats(0.0001f));
    const vec3f effectiveMin = v_sub(hMin, v_mul(v_sub(safeSize, hSize), V_C_HALF));
    // From the ENCODED frame (expanded, then floored): the incremental path lets a leaf drift
    // anywhere inside it, so the deviation gate must assume the worst in-frame magnitude.
    tlasFrameMag = v_extract_x(v_hmax3(v_max(v_abs(effectiveMin), v_abs(v_add(effectiveMin, safeSize)))));
    tlas.scale = v_div(v_splats(COLLRES_TLAS_QUANT_SPAN), safeSize);
    tlas.origin = effectiveMin;
  }
  if (tlas.data.empty() || tlasCloneGen != res->data->tlasGeneration)
  {
    // A clone keyed to another generation is replaced, never refit at the new offsets; the
    // generation is stamped once at the load, so this is the first clone. The clone takes the
    // TREE BYTES alone: the topology metadata behind them stays resource-side.
    tlas.data.resize_noinit(res->data->tlasTreeBytes);
    memcpy(tlas.data.data(), res->data->tlasData().data(), res->data->tlasTreeBytes);
    tlasCloneGen = res->data->tlasGeneration;
  }

  for (uint32_t k = 0, e = (uint32_t)leafNodes.size(); k < e; ++k)
  {
    const uint32_t i = leafNodes[k];
    encodeTlasLeafBox((int)i, nodes[i].tlasLeafLoc, posed_boxes ? &posed_boxes[k] : &ownedBoxes[k]);
  }
  if (allHidden)
    return; // inner boxes stay where they were: conservative, never a missed hit

  // The emitter writes a parent before its children, so one reverse sweep publishes every node's
  // union into its parent's lane with each child already final.
  uint8_t *treeData = tlas.data.data();
  const dag::ConstSpan<uint32_t> nodeOfs = res->tlasNodeOfs();
  const dag::ConstSpan<uint32_t> parentPacked = res->tlasNodeParentPacked();
  for (uint32_t i = res->data->tlasInnerCount - 1; i > 0; --i)
  {
    uint16_t mn[3], mx[3];
    tlas_node_union((const uint16_t *)(treeData + nodeOfs[i]), CollisionResource::tlasChildCountOf(parentPacked[i]), mn, mx);
    const uint32_t pp = CollisionResource::tlasParentOf(parentPacked[i]);
    soa4::storeLaneBoxU16(treeData + nodeOfs[pp >> 2], CollisionResource::tlasChildCountOf(parentPacked[pp >> 2]), (int)(pp & 3u), mn,
      mx);
  }
}

void CollisionResourceInstance::refitTlasLeaf(int node_index, const mat44f *geom_tm, const bbox3f *posed_box) const
{
  if (!res || !res->hasAllNodesTLAS())
    return;
  // Latched decline: this blueprint generation keeps the linear walk (see declineTlas).
  if (tlasDeclined && tlasDeclinedGen == res->data->tlasGeneration)
    return;
  const uint32_t loc = res->getAllNodes()[node_index].tlasLeafLoc;
  if (loc == ~0u)
    return; // no leaf (POINTS or geometry-less): nothing to refit, and nothing worth cloning for
  if (tlas.data.empty() || tlasCloneGen != res->data->tlasGeneration)
  {
    // First write with no usable clone: the full refit materializes and derives the frame.
    tlasDeclined = false;
    refitAllTlasLeaves();
    return;
  }

  const CollisionNode &node = res->getAllNodes()[node_index];
  bbox3f nodeBox, rawBox;
  v_bbox3_init_empty(rawBox); // MSVC C4701: hasBox guards the unwritten arm, invisibly to flow analysis
  bool hasBox = posed_box != nullptr;
  if (hasBox)
  {
    // a leaf must enclose both frames, and the caller's grow box carries no eps-IDENT union
    rawBox = res->getNodeGeometryBBox(node);
    nodeBox = *posed_box;
    add_eps_ident_raw_box(nodeBox, node, poseMeta[node_index], rawBox);
  }
  else
    hasBox = ownedPosedNodeBox(node_index, nodeBox, &rawBox);
  if (hasBox)
  {
    // A pose write can raise the operand magnitude or conditioning past the frame budget, and a
    // box escaping the frame would clamp to the wall and false-miss rays outside it: either way
    // re-derive by full refit, with growth headroom (a monotonic drift would trip again on the
    // very next write). The escape test uses the SAME padded values the encoder writes.
    const float rawMag = v_extract_x(v_hmax3(v_max(v_abs(rawBox.bmin), v_abs(rawBox.bmax))));
    const float boxMag = v_extract_x(v_hmax3(v_max(v_abs(nodeBox.bmin), v_abs(nodeBox.bmax))));
    const float scale = (uint32_t)node_index < (uint32_t)poseMeta.size() ? poseMeta[node_index].maxTmScale : 1.f;
    mat44f gt;
    if (geom_tm)
      gt = *geom_tm;
    else
    {
      mat44f pt;
      v_mat44_make_from_43cu_unsafe(pt, nodeTm[node_index].array);
      gt = res->geometryTmFromPosed(node_index, pt, poseMeta[node_index]);
    }
    const float ampl = collres_tlas_tm_conditioning(gt);
    bool needFull = !(collres_tlas_node_op(scale, rawMag, boxMag, ampl) <= tlasFrameNodeOp) || !(ampl <= tlasFrameAmpl);
    if (!needFull)
    {
      vec4f qmin, qmax;
      tlasQuantPadded(nodeBox, qmin, qmax);
      needFull = !v_check_xyz_all_true(v_and(v_cmp_ge(qmin, v_zero()), v_cmp_ge(v_splats(COLLRES_TLAS_QUANT_CLAMP_HI), qmax)));
    }
    if (needFull)
    {
      refitAllTlasLeaves(/*frame_headroom*/ 0.25f);
      return;
    }
  }
  encodeTlasLeafBox(node_index, loc, hasBox ? &nodeBox : nullptr);

  uint8_t *treeData = tlas.data.data();
  const dag::ConstSpan<uint32_t> nodeOfs = res->tlasNodeOfs();
  const dag::ConstSpan<uint32_t> parentPacked = res->tlasNodeParentPacked();
  uint32_t idx = loc >> 2;
  int guard = 0;
  while (CollisionResource::tlasParentOf(parentPacked[idx]) != CollisionResource::TLAS_PARENT_ROOT &&
         guard++ <= (int)res->data->tlasInnerCount)
  {
    uint16_t mn[3], mx[3];
    tlas_node_union((const uint16_t *)(treeData + nodeOfs[idx]), CollisionResource::tlasChildCountOf(parentPacked[idx]), mn, mx);
    const uint32_t pp = CollisionResource::tlasParentOf(parentPacked[idx]);
    const uint32_t pi = pp >> 2;
    const int pn = CollisionResource::tlasChildCountOf(parentPacked[pi]), lane = (int)(pp & 3u);
    uint16_t *pa = (uint16_t *)(treeData + nodeOfs[pi]);
    bool changed = false;
    for (int k = 0; k < 3; ++k)
    {
      if (pa[k * pn + lane] != mn[k])
      {
        pa[k * pn + lane] = mn[k];
        changed = true;
      }
      if (pa[(3 + k) * pn + lane] != mx[k])
      {
        pa[(3 + k) * pn + lane] = mx[k];
        changed = true;
      }
    }
    if (!changed)
      break; // this ancestor already enclosed the new box, so every ancestor above it does too
    idx = pi;
  }
}

void CollisionResourceInstance::recomputePoseMeta(int node_index)
{
  {
    // Before any early return: the bit describes nodeTm, which the caller has just written.
    mat44f stored;
    v_mat44_make_from_43cu_unsafe(stored, nodeTm[node_index].array);
    poseMeta[node_index].setPoseIdentity(collres_is_exact_identity_43(stored));
  }
  // A geometry-baked (singular-authored) primitive has no recoverable node-local frame,
  // so no live pose is realizable: keep the node hidden rather than classifying the
  // identity geometry tm, which would resurrect it at its baked bind pose. A RETAINED bake
  // (valid general sphere) IS poseable: it falls through and classifies its compatibility
  // transform like any other node.
  if (DAGOR_UNLIKELY(poseMeta[node_index].isGeometryBaked() && !poseMeta[node_index].isRetainedBake()))
  {
    poseMeta[node_index].setTraceable(false);
    poseMeta[node_index].setComposable(false); // stored baked values are the right report
    LOGWARN_ONCE("collres instance %p of res %p: live pose on geometry-baked node %d is not realizable; node stays hidden", this, res,
      node_index);
    return;
  }
  // A non-finite component anywhere in the affine pose (translation included) must not
  // classify as traceable nor reach posed bounds. Max-abs, not a column sum: finite
  // components can overflow the intermediate sum near the scale bound. The exponent-bit
  // test survives -ffinite-math-only, which folds float-domain tricks like x - x away.
  mat44f posedFull;
  v_mat44_make_from_43cu_unsafe(posedFull, nodeTm[node_index].array);
  const bool poseFinite = v_test_xyzw_finite(
    v_max(v_max(v_abs(posedFull.col0), v_abs(posedFull.col1)), v_max(v_abs(posedFull.col2), v_abs(posedFull.col3))));
  const mat44f geometryTm = getNodeGeometryTm(node_index);
  uint8_t flags = CollisionResource::classifyNodeTmFlags(geometryTm, poseMeta[node_index].maxTmScale);
  const bool exactIdentity = collres_is_exact_identity_43(geometryTm);
  // Epsilon classes can still under-bound shear; bit-exact rigid bases keep their exact stamp.
  if (!exactIdentity && !is_exact_rigid_basis_v(geometryTm))
    poseMeta[node_index].maxTmScale = max(poseMeta[node_index].maxTmScale, mat33_spectral_norm(geometryTm) * 1.0002f);
  if ((flags & CollisionNode::IDENT) && !exactIdentity)
    flags = (flags & ~CollisionNode::IDENT) | CollisionNode::TRANSLATE;
  poseMeta[node_index].flags = flags;
  // The relative determinant gate accepts tiny valid poses but rejects collapsed or mirrored
  // ones; normalized so a large finite pose cannot overflow det and scale^3 into Inf > Inf.
  float ndet;
  const bool detOk = CollisionResource::relativeDetAboveFloor(geometryTm, ndet);
  // Inverse-based dispatch needs the whole inverse finite: near-bound translations and
  // huge-scale cofactors overflow it while the forward pose stays representable.
  bool composable = poseFinite && detOk;
  if (composable)
  {
    mat44f inv;
    v_mat44_inverse43(inv, geometryTm);
    composable = v_test_xyzw_finite(v_max(v_max(v_abs(inv.col0), v_abs(inv.col1)), v_max(v_abs(inv.col2), v_abs(inv.col3))));
  }
  poseMeta[node_index].setComposable(composable);
  const bool traceOk = composable && ndet > 0.f;
  poseMeta[node_index].setTraceable(traceOk);
  if (DAGOR_UNLIKELY(detOk && ndet < 0.f))
    LOGWARN_ONCE("collres instance %p of res %p: mirrored pose (ndet=%g) for node %d; node is not traceable", this, res, ndet,
      node_index);
  else if (DAGOR_UNLIKELY(!traceOk))
    LOGWARN_ONCE("collres instance %p of res %p: singular, non-finite or inverse-overflowing pose (ndet=%g) for node %d; node is not "
                 "traceable",
      this, res, ndet, node_index);
}

bool CollisionResourceInstance::validateForUpdate() const
{
  const bool bound = res && !isDefault();
  G_ASSERTF(bound, "updating a default or resource-less CollisionResourceInstance");
  if (DAGOR_UNLIKELY(!bound))
  {
    LOGERR_ONCE("updating a default or resource-less CollisionResourceInstance is ignored");
    return false;
  }
  return check_instance_owned_and_fresh(res, *this, "update"); // a failed update is ignored
}

bool CollisionResourceInstance::updateNodeTmImpl(int node_index, mat44f_cref tm)
{
  G_ASSERT_RETURN((uint32_t)node_index < nodeTm.size(), false);
  // rootBBox grows FIRST (conservative raw compose; the exact grow re-runs after the store): a
  // racing trace whose reject load lands after the grow sees bounds covering the new pose. No
  // cross-thread ordering is implied -- a reject load may still precede the grow, and poseMeta
  // recomputes after the store -- so this only narrows the unsynchronized window: pose writes
  // vs. traces on one resource stay the caller's synchronization duty (legacy contract).
  // Structural mutations (setNodeEnabled, resets: rootBBox can shrink) keep exclusive access.
  // A non-finite pose must not reach the union: it would poison the grow-only box for good
  // (recomputePoseMeta below only hides the node).
  // Max-abs, not a sum: two finite same-sign terms can overflow ONLY in the addition, and a
  // false negative here would keep rootBBox at the old pose and MISS rays at the new one.
  const bool poseFinite = v_test_xyz_finite(v_max(v_max(v_abs(tm.col0), v_abs(tm.col1)), v_max(v_abs(tm.col2), v_abs(tm.col3))));
  const CollisionNode &node = res->getAllNodes()[node_index];
  // Zero-vert markers (r < 0) and permanently hidden geometry bakes never join: they cannot
  // trace at any pose, and the grow-only union keeps their phantom for the resource's lifetime.
  if (poseFinite && isNodeEnabled(node_index) && node.type != COLLISION_NODE_TYPE_POINTS && node.radiusAroundBoxCenter >= 0.f &&
      !(poseMeta[node_index].isGeometryBaked() && !poseMeta[node_index].isRetainedBake()) &&
      !((node.type == COLLISION_NODE_TYPE_MESH || node.type == COLLISION_NODE_TYPE_CONVEX) && !node.hasGeometry()))
  {
    bbox3f newBox;
    // The incoming pose's COMPATIBILITY transform, not tm itself: an eps-IDENT or retained-bake
    // prim stores geometry in its authored frame, and a raw-tm grow would leave the authored
    // offset uncovered for the window before the store.
    mat44f growTm = tm;
    if (DAGOR_UNLIKELY(CollisionResource::usesAuthoredFrame(node, poseMeta[node_index])))
    {
      mat44f invAuthored;
      if (DAGOR_LIKELY((uint32_t)node_index < res->data->authoredNodeItm().size()))
        v_mat44_make_from_43cu_unsafe(invAuthored, res->data->authoredNodeItm()[node_index].array);
      else
      {
        mat44f authored;
        v_mat44_make_from_43cu_unsafe(authored, res->data->authoredNodeTm()[node_index].array);
        v_mat44_inverse43(invAuthored, authored);
      }
      v_mat44_mul43(growTm, tm, invAuthored);
    }
    // A pose that will hide (singular, mirrored, inverse-overflowing) must not widen the
    // grow-only union: it cannot shrink back before a structural recompute.
    float growNdet;
    bool growTraceable = CollisionResource::relativeDetAboveFloor(growTm, growNdet) && growNdet > 0.f;
    if (growTraceable)
    {
      mat44f growInv;
      v_mat44_inverse43(growInv, growTm);
      growTraceable =
        v_test_xyzw_finite(v_max(v_max(v_abs(growInv.col0), v_abs(growInv.col1)), v_max(v_abs(growInv.col2), v_abs(growInv.col3))));
    }
    if (growTraceable)
    {
      v_bbox3_init(newBox, growTm, res->getNodeGeometryBBox(node));
      // A finite basis can still overflow the composed extent: saturate, never drop (the node
      // stays traceable, so a short root box would cull rays to its reachable part).
      join_saturated_finite(rootBBox, newBox);
    }
  }
  posedSinceBind = true;
  const bool wasTraceable = poseMeta[node_index].isTraceable();
  // A byte-identical re-push cannot move the leaf box, so it skips the refit (per-frame
  // writers re-push static nodes); any representational difference conservatively re-refits.
  TMatrix asStored;
  v_mat_43cu_from_mat44(asStored.array, tm);
  const bool poseUnchanged = memcmp(&asStored, &nodeTm[node_index], sizeof(TMatrix)) == 0; //-V1014
  nodeTm[node_index] = asStored;
  recomputePoseMeta(node_index);
  // A leafless node this write recovers (see tlas_leafless_recovery) declines the clone; one that
  // un-recovers re-arms THROUGH a full refit (a bare latch clear would strand it clone-less).
  if (DAGOR_UNLIKELY(!wasTraceable && res->getAllNodes()[node_index].tlasLeafLoc == ~0u && res->hasAllNodesTLAS() &&
                     tlas_leafless_recovery(res->getAllNodes()[node_index], poseMeta[node_index],
                       res->getAllNodes()[node_index].radiusAroundBoxCenter)))
    declineTlas();
  else if (DAGOR_UNLIKELY(
             wasTraceable && !poseMeta[node_index].isTraceable() && tlasDeclined && res->getAllNodes()[node_index].tlasLeafLoc == ~0u))
    refitAllTlasLeaves(); // re-evaluates: re-arms on a passing envelope, re-declines otherwise
  // Nodes that never trace (hidden, unrealizable, POINTS, zero-vert markers, geometry-less
  // mesh/convex) must not widen (or poison) the posed bounds either: same predicate set as
  // recomputeRootBBox, so the grow-only arm never over-widens relative to the next full recompute.
  const bool boundsRelevant =
    isNodeEnabled(node_index) && poseMeta[node_index].isTraceable() && node.type != COLLISION_NODE_TYPE_POINTS &&
    node.radiusAroundBoxCenter >= 0.f &&
    !((node.type == COLLISION_NODE_TYPE_MESH || node.type == COLLISION_NODE_TYPE_CONVEX) && !node.hasGeometry());
  mat44f geomTm;
  bbox3f nodeBox;
  if (boundsRelevant)
  {
    // composed_or_raw_box: a return to exact identity must keep the stored box verbatim.
    geomTm = getNodeGeometryTm(node_index);
    nodeBox = composed_or_raw_box(geomTm, res->getNodeGeometryBBox(node));
    // Same saturate-never-drop guard as the pre-store grow.
    join_saturated_finite(rootBBox, nodeBox); // grow-only: exact bounds return on the next full update
  }
  // Runs AFTER the pose store: pose writes concurrent with traces of the same holder require
  // external serialization on the TLAS path (the public contract). Reuses the grow's compose.
  if (!poseUnchanged || !tlasCloneCurrent(res->data->tlasGeneration))
    refitTlasLeaf(node_index, boundsRelevant ? &geomTm : nullptr, boundsRelevant ? &nodeBox : nullptr);
  if (this == &res->defaultInstance && !poseUnchanged)
    res->defaultPoseGen++; // tree-backed instances re-derive their unbound-node state
  return true;
}

bool CollisionResourceInstance::updateNodeTm(int node_index, mat44f_cref tm)
{
  if (!validateForUpdate())
    return false;
  // A tree-backed pose is driven by its tree; manual node poses need the owned-matrix form.
  G_ASSERTF_RETURN(!tree, false, "collres instance %p of res %p: updateNodeTm needs the owned-matrix form", this, res);
  return updateNodeTmImpl(node_index, tm);
}

// Valid on tree-backed instances too: structural hides are per-instance metadata,
// independent of the matrix source (unlike updateNodeTm's owned-form-only contract).
// The caller-side serialization contract covers the plain meta write; the generation
// reset below makes the next trace re-derive everything the hide affects.
bool CollisionResourceInstance::setNodeEnabled(int node_index, bool enabled)
{
  if (!validateForUpdate())
    return false;
  // Out-of-range writes assert and drop. The bound is the meta array, which BOTH forms
  // carry (updateNodeTmImpl bounds on the owned-form nodeTm instead).
  G_ASSERT_RETURN((uint32_t)node_index < poseMeta.size(), false);
  PoseMeta &pm = poseMeta[(uint32_t)node_index];
  if (pm.isDisabled() == !enabled)
    return true; // already in the requested state: accepted, not dropped
  pm.setDisabled(!enabled);
  if (tree)
  {
    // Pose writes must be serialized against traces: catch a refresh racing this hide.
    G_ASSERTF(interlocked_acquire_load(refreshLock) == 0,
      "collres instance %p of res %p: setNodeEnabled during a concurrent trace refresh; serialize pose writes against traces", this,
      res);
    interlocked_release_store(poseGeneration, 0u); // never-refreshed: the next trace re-derives from the live tree
    // pose STATE changed without a tree generation bump: the lazy TLAS refresh must re-run
    interlocked_release_store(tlasPoseGeneration, 0u);
  }
  else
  {
    recomputeRootBBox();
    // A hide moves the frame and every leaf; a latched decline refits too, since hiding the
    // declining node removes the reason and only the refit can re-arm candidate mode.
    if (!tlas.data.empty() || tlasDeclined)
      refitAllTlasLeaves();
  }
  return true;
}

void CollisionResourceInstance::recomputeRootBBox()
{
  bbox3f box;
  v_bbox3_init_empty(box);
  dag::ConstSpan<CollisionNode> nodes = res->getAllNodes();
  for (uint32_t i = 0, e = min<uint32_t>(nodes.size(), nodeTm.size()); i < e; ++i)
  {
    const CollisionNode &node = nodes[i];
    if (!isNodeEnabled((int)i) || !poseMeta[i].isTraceable())
      continue; // hidden or unrealizable poses do not trace and must not bound
    if (node.type == COLLISION_NODE_TYPE_POINTS)
      continue; // never a trace target
    if (node.radiusAroundBoxCenter < 0.f)
      continue; // zero-vert marker: corner-mapping the inverted box would union a phantom
    if ((node.type == COLLISION_NODE_TYPE_MESH || node.type == COLLISION_NODE_TYPE_CONVEX) && !node.hasGeometry())
      continue;
    bbox3f nodeBox = res->getNodeGeometryBBox(node);
    bbox3f composedBox = composed_or_raw_box(getNodeGeometryTm((int)i), nodeBox);
    // Same saturate-never-drop guard as the incremental grows: NaN stays out, an overflowed
    // extent clamps to the float range so the traceable node still bounds.
    join_saturated_finite(box, composedBox);
    // A live pose diverging within the IDENT eps widens this union conservatively.
    add_eps_ident_raw_box(box, node, poseMeta[i], nodeBox);
  }
  rootBBox = box;
}

void CollisionResourceInstance::seedPose()
{
  const CollisionResourceInstance &def = res->defaultInstance;
  G_ASSERT(&def != this);
  // Generation read BEFORE the copy: a racing write leaves seen behind the current
  // generation, so the first mirror re-copies instead of freezing pre-write flags.
  const uint32_t liveGenAtSeed = interlocked_acquire_load(res->liveFlagsGen);
  poseMeta = def.poseMeta;
  interlocked_release_store(liveFlagsGenSeen, liveGenAtSeed);
  // Pose state seeds, but a fresh instance starts with every node enabled.
  bool clearedHide = false;
  for (PoseMeta &pm : poseMeta)
  {
    clearedHide |= pm.isDisabled();
    pm.setDisabled(false);
  }
  if (tree)
  {
    nodeTm.clear();
    // A driven pose deviates from bind and never licenses bind shortcuts.
    posedSinceBind = true;
    // Bind meta stays valid for driven nodes under the no-scale contract, except IDENT: a
    // driven node can leave its authored placement, so it demotes to the translate class.
    // maxTmScale of a driven node bounds only the rel-tm (the wtm contributes scale 1).
    dag::ConstSpan<CollisionNode> nodes = res->getAllNodes();
    for (uint32_t i = 0, e = min<uint32_t>(nodes.size(), (uint32_t)poseMeta.size()); i < e; ++i)
    {
      if (nodes[i].geomNodeId.index() >= treeNodeCountAtBind)
        continue;                         // unbound: keeps its default-pose meta and matrices
      poseMeta[i].setPoseIdentity(false); // driven: no stored matrix to shortcut
      PoseMeta &pm = poseMeta[i];
      // A geometry-baked (singular-authored) prim has no recoverable node-local frame, so no
      // driven pose is realizable: keep the load-time hide instead of reclassifying it from
      // the rel tm (the owned form and the removed eager path hide it the same way). A
      // RETAINED bake is poseable and classifies below.
      if (DAGOR_UNLIKELY(pm.isGeometryBaked() && !pm.isRetainedBake()))
      {
        pm.setBindTraceable(false); // the refresh re-derives TRACEABLE from this: hidden every generation
        continue;
      }
      mat44f relTm;
      if ((res->collisionFlags & COLLISION_RES_FLAG_HAS_REL_GEOM_NODE_ID) && i < (uint32_t)res->data->relGeomNodeTms().size())
        v_mat44_make_from_43cu_unsafe(relTm, res->data->relGeomNodeTms()[i].array);
      else
        v_mat44_ident(relTm);
      pm.maxTmScale = 1.f;
      uint8_t flags = CollisionResource::classifyNodeTmFlags(relTm, pm.maxTmScale);
      if (!(flags & CollisionNode::ORTHONORMALIZED))
        pm.maxTmScale = max(pm.maxTmScale, mat33_spectral_norm(relTm) * 1.0002f);
      else if (!is_exact_rigid_basis_v(relTm))
        pm.maxTmScale = max(pm.maxTmScale, 1.0015f); // in-class shear: the Gershgorin pad of the builder's scale stamp
      if (flags & CollisionNode::IDENT)
        flags = (flags & ~CollisionNode::IDENT) | CollisionNode::TRANSLATE;
      pm.flags = flags;
      // Normalized det floor: a raw det/scale^3 compare overflows to Inf > Inf for large
      // finite rel tms and falsely hides well-conditioned nodes (the owned classify shares it).
      float ndet;
      const bool floorOk = CollisionResource::relativeDetAboveFloor(relTm, ndet);
      // Handedness belongs to the refresh's live sign compare on the COMPOSED placement (a
      // mirrored rel under a mirrored wtm is right-handed); the authored gate accepts mirrored
      // placements the same way, so the seed gates on the det floor alone.
      pm.setTraceable(floorOk);
      pm.setBindTraceable(floorOk); // the refresh re-derives TRACEABLE from this
      pm.setComposable(floorOk);    // tree seeds skip the inverse probe; the det floor stands in
    }
    // rootBBox stays empty until the first trace refreshes it (poseGeneration == 0).
    v_bbox3_init_empty(rootBBox);
    return;
  }
  nodeTm = def.nodeTm;
  posedSinceBind = def.posedSinceBind; // a posed default seeds a posed copy
  // Poses seed verbatim, so the default's rootBBox stays valid unless a hide was cleared.
  if (DAGOR_UNLIKELY(clearedHide))
    recomputeRootBBox();
  else
    rootBBox = def.rootBBox;
  // A posed seed can stay read-only forever: materialize the clone now or the prefilter
  // stays silently absent.
  if (posedSinceBind && res->hasAllNodesTLAS())
    refitAllTlasLeaves();
}
