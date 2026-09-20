// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "spirv_extractor.h"
#include <drv/shadersMetaData/spirv/unpack.h>

using namespace drv3d_vulkan;

#if VULKAN_LOAD_SHADER_EXTENDED_DEBUG_DATA

const char unknown_name[] = "<unknown>";

ShaderDebugInfo spirv_extractor::getDebugInfo(const Tab<spirv::ChunkHeader> &chunks, const dag::ConstSpan<uint8_t> &chunk_data,
  uint32_t extension_bits)
{
  const spirv::ChunkSetRef chunkSet{make_span_const(chunks), chunk_data};
  auto readChunk = [&](spirv::ChunkType type, String &target) {
    auto data = spirv::find_chunk_data(chunkSet, type, extension_bits);
    if (!data.empty())
      target.setStr(reinterpret_cast<const char *>(data.data()), data.size());
  };

  ShaderDebugInfo result;
  readChunk(spirv::ChunkType::SPIR_V_DISASSEMBLY, result.spirvDisAsm);
  readChunk(spirv::ChunkType::HLSL_DISASSEMBLY, result.dxbcDisAsm);
  readChunk(spirv::ChunkType::RECONSTRUCTED_GLSL, result.sourceGLSL);
  readChunk(spirv::ChunkType::RECONSTRUCTED_HLSL_DISASSEMBLY, result.reconstructedHLSL);
  readChunk(spirv::ChunkType::HLSL_AND_RECONSTRUCTED_HLSL_XDIF, result.reconstructedHLSLAndSourceHLSLXDif);
  readChunk(spirv::ChunkType::UNPROCESSED_HLSL, result.sourceHLSL);

  result.name = result.debugName = unknown_name;

  return result;
}

#endif

ShaderModuleBlob spirv_extractor::getBlob(const ShaderModuleHeader &header, const ShaderSource &source,
  const ShaderProgramData &bytecode)
{
  ShaderModuleBlob result;
  result.source = source;
  result.offset = bytecode.offset;
  result.sizeSmolv = header.header.smolvSize;
  result.size = bytecode.size;

#if VULKAN_LOAD_SHADER_EXTENDED_DEBUG_DATA
  result.name = unknown_name;
#endif

  return result;
}

eastl::optional<ShaderModuleHeader> spirv_extractor::getHeader(VkShaderStageFlags stage, const Tab<spirv::ChunkHeader> &chunks,
  dag::ConstSpan<uint8_t> chunk_data, uint32_t extension_bits)
{
  eastl::string error;
  auto extracted = spirv::extract_header({make_span_const(chunks), chunk_data}, extension_bits, &error);
  if (!extracted)
  {
    if (!error.empty())
      DAG_FATAL("vulkan: %s", error.c_str());
    return {};
  }
  return ShaderModuleHeader{extracted->header, extracted->hash, stage};
}
