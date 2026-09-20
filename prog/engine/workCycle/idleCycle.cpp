// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <supp/_platform.h>
#include <workCycle/dag_workCycle.h>
#include <workCycle/dag_gameSettings.h>
#include <startup/dag_demoMode.h>
#include <util/dag_globDef.h>
#include <util/dag_delayedAction.h>
#include <osApiWrappers/dag_wndProcComponent.h>
#include <osApiWrappers/dag_miscApi.h>
#include <osApiWrappers/dag_atomic.h>
#include <debug/dag_debug.h>
#include "workCyclePriv.h"
#include <3d/dag_lowLatency.h>
#include <startup/dag_globalSettings.h>

#if _TARGET_C4


#endif

#if _TARGET_PC_WIN | _TARGET_XBOX
#include <workCycle/threadedWindow.h>

static bool pump_messages(bool input_only)
{
  TIME_PROFILE(pump_messages);
  bool msg_processed = false;
  if (windows::process_main_thread_messages(input_only, msg_processed))
    return msg_processed;

#if _TARGET_PC_WIN
  static MSG msg;
  bool were_events = false;

  while (PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE | (input_only ? PM_QS_INPUT : 0)))
  {
    if (!GetMessageW(&msg, NULL, 0, 0))
    {
      if (!input_only)
        quit_game(msg.wParam);
    }

    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    were_events = true;
  }

  return were_events;
#else
  return msg_processed;
#endif
}
#endif

void dagor_process_sys_messages(bool input_only)
{
#if _TARGET_PC_WIN | _TARGET_XBOX
  bool were_events = pump_messages(input_only);

  if (were_events)
  {
    TIME_PROFILE(additional_perform_wnd_proc_components);
    intptr_t result;
    ::perform_wnd_proc_components(NULL, 0, 0, 0, result);
  }

#if _TARGET_C4














#endif

  if (::dgs_dont_use_cpu_in_background && !::dgs_app_active)
  {
    TIME_PROFILE(application_inactive_sleep);
    sleep_msec(100);
  }

#if _TARGET_PC_WIN
  if (dagor_demo_check_idle_timeout())
  {
    debug("QUIT GAME: demo idle timeout");
    quit_game(0, false);
  }
#endif
#elif _TARGET_PC_LINUX
  if (input_only) //== implement properly [to be usable]
    return;
  workcycle_internal::idle_loop();
#else
  (void)(input_only);
#endif
}

void dagor_idle_cycle(bool input_only, bool is_work_cycle)
{
  TIME_PROFILE(dagor_idle_cycle);
#if _TARGET_XBOX
  gdk::process_pending_suspend();
#endif
  perform_regular_actions_for_idle_cycle(is_work_cycle);
  dagor_process_sys_messages(input_only);
}
