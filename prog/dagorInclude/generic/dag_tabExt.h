//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <generic/dag_tab.h>
#include <memory/dag_framemem.h>
#include <util/dag_compilerDefs.h>

template <class T>
class DAGOR_WARN_IF_UNUSED FTab : public Tab<T>
{
public:
  FTab() : Tab<T>(framemem_ptr()) {}
};

template <typename T>
using FVec = dag::Vector<T, framemem_allocator, false, uint32_t>;
