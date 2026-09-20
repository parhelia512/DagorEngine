// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "loadingUi.h"

#include <drv/3d/dag_commands.h>
#include <sqmodules/sqmodules.h>

namespace loading_ui
{

static bool fully_covering = false;

bool is_fully_covering() { return fully_covering; }
void set_fully_covering(bool o) { fully_covering = o; }

static int get_pipeline_compilation_queue_length() { return d3d::driver_command(Drv3dCommand::GET_PIPELINE_COMPILATION_QUEUE_LENGTH); }

void bind(SqModules *moduleMgr)
{
  Sqrat::Table aTable(moduleMgr->getVM());
  aTable //
    .Func("is_fully_covering", is_fully_covering)
    .Func("set_fully_covering", set_fully_covering)
    .Func("get_pipeline_compilation_queue_length", get_pipeline_compilation_queue_length)
    /**/;
  moduleMgr->addNativeModule("loading_ui", aTable);
}

} // namespace loading_ui