//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <math/dag_bounds3.h>
#include <math/dag_Point3.h>
#include <math/dag_Quat.h>
#include <math/integer/dag_IPoint3.h>
#include <generic/dag_tab.h>
#include <EASTL/bitvector.h>
#include <EASTL/fixed_function.h>
#include <3d/dag_resPtr.h>
#include <shaders/dag_computeShaders.h>
#include <shaders/dag_postFxRenderer.h>

struct DaGIMediaVolumeInstance
{
  Point3 pos;
  Point3 scale;
  Quat rot;      // volume local to world
  uint32_t type; // from DaGIMediaVolumes::addType
};
// append instances whose volume intersects box; called for each media scene refill region.
// a region keeps at most DaGIMediaVolumes::MAX_REGION_INSTANCES records; the extra are
// dropped with one logerr, the signal that a region overflowed the record budget
typedef eastl::fixed_function<64, void(const BBox3 &box, Tab<DaGIMediaVolumeInstance> &out)> dagi_media_volumes_cb;
// draw the type's mesh at the origin in the project's voxelization mode, covering the three
// voxelize axes the way the project's shaders expect (an axis var loop or instance id).
// entering the mode belongs to the project; it only needs the voxelize space vars, so
// worldPosToVoxelSpace maps the local box to the raster. the albedo write of that mode lands
// in the type's density brick only if the drawing shader instantiates the redirect,
// DAGI_MEDIA_VOL_BAKE_WRITE_INIT and _USE from dagi_media_vol_bake_write.dshl: without them
// the bake draws into nothing and the empty brick looks exactly like a streaming stall. called once per bake. return false
// when the type can not render yet (streaming): the bake is aborted and retried on a later frame
typedef eastl::fixed_function<64, bool(int type)> dagi_media_volume_render_cb;

// runtime-baked per-type media density volumes (tree canopies, bushes, any translucent
// aggregate), injected into the media scene per refill region. Nothing is allocated
// until the first type is added; atlas and buffers grow on demand.
struct DaGIMediaVolumes
{
  // the record and slot formats cap one refill region; the gather callback may stop here
  static constexpr uint32_t MAX_REGION_INSTANCES = (64 << 10) - 1;
  ~DaGIMediaVolumes(); // zeroes the grid shader var: a recreated DaGI without volumes must not read stale buffers
  int addType(const BBox3 &local_box);
  // the reach addType derived for a type at unit scale (xz corner and full corner):
  // margins outside must agree with the binning reach, so they read it back
  float typeReach(int type) const { return types[type].reach; }
  float typeReach3d(int type) const { return types[type].reach3d; }
  // debug trace of the source instances + bricks around the camera (gi_media_vols_debug):
  // prepare walks the scene where the gather callback is at hand, render draws in the debug pass
  void debugPrepare(const Point3 &cam_pos, const dagi_media_volumes_cb &gather);
  void debugRender();
  // bakes up to gi_media_vol_bakes_per_frame not yet baked types, rest stay pending.
  // a type whose render callback returns false (still streaming) stays pending too; a separate attempt
  // cap bounds the frame cost when many types wait, a rotating cursor still reaches all of them.
  // true on the one frame a bake wave settles, which is when the media scene must refill:
  // call DaGI::invalidateInitialMedia then, before updatePosition
  bool bakePending(const dagi_media_volume_render_cb &render_type);
  // fills one media scene refill region: this is the body of the DaGI::prepare_initial_media_cb
  // the project hands to updatePosition
  void setRegionInstances(const BBox3 &box, float voxel_size, const dagi_media_volumes_cb &gather);
  void afterReset(); // a device reset loses the atlas: rebake every type
  // forgets every registered type and its baked brick. addType then starts at 0 again, so a
  // project that keeps its own per type tables must clear them together with this or the two
  // sets of type ids drift apart
  void clear();

protected:
  static constexpr uint32_t BAKE_WAVE_QUIET_FRAMES = 64;
  bool bakeReadyTypes(const dagi_media_volume_render_cb &render_type); // true if anything baked this frame
  // quiet frames left before an open bake wave ends, 0 when no wave runs. the wave end is
  // what the media scene refills on, so a burst costs one refill, not one per bake frame
  uint32_t waveQuietLeft = 0;
  void uploadInstances(dag::ConstSpan<DaGIMediaVolumeInstance> src, float voxel_size);
  Point3 debugInstsPos = Point3(1e6f, 1e6f, 1e6f); // last gather center; a meter of travel re-gathers
  float debugGatherDist = 0;                       // the knobs the cached gather used: a change re-gathers on the spot
  int debugGatherInsts = 0;
  uint32_t debugInstCount = 0;          // closest baked instances now in debugInstsBuf
  UniqueBufWithShaderVar debugInstsBuf; // persistent: filled on gather, read every debug frame
  PostFxRenderer volsDebugRenderer;
  void resetDebugCache();
  void keepBakedInstances(Tab<DaGIMediaVolumeInstance> &insts) const;

  void initGpu();
  void ensureTypeCapacity(int count);
  bool bakeType(int type, const dagi_media_volume_render_cb &render_type);
  void uploadType(int type);

  struct Type
  {
    BBox3 box;
    float reach = 0;   // xz distance from the model origin to the farthest box corner, at unit
                       // scale: the grid bins in xz, a 3d reach would fatten every cell range
    float reach3d = 0; // full corner distance: a pitched or rolled instance (fallen riex
                       // tree) can swing the box height into xz, the yaw only reach misses it
  };
  Tab<Type> types;
  eastl::bitvector<> baked;
  Tab<uint32_t> retryAfterFrame; // per type frame gate, paces bake retries of streaming stalled types
  uint32_t lastGatherCount = 0;  // reserve estimate: consecutive regions hold similar instance counts
  int pendingBakes = 0;          // not yet baked types: the completed state early outs in O(1)
  UniqueTexWithShaderVar atlas;
  UniqueTex bakeColor; // only sets the bake raster size, nothing draws to it and no shader reads it
  UniqueBufWithShaderVar typesBuf, instancesBuf, gridBuf, gridIxBuf, albedoBuf, bitsBuf;
  eastl::unique_ptr<ComputeShaderElement> finalizeCs;
  int typeCapacity = 0;
  int nextBakeScan = 0;                 // rotating bake attempt cursor
  uint32_t nextRetryFrame = 0;          // earliest frame a pending type comes ready: the stall gate
  IPoint3 atlasGrid = IPoint3(0, 0, 0); // types pack as a 3d grid of bricks, not one z column
};
