//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <drv/3d/dag_shader.h>
#include <drv/shadersMetaData/dx11/unpack.h>
#include <drv/shadersMetaData/dxil/unpack.h>
#include <drv/shadersMetaData/spirv/unpack.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <generic/dag_span.h>
#include <generic/dag_tab.h>
#include <generic/dag_expected.h>

// Cross-API facade over the per-API decoders: one call turns a shader blob
// (metadata + bytecode) into per-stage raw bytecode modules. Meant for tools;
// drivers call the per-API decoders directly to keep their lazy / zero-copy
// paths.

namespace shader_blob
{

enum class Api : uint8_t
{
  DX11,
  DX12,
  SPIRV,
  INVALID,
};

enum class Stage : uint8_t
{
  VS,
  PS,
  CS,
  GS,
  HS,
  DS,
  MS,
  AS,
  INVALID,
};

const char *api_name(Api api);
const char *stage_name(Stage stage);
const char *bytecode_ext(Api api);

dag::Expected<Api, eastl::string> detect_api(dag::ConstSpan<uint8_t> metadata);

struct StageBlob
{
  Stage stage = Stage::INVALID;
  dag::ConstSpan<uint8_t> bytecode;
  const dxil::ShaderHeader *dxilHeader = nullptr;
  const spirv::ShaderHeader *spirvHeader = nullptr;
  const dx11::SimpleHeader *dx11Header = nullptr;
};

class UnpackedShader
{
public:
  static constexpr uint32_t MAX_STAGES = 5;

  Api api = Api::DX11;
  eastl::fixed_vector<StageBlob, MAX_STAGES, false> stages;

  const spirv::ChunkSetRef *spirvChunks(uint32_t stage_index) const
  {
    return api == Api::SPIRV && stage_index < spirvChunkRefs.size() ? &spirvChunkRefs[stage_index] : nullptr;
  }

  Tab<uint8_t> bytecodeStorage;
  eastl::fixed_vector<Tab<uint8_t>, MAX_STAGES, false> spirvCodeStorage;
  struct SpirvChunkSet
  {
    Tab<spirv::ChunkHeader> chunks;
    Tab<uint8_t> chunkData;
  };
  eastl::fixed_vector<SpirvChunkSet, MAX_STAGES, false> spirvChunkSets;
  eastl::fixed_vector<spirv::ChunkSetRef, MAX_STAGES, false> spirvChunkRefs;
  eastl::fixed_vector<spirv::ShaderHeader, MAX_STAGES, false> spirvHeaders;
  eastl::fixed_vector<dxil::ShaderHeader, MAX_STAGES, false> dxilHeaders;
  dx11::SimpleHeader dx11Header = {};
};

// main_stage names the stage for containers that do not carry it themselves.
bool unpack(Api api, dag::ConstSpan<uint8_t> metadata, dag::ConstSpan<uint8_t> bytecode, Stage main_stage, UnpackedShader &out,
  eastl::string *out_error = nullptr);

bool unpack(Api api, const ShaderSource &src, Stage main_stage, UnpackedShader &out, eastl::string *out_error = nullptr);

} // namespace shader_blob
