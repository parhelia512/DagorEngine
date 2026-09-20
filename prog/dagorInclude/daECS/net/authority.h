//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <osApiWrappers/dag_atomic_types.h>
#include <osApiWrappers/dag_miscApi.h>

namespace net
{

// Which thread may mutate live net (main vs user-thread net, etc.). 0 = unset.
inline dag::AtomicInteger<int64_t> g_net_authority_tid{0};

inline bool is_net_authority_thread(bool allow_unset = false)
{
  const int64_t owner = g_net_authority_tid.load(dag::mo::acquire);
  if (owner == 0)
    return allow_unset;
  return owner == get_current_thread_id();
}

inline void set_net_authority_thread() { g_net_authority_tid.store(get_current_thread_id(), dag::mo::release); }

inline void clear_net_authority_thread() { g_net_authority_tid.store(0, dag::mo::release); }

inline bool is_net_authority_thread_or_unset() { return is_net_authority_thread(true); }
inline bool is_net_session_active() { return g_net_authority_tid.load(dag::mo::acquire) != 0; }
inline bool is_net_authority_thread_unset() { return !is_net_session_active(); }

// Mutating net (net_update, net_init, netPhys, time reset): authority tid
// if set, else main thread (tid 0 = main-thread net / no session yet).
inline bool is_net_lifecycle_thread()
{
  const int64_t owner = g_net_authority_tid.load(dag::mo::acquire);
  if (owner == 0)
    return is_main_thread();
  return owner == get_current_thread_id();
}

} // namespace net
