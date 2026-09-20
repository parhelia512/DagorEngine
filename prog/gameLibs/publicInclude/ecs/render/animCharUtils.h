//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <EASTL/vector.h>
#include <generic/dag_functionRef.h>
#include <memory/dag_framemem.h>

class ShaderMaterial;
namespace AnimV20
{
class AnimcharRendComponent;
}

// the callables are only invoked during the call, never stored
bool recreate_material_with_new_params(AnimV20::AnimcharRendComponent &animchar_render,
  dag::FunctionRef<bool(const ShaderMaterial *) const> material_filter,
  dag::FunctionRef<void(ShaderMaterial *) const> shader_var_setter);

bool recreate_material_with_new_params(AnimV20::AnimcharRendComponent &animchar_render,
  dag::FunctionRef<void(ShaderMaterial *) const> shader_var_setter);

bool recreate_material_with_new_params(AnimV20::AnimcharRendComponent &animchar_render, const char *shader_name,
  dag::FunctionRef<void(ShaderMaterial *) const> shader_var_setter);

bool recreate_material_with_new_params(AnimV20::AnimcharRendComponent &animchar_render,
  const eastl::vector<const char *, framemem_allocator> &shader_names_filter,
  dag::FunctionRef<void(ShaderMaterial *) const> shader_var_setter);
