// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <imgui/imgui.h>

#include <graphEditor/graph_data.h>

// Per-type color table, shared by every pass that draws typed graph elements.
ImU32 pin_color_for_type(PinType t);

// Same hue at reduced alpha, for a pin or edge on a dead (unreachable) path.
ImU32 dead_color_for_type(PinType t);
