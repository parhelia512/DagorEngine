// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "riGen/riGenData.h"

#include <shaders/dag_shaderBlock.h>
#include <shaders/dag_shaderVar.h>
#include <drv/3d/dag_viewScissor.h>
#include <drv/3d/dag_renderTarget.h>
#include <drv/3d/dag_matricesAndPerspective.h>
#include <drv/3d/dag_texture.h>
#include <drv/3d/dag_lock.h>
#include <drv/3d/dag_info.h>
#include <math/dag_bounds2.h>
#include <math/dag_TMatrix4.h>
#include <image/dag_texPixel.h>
#include <memory/dag_framemem.h>
#include <rendInst/rendInstGenRender.h>
#include <rendInst/clipmapShadow.h>

static constexpr float CLIPMAP_SHADOW_DELTA_MUL = 1.f / 16.f;
static constexpr int CLIPMAP_SHADOW_SCROLL_TEXELS = 128;

static int clipmap_shadow_near_far_tc_offsetVarId = -1;
static int clipmap_shadowsBlockId = -1;
static int clipmapShadowTexVarId = -1;
static int clipmapShadowFadeOutVarId = -1;

static constexpr int DRAW_ESTIMATION_RECURSION_DEPTH_STEP = 2;

static BBox2 clipmap_shadow_query_box(const ToroidalQuadRegion &reg, float texel_size)
{
  BBox2 boxReg(point2(reg.texelsFrom) * texel_size, point2(reg.texelsFrom + reg.wd) * texel_size);
  const float boxExpand = 100.0f;
  boxReg[0] -= Point2(1, 1) * boxExpand;
  boxReg[1] += Point2(1, 1) * boxExpand;
  return boxReg;
}

static bool is_toroidal_region_indivisible(const ToroidalQuadRegion &reg) { return reg.wd.x <= 1 && reg.wd.y <= 1; }

static void split_region_by_draw_budget(Tab<ToroidalQuadRegion> &dest, const ToroidalQuadRegion &reg, float texel_size, int cascade_no,
  int budget, int current_depth = 0)
{
  bool indivisible = is_toroidal_region_indivisible(reg);
  bool checkpoint = current_depth != 0 && current_depth % DRAW_ESTIMATION_RECURSION_DEPTH_STEP == 0;

  if (indivisible || checkpoint)
  {
    int regionRequiredDraws = 0;
    rendinst::render::tryRenderRIGenShadowsToClipmap(clipmap_shadow_query_box(reg, texel_size), cascade_no, 0, regionRequiredDraws);
    if (indivisible || regionRequiredDraws <= budget)
    {
      append_items(dest, 1, &reg);
      return;
    }
  }

  bool splitX = reg.wd.x >= reg.wd.y;
  int half = (splitX ? reg.wd.x : reg.wd.y) / 2;
  IPoint2 wdA = splitX ? IPoint2(half, reg.wd.y) : IPoint2(reg.wd.x, half);
  IPoint2 wdB = splitX ? IPoint2(reg.wd.x - half, reg.wd.y) : IPoint2(reg.wd.x, reg.wd.y - half);
  IPoint2 ofs = splitX ? IPoint2(half, 0) : IPoint2(0, half);
  split_region_by_draw_budget(dest, ToroidalQuadRegion(reg.lt, wdA, reg.texelsFrom), texel_size, cascade_no, budget,
    current_depth + 1);
  split_region_by_draw_budget(dest, ToroidalQuadRegion(reg.lt + ofs, wdB, reg.texelsFrom + ofs), texel_size, cascade_no, budget,
    current_depth + 1);
}

static int subtract_drawn_area_from_region(const ToroidalQuadRegion &region, const ToroidalQuadRegion &drawn_area,
  ToroidalQuadRegion out[4])
{
  int ox0 = max(region.lt.x, drawn_area.lt.x);
  int oy0 = max(region.lt.y, drawn_area.lt.y);
  int ox1 = min(region.lt.x + region.wd.x, drawn_area.lt.x + drawn_area.wd.x);
  int oy1 = min(region.lt.y + region.wd.y, drawn_area.lt.y + drawn_area.wd.y);
  if (ox0 >= ox1 || oy0 >= oy1)
    return -1;

  // region minus the [ox0,ox1)x[oy0,oy1) hole, as up to 4 non-overlapping strips: full-width top
  // and bottom, then left and right narrowed to the hole's own row band so the four never overlap.
  int n = 0;
  if (region.lt.y < oy0)
    out[n++] = ToroidalQuadRegion(region.lt, IPoint2(region.wd.x, oy0 - region.lt.y), region.texelsFrom);
  if (oy1 < region.lt.y + region.wd.y)
    out[n++] = ToroidalQuadRegion(IPoint2(region.lt.x, oy1), IPoint2(region.wd.x, region.lt.y + region.wd.y - oy1),
      region.texelsFrom + IPoint2(0, oy1 - region.lt.y));
  if (region.lt.x < ox0)
    out[n++] = ToroidalQuadRegion(IPoint2(region.lt.x, oy0), IPoint2(ox0 - region.lt.x, oy1 - oy0),
      region.texelsFrom + IPoint2(0, oy0 - region.lt.y));
  if (ox1 < region.lt.x + region.wd.x)
    out[n++] = ToroidalQuadRegion(IPoint2(ox1, oy0), IPoint2(region.lt.x + region.wd.x - ox1, oy1 - oy0),
      region.texelsFrom + IPoint2(ox1 - region.lt.x, oy0 - region.lt.y));
  return n;
}

static void remove_drawn_area(Tab<ToroidalQuadRegion> &regions, const ToroidalQuadRegion &drawn_area)
{
  for (int i = 0; i < regions.size();)
  {
    ToroidalQuadRegion region = regions[i]; // copy: append_items below may reallocate regions
    ToroidalQuadRegion remainder[4];
    int numRemainder = subtract_drawn_area_from_region(region, drawn_area, remainder);

    if (numRemainder < 0)
    {
      ++i;
      continue;
    }
    if (numRemainder == 0)
    {
      erase_items(regions, i, 1);
      continue;
    }

    regions[i] = remainder[0];
    for (int r = 1; r < numRemainder; ++r)
      append_items(regions, 1, &remainder[r]);
    ++i;
  }
}

void ClipmapShadow::setUpSampler() const
{
  d3d::SamplerInfo smpInfo;
  smpInfo.address_mode_u = smpInfo.address_mode_v = smpInfo.address_mode_w = d3d::AddressMode::Clamp;
  smpInfo.border_color = d3d::BorderColor::Color::OpaqueWhite;
  smpInfo.anisotropic_max = 1;
  ShaderGlobal::set_sampler(get_shader_variable_id("clipmap_shadow_tex_samplerstate", true), d3d::request_sampler(smpInfo));
}

void ClipmapShadow::init(int shadowSize, int async_update_draws_budget)
{
  asyncUpdateDrawsBudget = async_update_draws_budget;

  lookDownVtm.setcol(0, 1, 0, 0);
  lookDownVtm.setcol(1, 0, 0, 1);
  lookDownVtm.setcol(2, 0, 1, 0);
  lookDownVtm.setcol(3, 0, 0, 0);
  lookDownVtm = orthonormalized_inverse(lookDownVtm);

  clipmapShadowSize = shadowSize;
  clipmap_shadowsBlockId = ShaderGlobal::getBlockId("clipmap_shadows");

  unsigned clipmapShadowFlags = TEXFMT_R8;
  if (!(d3d::get_texformat_usage(TEXFMT_R8, D3DResourceType::TEX) & d3d::USAGE_RTARGET))
  {
    debug("l8  format not supported - reduce clipmapshadow by half");
    clipmapShadowSize /= 2;
    clipmapShadowFlags = TEXFMT_A8R8G8B8;
  }

  clipmapShadowTex = dag::create_tex(nullptr, clipmapShadowSize * NUM_CLIPMAP_SHADOW_CASCADES, clipmapShadowSize,
    clipmapShadowFlags | TEXCF_RTARGET, 1, "clipmapShadowTex", RESTAG_RENDINST);

  d3d_err(clipmapShadowTex.getTex2D());

  setUpSampler();

  clipmapShadowTexVarId = get_shader_variable_id("clipmap_shadow_tex");
  worldToClipmapShadowVarId[0] = get_shader_variable_id("world_to_far_clipmap_shadow");
  worldToClipmapShadowVarId[1] = get_shader_variable_id("world_to_near_clipmap_shadow");
  clipmap_shadow_near_far_tc_offsetVarId = get_shader_variable_id("clipmap_shadow_near_far_tc_offset");

  clipmapShadowFadeOutVarId = get_shader_variable_id("clipmap_shadow_fade_out", true);

  for (int j = 0; j < NUM_CLIPMAP_SHADOW_CASCADES; ++j)
  {
    torHelpers[j].texSize = clipmapShadowSize;
    torHelpers[j].curOrigin = IPoint2(-1000000, 100000);

    worldToToroidal[j] = Color4(0, 0, 0, 0);
    uvOffset[j] = Point2(0, 0);
    deferredRegions[j].clear();
  }

  invalidate();
  setDistance(5000, 0.3, 0.35);
}

void ClipmapShadow::setDistance(float averageFarPlane, float delta, float near_cascade_scale)
{
  for (unsigned int cascadeNo = 0; cascadeNo < NUM_CLIPMAP_SHADOW_CASCADES; cascadeNo++)
  {
    float scale = (cascadeNo == 0) ? 1.f : near_cascade_scale;
    clipmapShadowWorldSize[cascadeNo] =
      2.f * averageFarPlane * scale * (1.f + CLIPMAP_SHADOW_DELTA_MUL / (1.f - CLIPMAP_SHADOW_DELTA_MUL));
    // clipmapShadowDelta[cascadeNo] = 0.5f * clipmapShadowWorldSize[cascadeNo] * CLIPMAP_SHADOW_DELTA_MUL;
  }
  invalidate();

  // Set clipmap shadow to fade out at max RI distance roughly.

  // A*max_dist + B = 0
  // A*min_dist + B = 1
  // A = 1/(min_dist-maxdist)
  // B = -max_dist/(min_dist-maxdist)
  // A = 1/(maxdist*delta-maxdist)
  // B = 1/(1-delta)

  float deltaRcp = 1.0 - delta;
  debug("deltaShadowRCP = %g A=%g B=%g", 1.f - deltaRcp, -1. / averageFarPlane / (1.f - deltaRcp), 1. / (1.f - deltaRcp));
  ShaderGlobal::set_float4(clipmapShadowFadeOutVarId, -1.f / (averageFarPlane * (1.f - deltaRcp)), 1.f / (1.f - deltaRcp), 0.f, 0.f);
}

void ClipmapShadow::close() { clipmapShadowTex.close(); }

void ClipmapShadow::switchOff()
{
  close();
  invalidate();
  Color4 worldToClipEmpty(1000000, 1000000, 1e6f, 1e6f);
  TexImage32 img[2];
  img[0].w = img[0].h = 1;
  img[1].w = img[1].h = -1;
  clipmapShadowTex = dag::create_tex(img, 1, 1, TEXFMT_A8R8G8B8, 1, "clipmapShadowWhiteTex", RESTAG_RENDINST);
  clipmapShadowSize = 0;
  d3d_err(clipmapShadowTex.getTex2D());
  setUpSampler();

  clipmapShadowTexVarId = get_shader_variable_id("clipmap_shadow_tex", true);
  ShaderGlobal::set_texture(clipmapShadowTexVarId, clipmapShadowTex.getTexId());
  ShaderGlobal::set_float4(get_shader_variable_id("world_to_far_clipmap_shadow", true), worldToClipEmpty);
  ShaderGlobal::set_float4(get_shader_variable_id("world_to_near_clipmap_shadow", true), worldToClipEmpty);
}

void ClipmapShadow::reset()
{
  if (clipmapShadowTex.getTex2D())
  {
    // If clipmap shadow is turned off, the texture is not a render target, but a white pixel
    TextureInfo ti;
    clipmapShadowTex->getinfo(ti);
    if (ti.cflg & TEXCF_RTARGET)
    {
      d3d::GpuAutoLock gpuLock;
      Driver3dRenderTarget prevRt;
      d3d::get_render_target(prevRt);
      d3d::set_render_target({}, DepthAccess::RW, {{clipmapShadowTex.getTex2D(), 0, 0}});
      d3d::clearview(CLEAR_TARGET, 0xFFFFFFFF, 1.f, 0);
      d3d::set_render_target(prevRt);
      d3d::resource_barrier({clipmapShadowTex.getTex2D(), RB_RO_SRV | RB_STAGE_PIXEL, 0, 0});
    }
  }
  invalidate();
}

void ClipmapShadow::invalidate()
{
  for (int j = 0; j < NUM_CLIPMAP_SHADOW_CASCADES; ++j)
  {
    deferredRegions[j].clear();
    torHelpers[j].curOrigin = IPoint2(-10000000, -1000000);
  }
}

bool ClipmapShadow::getBBox(BBox2 &box) const
{
  if (!clipmapShadowTex.getTex2D() || !clipmapShadowSize)
    return false;
  float texelSize = clipmapShadowWorldSize[0] / (float)clipmapShadowSize;
  Point2 halfSize(0.5f * clipmapShadowWorldSize[0], 0.5f * clipmapShadowWorldSize[0]);
  box[0] = Point2(torHelpers[0].curOrigin) * texelSize - halfSize;
  box[1] = Point2(torHelpers[0].curOrigin) * texelSize + halfSize;
  return true;
}


static bool clipmap_shadow_cascade_crossed_threshold(const ToroidalHelper &tor_helper, const Point3 &view_pos, float texel_size)
{
  IPoint2 center_pos;
  center_pos.x = 4 * floorf(view_pos.x / (4.0f * texel_size));
  center_pos.y = 4 * floorf(view_pos.z / (4.0f * texel_size));
  return !((abs(tor_helper.curOrigin.x - center_pos.x) < CLIPMAP_SHADOW_SCROLL_TEXELS) &&
           (abs(tor_helper.curOrigin.y - center_pos.y) < CLIPMAP_SHADOW_SCROLL_TEXELS));
}

bool ClipmapShadow::update(float min_height, float max_height, const Point3 &view_pos)
{
  if (!clipmapShadowTex.getTex2D() || !RendInstGenData::renderResRequired)
    return false;

  TextureInfo ti;
  clipmapShadowTex->getinfo(ti);
  if (!(ti.cflg & TEXCF_RTARGET))
    return false;

  bool anyWork = false;
  for (int cascadeNo = 0; cascadeNo < NUM_CLIPMAP_SHADOW_CASCADES; ++cascadeNo)
  {
    float texelSize = clipmapShadowWorldSize[cascadeNo] / (float)clipmapShadowSize;
    if (clipmap_shadow_cascade_crossed_threshold(torHelpers[cascadeNo], view_pos, texelSize) || !deferredRegions[cascadeNo].empty())
    {
      anyWork = true;
      break;
    }
  }
  if (!anyWork)
    return false;

  SCOPE_RENDER_TARGET;
  SCOPE_VIEW_PROJ_MATRIX;

  int currentBudget = asyncUpdateDrawsBudget;
  for (int cascadeNo = NUM_CLIPMAP_SHADOW_CASCADES - 1; cascadeNo >= 0; cascadeNo--)
  {
    ToroidalHelper &torHelper = torHelpers[cascadeNo];
    float texelSize = clipmapShadowWorldSize[cascadeNo] / (float)clipmapShadowSize;

    ToroidalGatherCallback::RegionTab immediateRegions;

    if (clipmap_shadow_cascade_crossed_threshold(torHelper, view_pos, texelSize))
    {
      IPoint2 center_pos;
      center_pos.x = 4 * floorf(view_pos.x / (4.0f * texelSize));
      center_pos.y = 4 * floorf(view_pos.z / (4.0f * texelSize));

      const int pixelTreshold = CLIPMAP_SHADOW_SCROLL_TEXELS;
      IPoint2 newTexelOrigin = torHelper.curOrigin;
      if (abs(torHelper.curOrigin.x - center_pos.x) > abs(torHelper.curOrigin.y - center_pos.y))
        newTexelOrigin.x = center_pos.x;
      else
        newTexelOrigin.y = center_pos.y;

      if (max(abs(torHelper.curOrigin.x - newTexelOrigin.x), abs(torHelper.curOrigin.y - newTexelOrigin.y)) > pixelTreshold * 2)
        newTexelOrigin = center_pos;

      IPoint2 prevMainOrigin = torHelper.mainOrigin;
      ToroidalGatherCallback cb(immediateRegions);
      toroidal_update(newTexelOrigin, torHelper, 0.33f * clipmapShadowSize, cb);

      if (torHelper.mainOrigin != prevMainOrigin)
        deferredRegions[cascadeNo].clear();

      float toroidalWorldSize = clipmapShadowWorldSize[cascadeNo];
      Point2 worldSpaceOrigin = point2(torHelper.curOrigin) * texelSize;
      worldToToroidal[cascadeNo] = Color4(1.f / toroidalWorldSize, 1.f / toroidalWorldSize,
        0.5f - worldSpaceOrigin.x / toroidalWorldSize, 0.5f - worldSpaceOrigin.y / toroidalWorldSize);
      ShaderGlobal::set_float4(worldToClipmapShadowVarId[cascadeNo], worldToToroidal[cascadeNo]);

      uvOffset[cascadeNo] = -point2((torHelper.mainOrigin - torHelper.curOrigin) % torHelper.texSize) / torHelper.texSize;
    }

    if (immediateRegions.empty() && deferredRegions[cascadeNo].empty())
      continue;

    d3d::set_render_target({}, DepthAccess::RW, {{clipmapShadowTex.getTex2D(), 0, 0}});
    d3d::settm(TM_VIEW, lookDownVtm);

    auto clearQuad = [&](const ToroidalQuadRegion &reg) {
      d3d::setview(clipmapShadowSize * cascadeNo + reg.lt.x, reg.lt.y, reg.wd.x, reg.wd.y, 0, 1);
      d3d::clearview(CLEAR_TARGET, 0xFFFFFFFF, 1.f, 0);
    };

    auto tryRenderQuad = [&](const ToroidalQuadRegion &reg, int budget, int &draws) {
      clearQuad(reg);
      BBox2 boxReg(point2(reg.texelsFrom) * texelSize, point2(reg.texelsFrom + reg.wd) * texelSize);
      TMatrix4 proj = matrix_ortho_off_center_lh(boxReg[0].x, boxReg[1].x, boxReg[1].y, boxReg[0].y, min_height, max_height);
      d3d::settm(TM_PROJ, &proj);

      ShaderGlobal::setBlock(clipmap_shadowsBlockId, ShaderGlobal::LAYER_FRAME);

      return rendinst::render::tryRenderRIGenShadowsToClipmap(clipmap_shadow_query_box(reg, texelSize), cascadeNo, budget, draws);
    };

    auto drainQuadRegions = [&](Tab<ToroidalQuadRegion> &regs, int &budget) {
      int numProcessed = 0;
      int lastRequiredDraws = 0;

      while (numProcessed < regs.size() && budget > 0)
      {
        int actualDraws = tryRenderQuad(regs[numProcessed], budget, lastRequiredDraws);
        budget -= actualDraws;
        if (actualDraws != lastRequiredDraws)
          break;
        ++numProcessed;
      }

      if (numProcessed == 0 && regs.size() > 0 && lastRequiredDraws > asyncUpdateDrawsBudget)
      {
        const auto reg = regs[numProcessed];

        if (is_toroidal_region_indivisible(reg))
        {
          int requiredDraws = 0;
          int actualDraws = tryRenderQuad(reg, INT_MAX, requiredDraws);
          logwarn("clipmapShadow: too tight region, cascade %d forced %d draws over budget %d", cascadeNo, requiredDraws,
            asyncUpdateDrawsBudget);
          budget -= actualDraws;
          erase_items(regs, 0, 1);
        }
        else
        {
          erase_items(regs, 0, 1);
          Tab<ToroidalQuadRegion> resplit(framemem_ptr());
          split_region_by_draw_budget(resplit, reg, texelSize, cascadeNo, asyncUpdateDrawsBudget);
          insert_items(regs, 0, resplit.size(), resplit.data());
        }
      }

      erase_items(regs, 0, numProcessed);
    };

    for (int i = 0; i < immediateRegions.size(); ++i)
    {
      int requiredDraws = 0;
      int actualDraws = tryRenderQuad(immediateRegions[i], currentBudget, requiredDraws);
      currentBudget -= actualDraws;
      if (actualDraws == requiredDraws)
        remove_drawn_area(deferredRegions[cascadeNo], immediateRegions[i]);
      else if (requiredDraws > asyncUpdateDrawsBudget)
        split_region_by_draw_budget(deferredRegions[cascadeNo], immediateRegions[i], texelSize, cascadeNo, asyncUpdateDrawsBudget);
      else
        append_items(deferredRegions[cascadeNo], 1, &immediateRegions[i]);
    }

    drainQuadRegions(deferredRegions[cascadeNo], currentBudget);
  }

  ShaderGlobal::set_texture(clipmapShadowTexVarId, clipmapShadowTex.getTexId());
  ShaderGlobal::set_float4(clipmap_shadow_near_far_tc_offsetVarId, Color4(uvOffset[1].x, uvOffset[1].y, uvOffset[0].x, uvOffset[0].y));

  d3d::resource_barrier({clipmapShadowTex.getTex2D(), RB_RO_SRV | RB_STAGE_PIXEL, 0, 0});

  return true;
}
