// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "rga.h"
#include "rgaPipelineState.h"

#include <libTools/util/fileUtils.h>
#include <osApiWrappers/dag_files.h>
#include <osApiWrappers/dag_direct.h>
#include <osApiWrappers/dag_miscApi.h>
#include <generic/dag_enumerate.h>
#include <EASTL/vector_map.h>

namespace rga
{

#if defined(RGA_DIR_REL_TO_EXE)
#define STR_IMPL(x)            #x
#define STR(x)                 STR_IMPL(x)
#define RGA_DIR_REL_TO_EXE_STR STR(RGA_DIR_REL_TO_EXE)
#endif

static const eastl::vector_map<eastl::string_view, eastl::string_view> additional_device_aliases = {
  {"ps5-imprecise", "gfx1102"},
  {"scarlett-s-imprecise", "gfx1102"},
  {"scarlett-x-imprecise", "gfx1101"},
};

static constexpr const char *STAGE_ARGS[][ExtractionContext::STAGE_COUNT] = {
  {
    // dx11
    "",
    "",
    "",
    "",
    "",
    "",
  },
  {
    // dx12
    "--vs-blob",
    "--hs-blob",
    "--ds-blob",
    "--gs-blob",
    "--ps-blob",
    "--cs-blob",
  },
  {
    // spirv
    "--vert",
    "--tesc",
    "--tese",
    "--geom",
    "--frag",
    "--comp",
  },
};
static constexpr const char *STAGE_TAGS[ExtractionContext::STAGE_COUNT] = {
  ".vs",
  ".hs",
  ".ds",
  ".gs",
  ".ps",
  ".cs",
};

static bool string_arg_is_missing(const char *value) { return !value || strlen(value) == 0; }

static bool need_rga_invocation(const Config &config)
{
  return config.analysis || config.liveregAnalysis || config.liveregSgprAnalysis || config.isa;
}

static void parse_rga_device(const char *arg, Config &config)
{
  eastl::string_view name{arg};
  if (auto it = additional_device_aliases.find(name); it != additional_device_aliases.end())
    config.device = it->second.data();
  else
    config.device = arg;
}

static bool path_contains_spaces(const char *path)
{
  return !string_arg_is_missing(path) &&
         eastl::any_of((const unsigned char *)path, (const unsigned char *)path + strlen(path), &isspace);
}

bool parse_arg_to_config(const char *arg, Config &config)
{
  if (strncmp(arg, "-rga-extract:", 13) == 0)
    config.extractDir = arg + 13;
  else if (strncmp(arg, "-rga-device:", 12) == 0)
    parse_rga_device(arg + 12, config);
  else if (strcmp(arg, "-rga-analysis") == 0)
    config.analysis = true;
  else if (strcmp(arg, "-rga-livereg") == 0)
    config.liveregAnalysis = true;
  else if (strcmp(arg, "-rga-sgpr") == 0)
    config.liveregSgprAnalysis = true;
  else if (strcmp(arg, "-rga-isa") == 0)
    config.isa = true;
  else
    return false;

  return true;
}

static bool validate_config(const Config &config)
{
  if (string_arg_is_missing(config.extractDir))
  {
    printf("ERR: rga: usage of rga arguments requires -rga-extract:<DIR> argument specified\n");
    return false;
  }
  if (string_arg_is_missing(config.extractNameBase))
  {
    printf("ERR: rga: usage of rga requires -shader:name#N.M filter specified\n");
    return false;
  }
  if (string_arg_is_missing(config.toolInstallation) && need_rga_invocation(config))
  {
    printf("ERR: rga: analysis/isa options (-rga-analysis -rga-livereg -rga-sgpr -rga-isa) require an RGA installation -- either "
           "update devtools, or define the RDTS_ROOT environment variable for a custom installation\n");
    return false;
  }
  if (string_arg_is_missing(config.device) && need_rga_invocation(config))
  {
    printf("ERR: rga: analysis/isa options (-rga-analysis -rga-livereg -rga-sgpr -rga-isa) require -rga-device:<NAME> argument "
           "specified\n");
    return false;
  }

  if (path_contains_spaces(config.extractDir))
  {
    printf("ERR: rga: extract paths with spaces are not supported, provided: '%s'\n", config.extractDir);
    return false;
  }
  if (path_contains_spaces(config.extractNameBase))
  {
    printf("ERR: rga: shader names with spaces are invalid, provided: '%s'\n", config.extractNameBase);
    return false;
  }
  if (need_rga_invocation(config) && path_contains_spaces(config.toolInstallation))
  {
    printf("ERR: rga: installation paths with spaces are not supported, provided: '%s'\n", config.toolInstallation);
    return false;
  }

  printf("RGA configured to:\n"
         "  dest directory: %s\n"
         "  target device: %s\n"
         "  analysis summary: %s\n"
         "  livereg analysis: %s\n"
         "  sgpr analysis: %s\n"
         "  isa dump: %s\n",
    config.extractDir, config.device ? config.device : "unspecified", config.analysis ? "yes" : "no",
    config.liveregAnalysis ? "yes" : "no", config.liveregSgprAnalysis ? "yes" : "no", config.isa ? "yes" : "no");
  return true;
}

bool finalize_config(const char *shader_name, Config &config)
{
  if (config != Config{})
  {
    config.extractNameBase = shader_name;
#if defined(RGA_DIR_REL_TO_EXE_STR)
    static String buf;
    buf.resize(1024);
    dag_get_appmodule_dir(buf.data(), buf.size());
    buf.updateSz();
    buf.append("/");
    buf.append(RGA_DIR_REL_TO_EXE_STR);
    config.toolInstallation = buf.c_str();
#endif
    if (const char *customPath = getenv("RDTS_ROOT"))
      config.toolInstallation = customPath;

    if (!validate_config(config))
      return false;

    dd_mkdir(config.extractDir);
  }
  return true;
}

String additional_usage_string()
{
  String rgaTargetAliasString{};
  rgaTargetAliasString += "RGA target alias list:\n";
  for (auto pair : additional_device_aliases)
    rgaTargetAliasString.aprintf(0, "  %s: %s\n", pair.first, pair.second);
  return rgaTargetAliasString;
}

bool need_module_extraction(const Config &config) { return config.extractDir != nullptr; }

static const char *api_to_rga_mode(shader_blob::Api api)
{
  switch (api)
  {
    case shader_blob::Api::DX11: return "dx11";
    case shader_blob::Api::DX12: return "dx12";
    case shader_blob::Api::SPIRV: return "vulkan";
    default: printf("ERR: rga: invalid api '%s' for rga analysis\n", shader_blob::api_name(api)); return nullptr;
  }
}
static bool extract_one(const ShaderSource &src, shader_blob::Stage stage_hint, const char *sub, int idx,
  shader_blob::UnpackedShader &unpacked, ExtractionContext &ctx, const Config &config)
{
  eastl::string error;
  auto blobApi = shader_blob::detect_api(src.metadata);
  if (!blobApi)
  {
    printf("ERR: rga-extract: failed to detect api: %s\n", blobApi.error().c_str());
    return false;
  }
  if (ctx.detectedApi == shader_blob::Api::INVALID)
  {
    ctx.detectedApi = *blobApi;
  }
  else if (ctx.detectedApi != *blobApi)
  {
    printf("ERR: rga-extract: dump contains blobs from divergent backends '%s' and '%s', thus it is malformed\n",
      shader_blob::api_name(ctx.detectedApi), shader_blob::api_name(*blobApi));
    return false;
  }

  if (!shader_blob::unpack(ctx.detectedApi, src, stage_hint, unpacked, &error))
  {
    printf("ERR: rga-extract: failed to unpack %s%d: %s\n", sub, idx, error.c_str());
    return false;
  }
  for (const auto &[i, stage] : enumerate(unpacked.stages))
  {
    if (stage.stage == shader_blob::Stage::INVALID)
    {
      printf("rga-extract: %s%d has a stage of unsupported type (raytrace?), skipped\n", sub, idx);
      continue;
    }
    String fn(0, "%s/%s.%s.%s", config.extractDir, config.extractNameBase, shader_blob::stage_name(stage.stage),
      shader_blob::bytecode_ext(ctx.detectedApi));
    {
      FullFileSaveCB cwr(fn);
      cwr.write(stage.bytecode.data(), stage.bytecode.size());
      printf("rga-extract: %s%d -> %s (%d bytes)\n", sub, idx, fn.str(), (int)stage.bytecode.size());
    }

    int slot = -1;
    switch (stage.stage)
    {
      case shader_blob::Stage::VS: slot = 0; break;
      case shader_blob::Stage::HS: slot = 1; break;
      case shader_blob::Stage::DS: slot = 2; break;
      case shader_blob::Stage::GS: slot = 3; break;
      case shader_blob::Stage::PS: slot = 4; break;
      case shader_blob::Stage::CS: slot = 5; break;
      default: break;
    }
    if (slot < 0)
    {
      printf("rga-extract: %s%d has a stage with no rga slot (%s), skipped\n", sub, idx, shader_blob::stage_name(stage.stage));
      continue;
    }
    ctx.extractedPathes[slot] = eastl::move(fn);
  }
  return true;
}

bool extract_vertex_stage(const ShaderSource &src, int idx, ExtractionContext &ctx, const Config &config)
{
  return extract_one(src, shader_blob::Stage::VS, "vs", idx, ctx.vsUnpackScratch, ctx, config);
}

bool extract_pixel_stage(const ShaderSource &src, int idx, ExtractionContext &ctx, const Config &config)
{
  return extract_one(src, shader_blob::Stage::PS, "ps", idx, ctx.psOrCsUnpackScratch, ctx, config);
}

bool extract_compute_stage(const ShaderSource &src, int idx, ExtractionContext &ctx, const Config &config)
{
  return extract_one(src, shader_blob::Stage::CS, "cs", idx, ctx.psOrCsUnpackScratch, ctx, config);
}

bool extract_pipeline_state(const shaders::RenderState &render_state, int idx, ExtractionContext &ctx, const Config &config)
{
  if (!extracting_graphics_pipeline(ctx))
    return true;

  G_ASSERT(ctx.detectedApi != shader_blob::Api::INVALID);
  if (ctx.detectedApi == shader_blob::Api::DX11) // Rga analyzes dx11 shader per-module w/o render state
    return true;

  const char *ext = shader_blob::bytecode_ext(ctx.detectedApi);

  eastl::string error;
  ctx.gpsoPath.printf(0, "%s/%s.%s.gpso", config.extractDir, config.extractNameBase, ext);
  bool psoOk = true;
  bool rootsigOk = true;
  if (ctx.detectedApi == shader_blob::Api::DX12)
  {
    psoOk = pso::write_dx12_gpso(ctx.gpsoPath, ctx.vsUnpackScratch, ctx.psOrCsUnpackScratch, error);
    // for a null-PS pipeline RGA autogenerates the missing PS from an hlsl stub, which is
    // incompatible with a serialized root signature; let it autogenerate the signature too
    if (!ctx.psOrCsUnpackScratch.stages.empty())
    {
      ctx.rootSigPath.printf(0, "%s/%s.%s.rs.bin", config.extractDir, config.extractNameBase, ext);
      rootsigOk = rga::pso::write_dx12_root_signature(ctx.rootSigPath, ctx.vsUnpackScratch, ctx.psOrCsUnpackScratch, error);
    }
  }
  else
  {
    psoOk = rga::pso::write_vulkan_gpso(ctx.gpsoPath, ctx.vsUnpackScratch, ctx.psOrCsUnpackScratch, render_state, error);
  }
  if (!psoOk)
    printf("ERR: rga-extract: failed to write pipeline state: %s\n", error.c_str());
  if (!rootsigOk)
    printf("ERR: rga-extract: failed to write root signature: %s\n", error.c_str());
  if (!psoOk || !rootsigOk)
    return false;
  printf("rga-extract: pipeline state (rs%d) -> %s\n", idx, ctx.gpsoPath.str());
  if (!ctx.rootSigPath.empty())
    printf("rga-extract: root signature -> %s\n", ctx.rootSigPath.str());
  return true;
}

bool extracting_graphics_pipeline(const ExtractionContext &ctx) { return !ctx.vsUnpackScratch.stages.empty(); }

bool run_rga_analysis(const ExtractionContext &ctx, const Config &config)
{
  if (!need_rga_invocation(config))
    return true;

  const char *ext = shader_blob::bytecode_ext(ctx.detectedApi);

  auto runRgaOnce = [&](String command) {
    String tempOutFile(0, "%s/__tmp_rga_output_%d__.txt", config.extractDir, get_process_uid());
    command.aprintf(0, " > \"%s\" 2>&1", tempOutFile.c_str());

    int res = system(command.c_str());
    String output;
    {
      FullFileLoadCB crd(tempOutFile);
      if (!crd.fileHandle)
      {
        printf("ERR: can't open <%s>\nCommandline:%s\n", tempOutFile.c_str(), command.c_str());
        return false;
      }
      const int outputLength = df_length(crd.fileHandle);
      output.resize(outputLength + 1);
      crd.read(output.data(), outputLength);
      output[outputLength] = 0;
    }
    dd_erase(tempOutFile.c_str());

    // RGA seems to at least sometimes fail with 0 code, so need to check output as well. Seems like a good marker is the word
    // "succeeded", which is the last in the output. Note, that this should be revised on every RGA version update uploaded to
    // devtools.
    if (res != 0 || !output.find("succeeded"))
    {
      printf("ERR: rga execution failed\nCommandline: %s\nOutput:\n%s\n", command.c_str(), output.c_str());
      return false;
    }

    // RGA silently switches to reflection-guessed state or offline compilation when the
    // pipeline can not be built as specified; such results do not match the runtime pipeline
    if (output.find("falling back") || output.find("Auto-generating"))
      printf("rga: WARNING: rga used a fallback compilation path, results may be inaccurate:\n%s\n", output.c_str());

    printf("rga: successful tool execution\n");
    return true;
  };

  auto fillAnalysisArgs = [&](String &commandline, const char *tag = "") {
    if (config.analysis)
      commandline.aprintf(0, " -a %s/%s%s.%s.analysis.csv", config.extractDir, config.extractNameBase, tag, ext);
    if (config.liveregAnalysis)
      commandline.aprintf(0, " --livereg %s/%s%s.%s.livereg.txt", config.extractDir, config.extractNameBase, tag, ext);
    if (config.liveregSgprAnalysis)
      commandline.aprintf(0, " --livereg-sgpr %s/%s%s.%s.sgpr.txt", config.extractDir, config.extractNameBase, tag, ext);
    if (config.isa)
      commandline.aprintf(0, " --isa %s/%s%s.%s.isa", config.extractDir, config.extractNameBase, tag, ext);
  };

  String rgaCommandline{};
  rgaCommandline.aprintf(0, "%s/rga", config.toolInstallation);

  rgaCommandline.aprintf(0, " -s %s", api_to_rga_mode(ctx.detectedApi));

  if (config.device)
    rgaCommandline.aprintf(0, " -c %s", config.device);

  if (ctx.detectedApi == shader_blob::Api::DX11)
  {
    rgaCommandline.aprintf(0, " --dxbc");
    for (int i = 0; i < c_countof(ctx.extractedPathes); ++i)
      if (!ctx.extractedPathes[i].empty())
      {
        String singleModuleCmdline = rgaCommandline;
        fillAnalysisArgs(singleModuleCmdline, STAGE_TAGS[i]);
        singleModuleCmdline.aprintf(0, " %s %s", STAGE_ARGS[int(ctx.detectedApi)][i], ctx.extractedPathes[i].c_str());
        if (!runRgaOnce(singleModuleCmdline))
          return false;
      }
  }
  else
  {
    if (ctx.detectedApi == shader_blob::Api::DX12)
    {
      rgaCommandline.aprintf(0, " --offline");
      if (!ctx.rootSigPath.empty())
        rgaCommandline.aprintf(0, " --rs-bin %s", ctx.rootSigPath.c_str());
    }
    if (!ctx.gpsoPath.empty())
      rgaCommandline.aprintf(0, " %s %s", ctx.detectedApi == shader_blob::Api::DX12 ? "--gpso" : "--pso", ctx.gpsoPath.c_str());
    fillAnalysisArgs(rgaCommandline);
    for (int i = 0; i < c_countof(ctx.extractedPathes); ++i)
      if (!ctx.extractedPathes[i].empty())
        rgaCommandline.aprintf(0, " %s %s", STAGE_ARGS[int(ctx.detectedApi)][i], ctx.extractedPathes[i].c_str());
    if (!runRgaOnce(rgaCommandline))
      return false;
  }
  return true;
}

} // namespace rga
