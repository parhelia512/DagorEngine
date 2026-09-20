// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EASTL/hash_set.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <imgui/imgui.h>

struct GraphData;

// Off-screen node culling for the graph canvas: a node whose canvas-space rect lies entirely outside
// the viewport is not drawn at all, which is the bulk of the saving on a large graph panned while
// zoomed in. The pass is ~O(N^2) (ne::GetNodePosition / GetNodeSize are linear lookups), so its
// result is cached and rebuilt only when an input changed.
class GraphCanvasCull
{
public:
  // Call between ne::Begin and ne::End, before the node pass; the accessors below read this frame's
  // result. canvas_min / canvas_max are screen space. force_all_visible suspends culling for a frame
  // that must measure every node.
  //
  // Also drains pending_position_ids, pushing each id's position to ne. That belongs here because the
  // pass runs for EVERY node, culled or not, so a node added off-screen still gets positioned and
  // acquires bounds.
  void update(const GraphData &graph, const ImVec2 &canvas_min, const ImVec2 &canvas_max, bool force_all_visible,
    eastl::hash_set<int> &pending_position_ids);

  // A node whose bounds are not known yet (never laid out) reports visible, so it lays out this frame.
  bool visible(int node_index) const { return nodes[node_index].visible; }
  // needed: visible, or holds an endpoint of a link whose span crosses the viewport.
  bool needed(int node_index) const { return nodes[node_index].needed; }

  // Canvas-space rect as of this frame's pass; false when the node has no bounds yet, and for an
  // index the cache does not reach.
  bool nodeRect(int node_index, ImVec2 &out_min, ImVec2 &out_max) const;

  // Index into GraphData::nodes for a node id, or -1 when the cache does not hold it.
  int indexOf(int node_id) const;

  // Every graph mutation must raise this; update detects a moved view by itself.
  void markDirty() { dirty = true; }

private:
  struct NodeCull
  {
    ImVec2 rectMin;           // node rect in canvas space (ne::GetNodePosition)
    ImVec2 rectMax;           // rectMin + ne::GetNodeSize
    bool boundsKnown = false; // until the node has laid out once the rect is degenerate, not geometry
    bool visible = false;
    bool needed = false;
  };

  eastl::vector<NodeCull> nodes;
  eastl::vector<eastl::pair<int, int>> indexById; // sorted by node id
  bool dirty = true;
  // Keeps the pass running during a pointer interaction and a few frames after: a node drag or block
  // resize moves ne bounds with no view or graph-data change, so nothing else would notice.
  int settleFrames = 0;
  // Canvas-space viewport the cache was built against; a pan / zoom / navigate animation moves it.
  ImVec2 viewMin = ImVec2(0.0f, 0.0f);
  ImVec2 viewMax = ImVec2(0.0f, 0.0f);
};
