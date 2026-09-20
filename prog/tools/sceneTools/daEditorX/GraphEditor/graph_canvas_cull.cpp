// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "graph_canvas_cull.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_node_editor.h>

#include <graphEditor/graph_data.h>

namespace ne = ax::NodeEditor;

namespace
{
// The viewport is inflated by this fraction of its size on each side. The margin keeps nodes and links
// from popping at the edge during a pan, and covers link bezier bulge beyond the endpoint node rects.
constexpr float CULL_VIEWPORT_MARGIN_FRAC = 0.5f;
// Frames to keep reculling after a pointer interaction ends, so the committed node bounds are captured.
constexpr int CULL_INTERACTION_SETTLE_FRAMES = 2;
// Below this much viewport movement (canvas units) the cache is still valid; guards against fp noise.
constexpr float CULL_VIEW_MOVE_EPS = 0.01f;
} // namespace

int GraphCanvasCull::indexOf(int node_id) const
{
  const auto it = eastl::lower_bound(indexById.begin(), indexById.end(), node_id,
    [](const eastl::pair<int, int> &entry, int id) { return entry.first < id; });
  return (it != indexById.end() && it->first == node_id) ? it->second : -1;
}

bool GraphCanvasCull::nodeRect(int node_index, ImVec2 &out_min, ImVec2 &out_max) const
{
  // update() rebuilds the cache inside the canvas pass, so a caller that runs before it can hold an
  // index from a graph that has since grown.
  if (node_index < 0 || node_index >= static_cast<int>(nodes.size()))
  {
    return false;
  }

  const NodeCull &cull = nodes[node_index];
  if (!cull.boundsKnown)
  {
    return false;
  }
  out_min = cull.rectMin;
  out_max = cull.rectMax;
  return true;
}

void GraphCanvasCull::update(const GraphData &graph, const ImVec2 &canvas_min, const ImVec2 &canvas_max, bool force_all_visible,
  eastl::hash_set<int> &pending_position_ids)
{
  const int nodeCount = static_cast<int>(graph.nodes.size());

  ImRect viewRect(ne::ScreenToCanvas(canvas_min), ne::ScreenToCanvas(canvas_max));
  {
    const ImVec2 sz = viewRect.GetSize();
    viewRect.Expand(ImVec2(sz.x * CULL_VIEWPORT_MARGIN_FRAC, sz.y * CULL_VIEWPORT_MARGIN_FRAC));
  }

  if (ImGui::IsAnyMouseDown())
  {
    settleFrames = CULL_INTERACTION_SETTLE_FRAMES;
  }
  else if (settleFrames > 0)
  {
    --settleFrames;
  }

  const bool viewMoved =
    ImFabs(viewRect.Min.x - viewMin.x) > CULL_VIEW_MOVE_EPS || ImFabs(viewRect.Min.y - viewMin.y) > CULL_VIEW_MOVE_EPS ||
    ImFabs(viewRect.Max.x - viewMax.x) > CULL_VIEW_MOVE_EPS || ImFabs(viewRect.Max.y - viewMax.y) > CULL_VIEW_MOVE_EPS;

  // pending_position_ids is drained below, so a non-empty set must let the pass run; the size mismatch
  // is a safety net for a node add / remove that did not mark the cache dirty.
  const bool recull = dirty || force_all_visible || viewMoved || settleFrames > 0 || !pending_position_ids.empty() ||
                      static_cast<int>(nodes.size()) != nodeCount;
  if (!recull)
  {
    return;
  }

  nodes.resize(nodeCount);
  indexById.clear();
  indexById.reserve(nodeCount);

  for (int ni = 0; ni < nodeCount; ++ni)
  {
    const GraphData::Node &n = graph.nodes[ni];
    indexById.push_back({n.id, ni});

    if (auto pit = pending_position_ids.find(n.id); pit != pending_position_ids.end())
    {
      ne::SetNodePosition(ne::NodeId(make_node_id(n.id)), ImVec2(n.x, n.y));
      pending_position_ids.erase(pit);
    }

    const ne::NodeId nid = ne::NodeId(make_node_id(n.id));
    const ImVec2 pos = ne::GetNodePosition(nid);
    const ImVec2 size = ne::GetNodeSize(nid);
    NodeCull &cull = nodes[ni];
    cull.rectMin = pos;
    cull.rectMax = ImVec2(pos.x + size.x, pos.y + size.y);
    // size (0,0) == never laid out (cold load / just added).
    cull.boundsKnown = (size.x > 0.0f && size.y > 0.0f);
    cull.visible = force_all_visible || !cull.boundsKnown || viewRect.Overlaps(ImRect(cull.rectMin, cull.rectMax));
    cull.needed = cull.visible;
  }

  eastl::sort(indexById.begin(), indexById.end());

  // A link can cross the viewport with neither endpoint node inside it (two nodes on opposite
  // off-screen sides). The union of their rects is a superset of the link's bounding box.
  for (const GraphData::Edge &e : graph.edges)
  {
    const int ia = indexOf(e.elemA);
    const int ib = indexOf(e.elemB);
    if (ia < 0 || ib < 0)
    {
      continue;
    }
    if (nodes[ia].needed && nodes[ib].needed)
    {
      continue;
    }
    ImRect span(nodes[ia].rectMin, nodes[ia].rectMax);
    span.Add(ImRect(nodes[ib].rectMin, nodes[ib].rectMax));
    if (viewRect.Overlaps(span))
    {
      nodes[ia].needed = true;
      nodes[ib].needed = true;
    }
  }

  viewMin = viewRect.Min;
  viewMax = viewRect.Max;
  // A force_all_visible pass is an all-visible snapshot, not the steady state -- rebuild once framing ends.
  dirty = force_all_visible;
}
