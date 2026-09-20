// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <voxelizedMedia/rendInstMediaVolumes.h>
#include <shaders/dag_rendInstRes.h>
#include <shaders/dag_shaderBlock.h>
#include <rendInst/rendInstExtra.h>
#include <rendInst/rendInstExtraAccess.h>
#include <rendInst/rendInstAccess.h>
#include <rendInst/rendInstCollision.h>
#include <rendInst/ccExtra.h>
#include <math/dag_mathUtils.h>
#include <util/dag_globDef.h>
#include <util/dag_convar.h>
#include <perfMon/dag_statDrv.h>
#include <debug/dag_log.h>

// grid query for regions below this side, pool span sweep above: the sweep cost is bound
// by the media instance count, so only the rare full refill of the coarsest clips takes it
CONSOLE_INT_VAL("gi", gi_media_vol_riex_spatial_m, 512, 0, 4096);

static ShaderMesh *media_pool_mesh_at_lod(RenderableInstanceLodsResource *res, int lod)
{
  if (!res->lods[lod].scene)
    return nullptr;
  auto *meshRes = res->lods[lod].scene->getMesh();
  if (!meshRes)
    return nullptr;
  return meshRes->getMesh()->getMesh();
}

template <typename Pred>
static bool any_lod_elem(RenderableInstanceLodsResource *res, int lod, Pred pred)
{
  ShaderMesh *mesh = media_pool_mesh_at_lod(res, lod);
  if (!mesh)
    return false;
  for (const ShaderMesh::RElem &elem : mesh->getElems(ShaderMesh::STG_opaque, ShaderMesh::STG_atest))
    if (pred(elem, elem.mat ? elem.mat->getShaderClassName() : ""))
      return true;
  return false;
}

static bool media_bake_elem_filter(const char *cls)
{
  // a vine covered crate contributes its vines, not the crate; the perlin layered and
  // gradient tree shaders paint their albedo from masks the bake shader cannot read
  return strstr(cls, "rendinst_tree") && !strstr(cls, "perlin_layered") && !strstr(cls, "gradient");
}

// coarsest lod with streamed vegetation mesh data: impostor cards and plod points carry
// no 3d information, so a far only pool yields null until a real mesh lod streams in
static ShaderMesh *pick_media_bake_mesh(RenderableInstanceLodsResource *res)
{
  for (int l = res->lods.size() - 1; l >= 0; --l)
  {
    if (any_lod_elem(res, l,
          [](const ShaderMesh::RElem &e, const char *cls) { return e.e && !e.vertexData->isEmpty() && media_bake_elem_filter(cls); }))
      return media_pool_mesh_at_lod(res, l);
  }
  return nullptr;
}

static bool ri_res_is_media_canopy(RenderableInstanceLodsResource *res, const char *name)
{
  // vegetation evidence from the visible geometry: a gameplay canopy prop describes the
  // bullet penetrable shape, not the rendered one, so ask the elems the bake itself draws
  bool anyTreeElem = false;
  for (int l = 0; l < res->lods.size() && !anyTreeElem; ++l)
    anyTreeElem = any_lod_elem(res, l, [](const ShaderMesh::RElem &, const char *cls) { return media_bake_elem_filter(cls); });
  if (!anyTreeElem)
    return false;
  const Point3 w = res->bbox.width();
  const float maxSide = max(w.x, max(w.y, w.z));
  // forest border mega cards and tiny ground plants use vegetation shaders too; neither is a canopy
  if (maxSide > 64.f || maxSide < 1.5f)
  {
    debug("daGI media volumes: skip <%s>, box %@ is not a canopy", name, w);
    return false;
  }
  // a standalone tree or bush ships an impostor lod; vegetation without one is decoration
  if (!res->hasImpostor())
  {
    debug("daGI media volumes: skip <%s>, no impostor lod", name);
    return false;
  }
  return true;
}

static void set_media_pool_bit(dag::Vector<uint32_t> &bits, uint32_t pool)
{
  if (pool / 32 >= bits.size())
    bits.resize(pool / 32 + 1, 0u);
  bits[pool / 32] |= 1u << (pool & 31);
}

// sqrt(3): a pitched or rolled crown swings its full corner reach onto any axis, and hmax
// of the matrix rows bounds the scale by the same factor
static constexpr float media_vol_rot_margin = 1.7321f;
// one home for the margin: the grid query inflation and the per instance test must agree,
// or the query can drop a root the test would keep
static float crown_reach(float reach3d) { return reach3d * media_vol_rot_margin; }

// -1 = not a canopy, or past the type id limit. riex_pool < 0 registers a riGen only type;
// a riExtra twin that materializes later takes a type of its own, which costs one spare
// brick and keeps every instance mapped to exactly one type
int RendInstMediaVolumes::registerType(RenderableInstanceLodsResource *res, const char *name, int riex_pool)
{
  if (riex_pool >= 0)
  {
    if (const int known = typeOfRiexPool(riex_pool); known >= 0)
      return known; // two riGen pools can share one twin
    // no collision = not in the riex grid: the sliver gather would miss these instances
    // while the sweep sees them, flipping density with the region size. riGen sources them
    rendinst::ScopedRIExtraReadLock rd;
    if (!rendinst::getRIGenExtraCollRes(riex_pool))
      riex_pool = -1;
  }
  if (!ri_res_is_media_canopy(res, name))
    return -1;
  const BBox3 &box = res->bbox;          // riex lbb is assigned from this, so both sources agree
  const int type = volumes.addType(box); // -1 past the type id limit, addType logerrs
  if (type < 0)
    return -1;
  // the gathers test root points and a refill region can be a thin slab above them: the
  // margin covers the widest crown lean per axis. the xz and full corner reach come back
  // from the volume so the gather margin cannot drift from its binning reach
  const float reachXZ = volumes.typeReach(type), reach3d = volumes.typeReach3d(type);
  const float reachY = max(fabsf(box[0].y), fabsf(box[1].y));
  const Point3 reachPerAxis(reachXZ, reachY, reachXZ);
  maxReachPerAxis = max(maxReachPerAxis, reachPerAxis);
  maxReach3d = max(maxReach3d, reach3d);
  // seeded at unit scale: the gather that would teach it is only reached past
  // gi_media_vol_riex_spatial_m, and a project's clip sizes may never get there
  maxScaledRiexReach = max(maxScaledRiexReach, crown_reach(maxReach3d));
  res->addRef(); // the pool's own ref can drop on a res reload mid session; the bake reads our copy
  G_ASSERT(type == int(types.size()));
  types.push_back({res, riex_pool, reachPerAxis, reach3d});
  if (riex_pool >= 0)
  {
    riPoolToType.emplace_new(poolKeyRiex(riex_pool), type);
    set_media_pool_bit(canopyRiexPoolBits, uint32_t(riex_pool));
  }
  return type;
}

void RendInstMediaVolumes::clear()
{
  riPoolToType.clear();
  collWaits.clear();
  for (Type &t : types)
    t.res->delRef();
  riexPoolsScanned = 0;
  types.clear();
  canopyRiexPoolBits.clear();
  maxRiGenScale = 1;
  maxReachPerAxis.zero();
  maxReach3d = 0;
  maxScaledRiexReach = 0;
  // volumes hands out the type ids: cleared together, or addType returns ids past the
  // end of the tables rebuilt here
  volumes.clear();
}

void RendInstMediaVolumes::updateTypes()
{
  // forests are riGen pools: classify from the riGen res and share the riExtra twin's
  // type where one already exists, so an instance from either side maps to the same brick
  for (int l = 0, le = rendinst::getRIGenLayersCount(); l < le; ++l)
  {
    const int poolsCount = rendinst::getRIGenPoolsCount(l);
    for (int p = 0; p < poolsCount; ++p)
    {
      const uint32_t key = poolKeyRiGen(l, p);
      if (riPoolToType.findVal(key))
        continue;
      // res ref and name slot are read under the layer lock: the loading thread fills the
      // tables this walks. the name may still be null for a pool with no name entry
      const char *name = nullptr;
      RenderableInstanceLodsResource *res = rendinst::getRIGenResAddRef(l, p, &name);
      if (!res)
        continue; // the res loads later, classify then
      int riexPool = -1;
      if (name)
      {
        // game job threads add riex pools under the write lock, and unlike its siblings
        // this accessor does not lock itself; ids only append, so the value stays valid
        rendinst::ScopedRIExtraReadLock rd;
        riexPool = rendinst::getRIGenExtraResIdx(name);
      }
      // memoized either way: the verdict cannot change while the tables live. a tooling
      // reload that swaps a res later is not revisited; the GI reset those flows take rebuilds these tables
      riPoolToType.emplace_new(key, registerType(res, name ? name : "<unnamed>", riexPool));
      res->delRef(); // registerType held its own ref for the stored copy
    }
  }
  for (uint32_t count = rendinst::getRiGenExtraResCount(); riexPoolsScanned < count; ++riexPoolsScanned)
    if (typeOfRiexPool(riexPoolsScanned) < 0)
    {
      CollisionResource *coll = nullptr;
      RenderableInstanceLodsResource *res = nullptr;
      {
        rendinst::ScopedRIExtraReadLock rd; // the pool vector reallocates under game thread adds
        coll = rendinst::getRIGenExtraCollRes(riexPoolsScanned);
        res = rendinst::getRIGenExtraRes(riexPoolsScanned);
        if (res)
          res->addRef(); // guards the reads below: a tooling reload swaps the pool res with no write
                         // lock, so the swap-vs-ref instant stays the reload API's own race
      }
      if (!res)
        break; // still materializing (the id publishes before the res): rescan from here later
      // the add stores res before collRes with no lock, so a fresh pool can look collision
      // less for an instant: park it for the recheck below instead of stalling the scan
      if (!coll)
      {
        collWaits.push_back({riexPoolsScanned});
        res->delRef();
        continue;
      }
      classifyRiexPool(riexPoolsScanned, res, coll);
    }
  // parked pools: collRes appeared, or the grace ran out and the pool classifies as it
  // is. no collision = not gatherable (decorative vegetation ships this way on purpose):
  // no type; a riGen twin keeps the type through the pos instance walk instead
  for (uint32_t i = 0; i < collWaits.size();)
  {
    CollWait &cw = collWaits[i];
    CollisionResource *coll = nullptr;
    RenderableInstanceLodsResource *res = nullptr;
    {
      rendinst::ScopedRIExtraReadLock rd;
      coll = rendinst::getRIGenExtraCollRes(cw.pool);
      res = rendinst::getRIGenExtraRes(cw.pool);
      if (res)
        res->addRef();
    }
    if (res && (coll || ++cw.tries > 16))
    {
      classifyRiexPool(cw.pool, res, coll);
      collWaits[i] = collWaits.back();
      collWaits.pop_back();
      continue;
    }
    if (res)
      res->delRef();
    ++i;
  }
}

// classification tail shared by the scan and the coll wait recheck; takes over the res ref
void RendInstMediaVolumes::classifyRiexPool(uint32_t pool, RenderableInstanceLodsResource *res, CollisionResource *coll)
{
  const char *name = rendinst::getRIGenExtraName(pool);
  if (!name)
    name = "<unnamed>";
  if (coll)
    registerType(res, name, int(pool));
  else if (ri_res_is_media_canopy(res, name))
    debug("daGI media volumes: skip collision-less riex pool <%s>, not gatherable", name);
  res->delRef(); // registerType held its own ref for the stored copy
}

static void push_media_vol(Tab<DaGIMediaVolumeInstance> &out, vec3f pos, quat4f rot, vec3f scl, int type)
{
  DaGIMediaVolumeInstance &inst = out.push_back();
  v_stu_p3(&inst.pos.x, pos);
  v_stu_p3(&inst.scale.x, scl);
  v_stu(&inst.rot.x, rot);
  inst.type = uint32_t(type);
}

static void push_media_vol(Tab<DaGIMediaVolumeInstance> &out, mat44f_cref tm, int type)
{
  vec3f pos;
  quat4f rot;
  vec4f scl;
  v_mat4_decompose(tm, pos, rot, scl);
  push_media_vol(out, pos, rot, scl, type);
}

void RendInstMediaVolumes::gather(const BBox3 &box, Tab<DaGIMediaVolumeInstance> &out)
{
  TIME_PROFILE(gather_media_volumes);
  const bbox3f vBox = v_ldu_bbox3(box);
  auto pushInReach = [&](const mat43f &m43, int type) {
    mat44f m;
    v_mat43_transpose_to_mat44(m, m43);
    bbox3f instBox = vBox;
    const vec4f scaleUpper = v_hmax3(v_max(v_max(v_abs(m43.row0), v_abs(m43.row1)), v_abs(m43.row2)));
    // the grid query is inflated by this same bound, so it cannot drop a root this test
    // would have taken: widen it here and the next query follows
    maxScaledRiexReach = max(maxScaledRiexReach, crown_reach(maxReach3d) * v_extract_x(scaleUpper));
    v_bbox3_extend(instBox, v_mul(v_splats(crown_reach(types[type].reach3d)), scaleUpper));
    if (v_bbox3_test_pt_inside(instBox, m.col3))
      push_media_vol(out, m, type);
  };
  const float spatialSide = gi_media_vol_riex_spatial_m.get();
  if (box.width().x * box.width().z < spatialSide * spatialSide)
  {
    // small regions (toroidal slivers): the grid query touches few tiles, cheaper than a
    // sweep of every pool span. the query grows by the widest scaled reach met so far; an
    // instance larger than that stays missed until a big region sweep, which visits every
    // pool span, teaches the margin its scale
    BBox3 queryBox = box;
    queryBox.inflate(maxScaledRiexReach);
    rendinst::riex_collidable_t handles;
    // one lock across the query and the reads: the grid answers only while its lock is
    // held, and a write in the gap can recycle a slot into a zeroed instance at the origin
    rendinst::ScopedRIExtraReadLock rd;
    rendinst::gatherRIGenExtraCollidable(handles, queryBox, 0, canopyRiexPoolBits, false /*read_lock*/);
    dag::ConstSpan<mat43f> tms;
    uint32_t lastPool = ~0u;
    int lastType = -1;
    for (rendinst::riex_handle_t h : handles)
    {
      const uint32_t pool = rendinst::handle_to_ri_type(h);
      if (pool != lastPool)
      {
        tms = rendinst::getAllRIGenExtra43FromPool(pool);
        lastType = typeOfRiexPool(pool);
        lastPool = pool;
      }
      const uint32_t inst = rendinst::handle_to_ri_inst(h);
      // delRIGenExtra erases the grid entry before it frees or shrinks the slot span, all
      // under the write lock this read lock excludes: a handle decodes to a live instance
      pushInReach(tms[inst], lastType);
    }
  }
  else
  {
    // big regions (full clip refills): sweeping the registered pools own tm spans beats a
    // grid walk that visits every object in the box to return a handful
    rendinst::ScopedRIExtraReadLock rd;
    for (int type = 0; type < (int)types.size(); ++type)
    {
      if (types[type].riexPool < 0)
        continue; // riGen only: the pos instance walk below gathers it
      dag::ConstSpan<mat43f> tms = rendinst::getAllRIGenExtra43FromPool(types[type].riexPool);
      for (const mat43f &m43 : tms)
        if (rendinst::riex_is_instance_valid(m43)) // a destroyed instance leaves a zeroed slot in the span
          pushInReach(m43, type);
    }
  }
  // riGen pos instances alias their riExtra res by name; the pool filter spares the walk
  // the unpack of grass and other non canopy pools. the walk prunes cells at the widest
  // scale met so far, which grows only when some walk reports a bigger instance; each
  // root then passes its own type's reach at its scale
  struct RiGenGather // one pointer of capture: the callback type holds 16 bytes
  {
    Tab<DaGIMediaVolumeInstance> &out;
    bbox3f box;
  } riGen{out, vBox};
  rendinst::foreachRIGenPosInstanceInBox(
    box, maxReachPerAxis * maxRiGenScale,
    [](int layer_ix, int pool_ix, void *self) { return ((RendInstMediaVolumes *)self)->typeOfRiGenPool(layer_ix, pool_ix) >= 0; },
    this,
    [this, &riGen](const rendinst::RendInstDesc &desc, vec3f pos, quat4f rot, vec3f scale) {
      const int type = typeOfRiGenPool(desc.layer, desc.pool);
      const vec4f scaleUpper = v_hmax3(v_abs(scale)); // yaw only: the per axis reach holds
      maxRiGenScale = max(maxRiGenScale, v_extract_x(scaleUpper));
      bbox3f instBox = riGen.box;
      v_bbox3_extend(instBox, v_mul(v_ldu_p3_safe(&types[type].reachPerAxis.x), scaleUpper));
      if (v_bbox3_test_pt_inside(instBox, pos))
        push_media_vol(riGen.out, pos, rot, scale, type);
    });
}

bool RendInstMediaVolumes::bakeType(int type, int global_frame_block_id)
{
  // no streamed drawable mesh lod is a normal state (impostor range, far only pools):
  // daGI2 paces the retry. the res is ref held, so no riex lock is needed
  auto unready = [&] {
    if (types[type].bakeTries < 256 && ++types[type].bakeTries == 256) // saturates: no wrap into a repeat log
      debug("daGI media volumes: type %d (riex pool %d, -1 = riGen only) still unbaked after 256 tries: no drawable mesh lod, or the "
            "bake never streams",
        type, types[type].riexPool);
    return false;
  };
  ShaderMesh *mesh = pick_media_bake_mesh(types[type].res);
  if (!mesh)
    return unready();
  // the voxelizer draws the vegetation elems with its own bake shader at the model origin:
  // the rendinst shaders take no part, so none of them needs a media pass variant
  const int prevFrameBlock = ShaderGlobal::getBlock(ShaderGlobal::LAYER_FRAME);
  ShaderGlobal::setBlock(global_frame_block_id, ShaderGlobal::LAYER_FRAME);
  // only NotStreamed asks daGI2 to retry: an empty brick is the correct terminal state,
  // nothing of this pool feeds the media
  const bool ready = vegVoxelizer.voxelize(*mesh, [](const ShaderMaterial &mat) {
    return media_bake_elem_filter(mat.getShaderClassName());
  }) != VegetationMediaVoxelizer::Status::NotStreamed;
  ShaderGlobal::setBlock(prevFrameBlock, ShaderGlobal::LAYER_FRAME);
  return ready ? true : unready();
}

bool RendInstMediaVolumes::bakePending(int global_frame_block_id)
{
  return volumes.bakePending([&](int type) { return bakeType(type, global_frame_block_id); });
}

void RendInstMediaVolumes::setRegionInstances(const BBox3 &box, float voxel_size)
{
  volumes.setRegionInstances(box, voxel_size, [&](const BBox3 &b, Tab<DaGIMediaVolumeInstance> &out) { gather(b, out); });
}

void RendInstMediaVolumes::debugPrepare(const Point3 &cam_pos)
{
  volumes.debugPrepare(cam_pos, [&](const BBox3 &b, Tab<DaGIMediaVolumeInstance> &out) { gather(b, out); });
}
