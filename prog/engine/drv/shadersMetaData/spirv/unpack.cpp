// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <drv/shadersMetaData/spirv/unpack.h>
#include <ioSys/dag_memIo.h>
#include <ioSys/dag_zlibIo.h>
#include <smolv.h>
#include <cstring>

static bool set_error(eastl::string *out_error, const char *msg)
{
  if (out_error)
    *out_error = msg;
  return false;
}

bool spirv::decode_chunked_metadata(dag::ConstSpan<uint8_t> metadata, Tab<ChunkHeader> &out_chunks, Tab<uint8_t> &out_chunk_data,
  eastl::string *out_error)
{
  if (metadata.size() < 8)
    return set_error(out_error, "spirv blob is too small");

  const uint32_t ident = *(const uint32_t *)metadata.data();
  if (ident != SPIR_V_BLOB_IDENT && ident != SPIR_V_BLOB_IDENT_UNCOMPRESSED)
  {
    if (out_error)
      out_error->sprintf("unknown spirv blob ident %c%c%c%c", DUMP4C(ident));
    return false;
  }

  InPlaceMemLoadCB reader(metadata.data(), metadata.size());
  reader.readInt(); // ident
  const int blockSize = reader.beginBlock();
  if (blockSize < 0 || 8 + blockSize > metadata.size())
    return set_error(out_error, "spirv blob block size exceeds metadata size");

  if (ident == SPIR_V_BLOB_IDENT)
  {
    ZlibLoadCB zlibReader(reader, blockSize);
    zlibReader.readTab(out_chunks);
    zlibReader.readTab(out_chunk_data);
    zlibReader.ceaseReading();
  }
  else
  {
    reader.readTab(out_chunks);
    reader.readTab(out_chunk_data);
  }

  for (const ChunkHeader &chunk : out_chunks)
    if (chunk.offset + chunk.size > out_chunk_data.size())
      return set_error(out_error, "spirv chunk range exceeds chunk data size");
  return true;
}

bool spirv::decode_container(dag::ConstSpan<uint8_t> metadata, VkShaderStageFlagBits single_stage_hint, CombinedStageList &out,
  eastl::string *out_error)
{
  out.clear();
  if (metadata.size() < 8)
    return set_error(out_error, "spirv blob is too small");

  const uint32_t *dwords = (const uint32_t *)metadata.data();
  if (dwords[0] != SPIR_V_COMBINED_BLOB_IDENT)
  {
    CombinedStageRef &single = out.push_back();
    single.stage = single_stage_hint;
    single.metadata = metadata;
    return true;
  }

  const uint32_t count = dwords[1];
  if (count > MAX_COMBINED_STAGES)
    return set_error(out_error, "spirv combined blob has too many stages");

  auto *comboChunks = reinterpret_cast<const CombinedChunk *>(dwords + 2);
  auto *comboData = reinterpret_cast<const uint8_t *>(comboChunks + count);
  const uint8_t *dataEnd = metadata.data() + metadata.size();
  if ((const uint8_t *)(comboChunks + count) > dataEnd)
    return set_error(out_error, "spirv combined blob chunk table exceeds metadata size");

  uint32_t bytecodeOffset = 0;
  for (const CombinedChunk &chunk : make_span(comboChunks, count))
  {
    if (comboData + chunk.size > dataEnd)
      return set_error(out_error, "spirv combined blob stage exceeds metadata size");
    CombinedStageRef &stage = out.push_back();
    stage.stage = chunk.stage;
    stage.metadata = make_span_const(comboData, chunk.size);
    stage.bytecodeOffset = bytecodeOffset;
    stage.bytecodeSize = chunk.bytecode_size;
    comboData += chunk.size;
    bytecodeOffset += chunk.bytecode_size;
  }
  return true;
}

const spirv::ChunkHeader *spirv::find_chunk(const ChunkSetRef &chunks, ChunkType type, uint32_t extension_mask)
{
  for (const ChunkHeader &chunk : chunks.chunks)
  {
    if (type != chunk.type)
      continue;
    if (chunk.extensionBits != (extension_mask & chunk.extensionBits))
      continue;
    return &chunk;
  }
  return nullptr;
}

dag::ConstSpan<uint8_t> spirv::find_chunk_data(const ChunkSetRef &chunks, ChunkType type, uint32_t extension_bits)
{
  const ChunkHeader *selected = find_chunk(chunks, type, extension_bits);
  if (!selected)
    return {};
  G_ASSERT(selected->offset + selected->size <= chunks.chunkData.size());
  return make_span_const(chunks.chunkData.data() + selected->offset, selected->size);
}

const char *spirv::chunk_type_name(ChunkType type)
{
  switch (type)
  {
    case ChunkType::SHADER_HEADER: return "SHADER_HEADER";
    case ChunkType::SMOL_V: return "SMOL_V";
    case ChunkType::MARK_V: return "MARK_V";
    case ChunkType::SPIR_V: return "SPIR_V";
    case ChunkType::SPIR_V_DISASSEMBLY: return "SPIR_V_DISASSEMBLY";
    case ChunkType::HLSL_DISASSEMBLY: return "HLSL_DISASSEMBLY";
    case ChunkType::RECONSTRUCTED_GLSL: return "RECONSTRUCTED_GLSL";
    case ChunkType::RECONSTRUCTED_HLSL_DISASSEMBLY: return "RECONSTRUCTED_HLSL_DISASSEMBLY";
    case ChunkType::HLSL_AND_RECONSTRUCTED_HLSL_XDIF: return "HLSL_AND_RECONSTRUCTED_HLSL_XDIF";
    case ChunkType::UNPROCESSED_HLSL: return "UNPROCESSED_HLSL";
  }
  return "UNKNOWN";
}

bool spirv::decode_smolv(dag::ConstSpan<uint8_t> maybe_smolv, uint32_t smolv_size, Tab<uint8_t> &out_spirv)
{
  if (!smolv_size || smolv_size > maybe_smolv.size())
    return false;
  out_spirv.resize(smolv::GetDecodedBufferSize(maybe_smolv.data(), smolv_size));
  return smolv::Decode(maybe_smolv.data(), smolv_size, out_spirv.data(), out_spirv.size());
}

eastl::optional<spirv::ExtractedHeader> spirv::extract_header(const ChunkSetRef &chunks, uint32_t extension_bits,
  eastl::string *out_error)
{
  const ChunkHeader *selected = find_chunk(chunks, ChunkType::SHADER_HEADER, extension_bits);
  if (!selected)
    return {};

  G_ASSERT(selected->offset + selected->size <= chunks.chunkData.size());
  auto &hdr = *reinterpret_cast<const ShaderHeader *>(chunks.chunkData.data() + selected->offset);
  if (hdr.verMagic == HEADER_MAGIC_VER)
    return ExtractedHeader{hdr, selected->hash};
  if (out_error)
    out_error->sprintf("expected shader header ver %08X got %08X, check shader dump integrity!", HEADER_MAGIC_VER, hdr.verMagic);
  return {};
}
