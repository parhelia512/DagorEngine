// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "pointing_classdrv.h"
#include "gameinput.h"

#include <osApiWrappers/dag_miscApi.h>


namespace HumanInput
{

void GameInputPointingClassDriver::destroy()
{
  enable(false);
  composite.setClient(nullptr);
  gameinput::shutdown();
}

void GameInputPointingClassDriver::updateDevices()
{
  if (!is_main_thread()) // skip calls from gamepad updateDevice() in polling thread
    return;
  composite.update();
}

} // namespace HumanInput
