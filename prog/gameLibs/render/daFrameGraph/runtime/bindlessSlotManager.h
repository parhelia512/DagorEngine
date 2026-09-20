// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <util/dag_stdint.h>
#include <dag/dag_vector.h>
#include <id/idIndexedMapping.h>
#include <backend/intermediateRepresentation.h>

class D3dResource;

namespace dafg
{

// Owns the bindless descriptor ranges for bindlessShaderVar requests (separate
// driver heaps for textures and buffers). Each resource gets SCHEDULE_FRAME_WINDOW
// slots per recompile, one per in-flight frame, so the executor selects a slot by
// index instead of rewriting one descriptor each frame; descriptors are refreshed
// only when the bound resource pointer changes. Samplers are driver-deduped here.
class BindlessSlotManager
{
public:
  static constexpr uint32_t INVALID_SLOT = static_cast<uint32_t>(-1);

  void rebuild(const intermediate::Graph &graph);

  uint32_t baseSlot(intermediate::ResourceIndex res_idx) const { return baseSlots[res_idx]; }

  // Forgets cached descriptor->resource associations (forcing a refresh on next
  // bind) without freeing ranges. Call after a reschedule that kept the same slots.
  void invalidateSlotCache();

  // Points the descriptor at `slot` to `res`, skipping the driver call if unchanged.
  void updateBindlessResource(bool is_tex, uint32_t slot, D3dResource *res);

  // Frees the allocated ranges and drops the per-slot cache. Idempotent.
  void freeRanges();

  ~BindlessSlotManager();

private:
  IdIndexedMapping<intermediate::ResourceIndex, uint32_t> baseSlots;
  uint32_t texRangeBase = INVALID_SLOT;
  uint32_t texRangeCount = 0;
  uint32_t bufRangeBase = INVALID_SLOT;
  uint32_t bufRangeCount = 0;
  // Last resource written to each descriptor, indexed by slot - rangeBase; compared
  // on bind to elide redundant updates.
  dag::Vector<D3dResource *> texSlotResources;
  dag::Vector<D3dResource *> bufSlotResources;
};

} // namespace dafg
