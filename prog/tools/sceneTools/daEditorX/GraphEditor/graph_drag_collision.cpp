// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "graph_drag_collision.h"

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
// Grown onto every side of a node rect, so the rule keeps twice this much between two rects.
constexpr float NODE_BBOX_MARGIN = 8.0f;

// Live positions are integral, so a real change is at least one unit.
constexpr float MOVED_THRESHOLD = 0.5f;

bool has_moved(const ImVec2 &a, const ImVec2 &b) { return ImFabs(a.x - b.x) > MOVED_THRESHOLD || ImFabs(a.y - b.y) > MOVED_THRESHOLD; }

// A node legitimately sits on top of a container or a label, so these neither block nor get tested.
bool is_annotation_node(const GraphData::Node &n) { return n.descName == "block" || n.descName == "comment"; }

// Growing one rect by twice the margin is the same test as growing both by it, so blockers stay raw.
ImRect grow_to_box(const ImVec2 &rect_min, const ImVec2 &rect_max)
{
  ImRect box(rect_min, rect_max);
  box.Expand(2.0f * NODE_BBOX_MARGIN);
  return box;
}
} // namespace

void GraphDragCollision::update(const GraphData &graph, const GraphCanvasCull &cull)
{
  const bool buttonDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if (phase == Phase::Idle)
  {
    if (!buttonDown)
    {
      return;
    }
    arm(graph, cull);
  }
  // Never on the frame arm ran: it snapshots the very rects discover would diff against, and
  // the editor moves nodes in ne::End, after this returns.
  else if (phase == Phase::Armed)
  {
    discover(graph, cull);
  }

  if (phase == Phase::Tracking)
  {
    enforce(graph, cull);
  }

  // The release frame runs no drag step of its own, so the clamp above is what the panel's release
  // scan commits -- clearing has to come after it.
  if (!buttonDown)
  {
    reset();
  }
}

void GraphDragCollision::reset()
{
  armed.clear();
  dragged.clear();
  blockers.clear();
  candidates.clear();
  phase = Phase::Idle;
}

void GraphDragCollision::arm(const GraphData &graph, const GraphCanvasCull &cull)
{
  const int nodeCount = static_cast<int>(graph.nodes.size());
  dragged.clear();
  armed.clear();
  armed.reserve(nodeCount);
  for (int ni = 0; ni < nodeCount; ++ni)
  {
    const GraphData::Node &n = graph.nodes[ni];
    ImVec2 rectMin(0.0f, 0.0f);
    ImVec2 rectMax(0.0f, 0.0f);
    const bool known = cull.nodeRect(ni, rectMin, rectMax);
    armed.push_back(ArmedNode{n.id, rectMin, ImVec2(n.x, n.y), known});
  }
  phase = Phase::Armed;
}

void GraphDragCollision::discover(const GraphData &graph, const GraphCanvasCull &cull)
{
  const int nodeCount = static_cast<int>(graph.nodes.size());
  if (static_cast<int>(armed.size()) != nodeCount)
  {
    arm(graph, cull); // the graph changed under the gesture; rebase on what it holds now
    return;
  }

  // The editor moves every node it drags by one shared offset, so they all diverge on the same
  // frame and one pass sees the whole set.
  struct MovedNode
  {
    int index;
    ImVec2 size;
  };
  eastl::vector<MovedNode> moved;
  bool movedAnnotation = false;
  for (int ni = 0; ni < nodeCount; ++ni)
  {
    const GraphData::Node &n = graph.nodes[ni];
    if (armed[ni].nodeId != n.id)
    {
      arm(graph, cull);
      return;
    }
    ImVec2 rectMin(0.0f, 0.0f);
    ImVec2 rectMax(0.0f, 0.0f);
    if (!armed[ni].known || !cull.nodeRect(ni, rectMin, rectMax) || !has_moved(rectMin, armed[ni].pos))
    {
      continue;
    }
    // The cull pass pushes an undone position to the editor just before this runs, which looks
    // exactly like a drag. A keyboard undo fires with the button still held, so tell them apart by
    // the graph position -- a drag never touches it -- and rebase on the new layout.
    if (n.x != armed[ni].docPos.x || n.y != armed[ni].docPos.y)
    {
      arm(graph, cull);
      return;
    }
    movedAnnotation = movedAnnotation || is_annotation_node(n);
    moved.push_back(MovedNode{ni, ImVec2(rectMax.x - rectMin.x, rectMax.y - rectMin.y)});
  }

  if (moved.empty())
  {
    return; // the drag snaps to a grid, so early frames move nothing
  }
  if (movedAnnotation)
  {
    phase = Phase::Abandoned;
    return;
  }

  // Sorted here rather than after `dragged` is built so the two stay index-aligned for the seeding
  // pass below.
  eastl::sort(moved.begin(), moved.end(),
    [&graph](const MovedNode &a, const MovedNode &b) { return graph.nodes[a.index].id < graph.nodes[b.index].id; });

  dragged.reserve(moved.size());
  for (const MovedNode &m : moved)
  {
    Dragged d;
    d.nodeId = graph.nodes[m.index].id;
    d.docPos = armed[m.index].docPos;
    d.lastGood = armed[m.index].pos;
    dragged.push_back(eastl::move(d));
  }
  phase = Phase::Tracking;

  // Seed from the armed position, never the one the editor proposes: the drag snaps to a 16 unit
  // grid, so by the time a move shows the node has stepped the whole minimum gap and seeding there
  // would exempt the very pair the rule exists to stop.
  buildBlockers(graph, cull);
  for (int di = 0; di < static_cast<int>(dragged.size()); ++di)
  {
    const ImVec2 &armedMin = dragged[di].lastGood;
    const ImVec2 armedMax(armedMin.x + moved[di].size.x, armedMin.y + moved[di].size.y);
    seedExemptions(dragged[di], armedMin, armedMax);
  }
}

void GraphDragCollision::enforce(const GraphData &graph, const GraphCanvasCull &cull)
{
  const int nodeCount = static_cast<int>(graph.nodes.size());
  candidates.clear();
  candidates.reserve(dragged.size());
  for (int di = 0; di < static_cast<int>(dragged.size()); ++di)
  {
    // By value: a re-arm below clears `dragged`.
    const int nodeId = dragged[di].nodeId;
    const ImVec2 docPos = dragged[di].docPos;
    const int ni = cull.indexOf(nodeId);
    ImVec2 candMin(0.0f, 0.0f);
    ImVec2 candMax(0.0f, 0.0f);
    if (ni < 0 || ni >= nodeCount || graph.nodes[ni].id != nodeId || !cull.nodeRect(ni, candMin, candMax))
    {
      arm(graph, cull);
      return;
    }
    // A rewritten graph position leaves the armed basis and the seeded exemptions describing a
    // layout that no longer exists. Rebase rather than give up: ne keeps dragging either way, and
    // an unpoliced tail would let the release commit the overlap this exists to stop.
    const GraphData::Node &n = graph.nodes[ni];
    if (n.x != docPos.x || n.y != docPos.y)
    {
      arm(graph, cull);
      return;
    }
    candidates.push_back(NodeBox{candMin, candMax, nodeId});
  }

  buildBlockers(graph, cull);

  // One violation vetoes the whole set: clamping only the blocked nodes would tear a
  // multi-selection apart.
  bool blocked = false;
  for (int di = 0; di < static_cast<int>(dragged.size()); ++di)
  {
    if (violatesAt(dragged[di], candidates[di].rectMin, candidates[di].rectMax))
    {
      blocked = true;
      break;
    }
  }

  if (blocked)
  {
    for (const Dragged &d : dragged)
    {
      ne::SetNodePosition(ne::NodeId(make_node_id(d.nodeId)), d.lastGood);
    }
    return;
  }

  for (int di = 0; di < static_cast<int>(dragged.size()); ++di)
  {
    dragged[di].lastGood = candidates[di].rectMin;
    // Only an accepted frame proves a pair separated; on a vetoed one the node never got there.
    dropClearedExemptions(cull, dragged[di], candidates[di].rectMin, candidates[di].rectMax);
  }
}

bool GraphDragCollision::isDragged(int node_id) const
{
  const auto it = eastl::lower_bound(dragged.begin(), dragged.end(), node_id, [](const Dragged &d, int id) { return d.nodeId < id; });
  return it != dragged.end() && it->nodeId == node_id;
}

void GraphDragCollision::buildBlockers(const GraphData &graph, const GraphCanvasCull &cull)
{
  const int nodeCount = static_cast<int>(graph.nodes.size());
  blockers.clear();
  blockers.reserve(nodeCount);
  for (int ni = 0; ni < nodeCount; ++ni)
  {
    const GraphData::Node &other = graph.nodes[ni];
    if (is_annotation_node(other) || isDragged(other.id))
    {
      continue;
    }
    ImVec2 otherMin(0.0f, 0.0f);
    ImVec2 otherMax(0.0f, 0.0f);
    if (cull.nodeRect(ni, otherMin, otherMax))
    {
      blockers.push_back(NodeBox{otherMin, otherMax, other.id});
    }
  }
}

bool GraphDragCollision::violatesAt(const Dragged &d, const ImVec2 &rect_min, const ImVec2 &rect_max) const
{
  const ImRect box = grow_to_box(rect_min, rect_max);
  for (const NodeBox &blocker : blockers)
  {
    if (eastl::binary_search(d.exemptIds.begin(), d.exemptIds.end(), blocker.nodeId))
    {
      continue;
    }
    if (box.Overlaps(ImRect(blocker.rectMin, blocker.rectMax)))
    {
      return true;
    }
  }
  return false;
}

void GraphDragCollision::seedExemptions(Dragged &d, const ImVec2 &rect_min, const ImVec2 &rect_max) const
{
  const ImRect box = grow_to_box(rect_min, rect_max);
  for (const NodeBox &blocker : blockers)
  {
    if (box.Overlaps(ImRect(blocker.rectMin, blocker.rectMax)))
    {
      d.exemptIds.push_back(blocker.nodeId);
    }
  }
  eastl::sort(d.exemptIds.begin(), d.exemptIds.end()); // violatesAt binary-searches these
}

void GraphDragCollision::dropClearedExemptions(const GraphCanvasCull &cull, Dragged &d, const ImVec2 &rect_min,
  const ImVec2 &rect_max) const
{
  if (d.exemptIds.empty())
  {
    return;
  }
  const ImRect box = grow_to_box(rect_min, rect_max);
  const auto separated = [&box, &cull](int other_id) {
    const int ni = cull.indexOf(other_id);
    ImVec2 otherMin(0.0f, 0.0f);
    ImVec2 otherMax(0.0f, 0.0f);
    if (ni < 0 || !cull.nodeRect(ni, otherMin, otherMax))
    {
      return true;
    }
    return !box.Overlaps(ImRect(otherMin, otherMax));
  };
  // remove_if keeps the relative order, so the list stays sorted.
  d.exemptIds.erase(eastl::remove_if(d.exemptIds.begin(), d.exemptIds.end(), separated), d.exemptIds.end());
}
