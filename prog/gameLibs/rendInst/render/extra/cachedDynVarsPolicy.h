// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include "riExtraRendererT.h"

#include <shaders/dag_shaderVarsUtils.h>
#include <shaders/dag_dynVariantsCache.h>
#include <memory/dag_framemem.h>


namespace rendinst::render
{

struct CachedDynVarsPolicy
{
  DynVariantsCache<framemem_allocator> dynVarCache;

  shaders::CombinedDynVariantState getStates(const ScriptedShaderElement &shelem) const
  {
    return get_cached_dynamic_variant_states(shelem, dynVarCache.getCache());
  }
};

} // namespace rendinst::render
