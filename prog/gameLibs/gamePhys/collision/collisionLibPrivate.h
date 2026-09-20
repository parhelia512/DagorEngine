// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <rendInst/rendInstDesc.h>
#include <ska_hash_map/flat_hash_map2.hpp>

namespace dacoll
{
class CollisionInstances;

// No-op for already listed instance and for standalone instance (not in ri_instances) which has to be updated by its owner
void push_non_empty_ri_instance_list(CollisionInstances &ci);
void remove_empty_ri_instance_list(CollisionInstances &ci);

// Disabled instances don't interact physically with other objects, e.g. when their collision is processed
// in an alternative way (physobj, physbody). Global as it's empty most of the time, but it might grow with explored area.
struct RendInstDescHash
{
  size_t operator()(const rendinst::RendInstDesc &d) const
  {
    return ((size_t(uint32_t(d.pool)) * 0x9E3779B1u) ^ uint32_t(d.idx) ^ (size_t(d.offs) << 7)) + uint32_t(d.cellIdx) + d.layer;
  }
};
// Value is ri instance idx, to drop on unregister_collision_cb
extern ska::flat_hash_map<rendinst::RendInstDesc, int, RendInstDescHash> disabled_ri_instances;

bool is_ri_instance_disabled_outofline(const rendinst::RendInstDesc &desc);
inline bool is_ri_instance_enabled(const rendinst::RendInstDesc &desc)
{
  if (!disabled_ri_instances.empty()) [[unlikely]]
    return !is_ri_instance_disabled_outofline(desc);
  return true;
}

} // namespace dacoll
