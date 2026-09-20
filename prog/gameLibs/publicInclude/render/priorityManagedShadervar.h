//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <EASTL/vector_map.h>
#include <math/integer/dag_IPoint4.h>
#include <shaders/dag_shaderVar.h>
#include <EASTL/variant.h>

class PriorityManagedShadervar
{
public:
  using ShadervarUnion = eastl::variant<float, int, Point4, IPoint4>;

  PriorityManagedShadervar() = default; // Just for vector_map
  PriorityManagedShadervar(int id);
  void clear(int priority);
  template <typename T>
  void update(int priority, T value)
  {
    G_ASSERT(eastl::holds_alternative<T>(defaultValue));
    if (values.empty())
      defaultValue = get();
    values[priority] = value;
    setHighestPriority();
  }
  // Returns the value beneath any layers: the live ShaderGlobal value when no
  // layer is active, else the base captured on first set (what clear() restores).
  ShadervarUnion getBase();
  int getVarId();

private:
  int varId;
  int type;
  ShadervarUnion defaultValue;
  eastl::vector_map<int, ShadervarUnion> values;
  void setHighestPriority();
  ShadervarUnion get();
  void set(ShadervarUnion v);
};

class PriorityManagedShadervarMap
{
public:
  PriorityManagedShadervar &getEntry(int id);

private:
  eastl::vector_map<int, PriorityManagedShadervar> map;
};

namespace PriorityShadervar
{
void set_float(int id, int prio, float value);
void set_int(int id, int prio, int value);
void set_float4(int id, int prio, Point4 value);
void set_int4(int id, int prio, IPoint4 value);
void clear(int id, int prio);
float get_base_float(int id);
int get_base_int(int id);
Point4 get_base_float4(int id);
IPoint4 get_base_int4(int id);
} // namespace PriorityShadervar
