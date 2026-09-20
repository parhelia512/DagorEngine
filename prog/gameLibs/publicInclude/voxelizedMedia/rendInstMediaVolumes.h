//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <math/dag_bounds3.h>
#include <dag/dag_vector.h>
#include <util/dag_hashedKeyMap.h>
#include <voxelizedMedia/voxelizedMedia.h>
#include <voxelizedMedia/vegetationMediaVoxelizer.h>

class RenderableInstanceLodsResource;
class CollisionResource;

// the rendinst side of DaGIMediaVolumes: classifies tree/bush pools into media volume
// types, gathers their instances per refill region, bakes types through the voxelizer.
// render path confinement, no lock: everything runs from the GI update and clears with
// the GI; another path must add one
struct RendInstMediaVolumes
{
  ~RendInstMediaVolumes() { clear(); } // registerType refs every Type::res
  RendInstMediaVolumes() = default;
  RendInstMediaVolumes(const RendInstMediaVolumes &) = delete;
  RendInstMediaVolumes &operator=(const RendInstMediaVolumes &) = delete;
  void updateTypes(); // call each GI update: riGen layers and riex pools stream in over many frames
  // bakes pending types under the given frame block.
  // true on the frame a bake wave settles: call DaGI::invalidateInitialMedia then, before updatePosition
  bool bakePending(int global_frame_block_id);
  // the body of the DaGI::prepare_initial_media_cb. updateTypes must have run: an
  // unclassified region gathers empty, indistinguishable from one with no canopy
  void setRegionInstances(const BBox3 &box, float voxel_size);
  void debugPrepare(const Point3 &cam_pos); // gi_media_vols_debug: prepare on update (after updateTypes), render in the debug pass
  void debugRender() { volumes.debugRender(); }
  void afterReset() { volumes.afterReset(); } // the atlas did not survive the reset: rebake all types
  void clear();

protected:
  int registerType(RenderableInstanceLodsResource *res, const char *name, int riex_pool);
  void classifyRiexPool(uint32_t pool, RenderableInstanceLodsResource *res, CollisionResource *coll);
  void gather(const BBox3 &box, Tab<DaGIMediaVolumeInstance> &out);
  bool bakeType(int type, int global_frame_block_id);
  int typeOfRiexPool(uint32_t pool) const { return riPoolToType.findOr(poolKeyRiex(pool), -1); }
  int typeOfRiGenPool(int layer_ix, int pool_ix) const { return riPoolToType.findOr(poolKeyRiGen(layer_ix, pool_ix), -1); }
  static uint32_t poolKeyRiex(uint32_t pool) { return pool + 1; }
  static uint32_t poolKeyRiGen(int layer_ix, int pool_ix) { return (uint32_t(layer_ix + 1) << 16) + uint32_t(pool_ix) + 1; }

  DaGIMediaVolumes volumes;
  VegetationMediaVoxelizer vegVoxelizer;
  // one map for both pool kinds: pool ids are 16 bit, so the keys stay disjoint (0 is the
  // empty sentinel); a riGen pool with no entry is not a canopy
  HashedKeyMap<uint32_t, int> riPoolToType;
  uint32_t riexPoolsScanned = 0;
  struct CollWait
  {
    uint32_t pool = 0;
    uint16_t tries = 0;
  };
  dag::Vector<CollWait> collWaits; // riex pools seen with a res but no collRes yet
  struct Type
  {
    RenderableInstanceLodsResource *res = nullptr; // ref held: a reload can swap the pool's own res
    int riexPool = -1;                             // -1 for a riGen only type
    Point3 reachPerAxis = Point3(0, 0, 0);         // xz corner, y, xz corner: riGen instances only yaw
    float reach3d = 0;                             // full corner: riex instances can pitch or roll
    uint16_t bakeTries = 0;
  };
  dag::Vector<Type> types; // indexed by the DaGIMediaVolumes type id
  dag::Vector<uint32_t> canopyRiexPoolBits;
  Point3 maxReachPerAxis = Point3(0, 0, 0); // the riGen (yaw only) gather margin
  float maxReach3d = 0;                     // riex instances can pitch or roll (fallen trees)
  float maxScaledRiexReach = 0;             // grid query margin: maxReach3d at the widest scale met so far
  float maxRiGenScale = 1;                  // starts at 1: the pos instance walk prunes at unit scale
};
