//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

namespace das
{
class Context;
}

namespace bind_dascript
{
bool ecs_has_tag_in_context(const char *tag, das::Context *context);
}
