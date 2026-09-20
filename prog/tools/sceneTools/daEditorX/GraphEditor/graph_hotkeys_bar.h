// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

struct ImVec2;

// Whether the key does something in this state, which is not always the same as its dispatch
// firing: Tab stays live while the pin jump menu handles it directly.
struct GraphHotkeyContext
{
  bool pinJumpAvailable = false;
  bool addNodeAvailable = false;
  bool addTransitNodeAvailable = false;
};

void draw_graph_hotkeys_bar(const ImVec2 &canvas_max, const GraphHotkeyContext &context);
