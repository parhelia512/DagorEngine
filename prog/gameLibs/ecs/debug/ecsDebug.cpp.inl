// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <daECS/core/entityManager.h>
#include <daECS/core/entitySystem.h>
#include <daECS/core/coreEvents.h>
#include <daECS/core/componentTypes.h>
#include <startup/dag_globalSettings.h>
#include <ioSys/dag_dataBlock.h>
#include <util/dag_hash.h>
#include <EASTL/vector.h>

#define debug(...) logmessage(_MAKE4C('ECS '), __VA_ARGS__)

// template ids are recycled on manager clear and defrag, so keep the name hash per id and
// re-print the name on a mismatch
static eastl::vector<uint64_t> &named_templates()
{
  static eastl::vector<uint64_t> v;
  return v;
}

static bool templ_needs_name(ecs::EntityManager &manager, ecs::EntityId eid, ecs::template_t &t, const char *&name)
{
  eastl::vector<uint64_t> &named = named_templates();
  t = manager.getEntityTemplateId(eid);
  name = manager.getTemplateName(t); // null for a just-allocated entity
  if (!name)
    return false;
  const uint64_t h = str_hash_fnv1<64>(name) | 1u;
  if (t >= named.size())
    named.resize(t + 1, uint64_t(0));
  if (named[t] == h)
    return false;
  named[t] = h;
  return true;
}

static bool should_log_destroy = true;
static int8_t force_ecs_debug_state = -1;
static inline bool force_ecs_debug()
{
  if (DAGOR_UNLIKELY(force_ecs_debug_state < 0))
    force_ecs_debug_state = dgs_get_settings()->getBool("forceEcsDebug", false);
  return force_ecs_debug_state > 0;
}

ECS_TAG(ecsDebug)
ECS_BEFORE(__first_sync_point)
ECS_ON_EVENT(ecs::EventEntityManagerBeforeClear, ecs::EventEntityManagerAfterClear)
static inline void ecs_debug_entity_clear_es(const ecs::Event &evt)
{
  // Do not log destroy on Emgr's clear
  should_log_destroy = evt.is<ecs::EventEntityManagerAfterClear>();
  if (should_log_destroy) // ids restart after the clear; make the next level name them again
    named_templates().clear();
}

ECS_TAG(ecsDebug)
ECS_BEFORE(__first_sync_point)
static inline void ecs_debug_entity_created_es(const ecs::EventEntityCreated &, ecs::EntityManager &manager, ecs::EntityId eid,
  const TMatrix *transform, const Point3 *position, const ecs::Tag *noECSDebug)
{
  if (noECSDebug && !force_ecs_debug())
    return;
  ecs::template_t t;
  const char *name;
  const bool withName = templ_needs_name(manager, eid, t, name);
  if (const Point3 *pos = transform ? &transform->getcol(3) : position)
    withName ? debug("%d: created t#%u <%s> at %@", ecs::entity_id_t(eid), t, name, *pos)
             : debug("%d: created t#%u at %@", ecs::entity_id_t(eid), t, *pos);
  else
    withName ? debug("%d: created t#%u <%s>", ecs::entity_id_t(eid), t, name) : debug("%d: created t#%u", ecs::entity_id_t(eid), t);
}

ECS_TAG(ecsDebug)
ECS_BEFORE(__first_sync_point)
static inline void ecs_debug_entity_recreated_es(const ecs::EventEntityRecreated &, ecs::EntityManager &manager, ecs::EntityId eid,
  const ecs::Tag *noECSDebug)
{
  if (noECSDebug && !force_ecs_debug())
    return;
  ecs::template_t t;
  const char *name;
  if (templ_needs_name(manager, eid, t, name))
    debug("%d: recreated as t#%u <%s>", ecs::entity_id_t(eid), t, name);
  else
    debug("%d: recreated as t#%u", ecs::entity_id_t(eid), t);
}

ECS_TAG(ecsDebug)
ECS_BEFORE(__first_sync_point)
static inline void ecs_debug_entity_destroyed_es(const ecs::EventEntityDestroyed &, ecs::EntityManager &manager, ecs::EntityId eid,
  const ecs::Tag *noECSDebug)
{
  if ((noECSDebug && !force_ecs_debug()) || !should_log_destroy)
    return;
  ecs::template_t t;
  const char *name;
  if (templ_needs_name(manager, eid, t, name))
    debug("%d: destroyed t#%u <%s>", ecs::entity_id_t(eid), t, name);
  else
    debug("%d: destroyed t#%u", ecs::entity_id_t(eid), t);
}
