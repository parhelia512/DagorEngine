// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <float.h>
#include <util/dag_convar.h>
#include <memory/dag_framemem.h>
#include <vecmath/dag_vecMath.h>
#include <drv/3d/dag_renderTarget.h>
#include <drv/3d/dag_driverDesc.h>
#include <drv/3d/dag_matricesAndPerspective.h>
#include <drv/3d/dag_buffers.h>
#include <drv/3d/dag_rwResource.h>
#include <drv/3d/dag_texture.h>
#include <math/dag_TMatrix4.h>
#include <math/dag_half.h>
#include <math/dag_adjpow2.h>
#include <math/integer/dag_IPoint2.h>
#include <util/dag_stlqsort.h>
#include <util/dag_string.h>
#include <perfMon/dag_statDrv.h>
#include <shaders/dag_shaders.h>
#include <workCycle/dag_workCycle.h>
#include <voxelizedMedia/voxelizedMedia.h>
#include "shaders/dagi_media_volumes.hlsli"

static_assert(DaGIMediaVolumes::MAX_REGION_INSTANCES == DAGI_MEDIA_VOLS_MAX_REGION_INSTANCES,
  "the public cap and the record format it comes from must agree");

// target xz grid dimension per region; cells never get smaller than min_cell meters
CONSOLE_INT_VAL("gi", gi_media_vol_grid_dim, 32, 1, 256);
CONSOLE_FLOAT_VAL_MINMAX("gi", gi_media_vol_min_cell, 8.f, 1.f, 64.f);
CONSOLE_INT_VAL("gi", gi_media_vol_bakes_per_frame, 8, 1, 4096);
// debug trace of the source instances + bricks: 1 = exact brick dda, 2 = injection raymarch.
// the trace covers the insts closest instances inside dist around the camera: a straight
// cut through a forest means raise them
CONSOLE_INT_VAL("gi", gi_media_vols_debug, 0, 0, 2);
CONSOLE_FLOAT_VAL_MINMAX("gi", gi_media_vols_debug_dist, 128.f, 8.f, 512.f);
CONSOLE_INT_VAL("gi", gi_media_vols_debug_insts, 384, 16, 4096);
CONSOLE_FLOAT_VAL_MINMAX("gi", gi_media_vols_debug_step, 1.f, 0.05f, 8.f); // raymarch step of mode 2

#define GLOBAL_VARS_LIST                     \
  VAR(dagi_media_vol_bake)                   \
  VAR(voxelize_world_to_rasterize_space_mul) \
  VAR(voxelize_world_to_rasterize_space_add) \
  VAR(dagi_media_vol_grid_lt_sz)             \
  VAR(dagi_media_vol_grid_cell)              \
  VAR(dagi_media_vol_atlas_grid)             \
  VAR(dagi_media_vols_debug_count)           \
  VAR(dagi_media_vols_debug_mode)            \
  VAR(dagi_media_vols_debug_step)

#define VAR(a) static ShaderVariableInfo a##VarId(#a, true);
GLOBAL_VARS_LIST
#undef VAR

DaGIMediaVolumes::~DaGIMediaVolumes() { ShaderGlobal::set_int4(dagi_media_vol_grid_lt_szVarId, 0, 0, 0, 0); }

void DaGIMediaVolumes::initGpu()
{
  if (bitsBuf)
    return;
  bakeColor = dag::create_tex(nullptr, DAGI_MEDIA_VOL_RASTER, DAGI_MEDIA_VOL_RASTER, TEXCF_RTARGET, 1, "dagi_media_vol_bake_color");
  bitsBuf = dag::buffers::create_ua_sr_byte_address(DAGI_MEDIA_VOL_ALBEDO_OFS / 4 + 4, "dagi_media_vol_bake_bits");
  finalizeCs.reset(new_compute_shader("dagi_media_vol_finalize_cs"));
}

void DaGIMediaVolumes::ensureTypeCapacity(int count)
{
  if (count <= typeCapacity)
    return;
  // geometric growth. types pack as a near cubic 3d grid of bricks, so the count is not
  // bound by the volume depth limit, only each axis is
  const int maxAxis = max(int(d3d::get_driver_desc().maxvolsize / DAGI_MEDIA_VOL_BRICK), 1);
  const int req = max(count, max(typeCapacity * 2, 8));
  IPoint3 g;
  g.x = min(int(ceilf(cbrtf(float(req)))), maxAxis);
  g.y = min(int(ceilf(sqrtf(float(req) / g.x))), maxAxis);
  g.z = min((req + g.x * g.y - 1) / (g.x * g.y), maxAxis);
  // every axis is already at the volume limit: keep the atlas, addType rejects the type.
  // rebuilding here would also upload the not yet rejected type past the types buffer
  if (g.x * g.y * g.z <= typeCapacity)
    return;
  const IPoint3 oldGrid = atlasGrid;
  const int oldCapacity = typeCapacity;
  typeCapacity = g.x * g.y * g.z;
  atlasGrid = g;
  // managed res names must be unique among live resources, and the old atlas is alive
  // until the copy is done: suffix the growing capacity
  UniqueTex grown = dag::create_voltex(DAGI_MEDIA_VOL_BRICK * g.x, DAGI_MEDIA_VOL_BRICK * g.y, DAGI_MEDIA_VOL_BRICK * g.z,
    TEXCF_UNORDERED | TEXCF_CLEAR_ON_CREATE | TEXFMT_R8, DAGI_MEDIA_VOL_MIPS, String(0, "dagi_media_vol_atlas_%d", typeCapacity));
  UniqueBuf grownAlbedo = // unorm8 rgb per type
    dag::buffers::create_ua_sr_byte_address(typeCapacity, String(0, "dagi_media_vol_albedo_%d", typeCapacity),
      dag::buffers::Init::Zero);
  if (oldCapacity)
  {
    // baked bricks survive a regrow: copy each type cell into its new grid slot and keep
    // the baked bits, so only new types render
    for (int t = 0, te = min<int>(types.size(), oldCapacity); t < te; ++t)
    {
      const IPoint3 oc(t % oldGrid.x, (t / oldGrid.x) % oldGrid.y, t / (oldGrid.x * oldGrid.y));
      const IPoint3 nc(t % g.x, (t / g.x) % g.y, t / (g.x * g.y));
      for (int m = 0; m < DAGI_MEDIA_VOL_MIPS; ++m)
      {
        const int s = DAGI_MEDIA_VOL_BRICK >> m;
        d3d::update_sub_region(atlas.getVolTex(), m, oc.x * s, oc.y * s, oc.z * s, s, s, s, grown.getVolTex(), m, nc.x * s, nc.y * s,
          nc.z * s);
      }
    }
    albedoBuf.getBuf()->copyTo(grownAlbedo.getBuf(), 0, 0, oldCapacity * 4);
    // the debug tracer samples both in a ps, and a bake that finds no ready type returns
    // before the completion barriers: declare the pixel stage on the copies as well
    d3d::resource_barrier({grown.getVolTex(), RB_RO_SRV | RB_STAGE_COMPUTE | RB_STAGE_PIXEL, 0, 0});
    d3d::resource_barrier({grownAlbedo.getBuf(), RB_RO_SRV | RB_STAGE_COMPUTE | RB_STAGE_PIXEL});
  }
  atlas.close();
  atlas = UniqueTexWithShaderVar(eastl::move(grown), "dagi_media_vol_atlas");
  albedoBuf.close();
  albedoBuf = UniqueBufWithShaderVar(eastl::move(grownAlbedo), "dagi_media_vol_albedo");
  typesBuf.close();
  typesBuf = dag::buffers::create_persistent_sr_structured(sizeof(Point4), typeCapacity * 2, "dagi_media_vol_types");
  ShaderGlobal::set_int4(dagi_media_vol_atlas_gridVarId, g.x, g.y, g.z, g.x * g.y);
  for (int t = 0; t < types.size(); ++t)
    uploadType(t);
}

void DaGIMediaVolumes::uploadType(int type)
{
  const BBox3 &box = types[type].box;
  const Point3 inv(1.f / box.width().x, 1.f / box.width().y, 1.f / box.width().z);
  const Point4 data[2] = {Point4::xyz0(box[0]), Point4::xyz0(inv)};
  typesBuf.getBuf()->updateData(type * sizeof(data), sizeof(data), data, VBLOCK_WRITEONLY);
}

int DaGIMediaVolumes::addType(const BBox3 &local_box)
{
  if (types.size() >= (64 << 10)) // the instance record packs the type as u16
  {
    LOGERR_ONCE("daGI media volumes: over %d types", 64 << 10);
    return -1;
  }
  initGpu();
  BBox3 box = local_box;
  box[1] = max(box[1], box[0] + Point3(0.1f, 0.1f, 0.1f));
  const int type = types.size();
  // the box may sit anywhere around the model origin, and the instance rotates about that origin
  const Point3 corner = max(abs(box[0]), abs(box[1]));
  types.push_back({box, sqrtf(corner.x * corner.x + corner.z * corner.z), length(corner)});
  baked.push_back(false);
  ++pendingBakes;
  nextRetryFrame = 0; // a new type is ready to bake now: the stall gate must not skip it
  ensureTypeCapacity(types.size());
  if (types.size() > typeCapacity) // the atlas grid hit maxvolsize on every axis
  {
    LOGERR_ONCE("daGI media volumes: over %d types, the atlas cannot grow", typeCapacity);
    types.pop_back();
    baked.pop_back();
    --pendingBakes; // the rollback must keep the completed state O(1) early outs alive
    return -1;
  }
  uploadType(type);
  return type;
}

void DaGIMediaVolumes::clear()
{
  // the atlas and the type table go together: a stale brick would be sampled by whatever type
  // id lands on its cell next. the bake scratch (bitsBuf, bakeColor, finalizeCs) is type
  // independent and stays, so a re-registered set does not pay initGpu again
  types.clear();
  baked.clear();
  retryAfterFrame.clear();
  pendingBakes = 0;
  nextBakeScan = 0;
  nextRetryFrame = 0;
  waveQuietLeft = 0;
  lastGatherCount = 0;
  typeCapacity = 0;
  atlasGrid = IPoint3(0, 0, 0);
  atlas.close();
  albedoBuf.close();
  typesBuf.close();
  instancesBuf.close();
  gridBuf.close();
  gridIxBuf.close();
  resetDebugCache();
  // the injection must read nothing until a region is uploaded again
  ShaderGlobal::set_int4(dagi_media_vol_grid_lt_szVarId, 0, 0, 0, 0);
}

void DaGIMediaVolumes::resetDebugCache()
{
  debugInstsBuf.close();
  debugInstCount = 0;
  debugInstsPos = Point3(1e6f, 1e6f, 1e6f);
  ShaderGlobal::set_int(dagi_media_vols_debug_countVarId, 0);
}

// the pack paths index types[] raw: drop instances naming an unbaked type, or one that no
// longer exists (a project kept its own table across clear())
void DaGIMediaVolumes::keepBakedInstances(Tab<DaGIMediaVolumeInstance> &insts) const
{
  int kept = 0;
  for (const DaGIMediaVolumeInstance &in : insts)
    if (in.type < (uint32_t)types.size() && baked[in.type])
      insts[kept++] = in;
  insts.resize(kept);
}

void DaGIMediaVolumes::afterReset()
{
  for (int t = 0; t < types.size(); ++t)
  {
    baked[t] = false; // atlas and buffer contents do not survive a device reset
    uploadType(t);
  }
  pendingBakes = types.size();
  // the injection reads the one frame buffers only while the grid size is set, and their
  // contents are gone with the device: shut the gate until a region uploads again
  ShaderGlobal::set_int4(dagi_media_vol_grid_lt_szVarId, 0, 0, 0, 0);
  // the persistent debug buffer is lost with everything else: the standing camera cache
  // would keep the tracer reading it
  resetDebugCache();
  waveQuietLeft = 0;          // the rebake opens its own wave, a stale one would refill once for nothing
  mem_set_0(retryAfterFrame); // re-bakes must not wait out a stale pace gate
  nextRetryFrame = 0;
}

bool DaGIMediaVolumes::bakePending(const dagi_media_volume_render_cb &render_type)
{
  const bool bakedAny = bakeReadyTypes(render_type);
  // a bake burst spans many frames and every refill discards the gbuf accumulated radiance,
  // so the media scene is told only once the burst settles. a streaming stall between bakes
  // is not the end of it, and types whose textures never stream must not starve the signal:
  // a fixed quiet run closes the wave
  if (bakedAny)
  {
    // the debug list only re-gathers on camera travel, so a type that just got its brick would
    // stay missing from a standing view: drop the cache instead
    debugInstsPos = Point3(1e6f, 1e6f, 1e6f);
    if (!pendingBakes) // the last type just baked: nothing is left to coalesce, settle now
    {
      waveQuietLeft = 0;
      return true;
    }
    waveQuietLeft = BAKE_WAVE_QUIET_FRAMES;
    return false;
  }
  return waveQuietLeft && --waveQuietLeft == 0;
}

bool DaGIMediaVolumes::bakeReadyTypes(const dagi_media_volume_render_cb &render_type)
{
  if (!finalizeCs)
    return false;
  // one bake is 3*RASTER mesh renders, do not stall a frame with all of them
  int budget = gi_media_vol_bakes_per_frame.get();
  const uint32_t frame = ::dagor_frame_no();
  if (!pendingBakes) // the completed state must cost nothing on the per frame path
    return false;
  if (!render_type) // an unbaked type is dropped from every region, so this would be silent otherwise
  {
    LOGERR_ONCE("daGI media volumes: %d type(s) registered, but bakePending got no render callback to bake them", types.size());
    return false;
  }
  // a type that could not bake (still streaming, or never streaming for far only pools) retries
  // on a pace. nextRetryFrame is the earliest frame any pending type comes ready again, so a
  // streaming stall costs one compare here, not a scan of every type each frame
  if (frame < nextRetryFrame)
    return false;
  retryAfterFrame.resize(types.size(), 0u);
  const int count = types.size();
  DA_PROFILE_GPU;
  SCOPE_RENDER_TARGET;
  bool bakedAny = false;
  // the budget counts completed bakes: a type still waiting for streaming must not block
  // the ready ones behind it in the queue. a separate cap bounds the attempts, since a
  // failed attempt still pays a target set and the ready check; the cursor rotates so
  // capped frames reach every pending type over a few frames
  int attempts = max(budget * 4, 16);
  for (int i = 0; i < count && budget > 0 && attempts > 0; ++i)
  {
    const int t = (nextBakeScan + i) % count;
    if (baked[t] || frame < retryAfterFrame[t])
      continue;
    --attempts;
    if (bakeType(t, render_type))
    {
      bakedAny = true;
      --budget;
    }
    else
      retryAfterFrame[t] = frame + 16;
    if (!budget || !attempts)
      nextBakeScan = (t + 1) % count;
  }
  // the gate above skips the scan until a pending type is ready again; this runs only on a
  // frame that already scanned, so a stall pays the O(types) sweep once per pace, not per frame
  uint32_t earliest = ~0u;
  for (int t = 0; t < count; ++t)
    if (!baked[t])
      earliest = min(earliest, retryAfterFrame[t]);
  nextRetryFrame = earliest;
  return bakedAny;
}

bool DaGIMediaVolumes::bakeType(int type, const dagi_media_volume_render_cb &render_type)
{
  const BBox3 &box = types[type].box;
  const Point3 ext = box.width();
  // the type mesh renders once, at the origin, through the game's albedo voxelization mode
  // (3 voxelize axes in one instanced draw): the box maps to the full raster on every axis
  // (worldPosToVoxelSpace swizzles), and the albedo write is redirected into the sub
  // occupancy bits while dagi_media_vol_bake.w is on (dagi_media_vol_bake_write.dshl)
  const Point3 mul(2.f / ext.x, 2.f / ext.y, 2.f / ext.z);
  ShaderGlobal::set_float4(voxelize_world_to_rasterize_space_mulVarId, mul.x, mul.y, mul.z, 0);
  ShaderGlobal::set_float4(voxelize_world_to_rasterize_space_addVarId, -box[0].x * mul.x - 1.f, -box[0].y * mul.y - 1.f,
    -box[0].z * mul.z - 1.f, 0);
  d3d::zero_rwbufi(bitsBuf.getBuf());
  d3d::resource_barrier({bitsBuf.getBuf(), RB_FLUSH_UAV | RB_SOURCE_STAGE_COMPUTE | RB_STAGE_PIXEL});
  // only sets the raster size, the pass writes nothing but the bits uav
  d3d::set_render_target({}, DepthAccess::SampledRO, {{bakeColor.getTex2D(), 0, 0}});
  ShaderGlobal::set_int4(dagi_media_vol_bakeVarId, 0, 0, type, 1);
  const bool ok = render_type(type);
  ShaderGlobal::set_int4(dagi_media_vol_bakeVarId, 0, 0, type, 0);
  // the bake write leaves the bits bound as a ps uav, and dx11 refuses the same buffer as a
  // ps uav and the cs srv the finalize reads: drop the binding before the dispatch
  for (int i = 0; i < Driver3dRenderTarget::MAX_SIMRT; ++i)
    d3d::set_rwbuffer(STAGE_PS, i, nullptr);
  ShaderElement::invalidate_cached_state_block();
  if (!ok) // still streaming: retry on a later frame, a stub texture bake would be empty
    return false;
  d3d::resource_barrier({bitsBuf.getBuf(), RB_RO_SRV | RB_SOURCE_STAGE_PIXEL | RB_STAGE_COMPUTE});
  d3d::set_rwbuffer(STAGE_CS, DAGI_MEDIA_VOL_ALBEDO_UAV_NO, albedoBuf.getBuf());
  for (int m = 0; m < DAGI_MEDIA_VOL_MIPS; ++m)
    d3d::set_rwtex(STAGE_CS, DAGI_MEDIA_VOL_ATLAS_UAV_NO + m, atlas.getVolTex(), 0, m);
  finalizeCs->dispatch(1, 1, 1);
  d3d::set_rwbuffer(STAGE_CS, DAGI_MEDIA_VOL_ALBEDO_UAV_NO, nullptr);
  for (int m = 0; m < DAGI_MEDIA_VOL_MIPS; ++m)
    d3d::set_rwtex(STAGE_CS, DAGI_MEDIA_VOL_ATLAS_UAV_NO + m, nullptr, 0, 0);
  // the injection reads both in cs, the debug tracer in ps, and a bake shares its frame with
  // the draw that shows it: declare both stages, as the media scene does for the same reason
  d3d::resource_barrier({atlas.getVolTex(), RB_RO_SRV | RB_SOURCE_STAGE_COMPUTE | RB_STAGE_COMPUTE | RB_STAGE_PIXEL, 0, 0});
  d3d::resource_barrier({albedoBuf.getBuf(), RB_RO_SRV | RB_SOURCE_STAGE_COMPUTE | RB_STAGE_COMPUTE | RB_STAGE_PIXEL});
  baked[type] = true;
  --pendingBakes;
  return true;
}

// one-frame buffers grow on demand, recreation is safe (each lock discards)
static uint32_t *lock_dwords(UniqueBufWithShaderVar &buf, uint32_t dwords, const char *name)
{
  if (!buf || uint32_t(buf.getBuf()->getNumElements()) < dwords)
  {
    buf.close();
    buf = dag::buffers::create_one_frame_sr_byte_address(max(uint32_t(get_bigger_pow2(dwords)), 256u), name);
  }
  uint32_t *dst = nullptr;
  if (!buf.getBuf()->lock(0, dwords * 4, (void **)&dst, VBLOCK_DISCARD | VBLOCK_WRITEONLY) || !dst)
  {
    // every caller returns on this, leaving the region with no media at all: say so, or an
    // empty forest reads the same as one that legitimately has none
    LOGERR_ONCE("daGI media volumes: <%s> lock failed, the region keeps no media this refill", name);
    return nullptr;
  }
  return dst;
}

static bool upload_dwords(UniqueBufWithShaderVar &buf, const uint32_t *data, uint32_t dwords, const char *name)
{
  uint32_t *dst = lock_dwords(buf, dwords, name);
  if (!dst)
    return false;
  memcpy(dst, data, dwords * 4);
  buf.getBuf()->unlock();
  return true;
}

static void set_grid_buffers_bound(UniqueBufWithShaderVar &instances, UniqueBufWithShaderVar &grid, UniqueBufWithShaderVar &grid_ix,
  bool bound)
{
  UniqueBufWithShaderVar *bufs[] = {&instances, &grid, &grid_ix};
  for (UniqueBufWithShaderVar *b : bufs)
  {
    if (b->getVarId() == -1) // never created yet: nothing is bound to drop
      continue;
    if (bound)
      b->setVar();
    else
      ShaderGlobal::set_buffer(b->getVarId(), BAD_D3DRESID);
  }
}

static void pack_vol_instance(uint32_t *d, const DaGIMediaVolumeInstance &in)
{
  memcpy(d, &in.pos, sizeof(in.pos));
  uint16_t h[4];
  // nothing places a mirrored instance, so the abs normalizes a degenerate scale rather than
  // dropping a sign the sampler would need. the high clamp keeps the f16 finite: an inf scale
  // would invert to a zero sized volume and vanish
  v_float_to_half(h, v_clamp(v_abs(v_ldu(&in.scale.x)), v_splats(1e-3f), v_splats(65504.f)));
  d[3] = h[0] | (uint32_t(h[1]) << 16);
  d[4] = h[2] | (in.type << 16);
  // snorm8, rounded and decoded as (u8 - 128)/127.5 so that identity is exact
  vec4f q = v_clamp(v_ldu(&in.rot.x), v_splats(-1.f), V_C_ONE);
  alignas(16) uint32_t qi[4];
  v_sti(qi, v_cvti_vec4i(v_min(v_madd(q, v_splats(127.5f), v_splats(128.5f)), v_splats(255.f))));
  d[5] = qi[0] | (qi[1] << 8) | (qi[2] << 16) | (qi[3] << 24);
}

void DaGIMediaVolumes::debugPrepare(const Point3 &cam_pos, const dagi_media_volumes_cb &gather)
{
  if (!gi_media_vols_debug.get())
  {
    resetDebugCache(); // the mode was on: its buffer must not stay for the rest of the run
    return;
  }
  const float d = gi_media_vols_debug_dist.get();
  const int wanted = gi_media_vols_debug_insts.get();
  // the persistent buffer holds; re-gather each meter of travel, or when either knob moves:
  // a standing camera must still see a changed range or count take effect
  if (lengthSq(cam_pos - debugInstsPos) < 1.f && d == debugGatherDist && wanted == debugGatherInsts)
    return;
  debugInstsPos = cam_pos;
  debugGatherDist = d;
  debugGatherInsts = wanted;
  Tab<DaGIMediaVolumeInstance> gathered(framemem_ptr());
  if (gather)
    gather(BBox3(cam_pos - Point3(d, d, d), cam_pos + Point3(d, d, d)), gathered);
  // drop unbaked before the nearest are picked, or during a bake wave they take the kept
  // prefix and hide the baked instances behind them
  keepBakedInstances(gathered);
  // keep the closest gi_media_vols_debug_insts only: nth_element bounds the cost, the tracer
  // slab tests the list and orders the hits per pixel, so the kept prefix needs no full sort
  if (gathered.size() > wanted)
    stlsort::nth_element(gathered.begin(), gathered.begin() + wanted, gathered.end(),
      [&](const DaGIMediaVolumeInstance &a, const DaGIMediaVolumeInstance &b) {
        return lengthSq(a.pos - cam_pos) < lengthSq(b.pos - cam_pos);
      });
  constexpr uint32_t instDwords = DAGI_MEDIA_VOL_INST_SIZE / 4;
  const int take = min<int>(gathered.size(), wanted);
  Tab<uint32_t> packed(framemem_ptr());
  packed.resize(take * instDwords);
  for (int i = 0; i < take; ++i)
    pack_vol_instance(&packed[i * instDwords], gathered[i]);
  debugInstCount = take;
  ShaderGlobal::set_int(dagi_media_vols_debug_countVarId, debugInstCount);
  if (!debugInstCount)
    return;
  // persistent buffer: prepare runs on camera motion, the draw runs every frame, so a one
  // frame buffer would read undefined between gathers
  if (!debugInstsBuf || uint32_t(debugInstsBuf.getBuf()->getNumElements()) < packed.size())
  {
    debugInstsBuf.close();
    debugInstsBuf = dag::buffers::create_persistent_sr_byte_address(max(uint32_t(get_bigger_pow2(packed.size())), 256u),
      "dagi_media_vols_debug_insts");
  }
  if (!debugInstsBuf.getBuf()->updateData(0, packed.size() * 4, packed.data(), VBLOCK_WRITEONLY))
  {
    debugInstCount = 0;
    ShaderGlobal::set_int(dagi_media_vols_debug_countVarId, 0);
  }
}

void DaGIMediaVolumes::debugRender()
{
  if (!gi_media_vols_debug.get() || !debugInstCount)
    return;
  if (!volsDebugRenderer.getElem())
    volsDebugRenderer.init("dagi_media_vols_debug");
  ShaderGlobal::set_int(dagi_media_vols_debug_modeVarId, gi_media_vols_debug.get());
  ShaderGlobal::set_float(dagi_media_vols_debug_stepVarId, gi_media_vols_debug_step.get());
  DA_PROFILE_GPU;
  volsDebugRenderer.render();
}

void DaGIMediaVolumes::setRegionInstances(const BBox3 &box, float voxel_size, const dagi_media_volumes_cb &gather)
{
  ShaderGlobal::set_int4(dagi_media_vol_grid_lt_szVarId, 0, 0, 0, 0);
  // on early exit grid framemem buffers are not updated, unbind to avoid accessing them
  set_grid_buffers_bound(instancesBuf, gridBuf, gridIxBuf, false);
  if (!gather || types.empty())
    return;
  Tab<DaGIMediaVolumeInstance> gathered(framemem_ptr());
  // consecutive regions hold similar counts: the estimate spares the growth copies
  gathered.reserve(lastGatherCount + lastGatherCount / 4 + 64);
  gather(box, gathered);
  if (gathered.size() > DAGI_MEDIA_VOLS_MAX_REGION_INSTANCES) // the record and slot formats cap the region
  {
    LOGERR_ONCE("daGI media volumes: %d instances in one region, extra dropped", gathered.size());
    gathered.resize(DAGI_MEDIA_VOLS_MAX_REGION_INSTANCES);
  }
  lastGatherCount = gathered.size(); // the reserve estimate wants the raw gather, not the filtered one
  keepBakedInstances(gathered);      // every region, not only while bakes are pending
  uploadInstances(gathered, voxel_size);
}

void DaGIMediaVolumes::uploadInstances(dag::ConstSpan<DaGIMediaVolumeInstance> src, float voxel_size)
{
  // setRegionInstances already zeroed the grid var and returned on an empty type table:
  // an early out here leaves that zero, the success path writes the real grid over it
  if (src.empty())
    return;
  dag::ConstSpan<DaGIMediaVolumeInstance> insts = src; // the caller filtered to baked types

  // an instance bins into EVERY cell its volume can touch: reach at its scale, plus the
  // half voxel the overlap test accepts around a center lookup (a full voxel margin
  // multiplies the touched cells on coarse clips). the reader looks up exactly one cell.
  // v_ldu over-reads into the next member by contract, all loads stay inside the struct
  Tab<IPoint4> ranges(framemem_ptr()); // per instance xz cell range, filled after the grid is known
  ranges.resize(insts.size());
  vec4f bMin4 = v_splats(FLT_MAX), bMax4 = v_neg(bMin4);
  const vec4f vVox = v_splats(0.5f * voxel_size);
  // a yaw only rotation keeps the xz reach; pitch or roll (fallen riex trees) can swing
  // the box height into xz, those instances bin with the full corner distance
  const auto instReach = [&](const DaGIMediaVolumeInstance &in) {
    const Type &t = types[in.type];
    return in.rot.x != 0.f || in.rot.z != 0.f ? t.reach3d : t.reach;
  };
  for (const DaGIMediaVolumeInstance &in : insts)
  {
    vec4f margin = v_add(v_mul(v_splats(instReach(in)), v_hmax3(v_abs(v_ldu(&in.scale.x)))), vVox);
    vec4f p = v_ldu(&in.pos.x);
    bMin4 = v_min(bMin4, v_sub(p, margin));
    bMax4 = v_max(bMax4, v_add(p, margin));
  }
  // semi-fixed grid: at most grid_dim cells per axis, cells never smaller than min_cell
  const vec4f gridDim = v_splats(gi_media_vol_grid_dim.get()), minCell = v_splats(gi_media_vol_min_cell.get());
  const vec4f invCell4 = v_rcp(v_max(minCell, v_div(v_sub(bMax4, bMin4), gridDim)));
  const vec4i cMin4 = v_cvti_vec4i(v_floor(v_mul(bMin4, invCell4)));
  const vec4i cMax4 = v_cvti_vec4i(v_floor(v_mul(bMax4, invCell4)));
  const IPoint2 cMin(v_extract_xi(cMin4), v_extract_zi(cMin4));
  const int gw = v_extract_xi(cMax4) - cMin.x + 1, gh = v_extract_zi(cMax4) - cMin.y + 1;
  const uint32_t cells = uint32_t(gw) * gh;
  Tab<uint32_t> cellStart(framemem_ptr());
  cellStart.resize(cells + 1);
  mem_set_0(cellStart);
  for (int i = 0; i < insts.size(); ++i)
  {
    const DaGIMediaVolumeInstance &in = insts[i];
    vec4f margin = v_add(v_mul(v_splats(instReach(in)), v_hmax3(v_abs(v_ldu(&in.scale.x)))), vVox);
    vec4f p = v_ldu(&in.pos.x);
    vec4i c0 = v_cvti_vec4i(v_floor(v_mul(v_sub(p, margin), invCell4)));
    vec4i c1 = v_cvti_vec4i(v_floor(v_mul(v_add(p, margin), invCell4)));
    const IPoint4 r(v_extract_xi(c0) - cMin.x, v_extract_zi(c0) - cMin.y, v_extract_xi(c1) - cMin.x, v_extract_zi(c1) - cMin.y);
    ranges[i] = r;
    for (int cy = r.y; cy <= r.w; ++cy)
      for (int cx = r.x; cx <= r.z; ++cx)
        ++cellStart[cy * gw + cx + 1];
  }
  // the running sum is wider than a slot index on purpose: a full region (65535 instances)
  // whose instances each span the whole grid would wrap 32 bits before the cap below applies.
  // saturating the prefix at the cap keeps every offset under it exact, the scatter drops the rest
  constexpr uint32_t maxSlots = (1 << DAGI_MEDIA_VOL_GRID_OFS_BITS) - 1;
  uint64_t total = 0;
  for (uint32_t i = 1; i <= cells; ++i)
  {
    total += cellStart[i];
    cellStart[i] = uint32_t(min<uint64_t>(total, maxSlots));
  }
  const uint32_t slots = uint32_t(min<uint64_t>(total, maxSlots));
  if (total > slots)
    LOGERR_ONCE("daGI media volumes: %u grid slots in one region, extra dropped", unsigned(min<uint64_t>(total, ~0u)));
  Tab<uint32_t> slotIx(framemem_ptr()); // the u16 slot indices pack as dwords for the upload
  slotIx.resize((slots + 1) / 2);
  if (slotIx.size())
    slotIx.back() = 0; // the odd tail half stays deterministic
  uint16_t *slotIxHalf = (uint16_t *)slotIx.data();
  // the records pack straight into the locked one frame buffer, in gather order: write
  // combined memory takes the sequential stores fine and a staging pass would double them
  uint32_t *packedDst = lock_dwords(instancesBuf, insts.size() * (DAGI_MEDIA_VOL_INST_SIZE / 4), "dagi_media_vol_instances");
  if (!packedDst)
    return;
  for (int i = 0; i < insts.size(); ++i)
  {
    const DaGIMediaVolumeInstance &in = insts[i];
    pack_vol_instance(packedDst + i * (DAGI_MEDIA_VOL_INST_SIZE / 4), in);
    const IPoint4 &r = ranges[i];
    for (int cy = r.y; cy <= r.w; ++cy)
      for (int cx = r.x; cx <= r.z; ++cx)
      {
        const uint32_t at = cellStart[cy * gw + cx]++;
        if (at < slots)
          slotIxHalf[at] = uint16_t(i);
      }
  }
  instancesBuf.getBuf()->unlock();
  // after the scatter cellStart[c] holds the end of cell c: fold into offset | count words in place
  constexpr uint32_t maxCount = DAGI_MEDIA_VOL_GRID_MAX_COUNT;
  bool clamped = false;
  for (uint32_t i = 0, prev = 0; i < cells; ++i)
  {
    const uint32_t end = min(cellStart[i], slots), count = end - min(prev, end);
    clamped |= count > maxCount;
    cellStart[i] = min(prev, slots) | (min(count, maxCount) << DAGI_MEDIA_VOL_GRID_OFS_BITS);
    prev = end;
  }
  if (clamped)
    LOGERR_ONCE("daGI media volumes: over %d instances in one grid cell, extra dropped", maxCount);
  if (!upload_dwords(gridIxBuf, slotIx.data(), slotIx.size(), "dagi_media_vol_grid_ix") ||
      !upload_dwords(gridBuf, cellStart.data(), cells, "dagi_media_vol_grid"))
    return;
  set_grid_buffers_bound(instancesBuf, gridBuf, gridIxBuf, true);
  ShaderGlobal::set_float4(dagi_media_vol_grid_cellVarId, v_extract_x(invCell4), v_extract_z(invCell4), 0, 0);
  ShaderGlobal::set_int4(dagi_media_vol_grid_lt_szVarId, cMin.x, cMin.y, gw, gh);
}
