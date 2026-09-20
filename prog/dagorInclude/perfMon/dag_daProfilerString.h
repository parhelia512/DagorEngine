//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <perfMon/dag_timeProfilerEnabled.h>

#if TIME_PROFILER_ENABLED
#define DAPROFILER_STRING(s) s
#else
#define DAPROFILER_STRING(s) ((const char *)nullptr)
#endif
