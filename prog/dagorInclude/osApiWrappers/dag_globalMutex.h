//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <supp/dag_define_KRNLIMP.h>

//! creates named inter-process mutex
KRNLIMP void *global_mutex_create(const char *mutex_name);

//! locks named inter-process mutex
KRNLIMP int global_mutex_enter(void *mutex, int timeout_msec = -1 /*infinite*/);

//! unlocks named inter-process mutex
KRNLIMP int global_mutex_leave(void *mutex);

//! drops this process's reference; the mutex stays alive for other processes still holding it
KRNLIMP int global_mutex_close(void *mutex);

//! retires the name, so a later create() makes a new mutex while current holders keep using this one;
//! a caller that cannot prove it is the last participant should use close() alone
KRNLIMP int global_mutex_unlink(const char *mutex_name);

//! destroys named inter-process mutex (close + unlink)
inline void global_mutex_destroy(void *mutex, const char *mutex_name)
{
  if (global_mutex_close(mutex) == 0)
    global_mutex_unlink(mutex_name);
}

//! combined create+lock
inline void *global_mutex_create_enter(const char *mutex_name)
{
  void *m = global_mutex_create(mutex_name);
  if (m)
    global_mutex_enter(m);
  return m;
}

//! combined unlock+close+unlink
inline void global_mutex_leave_destroy(void *mutex, const char *mutex_name)
{
  int ret = global_mutex_leave(mutex);
  if (ret)
    return;
  global_mutex_destroy(mutex, mutex_name);
}

#include <supp/dag_undef_KRNLIMP.h>
