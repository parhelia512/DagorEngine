// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include "nameMap.h"
#include <EASTL/bitvector.h>


struct ShaderMessages
{
  SCFastNameMap strings;

  int addMessage(const char *message)
  {
    int id = strings.addNameId(message);
    return id;
  }
};
