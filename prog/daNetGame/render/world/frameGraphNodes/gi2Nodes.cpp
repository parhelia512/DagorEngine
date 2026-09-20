// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <render/daFrameGraph/daFG.h>
#include <render/world/bvh.h>

#include <daGI2/daGI2.h>
#include <util/dag_convar.h>
#include <render/viewVecs.h>

#define INSIDE_RENDERER 1
#include "../private_worldRenderer.h"
#include "../global_vars.h"
#include "frameGraphNodes.h"
#include <render/world/frameGraphHelpers.h>
#include <drv/3d/dag_renderTarget.h>

// giVerifier replay capture (render/giVerifierCapture.h) impl
#if DAGOR_DBGLEVEL > 0
#include <render/giVerifierCapture.h>
#include <util/dag_console.h>
#include <util/dag_string.h>
#include <generic/dag_tab.h>
#include <shaders/dag_shaders.h>
#include <ioSys/dag_dataBlock.h>
#include <osApiWrappers/dag_direct.h>
#include <daECS/core/entityManager.h>
#include "main/level.h"

static struct
{
  bool pending = false;
  String dir, riDump, levelBin;
} gi_verify_request;

static void gi_verify_perform_capture(const CameraParams &cam, int w, int h)
{
  gi_verify_request.pending = false;
  String levelBin = gi_verify_request.levelBin;
  if (levelBin.empty())
  {
    const char *blkPath = g_entity_mgr->getOr(get_current_level_eid(), ECS_HASH("level__blk"), "");
    DataBlock lblk;
    if (*blkPath && dblk::load(lblk, blkPath, dblk::ReadFlag::ROBUST))
      levelBin = lblk.getStr("levelBin", "");
  }
  Color4 fromSun = ShaderGlobal::get_float4(get_shader_variable_id("from_sun_direction", true));
  Point3 dirToSun(-fromSun.r, -fromSun.g, -fromSun.b);
  bool ok = gi_verify::save_capture(gi_verify_request.dir.str(), cam.viewItm, cam.noJitterPersp, w, h, dirToSun,
    gi_verify_request.riDump.str(), levelBin.str(), &gi_verify::write_env_exr);
  console::print_d("gi_save_verify_capture: %s -> %s (ri dump: %s, level bin: %s)", ok ? "saved" : "FAILED",
    gi_verify_request.dir.str(), gi_verify_request.riDump.str(), levelBin.str());
}

static bool gi_verify_console_handler(const char *argv[], int argc)
{
  int found = 0;
  CONSOLE_CHECK_NAME("render", "gi_save_verify_capture", 1, 4)
  {
    gi_verify_request.dir = argc > 1 ? argv[1] : "gi_verify_capture";
    gi_verify_request.riDump = argc > 2 ? argv[2] : "ri_collisions.bin";
    gi_verify_request.levelBin = argc > 3 ? argv[3] : "";
    if (!dd_file_exists(gi_verify_request.riDump.str()))
      console::print_d("note: ri dump '%s' does not exist - run 'ri.dump_coll %s' first, the capture fails without it",
        gi_verify_request.riDump.str(), gi_verify_request.riDump.str());
    gi_verify_request.pending = true;
  }
  return found;
}
REGISTER_CONSOLE_HANDLER(gi_verify_console_handler);
#endif

dafg::NodeHandle makeGiCalcNode()
{
  return dafg::register_node("gi_before_frame_lit", DAFG_PP_NODE_SRC, [](dafg::Registry registry) {
    registry.orderMeAfter("combine_shadows_node");
    registry.readBlob<OrderingToken>("bvh_ready_token").optional();
    registry.createBlob<OrderingToken>("gi_before_frame_lit_token");

    // fixme: add ssao dependence
    auto &wr = *static_cast<WorldRenderer *>(get_world_renderer());
    if (wr.hasFeature(FeatureRenderFlags::COMBINED_SHADOWS))
    {
      registry.readTexture("combined_shadows").atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("combined_shadows");
      registry.read("combined_shadows_sampler").blob<d3d::SamplerHandle>().bindToShaderVar("combined_shadows_samplerstate");
    }
    read_gbuffer(registry);
    read_gbuffer_depth(registry);
    registry.requestState().setFrameBlock("global_frame");
    registry.read("motion_vecs").texture().atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("motion_gbuf").optional();
    registry.read("gbuf_sampler").blob<d3d::SamplerHandle>().bindToShaderVar("motion_gbuf_samplerstate").optional();

    registry.readTexture("close_depth").atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("downsampled_close_depth_tex").optional();
    registry.read("close_depth_sampler")
      .blob<d3d::SamplerHandle>()
      .bindToShaderVar("downsampled_close_depth_tex_samplerstate")
      .optional();

    registry.readTexture("checkerboard_depth")
      .atStage(dafg::Stage::PS_OR_CS)
      .bindToShaderVar("downsampled_checkerboard_depth_tex")
      .optional();
    registry.read("checkerboard_depth_sampler")
      .blob<d3d::SamplerHandle>()
      .bindToShaderVar("downsampled_checkerboard_depth_tex_samplerstate")
      .optional();

    registry.readTextureHistory("close_depth").atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("gi_prev_downsampled_close_depth_tex");
    registry.read("close_depth_sampler")
      .blob<d3d::SamplerHandle>()
      .bindToShaderVar("gi_prev_downsampled_close_depth_tex_samplerstate");

    registry.read("downsampled_normals").texture().atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("downsampled_normals").optional();
    registry.read("downsampled_normals_sampler")
      .blob<d3d::SamplerHandle>()
      .bindToShaderVar("downsampled_normals_samplerstate")
      .optional();
    registry.readTextureHistory("prev_frame_tex").atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("prev_frame_tex").optional();
    registry.read("prev_frame_sampler").blob<d3d::SamplerHandle>().bindToShaderVar("prev_frame_tex_samplerstate").optional();

    registry.readTexture("far_downsampled_depth").atStage(dafg::Stage::COMPUTE).bindToShaderVar("downsampled_far_depth_tex");
    registry.historyFor("far_downsampled_depth")
      .texture()
      .atStage(dafg::Stage::PS_OR_CS)
      .bindToShaderVar("prev_downsampled_far_depth_tex");
    {
      d3d::SamplerInfo smpInfo;
      smpInfo.address_mode_u = smpInfo.address_mode_v = smpInfo.address_mode_w = d3d::AddressMode::Clamp;
      smpInfo.filter_mode = d3d::FilterMode::Point;
      registry.create("gi_far_downsampled_depth_sampler")
        .blob<d3d::SamplerHandle>(d3d::request_sampler(smpInfo))
        .bindToShaderVar("downsampled_far_depth_tex_samplerstate")
        .bindToShaderVar("prev_downsampled_far_depth_tex_samplerstate");
    }
    auto hasAnyDynamicLights = registry.readBlob<bool>("has_any_dynamic_lights").handle();
    auto currentCameraHndl = registry.readBlob<CameraParams>("current_camera").handle();
    const auto resolution = registry.getResolution<2>("main_view");
    return [hasAnyDynamicLights, currentCameraHndl, resolution]() {
      auto &wr = *static_cast<WorldRenderer *>(get_world_renderer());

      if (!wr.daGI2)
        return;

      bvh_bind_resources(resolution.get().x);

      set_inv_globtm_to_shader(currentCameraHndl.ref().viewTm, currentCameraHndl.ref().jitterProjTm, false);

      bool allowFrustumLights = hasAnyDynamicLights.ref();
      wr.setGILightsToShader(allowFrustumLights);

      wr.daGI2->beforeFrameLit(wr.canChangeAltitudeUnexpectedly ? 0 : wr.giDynamicQuality);

      bvh_unbind_resources();
    };
  });
}

dafg::NodeHandle makeGiFeedbackNode()
{
  return dafg::register_node("gi_after_frame_lit", DAFG_PP_NODE_SRC, [](dafg::Registry registry) {
    registry.orderMeAfter("resolve_gbuffer_node");
    registry.readBlob<OrderingToken>("bvh_ready_token").optional();
    registry.executionHas(dafg::SideEffects::External);
    auto &wr = *static_cast<WorldRenderer *>(get_world_renderer());
    if (wr.hasFeature(FeatureRenderFlags::FULL_DEFERRED))
    {
      // fixme: add ssao dependence
      if (wr.hasFeature(FeatureRenderFlags::COMBINED_SHADOWS))
      {
        registry.readTexture("combined_shadows").atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("combined_shadows");
        registry.read("combined_shadows_sampler").blob<d3d::SamplerHandle>().bindToShaderVar("combined_shadows_samplerstate");
      }

      if (wr.hasFeature(FeatureRenderFlags::DEFERRED_LIGHT) && shader_exists("deferredLight"))
      {
        registry.readTexture("current_ambient").atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("current_ambient");
      }

      if (wr.hasFeature(FeatureRenderFlags::SSAO))
      {
        registry.readTexture("ssao_tex").atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("ssao_tex").optional();
        registry.read("ssao_sampler").blob<d3d::SamplerHandle>().bindToShaderVar("ssao_tex_samplerstate").optional();
      }

      read_gbuffer(registry);
      read_gbuffer_depth(registry);
    }
    registry.read("close_depth").texture().atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("downsampled_close_depth_tex");
    registry.read("close_depth_sampler").blob<d3d::SamplerHandle>().bindToShaderVar("downsampled_close_depth_tex_samplerstate");

    // far_downsampled_depth is needed for HZB occlusion
    registry.read("far_downsampled_depth").texture().atStage(dafg::Stage::PS_OR_CS).bindToShaderVar("downsampled_far_depth_tex");
    registry.read("far_downsampled_depth_sampler")
      .blob<d3d::SamplerHandle>()
      .bindToShaderVar("downsampled_far_depth_tex_samplerstate");
    registry.requestState().setFrameBlock("global_frame");

    auto hasAnyDynamicLights = registry.readBlob<bool>("has_any_dynamic_lights").handle();
    auto currentCameraHndl = registry.readBlob<CameraParams>("current_camera").handle();
    const auto resolution = registry.getResolution<2>("main_view");
    return [hasAnyDynamicLights, currentCameraHndl, resolution]() {
      auto &wr = *static_cast<WorldRenderer *>(get_world_renderer());

      if (!wr.daGI2)
        return;

      bvh_bind_resources(resolution.get().x);

      set_inv_globtm_to_shader(currentCameraHndl.ref().viewTm, currentCameraHndl.ref().jitterProjTm, false);
      const auto flags =
        wr.hasFeature(FeatureRenderFlags::FULL_DEFERRED) ? DaGI::FrameData(DaGI::FrameHasAll) : DaGI::FrameData(DaGI::FrameHasDepth);

      bool allowFrustumLights = hasAnyDynamicLights.ref() && (flags & DaGI::FrameHasLitScene);
      wr.setGILightsToShader(allowFrustumLights);

      const bool allowUpdateFromGbuf = !camera_in_camera::is_lens_render_active();
      wr.daGI2->afterFrameRendered(flags, allowUpdateFromGbuf);

#if DAGOR_DBGLEVEL > 0
      if (gi_verify_request.pending && wr.hasFeature(FeatureRenderFlags::FULL_DEFERRED))
        gi_verify_perform_capture(currentCameraHndl.ref(), resolution.get().x, resolution.get().y);
#endif

      bvh_unbind_resources();
    };
  });
}

dafg::NodeHandle makeGiScreenDebugDepthNode()
{
  return dafg::register_node("gi_screen_debug_depth_node", DAFG_PP_NODE_SRC, [](dafg::Registry registry) {
    auto debugNs = registry.root() / "debug";
    auto colorTarget = debugNs.modifyTexture("target_for_debug");
    registry.requestRenderPass().color({colorTarget});
    registry.readTexture("depth_for_postfx").atStage(dafg::Stage::PS).bindToShaderVar("depth_gbuf");
    return [] {
      auto &wr = *static_cast<WorldRenderer *>(get_world_renderer());
      if (!wr.daGI2)
        return;
      wr.daGI2->debugRenderScreenDepth();
    };
  });
}

dafg::NodeHandle makeGiScreenDebugNode()
{
  return dafg::register_node("gi_screen_debug_node", DAFG_PP_NODE_SRC, [](dafg::Registry registry) {
    auto debugNs = registry.root() / "debug";
    auto colorTarget = debugNs.modifyTexture("target_for_debug");
    registry.requestRenderPass().color({colorTarget});
    registry.readTexture("depth_for_postfx").atStage(dafg::Stage::PS).bindToShaderVar("depth_gbuf");
    return [] {
      auto &wr = *static_cast<WorldRenderer *>(get_world_renderer());
      if (!wr.daGI2)
        return;
      wr.daGI2->debugRenderScreen();
    };
  });
}
