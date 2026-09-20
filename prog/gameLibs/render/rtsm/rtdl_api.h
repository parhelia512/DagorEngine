// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <drv/3d/dag_tex3d.h>
#include <math/dag_Point3.h>
#include <render/denoiser.h>

namespace bvh
{
struct Context;
using ContextId = Context *;
} // namespace bvh

namespace rtdl
{
namespace TextureNames
{
inline const char *const rt_direct_lighting_output = "rt_direct_lighting_output";
inline const char *const rt_direct_lighting_sun_shadow = "rt_direct_lighting_sun_shadow";
inline const char *const rt_direct_lighting_shadows0 = "rt_direct_lighting_shadows0";
inline const char *const rt_direct_lighting_shadows1 = "rt_direct_lighting_shadows1";
inline const char *const rt_direct_lighting_history0 = "rt_direct_lighting_history0";
inline const char *const rt_direct_lighting_history1 = "rt_direct_lighting_history1";
inline const char *const rt_direct_lighting_radiance0 = "rt_direct_lighting_radiance0";
inline const char *const rt_direct_lighting_radiance1 = "rt_direct_lighting_radiance1";
inline const char *const rt_direct_lighting_radiance2 = "rt_direct_lighting_radiance2";
inline const char *const rt_direct_lighting_shadow_samples = "rt_direct_lighting_shadow_samples";
inline const char *const rt_direct_lighting_normals = "rt_direct_lighting_normals";
inline const char *const rt_direct_lighting_sun_samples = "rt_direct_lighting_sun_samples";
inline const char *const rt_direct_lighting_tiles = "rt_direct_lighting_tiles";
inline const char *const rt_direct_lighting_tiles_dilated = "rt_direct_lighting_tiles_dilated";
inline const char *const rt_direct_lighting_sun_spatial = "rt_direct_lighting_sun_spatial";
} // namespace TextureNames

struct Params
{
  bvh::ContextId ctxId = nullptr;
  Point3 viewPos = Point3::ZERO;
  Texture *motionVectors = nullptr;
  Texture *csmTexture = nullptr;
  d3d::SamplerHandle csmSampler = d3d::INVALID_SAMPLER_HANDLE;
  Texture *vsmTexture = nullptr;
  d3d::SamplerHandle vsmSampler = d3d::INVALID_SAMPLER_HANDLE;
  float defaultLightSourceRadius = 0.1f;
  int sunQuality = 1;
  bool hasNuke = false;
  bool resetHistory = false;
};

void initialize();
void teardown();
bool is_initialized();

// The texture maps are keyed by these name pointers, not by the string contents.
// The denoiser is initialized and prepared for the frame before the descriptors are requested and render is called.
void get_required_persistent_texture_descriptors(denoiser::TexInfoMap &persistent_textures);
void get_required_transient_texture_descriptors(denoiser::TexInfoMap &transient_textures);

bool render(const Params &params, const denoiser::TexMap &textures);
} // namespace rtdl
