//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <daECS/core/entityManager.h>
#include <generic/dag_tab.h>
#include <EASTL/algorithm.h>
#include <string.h>

Tab<const char *> ecs_get_global_tags_context(ecs::EntityManager &mgr);

namespace bind_dascript
{
inline bool ecs_has_tag_in_mgr(const char *tag, ecs::EntityManager &mgr)
{
  if (!tag)
    return false;
  const Tab<const char *> tags = ecs_get_global_tags_context(mgr);
  auto pred = [tag](const char *str) -> bool { return strcmp(str, tag) == 0; };
  return eastl::find_if(tags.begin(), tags.end(), pred) != tags.end();
}

inline bool ecs_has_global_tag(const char *tag)
{
  if (!tag || !g_entity_mgr)
    return false;
  return ecs_has_tag_in_mgr(tag, *g_entity_mgr);
}
} // namespace bind_dascript
