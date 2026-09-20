// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EASTL/string.h>

class GraphEditorPlg;
class GraphPanel;

// The ghost that follows the cursor while a node-library drag is in flight. It has to sit over every
// panel, not just the canvas, so it draws on the hovered viewport's foreground list rather than from
// either of them.
class NodeDragOverlay
{
public:
  // Once per frame, after the panels: it reads the canvas drop target's verdict from that same frame.
  // A null graph_panel (its window closed) just means no drop can land anywhere.
  void draw(GraphEditorPlg &plugin, const GraphPanel *graph_panel);

private:
  void resolveTemplate(GraphEditorPlg &plugin, const char *template_uid);

  // Resolved once per dragged template, then reused for as long as the payload stays put.
  eastl::string uid;
  eastl::string name;
  bool hasInputs = false;
  bool hasOutputs = false;
};
