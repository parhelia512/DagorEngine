//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include "riExtraRenderer.h"

#include <3d/dag_texStreamingContext.h>
#include <shaders/dag_shaderVarsUtils.h>
#include <util/dag_threadPool.h>

#include <EASTL/unique_ptr.h>

#include <rendInst/constants.h>
#include <rendInst/renderPass.h>


struct RiGenVisibility;

namespace rendinst::render
{

class RiExtraRendererBuilder
{
public:
  RiExtraRendererBuilder(int vb_extra_ctx_id, RenderPass pass, OptimizeDepthPass optimize_depth_pass = OptimizeDepthPass::No);
  ~RiExtraRendererBuilder();
  RiExtraRendererBuilder(const RiExtraRendererBuilder &) = delete;
  RiExtraRendererBuilder &operator=(const RiExtraRendererBuilder &) = delete;

  RiExtraRenderer *buildNow(RiGenVisibility &v, int frame_stblk, int scene_stblk, TexStreamingContext tex_ctx,
    RiExtraRenderingSubset subset = RiExtraRenderingSubset::All);


  void capture(int frame_stblk, int scene_stblk, TexStreamingContext tex_ctx);
  bool build(RiGenVisibility &v, RiExtraRenderingSubset subset = RiExtraRenderingSubset::All);
  void releaseCapture();

  RiExtraRenderer *getRenderer() const { return riExRenderer.get(); }
  int getVbExtraCtxId() const { return vbExtraCtxId; }

  void resetVbCtx();

private:
  eastl::unique_ptr<RiExtraRenderer, RiExtraRendererDelete> riExRenderer;
  GlobalVariableStates gvars;
  TexStreamingContext texContext = TexStreamingContext(0);
  int vbExtraCtxId = 0;
  RenderPass renderPass = RenderPass::Normal;
  OptimizeDepthPass optimizeDepthPass = OptimizeDepthPass::No;
};

} // namespace rendinst::render

struct RenderRiExtraJob final : public cpujobs::IJob
{
  rendinst::render::RiExtraRendererBuilder builder;
  RiGenVisibility *vbase = nullptr;
  rendinst::RiExtraRenderingSubset renderingSubset = rendinst::RiExtraRenderingSubset::All;

  RenderRiExtraJob(int vb_extra_ctx_id, rendinst::RenderPass pass = rendinst::RenderPass::Normal,
    rendinst::OptimizeDepthPass optimize_depth_pass = rendinst::OptimizeDepthPass::No);

  void prepare(RiGenVisibility &v, int frame_stblk, bool enable, TexStreamingContext texCtx);
  void prepare(RiGenVisibility &v, int frame_stblk, int scene_stblk, bool enable, TexStreamingContext texCtx);
  void start(RiGenVisibility &v, bool wake);
  const char *getJobName(bool &) const override { return DAPROFILER_STRING("RenderRiExtraJob"); }
  void doJob() override;

  void waitVbFill(const RiGenVisibility *v);
  rendinst::render::RiExtraRenderer *wait(const RiGenVisibility *v);
  void resetRiExtraCtx();
};