//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include "compiled_shader_header.h"
#include <util/dag_globDef.h>
#include <EASTL/iterator.h>

namespace dxil
{

// D3D12_ROOT_SIGNATURE_FLAG_* and D3D_SHADER_REQUIRES_* values that are missing from
// pre-SM6.6 sdk headers
constexpr uint32_t ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED = 0x400;
constexpr uint32_t ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED = 0x800;
constexpr uint64_t SHADER_REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING = 0x02000000;
constexpr uint64_t SHADER_REQUIRES_SAMPLER_DESCRIPTOR_HEAP_INDEXING = 0x04000000;

template <size_t StageCount, typename D>
struct RootSignatureGeneratorBase
{
  static constexpr uint32_t per_stage_resource_limits = MAX_T_REGISTERS + MAX_S_REGISTERS + MAX_U_REGISTERS + MAX_B_REGISTERS;
  // cbuffer each one param, then srv, uav and sampler each one
  static constexpr uint32_t per_stage_resource_group_limits = MAX_B_REGISTERS + 1 + 1 + 1;
  static constexpr uint32_t bindles_stage_descriptors = 2;

  D3D12_DESCRIPTOR_RANGE ranges[per_stage_resource_limits * StageCount + bindles_stage_descriptors] = {};
  D3D12_ROOT_PARAMETER params[per_stage_resource_group_limits * StageCount + bindles_stage_descriptors] = {};
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  D3D12_DESCRIPTOR_RANGE *rangePosition = &ranges[0];
  D3D12_ROOT_PARAMETER *unboundedSamplersRootParam = nullptr;
  D3D12_ROOT_PARAMETER *bindlessSRVRootParam = nullptr;
  uint32_t rangeSize = 0;
  uint32_t signatureCost = 0;
  D3D12_SHADER_VISIBILITY currentVisibility = D3D12_SHADER_VISIBILITY_ALL;
  uint32_t vendorExtensionLocation = 0;
  bool useConstantBufferRootDescriptors = true;

  D &self() { return *static_cast<D *>(this); }
  bool shouldUseConstantBufferRootDescriptors() const { return useConstantBufferRootDescriptors; }

  // hooks reporting the root param index each resource group was placed at; a derived class
  // implements them when it has to remember the signature layout for later binding
  void onRootConstantsParamIndex(uint32_t) {}
  void onConstantBuffersParamIndex(uint32_t, bool) {}
  void onSamplersParamIndex(uint32_t) {}
  void onShaderResourceViewsParamIndex(uint32_t) {}
  void onUnorderedAccessViewsParamIndex(uint32_t) {}
  void onBindlessSamplersParamIndex(uint32_t) {}
  void onBindlessShaderResourceViewsParamIndex(uint32_t) {}

  void begin()
  {
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers = nullptr;
    desc.pParameters = params;
  }
  void end()
  {
#if _TARGET_PC_WIN
    if (vendorExtensionLocation)
    {
      rangePosition->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
      rangePosition->NumDescriptors = 1;
      rangePosition->BaseShaderRegister = 0;
      rangePosition->RegisterSpace = vendorExtensionLocation;
      rangePosition->OffsetInDescriptorsFromTableStart = 0;

      auto &pTarget = params[desc.NumParameters++];
      pTarget.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      pTarget.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      pTarget.DescriptorTable.NumDescriptorRanges = 1;
      pTarget.DescriptorTable.pDescriptorRanges = rangePosition;

      ++rangePosition;
      G_ASSERT(rangePosition <= eastl::end(ranges));

      signatureCost += 1; // offset into active descriptor heap
    }
#endif
  }
  // platform specific flag on the driver side
  void hasAccelerationStructure() {}
  void noPixelShaderResources() { desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS; }
  void hasStreamOutput() { desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT; }
  void setVisibilityPixelShader() { currentVisibility = D3D12_SHADER_VISIBILITY_PIXEL; }
  void addRootParameterConstantExplicit(uint32_t space, uint32_t index, uint32_t dwords, D3D12_SHADER_VISIBILITY vis)
  {
    G_ASSERT(desc.NumParameters < countof(params));

    auto &target = params[desc.NumParameters++];
    target.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    target.ShaderVisibility = vis;
    target.Constants.ShaderRegister = index;
    target.Constants.RegisterSpace = space;
    target.Constants.Num32BitValues = dwords;

    signatureCost += dwords;
  }
  void addRootParameterConstant(uint32_t space, uint32_t index, uint32_t dwords)
  {
    addRootParameterConstantExplicit(space, index, dwords, currentVisibility);
  }
  void rootConstantBuffer(uint32_t space, uint32_t index, uint32_t dwords)
  {
    self().onRootConstantsParamIndex(desc.NumParameters);
    addRootParameterConstant(space, index, dwords);
  }
  void specialConstants(uint32_t space, uint32_t index)
  {
    addRootParameterConstantExplicit(space, index, 1, D3D12_SHADER_VISIBILITY_ALL);
  }
  void nvidiaExtension(uint32_t space, uint32_t index)
  {
    G_UNUSED(index);
    vendorExtensionLocation = space;
  }
  void amdExtension(uint32_t space, uint32_t index)
  {
    G_UNUSED(index);
    vendorExtensionLocation = space;
  }
  void useResourceDescriptorHeapIndexing()
  {
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAGS(desc.Flags | ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
  }
  void useSamplerDescriptorHeapIndexing()
  {
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAGS(desc.Flags | ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);
  }
  void beginConstantBuffers()
  {
    rangeSize = 0;
    self().onConstantBuffersParamIndex(desc.NumParameters, shouldUseConstantBufferRootDescriptors());
  }
  void endConstantBuffers()
  {
    if (!shouldUseConstantBufferRootDescriptors())
    {
      G_ASSERT(desc.NumParameters < countof(params));
      G_ASSERT(rangeSize > 0);

      auto &target = params[desc.NumParameters++];
      target.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      target.ShaderVisibility = currentVisibility;
      target.DescriptorTable.NumDescriptorRanges = rangeSize;
      target.DescriptorTable.pDescriptorRanges = rangePosition;

      rangePosition += rangeSize;
      rangeSize = 0;
      G_ASSERT(rangePosition <= eastl::end(ranges));

      signatureCost += 1; // offset into active descriptor heap
    }
  }
  void constantBuffer(uint32_t space, uint32_t slot, uint32_t linear_index)
  {
    if (shouldUseConstantBufferRootDescriptors())
    {
      G_UNUSED(linear_index);
      G_ASSERT(desc.NumParameters < countof(params));

      auto &target = params[desc.NumParameters++];
      target.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      target.ShaderVisibility = currentVisibility;
      target.Descriptor.ShaderRegister = slot;
      target.Descriptor.RegisterSpace = space;

      signatureCost += 2; // cbuffer is a 64bit gpu address
      ++rangeSize;
    }
    else
    {
      auto &target = rangePosition[rangeSize++];
      G_ASSERT(&target < eastl::end(ranges));
      target.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
      target.NumDescriptors = 1;
      target.BaseShaderRegister = slot;
      target.RegisterSpace = space;
      target.OffsetInDescriptorsFromTableStart = linear_index;
    }
  }
  void beginSamplers()
  {
    rangeSize = 0;
    self().onSamplersParamIndex(desc.NumParameters);
  }
  void endSamplers()
  {
    G_ASSERT(desc.NumParameters < countof(params));
    G_ASSERT(rangeSize > 0);

    auto &target = params[desc.NumParameters++];
    target.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    target.ShaderVisibility = currentVisibility;
    target.DescriptorTable.NumDescriptorRanges = rangeSize;
    target.DescriptorTable.pDescriptorRanges = rangePosition;

    rangePosition += rangeSize;
    G_ASSERT(rangePosition <= eastl::end(ranges));

    signatureCost += 1; // offset into active descriptor heap
  }
  void sampler(uint32_t space, uint32_t slot, uint32_t linear_index)
  {
    auto &target = rangePosition[rangeSize++];
    G_ASSERT(&target < eastl::end(ranges));
    target.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    target.NumDescriptors = 1;
    target.BaseShaderRegister = slot;
    target.RegisterSpace = space;
    target.OffsetInDescriptorsFromTableStart = linear_index;
  }
  void beginBindlessSamplers()
  {
    if (unboundedSamplersRootParam == nullptr)
    {
      G_ASSERT(desc.NumParameters < countof(params));
      self().onBindlessSamplersParamIndex(desc.NumParameters);
      unboundedSamplersRootParam = &params[desc.NumParameters++];
      unboundedSamplersRootParam->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      unboundedSamplersRootParam->DescriptorTable.pDescriptorRanges = rangePosition;
      unboundedSamplersRootParam->ShaderVisibility = currentVisibility;
      signatureCost += 1; // 1 root param for all unbounded sampler array ranges
    }
    // Note: - We can't "OR" ShaderVisibility flags together, so if we need this for more than one stage, just use
    //         D3D12_SHADER_VISIBILITY_ALL
    else
    {
      G_ASSERTF(unboundedSamplersRootParam->ShaderVisibility != currentVisibility,
        "beginBindlessSamplers() shouldn't be called with the same visibility more than once");
      unboundedSamplersRootParam->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
  }
  void endBindlessSamplers() { G_ASSERT(unboundedSamplersRootParam->DescriptorTable.NumDescriptorRanges != 0); }
  void bindlessSamplers(uint32_t space, uint32_t slot)
  {
    G_ASSERT(space < MAX_UNBOUNDED_REGISTER_SPACES);

    // Deduplicate ranges
    for (uint32_t i = 0; i < unboundedSamplersRootParam->DescriptorTable.NumDescriptorRanges; i++)
    {
      auto &range = unboundedSamplersRootParam->DescriptorTable.pDescriptorRanges[i];
      if (slot == range.BaseShaderRegister && space == range.RegisterSpace)
        return;
    }

    auto &smpRange = rangePosition[0];
    G_ASSERT(&smpRange < eastl::end(ranges));
    smpRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    smpRange.NumDescriptors = UINT_MAX; // UINT_MAX means unbounded
    smpRange.BaseShaderRegister = slot;
    smpRange.RegisterSpace = space;
    smpRange.OffsetInDescriptorsFromTableStart = 0;

    unboundedSamplersRootParam->DescriptorTable.NumDescriptorRanges++;
    rangePosition++;
    G_ASSERT(rangePosition <= eastl::end(ranges));
  }
  void beginShaderResourceViews()
  {
    rangeSize = 0;
    self().onShaderResourceViewsParamIndex(desc.NumParameters);
  }
  void endShaderResourceViews()
  {
    G_ASSERT(desc.NumParameters < countof(params));
    G_ASSERT(rangeSize > 0);

    auto &target = params[desc.NumParameters++];
    target.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    target.ShaderVisibility = currentVisibility;
    target.DescriptorTable.NumDescriptorRanges = rangeSize;
    target.DescriptorTable.pDescriptorRanges = rangePosition;

    rangePosition += rangeSize;
    G_ASSERT(rangePosition <= eastl::end(ranges));

    signatureCost += 1; // offset into active descriptor heap
  }
  void shaderResourceView(uint32_t space, uint32_t slot, uint32_t descriptor_count, uint32_t linear_index)
  {
    auto &target = rangePosition[rangeSize++];
    G_ASSERT(&target < eastl::end(ranges));
    target.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    target.NumDescriptors = descriptor_count;
    target.BaseShaderRegister = slot;
    target.RegisterSpace = space;
    target.OffsetInDescriptorsFromTableStart = linear_index;
  }
  void beginBindlessShaderResourceViews()
  {
    if (bindlessSRVRootParam == nullptr)
    {
      G_ASSERT(desc.NumParameters < countof(params));
      self().onBindlessShaderResourceViewsParamIndex(desc.NumParameters);
      bindlessSRVRootParam = &params[desc.NumParameters++];
      bindlessSRVRootParam->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      bindlessSRVRootParam->DescriptorTable.pDescriptorRanges = rangePosition;
      bindlessSRVRootParam->ShaderVisibility = currentVisibility;
      signatureCost += 1; // 1 root param for all unbounded srv array ranges
    }
    // Note: - We can't "OR" ShaderVisibility flags together, so if we need this for more than one stage, just use
    //         D3D12_SHADER_VISIBILITY_ALL
    else
    {
      G_ASSERTF(bindlessSRVRootParam->ShaderVisibility != currentVisibility,
        "beginBindlessShaderResourceViews() shouldn't be called with the same visibility more than once");
      bindlessSRVRootParam->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
  }
  void endBindlessShaderResourceViews() {}
  void bindlessShaderResourceViews(uint32_t space, uint32_t slot)
  {
    // Deduplicate ranges
    for (uint32_t i = 0; i < bindlessSRVRootParam->DescriptorTable.NumDescriptorRanges; i++)
    {
      auto &range = bindlessSRVRootParam->DescriptorTable.pDescriptorRanges[i];
      if (slot == range.BaseShaderRegister && space == range.RegisterSpace)
        return;
    }

    auto &registerRange = rangePosition[0];
    G_ASSERT(&registerRange < eastl::end(ranges));
    registerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    registerRange.NumDescriptors = UINT_MAX; // UINT_MAX means unbounded
    registerRange.BaseShaderRegister = slot;
    registerRange.RegisterSpace = space;
    registerRange.OffsetInDescriptorsFromTableStart = 0;

    bindlessSRVRootParam->DescriptorTable.NumDescriptorRanges++;
    rangePosition++;
    G_ASSERT(rangePosition <= eastl::end(ranges));
  }
  void beginUnorderedAccessViews()
  {
    rangeSize = 0;
    self().onUnorderedAccessViewsParamIndex(desc.NumParameters);
  }
  void endUnorderedAccessViews()
  {
    G_ASSERT(desc.NumParameters < countof(params));
    G_ASSERT(rangeSize > 0);

    auto &target = params[desc.NumParameters++];
    target.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    target.ShaderVisibility = currentVisibility;
    target.DescriptorTable.NumDescriptorRanges = rangeSize;
    target.DescriptorTable.pDescriptorRanges = rangePosition;

    rangePosition += rangeSize;
    G_ASSERT(rangePosition <= eastl::end(ranges));

    signatureCost += 1; // offset into active descriptor heap
  }
  void unorderedAccessView(uint32_t space, uint32_t slot, uint32_t descriptor_count, uint32_t linear_index)
  {
    auto &target = rangePosition[rangeSize++];
    G_ASSERT(&target < eastl::end(ranges));
    target.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    target.NumDescriptors = descriptor_count;
    target.BaseShaderRegister = slot;
    target.RegisterSpace = space;
    target.OffsetInDescriptorsFromTableStart = linear_index;
  }
};

// full callback set for decode_graphics_root_signature building a vertex pipeline signature desc
template <typename D>
struct GraphicsRootSignatureGeneratorBase : RootSignatureGeneratorBase<5, D>
{
  using BaseType = RootSignatureGeneratorBase<5, D>;

  void begin()
  {
    BaseType::begin();
    this->desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  }
  void beginFlags() {}
  void endFlags() {}
  void hasVertexInputs() { this->desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT; }
  void noVertexShaderResources() { this->desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS; }
  void noHullShaderResources() { this->desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS; }
  void noDomainShaderResources() { this->desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS; }
  void noGeometryShaderResources() { this->desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS; }
  void setVisibilityVertexShader() { this->currentVisibility = D3D12_SHADER_VISIBILITY_VERTEX; }
  void setVisibilityHullShader() { this->currentVisibility = D3D12_SHADER_VISIBILITY_HULL; }
  void setVisibilityDomainShader() { this->currentVisibility = D3D12_SHADER_VISIBILITY_DOMAIN; }
  void setVisibilityGeometryShader() { this->currentVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY; }
};

} // namespace dxil
