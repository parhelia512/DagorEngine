// Copyright (C) Gaijin Games KFT.  All rights reserved.

// shader mobules/programs implementation
#include <drv/3d/dag_shader.h>
#include <drv/3d/dag_consts.h>
#include <drv/3d/dag_platform.h>
#include <memory/dag_framemem.h>
#include "globals.h"
#include "shader/program_database.h"
#include "device_context.h"
#include "backend/cmd/debug.h"
// after the driver headers: it pulls vulkan.h, which must see vulkan_api.h platform defines first
#include <drv/shadersMetaData/spirv/unpack.h>

using namespace drv3d_vulkan;

static void decode_shader_binary(const uint32_t *metadata, uint32_t size, VkShaderStageFlags, Tab<spirv::ChunkHeader> &chunks,
  Tab<uint8_t> &chunk_data)
{
  eastl::string error;
  if (!spirv::decode_chunked_metadata(make_span_const(reinterpret_cast<const uint8_t *>(metadata), size), chunks, chunk_data, &error))
    DAG_FATAL("vulkan: %s", error.c_str());
}

static int create_shader_for_stage(const uint32_t *metadata, const ShaderSourceExt &source, VkShaderStageFlagBits stage,
  uintptr_t size)
{
  auto metadataSpan = make_span_const(reinterpret_cast<const uint8_t *>(metadata), size);
  if (spirv::is_combined(metadataSpan))
  {
    spirv::CombinedStageList stageRefs;
    eastl::string error;
    if (!spirv::decode_container(metadataSpan, stage, stageRefs, &error))
      DAG_FATAL("vulkan: %s", error.c_str());

    dag::Vector<VkShaderStageFlagBits> comboStages;
    dag::Vector<Tab<spirv::ChunkHeader>> comboChunks;
    dag::Vector<Tab<uint8_t>> comboChunkData;
    dag::Vector<ShaderProgramData> comboBytecode;
    comboStages.reserve(stageRefs.size());
    comboChunks.reserve(stageRefs.size());
    comboChunkData.reserve(stageRefs.size());

    for (const spirv::CombinedStageRef &stageRef : stageRefs)
    {
      Tab<spirv::ChunkHeader> chunks;
      Tab<uint8_t> chunkData;
      decode_shader_binary(reinterpret_cast<const uint32_t *>(stageRef.metadata.data()), stageRef.metadata.size(), stageRef.stage,
        chunks, chunkData);
      comboStages.push_back(stageRef.stage);
      comboChunks.push_back(eastl::move(chunks));
      comboChunkData.push_back(eastl::move(chunkData));
      comboBytecode.emplace_back(stageRef.bytecodeOffset, stageRef.bytecodeSize);
    }
    return Globals::shaderProgramDatabase
      .newShader(Globals::ctx, eastl::move(comboStages), eastl::move(comboChunks), eastl::move(comboChunkData),
        eastl::move(comboBytecode), source)
      .get();
  }
  else
  {
    Tab<spirv::ChunkHeader> chunks;
    Tab<uint8_t> chunkData;
    decode_shader_binary(metadata, size, stage, chunks, chunkData);
    return Globals::shaderProgramDatabase.newShader(Globals::ctx, stage, chunks, chunkData, source).get();
  }
}

VPROG d3d::create_vertex_shader(const ShaderSourceExt &data)
{
  return create_shader_for_stage((const uint32_t *)data.metadata.data(), data, VK_SHADER_STAGE_VERTEX_BIT, data.metadata.size());
}

void d3d::delete_vertex_shader(VPROG vs) { Globals::shaderProgramDatabase.deleteShader(Globals::ctx, ShaderID(vs)); }


FSHADER d3d::create_pixel_shader(const ShaderSourceExt &data)
{
  return create_shader_for_stage((const uint32_t *)data.metadata.data(), data, VK_SHADER_STAGE_FRAGMENT_BIT, data.metadata.size());
}

void d3d::delete_pixel_shader(FSHADER ps) { Globals::shaderProgramDatabase.deleteShader(Globals::ctx, ShaderID(ps)); }

PROGRAM d3d::get_debug_program() { return Globals::shaderProgramDatabase.getDebugProgram().get(); }

PROGRAM d3d::create_program(VPROG vs, FSHADER fs, VDECL vdecl, unsigned *, unsigned)
{
  return Globals::shaderProgramDatabase.newGraphicsProgram(Globals::ctx, InputLayoutID(vdecl), ShaderID(vs), ShaderID(fs)).get();
}

PROGRAM d3d::create_program_cs(const ShaderSourceExt &data, CSPreloaded)
{
  Tab<spirv::ChunkHeader> chunks;
  Tab<uint8_t> chunkData;
  decode_shader_binary((const uint32_t *)data.metadata.data(), data.metadata.size(), VK_SHADER_STAGE_COMPUTE_BIT, chunks, chunkData);
  [[maybe_unused]] auto debugName = data.getDebugName();

  auto smh = spirv_extractor::getHeader(VK_SHADER_STAGE_COMPUTE_BIT, chunks, chunkData, 0);
  if (!smh)
    DAG_FATAL("Shader has no header");

  auto smb = spirv_extractor::getBlob(*smh, data, {0, 0});
  if (smb.source.compressedData.empty())
    DAG_FATAL("Shader has no byte code blob");

#if VULKAN_LOAD_SHADER_EXTENDED_DEBUG_DATA
  if (!debugName.empty())
    smb.name = String(debugName.data(), debugName.length());
#endif

  // if we try to use CS that have raytracing yet device don't support it, return stub program
  // this avoids crashing/issues while keeping caller logic intact
  if (!Globals::VK::phy.hasAccelerationStructure || !Globals::VK::phy.hasRayQuery)
  {
    const auto &hdr = smh->header;
    for (uint32_t i = 0; i < spirv::T_REGISTER_INDEX_MAX; ++i)
      if ((hdr.tRegisterUseMask & (1u << i)) && hdr.resTypeMask.forTSlot(i) == spirv::ResourceTypeMask::AS)
        return Globals::shaderProgramDatabase.getStubComputeProgram().get();
  }

  PROGRAM prog = Globals::shaderProgramDatabase.newComputeProgram(Globals::ctx, *smh, smb).get();

#if VULKAN_LOAD_SHADER_EXTENDED_DEBUG_DATA
  auto dbg = spirv_extractor::getDebugInfo(chunks, chunkData, 0);
  if (!smb.name.empty())
    dbg.name = dbg.debugName = smb.name;
  Globals::ctx.dispatchCmd<CmdAttachComputeProgramDebugInfo>({ProgramID(prog), eastl::make_unique<ShaderDebugInfo>(dbg).release()});
#endif

  return prog;
}

void d3d::delete_program(PROGRAM prog)
{
  ProgramID pid{prog};
  if (pid == Globals::shaderProgramDatabase.getStubComputeProgram())
    return;
  Globals::shaderProgramDatabase.removeProgram(Globals::ctx, pid);
}

#if _TARGET_PC
// NOTE: entry point should be removed from the interface
bool d3d::set_pixel_shader(FSHADER /*shader*/)
{
  G_ASSERT(false);
  return true;
}

// NOTE: entry point should be removed from the interface
bool d3d::set_vertex_shader(VPROG /*shader*/)
{
  G_ASSERT(false);
  return true;
}
#endif // _TARGET_PC
