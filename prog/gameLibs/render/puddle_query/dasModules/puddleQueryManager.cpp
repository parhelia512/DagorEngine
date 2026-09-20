// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <daScript/daScriptBind.h>
#include <dasModules/aotGpuReadbackQuery.h>
#include "puddleQueryManager.h"

DAS_MODULE_DECL(PuddleQueryManager, "<dasModules/aotGpuReadbackQuery.h>", "<render/puddle_query/dasModules/puddleQueryManager.h>")
{
  addBuiltinDependency(lib, require("gpuReadbackQuery"));

  DAS_ADD_FUN_BIND("puddle_query_start", modifyExternal, bind_dascript::puddle_query_start);
  DAS_ADD_FUN_BIND("puddle_query_value", modifyArgumentAndExternal, bind_dascript::puddle_query_value);
}
