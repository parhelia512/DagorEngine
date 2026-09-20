// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <imgui/imgui.h>

#include <stdint.h>

struct GraphData;

class GraphPinJumpMenu
{
public:
  enum class Jump
  {
    None,       // nothing reachable from this pin
    Single,     // one destination, reported through the out params; no menu opened
    MenuOpened, // several; the chooser is up and reports the pick through draw()
  };

  // Destinations the view cannot reach -- a dangling edge, or one landing on a pin the renderer never
  // submits -- are dropped before the count, so every outcome honours the same filter.
  Jump resolveJump(const GraphData &gd, int node_id, int pin_index, uint64_t graph_revision, int &out_node, int &out_pin);

  bool isOpen() const { return menuOpen; }

  // Pin the menu is navigating from, for the caller that has to re-read its position.
  int sourceNodeId() const { return sourceNode; }
  int sourcePinIndex() const { return sourcePin; }

  // Keeps the pin being navigated from decorated as hovered while the cursor sits over the menu.
  bool isSourcePin(int node_id, int pin_index) const { return menuOpen && node_id == sourceNode && pin_index == sourcePin; }

  // Edge to draw highlighted, or -1. It trails the keyboard and the mouse by one frame: links are
  // submitted before the menu draws.
  int highlightedEdgeId() const;

  // Call between ne::Suspend() and ne::Resume(). Returns true once a row is confirmed. pin_screen_pos is
  // re-read every frame: a navigate animation or a canvas resize moves the pin under a standing menu.
  bool draw(const GraphData &gd, uint64_t graph_revision, const ImVec2 &canvas_min, const ImVec2 &canvas_max,
    const ImVec2 &pin_screen_pos, int &out_node, int &out_pin);

private:
  struct Row
  {
    int edgeId = -1;
    int node = -1; // opposite end of the edge
    int pin = -1;
    eastl::string label;
    eastl::string comment;
  };

  // The menu closes itself: on Escape, a click outside, and a fan-out that lost its choice.
  void close();

  // Fills rows with the edges touching a pin whose opposite end the view can actually go to.
  void collectRows(const GraphData &gd, int node_id, int pin_index);

  eastl::vector<Row> rows;
  int selected = 0;
  bool menuOpen = false;
  bool pendingPopupOpen = false;
  bool scrollToSelected = false;
  int sourceNode = -1;
  int sourcePin = -1;
  bool sourceIsOutput = false; // the menu grows away from that pin's edges so they stay visible
  uint64_t revision = 0;
};
