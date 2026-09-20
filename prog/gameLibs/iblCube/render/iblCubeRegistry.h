// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <daECS/core/entityComponent.h>
#include <dag/dag_vectorMap.h>
#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <3d/dag_resPtr.h>
#include <drv/3d/dag_resId.h>


class IBLCubeRegistry
{
public:
  static constexpr int SLICE_SIZE = 256;

  IBLCubeRegistry();
  IBLCubeRegistry(const IBLCubeRegistry &) = delete;
  IBLCubeRegistry &operator=(const IBLCubeRegistry &) = delete;

  void acquire(const char *source);
  void release(const char *source);

  eastl::pair<const UniqueTex *, bool> getOrCreateAtlas(const eastl::string &source);
  bool isBaked(const char *source) const;
  void markBaked(const char *source);
  void resetBaked();

private:
  struct AtlasEntry
  {
    UniqueTex atlas;
    int refCount = 0;
    bool baked = false;
  };

  dag::VectorMap<eastl::string, AtlasEntry> atlases;
};

ECS_DECLARE_BOXED_TYPE(IBLCubeRegistry);
