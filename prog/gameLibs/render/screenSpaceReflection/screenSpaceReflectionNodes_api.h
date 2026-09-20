// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include "screenSpaceReflections_api.h"
#include <render/cameraParams.h>
#include <render/daFrameGraph/daFG.h>
#include <drv/3d/dag_resource.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/variant.h>

namespace render::ssr
{

struct ResourceNames
{
  const char *closeDepthResName = nullptr;

  const char *closeDepthSamplerResName = nullptr;
  const char *normalsSamplerResName = nullptr;
  const char *motionVectorsSamplerResName = nullptr;

  const char *motionVectorsVar = "downsampled_motion_vectors_tex";

  const char *checkerboardDepthResName = nullptr;

  const char *gbufSamplerResName = nullptr;
};

inline constexpr const char *frame_params_blob_name = "ssr_frame_params";
inline constexpr const char *trace_reprojection_blob_name = "ssr_trace_reprojection";

struct FrameParams
{
  bool shouldRender = false; // false: skip rendering this frame and clear the target.
  TMatrix viewTm;
  TMatrix4 projTm;
  DPoint3 worldPos;
  // Set if viewTm above is a temporary rebase of the real view.
  bool rebase = false;
  bool historyValid = false;
};

struct TargetDesc
{
  uint32_t fmt = 0;
  SSRQuality quality = {};
  // Half res or full res.
  bool isFullres = false;
};

namespace detail
{

// Which parts of the shared input set a node reads.
struct InputParameters
{
  dafg::Stage stage = dafg::Stage::PS_OR_CS;
  bool readPrevFrame = false;
  bool readNormalsHistory = false;
  bool readCheckerboardDepth = false;
  bool readMaterialGbuf = false;
  bool isFullres = false;
};

// The only place that maps the ssr shader inputs to daFG resources and shader var slots.
void declare_inputs(dafg::Registry registry, const ResourceNames &names, const InputParameters &params);

inline bool use_simple_denoiser(const TargetDesc &target, bool requested)
{
  // The ps path doesn't support the simple denoiser for now.
  return requested && target.quality == SSRQuality::Compute;
}

inline bool use_normals_history(bool has_normals_history, const TargetDesc &target)
{
  // The ps path doesn't reproject the previous normals right, it has to see prev_downsampled_normals as NULL.
  return has_normals_history && target.quality == SSRQuality::Compute;
}

} // namespace detail

struct ResourceProviderParameters
{
  TargetDesc target;
  d3d::AddressMode samplerAddressMode = d3d::AddressMode::Clamp;
  d3d::BorderColor::Color samplerBorderColor = d3d::BorderColor::Color::TransparentBlack;
};

// Creates "ssr_target_before_denoise" (target-sized, mip count from
// ScreenSpaceReflections::getMipCount(quality)) and "ssr_target_sampler".
dafg::NodeHandle make_resource_provider_node(dafg::NameSpace ns, const ResourceProviderParameters &params);

struct DenoiserParameters
{
  ResourceNames resourceNames;
  // Namespace of the trace node, relative to the root, when it is not the denoise node's own one.
  const char *traceViewNsName = nullptr;
  TargetDesc target;
  bool useSimpleDenoiser = false;
  bool readMaterialGbuf = false;
  // False if the project's downsampled_normals has no history: then no pass reprojects the previous normals.
  bool hasNormalsHistory = true;
};

// Either renames ssr_target_before_denoise -> ssr_target (useSimpleDenoiser == false), or
// dispatches ssr_temporal_denoise_cs into a newly created one.
dafg::NodeHandle make_denoiser_node(dafg::NameSpace ns, const DenoiserParameters &params);

struct TraceNodeParameters
{
  ResourceNames resourceNames;
  TargetDesc target;
  bool useSimpleDenoiser = false;
  bool readMaterialGbuf = false;
  // False if the project's downsampled_normals has no history: then no pass reprojects the previous normals.
  bool hasNormalsHistory = true;
  bool isMainView = true;
};

namespace detail
{

// The trace resources that do not depend on the project's context type.
struct TraceResources
{
  dafg::VirtualResourceHandle<BaseTexture, true, false> target;
  dafg::VirtualResourceHandle<const BaseTexture, true, false> targetHistory;
  dafg::VirtualResourceHandle<const SubFrameSample, false, true> subFrameSample;
  dafg::VirtualResourceHandle<const FrameParams, false, false> frameParams;
  dafg::VirtualResourceHandle<const CameraParams, false, false> camera;
  dafg::VirtualResourceHandle<ScreenSpaceReflections::Reprojection, false, false> traceReprojection;
  bool useSimpleDenoiser = false;
  bool isMainView = true;
};

TraceResources declare_trace_resources(dafg::Registry registry, const TraceNodeParameters &params);
bool prepare_ssr(const TraceResources &res, ScreenSpaceReflections &ssr);
void render_ssr(const TraceResources &res, ScreenSpaceReflections &ssr, const dafg::multiplexing::Index &multiplexing_index);

} // namespace detail

// The trace node.
// Context is an RAII guard around render() call, constructed as
// Context(const CameraParams &camera, const FrameParams &params, bool is_main_view).
// DoneToken types the done blob.
template <class Context, class DoneToken = eastl::monostate>
dafg::NodeHandle make_trace_node(dafg::NameSpace ns, const TraceNodeParameters &params)
{
  return ns.registerNode("ssr_node", DAFG_PP_NODE_SRC, [params](dafg::Registry registry) {
    registry.createBlob<DoneToken>("after_ssr_node_token");
    const detail::TraceResources res = detail::declare_trace_resources(registry, params);

    return [res, ssr = eastl::make_unique<ScreenSpaceReflections>(0, 0, 1, params.target.fmt, params.target.quality, SSRFlag::None)](
             const dafg::multiplexing::Index &multiplexing_index) {
      if (!detail::prepare_ssr(res, *ssr))
        return;

      // Project specific context.
      const Context ctx(res.camera.ref(), res.frameParams.ref(), res.isMainView);
      detail::render_ssr(res, *ssr, multiplexing_index);
    };
  });
}

} // namespace render::ssr
