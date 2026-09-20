// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "rgaPipelineState.h"

#include <drv/shadersMetaData/dxil/utility.h>
#include <drv/shadersMetaData/spirv/compiled_meta_data.h>
#include <drv/shadersMetaData/spirv/translate_d3d_to_vk.h>
#include <ioSys/dag_fileIo.h>
#include <util/dag_string.h>
#include <math/dag_e3dColor.h>
#include <EASTL/algorithm.h>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <vulkan/vulkan.h>

#if _TARGET_PC_WIN
#include <d3d12.h>
#include <drv/shadersMetaData/dxil/root_signature_generator.h>
#endif

using shader_blob::Stage;
using shader_blob::StageBlob;
using shader_blob::UnpackedShader;

static bool set_error(eastl::string &out_error, const char *msg)
{
  out_error = msg;
  return false;
}

static const StageBlob *find_stage(const UnpackedShader &shader, Stage stage)
{
  for (const StageBlob &blob : shader.stages)
    if (blob.stage == stage)
      return &blob;
  return nullptr;
}

static bool write_file(const char *fn, const void *data, size_t size, eastl::string &out_error)
{
  FullFileSaveCB cwr(fn);
  if (!cwr.fileHandle)
  {
    out_error = eastl::string(eastl::string::CtorSprintf{}, "can't open '%s' for writing", fn);
    return false;
  }
  cwr.write(data, (int)size);
  return true;
}

// ---------------------------------------------------------------------------
// DX12
// ---------------------------------------------------------------------------

static uint32_t dx12_render_target_count(const dxil::ShaderHeader &ps_header)
{
  // for PS inOutSemanticMask is an RGBA write mask per each of the 8 render targets
  uint32_t count = 0;
  for (uint32_t rt = 0; rt < 8; ++rt)
    if ((ps_header.inOutSemanticMask >> (rt * 4)) & 0xF)
      count = rt + 1;
  return count;
}

bool rga::pso::write_dx12_gpso(const char *fn, const UnpackedShader &vs, const UnpackedShader &ps, eastl::string &out_error)
{
  const StageBlob *vsBlob = find_stage(vs, Stage::VS);
  const StageBlob *psBlob = find_stage(ps, Stage::PS); // null for depth-only pipelines
  if (!vsBlob || !vsBlob->dxilHeader || (psBlob && !psBlob->dxilHeader))
    return set_error(out_error, "dx12 gpso requires a VS stage with a dxil header");

  // the '# <keyword>' section headers are mandatory: the RGA parser locates sections by them
  String text;
  text += "# schemaVersion\n1.0\n\n";

  const uint32_t inputMask = vsBlob->dxilHeader->inOutSemanticMask;
  uint32_t elementCount = 0;
  for (uint32_t i = 0; i < countof(dxil::semantic_remap); ++i)
    if (inputMask & (1u << i))
      ++elementCount;

  text.aprintf(0, "# InputLayoutNumElements\n%u\n\n", elementCount);

  text += "# InputLayout\n";
  // The dump does not keep the vertex declaration next to the shader, only the semantic use
  // mask, so a generic float4 per semantic is declared; enough for a valid fetch prologue.
  uint32_t offset = 0, written = 0;
  for (uint32_t i = 0; i < countof(dxil::semantic_remap); ++i)
  {
    if (!(inputMask & (1u << i)))
      continue;
    const dxil::SemanticInfo *info = dxil::getSemanticInfoFromIndex(i);
    ++written;
    text.aprintf(0, "{ \"%s\", %u, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, %u, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }%s\n",
      info->name, info->index, offset, written < elementCount ? "," : ""); // -V522
    offset += 16;
  }
  text += "\n";

  const bool hasHull = find_stage(vs, Stage::HS) != nullptr;
  text.aprintf(0, "# PrimitiveTopologyType\n%s\n\n",
    hasHull ? "D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH" : "D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE");

  const uint32_t rtCount = psBlob ? dx12_render_target_count(*psBlob->dxilHeader) : 0; // -V522
  text.aprintf(0, "# NumRenderTargets\n%u\n\n", rtCount);
  text += "# RTVFormats\n{ ";
  for (uint32_t rt = 0; rt < rtCount; ++rt)
    text.aprintf(0, "%sDXGI_FORMAT_R8G8B8A8_UNORM", rt ? ", " : "");
  text += " }\n";

  return write_file(fn, text.str(), text.length(), out_error);
}

#if _TARGET_PC_WIN

namespace
{
// The desc building is shared with the DX12 driver (root_signature_generator.h); the driver-side
// binding bookkeeping hooks are left defaulted. Constant buffers are root descriptors, matching
// the PC runtime default (rootSignaturesUsesCBVDescriptorRanges is off by default).
struct RgaGraphicsRootSignatureGenerator : dxil::GraphicsRootSignatureGeneratorBase<RgaGraphicsRootSignatureGenerator>
{};
} // namespace

static uint64_t feature_requirement_mask(const dxil::ShaderHeader &header)
{
  return uint64_t(header.deviceRequirement.shaderFeatureFlagsLow) | (uint64_t(header.deviceRequirement.shaderFeatureFlagsHigh) << 32u);
}

bool rga::pso::write_dx12_root_signature(const char *fn, const UnpackedShader &vs, const UnpackedShader &ps, eastl::string &out_error)
{
  const StageBlob *vsBlob = find_stage(vs, Stage::VS);
  const StageBlob *psBlob = find_stage(ps, Stage::PS); // null for depth-only pipelines
  const StageBlob *gsBlob = find_stage(vs, Stage::GS);
  const StageBlob *hsBlob = find_stage(vs, Stage::HS);
  const StageBlob *dsBlob = find_stage(vs, Stage::DS);
  if (!vsBlob || !vsBlob->dxilHeader || (psBlob && !psBlob->dxilHeader))
    return set_error(out_error, "dx12 root signature requires a VS stage with a dxil header");

  uint64_t featureFlags = feature_requirement_mask(*vsBlob->dxilHeader);
  for (const StageBlob *blob : {psBlob, gsBlob, hsBlob, dsBlob})
    if (blob && blob->dxilHeader)
      featureFlags |= feature_requirement_mask(*blob->dxilHeader);

  const dxil::ShaderResourceUsageTable emptyTable{};
  dxil::GraphicsRootSignatureExtraProperties properties = {
    .hasVertexInputs = vsBlob->dxilHeader->inOutSemanticMask != 0,
    .useResourceDescriptorHeapIndexing = 0 != (dxil::SHADER_REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING & featureFlags),
    .useSamplerDescriptorHeapIndexing = 0 != (dxil::SHADER_REQUIRES_SAMPLER_DESCRIPTOR_HEAP_INDEXING & featureFlags),
  };

  RgaGraphicsRootSignatureGenerator generator;
  dxil::decode_graphics_root_signature(properties, vsBlob->dxilHeader->resourceUsageTable,
    psBlob ? psBlob->dxilHeader->resourceUsageTable : emptyTable,                                   // -V522
    hsBlob && hsBlob->dxilHeader ? hsBlob->dxilHeader->resourceUsageTable : emptyTable,             // -V522
    dsBlob && dsBlob->dxilHeader ? dsBlob->dxilHeader->resourceUsageTable : emptyTable,             // -V522
    gsBlob && gsBlob->dxilHeader ? gsBlob->dxilHeader->resourceUsageTable : emptyTable, generator); // -V522

  HMODULE d3d12Module = LoadLibraryA("d3d12.dll");
  auto serialize =
    d3d12Module ? (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)GetProcAddress(d3d12Module, "D3D12SerializeRootSignature") : nullptr;
  if (!serialize)
    return set_error(out_error, "can't load D3D12SerializeRootSignature from d3d12.dll");

  ID3DBlob *rootSignBlob = nullptr;
  ID3DBlob *errorBlob = nullptr;
  HRESULT hr = serialize(&generator.desc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignBlob, &errorBlob);
  if (FAILED(hr) || !rootSignBlob)
  {
    out_error = eastl::string(eastl::string::CtorSprintf{}, "D3D12SerializeRootSignature failed: %s",
      errorBlob ? (const char *)errorBlob->GetBufferPointer() : "unknown error");
    if (errorBlob)
      errorBlob->Release();
    return false;
  }

  bool ok = write_file(fn, rootSignBlob->GetBufferPointer(), rootSignBlob->GetBufferSize(), out_error);
  rootSignBlob->Release();
  if (errorBlob)
    errorBlob->Release();
  return ok;
}

#else

bool rga::pso::write_dx12_root_signature(const char *, const UnpackedShader &, const UnpackedShader &, eastl::string &out_error)
{
  return set_error(out_error, "dx12 root signature serialization is only supported on windows");
}

#endif

// ---------------------------------------------------------------------------
// Vulkan
// ---------------------------------------------------------------------------

// Schema constants of the RGA vulkan pipeline state serializer (rg_pso_serializer_vulkan.cpp):
// version 3 == VERSION_1_2, booleans are "true"/"false" strings, handles are hex strings.
static constexpr int RGA_VULKAN_PSO_VERSION = 3;

using JAlloc = rapidjson::Document::AllocatorType;
using JValue = rapidjson::Value;

static void add_bool(JValue &obj, const char *key, bool v, JAlloc &alloc)
{
  obj.AddMember(rapidjson::StringRef(key), JValue(v ? "true" : "false", alloc), alloc);
}
static void add_u(JValue &obj, const char *key, uint32_t v, JAlloc &alloc)
{
  obj.AddMember(rapidjson::StringRef(key), JValue(v), alloc);
}
static void add_i(JValue &obj, const char *key, int32_t v, JAlloc &alloc)
{
  obj.AddMember(rapidjson::StringRef(key), JValue(v), alloc);
}
static void add_f(JValue &obj, const char *key, float v, JAlloc &alloc)
{
  obj.AddMember(rapidjson::StringRef(key), JValue(double(v)), alloc);
}
static void add_str(JValue &obj, const char *key, const char *v, JAlloc &alloc)
{
  obj.AddMember(rapidjson::StringRef(key), JValue(v, alloc), alloc);
}
static void add_handle(JValue &obj, const char *key, uint64_t v, JAlloc &alloc)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "0x%016llX", (unsigned long long)v);
  obj.AddMember(rapidjson::StringRef(key), JValue(buf, alloc), alloc);
}
static JValue make_struct(uint32_t s_type, JAlloc &alloc)
{
  JValue obj(rapidjson::kObjectType);
  add_u(obj, "sType", s_type, alloc);
  add_handle(obj, "pNext", 0, alloc);
  add_u(obj, "flags", 0, alloc);
  return obj;
}

// descriptor set indexes decorated in the module; tells apart the bindless layout
// (per stage register sets shifted by spirv::bindless::MAX_SETS) from the plain one
static uint32_t spirv_descriptor_set_mask(dag::ConstSpan<uint8_t> bytecode)
{
  constexpr uint32_t SPIRV_MAGIC = 0x07230203;
  constexpr uint16_t OP_DECORATE = 71;
  constexpr uint32_t DECORATION_DESCRIPTOR_SET = 34;
  const uint32_t *words = (const uint32_t *)bytecode.data();
  const size_t wordCount = bytecode.size() / 4;
  if (wordCount < 5 || words[0] != SPIRV_MAGIC)
    return 0;
  uint32_t mask = 0;
  for (size_t i = 5; i < wordCount;)
  {
    const uint32_t lengthAndOpcode = words[i];
    const uint32_t length = lengthAndOpcode >> 16;
    if (!length || i + length > wordCount)
      break;
    if ((lengthAndOpcode & 0xFFFF) == OP_DECORATE && length >= 4 && words[i + 2] == DECORATION_DESCRIPTOR_SET && words[i + 3] < 32)
      mask |= 1u << words[i + 3];
    i += length;
  }
  return mask;
}

namespace
{
struct VulkanStageEntry
{
  const StageBlob *blob = nullptr;
  uint32_t registersSetIndex = 0; // spirv::graphics::*::REGISTERS_SET_INDEX
  VkShaderStageFlagBits stageBit = VkShaderStageFlagBits(0);
};
} // namespace

static JValue make_vk_stage(const VulkanStageEntry &stage, JAlloc &alloc)
{
  JValue obj = make_struct(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, alloc);
  add_u(obj, "stage", stage.stageBit, alloc);
  add_handle(obj, "module", 0, alloc);
  add_str(obj, "name", "main", alloc);
  return obj;
}

static JValue make_vk_vertex_input(uint32_t input_mask, JAlloc &alloc)
{
  JValue obj = make_struct(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, alloc);
  uint32_t attribCount = 0;
  for (uint32_t loc = 0; loc < 32; ++loc)
    if (input_mask & (1u << loc))
      ++attribCount;
  add_u(obj, "vertexBindingDescriptionCount", attribCount ? 1 : 0, alloc);
  add_u(obj, "vertexAttributeDescriptionCount", attribCount, alloc);
  if (!attribCount)
    return obj;

  // the dump has no vertex declaration for the shader, declare a generic float4 per input location
  JValue bindings(rapidjson::kArrayType);
  JValue binding(rapidjson::kObjectType);
  add_u(binding, "binding", 0, alloc);
  add_u(binding, "stride", attribCount * 16, alloc);
  add_u(binding, "inputRate", VK_VERTEX_INPUT_RATE_VERTEX, alloc);
  bindings.PushBack(binding, alloc);
  obj.AddMember("pVertexBindingDescriptions", bindings, alloc);

  JValue attribs(rapidjson::kArrayType);
  uint32_t offset = 0;
  for (uint32_t loc = 0; loc < 32; ++loc)
  {
    if (!(input_mask & (1u << loc)))
      continue;
    JValue attrib(rapidjson::kObjectType);
    add_u(attrib, "location", loc, alloc);
    add_u(attrib, "binding", 0, alloc);
    add_u(attrib, "format", VK_FORMAT_R32G32B32A32_SFLOAT, alloc);
    add_u(attrib, "offset", offset, alloc);
    attribs.PushBack(attrib, alloc);
    offset += 16;
  }
  obj.AddMember("pVertexAttributeDescriptions", attribs, alloc);
  return obj;
}

static JValue make_vk_viewport_state(JAlloc &alloc)
{
  JValue obj = make_struct(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, alloc);
  add_u(obj, "viewportCount", 1, alloc);
  add_u(obj, "scissorCount", 1, alloc);
  JValue viewports(rapidjson::kArrayType);
  JValue viewport(rapidjson::kObjectType);
  add_f(viewport, "x", 0.f, alloc);
  add_f(viewport, "y", 0.f, alloc);
  add_f(viewport, "width", 1920.f, alloc);
  add_f(viewport, "height", 1080.f, alloc);
  add_f(viewport, "minDepth", 0.f, alloc);
  add_f(viewport, "maxDepth", 1.f, alloc);
  viewports.PushBack(viewport, alloc);
  obj.AddMember("pViewports", viewports, alloc);
  JValue scissors(rapidjson::kArrayType);
  JValue scissor(rapidjson::kObjectType);
  JValue scissorOffset(rapidjson::kObjectType);
  add_i(scissorOffset, "x", 0, alloc);
  add_i(scissorOffset, "y", 0, alloc);
  scissor.AddMember("offset", scissorOffset, alloc);
  JValue scissorExtent(rapidjson::kObjectType);
  add_u(scissorExtent, "width", 1920, alloc);
  add_u(scissorExtent, "height", 1080, alloc);
  scissor.AddMember("extent", scissorExtent, alloc);
  scissors.PushBack(scissor, alloc);
  obj.AddMember("pScissors", scissors, alloc);
  return obj;
}

static JValue make_vk_rasterization(const shaders::RenderState &rs, JAlloc &alloc)
{
  JValue obj = make_struct(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, alloc);
  add_bool(obj, "depthClampEnable", !rs.zClip, alloc);
  add_bool(obj, "rasterizerDiscardEnable", false, alloc);
  add_u(obj, "polygonMode", VK_POLYGON_MODE_FILL, alloc);
  add_u(obj, "cullMode", rs.cull == CULL_NONE ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT, alloc);
  add_u(obj, "frontFace", rs.cull == CULL_CCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE, alloc);
  add_bool(obj, "depthBiasEnable", rs.zBias != 0.f || rs.slopeZBias != 0.f, alloc);
  add_f(obj, "depthBiasConstantFactor", rs.zBias, alloc);
  add_f(obj, "depthBiasClamp", 0.f, alloc);
  add_f(obj, "depthBiasSlopeFactor", rs.slopeZBias, alloc);
  add_f(obj, "lineWidth", 1.f, alloc);
  return obj;
}

static JValue make_vk_multisample(const shaders::RenderState &rs, JAlloc &alloc)
{
  JValue obj = make_struct(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, alloc);
  add_u(obj, "rasterizationSamples", VK_SAMPLE_COUNT_1_BIT, alloc);
  add_bool(obj, "sampleShadingEnable", false, alloc);
  add_f(obj, "minSampleShading", 0.f, alloc);
  add_bool(obj, "alphaToCoverageEnable", rs.alphaToCoverage, alloc);
  add_bool(obj, "alphaToOneEnable", false, alloc);
  return obj;
}

static JValue make_vk_stencil_op_state(const shaders::RenderState &rs, JAlloc &alloc)
{
  const bool enabled = rs.stencil.func > 0;
  JValue obj(rapidjson::kObjectType);
  add_u(obj, "failOp", enabled ? drv3d_vulkan::translate_stencil_op_to_vulkan(rs.stencil.fail) : VK_STENCIL_OP_KEEP, alloc);
  add_u(obj, "passOp", enabled ? drv3d_vulkan::translate_stencil_op_to_vulkan(rs.stencil.pass) : VK_STENCIL_OP_KEEP, alloc);
  add_u(obj, "depthFailOp", enabled ? drv3d_vulkan::translate_stencil_op_to_vulkan(rs.stencil.zFail) : VK_STENCIL_OP_KEEP, alloc);
  add_u(obj, "compareOp", enabled ? drv3d_vulkan::translate_compare_func_to_vulkan(rs.stencil.func) : VK_COMPARE_OP_ALWAYS, alloc);
  add_u(obj, "compareMask", enabled ? rs.stencil.readMask : 0, alloc);
  add_u(obj, "writeMask", enabled ? rs.stencil.writeMask : 0, alloc);
  add_u(obj, "reference", enabled ? rs.stencilRef : 0, alloc);
  return obj;
}

static JValue make_vk_depth_stencil(const shaders::RenderState &rs, JAlloc &alloc)
{
  JValue obj = make_struct(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, alloc);
  add_bool(obj, "depthTestEnable", rs.ztest, alloc);
  add_bool(obj, "depthWriteEnable", rs.zwrite, alloc);
  add_u(obj, "depthCompareOp", rs.ztest ? drv3d_vulkan::translate_compare_func_to_vulkan(rs.zFunc) : VK_COMPARE_OP_ALWAYS, alloc);
  add_bool(obj, "depthBoundsTestEnable", rs.depthBoundsEnable, alloc);
  add_bool(obj, "stencilTestEnable", rs.stencil.func > 0, alloc);
  JValue front = make_vk_stencil_op_state(rs, alloc);
  obj.AddMember("front", front, alloc);
  JValue back = make_vk_stencil_op_state(rs, alloc);
  obj.AddMember("back", back, alloc);
  add_f(obj, "minDepthBounds", 0.f, alloc);
  add_f(obj, "maxDepthBounds", 1.f, alloc);
  return obj;
}

static JValue make_vk_blend_attachment(const shaders::RenderState &rs, uint32_t rt_index, JAlloc &alloc)
{
  JValue obj(rapidjson::kObjectType);
  uint32_t srcRgb = 0, dstRgb = 0, srcA = 0, dstA = 0, opRgb = BLENDOP_ADD, opA = BLENDOP_ADD;
  bool blend = false, sepablend = false;
  if (rs.dualSourceBlendEnabled)
  {
    const auto &params = rs.dualSourceBlend.params;
    blend = params.ablend;
    sepablend = params.sepablend;
    srcRgb = params.ablendFactors.src;
    dstRgb = params.ablendFactors.dst;
    srcA = params.sepablendFactors.src;
    dstA = params.sepablendFactors.dst;
    opRgb = params.blendOp;
    opA = params.sepablendOp;
  }
  else
  {
    const auto &params =
      rs.blendParams[rs.independentBlendEnabled ? eastl::min(rt_index, shaders::RenderState::NumIndependentBlendParameters - 1) : 0];
    blend = params.ablend;
    sepablend = params.sepablend;
    srcRgb = params.ablendFactors.src;
    dstRgb = params.ablendFactors.dst;
    srcA = params.sepablendFactors.src;
    dstA = params.sepablendFactors.dst;
    opRgb = params.blendOp;
    opA = params.sepablendOp;
  }

  add_bool(obj, "blendEnable", blend, alloc);
  add_u(obj, "srcColorBlendFactor", blend ? drv3d_vulkan::translate_rgb_blend_mode_to_vulkan(srcRgb) : VK_BLEND_FACTOR_ONE, alloc);
  add_u(obj, "dstColorBlendFactor", blend ? drv3d_vulkan::translate_rgb_blend_mode_to_vulkan(dstRgb) : VK_BLEND_FACTOR_ZERO, alloc);
  add_u(obj, "colorBlendOp", blend ? drv3d_vulkan::translate_blend_op_to_vulkan(opRgb) : VK_BLEND_OP_ADD, alloc);
  add_u(obj, "srcAlphaBlendFactor", sepablend ? drv3d_vulkan::translate_alpha_blend_mode_to_vulkan(srcA) : VK_BLEND_FACTOR_ONE, alloc);
  add_u(obj, "dstAlphaBlendFactor", sepablend ? drv3d_vulkan::translate_alpha_blend_mode_to_vulkan(dstA) : VK_BLEND_FACTOR_ZERO,
    alloc);
  add_u(obj, "alphaBlendOp", sepablend ? drv3d_vulkan::translate_blend_op_to_vulkan(opA) : VK_BLEND_OP_ADD, alloc);
  add_u(obj, "colorWriteMask", (rs.colorWr >> (rt_index * 4)) & 0xF, alloc);
  return obj;
}

static JValue make_vk_color_blend(const shaders::RenderState &rs, uint32_t color_count, JAlloc &alloc)
{
  JValue obj = make_struct(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, alloc);
  add_bool(obj, "logicOpEnable", false, alloc);
  add_u(obj, "logicOp", VK_LOGIC_OP_COPY, alloc);
  add_u(obj, "attachmentCount", color_count, alloc);
  if (color_count)
  {
    JValue attachments(rapidjson::kArrayType);
    for (uint32_t i = 0; i < color_count; ++i)
    {
      JValue attachment = make_vk_blend_attachment(rs, i, alloc);
      attachments.PushBack(attachment, alloc);
    }
    obj.AddMember("pAttachments", attachments, alloc);
  }
  JValue blendConstants(rapidjson::kArrayType);
  blendConstants.PushBack(JValue(rs.blendFactor.r / 255.0), alloc);
  blendConstants.PushBack(JValue(rs.blendFactor.g / 255.0), alloc);
  blendConstants.PushBack(JValue(rs.blendFactor.b / 255.0), alloc);
  blendConstants.PushBack(JValue(rs.blendFactor.a / 255.0), alloc);
  obj.AddMember("blendConstants", blendConstants, alloc);
  return obj;
}

static JValue make_vk_attachment(VkFormat format, JAlloc &alloc)
{
  const bool depth = format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
  JValue obj(rapidjson::kObjectType);
  add_u(obj, "flags", 0, alloc);
  add_u(obj, "format", format, alloc);
  add_u(obj, "samples", VK_SAMPLE_COUNT_1_BIT, alloc);
  add_u(obj, "loadOp", VK_ATTACHMENT_LOAD_OP_CLEAR, alloc);
  add_u(obj, "storeOp", VK_ATTACHMENT_STORE_OP_STORE, alloc);
  add_u(obj, "stencilLoadOp", VK_ATTACHMENT_LOAD_OP_DONT_CARE, alloc);
  add_u(obj, "stencilStoreOp", VK_ATTACHMENT_STORE_OP_DONT_CARE, alloc);
  add_u(obj, "initialLayout", depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    alloc);
  add_u(obj, "finalLayout", depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    alloc);
  return obj;
}

static JValue make_vk_render_pass(const shaders::RenderState &rs, uint32_t color_count, JAlloc &alloc)
{
  JValue obj = make_struct(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, alloc);
  const VkFormat depthFormat = rs.stencil.func > 0 ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_D32_SFLOAT;

  add_u(obj, "attachmentCount", color_count + 1, alloc);
  JValue attachments(rapidjson::kArrayType);
  for (uint32_t i = 0; i < color_count; ++i)
  {
    JValue attachment = make_vk_attachment(VK_FORMAT_R8G8B8A8_UNORM, alloc);
    attachments.PushBack(attachment, alloc);
  }
  JValue depthAttachment = make_vk_attachment(depthFormat, alloc);
  attachments.PushBack(depthAttachment, alloc);
  obj.AddMember("pAttachments", attachments, alloc);

  add_u(obj, "subpassCount", 1, alloc);
  JValue subpasses(rapidjson::kArrayType);
  JValue subpass(rapidjson::kObjectType);
  add_u(subpass, "flags", 0, alloc);
  add_u(subpass, "pipelineBindPoint", VK_PIPELINE_BIND_POINT_GRAPHICS, alloc);
  add_u(subpass, "inputAttachmentCount", 0, alloc);
  add_u(subpass, "colorAttachmentCount", color_count, alloc);
  if (color_count)
  {
    JValue colorRefs(rapidjson::kArrayType);
    for (uint32_t i = 0; i < color_count; ++i)
    {
      JValue ref(rapidjson::kObjectType);
      add_u(ref, "attachment", i, alloc);
      add_u(ref, "layout", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, alloc);
      colorRefs.PushBack(ref, alloc);
    }
    subpass.AddMember("pColorAttachments", colorRefs, alloc);
  }
  JValue depthRef(rapidjson::kObjectType);
  add_u(depthRef, "attachment", color_count, alloc);
  add_u(depthRef, "layout", VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, alloc);
  subpass.AddMember("pDepthStencilAttachment", depthRef, alloc);
  add_u(subpass, "preserveAttachmentCount", 0, alloc);
  subpasses.PushBack(subpass, alloc);
  obj.AddMember("pSubpasses", subpasses, alloc);

  add_u(obj, "dependencyCount", 0, alloc);
  return obj;
}

static JValue make_vk_descriptor_set_layout(const spirv::ShaderHeader *header, VkShaderStageFlags stage_bits, JAlloc &alloc)
{
  JValue obj = make_struct(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, alloc);
  const uint32_t bindingCount = header ? header->registerCount : 0;
  add_u(obj, "bindingCount", bindingCount, alloc);
  if (!bindingCount)
    return obj;
  // mirrors drv3d_vulkan descriptor_set.h: binding index == register index, one descriptor each
  JValue bindings(rapidjson::kArrayType);
  for (uint32_t i = 0; i < bindingCount; ++i)
  {
    JValue binding(rapidjson::kObjectType);
    add_u(binding, "binding", i, alloc);
    add_u(binding, "descriptorType", header->descriptorTypes[i].get(), alloc);
    add_u(binding, "descriptorCount", 1, alloc);
    add_u(binding, "stageFlags", stage_bits, alloc);
    bindings.PushBack(binding, alloc);
  }
  obj.AddMember("pBindings", bindings, alloc);
  return obj;
}

static JValue make_vk_bindless_set_layout(VkDescriptorType type, uint32_t count, JAlloc &alloc)
{
  JValue obj = make_struct(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, alloc);
  add_u(obj, "bindingCount", 1, alloc);
  JValue bindings(rapidjson::kArrayType);
  JValue binding(rapidjson::kObjectType);
  add_u(binding, "binding", 0, alloc);
  add_u(binding, "descriptorType", type, alloc);
  add_u(binding, "descriptorCount", count, alloc);
  add_u(binding, "stageFlags", VK_SHADER_STAGE_ALL, alloc);
  bindings.PushBack(binding, alloc);
  obj.AddMember("pBindings", bindings, alloc);
  return obj;
}

bool rga::pso::write_vulkan_gpso(const char *fn, const UnpackedShader &vs, const UnpackedShader &ps,
  const shaders::RenderState &render_state, eastl::string &out_error)
{
  const StageBlob *psBlob = find_stage(ps, Stage::PS); // null for depth-only pipelines
  if (!find_stage(vs, Stage::VS) || (psBlob && !psBlob->spirvHeader))
    return set_error(out_error, "vulkan gpso requires a VS stage with a spirv header");

  // pipeline stages in the driver register set order; shader_blob reuses HS/DS tags for tesc/tese
  VulkanStageEntry stages[spirv::graphics::MAX_SETS] = {};
  const eastl::pair<Stage, VkShaderStageFlagBits> stageMapping[] = {
    {Stage::VS, VK_SHADER_STAGE_VERTEX_BIT},
    {Stage::PS, VK_SHADER_STAGE_FRAGMENT_BIT},
    {Stage::GS, VK_SHADER_STAGE_GEOMETRY_BIT},
    {Stage::HS, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
    {Stage::DS, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
  };
  uint32_t maxRegistersSetIndex = 0;
  bool bindless = false;
  for (uint32_t i = 0; i < countof(stageMapping); ++i)
  {
    const StageBlob *blob = find_stage(stageMapping[i].first == Stage::PS ? ps : vs, stageMapping[i].first);
    if (!blob)
      continue;
    if (!blob->spirvHeader)
      return set_error(out_error, "vulkan gpso: stage without spirv header");
    stages[i] = {blob, i, stageMapping[i].second};
    maxRegistersSetIndex = eastl::max(maxRegistersSetIndex, i);
    // bindless dumps shift every register set by spirv::bindless::MAX_SETS, even in shaders
    // that use no bindless resource, so the layout mode is detected from the set decorations
    if (
      blob->spirvHeader->bindlessSetsUsed > 0 || (spirv_descriptor_set_mask(blob->bytecode) & (1u << (i + spirv::bindless::MAX_SETS))))
      bindless = true;
  }
  uint32_t colorCount = 0;
  if (psBlob)
    for (uint32_t loc = 0; loc < spirv::platform::MAX_COLOR_ATTACHMENTS; ++loc)
      if (psBlob->spirvHeader->outputMask & (1u << loc)) // -V522
        colorCount = loc + 1;

  rapidjson::Document doc(rapidjson::kObjectType);
  JAlloc &alloc = doc.GetAllocator();
  doc.AddMember("version", RGA_VULKAN_PSO_VERSION, alloc);

  JValue pipeline = make_struct(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, alloc);
  JValue stageArray(rapidjson::kArrayType);
  uint32_t stageCount = 0;
  for (const VulkanStageEntry &stage : stages)
    if (stage.blob)
    {
      JValue stageValue = make_vk_stage(stage, alloc);
      stageArray.PushBack(stageValue, alloc);
      ++stageCount;
    }
  add_u(pipeline, "stageCount", stageCount, alloc);
  pipeline.AddMember("pStages", stageArray, alloc);

  const bool hasTess = stages[spirv::graphics::control::REGISTERS_SET_INDEX].blob != nullptr;

  JValue vertexInput = make_vk_vertex_input(stages[spirv::graphics::vertex::REGISTERS_SET_INDEX].blob->spirvHeader->inputMask, alloc);
  pipeline.AddMember("pVertexInputState", vertexInput, alloc);
  JValue inputAssembly = make_struct(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, alloc);
  add_u(inputAssembly, "topology", hasTess ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, alloc);
  add_bool(inputAssembly, "primitiveRestartEnable", false, alloc);
  pipeline.AddMember("pInputAssemblyState", inputAssembly, alloc);
  if (hasTess)
  {
    JValue tess = make_struct(VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO, alloc);
    add_u(tess, "patchControlPoints", 4, alloc);
    pipeline.AddMember("pTessellationState", tess, alloc);
  }
  JValue viewportState = make_vk_viewport_state(alloc);
  pipeline.AddMember("pViewportState", viewportState, alloc);
  JValue rasterization = make_vk_rasterization(render_state, alloc);
  pipeline.AddMember("pRasterizationState", rasterization, alloc);
  JValue multisample = make_vk_multisample(render_state, alloc);
  pipeline.AddMember("pMultisampleState", multisample, alloc);
  JValue depthStencil = make_vk_depth_stencil(render_state, alloc);
  pipeline.AddMember("pDepthStencilState", depthStencil, alloc);
  JValue colorBlend = make_vk_color_blend(render_state, colorCount, alloc);
  pipeline.AddMember("pColorBlendState", colorBlend, alloc);
  add_u(pipeline, "subpass", 0, alloc);
  add_i(pipeline, "basePipelineIndex", -1, alloc);
  doc.AddMember("VkGraphicsPipelineCreateInfo", pipeline, alloc);

  JValue renderPass = make_vk_render_pass(render_state, colorCount, alloc);
  doc.AddMember("VkRenderPassCreateInfo", renderPass, alloc);

  // descriptor set layouts, array index == set number: bindless sets first (when the dump is
  // compiled for bindless), then one register set per stage in driver set order
  JValue setLayouts(rapidjson::kArrayType);
  uint32_t setLayoutCount = 0;
  if (bindless)
  {
    // slot counts are compile-only stand-ins for the runtime configured limits, small enough
    // to not need the update-after-bind layout flags the RGA pso schema can not express
    static const uint32_t bindlessSlotCounts[spirv::bindless::MAX_SETS] = {1024, 64, 1024};
    for (uint32_t i = 0; i < spirv::bindless::MAX_SETS; ++i)
    {
      JValue setLayout = make_vk_bindless_set_layout(spirv::bindless::SET_DESCRIPTOR_TYPES[i], bindlessSlotCounts[i], alloc);
      setLayouts.PushBack(setLayout, alloc);
    }
    setLayoutCount += spirv::bindless::MAX_SETS;
  }
  for (uint32_t i = 0; i <= maxRegistersSetIndex; ++i)
  {
    JValue setLayout =
      make_vk_descriptor_set_layout(stages[i].blob ? stages[i].blob->spirvHeader : nullptr, stages[i].stageBit, alloc);
    setLayouts.PushBack(setLayout, alloc);
    ++setLayoutCount;
  }

  JValue pipelineLayout = make_struct(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, alloc);
  add_u(pipelineLayout, "setLayoutCount", setLayoutCount, alloc);
  JValue setLayoutRefs(rapidjson::kArrayType);
  for (uint32_t i = 0; i < setLayoutCount; ++i)
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08X", i);
    setLayoutRefs.PushBack(JValue(buf, alloc), alloc);
  }
  pipelineLayout.AddMember("pSetLayouts", setLayoutRefs, alloc);

  // immediate consts become push constants: VS range at offset 0, FS range after the
  // reserved VS words; extra stages reuse the VS range like the driver does
  JValue pushConstantRanges(rapidjson::kArrayType);
  uint32_t pushConstantRangeCount = 0;
  using spirv::MAX_IMMEDIATE_CONST_WORDS;
  const spirv::ShaderHeader &vsHeader = *stages[spirv::graphics::vertex::REGISTERS_SET_INDEX].blob->spirvHeader;
  uint32_t vsRangeStages = 0, vsRangeSize = vsHeader.pushConstantsCount * 4;
  for (const VulkanStageEntry &stage : stages)
    if (stage.blob && stage.stageBit != VK_SHADER_STAGE_VERTEX_BIT && stage.stageBit != VK_SHADER_STAGE_FRAGMENT_BIT &&
        stage.blob->spirvHeader->pushConstantsCount)
    {
      vsRangeStages |= stage.stageBit;
      vsRangeSize = MAX_IMMEDIATE_CONST_WORDS * 4;
    }
  if (vsRangeSize)
  {
    JValue range(rapidjson::kObjectType);
    add_u(range, "stageFlags", VK_SHADER_STAGE_VERTEX_BIT | vsRangeStages, alloc);
    add_u(range, "offset", 0, alloc);
    add_u(range, "size", vsRangeSize, alloc);
    pushConstantRanges.PushBack(range, alloc);
    ++pushConstantRangeCount;
  }
  if (psBlob && psBlob->spirvHeader->pushConstantsCount)
  {
    JValue range(rapidjson::kObjectType);
    add_u(range, "stageFlags", VK_SHADER_STAGE_FRAGMENT_BIT, alloc);
    add_u(range, "offset", MAX_IMMEDIATE_CONST_WORDS * 4, alloc);
    add_u(range, "size", psBlob->spirvHeader->pushConstantsCount * 4, alloc);
    pushConstantRanges.PushBack(range, alloc);
    ++pushConstantRangeCount;
  }
  add_u(pipelineLayout, "pushConstantRangeCount", pushConstantRangeCount, alloc);
  if (pushConstantRangeCount)
    pipelineLayout.AddMember("pPushConstantRanges", pushConstantRanges, alloc);
  doc.AddMember("VkPipelineLayoutCreateInfo", pipelineLayout, alloc);
  doc.AddMember("VkDescriptorSetLayoutCreateInfo", setLayouts, alloc);

  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  return write_file(fn, buffer.GetString(), buffer.GetSize(), out_error);
}
