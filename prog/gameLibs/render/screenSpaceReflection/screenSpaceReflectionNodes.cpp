// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "screenSpaceReflectionNodes_api.h"
#include <drv/3d/dag_renderTarget.h>
#include <drv/3d/dag_rwResource.h>
#include <drv/3d/dag_sampler.h>
#include <drv/3d/dag_decl.h>
#include <shaders/dag_computeShaders.h>
#include <shaders/dag_shaderVariableInfo.h>
#include <startup/dag_globalSettings.h>
#include <EASTL/unique_ptr.h>

namespace render::ssr
{

static void clear_ssr_target(BaseTexture *tex, uint32_t mip_count, bool is_compute)
{
  if (is_compute)
  {
    const float zero[4] = {0, 0, 0, 0};
    d3d::clear_rwtexf(tex, zero, 0, 0);
    return;
  }
  for (uint32_t mip = 0; mip < mip_count; ++mip)
    d3d::clear_rt({tex, mip, 0}, make_clear_value(0.f, 0.f, 0.f, 0.f));
}

static float target_resolution_scale(const TargetDesc &target) { return target.isFullres ? 1.f : 0.5f; }

// The trace and the denoise pass can live in different namespaces, the params blob always belongs to the trace.
static dafg::NameSpaceRequest trace_name_space(dafg::Registry registry, const char *trace_view_ns_name)
{
  return trace_view_ns_name ? registry.root() / trace_view_ns_name : registry.currNameSpace();
}

namespace detail
{

void declare_inputs(dafg::Registry registry, const ResourceNames &names, const InputParameters &params)
{
  const dafg::Stage stage = params.stage;

  if (params.readPrevFrame)
  {
    registry.readTextureHistory("prev_frame_tex").atStage(stage).bindToShaderVar("prev_frame_tex").optional();
    registry.read("prev_frame_sampler").blob<d3d::SamplerHandle>().bindToShaderVar("prev_frame_tex_samplerstate").optional();
  }

  G_ASSERT(names.closeDepthResName);
  registry.readTexture(names.closeDepthResName).atStage(stage).bindToShaderVar("downsampled_close_depth_tex").optional();
  registry.readTextureHistory(names.closeDepthResName).atStage(stage).bindToShaderVar("prev_downsampled_close_depth_tex").optional();
  if (names.closeDepthSamplerResName)
  {
    registry.read(names.closeDepthSamplerResName)
      .blob<d3d::SamplerHandle>()
      .bindToShaderVar("downsampled_close_depth_tex_samplerstate")
      .bindToShaderVar("prev_downsampled_close_depth_tex_samplerstate")
      .optional();
  }

  registry.readTexture("downsampled_normals").atStage(stage).bindToShaderVar("downsampled_normals").optional();
  if (params.readNormalsHistory)
    registry.readTextureHistory("downsampled_normals").atStage(stage).bindToShaderVar("prev_downsampled_normals").optional();
  if (names.normalsSamplerResName)
    registry.read(names.normalsSamplerResName)
      .blob<d3d::SamplerHandle>()
      .bindToShaderVar("downsampled_normals_samplerstate")
      .optional();

  registry.readTexture("downsampled_motion_vectors_tex").atStage(stage).bindToShaderVar(names.motionVectorsVar).optional();
  if (names.motionVectorsSamplerResName)
  {
    registry.read(names.motionVectorsSamplerResName)
      .blob<d3d::SamplerHandle>()
      .bindToShaderVar("downsampled_motion_vectors_tex_samplerstate")
      .bindToShaderVar("prev_downsampled_motion_vectors_tex_samplerstate")
      .optional();
  }

  if (params.readCheckerboardDepth && names.checkerboardDepthResName)
  {
    registry.readTexture(names.checkerboardDepthResName)
      .atStage(stage)
      .bindToShaderVar("downsampled_checkerboard_depth_tex")
      .optional();
  }

  // At full res, the ssr passes need the full gbuffer's normals and depth.
  if (params.isFullres)
  {
    registry.read("gbuf_1").texture().atStage(stage).bindToShaderVar("normal_gbuf");
    registry.readTexture("gbuf_depth").atStage(stage).bindToShaderVar("depth_gbuf");
  }

  if (params.readMaterialGbuf || params.isFullres)
    registry.read("gbuf_2").texture().atStage(stage).bindToShaderVar("material_gbuf").optional();

  // The whole gbuffer shares one sampler, so bind every var of it through a single request.
  if (names.gbufSamplerResName)
  {
    registry.read(names.gbufSamplerResName)
      .blob<d3d::SamplerHandle>()
      .bindToShaderVar("material_gbuf_samplerstate")
      .bindToShaderVar("normal_gbuf_samplerstate")
      .bindToShaderVar("depth_gbuf_samplerstate")
      .optional();
  }
}

TraceResources declare_trace_resources(dafg::Registry registry, const TraceNodeParameters &params)
{
  declare_inputs(registry, params.resourceNames,
    {
      .stage = dafg::Stage::PS_OR_CS,
      .readPrevFrame = true,
      .readNormalsHistory = use_normals_history(params.hasNormalsHistory, params.target),
      .readCheckerboardDepth = true,
      .readMaterialGbuf = params.readMaterialGbuf,
      .isFullres = params.target.isFullres,
    });

  // The compute trace writes the target through a UAV, the pixel one renders into it.
  const dafg::Usage usage =
    params.target.quality == SSRQuality::Compute ? dafg::Usage::SHADER_RESOURCE : dafg::Usage::COLOR_ATTACHMENT;

  return {
    .target = registry.modifyTexture("ssr_target_before_denoise").atStage(dafg::Stage::PS_OR_CS).useAs(usage).handle(),
    .targetHistory =
      registry.historyFor("ssr_target").texture().atStage(dafg::Stage::PS_OR_CS).useAs(dafg::Usage::SHADER_RESOURCE).handle(),
    .subFrameSample = registry.readBlob<SubFrameSample>("sub_frame_sample").optional().handle(),
    // Required: a project node must create and fill this one.
    .frameParams = registry.readBlob<FrameParams>(frame_params_blob_name).handle(),
    .camera = registry.readBlob<CameraParams>("current_camera").handle(),
    // The denoise pass reprojects the same history, so we have to pass the reprojection params there.
    .traceReprojection = registry.createBlob<ScreenSpaceReflections::Reprojection>(trace_reprojection_blob_name).handle(),
    .useSimpleDenoiser = use_simple_denoiser(params.target, params.useSimpleDenoiser),
    .isMainView = params.isMainView,
  };
}

bool prepare_ssr(const TraceResources &res, ScreenSpaceReflections &ssr)
{
  static ShaderVariableInfo ssr_denoiser_typeVarId("ssr_denoiser_type", true);
  ShaderGlobal::set_int(ssr_denoiser_typeVarId, res.useSimpleDenoiser ? 1 : 0);

  const FrameParams &frameParams = res.frameParams.ref();
  // The node that publishes ssr_target clears it on a skipped frame, so there is nothing to do here.
  if (!frameParams.shouldRender)
    return false;

  // Pass the reprojection parameters to the denoiser node.
  res.traceReprojection.ref() = ssr.getReprojection();

  TextureInfo info;
  res.target.get()->getinfo(info);
  // Compute SSR needs the explicit resolution change, its dispatch size depends on it.
  ssr.changeDynamicResolution(info.w, info.h);
  ssr.setHistoryValid(frameParams.historyValid);
  return true;
}

void render_ssr(const TraceResources &res, ScreenSpaceReflections &ssr, const dafg::multiplexing::Index &multiplexing_index)
{
  const SubFrameSample *subFrameSample = res.subFrameSample.get();
  const SubFrameSample sample = subFrameSample ? *subFrameSample : SubFrameSample::Single;
  const int callId = ::dagor_frame_no() + multiplexing_index.viewport + multiplexing_index.subSample + multiplexing_index.superSample;

  const FrameParams &frameParams = res.frameParams.ref();
  BaseTexture *ssrTex = res.target.get();
  ssr.render(frameParams.viewTm, frameParams.projTm, frameParams.worldPos, sample, ssrTex, res.targetHistory.get(), ssrTex, callId);
}

} // namespace detail

dafg::NodeHandle make_resource_provider_node(dafg::NameSpace ns, const ResourceProviderParameters &params)
{
  return ns.registerNode("ssr_node_camera_res_provider", DAFG_PP_NODE_SRC, [params](dafg::Registry registry) {
    registry.create("ssr_target_before_denoise")
      .texture({params.target.fmt, registry.getResolution<2>("main_view", target_resolution_scale(params.target)),
        (uint32_t)ScreenSpaceReflections::getMipCount(params.target.quality)});

    d3d::SamplerInfo smpInfo;
    smpInfo.address_mode_u = smpInfo.address_mode_v = smpInfo.address_mode_w = params.samplerAddressMode;
    smpInfo.border_color = params.samplerBorderColor;
    registry.create("ssr_target_sampler").blob<d3d::SamplerHandle>(d3d::request_sampler(smpInfo));

    return [] {};
  });
}

dafg::NodeHandle make_denoiser_node(dafg::NameSpace ns, const DenoiserParameters &params)
{
  if (!detail::use_simple_denoiser(params.target, params.useSimpleDenoiser))
  {
    return ns.registerNode("ssr_publish_node", DAFG_PP_NODE_SRC, [params](dafg::Registry registry) {
      const bool isCompute = params.target.quality == SSRQuality::Compute;
      auto ssrTargetHndl = registry.renameTexture("ssr_target_before_denoise", "ssr_target")
                             .withHistory(dafg::History::ClearZeroOnFirstFrame)
                             .atStage(dafg::Stage::PS_OR_CS)
                             .useAs(isCompute ? dafg::Usage::SHADER_RESOURCE : dafg::Usage::COLOR_ATTACHMENT)
                             .handle();
      auto frameParamsHndl = trace_name_space(registry, params.traceViewNsName).readBlob<FrameParams>(frame_params_blob_name).handle();

      const uint32_t mipCount = (uint32_t)ScreenSpaceReflections::getMipCount(params.target.quality);
      return [ssrTargetHndl, frameParamsHndl, mipCount, isCompute] {
        // The trace leaves the target untouched on a skipped frame, and the resolve still samples it.
        if (!frameParamsHndl.ref().shouldRender)
          clear_ssr_target(ssrTargetHndl.get(), mipCount, isCompute);
      };
    });
  }

  return ns.registerNode("ssr_temporal_denoiser_node", DAFG_PP_NODE_SRC, [params](dafg::Registry registry) {
    // The node only dispatches ssr_temporal_denoise_cs, so every access is CS-stage.
    constexpr dafg::Stage stage = dafg::Stage::CS;
    detail::declare_inputs(registry, params.resourceNames,
      {.stage = stage,
        .readNormalsHistory = detail::use_normals_history(params.hasNormalsHistory, params.target),
        .readMaterialGbuf = params.readMaterialGbuf,
        .isFullres = params.target.isFullres});

    registry.readTexture("ssr_target_before_denoise").atStage(stage).bindToShaderVar("ssr_target_before_denoise");

    auto ssrTargetHndl = registry.create("ssr_target")
                           .texture({params.target.fmt | TEXCF_UNORDERED,
                             registry.getResolution<2>("main_view", target_resolution_scale(params.target)), 1})
                           .withHistory(dafg::History::ClearZeroOnFirstFrame)
                           .atStage(stage)
                           .bindToShaderVar("ssr_target")
                           .handle();
    registry.readTextureHistory("ssr_target").atStage(stage).bindToShaderVar("ssr_prev_target");
    registry.read("ssr_target_sampler").blob<d3d::SamplerHandle>().bindToShaderVar("ssr_prev_target_samplerstate");

    const dafg::NameSpaceRequest traceNs = trace_name_space(registry, params.traceViewNsName);
    auto frameParamsHndl = traceNs.readBlob<FrameParams>(frame_params_blob_name).handle();
    auto traceReprojectionHndl = traceNs.readBlob<ScreenSpaceReflections::Reprojection>(trace_reprojection_blob_name).handle();

    return [ssrTargetHndl, frameParamsHndl, traceReprojectionHndl,
             cs = eastl::unique_ptr<ComputeShaderElement>(new_compute_shader("ssr_temporal_denoise_cs"))]() {
      const FrameParams &frameParams = frameParamsHndl.ref();
      if (!frameParams.shouldRender)
      {
        clear_ssr_target(ssrTargetHndl.get(), 1, /*is_compute*/ true);
        return;
      }

      static ShaderVariableInfo force_ignore_historyVarId("force_ignore_history", true);
      const int prevIgnoreHistory = ShaderGlobal::get_int(force_ignore_historyVarId);
      STATE_GUARD(ShaderGlobal::set_int(force_ignore_historyVarId, VALUE), !frameParams.historyValid || prevIgnoreHistory,
        prevIgnoreHistory);

      ScreenSpaceReflections::applyReprojection(traceReprojectionHndl.ref(), frameParams.viewTm, frameParams.projTm,
        frameParams.worldPos);

      TextureInfo info;
      ssrTargetHndl.get()->getinfo(info);
      cs->dispatchThreads(info.w, info.h, 1);
    };
  });
}

} // namespace render::ssr
