// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <render/rtsm/rtdl_api.h>
#include <render/rtsm/shaders/rtdl_consts.hlsli>
#include <render/denoiser.h>
#include <3d/dag_resPtr.h>
#include <bvh/bvh.h>
#include <drv/3d/dag_rwResource.h>
#include <drv/3d/dag_tex3d.h>
#include <math/dag_mathBase.h>
#include <math/integer/dag_IPoint2.h>
#include <perfMon/dag_statDrv.h>
#include <shaders/dag_computeShaders.h>
#include <shaders/dag_shaders.h>
#include <imgui/imgui.h>
#include <gui/dag_imgui.h>
#include <gui/dag_imguiUtil.h>

namespace rtdl
{
static ComputeShaderElement *sunTracePass = nullptr;
static ComputeShaderElement *sunFillPass = nullptr;
static ComputeShaderElement *sunTilesPass = nullptr;
static ComputeShaderElement *sunBlurPass = nullptr;
static ComputeShaderElement *localPass = nullptr;
static ComputeShaderElement *stabilizePass = nullptr;

static int rt_direct_lighting_resolutionVarId = -1;
static int rt_direct_lighting_resolutionIVarId = -1;
static int rt_direct_lighting_full_resolutionIVarId = -1;
static int rtsm_bindless_slotVarId = -1;
static int rt_shadow_resolutionVarId = -1;
static int rt_shadow_resolutionIVarId = -1;
static int rt_direct_lighting_paramsVarId = -1;
static int rt_direct_lighting_sun_paramsVarId = -1;
static int rt_direct_lighting_stabilize_paramsVarId = -1;
static int rt_direct_lighting_sun_qualityVarId = -1;
static int rt_direct_lighting_frame_paramsVarId = -1;
static int rt_direct_lighting_shadowsVarId = -1;
static int rt_direct_lighting_shadows_prevVarId = -1;
static int rt_direct_lighting_historyVarId = -1;
static int rt_direct_lighting_history_prevVarId = -1;
static int rt_direct_lighting_radianceVarIds[3] = {-1, -1, -1};
static int rt_direct_lighting_shadow_samplesVarId = -1;
static int rt_direct_lighting_normalsVarId = -1;
static int rt_direct_lighting_sun_samplesVarId = -1;
static int rt_direct_lighting_tilesVarId = -1;
static int rt_direct_lighting_tiles_dilatedVarId = -1;
static int rt_direct_lighting_sun_spatialVarId = -1;
static int rt_direct_lighting_motion_vectorsVarId = -1;
static int rt_direct_lighting_sun_shadowVarId = -1;
static int precomputed_dynamic_lightsVarId = -1;
static int render_direct_lights_use_tilingVarId = -1;
static int use_precomputed_dynamic_lightsVarId = -1;
static int rtsm_has_nukeVarId = -1;
static int world_view_posVarId = -1;

static int last_rendered_frame = -1;
static bool history_in_copy1 = false;
static bool history_valid_frame = false;

static int shadow_rays_per_pixel = 2;
static int max_sun_history = 8;
static float sun_blur_scale = 0.5f;
static float sun_blur_min_radius = 1;
static float sun_blur_max_radius = 24;
static float sun_clamp_sigma = 3;
static int max_local_shadow_history = 16;
static float local_clamp_sigma = 3;
static float local_shadow_radius = 4;
static int shadow_retest_period = 8;
static float slot_hysteresis = 1.5f;
static bool shadow_rest_lights = true;

static Texture *output_for_debug = nullptr;
static Texture *shadow_for_debug = nullptr;

void initialize()
{
  if (!bvh::is_available())
    return;

  rt_direct_lighting_resolutionVarId = get_shader_variable_id("rt_direct_lighting_resolution");
  rt_direct_lighting_resolutionIVarId = get_shader_variable_id("rt_direct_lighting_resolutionI");
  rt_direct_lighting_full_resolutionIVarId = get_shader_variable_id("rt_direct_lighting_full_resolutionI");
  rtsm_bindless_slotVarId = get_shader_variable_id("rtsm_bindless_slot");
  rt_shadow_resolutionVarId = get_shader_variable_id("rt_shadow_resolution");
  rt_shadow_resolutionIVarId = get_shader_variable_id("rt_shadow_resolutionI");
  rt_direct_lighting_paramsVarId = get_shader_variable_id("rt_direct_lighting_params");
  rt_direct_lighting_sun_paramsVarId = get_shader_variable_id("rt_direct_lighting_sun_params");
  rt_direct_lighting_stabilize_paramsVarId = get_shader_variable_id("rt_direct_lighting_stabilize_params");
  rt_direct_lighting_sun_qualityVarId = get_shader_variable_id("rt_direct_lighting_sun_quality");
  rt_direct_lighting_frame_paramsVarId = get_shader_variable_id("rt_direct_lighting_frame_params");
  rt_direct_lighting_shadowsVarId = get_shader_variable_id("rt_direct_lighting_shadows");
  rt_direct_lighting_shadows_prevVarId = get_shader_variable_id("rt_direct_lighting_shadows_prev");
  rt_direct_lighting_historyVarId = get_shader_variable_id("rt_direct_lighting_history");
  rt_direct_lighting_history_prevVarId = get_shader_variable_id("rt_direct_lighting_history_prev");
  rt_direct_lighting_radianceVarIds[0] = get_shader_variable_id("rt_direct_lighting_radiance0");
  rt_direct_lighting_radianceVarIds[1] = get_shader_variable_id("rt_direct_lighting_radiance1");
  rt_direct_lighting_radianceVarIds[2] = get_shader_variable_id("rt_direct_lighting_radiance2");
  rt_direct_lighting_shadow_samplesVarId = get_shader_variable_id("rt_direct_lighting_shadow_samples");
  rt_direct_lighting_normalsVarId = get_shader_variable_id("rt_direct_lighting_normals");
  rt_direct_lighting_sun_samplesVarId = get_shader_variable_id("rt_direct_lighting_sun_samples");
  rt_direct_lighting_tilesVarId = get_shader_variable_id("rt_direct_lighting_tiles");
  rt_direct_lighting_tiles_dilatedVarId = get_shader_variable_id("rt_direct_lighting_tiles_dilated");
  rt_direct_lighting_sun_spatialVarId = get_shader_variable_id("rt_direct_lighting_sun_spatial");
  rt_direct_lighting_motion_vectorsVarId = get_shader_variable_id("rt_direct_lighting_motion_vectors");
  rt_direct_lighting_sun_shadowVarId = get_shader_variable_id("rt_direct_lighting_sun_shadow");
  precomputed_dynamic_lightsVarId = get_shader_variable_id("precomputed_dynamic_lights");
  render_direct_lights_use_tilingVarId = get_shader_variable_id("render_direct_lights_use_tiling", true);
  use_precomputed_dynamic_lightsVarId = get_shader_variable_id("use_precomputed_dynamic_lights", true);
  rtsm_has_nukeVarId = get_shader_variable_id("rtsm_has_nuke");
  world_view_posVarId = get_shader_variable_id("world_view_pos");

  if (!sunTracePass)
    sunTracePass = new_compute_shader("rt_direct_lighting_sun_trace");
  if (!sunFillPass)
    sunFillPass = new_compute_shader("rt_direct_lighting_sun_fill");
  if (!sunTilesPass)
    sunTilesPass = new_compute_shader("rt_direct_lighting_sun_tiles");
  if (!sunBlurPass)
    sunBlurPass = new_compute_shader("rt_direct_lighting_sun_blur");
  if (!localPass)
    localPass = new_compute_shader("rt_direct_lighting_local");
  if (!stabilizePass)
    stabilizePass = new_compute_shader("rt_direct_lighting_stabilize");

  last_rendered_frame = -1;
  history_valid_frame = false;
}

template <typename T>
static void safe_delete(T *&ptr)
{
  if (ptr)
  {
    delete ptr;
    ptr = nullptr;
  }
}

static void clean_shvars()
{
  ShaderGlobal::set_texture(rt_direct_lighting_shadowsVarId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_shadows_prevVarId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_historyVarId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_history_prevVarId, nullptr);
  for (int varId : rt_direct_lighting_radianceVarIds)
    ShaderGlobal::set_texture(varId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_shadow_samplesVarId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_normalsVarId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_sun_samplesVarId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_tilesVarId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_tiles_dilatedVarId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_sun_spatialVarId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_motion_vectorsVarId, nullptr);
  ShaderGlobal::set_texture(rt_direct_lighting_sun_shadowVarId, nullptr);
}

void teardown()
{
  safe_delete(sunTracePass);
  safe_delete(sunFillPass);
  safe_delete(sunTilesPass);
  safe_delete(sunBlurPass);
  safe_delete(localPass);
  safe_delete(stabilizePass);
  output_for_debug = nullptr;
  shadow_for_debug = nullptr;
  last_rendered_frame = -1;
  history_valid_frame = false;
  ShaderGlobal::set_int(rtsm_bindless_slotVarId, -1);
  clean_shvars();
}

bool is_initialized() { return sunTracePass && sunFillPass && sunTilesPass && sunBlurPass && localPass && stabilizePass; }

static void add_texture(denoiser::TexInfoMap &textures, const char *name, unsigned int cflg, int w, int h)
{
  auto &ti = textures[name];
  ti.w = w;
  ti.h = h;
  ti.mipLevels = 1;
  ti.type = D3DResourceType::TEX;
  ti.cflg = TEXCF_UNORDERED | cflg;
}

static void add_screen_texture(denoiser::TexInfoMap &textures, const char *name, unsigned int cflg)
{
  add_texture(textures, name, cflg, denoiser::resolution_config.width, denoiser::resolution_config.height);
}

void get_required_persistent_texture_descriptors(denoiser::TexInfoMap &persistent_textures)
{
  if (denoiser::resolution_config.width == 0 || denoiser::resolution_config.height == 0)
    return;

  add_screen_texture(persistent_textures, TextureNames::rt_direct_lighting_output, TEXFMT_R11G11B10F);
  add_screen_texture(persistent_textures, TextureNames::rt_direct_lighting_sun_shadow, TEXCF_RTARGET | TEXFMT_R8);
  add_screen_texture(persistent_textures, TextureNames::rt_direct_lighting_shadows0, TEXFMT_A32B32G32R32UI);
  add_screen_texture(persistent_textures, TextureNames::rt_direct_lighting_shadows1, TEXFMT_A32B32G32R32UI);
  add_screen_texture(persistent_textures, TextureNames::rt_direct_lighting_history0, TEXFMT_R32G32UI);
  add_screen_texture(persistent_textures, TextureNames::rt_direct_lighting_history1, TEXFMT_R32G32UI);
}

void get_required_transient_texture_descriptors(denoiser::TexInfoMap &transient_textures)
{
  if (denoiser::resolution_config.width == 0 || denoiser::resolution_config.height == 0)
    return;

  add_screen_texture(transient_textures, TextureNames::rt_direct_lighting_radiance0, TEXFMT_A16B16G16R16F);
  add_screen_texture(transient_textures, TextureNames::rt_direct_lighting_radiance1, TEXFMT_A16B16G16R16F);
  add_screen_texture(transient_textures, TextureNames::rt_direct_lighting_radiance2, TEXFMT_A16B16G16R16F);
  add_screen_texture(transient_textures, TextureNames::rt_direct_lighting_shadow_samples, TEXFMT_R32G32UI);
  add_screen_texture(transient_textures, TextureNames::rt_direct_lighting_normals, TEXFMT_R8UI);
  add_screen_texture(transient_textures, TextureNames::rt_direct_lighting_sun_samples, TEXFMT_R8UI);
  add_screen_texture(transient_textures, TextureNames::rt_direct_lighting_sun_spatial, TEXFMT_R16F);
  const int tilesW = (denoiser::resolution_config.width + RTDL_TILE_W - 1) / RTDL_TILE_W;
  const int tilesH = (denoiser::resolution_config.height + RTDL_TILE_H - 1) / RTDL_TILE_H;
  add_texture(transient_textures, TextureNames::rt_direct_lighting_tiles, TEXFMT_R32UI, tilesW, tilesH);
  add_texture(transient_textures, TextureNames::rt_direct_lighting_tiles_dilated, TEXFMT_R32UI, tilesW, tilesH);
}

bool render(const Params &params, const denoiser::TexMap &textures)
{
  if (!is_initialized() || !params.ctxId)
    return false;

  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_output, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_sun_shadow, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_shadows0, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_shadows1, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_history0, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_history1, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_radiance0, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_radiance1, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_radiance2, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_shadow_samples, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_normals, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_sun_samples, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_tiles, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_tiles_dilated, false);
  ACQUIRE_DENOISER_TEXTURE_BASE(TextureNames, textures, rt_direct_lighting_sun_spatial, false);
  Texture *radiances[3] = {rt_direct_lighting_radiance0, rt_direct_lighting_radiance1, rt_direct_lighting_radiance2};

  TIME_D3D_PROFILE(rtdl::render);

  const IPoint2 &resolution = denoiser::resolution_config.dynRes.res;
  const IPoint2 &prevResolution = denoiser::resolution_config.dynRes.prevRes;

  TextureInfo ti;
  rt_direct_lighting_output->getinfo(ti);

  int frame = denoiser::get_frame_number();
  if (frame != last_rendered_frame)
  {
    history_valid_frame = last_rendered_frame >= 0 && frame == last_rendered_frame + 1;
    history_in_copy1 = !history_in_copy1;
    last_rendered_frame = frame;
  }
  bool historyValid = history_valid_frame && params.motionVectors && !params.resetHistory;

  Texture *shadowsPrev = history_in_copy1 ? rt_direct_lighting_shadows1 : rt_direct_lighting_shadows0;
  Texture *shadowsCur = history_in_copy1 ? rt_direct_lighting_shadows0 : rt_direct_lighting_shadows1;
  Texture *historyPrev = history_in_copy1 ? rt_direct_lighting_history1 : rt_direct_lighting_history0;
  Texture *historyCur = history_in_copy1 ? rt_direct_lighting_history0 : rt_direct_lighting_history1;

  ShaderGlobal::set_float4(rt_direct_lighting_resolutionVarId, resolution.x, resolution.y, 0, 0);
  ShaderGlobal::set_int4(rt_direct_lighting_resolutionIVarId, resolution.x, resolution.y, prevResolution.x, prevResolution.y);
  ShaderGlobal::set_int4(rt_direct_lighting_full_resolutionIVarId, ti.w, ti.h, 0, 0);
  ShaderGlobal::set_float4(rt_direct_lighting_paramsVarId, params.defaultLightSourceRadius, shadow_rays_per_pixel, max_sun_history,
    slot_hysteresis);
  ShaderGlobal::set_float4(rt_direct_lighting_sun_paramsVarId, sun_blur_scale, sun_blur_min_radius, sun_blur_max_radius,
    sun_clamp_sigma);
  ShaderGlobal::set_float4(rt_direct_lighting_stabilize_paramsVarId, max_local_shadow_history, local_clamp_sigma, local_shadow_radius,
    shadow_retest_period);
  ShaderGlobal::set_int4(rt_direct_lighting_frame_paramsVarId, frame, historyValid ? 1 : 0, shadow_rest_lights ? 1 : 0, 0);
  const int sunQuality = clamp(params.sunQuality, 0, 2);
  ShaderGlobal::set_int(rt_direct_lighting_sun_qualityVarId, sunQuality);
  ShaderGlobal::set_float4(world_view_posVarId, params.viewPos);
  ShaderGlobal::set_int(rtsm_has_nukeVarId, params.hasNuke ? 1 : 0);

  ShaderGlobal::set_texture(rt_direct_lighting_shadowsVarId, shadowsCur);
  ShaderGlobal::set_texture(rt_direct_lighting_shadows_prevVarId, shadowsPrev);
  ShaderGlobal::set_texture(rt_direct_lighting_historyVarId, historyCur);
  ShaderGlobal::set_texture(rt_direct_lighting_history_prevVarId, historyPrev);
  for (int i = 0; i < 3; ++i)
    ShaderGlobal::set_texture(rt_direct_lighting_radianceVarIds[i], radiances[i]);
  ShaderGlobal::set_texture(rt_direct_lighting_shadow_samplesVarId, rt_direct_lighting_shadow_samples);
  ShaderGlobal::set_texture(rt_direct_lighting_normalsVarId, rt_direct_lighting_normals);
  ShaderGlobal::set_texture(rt_direct_lighting_sun_samplesVarId, rt_direct_lighting_sun_samples);
  ShaderGlobal::set_texture(rt_direct_lighting_tilesVarId, rt_direct_lighting_tiles);
  ShaderGlobal::set_texture(rt_direct_lighting_tiles_dilatedVarId, rt_direct_lighting_tiles_dilated);
  ShaderGlobal::set_texture(rt_direct_lighting_sun_spatialVarId, rt_direct_lighting_sun_spatial);
  ShaderGlobal::set_texture(rt_direct_lighting_motion_vectorsVarId, params.motionVectors);
  ShaderGlobal::set_texture(precomputed_dynamic_lightsVarId, rt_direct_lighting_output);
  ShaderGlobal::set_texture(rt_direct_lighting_sun_shadowVarId, rt_direct_lighting_sun_shadow);
  // the radiance covers the whole screen and no tile mask goes with it, so the resolve must not gate it
  ShaderGlobal::set_int(render_direct_lights_use_tilingVarId, 0);
  ShaderGlobal::set_int(use_precomputed_dynamic_lightsVarId, 1);

  denoiser::set_shadow_maps_bindless(params.csmTexture, params.csmSampler, params.vsmTexture, params.vsmSampler);

  bvh::bind_resources(params.ctxId, resolution.x);

  const bool traceAllPixels = sunQuality == 0;
  const int halfWidth = (resolution.x + 1) / 2;

  {
    TIME_D3D_PROFILE(rtdl::sun_trace);
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_sun_samples, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_tiles, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    sunTracePass->dispatch((resolution.x + RTDL_TILE_W - 1) / RTDL_TILE_W, (resolution.y + RTDL_TILE_H - 1) / RTDL_TILE_H, 1);
  }

  {
    TIME_D3D_PROFILE(rtdl::sun_tiles);
    TextureInfo tilesInfo;
    rt_direct_lighting_tiles->getinfo(tilesInfo);
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_tiles, RB_RO_SRV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_tiles_dilated, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    sunTilesPass->dispatchThreads(tilesInfo.w, tilesInfo.h, 1);
  }

  if (!traceAllPixels)
  {
    TIME_D3D_PROFILE(rtdl::sun_fill);
    d3d::resource_barrier(
      ResourceBarrierDesc(rt_direct_lighting_sun_samples, RB_FLUSH_UAV | RB_STAGE_COMPUTE | RB_SOURCE_STAGE_COMPUTE, 0, 0));
    sunFillPass->dispatchThreads(halfWidth, resolution.y, 1);
  }

  {
    TIME_D3D_PROFILE(rtdl::sun_blur);
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_sun_samples, RB_RO_SRV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_tiles_dilated, RB_RO_SRV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_sun_spatial, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    sunBlurPass->dispatchThreads(resolution.x, resolution.y, 1);
  }

  {
    TIME_D3D_PROFILE(rtdl::local);
    d3d::resource_barrier(ResourceBarrierDesc(shadowsPrev, RB_RO_SRV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(historyPrev, RB_RO_SRV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_shadow_samples, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_normals, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    for (Texture *radiance : radiances)
      d3d::resource_barrier(ResourceBarrierDesc(radiance, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    localPass->dispatchThreads(resolution.x, resolution.y, 1);
  }

  {
    TIME_D3D_PROFILE(rtdl::stabilize);
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_shadow_samples, RB_RO_SRV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_normals, RB_RO_SRV | RB_STAGE_COMPUTE, 0, 0));
    for (Texture *radiance : radiances)
      d3d::resource_barrier(ResourceBarrierDesc(radiance, RB_RO_SRV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_sun_spatial, RB_RO_SRV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(shadowsCur, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(historyCur, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_output, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_sun_shadow, RB_RW_UAV | RB_STAGE_COMPUTE, 0, 0));
    stabilizePass->dispatchThreads(resolution.x, resolution.y, 1);
  }

  d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_output, RB_RO_SRV | RB_STAGE_PIXEL | RB_STAGE_COMPUTE, 0, 0));
  d3d::resource_barrier(ResourceBarrierDesc(rt_direct_lighting_sun_shadow, RB_RO_SRV | RB_STAGE_ALL_SHADERS, 0, 0));

  bvh::unbind_resources();
  clean_shvars();

  denoiser::set_shadow_output_bindless(rt_direct_lighting_sun_shadow);
  ShaderGlobal::set_float4(rt_shadow_resolutionVarId, resolution.x, resolution.y);
  ShaderGlobal::set_int4(rt_shadow_resolutionIVarId, resolution.x, resolution.y, 0, 0);
  output_for_debug = rt_direct_lighting_output;
  shadow_for_debug = rt_direct_lighting_sun_shadow;

  return true;
}

#if DAGOR_DBGLEVEL > 0
static void imguiWindow()
{
  ImGui::SliderInt("Shadow rays per pixel", &shadow_rays_per_pixel, 1, 3);
  ImGui::SliderInt("Max sun history", &max_sun_history, 1, 63);
  ImGui::SliderFloat("Sun blur scale", &sun_blur_scale, 0, 2);
  ImGui::SliderFloat("Sun blur min radius", &sun_blur_min_radius, 0, 4);
  ImGui::SliderFloat("Sun blur max radius", &sun_blur_max_radius, 1, 64);
  ImGui::SliderFloat("Sun history clamp sigma", &sun_clamp_sigma, 0.5f, 6);
  ImGui::SliderInt("Max local shadow history", &max_local_shadow_history, 1, 63);
  ImGui::SliderFloat("Local shadow clamp sigma", &local_clamp_sigma, 0.5f, 6);
  ImGui::SliderFloat("Local shadow blur radius", &local_shadow_radius, 0, 8);
  ImGui::SliderInt("Settled shadow re-test period", &shadow_retest_period, 1, 32);
  ImGui::SliderFloat("Slot hysteresis", &slot_hysteresis, 1, 4);
  ImGui::Checkbox("Rest lights take the slot visibility", &shadow_rest_lights);
  if (output_for_debug)
    ImGuiDagor::Image(output_for_debug);
  if (shadow_for_debug)
    ImGuiDagor::Image(shadow_for_debug);
}

REGISTER_IMGUI_WINDOW("Render", "RT direct lighting", imguiWindow);
#endif
} // namespace rtdl
