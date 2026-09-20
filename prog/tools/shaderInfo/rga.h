// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <shaderBlobUnpack/shaderBlobUnpack.h>
#include <shaders/shader_layout.h>

namespace rga
{

#define RGA_USAGE_STRING "[-rga-extract:<DIR>] [-rga-device:<NAME>] [-rga-analysis] [-rga-livereg] [-rga-sgpr] [-rga-isa]"

struct Config
{
  const char *extractDir = nullptr;
  const char *extractNameBase = nullptr;
  const char *device = nullptr;
  bool analysis = false;
  bool liveregAnalysis = false;
  bool liveregSgprAnalysis = false;
  bool isa = false;
  const char *toolInstallation = nullptr;

  bool operator==(const Config &) const = default;
};

struct ExtractionContext
{
  static constexpr size_t STAGE_COUNT = 6;

  String extractedPathes[STAGE_COUNT]{};
  String gpsoPath;
  String rootSigPath;
  shader_blob::UnpackedShader vsUnpackScratch{};
  shader_blob::UnpackedShader psOrCsUnpackScratch{};
  shader_blob::Api detectedApi = shader_blob::Api::INVALID;
};

bool parse_arg_to_config(const char *arg, Config &config);

bool finalize_config(const char *shader_name, Config &config);

String additional_usage_string();

bool need_module_extraction(const Config &config);

bool extract_vertex_stage(const ShaderSource &src, int idx, ExtractionContext &ctx, const Config &config);
bool extract_pixel_stage(const ShaderSource &src, int idx, ExtractionContext &ctx, const Config &config);
bool extract_compute_stage(const ShaderSource &src, int idx, ExtractionContext &ctx, const Config &config);

bool extract_pipeline_state(const shaders::RenderState &render_state, int idx, ExtractionContext &ctx, const Config &config);

bool extracting_graphics_pipeline(const ExtractionContext &ctx);

bool run_rga_analysis(const ExtractionContext &ctx, const Config &config);

} // namespace rga
