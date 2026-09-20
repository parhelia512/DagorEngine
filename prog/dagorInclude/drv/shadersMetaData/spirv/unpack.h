//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

// compiled_meta_data.h pulls a plain <vulkan/vulkan.h>. Inside drv3d_vulkan that must not happen
// first: vulkan_api.h sets the VK_USE_PLATFORM_* defines, which gate extension blocks that in turn
// gate VulkanDevice/VulkanInstance members. A TU that misses them builds a shorter layout than the
// rest of the driver and calls through the wrong function pointer slot.
#if defined(INSIDE_DRIVER) && !defined(VULKAN_LIB_NAME_PREFIX)
#error "include the drv3d_vulkan headers (vulkan_api.h) before <drv/shadersMetaData/spirv/unpack.h>"
#endif

#include <drv/shadersMetaData/spirv/compiled_meta_data.h>
#include <generic/dag_span.h>
#include <generic/dag_tab.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/optional.h>
#include <EASTL/string.h>

namespace spirv
{

struct ChunkSetRef
{
  dag::ConstSpan<ChunkHeader> chunks;
  dag::ConstSpan<uint8_t> chunkData;
};

bool decode_chunked_metadata(dag::ConstSpan<uint8_t> metadata, Tab<ChunkHeader> &out_chunks, Tab<uint8_t> &out_chunk_data,
  eastl::string *out_error = nullptr);

inline bool is_combined(dag::ConstSpan<uint8_t> metadata)
{
  return metadata.size() >= 4 && *(const uint32_t *)metadata.data() == SPIR_V_COMBINED_BLOB_IDENT;
}

struct CombinedStageRef
{
  VkShaderStageFlagBits stage = VkShaderStageFlagBits(0);
  dag::ConstSpan<uint8_t> metadata;
  uint32_t bytecodeOffset = 0;
  uint32_t bytecodeSize = 0;
};

constexpr uint32_t MAX_COMBINED_STAGES = 5;
using CombinedStageList = eastl::fixed_vector<CombinedStageRef, MAX_COMBINED_STAGES, false>;

// Walks an 'SVc1' combined blob into per-stage entries. A non-combined blob
// yields a single entry with stage == single_stage_hint, so callers have one
// codepath.
bool decode_container(dag::ConstSpan<uint8_t> metadata, VkShaderStageFlagBits single_stage_hint, CombinedStageList &out,
  eastl::string *out_error = nullptr);

struct ExtractedHeader
{
  ShaderHeader header;
  HashValue hash;
};

// Finds the SHADER_HEADER chunk to the current layout. Empty result: no header chunk (out_error untouched) or unknown verMagic
// (out_error filled) - the caller decides how fatal that is.
eastl::optional<ExtractedHeader> extract_header(const ChunkSetRef &chunks, uint32_t extension_bits,
  eastl::string *out_error = nullptr);

const ChunkHeader *find_chunk(const ChunkSetRef &chunks, ChunkType type, uint32_t extension_mask);

dag::ConstSpan<uint8_t> find_chunk_data(const ChunkSetRef &chunks, ChunkType type, uint32_t extension_bits);

const char *chunk_type_name(ChunkType type);

// SMOL-V to SPIR-V. smolv_size must bound the input: the code buffer is padded
// up to a 4-byte word boundary and smolv::Decode loops over the whole input,
// decoding trailing padding as extra instructions and overflowing the
// (correctly sized) output. smolv_size == 0 (raw SPIR-V blob) returns false.
bool decode_smolv(dag::ConstSpan<uint8_t> maybe_smolv, uint32_t smolv_size, Tab<uint8_t> &out_spirv);

} // namespace spirv
