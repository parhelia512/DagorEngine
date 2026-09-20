// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "shader.h"
#include "device_context.h"
#include "pipeline/blk_cache.h"

#include <drv/shadersMetaData/dxil/unpack.h>
#include <EASTL/fixed_vector.h>

using namespace drv3d_dx12;

#define g_main debug_pixel_shader
#if _TARGET_XBOXONE
#include "shaders/debug.ps.x.h"
#elif _TARGET_SCARLETT
#include "shaders/debug.ps.xs.h"
#else
#include "shaders/debug.ps.h"
#endif
#undef g_main

#define g_main debug_vertex_shader
#if _TARGET_XBOXONE
#include "shaders/debug.vs.x.h"
#elif _TARGET_SCARLETT
#include "shaders/debug.vs.xs.h"
#else
#include "shaders/debug.vs.h"
#endif
#undef g_main

#define g_main null_pixel_shader
#if _TARGET_XBOXONE
#include "shaders/null.ps.x.h"
#elif _TARGET_SCARLETT
#include "shaders/null.ps.xs.h"
#else
#include "shaders/null.ps.h"
#endif
#undef g_main

bool InputLayout::fromVdecl(DecodeContext &context, const VSDTYPE &decl)
{
  if (VSD_END == decl)
  {
    return false;
  }

  const auto op = decl & VSDOP_MASK;
  if (op == VSDOP_INPUT)
  {
    if (decl & VSD_SKIPFLG)
    {
      context.ofs += GET_VSDSKIP(decl) * 4;
      return true;
    }

    const auto data = decl & VSDT_MASK;

    uint32_t locationIndex = GET_VSDREG(decl);
    useLocation(locationIndex);
    setLocationStreamSource(locationIndex, context.streamIndex);
    setLocationStreamOffset(locationIndex, context.ofs);
    setLocationFormatIndex(locationIndex, data);

    uint32_t sz = 0; // size of entry
    //-V::1037
    switch (data)
    {
      case VSDT_FLOAT1: sz = 32; break;
      case VSDT_FLOAT2: sz = 32 + 32; break;
      case VSDT_FLOAT3: sz = 32 + 32 + 32; break;
      case VSDT_FLOAT4: sz = 32 + 32 + 32 + 32; break;
      case VSDT_INT1: sz = 32; break;
      case VSDT_INT2: sz = 32 + 32; break;
      case VSDT_INT3: sz = 32 + 32 + 32; break;
      case VSDT_INT4: sz = 32 + 32 + 32 + 32; break;
      case VSDT_UINT1: sz = 32; break;
      case VSDT_UINT2: sz = 32 + 32; break;
      case VSDT_UINT3: sz = 32 + 32 + 32; break;
      case VSDT_UINT4: sz = 32 + 32 + 32 + 32; break;
      case VSDT_HALF2: sz = 16 + 16; break;
      case VSDT_SHORT2N: sz = 16 + 16; break;
      case VSDT_SHORT2: sz = 16 + 16; break;
      case VSDT_USHORT2N: sz = 16 + 16; break;

      case VSDT_HALF4: sz = 16 + 16 + 16 + 16; break;
      case VSDT_SHORT4N: sz = 16 + 16 + 16 + 16; break;
      case VSDT_SHORT4: sz = 16 + 16 + 16 + 16; break;
      case VSDT_USHORT4N: sz = 16 + 16 + 16 + 16; break;

      case VSDT_UDEC3: sz = 10 + 10 + 10 + 2; break;
      case VSDT_DEC3N: sz = 10 + 10 + 10 + 2; break;

      case VSDT_E3DCOLOR: sz = 8 + 8 + 8 + 8; break;
      case VSDT_UBYTE4: sz = 8 + 8 + 8 + 8; break;
      default: D3D_CONTRACT_ASSERT_FAIL_RETURN(false, "invalid vertex declaration type 0x%08X", data); break;
    }
    context.ofs += sz / 8;
  }
  else if (op == VSDOP_STREAM)
  {
    context.streamIndex = GET_VSDSTREAM(decl);
    context.ofs = 0;
    useStream(context.streamIndex);
    if (decl & VSDS_PER_INSTANCE_DATA)
    {
      setStreamStepRate(context.streamIndex, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA);
    }
    else
    {
      setStreamStepRate(context.streamIndex, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA);
    }
  }
  else
  {
    G_ASSERTF_RETURN(0, false, "Invalid vsd opcode 0x%08X", decl);
  }

  return true;
}

static void set_module_debug_name_from_source(auto *module, const ShaderSourceExt &source)
{
  G_UNUSED(module);
  G_UNUSED(source);
#if DAGOR_DBGLEVEL > 0
  if (auto debugName = source.getDebugName(); !debugName.empty() && module)
    module->debugName = debugName;
#endif
}

namespace
{
StageShaderModule to_stage_module(const dxil::StageModuleRef &ref, const ShaderSource &source)
{
  StageShaderModule result;
  result.ident.shaderHash = ref.hash;
  result.ident.shaderSize = ref.hashedSize;
  result.header = ref.header;
  result.source = source;
  result.bytecodeOffset = ref.bytecodeOffset;
  result.bytecodeSize = ref.bytecodeSize;
  if (!ref.debugName.empty())
    result.debugName.assign(ref.debugName.begin(), ref.debugName.end());
  return result;
}

StageShaderModuleInBinaryRef to_stage_module_ref(const dxil::StageModuleRef &ref, const uint8_t *bytecode)
{
  StageShaderModuleInBinaryRef result;
  result.ident.shaderHash = ref.hash;
  result.ident.shaderSize = ref.hashedSize;
  result.header = ref.header;
  result.byteCode = {bytecode + ref.bytecodeOffset, bytecode + ref.bytecodeOffset + ref.bytecodeSize};
  if (!ref.debugName.empty())
    result.debugName.assign(ref.debugName.begin(), ref.debugName.end());
  return result;
}
} // namespace

eastl::unique_ptr<VertexShaderModule> drv3d_dx12::decode_vertex_shader(const ShaderSourceExt &source)
{
  dxil::DecodedShaderRef decoded;
  eastl::string error;
  if (!dxil::decode_metadata(source.metadata, true, decoded, &error))
  {
    D3D_ERROR("DX12: Error while decoding vertex shader, %s", error.c_str());
    return {};
  }
#if _TARGET_XBOXONE
  // XB1 has no mesh shader stage
  if (decoded.isMesh)
  {
    D3D_ERROR("DX12: Error while decoding vertex shader, unexpected combined shader stage type %u", decoded.main.header.shaderType);
    return {};
  }
#endif

  auto vs = eastl::make_unique<VertexShaderModule>(to_stage_module(decoded.main, source));
  if (!decoded.streamOutput.empty())
  {
    vs->streamOutputDesc.resize(decoded.streamOutput.size());
    eastl::copy(decoded.streamOutput.begin(), decoded.streamOutput.end(), vs->streamOutputDesc.begin());
  }
  if (decoded.gsOrAs)
    vs->geometryShader = eastl::make_unique<StageShaderModule>(to_stage_module(decoded.gsOrAs, source));
  if (decoded.hs)
    vs->hullShader = eastl::make_unique<StageShaderModule>(to_stage_module(decoded.hs, source));
  if (decoded.ds)
    vs->domainShader = eastl::make_unique<StageShaderModule>(to_stage_module(decoded.ds, source));

  set_module_debug_name_from_source(vs.get(), source);
  return vs;
}

eastl::unique_ptr<PixelShaderModule> drv3d_dx12::decode_pixel_shader(const ShaderSourceExt &source)
{
  dxil::DecodedShaderRef decoded;
  eastl::string error;
  if (!dxil::decode_metadata(source.metadata, false, decoded, &error))
  {
    D3D_ERROR("DX12: Error while decoding pixel shader, %s", error.c_str());
    return {};
  }

  auto ps = eastl::make_unique<PixelShaderModule>(to_stage_module(decoded.main, source));
  set_module_debug_name_from_source(ps.get(), source);
  return ps;
}

uint16_t drv3d_dx12::decode_implicit_cbuf_reg_count(dag::ConstSpan<uint8_t> metadata, bool expect_vertex_pipeline)
{
  dxil::DecodedShaderRef decoded;
  if (!dxil::decode_metadata(metadata, expect_vertex_pipeline, decoded))
    return 0;
  uint32_t count = decoded.main.header.implicitCbufRegCount;
  for (const dxil::StageModuleRef *sub : {&decoded.gsOrAs, &decoded.hs, &decoded.ds})
  {
    if (*sub)
      count = max(count, sub->header.implicitCbufRegCount);
  }
  G_ASSERT(count < UINT16_MAX); // Must be true, max cbuf size is in the 16-bit range
  return uint16_t(count);
}

static uint32_t vertex_module_implicit_cbuf_reg_count(const VertexShaderModule &vs)
{
  uint32_t count = vs.header.implicitCbufRegCount;
  for (const StageShaderModule *sub : {vs.geometryShader.get(), vs.hullShader.get(), vs.domainShader.get()})
  {
    if (sub)
      count = max(count, sub->header.implicitCbufRegCount);
  }
  G_ASSERT(count < UINT16_MAX); // Must be true, max cbuf size is in the 16-bit range
  return uint16_t(count);
}

ComputeShaderModule drv3d_dx12::decode_compute_shader(const ShaderSource &source)
{
  dxil::DecodedShaderRef decoded;
  eastl::string error;
  if (!dxil::decode_metadata(source.metadata, false, decoded, &error))
  {
    D3D_ERROR("DX12: Error while decoding compute shader, %s", error.c_str());
    return {};
  }
  return to_stage_module(decoded.main, source);
}

VertexShaderModuleInBinaryRef drv3d_dx12::decode_vertex_shader_ref(const void *data, uint32_t size, const uint8_t *bytecode)
{
  G_ASSERT(data);
  G_ASSERT(bytecode);
  VertexShaderModuleInBinaryRef vs;
  dxil::DecodedShaderRef decoded;
  eastl::string error;
  if (!dxil::decode_metadata(make_span_const(reinterpret_cast<const uint8_t *>(data), size), true, decoded, &error))
  {
    D3D_ERROR("DX12: Error while decoding vertex shader, %s", error.c_str());
    return vs;
  }
#if _TARGET_XBOXONE
  // XB1 has no mesh shader stage
  if (decoded.isMesh)
  {
    D3D_ERROR("DX12: Error while decoding vertex shader, unexpected combined shader stage type %u", decoded.main.header.shaderType);
    return vs;
  }
#endif

  static_cast<StageShaderModuleInBinaryRef &>(vs) = to_stage_module_ref(decoded.main, bytecode);
  vs.streamOutputDesc = decoded.streamOutput;
  if (decoded.gsOrAs)
    vs.geometryShader = to_stage_module_ref(decoded.gsOrAs, bytecode);
  if (decoded.hs)
    vs.hullShader = to_stage_module_ref(decoded.hs, bytecode);
  if (decoded.ds)
    vs.domainShader = to_stage_module_ref(decoded.ds, bytecode);
  return vs;
}

PixelShaderModuleInBinaryRef drv3d_dx12::decode_pixel_shader_ref(const void *data, uint32_t size, const uint8_t *bytecode)
{
  PixelShaderModuleInBinaryRef result;
  dxil::DecodedShaderRef decoded;
  eastl::string error;
  if (!dxil::decode_metadata(make_span_const(reinterpret_cast<const uint8_t *>(data), size), false, decoded, &error))
  {
    D3D_ERROR("DX12: Error while decoding pixel shader, %s", error.c_str());
    return result;
  }
  result = to_stage_module_ref(decoded.main, bytecode);
  return result;
}

void ShaderProgramDatabase::initDebugProgram(DeviceContext &ctx)
{
  dxil::ShaderHeader debugVSHeader = {};
  debugVSHeader.shaderType = static_cast<uint16_t>(dxil::ShaderStage::VERTEX);
  debugVSHeader.inOutSemanticMask = 1ul << dxil::getIndexFromSementicAndSemanticIndex("POSITION", 0);
  debugVSHeader.inOutSemanticMask |= 1ul << dxil::getIndexFromSementicAndSemanticIndex("COLOR", 0);
  debugVSHeader.resourceUsageTable.bRegisterUseMask = 1ul << 0;
  debugVSHeader.implicitCbufRegCount = 4;

  dxil::ShaderHeader debugPSHeader = {};
  debugPSHeader.shaderType = static_cast<uint16_t>(dxil::ShaderStage::PIXEL);
  debugPSHeader.inOutSemanticMask = 0x0000000F;
  debugPSHeader.implicitCbufRegCount = 0;

  VSDTYPE ilDefAry[] = //
    {VSD_STREAM(0), VSD_REG(VSDR_POS, VSDT_FLOAT3), VSD_REG(VSDR_DIFF, VSDT_E3DCOLOR), VSD_END};
  InputLayout ilDef;
  ilDef.fromVdecl(ilDefAry);

  auto vs = newRawVertexShader(ctx, debugVSHeader, make_span(debug_vertex_shader, sizeof(debug_vertex_shader)));
  auto ps = newRawPixelShader(ctx, debugPSHeader, make_span(debug_pixel_shader, sizeof(debug_pixel_shader)));
  auto il = registerInputLayoutInternal(ctx, ilDef);

  debugProgram = newGraphicsProgram(ctx, il, vs, ps);
}

void ShaderProgramDatabase::initNullPixelShader(DeviceContext &ctx)
{
  dxil::ShaderHeader nullHeader = {};
  nullHeader.shaderType = static_cast<uint16_t>(dxil::ShaderStage::PIXEL);
  auto nullShader = make_span(null_pixel_shader, countof(null_pixel_shader));
  auto nPSH = newRawPixelShader(ctx, nullHeader, nullShader);
  nullPixelShader = nPSH;
}

ShaderID ShaderProgramDatabase::newRawVertexShader(DeviceContext &ctx, const dxil::ShaderHeader &header,
  dag::ConstSpan<uint8_t> byte_code)
{
  auto module = eastl::make_unique<VertexShaderModule>();
  module->ident.shaderHash = dxil::HashValue::calculate(byte_code.data(), byte_code.size());
  module->ident.shaderSize = static_cast<uint32_t>(byte_code.size());
  module->header = header;
  module->source = ShaderSource{.compressedData = byte_code, .uncompressedSize = byte_code.size()};
  module->bytecodeSize = byte_code.size();
  ShaderID id;
  {
    ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);
    id = shaderProgramGroups.addVertexShader(header.implicitCbufRegCount);
  }
  ctx.addVertexShader(id, eastl::move(module));
  return id;
}

ShaderID ShaderProgramDatabase::newRawPixelShader(DeviceContext &ctx, const dxil::ShaderHeader &header,
  dag::ConstSpan<uint8_t> byte_code)
{
  auto module = eastl::make_unique<PixelShaderModule>();
  module->ident.shaderHash = dxil::HashValue::calculate(byte_code.data(), byte_code.size());
  module->ident.shaderSize = static_cast<uint32_t>(byte_code.size());
  module->header = header;
  module->source = ShaderSource{.compressedData = byte_code, .uncompressedSize = byte_code.size()};
  module->bytecodeSize = byte_code.size();
  ShaderID id;
  {
    ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);
    id = shaderProgramGroups.addPixelShader(header.implicitCbufRegCount);
  }
  ctx.addPixelShader(id, eastl::move(module));
  return id;
}

ProgramID ShaderProgramDatabase::newComputeProgram(DeviceContext &ctx, const ShaderSourceExt &source, CSPreloaded preloaded)
{
  auto basicModule = decode_compute_shader(source);
  if (!basicModule)
  {
    return ProgramID::Null();
  }

  auto module = eastl::make_unique<ComputeShaderModule>(eastl::move(basicModule));
  set_module_debug_name_from_source(module.get(), source);

  ProgramID program;
  {
    ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);
    program = shaderProgramGroups.addComputeShaderProgram(module->header.implicitCbufRegCount);
  }
  ctx.addComputeProgram(program, eastl::move(module), preloaded);

  return program;
}

ProgramID ShaderProgramDatabase::newGraphicsProgram(DeviceContext &ctx, InputLayoutID vdecl, ShaderID vs, ShaderID ps)
{
  const auto key = shaderProgramGroups.getGraphicsProgramKey(vdecl, vs, ps);
  {
    ScopedLockReadTemplate<OSReadWriteLock> lock(dataGuard);
    ProgramID hashed = shaderProgramGroups.incrementCachedGraphicsProgram(key);
    if (hashed != ProgramID::Null())
      return hashed;
  }
  ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);

  // null pixel shader has to be patched here
  if (ps == ShaderID::Null())
  {
    ps = nullPixelShader;
  }

  GraphicsProgramID gid = shaderProgramGroups.findGraphicsProgram(vs, ps);
  if (GraphicsProgramID::Null() == gid)
  {
    gid = shaderProgramGroups.addGraphicsProgram(vs, ps);

    dataGuard.unlockWrite();
    ctx.addGraphicsProgram(gid, vs, ps);
    dataGuard.lockWrite();
  }
  // kick off loading of grouped graphics pipeline
  else if (gid.getGroup() != 0)
  {
    dataGuard.unlockWrite();
    ctx.addGraphicsProgram(gid, vs, ps);
    dataGuard.lockWrite();
  }

  return shaderProgramGroups.instanciateGraphicsProgram(key, gid, vdecl);
}

InputLayoutID ShaderProgramDatabase::getInputLayoutForGraphicsProgram(ProgramID program)
{
  return shaderProgramGroups.getGraphicsProgramInstanceInputLayout(program);
}

GraphicsProgramUsageInfo ShaderProgramDatabase::getGraphicsProgramForStateUpdate(ProgramID program)
{
  ScopedLockReadTemplate<OSReadWriteLock> lock(dataGuard);
  return shaderProgramGroups.getUsageInfo(program);
}

ComputeProgramUsageInfo ShaderProgramDatabase::getComputeProgramForStateUpdate(ProgramID program)
{
  ScopedLockReadTemplate<OSReadWriteLock> lock(dataGuard);
  return {program, shaderProgramGroups.getComputeProgramImplicitCbufRegCount(program)};
}

InputLayoutID ShaderProgramDatabase::registerInputLayoutInternal(DeviceContext &ctx, const InputLayout &layout)
{
  auto ref = eastl::find(begin(publicImputLayoutTable), end(publicImputLayoutTable), layout);

  InputLayoutID ident;
  if (ref == end(publicImputLayoutTable))
  {
    ref = publicImputLayoutTable.emplace(ref, layout);
    ident = InputLayoutID{int(ref - begin(publicImputLayoutTable))};
    ctx.registerInputLayout(ident, layout);
  }
  else
  {
    ident = InputLayoutID{int(ref - begin(publicImputLayoutTable))};
  }

  return ident;
}

InputLayoutID ShaderProgramDatabase::registerInputLayout(DeviceContext &ctx, const InputLayout &layout)
{
  ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);
  return registerInputLayoutInternal(ctx, layout);
}

void ShaderProgramDatabase::setup(DeviceContext &ctx, bool disable_precache)
{
  disablePreCache = disable_precache;

  initNullPixelShader(ctx);
  initDebugProgram(ctx);
}

void ShaderProgramDatabase::shutdown(DeviceContext &ctx)
{
  STORE_RETURN_ADDRESS();
  ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);

  shaderProgramGroups.iterateAllGraphicsPrograms([&](GraphicsProgramID gp) { ctx.removeGraphicsProgram(gp); });

  shaderProgramGroups.iterateAllComputeShaders([&](ProgramID prog) { ctx.removeProgram(prog); });

  shaderProgramGroups.itarateAllVertexShaders([&](ShaderID shader) { ctx.removeVertexShader(shader); });

  shaderProgramGroups.iterateAllPixelShaders([&](ShaderID shader) { ctx.removePixelShader(shader); });

  shaderProgramGroups.reset();

  publicImputLayoutTable.clear();

  debugProgram = ProgramID::Null();
  nullPixelShader = ShaderID::Null();
}

ShaderID ShaderProgramDatabase::newVertexShader(DeviceContext &ctx, const ShaderSourceExt &source)
{
  auto vs = decode_vertex_shader(source);

  ShaderID id = ShaderID::Null();
  if (vs)
  {
    {
      ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);
      id = shaderProgramGroups.addVertexShader(vertex_module_implicit_cbuf_reg_count(*vs));
    }
    ctx.addVertexShader(id, eastl::move(vs));
  }
  return id;
}

ShaderID ShaderProgramDatabase::newPixelShader(DeviceContext &ctx, const ShaderSourceExt &source)
{
  auto ps = decode_pixel_shader(source);

  ShaderID id = ShaderID::Null();
  if (ps)
  {
    {
      ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);
      id = shaderProgramGroups.addPixelShader(ps->header.implicitCbufRegCount);
    }

    ctx.addPixelShader(id, eastl::move(ps));
  }
  return id;
}

ProgramID ShaderProgramDatabase::getDebugProgram() { return debugProgram; }

void ShaderProgramDatabase::removeProgram(DeviceContext &ctx, ProgramID prog)
{
  ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);

  if (prog.isGraphics())
  {
    shaderProgramGroups.removeGraphicsProgramInstance(prog);
  }
  else if (prog.isCompute())
  {
    // only remove compute programs if not part of a shader group
    if (0 == prog.getGroup())
    {
      shaderProgramGroups.removeComputeProgram(prog);
      ctx.removeProgram(prog);
    }
  }
}

void ShaderProgramDatabase::deleteVertexShader(DeviceContext &ctx, ShaderID shader)
{
  // we never delete shaders of a bindump
  if (shader.getGroup() > 0)
    return;

  eastl::fixed_vector<GraphicsProgramID, 2, true> toRemove;
  {
    ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);
    shaderProgramGroups.removeVertexShader(shader);
    shaderProgramGroups.removeGraphicsProgramWithMatchingVertexShader(shader, [this, &toRemove](GraphicsProgramID gpid) //
      {
        toRemove.push_back(gpid);
        shaderProgramGroups.removeGraphicsProgramInstancesUsingMatchingTemplate(gpid);
      });
  }

  ctx.removeVertexShader(shader);
  for (auto gpid : toRemove)
    ctx.removeGraphicsProgram(gpid);
}

void ShaderProgramDatabase::deletePixelShader(DeviceContext &ctx, ShaderID shader)
{
  // we never delete shaders of a bindump
  if (shader.getGroup() > 0)
    return;

  eastl::fixed_vector<GraphicsProgramID, 2, true> toRemove;
  {
    ScopedLockWriteTemplate<OSReadWriteLock> lock(dataGuard);
    shaderProgramGroups.removePixelShader(shader);
    shaderProgramGroups.removeGraphicsProgramWithMatchingPixelShader(shader, [this, &toRemove](GraphicsProgramID gpid) //
      {
        toRemove.push_back(gpid);
        shaderProgramGroups.removeGraphicsProgramInstancesUsingMatchingTemplate(gpid);
      });
  }
  ctx.removePixelShader(shader);
  for (auto gpid : toRemove)
    ctx.removeGraphicsProgram(gpid);
}

void ShaderProgramDatabase::registerShaderBinDump(DeviceContext &ctx, ScriptedShadersBinDumpOwner *dump, const char *name)
{
  if (!dump)
  {
    ctx.removeShaderGroup(1);
    // wait until backend has processed the removal
    ctx.finish();
    // dropGroup should be called after ctx.finish() to avoid using broken dump in context commands
    shaderProgramGroups.dropGroup(1);
    return;
  }

  logdbg("DX12: registerShaderBinDump %p <%s>", dump, name);
  if (ShaderID::Null() == nullPixelShader)
  {
    initNullPixelShader(ctx);
  }

  shaderProgramGroups.setGroup(1, dump, nullPixelShader);
  ctx.addShaderGroup(1, dump, nullPixelShader, name);
}

void ShaderProgramDatabase::getBindumpShader(DeviceContext &ctx, uint32_t index, ShaderCodeType type, void *ident)
{
  auto stage = shaderProgramGroups.shaderFromIndex(1, index, type, ident);
  if (dxil::ShaderStage::COMPUTE == stage)
  {
    ctx.loadComputeShaderFromDump(ProgramID::importValue(*static_cast<FSHADER *>(ident)));
  }
}

DynamicArray<InputLayoutIDWithHash> ShaderProgramDatabase::loadInputLayoutFromBlk(DeviceContext &ctx, const DataBlock *blk,
  const char *default_format)
{
  DynamicArray<InputLayoutIDWithHash> result{blk->blockCount()};
  pipeline::DataBlockDecodeEnumarator<pipeline::InputLayoutDecoder> decoder{*blk, 0, default_format};
  for (; !decoder.completed(); decoder.next())
  {
    auto bi = decoder.index();
    auto &target = result[bi];
    InputLayout il;
    if (decoder.decode(il, target.hash))
    {
      target.id = registerInputLayout(ctx, il);
    }
    else
    {
      target.id = InputLayoutID::Null();
    }
  }
  return result;
}

void backend::ShaderModuleManager::addVertexShader(ShaderID id, VertexShaderModule *module)
{
  eastl::unique_ptr<VertexShaderModule> modulePtr{module};

  G_ASSERT_RETURN(0 == id.getGroup(), );

  ensure_container_space(shaderGroupZero.vertex, id.getIndex() + 1);
  auto &shader = shaderGroupZero.vertex[id.getIndex()];
  shader = eastl::make_unique<GroupZeroVertexShaderModule>();
  shader->header.hash = module->ident.shaderHash;
  shader->header.header = module->header;
  shader->header.streamOutputDesc.resize(module->streamOutputDesc.size());
  eastl::copy(module->streamOutputDesc.begin(), module->streamOutputDesc.end(), shader->header.streamOutputDesc.data());
  shader->header.debugName = module->debugName;
  shader->bytecode.source = module->source;
  shader->bytecode.bytecodeOffset = module->bytecodeOffset;
  shader->bytecode.bytecodeSize = module->bytecodeSize;
  dxil::ShaderHeader subShaderHeaders[3];
  StageShaderModuleBytecode subShaderBytecodes[3];
  uint32_t subShaderCount = 0;
  if (module->geometryShader)
  {
    auto &headerTarget = subShaderHeaders[subShaderCount];
    auto &bytecodeTarget = subShaderBytecodes[subShaderCount++];
    headerTarget = module->geometryShader->header;
    bytecodeTarget.bytecodeOffset = module->geometryShader->bytecodeOffset;
    bytecodeTarget.bytecodeSize = module->geometryShader->bytecodeSize;

    shader->header.hasGsOrAs = true;
  }
  if (module->hullShader && module->domainShader)
  {
    {
      auto &headerTarget = subShaderHeaders[subShaderCount];
      auto &bytecodeTarget = subShaderBytecodes[subShaderCount++];
      headerTarget = module->hullShader->header;
      bytecodeTarget.bytecodeOffset = module->hullShader->bytecodeOffset;
      bytecodeTarget.bytecodeSize = module->hullShader->bytecodeSize;
    }
    {
      auto &headerTarget = subShaderHeaders[subShaderCount];
      auto &bytecodeTarget = subShaderBytecodes[subShaderCount++];
      headerTarget = module->domainShader->header;
      bytecodeTarget.bytecodeOffset = module->domainShader->bytecodeOffset;
      bytecodeTarget.bytecodeSize = module->domainShader->bytecodeSize;
    }

    shader->header.hasDsAndHs = true;
  }
  if (subShaderCount > 0)
  {
    shader->header.subShaders = eastl::make_unique<dxil::ShaderHeader[]>(subShaderCount);
    eastl::copy_n(subShaderHeaders, subShaderCount, shader->header.subShaders.get());

    shader->bytecode.subShaders = eastl::make_unique<StageShaderModuleBytecode[]>(subShaderCount);
    for (uint32_t i = 0; i < subShaderCount; ++i)
    {
      shader->bytecode.subShaders[i] = eastl::move(subShaderBytecodes[i]);
    }
  }
}

void backend::ShaderModuleManager::addPixelShader(ShaderID id, PixelShaderModule *module)
{
  eastl::unique_ptr<PixelShaderModule> modulePtr{module};

  G_ASSERT_RETURN(0 == id.getGroup(), );

  ensure_container_space(shaderGroupZero.pixel, id.getIndex() + 1);
  auto &shader = shaderGroupZero.pixel[id.getIndex()];
  shader = eastl::make_unique<GroupZeroPixelShaderModule>();
  shader->header.hash = module->ident.shaderHash;
  shader->header.header = module->header;
  shader->header.debugName = module->debugName;
  shader->bytecode.source = module->source;
  shader->bytecode.bytecodeOffset = module->bytecodeOffset;
  shader->bytecode.bytecodeSize = module->bytecodeSize;
}

const dxil::HashValue &backend::ShaderModuleManager::getVertexShaderHash(ShaderID id) const
{
  if (0 != id.getGroup())
  {
    return shaderGroup[id.getGroup() - 1].vertex[id.getIndex()]->header.hash;
  }

  return shaderGroupZero.vertex[id.getIndex()]->header.hash;
}

const dxil::HashValue &backend::ShaderModuleManager::getPixelShaderHash(ShaderID id) const
{
  if (0 != id.getGroup())
  {
    return shaderGroup[id.getGroup() - 1].pixel[id.getIndex()]->header.hash;
  }

  return shaderGroupZero.pixel[id.getIndex()]->header.hash;
}

backend::VertexShaderModuleRefStore backend::ShaderModuleManager::getVertexShader(ShaderID id)
{
  if (0 != id.getGroup())
  {
    auto &container = shaderGroup[id.getGroup() - 1].vertex;
    auto &shader = container[id.getIndex()];
    // when shader size is 0 we did not decoded the shader binary
    if (0 == shader->bytecode.shaderSize)
    {
      auto [meta, byteCode, cacheHit] = getShaderByteCode(id.getGroup(), shader->bytecode.compressionIndex);
      if (!byteCode.empty())
      {
        auto module = decode_vertex_shader_ref(meta.data(), meta.size(), byteCode.data());
        shader->header.header = module.header;
        shader->header.debugName = module.debugName;
        shader->header.streamOutputDesc.resize(module.streamOutputDesc.size());
        eastl::copy(module.streamOutputDesc.begin(), module.streamOutputDesc.end(), shader->header.streamOutputDesc.data());
        shader->bytecode.shaderOffset = offset_to_base(byteCode.data(), module);
        shader->bytecode.shaderSize = module.byteCode.size();

        dxil::ShaderHeader subShaderHeaders[3];
        StageShaderModuleBytcodeInDumpOffsets subShaderBytecodes[3];
        uint32_t subShaderCount = 0;
        if (!module.geometryShader.byteCode.empty())
        {
          auto &headerTarget = subShaderHeaders[subShaderCount];
          auto &bytecodeTarget = subShaderBytecodes[subShaderCount++];
          headerTarget = module.geometryShader.header;
          bytecodeTarget.shaderOffset = offset_to_base(byteCode.data(), module.geometryShader);
          bytecodeTarget.shaderSize = module.geometryShader.byteCode.size();

          shader->header.hasGsOrAs = true;
        }
        if (!module.hullShader.byteCode.empty() && !module.domainShader.byteCode.empty())
        {
          {
            auto &headerTarget = subShaderHeaders[subShaderCount];
            auto &bytecodeTarget = subShaderBytecodes[subShaderCount++];
            headerTarget = module.hullShader.header;
            bytecodeTarget.shaderOffset = offset_to_base(byteCode.data(), module.hullShader);
            bytecodeTarget.shaderSize = module.hullShader.byteCode.size();
          }
          {
            auto &headerTarget = subShaderHeaders[subShaderCount];
            auto &bytecodeTarget = subShaderBytecodes[subShaderCount++];
            headerTarget = module.domainShader.header;
            bytecodeTarget.shaderOffset = offset_to_base(byteCode.data(), module.domainShader);
            bytecodeTarget.shaderSize = module.domainShader.byteCode.size();
          }

          shader->header.hasDsAndHs = true;
        }
        if (subShaderCount > 0)
        {
          shader->header.subShaders = eastl::make_unique<dxil::ShaderHeader[]>(subShaderCount);
          eastl::copy_n(subShaderHeaders, subShaderCount, shader->header.subShaders.get());

          shader->bytecode.subShaders = eastl::make_unique<StageShaderModuleBytcodeInDumpOffsets[]>(subShaderCount);
          eastl::copy_n(subShaderBytecodes, subShaderCount, shader->bytecode.subShaders.get());
        }
      }
    }
    return {shader->header, VertexShaderModuleBytcodeInDumpRef{id.getGroup(), &shader->bytecode}};
  }
  auto &shader = shaderGroupZero.vertex[id.getIndex()];
  return {shader->header, VertexShaderModuleBytecodeRef{&shader->bytecode}};
}

backend::PixelShaderModuleRefStore backend::ShaderModuleManager::getPixelShader(ShaderID id)
{
  if (0 != id.getGroup())
  {
    auto &container = shaderGroup[id.getGroup() - 1].pixel;
    auto &shader = container[id.getIndex()];
    // when shader size is 0 we did not decoded the shader binary
    if (0 == shader->bytecode.shaderSize)
    {
      auto [meta, byteCode, cacheHit] = getShaderByteCode(id.getGroup(), shader->bytecode.compressionIndex);
      if (!byteCode.empty())
      {
        auto module = decode_pixel_shader_ref(meta.data(), meta.size(), byteCode.data());
        shader->header.header = module.header;
        shader->header.debugName = module.debugName;
        shader->bytecode.shaderOffset = offset_to_base(byteCode.data(), module);
        shader->bytecode.shaderSize = module.byteCode.size();
      }
    }
    return {shader->header, PixelShaderModuleBytcodeInDumpRef{id.getGroup(), &shader->bytecode}};
  }

  auto &shader = shaderGroupZero.pixel[id.getIndex()];
  return {shader->header, PixelShaderModuleBytecodeRef{&shader->bytecode}};
}

backend::ShaderModuleManager::AnyShaderModuleUniquePointer backend::ShaderModuleManager::releaseVertexShader(ShaderID id)
{
  if (0 != id.getGroup())
  {
    return eastl::move(shaderGroup[id.getGroup() - 1].vertex[id.getIndex()]);
  }
  return eastl::move(shaderGroupZero.vertex[id.getIndex()]);
}

backend::ShaderModuleManager::AnyShaderModuleUniquePointer backend::ShaderModuleManager::releasePixelShader(ShaderID id)
{
  if (0 != id.getGroup())
  {
    return eastl::move(shaderGroup[id.getGroup() - 1].pixel[id.getIndex()]);
  }
  return eastl::move(shaderGroupZero.pixel[id.getIndex()]);
}

void backend::ShaderModuleManager::resetDumpOfGroup(uint32_t group_index)
{
  ScriptedShadersBinDumpManager::resetDumpOfGroup(group_index);
  if (0 != group_index)
  {
    shaderGroup[group_index - 1].vertex.clear();
    shaderGroup[group_index - 1].pixel.clear();
  }
  else
  {
    shaderGroupZero.vertex.clear();
    shaderGroupZero.pixel.clear();
  }
}

void backend::ShaderModuleManager::reserveVertexShaderRange(uint32_t group_index, uint32_t count)
{
  shaderGroup[group_index - 1].vertex.resize(count);
}

void backend::ShaderModuleManager::setVertexShaderCompressionGroup(uint32_t group_index, uint32_t index, const dxil::HashValue &hash,
  uint32_t compression_index)
{
  G_ASSERT_RETURN(0 != group_index, );
  auto &container = shaderGroup[group_index - 1].vertex;
  ensure_container_space(container, index + 1);
  auto &shader = container[index];
  shader = eastl::make_unique<GroupVertexShaderModule>();
  shader->header.hash = hash;
  shader->bytecode.compressionIndex = compression_index;
}

void backend::ShaderModuleManager::setPixelShaderCompressionGroup(uint32_t group_index, uint32_t index, const dxil::HashValue &hash,
  uint32_t compression_index)
{
  G_ASSERT_RETURN(0 != group_index, );

  auto &container = shaderGroup[group_index - 1].pixel;
  ensure_container_space(container, index + 1);
  auto &shader = container[index];
  shader = eastl::make_unique<GroupPixelShaderModule>();
  shader->header.hash = hash;
  shader->bytecode.compressionIndex = compression_index;
}
