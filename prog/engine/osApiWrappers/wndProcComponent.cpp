// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <osApiWrappers/dag_wndProcComponent.h>
#include <osApiWrappers/dag_atomic.h>

static constexpr int MAX_COMP_NUM = 32;
static IWndProcComponent *comp[MAX_COMP_NUM] = {0};

static int find_component(IWndProcComponent *c)
{
  for (int i = 0; i < MAX_COMP_NUM; i++)
    if (comp[i] == c)
      return i;
  return -1;
}

void add_wnd_proc_component(IWndProcComponent *c)
{
  if (find_component(c) != -1)
    return;

  for (int i = 0; i < MAX_COMP_NUM; i++)
    if (!comp[i])
    {
      comp[i] = c;
      return;
    }
}
void del_wnd_proc_component(IWndProcComponent *c)
{
  int id = find_component(c);
  if (id != -1)
    comp[id] = nullptr;
}

static bool is_inside_wnd_proc_components = false;
// A counter, not a snapshot of comp[]: the fatal message box suspends from its own thread while the
// main thread continues to add and remove components.
static int suspend_count = 0;

bool perform_wnd_proc_components(void *hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t &result)
{
  if (is_inside_wnd_proc_components || interlocked_acquire_load(suspend_count) > 0)
    return false;

  is_inside_wnd_proc_components = true;
  struct OnReturn
  {
    ~OnReturn() { is_inside_wnd_proc_components = false; }
  } onReturn;

  IWndProcComponent::RetCode ret;

  for (int i = 0; i < MAX_COMP_NUM; i++)
    if (comp[i])
    {
      ret = comp[i]->process(hwnd, msg, wParam, lParam, result);
      if (ret == IWndProcComponent::IMMEDIATE_RETURN)
        return true;
      if (ret == IWndProcComponent::PROCEED_DEF_WND_PROC)
        return false;
    }

  return false;
}

void suspend_wnd_proc_components() { interlocked_increment(suspend_count); }

void resume_wnd_proc_components() { interlocked_decrement(suspend_count); }

#define EXPORT_PULL dll_pull_osapiwrappers_wndProcComponent
#include <supp/exportPull.h>
