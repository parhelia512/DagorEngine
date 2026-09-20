// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <memory/dag_framemem.h>

#include <backend/intermediateRepresentation.h>
#include <backend/resourceScheduling/resourceScheduler.h>
#include <backend/resourceScheduling/resourceSchedulingTypes.h>
#include <backend/resourceScheduling/barrierScheduler.h>
#include <backend/resourceScheduling/resourcePropertyProvider.h>
#include <render/daFrameGraph/history.h>
#include <render/daFrameGraph/usage.h>
#include <render/daFrameGraph/stage.h>
#include <render/daFrameGraph/detail/access.h>
#include <drv/3d/dag_heap.h>
#include <drv/3d/dag_resource.h>
#include <drv/3d/dag_texFlags.h>
#include <util/dag_convar.h>
#include <catch2/catch_test_macros.hpp>


extern ConVarT<bool, false> fill_dedicated_fast_gpu_local;

namespace
{

// Fake heap groups -- just need to be stable non-null pointers
static int FAKE_GPU_HEAP_STORAGE;
static int FAKE_GPU_HEAP_2_STORAGE;
static int FAKE_DEDICATED_HEAP_STORAGE;
static ResourceHeapGroup *const FAKE_GPU_HEAP = reinterpret_cast<ResourceHeapGroup *>(&FAKE_GPU_HEAP_STORAGE);
static ResourceHeapGroup *const FAKE_GPU_HEAP_2 = reinterpret_cast<ResourceHeapGroup *>(&FAKE_GPU_HEAP_2_STORAGE);
// Stands for ESRAM: small, and resources only land in it when asked to
static ResourceHeapGroup *const FAKE_DEDICATED_HEAP = reinterpret_cast<ResourceHeapGroup *>(&FAKE_DEDICATED_HEAP_STORAGE);

struct FillDedicatedHeapGuard
{
  const bool prevValue = fill_dedicated_fast_gpu_local.get();
  explicit FillDedicatedHeapGuard(bool value = true) { fill_dedicated_fast_gpu_local.set(value); }
  ~FillDedicatedHeapGuard() { fill_dedicated_fast_gpu_local.set(prevValue); }
};

struct MockPropertyProvider : dafg::IResourcePropertyProvider
{
  size_t defaultSize = 1024;
  size_t defaultAlignment = 256;
  uint64_t maxHeapSize = 64u * 1024u * 1024u;
  uint64_t dedicatedMaxHeapSize = 4096;

  // Per-resource overrides keyed by ResourceDescription::asBasicRes.cFlags
  // (each test resource gets a unique cFlags via nextCFlags in the fixture)
  dag::VectorMap<uint32_t, size_t> sizeOverrides;
  dag::VectorMap<uint32_t, ResourceHeapGroup *> heapGroupOverrides;

  // Resources in this set are treated as vacant (not scheduled).
  dag::VectorSet<uint32_t> vacantCFlags;

  ResourceAllocationProperties getResourceAllocationProperties(const ResourceDescription &desc, bool) override
  {
    if (vacantCFlags.count(desc.asBasicRes.cFlags))
      return {0, 0, nullptr};
    size_t size = defaultSize;
    auto sIt = sizeOverrides.find(desc.asBasicRes.cFlags);
    if (sIt != sizeOverrides.end())
      size = sIt->second;
    ResourceHeapGroup *group = FAKE_GPU_HEAP;
    auto gIt = heapGroupOverrides.find(desc.asBasicRes.cFlags);
    if (gIt != heapGroupOverrides.end())
      group = gIt->second;
    return {size, defaultAlignment, group};
  }

  ResourceHeapGroupProperties getResourceHeapGroupProperties(ResourceHeapGroup *group) override
  {
    ResourceHeapGroupProperties props{};
    props.flags = 0;
    props.isGPULocal = true;
    props.optimalMaxHeapSize = 0;
    props.isDedicatedFastGPULocal = group == FAKE_DEDICATED_HEAP;
    props.maxHeapSize = props.isDedicatedFastGPULocal ? dedicatedMaxHeapSize : maxHeapSize;
    return props;
  }
};

static ResourceDescription make_tex_desc(uint32_t cflags)
{
  TextureResourceDescription tex{};
  tex.cFlags = cflags;
  tex.activation = ResourceActivationAction::DISCARD_AS_RTV_DSV;
  tex.clearValue = {};
  tex.mipLevels = 1;
  tex.width = 4;
  tex.height = 4;
  return ResourceDescription{tex};
}

struct ResourceSchedulerFixture
{
  dafg::intermediate::Graph graph;
  dafg::ResourceLifetimes lifetimes;
  MockPropertyProvider propertyProvider;
  dafg::ResourceScheduler scheduler;

  // Each resource gets a unique cFlags so the mock can apply per-resource size overrides.
  // Starting from 1 to avoid collisions with TEXFMT_A8R8G8B8 (= 0).
  uint32_t nextCFlags = 1;

  dafg::intermediate::ResourceIndex addGpuResource(dafg::History history = dafg::History::No, uint32_t extra_cflags = 0)
  {
    auto desc = make_tex_desc(nextCFlags++ | extra_cflags);

    dafg::intermediate::ScheduledResource scheduled;
    scheduled.description = desc;
    scheduled.resourceType = dafg::ResourceType::Texture;
    scheduled.resolutionType = eastl::nullopt;
    scheduled.clearStage = dafg::intermediate::ClearStage::None;
    scheduled.clearValue = ResourceClearValue{};
    scheduled.clearFlags = RESOURCE_CLEAR_ALL_CONTENT;
    scheduled.history = history;
    scheduled.autoMipCount = false;

    auto [idx, res] = graph.resources.appendNew();
    res.resource = eastl::move(scheduled);
    res.frontendResources.push_back(static_cast<dafg::ResNameId>(eastl::to_underlying(idx)));
    res.multiplexingIndex = dafg::intermediate::MultiplexingIndex{0};
    graph.resourceNames.emplaceAt(idx, "test_resource");
    return idx;
  }

  // Returns the cFlags assigned to a resource (for use as sizeOverrides key)
  uint32_t cFlagsOf(dafg::intermediate::ResourceIndex idx)
  {
    return graph.resources[idx].asScheduled().getGpuDescription().asBasicRes.cFlags;
  }

  // Changes the texture extent without touching cFlags, so the mock keeps
  // reporting the same allocation size. This is what a resolution change looks
  // like whenever both extents pad to the same gAPI tile-aligned image size.
  void resizeResource(dafg::intermediate::ResourceIndex idx, uint32_t width, uint32_t height)
  {
    auto &desc = graph.resources[idx].asScheduled().getGpuDescription();
    desc.asTexRes.width = width;
    desc.asTexRes.height = height;
  }

  dafg::intermediate::NodeIndex addNode()
  {
    auto [idx, node] = graph.nodes.appendNew();
    node.priority = 0;
    node.multiplexingIndex = dafg::intermediate::MultiplexingIndex{0};
    node.hasSideEffects = true;
    graph.nodeNames.emplaceAt(idx, "test_node");
    return idx;
  }

  // Resource usages are what the dedicated heap candidate scoring looks at
  void addUsage(dafg::intermediate::NodeIndex node, dafg::intermediate::ResourceIndex res, dafg::Usage usage,
    dafg::Access access = dafg::Access::READ_ONLY)
  {
    dafg::intermediate::Request request;
    request.resource = res;
    request.usage = {usage, access, dafg::Stage::POST_RASTER};
    request.fromLastFrame = false;
    graph.nodes[node].resourceRequests.push_back(request);
  }

  void ensureLifetimesSize()
  {
    for (auto &frameLifetimes : lifetimes)
      frameLifetimes.resize(graph.resources.totalKeys());
  }

  // Gives a non-temporal resource a lifetime in both scheduling frames.
  // Both frame copies live within their own frame.
  void addLifetime(dafg::intermediate::ResourceIndex res, dafg::intermediate::NodeIndex first_use_at,
    dafg::intermediate::NodeIndex release_at)
  {
    ensureLifetimesSize();
    for (uint32_t frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
      lifetimes[frame][res] = {{frame, eastl::to_underlying(first_use_at)}, {frame, eastl::to_underlying(release_at)}};
  }

  // Gives a temporal resource a lifetime that starts in one frame and ends in
  // the next. This creates wrapping lifetimes, which is what temporal resources
  // have in practice (produced in one frame, consumed as history in the next).
  void addTemporalLifetime(dafg::intermediate::ResourceIndex res, dafg::intermediate::NodeIndex first_use_at,
    dafg::intermediate::NodeIndex release_at)
  {
    ensureLifetimesSize();
    for (uint32_t frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
    {
      const uint32_t otherFrame = (frame + 1) % dafg::SCHEDULE_FRAME_WINDOW;
      lifetimes[frame][res] = {{frame, eastl::to_underlying(first_use_at)}, {otherFrame, eastl::to_underlying(release_at)}};
    }
  }

  // Resources whose historyPairing should be NOT_MAPPED rather than identity.
  dag::VectorSet<dafg::intermediate::ResourceIndex> brokenHistoryPairings;

  dafg::ResourceSchedule computeSchedule(int prev_frame = 0, const dafg::HeapRequests &allocated_heaps = {},
    bool all_lifetimes_changed = true)
  {
    FRAMEMEM_REGION;

    ensureLifetimesSize();

    const auto resCount = graph.resources.totalKeys();

    dafg::ResourceLifetimeCalculator::LifetimesChanged lifetimeChanged(resCount, all_lifetimes_changed);
    dafg::IrHistoryPairing historyPairing;
    historyPairing.resize(resCount, dafg::intermediate::RESOURCE_NOT_MAPPED);
    for (uint32_t i = 0; i < resCount; ++i)
    {
      auto idx = static_cast<dafg::intermediate::ResourceIndex>(i);
      if (graph.resources.isMapped(idx) && brokenHistoryPairings.find(idx) == brokenHistoryPairings.end())
        historyPairing[idx] = idx;
    }

    dafg::BadResolutionTracker::Corrections corrections;
    for (auto &c : corrections)
      c.resize(resCount, 0);

    dafg::ResourceScheduler::SchedulingContext ctx{graph, lifetimes, lifetimeChanged, historyPairing, corrections, propertyProvider,
      allocated_heaps, graph.resources, graph.resourceNames};

    return scheduler.computeSchedule(prev_frame, ctx);
  }
};

static eastl::optional<dafg::HeapIndex> find_heap_of_group(dafg::ResourceSchedule &schedule, ResourceHeapGroup *group)
{
  for (auto [idx, req] : schedule.heapRequests.enumerate())
    if (req.group == group)
      return idx;
  return eastl::nullopt;
}

static bool is_in_dedicated_heap(dafg::ResourceSchedule &schedule, dafg::intermediate::ResourceIndex res, uint32_t frame)
{
  const auto dedicatedHeap = find_heap_of_group(schedule, FAKE_DEDICATED_HEAP);
  return dedicatedHeap.has_value() && schedule.allocationLocations[frame].isMapped(res) &&
         schedule.allocationLocations[frame][res].heap == *dedicatedHeap;
}

// A resource that natively asks for the dedicated heap. Without one there is no
// dedicated heap request at all and nothing can be promoted into it.
static dafg::intermediate::ResourceIndex add_dedicated_resource(ResourceSchedulerFixture &f)
{
  auto res = f.addGpuResource();
  f.propertyProvider.heapGroupOverrides[f.cFlagsOf(res)] = FAKE_DEDICATED_HEAP;
  return res;
}

} // namespace


TEST_CASE("single resource gets allocated", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;

  auto res = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  f.addLifetime(res, node0, node1);

  auto schedule = f.computeSchedule();

  for (int frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
  {
    REQUIRE(schedule.allocationLocations[frame].isMapped(res));
    CHECK(schedule.allocationLocations[frame][res].heap != dafg::HeapIndex::Invalid);
  }

  bool hasNonZeroHeap = false;
  for (auto [idx, heap] : schedule.heapRequests.enumerate())
    if (heap.size > 0)
      hasNonZeroHeap = true;
  CHECK(hasNonZeroHeap);
}

TEST_CASE("non-overlapping resources alias", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 1024;

  auto resA = f.addGpuResource();
  auto resB = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  auto node2 = f.addNode();

  // A lives [node0, node1), B lives [node1, node2) -- non-overlapping
  f.addLifetime(resA, node0, node1);
  f.addLifetime(resB, node1, node2);

  auto schedule = f.computeSchedule();

  REQUIRE(schedule.allocationLocations[0].isMapped(resA));
  REQUIRE(schedule.allocationLocations[0].isMapped(resB));

  auto &locA = schedule.allocationLocations[0][resA];
  auto &locB = schedule.allocationLocations[0][resB];

  // They should be in the same heap
  CHECK(locA.heap == locB.heap);

  // With non-overlapping lifetimes and equal sizes, they alias perfectly
  for (auto [idx, heap] : schedule.heapRequests.enumerate())
    if (heap.size > 0 && heap.group == FAKE_GPU_HEAP)
      CHECK(heap.size < 2 * f.propertyProvider.defaultSize);
}

TEST_CASE("overlapping resources don't alias", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 1024;

  auto resA = f.addGpuResource();
  auto resB = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  auto node2 = f.addNode();
  auto node3 = f.addNode();

  // A lives [node0, node2), B lives [node1, node3) -- overlapping
  f.addLifetime(resA, node0, node2);
  f.addLifetime(resB, node1, node3);

  auto schedule = f.computeSchedule();

  REQUIRE(schedule.allocationLocations[0].isMapped(resA));
  REQUIRE(schedule.allocationLocations[0].isMapped(resB));

  uint64_t totalGpuHeapSize = 0;
  for (auto [idx, heap] : schedule.heapRequests.enumerate())
    if (heap.size > 0 && heap.group == FAKE_GPU_HEAP)
      totalGpuHeapSize += heap.size;

  CHECK(totalGpuHeapSize >= 2 * f.propertyProvider.defaultSize);
}

TEST_CASE("rebuild produces same schedule", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;

  auto res = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  f.addLifetime(res, node0, node1);

  auto schedule1 = f.computeSchedule(0);
  auto schedule2 = f.computeSchedule(1, schedule1.heapRequests);

  for (int frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
  {
    REQUIRE(schedule1.allocationLocations[frame].isMapped(res));
    REQUIRE(schedule2.allocationLocations[frame].isMapped(res));
    CHECK(schedule1.allocationLocations[frame][res].heap == schedule2.allocationLocations[frame][res].heap);
    CHECK(schedule1.allocationLocations[frame][res].offset == schedule2.allocationLocations[frame][res].offset);
  }
}

TEST_CASE("temporal resource preservation", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 1024;

  // Resource A is temporal (history), B and C are not
  auto resA = f.addGpuResource(dafg::History::DiscardOnFirstFrame);
  auto resB = f.addGpuResource();
  auto resC = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  auto node2 = f.addNode();
  auto node3 = f.addNode();

  // resA is temporal: wrapping lifetime (activated in one frame, deactivated in next)
  f.addTemporalLifetime(resA, node1, node1);
  // B and C are non-temporal with overlapping lifetimes in each frame
  f.addLifetime(resB, node0, node2);
  f.addLifetime(resC, node1, node3);

  // First schedule on frame 0
  auto schedule1 = f.computeSchedule(0);

  REQUIRE(schedule1.allocationLocations[1].isMapped(resA));

  // Change B and C sizes to force a repack
  f.propertyProvider.sizeOverrides[f.cFlagsOf(resB)] = 2048;
  f.propertyProvider.sizeOverrides[f.cFlagsOf(resC)] = 512;

  // Second schedule on frame 1 -- B and C changed, heap must repack,
  // but A should keep its offset (pinned via temporal preservation)
  // preserve_produced_on_frame for prev_frame=1 is 1, so frame 1's copy gets pinned
  auto schedule2 = f.computeSchedule(1, schedule1.heapRequests);

  REQUIRE(schedule2.allocationLocations[1].isMapped(resA));
  CHECK(schedule2.allocationLocations[1][resA].offset == schedule1.allocationLocations[1][resA].offset);
}

TEST_CASE("skipped heaps preserve their size", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 1024;

  auto res = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  f.addLifetime(res, node0, node1);

  // First schedule -- everything is new
  auto schedule1 = f.computeSchedule(0);

  uint64_t heapSize1 = 0;
  for (auto [idx, heap] : schedule1.heapRequests.enumerate())
    if (heap.size > 0 && heap.group == FAKE_GPU_HEAP)
      heapSize1 = heap.size;
  REQUIRE(heapSize1 > 0);

  // Second schedule -- nothing changed, heaps should be skipped but retain their sizes
  auto schedule2 = f.computeSchedule(1, schedule1.heapRequests, false);

  uint64_t heapSize2 = 0;
  for (auto [idx, heap] : schedule2.heapRequests.enumerate())
    if (heap.group == FAKE_GPU_HEAP)
      heapSize2 = heap.size;
  CHECK(heapSize2 == heapSize1);
}

TEST_CASE("temporal resource with cross-heap frame copies", "[resourceScheduler]")
{
  // A temporal resource's two frame copies can't alias (wrapping lifetime).
  // With maxHeapSize < 2*resourceSize, they go to separate heaps.
  // A non-temporal resource shares one heap. When the non-temporal resource
  // changes, its heap repacks. The temporal resource's copy in that heap
  // must still be correctly scheduled (not silently dropped).
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 768;
  f.propertyProvider.defaultAlignment = 256;
  // resT(768) + resX(256) = 1024 fits, but resT(768) + resT(768) = 1536 doesn't
  f.propertyProvider.maxHeapSize = 1024;

  auto resT = f.addGpuResource(dafg::History::DiscardOnFirstFrame);
  f.propertyProvider.sizeOverrides[f.nextCFlags] = 256;
  auto resX = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  f.addNode(); // timeline length
  f.addNode(); // timeline length

  f.addTemporalLifetime(resT, node0, node1);
  f.addLifetime(resX, node0, node1);

  auto schedule1 = f.computeSchedule(0);

  // resT's frame copies must be in different heaps
  REQUIRE(schedule1.allocationLocations[0][resT].heap != schedule1.allocationLocations[1][resT].heap);

  // Change resX to force a repack of its heap
  f.propertyProvider.sizeOverrides[f.cFlagsOf(resX)] = 128;

  auto schedule2 = f.computeSchedule(1, schedule1.heapRequests, false);

  // All allocations must be within heap bounds
  for (int frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
    for (auto res : {resT, resX})
    {
      auto [heap, offset] = schedule2.allocationLocations[frame][res];
      size_t resSize = (res == resX) ? 128 : f.propertyProvider.defaultSize;
      CHECK(offset + resSize <= schedule2.heapRequests[heap].size);
    }
}

TEST_CASE("resource moved into a group whose only heap is reused gets scheduled", "[resourceScheduler]")
{
  // Bug: bucketResourcesIntoHeaps falls back to the next non-reused heap of
  // the resource's group; if no such heap exists, it leaves desiredHeap
  // pointing at the reused heap, which is excluded from idxs and never
  // scheduled. The resource is silently dropped.
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 1024;

  auto resA = f.addGpuResource();
  auto resB = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();

  // Initially: A in group 1, B in group 2 -- one heap per group
  f.propertyProvider.heapGroupOverrides[f.cFlagsOf(resA)] = FAKE_GPU_HEAP;
  f.propertyProvider.heapGroupOverrides[f.cFlagsOf(resB)] = FAKE_GPU_HEAP_2;

  f.addLifetime(resA, node0, node1);
  f.addLifetime(resB, node0, node1);

  auto schedule1 = f.computeSchedule(0);

  REQUIRE(schedule1.allocationLocations[0].isMapped(resA));
  REQUIRE(schedule1.allocationLocations[0].isMapped(resB));
  const auto heapA1 = schedule1.allocationLocations[0][resA].heap;
  const auto heapB1 = schedule1.allocationLocations[0][resB].heap;
  REQUIRE(heapA1 != heapB1);
  REQUIRE(schedule1.heapRequests[heapA1].group == FAKE_GPU_HEAP);
  REQUIRE(schedule1.heapRequests[heapB1].group == FAKE_GPU_HEAP_2);

  // Move B into group 1. A is unchanged, so the only heap of group 1 (the one
  // containing A) qualifies for reuse. B's old heap (group 2) is the only heap
  // of group 1 candidate available is the reused one -- the scheduler must
  // either un-reuse it or create a new heap, not silently drop B.
  f.propertyProvider.heapGroupOverrides[f.cFlagsOf(resB)] = FAKE_GPU_HEAP;

  auto schedule2 = f.computeSchedule(1, schedule1.heapRequests, false);

  for (int frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
  {
    REQUIRE(schedule2.allocationLocations[0].isMapped(resB));
    const auto [heapB2, offsetB2] = schedule2.allocationLocations[frame][resB];
    // B must end up in a heap of its new group
    CHECK(schedule2.heapRequests[heapB2].group == FAKE_GPU_HEAP);
    // B's allocation must fit inside its heap
    CHECK(offsetB2 + f.propertyProvider.defaultSize <= schedule2.heapRequests[heapB2].size);
  }
}

TEST_CASE("history resource without a valid pairing is not marked preserved when its heap is reused", "[resourceScheduler]")
{
  // Bug: when a heap qualifies for reuse, the scheduler marks every history
  // resource in it as preserved without verifying that the resource has a
  // valid historical pair at the same heap location with the same size.
  // For a freshly-renamed resource (historyPairing == NOT_MAPPED), this tells
  // the runtime to skip activation and trust the heap memory -- but those
  // bytes belong to an unrelated previous resource, so the blob ends up
  // holding garbage.
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 1024;

  auto resA = f.addGpuResource(dafg::History::DiscardOnFirstFrame);
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  f.addTemporalLifetime(resA, node0, node1);

  auto schedule1 = f.computeSchedule(0);

  // Simulate a frontend rename: the next schedule sees resA but with no
  // matching old-side history pair (historyPairing[resA] = NOT_MAPPED).
  f.brokenHistoryPairings.insert(resA);

  auto schedule2 = f.computeSchedule(1, schedule1.heapRequests, false);

  // Without a valid history pair, the scheduler must not mark resA as
  // preserved -- otherwise the runtime would skip its activation and
  // expose stale heap memory to its consumers.
  for (int frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
    CHECK_FALSE(f.scheduler.isResourcePreserved(frame, resA));
}

// Preservation means "the runtime may keep using the very same gAPI object".
// The scheduler only ever compares memory layout -- heap, offset and byte size
// -- but NativeResourceAllocator::placeResource keys its per-heap object cache
// on the full ResourceDescription. So whenever the description changes while
// the allocation size stays put, the allocator hands out a different texture at
// the same offset, and preservation makes the runtime skip both the
// deactivation of the old object and the activation of the new one. Consumers
// then sample an image nothing ever wrote.
//
// The two tests below cover the two independent places that grant preservation:
// the reused-heap fast path in scheduleResourcesIntoHeaps, and the pinning
// block in scheduleHeap.

TEST_CASE("history resource whose description changed is not preserved when its heap is reused", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 1024;

  auto resA = f.addGpuResource(dafg::History::ClearZeroOnFirstFrame);
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  f.addTemporalLifetime(resA, node0, node1);

  auto schedule1 = f.computeSchedule(0);

  // Every input the placement-changed test looks at stays equal, so the heap
  // qualifies for wholesale reuse, but the texture is a different one now.
  f.resizeResource(resA, 8, 8);

  auto schedule2 = f.computeSchedule(1, schedule1.heapRequests, false);

  for (int frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
    CHECK_FALSE(f.scheduler.isResourcePreserved(frame, resA));
}

TEST_CASE("history resource whose description changed is not preserved when its heap is repacked", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 1024;

  auto resA = f.addGpuResource(dafg::History::ClearZeroOnFirstFrame);
  auto resB = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  f.addTemporalLifetime(resA, node0, node1);
  f.addLifetime(resB, node0, node1);

  auto schedule1 = f.computeSchedule(0);
  REQUIRE(schedule1.allocationLocations[0][resA].heap == schedule1.allocationLocations[0][resB].heap);

  // resB grows, which disqualifies the shared heap from wholesale reuse and
  // sends it through the packer. resA keeps its size and its old location, so
  // the packer pins it and preservation is granted -- but its extent changed.
  f.propertyProvider.sizeOverrides[f.cFlagsOf(resB)] = 2048;
  f.resizeResource(resA, 8, 8);

  auto schedule2 = f.computeSchedule(1, schedule1.heapRequests, false);

  for (int frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
    CHECK_FALSE(f.scheduler.isResourcePreserved(frame, resA));
}

TEST_CASE("schedule is self-consistent after back-to-back recompilations", "[resourceScheduler]")
{
  // Regression test: simulates the testIncrementality remove+restore cycle.
  // After removing some resources (making them vacant) and recompiling, then
  // restoring them and recompiling again, the schedule must remain valid.
  // The scheduler's cached state from the "remove" pass must not produce
  // invalid preservation pins in the "restore" pass.
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultAlignment = 256;
  // Tight heaps to force overflow and multi-heap layouts.
  f.propertyProvider.maxHeapSize = 4096;

  constexpr int NUM_TEMPORAL = 10;
  constexpr int NUM_NONTEMPORAL = 20;
  constexpr int NUM_RESOURCES = NUM_TEMPORAL + NUM_NONTEMPORAL;
  dafg::intermediate::ResourceIndex resources[NUM_RESOURCES];
  for (int i = 0; i < NUM_TEMPORAL; ++i)
    resources[i] = f.addGpuResource(dafg::History::DiscardOnFirstFrame);
  for (int i = NUM_TEMPORAL; i < NUM_RESOURCES; ++i)
    resources[i] = f.addGpuResource();

  constexpr int NUM_NODES = 8;
  dafg::intermediate::NodeIndex nodes[NUM_NODES];
  for (int i = 0; i < NUM_NODES; ++i)
    nodes[i] = f.addNode();

  for (int i = 0; i < NUM_TEMPORAL; ++i)
  {
    f.propertyProvider.sizeOverrides[f.cFlagsOf(resources[i])] = 256 + (i % 4) * 256;
    f.addTemporalLifetime(resources[i], nodes[i % NUM_NODES], nodes[(i + 2) % NUM_NODES]);
  }
  for (int i = NUM_TEMPORAL; i < NUM_RESOURCES; ++i)
  {
    f.propertyProvider.sizeOverrides[f.cFlagsOf(resources[i])] = 128 + (i % 5) * 128;
    auto start = nodes[i % NUM_NODES];
    auto end = nodes[(i + 1 + i % 3) % NUM_NODES];
    if (eastl::to_underlying(start) >= eastl::to_underlying(end))
      end = nodes[(eastl::to_underlying(start) + 1) % NUM_NODES];
    f.addLifetime(resources[i], start, end);
  }

  auto prevSchedule = f.computeSchedule(0);

  for (int iter = 0; iter < 30; ++iter)
  {
    // Remove ~40% of resources each iteration
    for (int i = 0; i < NUM_RESOURCES; ++i)
      if (((iter * 7 + i * 13) % 5) < 2)
        f.propertyProvider.vacantCFlags.insert(f.cFlagsOf(resources[i]));

    auto removed = f.computeSchedule(iter % 2, prevSchedule.heapRequests);

    f.propertyProvider.vacantCFlags.clear();

    auto restored = f.computeSchedule((iter + 1) % 2, removed.heapRequests);

    for (int i = 0; i < NUM_RESOURCES; ++i)
    {
      for (int frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
      {
        REQUIRE(restored.allocationLocations[frame].isMapped(resources[i]));
        auto [heap, offset] = restored.allocationLocations[frame][resources[i]];
        REQUIRE(restored.heapRequests.isMapped(heap));
        auto sIt = f.propertyProvider.sizeOverrides.find(f.cFlagsOf(resources[i]));
        auto resSize = sIt != f.propertyProvider.sizeOverrides.end() ? sIt->second : f.propertyProvider.defaultSize;
        CHECK(offset + resSize <= restored.heapRequests[heap].size);
      }
    }

    prevSchedule = restored;
  }
}

TEST_CASE("temporal resource on overflow heap is preserved across frames", "[resourceScheduler]")
{
  // Regression: when a temporal resource's frame copy overflows to a
  // non-default heap on the first schedule, the next schedule must
  // bucket it back to that same heap for preservation to succeed.
  //
  // Timeline shifting means the "preserve" frame alternates which copy
  // is wrapping vs non-wrapping. The non-wrapping copy overflows because
  // both copies (768 each) exceed maxHeapSize when placed together.
  // On the next schedule, the alternating shift makes the previously
  // overflowed copy the one that needs preservation. If the scheduler
  // always buckets to the group's default heap instead of the previous
  // heap, scheduleHeap can't pin it (prevLoc.heap != heap_idx) and
  // preservation fails.
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 768;
  f.propertyProvider.defaultAlignment = 256;
  // T(768) fits alongside X(256), but T(768)+T(768) = 1536 > 1024
  f.propertyProvider.maxHeapSize = 1024;

  auto resT = f.addGpuResource(dafg::History::DiscardOnFirstFrame);
  f.propertyProvider.sizeOverrides[f.nextCFlags] = 256;
  auto resX = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  f.addNode();
  f.addNode();

  f.addTemporalLifetime(resT, node0, node1);
  f.addLifetime(resX, node0, node1);

  // Schedule 1 (prev_frame=0): T's frame copies go to different heaps.
  // With the timeline shift for prev_frame=0, T_f0 becomes wrapping
  // (placed first, stays on default heap H0), T_f1 becomes non-wrapping
  // and overflows to H1.
  auto s1 = f.computeSchedule(0);
  REQUIRE(s1.allocationLocations[0][resT].heap != s1.allocationLocations[1][resT].heap);

  // Schedule 2 (prev_frame=1): preserveProducedOnFrame=1.
  // The timeline shift flips: now T_f1 is wrapping and T_f0 non-wrapping.
  // T_f1 needs preservation and was previously on overflowHeap.
  // The scheduler must bucket T_f1 to overflowHeap for the pin to work.
  auto s2 = f.computeSchedule(1, s1.heapRequests);
  CHECK(f.scheduler.isResourcePreserved(1, resT));

  // Schedule 3 (prev_frame=0): preserveProducedOnFrame=0.
  // Symmetric situation: T_f0 now needs preservation and was on the
  // overflow heap from schedule 2.
  auto s3 = f.computeSchedule(0, s2.heapRequests);
  CHECK(f.scheduler.isResourcePreserved(0, resT));
}

TEST_CASE("temporal resource does not pin at non-temporal history pair location", "[resourceScheduler]")
{
  // Regression: when a temporal resource's history pair is a non-temporal
  // resource that occupied a different offset on the same heap, pinning at
  // the non-temporal resource's offset can overlap with another temporal
  // resource's pin, triggering an assert in the packer's allocatePinned.
  //
  // The scheduler must only use previous allocations of temporal resources
  // for preservation decisions, not general allocation locations.
  FRAMEMEM_REGION;
  ResourceSchedulerFixture f;
  f.propertyProvider.defaultSize = 512;
  f.propertyProvider.defaultAlignment = 256;
  f.propertyProvider.maxHeapSize = 4096;

  // T1: temporal resource that will have an identity history pair
  auto resT1 = f.addGpuResource(dafg::History::DiscardOnFirstFrame);
  // N: non-temporal resource that will become T2's history pair
  auto resN = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  auto node2 = f.addNode();
  auto node3 = f.addNode();

  f.addTemporalLifetime(resT1, node0, node1);
  // N has a non-overlapping lifetime with T1 (they can alias at the same offset)
  f.addLifetime(resN, node2, node3);

  // Schedule 1: T1 and N placed on the same heap.
  // N may alias with T1 at the same offset due to non-overlapping lifetimes.
  auto s1 = f.computeSchedule(0);
  REQUIRE(s1.allocationLocations[0].isMapped(resT1));
  REQUIRE(s1.allocationLocations[0].isMapped(resN));

  // Now make N temporal and set its history pair to itself (identity).
  // This simulates a resource rename where the new temporal resource's
  // history pair maps to the old non-temporal resource's index.
  // The key invariant: the scheduler must NOT pin resN (now temporal)
  // at resN's old non-temporal offset if that would overlap with resT1's pin.
  f.graph.resources[resN].resource = f.graph.resources[resT1].resource;
  f.addTemporalLifetime(resN, node2, node3);

  // Schedule 2: both T1 and N are temporal. The scheduler must not crash.
  auto s2 = f.computeSchedule(1, s1.heapRequests);
  // Basic sanity: both resources are allocated within heap bounds.
  for (int frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
    for (auto res : {resT1, resN})
    {
      REQUIRE(s2.allocationLocations[frame].isMapped(res));
      auto [heap, offset] = s2.allocationLocations[frame][res];
      REQUIRE(s2.heapRequests.isMapped(heap));
      CHECK(offset + f.propertyProvider.defaultSize <= s2.heapRequests[heap].size);
    }
}

TEST_CASE("resources get promoted into the dedicated heap", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  FillDedicatedHeapGuard guard;
  ResourceSchedulerFixture f;

  auto dedicated = add_dedicated_resource(f);
  auto resA = f.addGpuResource();
  auto resB = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  for (auto res : {dedicated, resA, resB})
    f.addLifetime(res, node0, node1);

  auto schedule = f.computeSchedule();

  for (uint32_t frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
    for (auto res : {dedicated, resA, resB})
      CHECK(is_in_dedicated_heap(schedule, res, frame));

  // Nothing is left for the regular heap, and the dedicated one is always
  // requested at its full size.
  auto regularHeap = find_heap_of_group(schedule, FAKE_GPU_HEAP);
  if (regularHeap.has_value())
    CHECK(schedule.heapRequests[*regularHeap].size == 0);

  auto dedicatedHeap = find_heap_of_group(schedule, FAKE_DEDICATED_HEAP);
  REQUIRE(dedicatedHeap.has_value());
  CHECK(schedule.heapRequests[*dedicatedHeap].size == f.propertyProvider.dedicatedMaxHeapSize);
}

TEST_CASE("resources that do not fit the dedicated heap stay where they were", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  FillDedicatedHeapGuard guard;
  ResourceSchedulerFixture f;

  // Room for the native resource plus exactly one promoted one
  f.propertyProvider.dedicatedMaxHeapSize = 2 * f.propertyProvider.defaultSize;

  auto dedicated = add_dedicated_resource(f);
  auto resA = f.addGpuResource();
  auto resB = f.addGpuResource();
  auto resC = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  for (auto res : {dedicated, resA, resB, resC})
    f.addLifetime(res, node0, node1);

  auto schedule = f.computeSchedule();

  uint32_t promotedCount = 0;
  for (auto res : {resA, resB, resC})
    if (is_in_dedicated_heap(schedule, res, 0))
    {
      ++promotedCount;
      // Which candidate wins is up to the scoring, but it wins on every frame
      CHECK(is_in_dedicated_heap(schedule, res, 1));
    }
  CHECK(promotedCount == 1);

  // The rejected ones did not get lost on the way: they are packed into the
  // regular heap, which is sized to hold exactly the two of them (their frame
  // copies have disjoint lifetimes and hence alias).
  auto regularHeap = find_heap_of_group(schedule, FAKE_GPU_HEAP);
  REQUIRE(regularHeap.has_value());
  CHECK(schedule.heapRequests[*regularHeap].size == 2 * f.propertyProvider.defaultSize);

  for (uint32_t frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
    for (auto res : {resA, resB, resC})
    {
      REQUIRE(schedule.allocationLocations[frame].isMapped(res));
      auto [heap, offset] = schedule.allocationLocations[frame][res];
      CHECK((heap == *regularHeap || is_in_dedicated_heap(schedule, res, frame)));
      CHECK(offset + f.propertyProvider.defaultSize <= schedule.heapRequests[heap].size);
    }
}

TEST_CASE("cpu accessible resources are not promoted", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  FillDedicatedHeapGuard guard;
  ResourceSchedulerFixture f;

  auto dedicated = add_dedicated_resource(f);
  auto resGpu = f.addGpuResource();
  auto resCpu = f.addGpuResource(dafg::History::No, TEXCF_SYSMEM);
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  for (auto res : {dedicated, resGpu, resCpu})
    f.addLifetime(res, node0, node1);

  auto schedule = f.computeSchedule();

  for (uint32_t frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
  {
    CHECK(is_in_dedicated_heap(schedule, resGpu, frame));
    CHECK_FALSE(is_in_dedicated_heap(schedule, resCpu, frame));
  }
}

TEST_CASE("history resources are not promoted", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  FillDedicatedHeapGuard guard;
  ResourceSchedulerFixture f;

  auto dedicated = add_dedicated_resource(f);
  auto resPlain = f.addGpuResource();
  auto resHistory = f.addGpuResource(dafg::History::DiscardOnFirstFrame);
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  f.addLifetime(dedicated, node0, node1);
  f.addLifetime(resPlain, node0, node1);
  f.addTemporalLifetime(resHistory, node0, node1);

  auto schedule = f.computeSchedule();

  for (uint32_t frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
  {
    CHECK(is_in_dedicated_heap(schedule, resPlain, frame));
    CHECK_FALSE(is_in_dedicated_heap(schedule, resHistory, frame));
  }
}

TEST_CASE("heavily used resources are promoted first", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  FillDedicatedHeapGuard guard;
  ResourceSchedulerFixture f;

  // Room for the native resource plus exactly one promoted one
  f.propertyProvider.dedicatedMaxHeapSize = 2 * f.propertyProvider.defaultSize;

  auto dedicated = add_dedicated_resource(f);
  auto resHot = f.addGpuResource();
  auto resCold = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  for (auto res : {dedicated, resHot, resCold})
    f.addLifetime(res, node0, node1);

  // Same size, so the only thing telling them apart is bandwidth usage
  f.addUsage(node0, resHot, dafg::Usage::COLOR_ATTACHMENT);

  auto schedule = f.computeSchedule();

  for (uint32_t frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
  {
    CHECK(is_in_dedicated_heap(schedule, resHot, frame));
    CHECK_FALSE(is_in_dedicated_heap(schedule, resCold, frame));
  }
}

TEST_CASE("nothing is promoted unless asked for", "[resourceScheduler]")
{
  FRAMEMEM_REGION;
  FillDedicatedHeapGuard guard(false);
  ResourceSchedulerFixture f;

  auto dedicated = add_dedicated_resource(f);
  auto resA = f.addGpuResource();
  auto node0 = f.addNode();
  auto node1 = f.addNode();
  f.addLifetime(dedicated, node0, node1);
  f.addLifetime(resA, node0, node1);

  auto schedule = f.computeSchedule();

  for (uint32_t frame = 0; frame < dafg::SCHEDULE_FRAME_WINDOW; ++frame)
  {
    CHECK(is_in_dedicated_heap(schedule, dedicated, frame));
    CHECK_FALSE(is_in_dedicated_heap(schedule, resA, frame));
  }
}
