// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <shaders/dag_shaderVarsUtils.h>


namespace rendinst::render
{

struct OpaqueGlobalDynVarsPolicy
{
  GlobalVariableStates *globalVarsState = nullptr;

  shaders::CombinedDynVariantState getStates(const ScriptedShaderElement &shelem) const
  {
    return get_dynamic_variant_states(*globalVarsState, shelem);
  }
};

} // namespace rendinst::render
