//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <drv/shadersMetaData/dxil/compiled_shader_header.h>
#include <EASTL/string_view.h>

namespace dxil
{

struct StageModuleRef
{
  ShaderHeader header = {};
  HashValue hash = {};
  uint32_t hashedSize = 0;
  eastl::string_view debugName;
  uint32_t bytecodeOffset = 0;
  uint32_t bytecodeSize = 0; // 0 means absent or failed decode

  explicit operator bool() const { return bytecodeSize != 0; }
};

struct DecodedShaderRef
{
  StageModuleRef main; // vs / ms / ps / cs
  StageModuleRef gsOrAs, hs, ds;
  eastl::span<const StreamOutputComponentInfo> streamOutput;
  bool isMesh = false;

  explicit operator bool() const { return static_cast<bool>(main); }
};

// expect_vertex_pipeline permits combined / mesh containers; pixel and compute entry points pass false.
bool decode_metadata(dag::ConstSpan<uint8_t> metadata, bool expect_vertex_pipeline, DecodedShaderRef &out,
  eastl::string *out_error = nullptr);

} // namespace dxil
