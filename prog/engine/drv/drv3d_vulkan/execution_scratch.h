// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

// collection of various temporal information that used inside single work item execution, but need memory backing

#include <drv/3d/dag_commands.h>
#include <util/dag_stdint.h>

#include "vk_wrapped_handles.h"
#include "buffer_ref.h"
#include "device_queue.h"

namespace drv3d_vulkan
{

class Image;
class Buffer;

struct ExecutionScratch
{
  dag::Vector<FrameEvents *> frameEventCallbacks;
  dag::Vector<Image *> imageResidenceRestores;
  dag::Vector<Buffer *> bufferResidenceRestores;

  struct DiscardNotify
  {
    BufferRef oldBuf;
    BufferRef newBuf;
    uint32_t flags;
  };
  dag::Vector<DiscardNotify> delayedDiscards;
  struct CommandBufferSubmit
  {
    DeviceQueueType queue;
    VulkanCommandBufferHandle handle;
    uint32_t signals;
    uint32_t waits;
  };
  dag::Vector<CommandBufferSubmit> cmdListsToSubmit;

  struct CommandBufferSubmitDeps
  {
    uint32_t from;
    uint32_t to;
  };
  dag::Vector<CommandBufferSubmitDeps> cmdListsSubmitDeps;

  struct QueueSubmitItem
  {
    Tab<VulkanCommandBufferHandle> cbs;
    Tab<VulkanSemaphoreHandle> signals;
    Tab<VulkanSemaphoreHandle> waitSemaphores;
    Tab<DeviceQueue::TimelineInfo> waitTimelines;
    uint32_t signalsCount;
    DeviceQueueType queue;
    uint32_t originalSignalId;
    uint32_t originalWaitId;
    bool fenceWait;

    // prepare a pooled slot for reuse: drop contents but keep the internal arrays' allocated storage
    void reset(DeviceQueueType q, uint32_t original_id)
    {
      cbs.clear();
      signals.clear();
      waitSemaphores.clear();
      waitTimelines.clear();
      signalsCount = 0;
      queue = q;
      originalSignalId = original_id;
      originalWaitId = original_id;
      fenceWait = false;
    }
  };

  struct SubmitGraph
  {
    Tab<QueueSubmitItem> pool;
    uint32_t used = 0;

    uint32_t size() const { return used; }
    bool empty() const { return used == 0; }
    QueueSubmitItem *data() { return pool.data(); }
    QueueSubmitItem *begin() { return pool.data(); }
    QueueSubmitItem *end() { return pool.data() + used; }
    QueueSubmitItem &operator[](uint32_t i) { return pool[i]; }
    QueueSubmitItem &back() { return pool[used - 1]; }

    // grab next slot, growing the pool only when the high watermark increases; caller must reset() it
    QueueSubmitItem &push_back()
    {
      if (used == pool.size())
        pool.push_back();
      return pool[used++];
    }
    void reuse() { used = 0; }
  };
  SubmitGraph submitGraph;

  struct UserQueueSignal
  {
    size_t bufferIdx;
    uint8_t waitedOnQueuesMask;
  };
  dag::Vector<UserQueueSignal> userQueueSignals;

  struct DebugEvent
  {
    uint32_t color;
    const char *name;
  };
  dag::Vector<DebugEvent> debugEventStack;

  dag::Vector<Image *> mipGenList;

#if VULKAN_HAS_RAYTRACING
  // inputs of one batched acceleration structure build, kept here to reuse the allocations
  struct RaytraceBuildBatch
  {
    dag::Vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
    dag::Vector<const VkAccelerationStructureBuildRangeInfoKHR *> ranges;
    // BLAS geometries and ranges stay in the replay stores, TLAS ones have no store and live here
    dag::Vector<VkAccelerationStructureGeometryKHR> tlasGeometries;
    dag::Vector<VkAccelerationStructureBuildRangeInfoKHR> tlasRanges;

    // reserve keeps the pointers installed into infos valid while the batch fills up
    void reset(uint32_t count)
    {
      infos.clear();
      infos.reserve(count);
      ranges.clear();
      ranges.reserve(count);
      tlasGeometries.clear();
      tlasGeometries.reserve(count);
      tlasRanges.clear();
      tlasRanges.reserve(count);
    }
  };
  RaytraceBuildBatch raytraceBuildBatch;
#if VK_EXT_opacity_micromap
  dag::Vector<VkMicromapBuildInfoEXT> micromapBuildInfos;
#endif
#endif
};

} // namespace drv3d_vulkan
