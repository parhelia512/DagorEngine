// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "debug/names.h"
#include "device.h"

namespace drv3d_dx12
{

static const char *query_heap_kind_name(D3D12_QUERY_HEAP_TYPE type)
{
  switch (type)
  {
    case D3D12_QUERY_HEAP_TYPE_TIMESTAMP: return "TimestampQueryHeap";
    case D3D12_QUERY_HEAP_TYPE_OCCLUSION: return "OcclusionQueryHeap";
    case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS: return "PipelineStatisticsQueryHeap";
    default: return "QueryHeap";
  }
}

ComPtr<ID3D12QueryHeap> BackendQueryManager::createQueryHeap(Device &device, D3D12_QUERY_HEAP_TYPE type, uint32_t count)
{
  D3D12_QUERY_HEAP_DESC heapDesc{
    .Type = type,
    .Count = count,
    .NodeMask = 0,
  };
  ComPtr<ID3D12QueryHeap> heap;
  if (!DX12_CHECK_OK(device.getDevice()->CreateQueryHeap(&heapDesc, COM_ARGS(&heap))))
  {
    return {};
  }
  debug::name_object(heap.Get(), debug::make_pool_object_name(query_heap_kind_name(type)));
  return heap;
}

ComPtr<ID3D12Resource> BackendQueryManager::createQueryReadBackBuffer(Device &device, void **mapped_memory)
{
  D3D12_HEAP_PROPERTIES bufferHeapProps{
    .Type = D3D12_HEAP_TYPE_READBACK,
    .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
    .CreationNodeMask = 0,
    .VisibleNodeMask = 0,
  };

  D3D12_RESOURCE_DESC bufferDesc{
    .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
    .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
    .Width = read_back_buffer_size,
    .Height = 1,
    .DepthOrArraySize = 1,
    .MipLevels = 1,
    .Format = DXGI_FORMAT_UNKNOWN,
    .SampleDesc{
      .Count = 1,
      .Quality = 0,
    },
    .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    .Flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
  };
  ComPtr<ID3D12Resource> readBackBuffer;
  if (!DX12_CHECK_OK(device.getDevice()->CreateCommittedResource(&bufferHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, COM_ARGS(&readBackBuffer))))
  {
    return {};
  }

  D3D12_RANGE range{};
  if (!DX12_CHECK_OK(readBackBuffer->Map(0, &range, mapped_memory)))
  {
    return {};
  }
  debug::name_resource(readBackBuffer.Get(), debug::make_pool_object_name("QueryReadBackBuffer"));

  device.recordCommittedResourceAllocated(read_back_buffer_size, false);

  return readBackBuffer;
}

void BackendQueryManager::shutdown(Device &device)
{
  timestampFlushes.clear();
  visibilityFlushes.clear();
  pipelineStatsFlushes.clear();
  finishedPipelineStatsQueries.clear();
  currentPipelineStatsQueries.clear();
  lazyPipelineStatsQueries.clear();
  pendingDeactivationLazyPipelineStatsQueries.clear();

  auto freeHeaps = [&device](auto &heaps) {
    for (auto &heap : heaps)
      if (heap.readBackBuffer)
        device.recordCommittedResourceFreed(read_back_buffer_size, false);
    heaps.clear();
  };

  freeHeaps(timestampHeaps);
  freeHeaps(visibilityHeaps);
  freeHeaps(pipelineStatsHeaps);
}

} // namespace drv3d_dx12
