// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "rga.h"
#include <shadersBinaryData.h>
#include <shaders/shUtils.h>
#include <shaders/shader_name_format.h>
#include <shaders/shOpcode.h>
#include <shaders/shOpcodeFormat.h>
#include <shaders/shFunc.h>
#include <drv/3d/dag_sampler.h>
#include <ioSys/dag_fileIo.h>
#include <ioSys/dag_zstdIo.h>
#include <osApiWrappers/dag_files.h>
#include <osApiWrappers/dag_direct.h>
#include <osApiWrappers/dag_miscApi.h>
#include <debug/dag_debug.h>
#include <debug/dag_logSys.h>
#include <perfMon/dag_cpuFreq.h>
#include <util/dag_string.h>
#include <stdio.h>
#include <stdlib.h>
#include <EASTL/string.h>
#include <EASTL/vector_set.h>
#include <EASTL/vector_map.h>
#include <EASTL/vector.h>
#include <EASTL/sort.h>
#include <generic/dag_span.h>
#include <generic/dag_enumerate.h>
#include <util/dag_strUtil.h>
#include <util/dag_stringify.h>
#include <shaderBlobDisassembler/disasm.h>
#include <shaderBlobUnpack/shaderBlobUnpack.h>
#define USE_SHA1_HASH 0

#if _TARGET_PC_WIN
#include <d3dcompiler.h>
static void disassembleShader(dag::ConstSpan<uint32_t> native_code, const char *out_fn = nullptr)
{
  if (native_code.empty())
    return;
  ID3DBlob *disassembly = nullptr;
  HRESULT hr = E_FAIL;
  if (native_code.size() * 4 == native_code[0] + 12)
  { // DX11 uncompressed shaders
    hr = D3DDisassemble(native_code.data() + 3, data_size(native_code) - 12, 0, nullptr, &disassembly);
  }
  else if (native_code[3] == _MAKE4C('SH.z'))
  {
    debug("Outdated compressed shader format [SH.z] that is no longer supported");
    return;
  }
  else if (native_code[0] == _MAKE4C('SVu3'))
  { // SpirV shaders
  }
  else if (native_code[0] == _MAKE4C('sx12'))
  { // DX12 shaders (DXIL or DXBC)
  }

  if (hr == S_OK && disassembly && disassembly->GetBufferPointer() && *(char *)disassembly->GetBufferPointer())
  {
    if (out_fn)
    {
      FullFileSaveCB cwr(out_fn);
      cwr.write(disassembly->GetBufferPointer(), disassembly->GetBufferSize());
    }
    else
      debug("%s", disassembly->GetBufferPointer());
  }
  else if (!out_fn)
  {
    String dump_str(native_code.size() * 3 + 64, "  [%d words]\n", native_code.size());
    for (const uint32_t *c = native_code.data(), *c_e = c + native_code.size(); c < c_e; c += 16)
      if (c + 16 <= c_e)
        dump_str.aprintf(0, "  %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n", c[0], c[1], c[2],
          c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11], c[12], c[13], c[14], c[15]);
      else
      {
        dump_str.aprintf(0, "  %08X", c[0]);
        for (c++; c < c_e; c++)
          dump_str.aprintf(0, " %08X", c[0]);
        dump_str.aprintf(0, "\n");
      }
    debug_(dump_str);
  }

  if (disassembly)
    disassembly->Release();
}
#else
extern int __argc;
extern char **__argv;

static void disassembleShader(dag::ConstSpan<uint32_t> native_code, const char *out_fn = nullptr) {}
#endif
static void dumpCurrentShaders(const char *single_shader, shaderbindump::DumpDetails details, shaderbindump::SortType sortby,
  const char *sh_dir);

bool need_disasm = false;

static rga::Config rga_config = {};

static void showUsage()
{
  printf("\nUsage:\n"
         "  shaderInfo-dev.exe <in_shdump.bin> [-shader:<name[#STVARCODE.DYNVARCODE]>] [-outfile:<FILE>] [-outdir:<DIR>]\n"
         "    [-preset:MIN|BRIEF|TABLES|DETAIL|FULL](default:DETAIL)\n"
         "    [-globvars] [-nstblocks] [-maxreg] [-shaders] [-asm] [-stcode] [-rstates] [-attribvariants] [-samplers]\n"
         "    [-sortby:NAME|PASS](default:NAME)\n"
         "    [-disasm]\n" // Target is inferred from the blobs
         "    " RGA_USAGE_STRING "\n"
         "    [-headeronly] [-locvars] [-stinit] [-varsummary] [-stvartable]\n"
         "    [-codes] [-stvarmap] [-vtxchnl] [-dynvartable] [-variants]\n"
         "    [-fullprecision]\n"
         "\n%s",
    rga::additional_usage_string().c_str());
}

int DagorWinMain(bool debugmode)
{
  printf("Shader Info Tool v1.2\n"
         "Copyright (C) Gaijin Games KFT, 2026\n");

  if (__argc < 2)
  {
    showUsage();
    return 1;
  }

  const char *single_shader = nullptr;
  const char *output_log_fn = "shader_dump";
  const char *output_dir = nullptr;

  shaderbindump::DumpDetails userOutputDetails = {};

  auto outputPreset = shaderbindump::DumpDetails::Preset::DEFAULT;
  auto sortType = shaderbindump::SortType::DEFAULT;


  for (int i = 2; i < __argc; i++)
    if (strncmp(__argv[i], "-shader:", 8) == 0)
      single_shader = __argv[i] + 8;
    else if (strncmp(__argv[i], "-outfile:", 9) == 0)
      output_log_fn = __argv[i] + 9;
    else if (strncmp(__argv[i], "-outdir:", 8) == 0)
      output_dir = __argv[i] + 8;

    else if (strncmp(__argv[i], "-preset:", 8) == 0)
    {
      const char *presetName = __argv[i] + 8;
      if (strcmp(presetName, "MIN") == 0)
        outputPreset = shaderbindump::DumpDetails::Preset::MINIMAL;
      else if (strcmp(presetName, "BRIEF") == 0)
        outputPreset = shaderbindump::DumpDetails::Preset::BRIEF;
      else if (strcmp(presetName, "TABLES") == 0)
        outputPreset = shaderbindump::DumpDetails::Preset::TABLES;
      else if (strcmp(presetName, "DETAIL") == 0)
        outputPreset = shaderbindump::DumpDetails::Preset::DETAIL;
      else if (strcmp(presetName, "FULL") == 0)
        outputPreset = shaderbindump::DumpDetails::Preset::FULL;
      else
      {
        printf("ERR: unsupported preset <%s>\n"
               "  supported presets: MIN BRIEF TABLES DETAIL FULL\n",
          presetName);
        return 1;
      }
    }
    else if (strncmp(__argv[i], "-sortby:", 8) == 0)
    {
      const char *sortMode = __argv[i] + 8;
      if (strcmp(sortMode, "NAME") == 0)
        sortType = shaderbindump::SortType::NAME;
      else if (strcmp(sortMode, "PASS") == 0)
        sortType = shaderbindump::SortType::PASS;
      else
      {
        printf("ERR: unsupported sort mode <%s>\n"
               "  supported modes: NAME PASS\n",
          sortMode);
        return 1;
      }
    }
    else if (strcmp(__argv[i], "-disasm") == 0)
      need_disasm = true;
    else if (rga::parse_arg_to_config(__argv[i], rga_config))
      ;

    // overall output options
    else if (strcmp(__argv[i], "-globvars") == 0)
      userOutputDetails.globalVars = true;
    else if (strcmp(__argv[i], "-nstblocks") == 0)
      userOutputDetails.namedStateBlocks = true;
    else if (strcmp(__argv[i], "-maxreg") == 0)
      userOutputDetails.maxregCount = true;
    else if (strcmp(__argv[i], "-shaders") == 0)
      userOutputDetails.shaders = true;
    else if (strcmp(__argv[i], "-asm") == 0)
      userOutputDetails.assembly = true;
    else if (strcmp(__argv[i], "-stcode") == 0)
      userOutputDetails.stcode = true;
    else if (strcmp(__argv[i], "-rstates") == 0)
      userOutputDetails.renderStates = true;
    else if (strcmp(__argv[i], "-attribvariants") == 0)
      userOutputDetails.variantAttributions = true;
    else if (strcmp(__argv[i], "-samplers") == 0)
      userOutputDetails.samplers = true;
    // shader output options
    else if (strcmp(__argv[i], "-headeronly") == 0)
      userOutputDetails.shaderDetails.headerOnly = true;
    else if (strcmp(__argv[i], "-locvars") == 0)
      userOutputDetails.shaderDetails.localVars = true;
    else if (strcmp(__argv[i], "-stinit") == 0)
      userOutputDetails.shaderDetails.staticInit = true;
    else if (strcmp(__argv[i], "-varsummary") == 0)
      userOutputDetails.shaderDetails.varSummary = true;
    else if (strcmp(__argv[i], "-stvartable") == 0)
      userOutputDetails.shaderDetails.staticVariantTable = true;
    else if (strcmp(__argv[i], "-codes") == 0)
      userOutputDetails.shaderDetails.codeBlocks = true;
    else if (strcmp(__argv[i], "-stvarmap") == 0)
      userOutputDetails.shaderDetails.stvarmap = true;
    else if (strcmp(__argv[i], "-vtxchnl") == 0)
      userOutputDetails.shaderDetails.vertexChannels = true;
    else if (strcmp(__argv[i], "-dynvartable") == 0)
      userOutputDetails.shaderDetails.dynamicVariantTable = true;
    else if (strcmp(__argv[i], "-variants") == 0)
      userOutputDetails.shaderDetails.dumpVariants = true;
    else if (strcmp(__argv[i], "-fullprecision") == 0)
      shaderbindump::g_full_float_precision = true;

    else
    {
      printf("ERR: unsupported switch <%s>\n", __argv[i]);
      showUsage();
      return 1;
    }

  if (!rga::finalize_config(single_shader, rga_config))
    exit(1);

  // if none of output options are specified - use output preset (stated or default)
  // if at least one is specified - use user output options
  shaderbindump::DumpDetails outputDetails =
    userOutputDetails == shaderbindump::DumpDetails() ? shaderbindump::DumpDetails(outputPreset) : userOutputDetails;
  outputDetails.validate();


  FullFileLoadCB crd(__argv[1]);
  if (!crd.fileHandle)
  {
    printf("ERR: can't open <%s>\n", __argv[1]);
    return 1;
  }

  start_classic_debug_system(output_log_fn);
  debug_enable_timestamps(false);

  if (!shBinDumpOwner().loadFromFile(crd, df_length(crd.fileHandle)))
  {
    printf("ERR: can't read shaders binary dump from <%s>\n", __argv[1]);
    return 1;
  }

  printf("loaded binary dump %s (memSize=%uK)\n"
         "%d shaders, %d total vars, %d glob var\n"
         "dumping contents to: \"%s\"\n"
         "  globals:[%s %s %s %s %s %s %s]\n",
    __argv[1], (uint32_t)(shBinDumpOwner().getDumpSize() >> 10), (int)shBinDump().classes.size(), (int)shBinDump().varMap.size(),
    (int)shBinDump().globVars.v.size(), output_log_fn, outputDetails.globalVars ? "GLOB_VARS" : "",
    outputDetails.namedStateBlocks ? "NAMED_STBLOCKS" : "", outputDetails.maxregCount ? "MAXREG_CNT" : "",
    outputDetails.shaders ? "SHADERS" : "", outputDetails.assembly ? "ASM" : "", outputDetails.stcode ? "STCODE" : "",
    outputDetails.samplers ? "SAMPLERS" : "");

  if (outputDetails.shaders)
  {
    printf("  shaders:[%s %s %s %s %s %s %s %s %s %s]\n", outputDetails.shaderDetails.headerOnly ? "HEADER" : "",
      outputDetails.shaderDetails.localVars ? "LOCAL_VARS" : "", outputDetails.shaderDetails.staticInit ? "ST_INIT" : "",
      outputDetails.shaderDetails.varSummary ? "VARIANT_SUMMARY" : "",
      outputDetails.shaderDetails.staticVariantTable ? "ST_VARIANT_TABLE" : "", outputDetails.shaderDetails.codeBlocks ? "CODES" : "",
      outputDetails.shaderDetails.stvarmap ? "ST_VAR_MAP" : "", outputDetails.shaderDetails.vertexChannels ? "VERTEX_CHANNELS" : "",
      outputDetails.shaderDetails.dynamicVariantTable ? "DYN_VARIANT_TABLE" : "",
      outputDetails.shaderDetails.dumpVariants ? "VARIANTS" : "");
  }

  if (single_shader)
    printf("(dumping only contents of <%s> shader)\n", single_shader);
  if (output_dir)
    printf("(dumping with content-like names to folder: %s/ )\n", output_dir);

  int t0 = get_time_msec();
  dumpCurrentShaders(single_shader, outputDetails, sortType, output_dir);
  printf("\ndumped for %.1f seconds\n", (get_time_msec() - t0) / 1000.0f);

  return 0;
}

static String mk_str_cat3(const char *s1, const char *s2, const char *s3)
{
  String s;
  s.setStrCat3(s1, s2, s3);
  return s;
}

static String mk_str_cat4(const char *s1, const char *s2, const char *s3, const char *s4)
{
  String s;
  s.setStrCat4(s1, s2, s3, s4);
  return s;
}

#if USE_SHA1_HASH
#include <hash/sha1.h>
#define HASH_SIZE    20
#define HASH_CONTEXT sha1_context
#define HASH_UPDATE  sha1_update
#define HASH_INIT    sha1_starts
#define HASH_FINISH  sha1_finish

#else
#include <hash/BLAKE3/blake3.h>

#define HASH_SIZE    32
inline void blake3_finalize_32(const blake3_hasher *h, unsigned char *hash) { blake3_hasher_finalize(h, hash, HASH_SIZE); }
#define HASH_CONTEXT blake3_hasher
#define HASH_UPDATE  blake3_hasher_update
#define HASH_INIT    blake3_hasher_init
#define HASH_FINISH  blake3_finalize_32
#endif
static String calc_sha1(dag::ConstSpan<uint32_t> native_code)
{
  HASH_CONTEXT sha1;
  unsigned char srcSha1[HASH_SIZE];

  HASH_INIT(&sha1);
  HASH_UPDATE(&sha1, (const unsigned char *)native_code.data(), data_size(native_code));
  HASH_FINISH(&sha1, srcSha1);

  String s;
  data_to_str_hex(s, srcSha1, sizeof(srcSha1));
  return s;
}

dag::ConstSpan<uint32_t> uncompressShader(const ShaderSource &source, Tab<uint8_t> &tmpbuf)
{
  return make_span_const(source.uncompress(tmpbuf), source.uncompressedSize / 4);
}


static void dump_render_state(const shaders::RenderState &state)
{
  auto bitToText = [](auto bit) { return bit ? "yes" : "no"; };
  auto cmpfToText = [](auto cmpf) {
    switch (cmpf)
    {
      case CMPF_NEVER: return "CMPF_NEVER";
      case CMPF_LESS: return "CMPF_LESS";
      case CMPF_EQUAL: return "CMPF_EQUAL";
      case CMPF_LESSEQUAL: return "CMPF_LESSEQUAL";
      case CMPF_GREATER: return "CMPF_GREATER";
      case CMPF_NOTEQUAL: return "CMPF_NOTEQUAL";
      case CMPF_GREATEREQUAL: return "CMPF_GREATEREQUAL";
      case CMPF_ALWAYS: return "CMPF_ALWAYS";
      default: return "<invalid data>";
    }
  };
  auto cullToText = [](auto cull) {
    switch (cull)
    {
      case CULL_NONE: return "CULL_NONE";
      case CULL_CW: return "CULL_CW";
      case CULL_CCW: return "CULL_CCW";
      default: return "<invalid data>";
    }
  };
  auto stnclopToText = [](auto stnclop) {
    switch (stnclop)
    {
      case STNCLOP_KEEP: return "STNCLOP_KEEP";
      case STNCLOP_ZERO: return "STNCLOP_ZERO";
      case STNCLOP_REPLACE: return "STNCLOP_REPLACE";
      case STNCLOP_INCRSAT: return "STNCLOP_INCRSAT";
      case STNCLOP_DECRSAT: return "STNCLOP_DECRSAT";
      case STNCLOP_INVERT: return "STNCLOP_INVERT";
      case STNCLOP_INCR: return "STNCLOP_INCR";
      case STNCLOP_DECR: return "STNCLOP_DECR";
      default: return "<invalid data>";
    }
  };
  auto blendToText = [](auto blend) {
    switch (blend)
    {
      case BLEND_ZERO: return "BLEND_ZERO";
      case BLEND_ONE: return "BLEND_ONE";
      case BLEND_SRCCOLOR: return "BLEND_SRCCOLOR";
      case BLEND_INVSRCCOLOR: return "BLEND_INVSRCCOLOR";
      case BLEND_SRCALPHA: return "BLEND_SRCALPHA";
      case BLEND_INVSRCALPHA: return "BLEND_INVSRCALPHA";
      case BLEND_DESTALPHA: return "BLEND_DESTALPHA";
      case BLEND_INVDESTALPHA: return "BLEND_INVDESTALPHA";
      case BLEND_DESTCOLOR: return "BLEND_DESTCOLOR";
      case BLEND_INVDESTCOLOR: return "BLEND_INVDESTCOLOR";
      case BLEND_SRCALPHASAT: return "BLEND_SRCALPHASAT";
      case BLEND_BOTHINVSRCALPHA: return "BLEND_BOTHINVSRCALPHA";
      case BLEND_BLENDFACTOR: return "BLEND_BLENDFACTOR";
      case BLEND_INVBLENDFACTOR: return "BLEND_INVBLENDFACTOR";
      case EXT_BLEND_SRC1COLOR: return "EXT_BLEND_SRC1COLOR";
      case EXT_BLEND_INVSRC1COLOR: return "EXT_BLEND_INVSRC1COLOR";
      case EXT_BLEND_SRC1ALPHA: return "EXT_BLEND_SRC1ALPHA";
      case EXT_BLEND_INVSRC1ALPHA: return "EXT_BLEND_INVSRC1ALPHA";
      default: return "<invalid data>";
    }
  };
  auto blendopToText = [](auto blendop) {
    switch (blendop)
    {
      case BLENDOP_ADD: return "BLENDOP_ADD";
      case BLENDOP_SUBTRACT: return "BLENDOP_SUBTRACT";
      case BLENDOP_REVSUBTRACT: return "BLENDOP_REVSUBTRACT";
      case BLENDOP_MIN: return "BLENDOP_MIN";
      case BLENDOP_MAX: return "BLENDOP_MAX";
      default: return "<invalid data>";
    }
  };
  auto dumpBlendParams = [&](auto &desc, const auto &params, const char *ident) {
    desc.append_sprintf("%sablendFactors{\n", ident);
    desc.append_sprintf("%s  src=%s\n", ident, blendToText(params.ablendFactors.src));
    desc.append_sprintf("%s  dst=%s\n", ident, blendToText(params.ablendFactors.dst));
    desc.append_sprintf("%s  }\n", ident);
    desc.append_sprintf("%ssepablendFactors{\n", ident);
    desc.append_sprintf("%s  src=%s\n", ident, blendToText(params.sepablendFactors.src));
    desc.append_sprintf("%s  dst=%s\n", ident, blendToText(params.sepablendFactors.dst));
    desc.append_sprintf("%s}\n", ident);
    desc.append_sprintf("%sblendOp=%s\n", ident, blendopToText(params.blendOp));
    desc.append_sprintf("%ssepablendOp=%s\n", ident, blendopToText(params.sepablendOp));
    desc.append_sprintf("%sablend=%s\n", ident, bitToText(params.ablend));
    desc.append_sprintf("%ssepablend=%s\n", ident, bitToText(params.sepablend));
  };
  eastl::string desc{};
  desc.append("{\n");
  desc.append_sprintf("  zwrite=%s\n", bitToText(state.zwrite));
  desc.append_sprintf("  ztest=%s\n", bitToText(state.ztest));
  desc.append_sprintf("  zFunc=%s\n", cmpfToText(state.zFunc));
  desc.append_sprintf("  stencilRef=%u\n", state.stencilRef);
  desc.append_sprintf("  cull=%s\n", cullToText(state.cull));
  desc.append_sprintf("  depthBoundsEnable=%s\n", bitToText(state.depthBoundsEnable));
  desc.append_sprintf("  forcedSampleCount=%u\n", state.forcedSampleCount);
  desc.append_sprintf("  conservativeRaster=%s\n", bitToText(state.conservativeRaster));
  desc.append_sprintf("  zClip=%s\n", bitToText(state.zClip));
  desc.append_sprintf("  scissorEnabled=%s\n", bitToText(state.scissorEnabled));
  desc.append_sprintf("  independentBlendEnabled=%s\n", bitToText(state.independentBlendEnabled));
  desc.append_sprintf("  alphaToCoverage=%s\n", bitToText(state.alphaToCoverage));
  desc.append_sprintf("  viewInstanceCount=%u\n", state.viewInstanceCount);
  desc.append_sprintf("  blendFactorUsed=%s\n", bitToText(state.blendFactorUsed));
  desc.append_sprintf("  dualSourceBlendEnabled=%s\n", bitToText(state.dualSourceBlendEnabled));
  desc.append_sprintf("  colorWr=%x\n", state.colorWr);
  desc.append_sprintf("  zBias=%f\n", state.zBias);
  desc.append_sprintf("  slopeZBias=%f\n", state.slopeZBias);
  desc.append("  StencilState{\n");
  desc.append_sprintf("    func=%s\n", cmpfToText(state.stencil.func));
  desc.append_sprintf("    fail=%s\n", stnclopToText(state.stencil.fail));
  desc.append_sprintf("    zFail=%s\n", stnclopToText(state.stencil.zFail));
  desc.append_sprintf("    pass=%s\n", stnclopToText(state.stencil.pass));
  desc.append_sprintf("    readMask=%x\n", state.stencil.readMask);
  desc.append_sprintf("    writeMask=%x\n", state.stencil.writeMask);
  desc.append("  }\n");
  if (state.dualSourceBlendEnabled)
  {
    desc.append("  DualSourceBlendParams{\n");
    dumpBlendParams(desc, state.dualSourceBlend.params, "    ");
    desc.append("  }\n");
  }
  else
  {
    for (uint32_t i = 0; i < shaders::RenderState::NumIndependentBlendParameters; ++i)
    {
      desc.append_sprintf("  IndependentBlendParams[%d]{\n", i);
      dumpBlendParams(desc, state.blendParams[i], "    ");
      desc.append("  }\n");
    }
  }
  desc.append_sprintf("  blendFactor={%d,%d,%d,%d}\n", unsigned(state.blendFactor.r), unsigned(state.blendFactor.g),
    unsigned(state.blendFactor.b), unsigned(state.blendFactor.a));
  desc.append("}");
  debug("%s", desc.c_str());
}

struct AttributionRecord
{
  eastl::vector_set<eastl::string> attributions{};
};

static eastl::vector<AttributionRecord> collect_shclass_attributions_for_entity(size_t enitity_total_count,
  auto &&extract_entity_id_from_pass)
{
  eastl::vector<AttributionRecord> recordsByEntity;
  recordsByEntity.resize(enitity_total_count);
  for (auto &sh_class : shBinDump().classes)
    for (auto &sh_code : sh_class.code)
      for (auto &pass : sh_code.passes)
      {
        const auto id = extract_entity_id_from_pass(pass);
        if (id != 0xFFFF)
          recordsByEntity[id].attributions.insert(eastl::string(sh_class.name.data()));
      }
  return recordsByEntity;
}

static eastl::vector<AttributionRecord> collect_variant_attributions_for_entity(size_t enitity_total_count,
  auto &&extract_entity_id_from_pass)
{
  eastl::vector<AttributionRecord> recordsByEntity;
  recordsByEntity.resize(enitity_total_count);
  for (const auto &sh_class : shBinDump().classes)
    for (const auto &[stVarId, sh_code] : enumerate(sh_class.code))
      for (const auto &[dynVarId, pass] : enumerate(sh_code.passes))
      {
        const auto id = extract_entity_id_from_pass(pass);
        if (id != 0xFFFF)
        {
          sh_class.stVariants.enumerateCodesForVariant(stVarId, [&](uint32_t st_var_code) {
            sh_code.dynVariants.enumerateCodesForVariant(dynVarId, [&](uint32_t dyn_var_code) {
              eastl::string minName{};
              shader_name_format::compile_minimal_variant_name(minName,
                shader_name_format::VariantIdentifierRef{
                  .shClassName = sh_class.name.data(), .stVarCode = int(st_var_code), .dynVarCode = int(dyn_var_code)});
              recordsByEntity[id].attributions.insert(eastl::move(minName));
            });
          });
        }
      }
  return recordsByEntity;
}

static void dump_shclass_attributions(const AttributionRecord &rec)
{
  if (!rec.attributions.empty())
  {
    debug("\nUsed by shaders:");
    for (const eastl::string &name : rec.attributions)
      debug("  %s", name);
  }
}

static void dump_variant_attributions(const AttributionRecord &rec)
{
  if (!rec.attributions.empty())
  {
    debug("\nUsed by variants:");
    for (const eastl::string &name : rec.attributions)
      debug("  %s", name);
  }
}

static eastl::vector<AttributionRecord> collect_attributions_for_entity(size_t enitity_total_count, auto &&extract_entity_id_from_pass,
  const shaderbindump::DumpDetails &settings)
{
  if (settings.variantAttributions)
    return collect_variant_attributions_for_entity(enitity_total_count, extract_entity_id_from_pass);
  else
    return collect_shclass_attributions_for_entity(enitity_total_count, extract_entity_id_from_pass);
}

static void dump_attributions(const AttributionRecord &rec, const shaderbindump::DumpDetails &settings)
{
  if (settings.variantAttributions)
    return dump_variant_attributions(rec);
  else
    return dump_shclass_attributions(rec);
}

static const char *gvar_name_by_id(int id)
{
  return id >= 0 && id < (int)shBinDump().globVars.v.size() ? (const char *)shBinDump().varMap[shBinDump().globVars.v[id].nameId]
                                                            : "?";
}

static String format_sampler_info(const d3d::SamplerInfo &s)
{
  String out;
  out.aprintf(0, "mip=%s filter=%s addrU=%s addrV=%s addrW=%s border=%s aniso=%g bias=%g", to_string(s.mip_map_mode),
    to_string(s.filter_mode), to_string(s.address_mode_u), to_string(s.address_mode_v), to_string(s.address_mode_w),
    to_string(s.border_color.color), s.anisotropic_max, s.mip_map_bias);
  return out;
}

static void dump_immutable_samplers()
{
  const auto *v3 = shBinDumpOwner().getDumpV3();
  if (!v3)
  {
    debug(" immutable samplers (no sampler table in dump)");
    return;
  }
  debug(" immutable samplers (%d):", v3->immutableSamplersMap.size());
  for (uint32_t i = 0; i < v3->immutableSamplersMap.size(); i++)
  {
    int gvar_id = v3->immutableSamplersMap[i].globalVarId;
    int smp_id = v3->immutableSamplersMap[i].samplerId;
    const char *name = gvar_name_by_id(gvar_id);
    if (smp_id >= 0 && smp_id < (int)v3->samplers.size())
      debug("  #%d: %s = {%s}", smp_id, name, format_sampler_info(v3->samplers[smp_id]).c_str());
    else
      debug("  #%d: %s (invalid sampler id)", smp_id, name);
  }
}
struct ShaderSamplerWiring
{
  struct Bind
  {
    uint32_t gvarId;
  };
  eastl::vector<Bind> binds;
  eastl::vector<eastl::pair<int, int>> bfRequests; // {sampler table id, gvar id} from BF_REQUEST_SAMPLER calls
};

static void walk_glob_sampler_refs(dag::ConstSpan<int> cod, ShaderSamplerWiring &out)
{
  for (size_t i = 0; i < cod.size(); ++i)
  {
    switch (shaderopcode::getOp(cod[i]))
    {
      case SHCOD_GLOB_SAMPLER: out.binds.push_back({shaderopcode::getOpStageSlot_Reg(cod[i])}); break;
      case SHCOD_IMM_REAL:
      case SHCOD_MAKE_VEC: ++i; break;
      case SHCOD_IMM_VEC: i += 4; break;
      case SHCOD_STATIC_BLOCK:
      case SHCOD_STATIC_MULTIDRAW_BLOCK: i += 2; break;
      case SHCOD_CALL_FUNCTION:
        if (shaderopcode::getOpFunctionCall_FuncId(cod[i]) == functional::BF_REQUEST_SAMPLER && i + 1 < cod.size())
          out.bfRequests.push_back({(int)shaderopcode::getOpFunctionCall_OutReg(cod[i]), cod[i + 1]});
        i += shaderopcode::getOpFunctionCall_ArgCount(cod[i]);
        break;
      default: break;
    }
  }
}

static void collect_shader_sampler_wiring(const shaderbindump::ShaderClass &cls, ShaderSamplerWiring &out)
{
  out.binds.clear();
  out.bfRequests.clear();
  const auto &stcode = shBinDump().stcode;
  for (const shaderbindump::ShaderCode &code : cls.code)
    for (const shaderbindump::ShaderCode::Pass &pass : code.passes)
    {
      if (!pass.rpass)
        continue;
      const auto walkPass = [&stcode, &out](int stcode_id) {
        if (stcode_id == 0xFFFF || stcode_id >= (int)stcode.size())
          return;
        walk_glob_sampler_refs(stcode[stcode_id], out);
      };
      walkPass(pass.rpass->stcodeId);
      walkPass(pass.rpass->stblkcodeId);
    }
}

template <typename Map>
static void add_mapped_pair(Map &map, int key, int value, const char *clsName, const char *keyKind)
{
  for (auto &entry : map)
    if (entry.first == key)
    {
      if (entry.second != value)
        logerr("shaderInfo: class %s: divergent %s=%d maps to gvars %s and %s - corrupted bindump", clsName, keyKind, key,
          gvar_name_by_id(entry.second), gvar_name_by_id(value));
      return;
    }
  map.push_back({key, value});
}

struct DumpSamplers
{
  eastl::vector<eastl::pair<int, int>> smpIdToGvar; // sampler table id -> gvar id (stcode-requested)
  eastl::vector_set<int> stcodeSourcedGvars;        // gvars filled by a BF_REQUEST_SAMPLER call anywhere
  eastl::vector_set<int> referencedGvars;           // gvars bound by a SHCOD_GLOB_SAMPLER anywhere
  eastl::vector_set<int> immutableGvars;
};

static void collect_dump_samplers(DumpSamplers &out)
{
  const auto *v3 = shBinDumpOwner().getDumpV3();
  if (v3)
    for (auto m : v3->immutableSamplersMap)
      out.immutableGvars.insert(m.globalVarId);
  const auto collectProgram = [&out](const char *who, int stcode_id) {
    if (stcode_id < 0 || stcode_id >= (int)shBinDump().stcode.size())
      return;
    ShaderSamplerWiring wiring;
    walk_glob_sampler_refs(shBinDump().stcode[stcode_id], wiring);
    for (auto &bind : wiring.binds)
      out.referencedGvars.insert(bind.gvarId);
    for (auto &req : wiring.bfRequests)
      add_mapped_pair(out.smpIdToGvar, req.first, req.second, who, "sampler id");
  };
  for (const shaderbindump::ShaderClass &cls : shBinDump().classes)
    for (const shaderbindump::ShaderCode &code : cls.code)
      for (const shaderbindump::ShaderCode::Pass &pass : code.passes)
        if (pass.rpass)
        {
          collectProgram(cls.name.data(), pass.rpass->stcodeId);
          collectProgram(cls.name.data(), pass.rpass->stblkcodeId);
        }
  // Named blocks can also bind or request samplers - walk them too.
  for (auto &block : shBinDump().blocks)
  {
    const char *blockName = block.nameId >= 0 && block.nameId < (int)shBinDump().blockNameMap.size()
                              ? (const char *)shBinDump().blockNameMap[block.nameId]
                              : "<block>";
    collectProgram(blockName, block.stcodeId);
  }
  for (auto &req : out.smpIdToGvar)
    out.stcodeSourcedGvars.insert(req.second);
}

static void dump_dynamic_samplers(const DumpSamplers &samplers)
{
  const auto *v3 = shBinDumpOwner().getDumpV3();
  if (!v3)
  {
    debug(" dynamic samplers (no sampler table in dump)");
    return;
  }

  eastl::vector<int> dynamicGvars;
  const auto isDynamic = [&samplers](int gvar_id) {
    return gvar_id >= 0 && gvar_id < (int)shBinDump().globVars.v.size() && shBinDump().globVars.v[gvar_id].type == SHVT_SAMPLER &&
           samplers.immutableGvars.find(gvar_id) == samplers.immutableGvars.end();
  };

  for (int gvarId : samplers.referencedGvars)
    if (isDynamic(gvarId))
      dynamicGvars.push_back(gvarId);

  for (int gvarId : samplers.stcodeSourcedGvars)
    if (isDynamic(gvarId) && samplers.referencedGvars.find(gvarId) == samplers.referencedGvars.end())
      dynamicGvars.push_back(gvarId);

  eastl::sort(dynamicGvars.begin(), dynamicGvars.end(),
    [](int a, int b) { return strcmp(gvar_name_by_id(a), gvar_name_by_id(b)) < 0; });
  debug(" dynamic samplers (%d):", (int)dynamicGvars.size());
  for (int gvarId : dynamicGvars)
  {
    const char *name = gvar_name_by_id(gvarId);
    int smp_id = -1;
    for (auto &req : samplers.smpIdToGvar)
      if (req.second == gvarId)
      {
        smp_id = req.first;
        break;
      }
    if (smp_id >= 0 && smp_id < (int)v3->samplers.size())
      debug("  #%d: %s = {%s} (stcode)", smp_id, name, format_sampler_info(v3->samplers[smp_id]).c_str());
    else if (smp_id >= 0)
      debug("  #%d: %s (invalid sampler id)", smp_id, name);
    else
      debug("  %s (cpp)", name);
  }
}

static void dump_shader_samplers(const shaderbindump::ShaderClass &cls, const DumpSamplers &dumpSmp)
{
  bool any = false;
  debug(" %s:", (const char *)cls.name);

  for (int i = 0; i < cls.localVars.v.size(); i++)
    if (cls.localVars.v[i].type == SHVT_SAMPLER)
    {
      const char *name = (const char *)shBinDump().varMap[cls.localVars.v[i].nameId];
      debug("  local (declared): %s", name);
      any = true;
    }

  ShaderSamplerWiring wiring;
  collect_shader_sampler_wiring(cls, wiring);
  eastl::vector_set<uint32_t> samplerGlobalRefs;
  for (auto &bind : wiring.binds)
    samplerGlobalRefs.insert(bind.gvarId);
  const auto *v3 = shBinDumpOwner().getDumpV3();
  for (uint32_t idx : samplerGlobalRefs)
  {
    if (idx >= shBinDump().globVars.v.size() || shBinDump().globVars.v[idx].type != SHVT_SAMPLER)
      continue;
    const char *name = (const char *)shBinDump().varMap[shBinDump().globVars.v[idx].nameId];
    if (v3)
    {
      int smp_id = -1;
      for (uint32_t i = 0; i < v3->immutableSamplersMap.size(); i++)
        if (v3->immutableSamplersMap[i].globalVarId == idx)
        {
          smp_id = v3->immutableSamplersMap[i].samplerId;
          break;
        }
      if (smp_id >= 0 && smp_id < (int)v3->samplers.size())
        debug("  global: %s = immutable #%d {%s}", name, smp_id, format_sampler_info(v3->samplers[smp_id]).c_str());
      else
      {
        const bool stcodeSourced = dumpSmp.stcodeSourcedGvars.find((int)idx) != dumpSmp.stcodeSourcedGvars.end();
        debug("  global: %s = dynamic (%s)", name, stcodeSourced ? "stcode" : "cpp");
      }
    }
    else
      debug("  global: %s (dynamic, no sampler table)", name);
    any = true;
  }

  if (!any)
    debug("  (none)");
}

static void dumpCurrentShaders(const char *single_shader, shaderbindump::DumpDetails details, shaderbindump::SortType sortby,
  const char *sh_dir)
{
  debug("--- START ---\n");

  DumpSamplers dumpSamplers;
  if (details.samplers)
    collect_dump_samplers(dumpSamplers);

  struct RelevantShaderIds
  {
    eastl::vector_set<int> vpr;
    eastl::vector_set<int> fsh;
    eastl::vector_set<int> stcode;
    eastl::vector_set<int> rstate;
  };
  eastl::optional<RelevantShaderIds> filter{};

  const bool singleShaderFilter = bool(single_shader);
  const bool singleVariantFilter = singleShaderFilter && strchr(single_shader, '#');

  if (singleShaderFilter)
  {
    shader_name_format::VariantIdentifier ident{};
    if (singleVariantFilter)
    {
      auto res = shader_name_format::parse_variant_name(single_shader);
      if (!res)
      {
        printf("ERR: invalid -shader:XXX argument: %s\n", res.error().c_str());
        exit(1);
      }
      ident = eastl::move(res.value());
    }
    else
    {
      ident = shader_name_format::VariantIdentifier{.shClassName = single_shader};
    }

    filter.emplace();
    bool shaderfound = false;
    for (int i = 0; i < shBinDump().classes.size(); i++)
      if (strcmp(ident.shClassName.c_str(), (const char *)shBinDump().classes[i].name) == 0)
      {
        shaderbindump::dumpShaderInfo(shBinDump(), shBinDump().classes[i], details.shaderDetails);
        int shcodeId = -1;
        if (singleVariantFilter)
        {
          const auto &table = shBinDump().classes[i].stVariants;
          shcodeId = table.findVariant(ident.stVarCode);
          if (shcodeId == table.FIND_NOTFOUND || shcodeId == table.FIND_NULL)
          {
            printf("ERR: static variant code does not mark an existing variant for '%s'\n", single_shader);
            exit(1);
          }
        }
        else
        {
          shaderfound = true;
        }

        if (details.samplers)
          dump_shader_samplers(shBinDump().classes[i], dumpSamplers);

        for (auto const &[sci, code] : enumerate(shBinDump().classes[i].code))
        {
          if (singleVariantFilter && sci != shcodeId)
            continue;
          int passId = -1;
          if (singleVariantFilter)
          {
            const auto &table = code.dynVariants;
            passId = table.findVariant(ident.dynVarCode);
            if (passId == table.FIND_NOTFOUND || passId == table.FIND_NULL)
            {
              printf("ERR: dynamic variant code does not mark an existing variant for '%s'\n", single_shader);
              exit(1);
            }
          }
          else
          {
            shaderfound = true;
          }
          for (auto const &[pid, pass] : enumerate(code.passes))
          {
            if (singleVariantFilter && pid != passId)
              continue;
            shaderfound = true;
            if (pass.rpass->vprId != uint16_t(-1))
              filter->vpr.insert(pass.rpass->vprId);
            if (pass.rpass->fshId != uint16_t(-1))
              filter->fsh.insert(pass.rpass->fshId);
            if (pass.rpass->stcodeId != uint16_t(-1))
              filter->stcode.insert(pass.rpass->stcodeId);
            if (pass.rpass->stblkcodeId != uint16_t(-1))
              filter->stcode.insert(pass.rpass->stblkcodeId);
            if (pass.rpass->renderStateNo != uint16_t(-1))
              filter->rstate.insert(pass.rpass->renderStateNo);
          }
        }
        break;
      }
    if (!shaderfound)
    {
      printf("ERR: shader \"%s\" not found!\n", single_shader);
      exit(1);
    }
  }

  if (singleVariantFilter)
  {
    eastl::vector<eastl::string> fullAliases;
    eastl::vector<eastl::string> shaderAliases;
    eastl::vector<eastl::string> graphicsVsAliases;
    eastl::vector<eastl::string> graphicsPsAliases;

    bool isGraphics = !filter->vpr.empty();
    auto anyMatch = [&filter](const auto &pass) {
      return filter->vpr.find(pass.vprId) != filter->vpr.end() || filter->fsh.find(pass.fshId) != filter->fsh.end() ||
             filter->stcode.find(pass.stcodeId) != filter->stcode.end() ||
             filter->stcode.find(pass.stblkcodeId) != filter->stcode.end() ||
             filter->rstate.find(pass.renderStateNo) != filter->rstate.end();
    };
    auto fullMatch = [&filter](const auto &pass) {
      bool match = true;
      if (pass.vprId != uint16_t(-1))
        match &= filter->vpr.find(pass.vprId) != filter->vpr.end();
      if (pass.fshId != uint16_t(-1))
        match &= filter->fsh.find(pass.fshId) != filter->fsh.end();
      if (pass.stcodeId != uint16_t(-1))
        match &= filter->stcode.find(pass.stcodeId) != filter->stcode.end();
      if (pass.stblkcodeId != uint16_t(-1))
        match &= filter->stcode.find(pass.stblkcodeId) != filter->stcode.end();
      if (pass.renderStateNo != uint16_t(-1))
        match &= filter->rstate.find(pass.renderStateNo) != filter->rstate.end();
      return match;
    };
    auto shaderModuleMatch = [&filter](const auto &pass) {
      bool match = true;
      if (pass.vprId >= 0)
        match &= filter->vpr.find(pass.vprId) != filter->vpr.end();
      if (pass.fshId >= 0)
        match &= filter->fsh.find(pass.fshId) != filter->fsh.end();
      return match;
    };
    auto gfxVsMatch = [&filter, isGraphics](const auto &pass) {
      if (!isGraphics)
        return false;
      bool match = true;
      if (pass.vprId >= 0)
        match &= filter->vpr.find(pass.vprId) != filter->vpr.end();
      return match;
    };
    auto gfxPsMatch = [&filter, isGraphics](const auto &pass) {
      if (!isGraphics)
        return false;
      bool match = true;
      if (pass.fshId >= 0)
        match &= filter->fsh.find(pass.fshId) != filter->fsh.end();
      return match;
    };

    for (auto &cls : shBinDump().classes)
    {
      auto ref = eastl::find_if(eastl::begin(cls.shrefStorage), eastl::end(cls.shrefStorage), anyMatch);
      if (ref == eastl::end(cls.shrefStorage))
        continue;
      for (uint32_t si = 0; si < cls.code.size(); ++si)
      {
        auto &sVar = cls.code[si];
        for (uint32_t di = 0; di < sVar.passes.size(); ++di)
        {
          auto &dVar = sVar.passes[di];
          if (!anyMatch(*dVar.rpass))
            continue;

          bool fm = fullMatch(*dVar.rpass);
          bool smm = shaderModuleMatch(*dVar.rpass);
          bool gvm = gfxVsMatch(*dVar.rpass);
          bool gpm = gfxPsMatch(*dVar.rpass);

          cls.stVariants.enumerateCodesForVariant(si, [&](uint32_t s_code) {
            sVar.dynVariants.enumerateCodesForVariant(di, [&](uint32_t d_code) {
              eastl::string name;
              shader_name_format::compile_human_readable_variant_name(name,
                shader_name_format::VariantIdentifierRef{.shClassName = cls.name.data(), .stVarCode = s_code, .dynVarCode = d_code},
                shBinDump(), *shBinDumpOwner().getDumpV2());

              if (fm)
                fullAliases.push_back(name);
              if (smm && !fm)
                shaderAliases.push_back(name);
              if (gvm && !smm)
                graphicsVsAliases.push_back(name);
              if (gpm && !smm)
                graphicsPsAliases.push_back(name);
            });
          });
        }
      }
    }

    // The shader itself should at least be included
    G_ASSERT(!fullAliases.empty());

    debug("List of shader variants aliasing '%s'\n", single_shader);
    if (!fullAliases.empty())
      debug("Complete aliases:");
    for (const auto &alias : fullAliases)
      debug("  %s", alias.c_str());
    if (!shaderAliases.empty())
    {
      debug("Shader module aliases (vs+ps match for gfx, cs for compute):");
      for (const auto &alias : shaderAliases)
        debug("  %s", alias.c_str());
    }
    if (!graphicsVsAliases.empty())
    {
      debug("VS-only module aliases:");
      for (const auto &alias : graphicsVsAliases)
        debug("  %s", alias.c_str());
    }
    if (!graphicsPsAliases.empty())
    {
      debug("PS-only module aliases:");
      for (const auto &alias : graphicsPsAliases)
        debug("  %s", alias.c_str());
    }
    debug("");
  }

  if (details.globalVars)
  {
    debug("global vars (%d):", shBinDump().globVars.v.size());
    shaderbindump::dumpVars(shBinDump(), shBinDump().globVars, nullptr);
    debug("");
  }


  if (details.namedStateBlocks)
  {
    debug("named state blocks (%d):", shBinDump().blockNameMap.size());
    for (int i = 0; i < shBinDump().blockNameMap.size(); i++)
    {
      debug("  block <%s>: uidMask=%04X uidVal=%04X, stcode=%d", (const char *)shBinDump().blockNameMap[i],
        shBinDump().blocks[i].uidMask, shBinDump().blocks[i].uidVal, shBinDump().blocks[i].stcodeId);

      const shader_layout::blk_word_t *sb = &shBinDump().blocks[i].suppBlockUid.get();
      if (!shBinDump().blocks[i].suppBlkMask || *sb == shader_layout::BLK_WORD_FULLMASK)
      {
        debug("    suppMask=%04X, no other blocks supported!", shBinDump().blocks[i].suppBlkMask);
      }
      else
      {
        debug_("    suppMask=%04X, supported block codes:", shBinDump().blocks[i].suppBlkMask);
        while (*sb != shader_layout::BLK_WORD_FULLMASK)
        {
          debug_(" %04X", *sb);
          sb++;
        }
        debug("");
      }
    }
    debug("");
  }


  if (details.maxregCount)
    debug("maxreg count = %d\n", shBinDump().maxRegSize);

  if (!single_shader && details.samplers)
  {
    dump_immutable_samplers();
    dump_dynamic_samplers(dumpSamplers);
  }

  const bool sortClassOrder = !single_shader && sortby != shaderbindump::SortType::NAME && (details.shaders || details.samplers);
  dag::Vector<eastl::pair<uint32_t, uint32_t>> sorted;
  if (sortClassOrder)
  {
    sorted.resize(shBinDump().classes.size());
    for (int i = 0; i < shBinDump().classes.size(); i++)
    {
      uint32_t totalPasses = 0;
      uint32_t totalUniquePasses = 0;
      shaderbindump::getTotalPasses(shBinDump().classes[i], totalPasses, totalUniquePasses);
      sorted[i] = {i, totalUniquePasses};
    }

    eastl::sort(sorted.begin(), sorted.end(),
      [](const eastl::pair<uint32_t, uint32_t> &a, const eastl::pair<uint32_t, uint32_t> &b) { return a.second > b.second; });
  }

  if (details.shaders && !single_shader)
  {
    debug("shaders (%d):", shBinDump().classes.size());
    for (int i = 0; i < shBinDump().classes.size(); i++)
      shaderbindump::dumpShaderInfo(shBinDump(), shBinDump().classes[sortClassOrder ? sorted[i].first : i], details.shaderDetails);
    debug("");
  }

  if (details.samplers && !single_shader)
  {
    debug("samplers by shader:");
    for (int i = 0; i < shBinDump().classes.size(); i++)
      dump_shader_samplers(shBinDump().classes[sortClassOrder ? sorted[i].first : i], dumpSamplers);
    debug("");
  }

  const auto vprIdCount = shBinDump().vprCount;
  const auto fshIdCount = shBinDump().fshCount;

  if (rga::need_module_extraction(rga_config))
  {
    if (!singleVariantFilter)
    {
      printf("rga-extract: must be used with a full variant filter '-shader:name#N.M'\n");
      exit(1);
    }

    rga::ExtractionContext context = {};
    for (int i = 0; i < vprIdCount; i++)
    {
      if (filter && filter->vpr.find(i) == filter->vpr.end())
        continue;
      if (!rga::extract_vertex_stage(shBinDumpOwner().getCode(i, ShaderCodeType::VERTEX), i, context, rga_config))
        exit(1);
    }
    for (int i = 0; i < fshIdCount; i++)
    {
      if (filter && filter->fsh.find(i) == filter->fsh.end())
        continue;
      bool res;
      if (rga::extracting_graphics_pipeline(context))
        res = rga::extract_pixel_stage(shBinDumpOwner().getCode(i, ShaderCodeType::PIXEL), i, context, rga_config);
      else
        res = rga::extract_compute_stage(shBinDumpOwner().getCode(i, ShaderCodeType::COMPUTE), i, context, rga_config);
      if (!res)
        exit(1);
    }
    for (int i = 0; i < shBinDump().renderStates.size(); i++)
    {
      if (filter && filter->rstate.find(i) == filter->rstate.end())
        continue;
      if (!rga::extract_pipeline_state(shBinDump().renderStates[i], i, context, rga_config))
        exit(1);
    }

    if (!rga::run_rga_analysis(context, rga_config))
      exit(1);
  }

  if (!details.assembly && !need_disasm && !sh_dir)
    debug("\n******* %d vertex shaders\n******* %d pixel shaders", vprIdCount, fshIdCount);
  else if (sh_dir)
  {
    ShaderBytecode tmpbuf;
    debug("");
    dd_mkdir(String::mk_str_cat(sh_dir, "/vs"));

    auto writeStrToFile = [](auto const &str, String const &fn) {
      file_ptr_t fp = df_open(fn.str(), DF_WRITE | DF_CREATE);
      G_ASSERT_RETURN(fp, );
      df_write(fp, str.c_str(), str.size());
      df_close(fp);
    };

    for (int i = 0; i < vprIdCount; i++)
    {
      if (filter && filter->vpr.find(i) == filter->vpr.end())
        continue;
      ShaderSource src = shBinDumpOwner().getCode(i, ShaderCodeType::VERTEX);
      dag::ConstSpan<uint32_t> code = uncompressShader(src, tmpbuf);
      String sha1 = calc_sha1(code);
      debug("Vertex shader --v%d-- %s/vs/%s (%d bytes)", i, sh_dir, sha1, data_size(tmpbuf));
      FullFileSaveCB cwr(mk_str_cat3(sh_dir, "/vs/", sha1));
      cwr.write(tmpbuf.data(), data_size(tmpbuf));
      if (details.assembly)
        disassembleShader(code, mk_str_cat4(sh_dir, "/vs/", sha1, ".asm"));
      if (need_disasm)
      {
        writeStrToFile(shader_blob_disasm::disassembleShaderBlob(
                         dag::ConstSpan<uint8_t>{(uint8_t const *)code.data(), code.size() * elem_size(code)}, src.metadata),
          mk_str_cat4(sh_dir, "/vs/", sha1, ".disasm"));
      }
    }

    dd_mkdir(String::mk_str_cat(sh_dir, "/ps"));
    for (int i = 0; i < fshIdCount; i++)
    {
      if (filter && filter->fsh.find(i) == filter->fsh.end())
        continue;
      ShaderSource src = shBinDumpOwner().getCode(i, ShaderCodeType::PIXEL);
      dag::ConstSpan<uint32_t> code = uncompressShader(src, tmpbuf);
      String sha1 = calc_sha1(code);
      debug("Pixel shader --p%d-- %s/ps/%s (%d bytes)", i, sh_dir, sha1, data_size(tmpbuf));
      FullFileSaveCB cwr(mk_str_cat3(sh_dir, "/ps/", sha1));
      cwr.write(tmpbuf.data(), data_size(tmpbuf));
      if (details.assembly)
        disassembleShader(code, mk_str_cat4(sh_dir, "/ps/", sha1, ".asm"));
      if (need_disasm)
      {
        writeStrToFile(shader_blob_disasm::disassembleShaderBlob(
                         dag::ConstSpan<uint8_t>{(uint8_t const *)code.data(), code.size() * elem_size(code)}, src.metadata),
          mk_str_cat4(sh_dir, "/ps/", sha1, ".disasm"));
      }
    }
  }
  else
  {
    auto vprAttribs = collect_attributions_for_entity(vprIdCount, [](const auto &pass) { return pass.rpass->vprId; }, details);
    auto fshAttribs = collect_attributions_for_entity(fshIdCount, [](const auto &pass) { return pass.rpass->fshId; }, details);

    ShaderBytecode tmpbuf;
    for (int i = 0; i < vprIdCount; i++)
    {
      if (filter && filter->vpr.find(i) == filter->vpr.end())
        continue;
      debug("\n******* Vertex shader --v%d--", i);
      ShaderSource src = shBinDumpOwner().getCode(i, ShaderCodeType::VERTEX);
      dag::ConstSpan<uint32_t> code = uncompressShader(src, tmpbuf);
      if (details.assembly)
        disassembleShader(code);
      if (need_disasm)
      {
        debug("%s", shader_blob_disasm::disassembleShaderBlob(
                      dag::ConstSpan<uint8_t>{(uint8_t const *)code.data(), code.size() * elem_size(code)}, src.metadata)
                      .c_str());
      }
      dump_attributions(vprAttribs[i], details);
    }

    for (int i = 0; i < fshIdCount; i++)
    {
      if (filter && filter->fsh.find(i) == filter->fsh.end())
        continue;
      debug("\n******* Pixel shader --p%d--", i);
      ShaderSource src = shBinDumpOwner().getCode(i, ShaderCodeType::PIXEL);
      dag::ConstSpan<uint32_t> code = uncompressShader(src, tmpbuf);
      if (details.assembly)
        disassembleShader(uncompressShader(src, tmpbuf));
      if (need_disasm)
      {
        debug("%s", shader_blob_disasm::disassembleShaderBlob(
                      dag::ConstSpan<uint8_t>{(uint8_t const *)code.data(), code.size() * elem_size(code)}, src.metadata)
                      .c_str());
      }
      dump_attributions(fshAttribs[i], details);
    }
  }

  if (!details.stcode)
  {
    uint64_t totalBytes = 0;
    for (int i = 0; i < shBinDump().stcode.size(); i++)
    {
      const dag::ConstSpan<int> &prog = shBinDump().stcode[i];
      totalBytes += data_size(prog);
    }
    debug("\n******* %d stcode programs, total mem %lfkb", shBinDump().stcode.size(), (double)totalBytes / 1024.0);
  }
  else
  {
    struct Record
    {
      eastl::vector_set<eastl::string> classes{};
      eastl::vector_set<eastl::string> blocks{};
      bool isStatic = false;
    };
    eastl::vector<Record> recordsByStcode;
    recordsByStcode.resize(shBinDump().stcode.size());
    for (auto &sh_class : shBinDump().classes)
      for (const auto &[stVarId, sh_code] : enumerate(sh_class.code))
        for (const auto &[dynVarId, pass] : enumerate(sh_code.passes))
        {
          eastl::vector<eastl::string> namesToInsert{};
          if (details.variantAttributions)
          {
            sh_class.stVariants.enumerateCodesForVariant(stVarId, [&](uint32_t st_var_code) {
              sh_code.dynVariants.enumerateCodesForVariant(dynVarId, [&](uint32_t dyn_var_code) {
                eastl::string minName{};
                shader_name_format::compile_minimal_variant_name(minName,
                  shader_name_format::VariantIdentifierRef{
                    .shClassName = sh_class.name.data(), .stVarCode = int(st_var_code), .dynVarCode = int(dyn_var_code)});
                namesToInsert.push_back(eastl::move(minName));
              });
            });
          }
          else
          {
            namesToInsert.emplace_back(sh_class.name.data());
          }
          if (pass.rpass->stcodeId != 0xFFFF)
          {
            G_ASSERT(!recordsByStcode[pass.rpass->stcodeId].isStatic);
            for (auto &&name : namesToInsert)
              recordsByStcode[pass.rpass->stcodeId].classes.insert(eastl::move(name));
          }
          if (pass.rpass->stblkcodeId != 0xFFFF)
          {
            G_ASSERT(recordsByStcode[pass.rpass->stblkcodeId].blocks.empty());
            G_ASSERT(recordsByStcode[pass.rpass->stblkcodeId].isStatic || recordsByStcode[pass.rpass->stblkcodeId].classes.empty());
            recordsByStcode[pass.rpass->stblkcodeId].isStatic = true;
            for (auto &&name : namesToInsert)
              recordsByStcode[pass.rpass->stblkcodeId].classes.insert(eastl::move(name));
          }
        }
    for (auto &sh_block : shBinDump().blocks)
      if (sh_block.stcodeId != -1 && sh_block.nameId != -1)
      {
        G_ASSERT(!recordsByStcode[sh_block.stcodeId].isStatic);
        recordsByStcode[sh_block.stcodeId].blocks.insert(eastl::string(shBinDump().blockNameMap[sh_block.nameId].c_str()));
      }

    for (int i = 0; i < shBinDump().stcode.size(); i++)
    {
      if (filter && filter->stcode.find(i) == filter->stcode.end())
        continue;

      debug("\n******* State code shader --s%d-- (%s)", i, recordsByStcode[i].isStatic ? "static" : "dynamic");
      if (shBinDump().stcode[i].size())
      {
        ShUtils::shcod_dump(shBinDump().stcode[i], &shBinDump().globVars, nullptr, nullptr, &shBinDump(), {}, false);
        if (!recordsByStcode[i].classes.empty())
        {
          debug("\nUsed by %s:", details.variantAttributions ? "variants" : "shaders");
          for (const eastl::string &name : recordsByStcode[i].classes)
            debug("  %s", name);
        }
        if (!recordsByStcode[i].blocks.empty())
        {
          debug("\nUsed by blocks:");
          for (const eastl::string &name : recordsByStcode[i].blocks)
            debug("  %s", name);
        }
      }
    }
  }

  if (!details.renderStates)
  {
    debug("\n******* %d render state records, total mem %dkb", shBinDump().renderStates.size(),
      (shBinDump().renderStates.size() * sizeof(shBinDump().renderStates[0])) >> 10);
  }
  else
  {
    auto recordsByState = collect_attributions_for_entity(
      shBinDump().renderStates.size(), [](const auto &pass) { return pass.rpass->renderStateNo; }, details);
    for (int i = 0; i < shBinDump().renderStates.size(); i++)
    {
      if (filter && filter->rstate.find(i) == filter->rstate.end())
        continue;

      debug("\n******* Render state --rs%d--", i);
      dump_render_state(shBinDump().renderStates[i]);
      dump_attributions(recordsByState[i], details);
    }
  }

  debug("--- END ---");
}
