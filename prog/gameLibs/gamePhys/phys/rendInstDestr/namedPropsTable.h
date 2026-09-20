// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EASTL/deque.h>
#include <util/dag_oaHashNameMap.h>
#include <debug/dag_assert.h>

// Name-keyed append-only table. Entries never move, so they are referenced by pointer until clear().
template <typename T>
class NamedPropsTable
{
  FastNameMap names;
  eastl::deque<T> items;

public:
  // init(T &) fills the new entry and returns false to drop it; an init that adds other entries must not fail afterwards
  template <typename InitFn>
  T *getOrAdd(const char *name, InitFn &&init)
  {
    const int id = names.addNameId(name);
    G_ASSERT_RETURN(unsigned(id) <= items.size(), nullptr);
    if (unsigned(id) < items.size())
      return &items[id];
    T &item = items.push_back();
    if (init(item))
      return &item;
    G_ASSERT(&item == &items.back());
    names.erase(id);
    items.pop_back();
    return nullptr;
  }

  void clear()
  {
    names.clear();
    items.clear();
  }
};
