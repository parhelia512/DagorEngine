// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <memory/dag_framemem.h>

#include <backend/intermediateRepresentation.h>
#include <backend/passColoring.h>
#include <backend/resourceScheduling/barrierScheduler.h>
#include <backend/resourceScheduling/resourceLifetimes.h>
#include <id/idIndexedFlags.h>

#include "testRuntime.h"

#include <catch2/catch_test_macros.hpp>


namespace
{

using dafg::intermediate::NodeIndex;
using dafg::intermediate::ResourceIndex;

NodeIndex node(uint32_t idx) { return static_cast<NodeIndex>(idx); }

bool has_barrier(const dafg::BarrierScheduler::EventsCollection &events, ResourceIndex res, ResourceBarrier flag)
{
  for (const auto &frameEvents : events)
    for (const auto &nodeEvents : frameEvents.values())
      for (const auto &event : nodeEvents)
        if (event.resource == res)
          if (const auto *barrier = eastl::get_if<dafg::BarrierScheduler::Event::Barrier>(&event.data))
            if ((barrier->barrier & flag) != 0)
              return true;
  return false;
}

} // namespace

TEST_CASE("a request change alone rebuilds the resource's barriers", "[barrierScheduler][incremental]")
{
  TestRuntime testRuntime{};
  FRAMEMEM_REGION;

  dafg::intermediate::Graph graph;

  TextureResourceDescription texDesc{};
  texDesc.cFlags = TEXFMT_R8G8B8A8 | TEXCF_RTARGET;
  texDesc.activation = ResourceActivationAction::DISCARD_AS_RTV_DSV;
  texDesc.mipLevels = 1;
  texDesc.width = 4;
  texDesc.height = 4;
  dafg::intermediate::ScheduledResource scheduled;
  scheduled.description = ResourceDescription{texDesc};
  scheduled.resourceType = dafg::ResourceType::Texture;
  scheduled.clearStage = dafg::intermediate::ClearStage::None;
  scheduled.clearValue = ResourceClearValue{};
  scheduled.clearFlags = RESOURCE_CLEAR_ALL_CONTENT;
  scheduled.history = dafg::History::No;
  scheduled.autoMipCount = false;
  auto [texIdx, texRes] = graph.resources.appendNew();
  const ResourceIndex tex = texIdx;
  texRes.resource = eastl::move(scheduled);
  texRes.multiplexingIndex = dafg::intermediate::MultiplexingIndex{0};
  texRes.frontendResources.push_back(static_cast<dafg::ResNameId>(0));
  graph.resourceNames.emplaceAt(tex, "tex");

  const auto addNode = [&graph, tex](uint32_t idx, const char *name, dafg::intermediate::ResourceUsage usage) {
    auto *n = graph.nodes.emplaceAt(node(idx));
    n->multiplexingIndex = dafg::intermediate::MultiplexingIndex{0};
    n->resourceRequests.push_back({tex, usage, false});
    graph.nodeStates.emplaceAt(node(idx));
    graph.nodeNames.emplaceAt(node(idx), name);
  };
  addNode(0, "producer", {dafg::Usage::COLOR_ATTACHMENT, dafg::Access::READ_WRITE, dafg::Stage::PS});
  addNode(1, "consumer", {dafg::Usage::SHADER_RESOURCE, dafg::Access::READ_ONLY, dafg::Stage::PS});
  graph.nodes[node(1)].predecessors.insert(node(0));
  graph.nodes.emplaceAt(node(2))->predecessors.insert(node(1));
  graph.nodes[node(2)].multiplexingIndex = dafg::intermediate::MultiplexingIndex{0};
  graph.nodeStates.emplaceAt(node(2));
  graph.nodeNames.emplaceAt(node(2), "sentinel");

  dafg::PassColoring coloring(3, dafg::PassColor{0});
  coloring[node(1)] = dafg::PassColor{1};
  coloring[node(2)] = dafg::PassColor{2};

  dafg::ResourceLifetimeCalculator lifetimeCalculator;
  dafg::BarrierScheduler scheduler;
  dafg::BarrierScheduler::EventsCollection events;

  IdIndexedFlags<NodeIndex, framemem_allocator> nodesChanged(3, true);
  IdIndexedFlags<ResourceIndex, framemem_allocator> allResources(1, true);
  IdIndexedFlags<ResourceIndex, framemem_allocator> noResources(1, false);

  const auto lifetimesChanged = lifetimeCalculator.recalculate(graph, coloring);
  scheduler.scheduleEvents(events, graph, lifetimeCalculator.lifetimes(), coloring, nodesChanged, allResources, allResources,
    lifetimesChanged);
  REQUIRE(has_barrier(events, tex, RB_RO_SRV));
  REQUIRE(!has_barrier(events, tex, RB_RO_COPY_SOURCE));

  graph.nodes[node(1)].resourceRequests.front().usage = {dafg::Usage::COPY, dafg::Access::READ_ONLY, dafg::Stage::TRANSFER};
  nodesChanged.assign(3, false);
  const auto lifetimesUnchanged = lifetimeCalculator.recalculate(graph, coloring);
  REQUIRE((lifetimesUnchanged.trueKeys().begin() == lifetimesUnchanged.trueKeys().end()));
  scheduler.scheduleEvents(events, graph, lifetimeCalculator.lifetimes(), coloring, nodesChanged, noResources, allResources,
    lifetimesUnchanged);
  CHECK(has_barrier(events, tex, RB_RO_COPY_SOURCE));
  CHECK(!has_barrier(events, tex, RB_RO_SRV));
}
