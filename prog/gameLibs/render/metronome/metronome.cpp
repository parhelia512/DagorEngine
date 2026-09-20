// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "metronome_detail.h"

#include <EASTL/sort.h>
#include <EASTL/utility.h>
#include <debug/dag_log.h>
#include <debug/dag_assert.h>
#include <generic/dag_expected.h>
#include <generic/dag_relocatableFixedVector.h>


namespace dafg::metronome
{
using detail::Scheduler;
using detail::StoredNode;
using detail::SubgraphState;

namespace
{

Scheduler &scheduler()
{
  static Scheduler instance;
  return instance;
}

enum class GetError
{
  InvalidId,
};

dag::Expected<eastl::reference_wrapper<SubgraphState>, GetError> get_subgraph(SubgraphId id)
{
  SubgraphState *sg = scheduler().subgraphs.get(id);
  if (!sg)
    return dag::Unexpected(GetError::InvalidId);
  return eastl::ref(*sg);
}

void activate(SubgraphState &sg)
{
  for (size_t i = sg.liveHandles.size(); i < sg.nodes.size(); ++i)
  {
    StoredNode &node = sg.nodes[i];
    sg.liveHandles.push_back(node.activator(node.ns, node.name.c_str(), node.sourceLocation.c_str()));
  }
  sg.state = UpdateStatus::Running;
}

} // anonymous namespace

void detail::register_node_impl(SubgraphId subgraph_id, NameSpace ns, const char *name, const char *source_location,
  detail::NodeActivator activator)
{
  get_subgraph(subgraph_id)
    .and_then([&](eastl::reference_wrapper<SubgraphState> sg) -> dag::Expected<void, GetError> {
      sg.get().nodes.push_back(StoredNode{ns, eastl::string{name}, eastl::string{source_location}, eastl::move(activator)});
      return {};
    })
    .or_else([&](GetError err) -> dag::Expected<void, GetError> {
      if (err == GetError::InvalidId)
        logerr("dafg::metronome: register_node: invalid subgraph id");
      return {};
    })
    .has_value();
}

SubgraphHandle make_subgraph(const char *name, uint32_t update_max_delay_frames)
{
  G_ASSERT(name != nullptr);
  G_ASSERT(update_max_delay_frames > 0);

  SubgraphId id = scheduler().subgraphs.emplaceOne();
  G_ASSERT(static_cast<bool>(id));

  SubgraphState *sg = scheduler().subgraphs.get(id);
  G_ASSERT(sg != nullptr);

  sg->name = name ? name : "<unnamed>";
  sg->maxDelayFrames = eastl::max(update_max_delay_frames, 1u);
  return SubgraphHandle{id};
}

SubgraphHandle::SubgraphHandle(SubgraphHandle &&other) noexcept : id{other.id} { other.id = SubgraphId{}; }

SubgraphHandle &SubgraphHandle::operator=(SubgraphHandle &&other) noexcept
{
  if (this == &other)
    return *this;

  if (valid())
    scheduler().subgraphs.destroyReference(id);

  id = other.id;
  other.id = SubgraphId{};
  return *this;
}

SubgraphHandle::~SubgraphHandle()
{
  if (valid())
    scheduler().subgraphs.destroyReference(id);
}

UpdateToken SubgraphHandle::schedule()
{
  auto sg = get_subgraph(id);
  G_ASSERT_RETURN(sg.has_value(), UpdateToken{});

  SubgraphState &s = sg.value().get();

  if (DAGOR_UNLIKELY(s.pendingSinceTick == scheduler().tick))
    logerr("dafg::metronome: schedule(): subgraph '%s' scheduled more than once in the same frame", s.name.c_str());

  ++s.scheduleGeneration;
  s.state = UpdateStatus::Pending;
  s.pendingSinceTick = scheduler().tick;

  return UpdateToken{id, s.scheduleGeneration};
}

UpdateStatus UpdateToken::status() const
{
  if (!subgraphId)
    return UpdateStatus::NotScheduled;

  SubgraphState *s = scheduler().subgraphs.get(subgraphId);
  if (!s)
    return UpdateStatus::Superseded;

  if (scheduleGeneration != s->scheduleGeneration)
    return UpdateStatus::Superseded;

  return s->state;
}

void update()
{
  Scheduler &sched = scheduler();
  ++sched.tick;

  struct PendingRequest
  {
    uint32_t deadline;         // last tick the request may be activated on
    uint32_t pendingSinceTick; // older request wins deadline ties
    uint32_t slot;             // creation order, final tie-break
    SubgraphId id;
  };
  constexpr uint32_t pendingInitialCapacity = 16;
  dag::RelocatableFixedVector<PendingRequest, pendingInitialCapacity> pending;
  for (uint32_t i = 0; i < sched.subgraphs.totalSize(); ++i)
  {
    SubgraphState *sg = sched.subgraphs.getByIdx(i);
    if (sg && sg->state == UpdateStatus::Pending)
      pending.push_back({sg->pendingSinceTick + sg->maxDelayFrames, sg->pendingSinceTick, i, sched.subgraphs.getRefByIdx(i)});
  }
  eastl::sort(pending.begin(), pending.end(), [](const PendingRequest &a, const PendingRequest &b) {
    if (a.deadline != b.deadline)
      return a.deadline < b.deadline;
    if (a.pendingSinceTick != b.pendingSinceTick)
      return a.pendingSinceTick < b.pendingSinceTick;
    return a.slot < b.slot;
  });

  if (DAGOR_UNLIKELY(pending.size() > pendingInitialCapacity))
    LOGERR_ONCE("dafg::metronome: too many pending subgraph updates, increase pendingInitialCapacity");

  // Even spread: the request at rank j (1-based, by deadline) has framesLeft
  // frames to run, so at least ceil(j / framesLeft) requests must run per
  // frame. 10 requests with a 5-frame deadline run 2 per frame.
  uint32_t runCount = 0;
  for (uint32_t j = 0; j < pending.size(); ++j)
  {
    const uint32_t framesLeft = pending[j].deadline - sched.tick + 1;
    runCount = eastl::max(runCount, (j + framesLeft) / framesLeft); // ceil((j + 1) / framesLeft)
  }

  // Subgraphs that ran in the previous frame and were not scheduled again
  // complete now.
  for (uint32_t i = 0; i < sched.subgraphs.totalSize(); ++i)
  {
    SubgraphState *sg = sched.subgraphs.getByIdx(i);
    if (!sg || sg->state != UpdateStatus::Running)
      continue;
    sg->liveHandles.clear();
    sg->state = UpdateStatus::Complete;
  }

  // Deferred requests give up their nodes until selected; a re-scheduled
  // request that does not run this frame stays pending.
  for (uint32_t j = runCount; j < pending.size(); ++j)
    sched.subgraphs.get(pending[j].id)->liveHandles.clear();

  for (uint32_t j = 0; j < runCount; ++j)
    activate(*sched.subgraphs.get(pending[j].id));
}

} // namespace dafg::metronome
