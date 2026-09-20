// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <graphEditor/graph_data.h>

#include <imgui/imgui.h>

struct DeadPaths;
struct GraphSelection;
class GraphCanvasCull;
class GraphDocument;
class GraphEdgeReconnect;
class GraphPinJumpMenu;

// What the edge pass reads, gathered once per frame. Borrows all five collaborators, one of them a
// caller local, so construct inside the ImGui frame and let it die with the pass.
struct GraphEdgeFrame
{
  GraphEdgeFrame(const GraphData &graph_data, const GraphCanvasCull &canvas_cull, const DeadPaths &dead_paths,
    const GraphSelection &graph_selection, const GraphPinJumpMenu &pin_jump_menu);

  const GraphData &graph;
  const GraphCanvasCull &canvasCull;
  const DeadPaths &deadPaths;
  const GraphSelection &selection;
  const GraphPinJumpMenu &pinJumpMenu;

  float thickness = 0.0f;
  float thicknessActive = 0.0f;
  float mutedDash = 0.0f;
};

// Type of the pin an edge endpoint lands on, or Unknown when the index pair does not resolve (a cull
// cache built against a different node list, a stale pin cache after a descriptor reload). Takes an
// index into GraphData::nodes, not a node id. Unknown maps to plain white.
PinType pin_type_at(const GraphData &graph, int node_index, int pin_index);

// Submits every edge whose endpoint nodes were declared live this frame. Between ne::Begin / ne::End.
void draw_graph_edges(const GraphEdgeFrame &frame);

struct LinkDropOnCanvas
{
  bool overEmptyCanvas = false; // this frame the drag hovers the background
  // A drop on a pin is pending: ne resolves it a frame after release, so the selection recorder has
  // to wait for the edit that follows rather than file the drag's own deselect.
  bool createInFlight = false;
  bool spawnRequested = false; // the drop was taken; the handler clears this
  int sourceNode = -1;
  int sourcePin = -1;
  bool sourceIsOutput = false;
  ImVec2 dropCanvasPos = ImVec2(0.0f, 0.0f);
};

// The ne::BeginCreate scope: a link the user is dragging.
void handle_link_create(const GraphEdgeFrame &frame, GraphDocument &doc, const GraphEdgeReconnect &edge_reconnect,
  LinkDropOnCanvas &out_drop);

// Horizontal tangents like an imgui-node-editor link, so the curve reads naturally wherever the loose
// end sits. Both points must be in the same space -- canvas-local inside ne::Begin / ne::End.
void draw_dangling_link(ImDrawList *draw_list, const ImVec2 &from, const ImVec2 &to, bool from_is_output, uint32_t color,
  float thickness);

// Call inside a ne::BeginDelete scope the caller opens; ne allows one per frame.
void handle_deleted_links(GraphDocument &doc);
