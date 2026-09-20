// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "time.h"
#include "netPrivate.h"

#include <daECS/net/time.h>
#include <osApiWrappers/dag_miscApi.h>
#include <perfMon/dag_cpuFreq.h>
#include <debug/dag_log.h>


float get_sync_time() { return (float)g_net_globals.timeMgr->getSeconds(); }
double get_sync_time_d() { return g_net_globals.timeMgr->getSeconds(); }
int get_sync_millis() { return g_net_globals.timeMgr->getMillis(); }
bool is_dummy_time() { return g_net_globals.timeMgr == &g_net_globals.dummyTime; }

int get_async_millis() { return g_net_globals.timeMgr->getAsyncMillis(); }


double advance_time(float dt, float &out_rt_dt)
{
  if (!net::is_net_lifecycle_thread())
    return get_sync_time_d();
  return g_net_globals.timeMgr->advance(dt, out_rt_dt);
}

void reset_time_mgr(ITimeManager *new_mgr)
{
  if (!net::is_net_lifecycle_thread())
  {
    if (new_mgr && new_mgr != &g_net_globals.dummyTime)
    {
      logerr("reset_time_mgr refused off the net lifecycle thread (current=%lld); dropping new mgr",
        (long long)get_current_thread_id());
      delete new_mgr;
    }
    return;
  }
  ITimeManager *prev = g_net_globals.timeMgr;
  g_net_globals.timeMgr = new_mgr ? new_mgr : &g_net_globals.dummyTime;
  if (prev && prev != &g_net_globals.dummyTime)
    delete prev;
}


ITimeManager &get_time_mgr() { return *g_net_globals.timeMgr; }


NetGlobals::~NetGlobals() { reset_time_mgr(nullptr); }
