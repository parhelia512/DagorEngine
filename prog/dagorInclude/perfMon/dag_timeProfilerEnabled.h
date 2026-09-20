//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

// single source of the profiler compile-time switch; dag_statDrv.h maps it to DA_PROFILER_ENABLED
#ifndef TIME_PROFILER_ENABLED
#if _TARGET_PC || (DAGOR_DBGLEVEL != 0)
#define TIME_PROFILER_ENABLED 1
#else
#define TIME_PROFILER_ENABLED 0
#endif
#endif
