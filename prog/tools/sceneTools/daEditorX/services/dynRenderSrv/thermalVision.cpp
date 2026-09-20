// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "thermalVision.h"
#include <render/specialVision/nightVisionPostFx.h>
#include <render/deferredRenderer.h>
#include <render/noiseTex.h>
#include <render/viewVecs.h>
#include <shaders/dag_shaders.h>
#include <drv/3d/dag_renderTarget.h>
#include <ioSys/dag_dataBlock.h>
#include <math/dag_TMatrix4.h>
#include <math/dag_color.h>

using namespace render::special_vision;

const IPoint2 resolution = IPoint2(640, 360);
const Point3 baseColor = Point3(0, 0, 0);
const Point3 midColor = Point3(0.5f, 0.5f, 0.5f);
const Point3 brightColor = Point3(1, 1, 1);
constexpr float ghosting = 0.4f;
constexpr bool whiteIsHot = true;

static bool is_wt_thermalvision_supported()
{
  Ptr<ShaderMaterial> mat0 = new_shader_material_by_name_optional("deferred_thermal_resolve");
  Ptr<ShaderMaterial> mat1 = new_shader_material_by_name_optional("night_vision_combine");
  return mat0.get() && mat1.get();
}

ThermalVision::ThermalVision() = default;

ThermalVision::~ThermalVision()
{
  if (enabled)
    setEnabled(false);
  if (noiseAcquired)
    release_perline_noise_3d();
}

void ThermalVision::init()
{
  if (postFx || !is_wt_thermalvision_supported())
    return;

  postFx = eastl::make_unique<NightVisionPostFx>();
  if (!postFx->init(true))
  {
    postFx.reset();
    return;
  }

  Point3 minR, maxR;
  init_and_get_perlin_noise_3d(minR, maxR);
  noiseAcquired = true;
}

void ThermalVision::createTargets()
{
  if (!postFx || pendingSize.x <= 0 || pendingSize.y <= 0)
    return;

  postFx->initTargets(RENDER_THERMAL_VISION, resolution.x, resolution.y, pendingSize.x, pendingSize.y);
  postFx->applySettings(0.f, ghosting, baseColor, midColor, brightColor, 0.f, 0.f, !whiteIsHot);
  currentSize = pendingSize;
}

bool ThermalVision::isActive() const { return enabled && postFx && pendingSize.x > 0 && pendingSize.y > 0; }

void ThermalVision::setEnabled(bool enable)
{
  if (!postFx || enabled == enable)
    return;
  enabled = enable;
  set_special_vision_shader_mode(enable);
}

void ThermalVision::resize(int target_w, int target_h) { pendingSize = {target_w, target_h}; }

void ThermalVision::closeTargets()
{
  if (postFx)
    postFx->releaseTargets();
  currentSize = {0, 0};
  pendingSize = {0, 0};
}

void ThermalVision::resolve(DeferredRenderTarget &gbuf, BaseTexture *dest, const TMatrix &view_tm, const TMatrix4 &proj_tm)
{
  if (!isActive())
    return;

  if (pendingSize != currentSize)
    createTargets();
  if (!postFx->hasTargets())
    return;

  gbuf.setVar();
  set_inv_globtm_to_shader(view_tm, proj_tm, true);
  set_viewvecs_to_shader(view_tm, proj_tm);
  d3d::set_render_target({}, DepthAccess::RW, {{dest, 0, 0}});
  postFx->resolve();
}

void ThermalVision::apply(BaseTexture *spectre, BaseTexture *dest)
{
  if (!isActive() || !postFx->hasTargets())
    return;

  postFx->apply(spectre, dest);
}
