//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <util/dag_stdint.h>

#include <supp/dag_define_KRNLIMP.h>

class IWndProcComponent
{
public:
  enum RetCode
  {
    PROCEED_OTHER_COMPONENTS,
    PROCEED_DEF_WND_PROC,
    IMMEDIATE_RETURN,
  };

  // receives the same parameters as WindowProc (see Win32 API)
  // stores optional result in 'result' and returns one of
  // RetCode enum constants
  virtual RetCode process(void *hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t &result) = 0;
};

KRNLIMP void add_wnd_proc_component(IWndProcComponent *comp);
KRNLIMP void del_wnd_proc_component(IWndProcComponent *comp);

// Stop and restart message delivery. Registration stays live while suspended, thus components added
// or removed in that time are correct after the resume. Nestable, and safe from any thread.
KRNLIMP void suspend_wnd_proc_components();
KRNLIMP void resume_wnd_proc_components();

struct ScopeSuspendWndProcComponents
{
  ScopeSuspendWndProcComponents() { suspend_wnd_proc_components(); }
  ~ScopeSuspendWndProcComponents() { resume_wnd_proc_components(); }
};

KRNLIMP bool perform_wnd_proc_components(void *hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t &result);

#include <supp/dag_undef_KRNLIMP.h>
