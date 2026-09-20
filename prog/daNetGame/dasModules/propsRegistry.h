// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <daScript/daScript.h>
#include <dasModules/dasModulesCommon.h>
#include <propsRegistry/propsRegistry.h>

namespace bind_dascript
{
inline int register_props(const char *filename, const char *class_name)
{
  return propsreg::register_props(filename ? filename : "", class_name ? class_name : "");
}
inline int get_props_id(const char *name, const char *class_name)
{
  return propsreg::get_props_id(name ? name : "", class_name ? class_name : "");
}
inline const char *get_props_registered_name(int props_id, const char *class_name)
{
  const char *name = propsreg::get_props_registered_name(props_id, class_name ? class_name : "");
  return name ? name : "";
}
inline bool is_props_valid(int props_id, const char *class_name)
{
  return propsreg::is_props_valid(props_id, class_name ? class_name : "");
}
inline int get_props_count(const char *class_name) { return propsreg::get_props_count(class_name ? class_name : ""); }
} // namespace bind_dascript