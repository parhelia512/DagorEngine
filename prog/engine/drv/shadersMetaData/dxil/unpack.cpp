// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <drv/shadersMetaData/dxil/unpack.h>
#include <debug/dag_debug.h>

static bool set_error(eastl::string *out_error, const char *msg)
{
  if (out_error)
    *out_error = msg;
  return false;
}

static dxil::StageModuleRef layout_to_module(const bindump::Mapper<dxil::Shader> &layout)
{
  dxil::StageModuleRef result;
  result.header = layout.shaderHeader;
  result.bytecodeOffset = layout.bytecodeOffset;
  result.bytecodeSize = layout.bytecodeSize;
  return result;
}

bool dxil::decode_metadata(dag::ConstSpan<uint8_t> metadata, bool expect_vertex_pipeline, DecodedShaderRef &out,
  eastl::string *out_error)
{
  out = {};
  if (metadata.size() < 4)
    return set_error(out_error, "shader blob is too small");

  [[maybe_unused]] const uint32_t ident = *reinterpret_cast<const uint32_t *>(metadata.data());

  auto *container = bindump::map<ShaderContainer>(metadata.data());
  if (!container)
  {
    if (out_error)
      out_error->sprintf("unexpected shader identifier 0x%08X", ident);
    return false;
  }

  dag::ConstSpan<uint8_t> containerData = container->data;
  const bindump::Mapper<ShaderWithStreamOutput> *programWithSo = nullptr;
  if (container->type.hasStreamOutput)
  {
    programWithSo = bindump::map<ShaderWithStreamOutput>(containerData.data());
    containerData = programWithSo->data;
  }

  if (container->type.shaderType == StoredShaderType::combinedVertexShader)
  {
    if (!expect_vertex_pipeline)
      return set_error(out_error, "combined shader container in a non vertex entry point");
    auto *combined = bindump::map<VertexShaderPipeline>(containerData.data());
    if (!combined)
      return set_error(out_error, "couldn't map to dxil::VertexShaderPipeline");
    out.main = layout_to_module(*combined->vertexShader);
    if (combined->geometryShader)
      out.gsOrAs = layout_to_module(*combined->geometryShader);
    if (combined->hullShader)
      out.hs = layout_to_module(*combined->hullShader);
    if (combined->domainShader)
      out.ds = layout_to_module(*combined->domainShader);
  }
  else if (container->type.shaderType == StoredShaderType::meshShader)
  {
    if (!expect_vertex_pipeline)
      return set_error(out_error, "mesh shader container in a non vertex entry point");
    auto *combined = bindump::map<MeshShaderPipeline>(containerData.data());
    if (!combined)
      return set_error(out_error, "couldn't map to dxil::MeshShaderPipeline");
    out.main = layout_to_module(*combined->meshShader);
    if (combined->amplificationShader)
      out.gsOrAs = layout_to_module(*combined->amplificationShader);
  }
  else
  {
    auto *shader = bindump::map<Shader>(containerData.data());
    if (!shader)
      return set_error(out_error, "couldn't map to dxil::Shader");
    out.main = layout_to_module(*shader);
  }

  if (programWithSo)
    out.streamOutput = programWithSo->streamOutputComponents;
  out.main.hash = container->dataHash;
  out.main.hashedSize = containerData.size();

  out.isMesh = static_cast<ShaderStage>(out.main.header.shaderType) == ShaderStage::MESH;
  return true;
}
