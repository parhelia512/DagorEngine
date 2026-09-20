// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <animated_splash_screen_api.h>
#include <bindQuirrelEx/autoBind.h>
#include <ioSys/dag_dataBlock.h>

static SQInteger set_loading_screen_config(HSQUIRRELVM vm)
{
  if (sq_gettype(vm, 2) == OT_NULL)
  {
    animated_splash_screen_set_config(nullptr);
    return 0;
  }
  if (!Sqrat::check_signature<DataBlock *>(vm, 2))
    return SQ_ERROR;
  Sqrat::Var<const DataBlock *> cfg(vm, 2);
  animated_splash_screen_set_config(cfg.value);
  return 0;
}

///@module loadingScreen
SQ_DEF_AUTO_BINDING_MODULE_EX(bind_loading_screen, "loadingScreen", sq::VM_ALL)
{
  Sqrat::Table tbl(vm);
  tbl //
    .SquirrelFuncDeclString(set_loading_screen_config, "set_loading_screen_config(cfg: instance|null): null")
    ///@brief Scene config for the animated loading splash starts that follow: cfg is a DataBlock with the settings.blk
    /// loadingScreen{} layout (screens{}, default, mode, exact, chosen); null or an empty block returns to settings.blk.
    /// Keys the block lacks still come from settings.blk, so a list-only block keeps the user's mode, exact and chosen
    /// keys, though a name they pick that the config does not list falls back to its default. graphDir is not layered:
    /// naming it here is ignored. The block is copied, so a later change to cfg needs another call. Safe from any thread.
    /**/;
  return tbl;
}
