// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <math/dag_hlsl_floatx.h>
#include <math/dag_TMatrix.h>
#include <vecmath/dag_vecMath.h>
#include <generic/dag_tab.h>
#include <osApiWrappers/dag_cpuJobs.h>
#include <rendInst/riexHandle.h>
#include "../common.h"
#include "../placer.h"
#include "../riexProcessor.h"
#include "../../shaders/dagdp_volume.hlsli"
#include "../../shaders/dagdp_volume_terrain.hlsli"


namespace dagdp
{

enum class GatherMode
{
  Async,
  Synchronous
};

static constexpr float MIN_GEOMETRY_SIZE = 1.0f;

// (bmax+bmin) and (bmax-bmin) of a volume's culling box, the form Frustum::testBoxExtentB takes.
inline vec4f volume_cull_extent2(mat44f_cref tm44, float scale, float max_bounding_radius)
{
  return v_add(v_mul(v_add(v_abs(tm44.col0), v_add(v_abs(tm44.col1), v_abs(tm44.col2))), v_splats(2.0f * scale)),
    v_splats(2.0f * max_bounding_radius));
}

inline bool volume_in_draw_range(mat44f_cref tm44, vec4f extent2, vec4f view_pos, float draw_radius)
{
  bbox3f volBox;
  v_bbox3_init_by_bsph(volBox, tm44.col3, v_mul(extent2, V_C_HALF));
  bbox3f drawBox;
  v_bbox3_init_by_bsph(drawBox, view_pos, v_splats(draw_radius));
  return v_bbox3_test_box_intersect(drawBox, volBox);
}

// The volume itself: the unit cube scaled by +-scale. Doubles as the riex query box.
inline bbox3f volume_local_box(mat44f_cref tm44, float scale)
{
  bbox3f box;
  v_bbox3_init(box, tm44, {v_splats(-scale), v_splats(scale)});
  return box;
}

struct DagdpRiexGatherJob : public cpujobs::IJob
{
  struct PlacerData
  {
    ecs::EntityId eid; // for validation
    size_t firstEntry;
    size_t entryCount;
  };

  struct AroundRiPlacerData : PlacerData
  {
    bbox3f fbox = {};
    float drawRadius = 0.0f;
    size_t firstLocalVolume = 0;
    size_t localVolumeCount = 0;
    size_t firstRiGenSource = 0; // into riGenSourcePool
    size_t riGenSourceCount = 0;
  };

  struct PerViewportData
  {
    bbox3f frustumBox = {};
    vec4f worldPos = {};
    bool valid = false;
    Tab<PlacerData> onRi;
    Tab<AroundRiPlacerData> aroundRi;
  };

  struct Entry
  {
    int resIdx;
    bbox3f box;
    Tab<rendinst::riex_handle_t> handles;
  };

  struct LocalVolumeSnapshot
  {
    TMatrix transform;
    float scale;
    int volumeType;
    // Filled by doJob: the entries of the (source, this volume) pairs that passed the draw range.
    size_t firstVolume = 0;
    size_t volumeCount = 0;
  };

  // One grid query. The query box does not depend on the viewport, so a world volume is queried once
  // for the whole view; a local volume belongs to the one viewport whose around_ri record made it.
  struct VolumeEntry
  {
    ecs::EntityId placerEid;
    TMatrix tm;
    float scale;
    int volumeType;
    bbox3f box;
    size_t firstHandle; // into volumeHandlePool
    size_t handleCount;
  };

  // The job owns everything below between start() and waitDone(): read or reset it only after the drain.
  // Pools never shrink to preserve inner Tab allocations across frames
  Tab<Entry> entryPool;
  size_t entryCount = 0;
  Tab<PerViewportData> viewportDataPool;
  size_t viewportCount = 0;
  Tab<LocalVolumeSnapshot> localVolumePool;
  size_t localVolumeCount = 0;
  Tab<VolumeEntry> volumePool; // world volumes first, then the local volumes appended by doJob
  size_t volumeCount = 0;
  size_t worldVolumeCount = 0;
  // One arena: a Tab per (local volume, source) pair would retain thousands of blocks.
  Tab<rendinst::riex_handle_t> volumeHandlePool;
  Tab<TMatrix> riGenSourcePool; // RIGen sources have no riex handle, so gather_start snapshots their transforms
  Tab<TMatrix> sourceTmScratch;

  // Draw-range culling inputs snapshotted at gather_start; gather_process culls with the same values,
  // so every volume it emits has pre-gathered handles.
  float rangeScale = 1.0f;
  float maxBoundingRadius = 0.0f;
  bool gridGathered = false;

  bool launched = false;
  bool scheduled = false;

  void reset();
  PerViewportData &addViewport();
  void addEntries(PlacerData &rec, const ecs::IntList &resource_ids, bbox3f_cref box);
  LocalVolumeSnapshot &addLocalVolume();
  VolumeEntry &addVolumeEntry(ecs::EntityId placer_eid, const TMatrix &tm, mat44f_cref tm44, float scale, int volume_type);
  dag::ConstSpan<rendinst::riex_handle_t> volumeHandles(const VolumeEntry &e) const
  {
    return make_span_const(volumeHandlePool.data() + e.firstHandle, e.handleCount);
  }
  dag::ConstSpan<VolumeEntry> localVolumeSources(const LocalVolumeSnapshot &local) const
  {
    return make_span_const(volumePool.data() + local.firstVolume, local.volumeCount);
  }
  void start(GatherMode mode);
  void doJob() override;
  const char *getJobName(bool &) const override { return DAPROFILER_STRING("dagdp_volume_riex_gather"); }
  void waitDone();

private:
  size_t addEntry(int res_idx, bbox3f_cref box);
  void gatherVolumeHandles(VolumeEntry &e);
  void gatherLocalVolumes(AroundRiPlacerData &rec, size_t viewport_index);
};

static constexpr uint32_t ESTIMATED_LOCAL_VOLUMES_PER_PLACER = 4;
using LocalVolumeSnapshots =
  dag::RelocatableFixedVector<DagdpRiexGatherJob::LocalVolumeSnapshot, ESTIMATED_LOCAL_VOLUMES_PER_PLACER, true, framemem_allocator>;

static constexpr uint32_t ESTIMATED_RELEVANT_MESHES_PER_FRAME = 256;
static constexpr uint32_t ESTIMATED_RELEVANT_TILES_PER_FRAME = 256;
static constexpr uint32_t ESTIMATED_RELEVANT_VOLUMES_PER_FRAME = 16;

struct VolumeVariant
{
  dag::RelocatableFixedVector<PlacerObjectGroup, 4> objectGroups;
  float density = 0.0f;
  float minTriangleArea = 0.0f;
  Point2 distBasedScale = Point2(1, 1);
  Point3 distBasedCenter = Point3(0, 0, 0);
  float distBasedRange = 1;
  float sampleRange = -1;
  bool axisAbs = false;
};

struct VolumeMappingItem
{
  uint32_t variantIndex;
  float density;
  float maxDrawDistance;
  int targetMeshLod;
  Point3 axis;
  bool axisLocal;
  int csmCascadeCount;
};

using VolumeMapping = dag::VectorMap<ecs::EntityId, VolumeMappingItem>;

struct VolumeBuilder
{
  dag::Vector<VolumeVariant> variants;
  VolumeMapping mapping;
};

struct VolumeManager
{
  VolumeBuilder currentBuilder; // Only valid while building a view.
};

void create_volume_nodes(const ViewInfo &view_info,
  const ViewBuilder &view_builder,
  const VolumeManager &volume_manager,
  NodeInserter node_inserter,
  const RulesBuilder &rules_builder);

struct MeshToProcess
{
  int startIndex;
  int numFaces;
  int baseVertex;
  int stride;
  int vbIndex;
};

using RelevantMeshes = dag::RelocatableFixedVector<MeshIntersection, ESTIMATED_RELEVANT_MESHES_PER_FRAME, true, framemem_allocator>;
using RelevantTiles = dag::RelocatableFixedVector<VolumeTerrainTile, ESTIMATED_RELEVANT_TILES_PER_FRAME, true, framemem_allocator>;
using RelevantVolumes = dag::RelocatableFixedVector<VolumeGpuData, ESTIMATED_RELEVANT_VOLUMES_PER_FRAME, true, framemem_allocator>;

void gather_start(DagdpRiexGatherJob &job,
  const VolumeMapping &volume_mapping,
  const ViewInfo &view_info,
  const ViewPerFrameData &view_per_frame,
  float max_bounding_radius,
  GatherMode mode);

void gather_process(DagdpRiexGatherJob &job,
  const VolumeMapping &volume_mapping,
  const ViewInfo &view_info,
  uint32_t viewport_index,
  const Viewport &viewport,
  float max_bounding_radius,
  int target_mesh_lod,
  RiexProcessor &riex_processor,
  RelevantMeshes &out_meshes,
  RelevantTiles &out_tiles,
  RelevantVolumes &out_volumes);

void start_gather_before_draw_volumes(ecs::EntityManager &manager, const Point3 &cam_pos, const Frustum &cam_frustum);

bool is_volume_early_riex_gather_enabled();

// Gathers the volume grid queries in the job as well. Has no effect while the riex gather above is off,
// which keeps the volume queries on the main thread as before.
bool is_volume_early_grid_gather_enabled();

} // namespace dagdp

ECS_DECLARE_BOXED_TYPE(dagdp::VolumeManager);
