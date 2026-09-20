// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "graph_document.h"

#include <EditorCore/ec_interface.h>
#include <libTools/util/undo.h>

#include "graph_panel.h"
#include "graph_undo.h"
#include "graph_validation.h"
#include "plugin.h"

#include "pluginService/graph_tex_gen_service.h"

#include <EASTL/algorithm.h>
#include <EASTL/hash_set.h>
#include <EASTL/sort.h>

namespace
{
// The graph-level texture fields seed every node's texture declaration in compile_graph_to_blks, so
// one of them moving means the graph has to be rebuilt. Nothing else in GraphSettings reaches it.
bool compile_inputs_differ(const GraphSettings &a, const GraphSettings &b)
{
  return a.graphTextureWidth != b.graphTextureWidth || a.graphTextureHeight != b.graphTextureHeight ||
         a.graphTextureDepth != b.graphTextureDepth || a.graphTextureType != b.graphTextureType ||
         a.graphTextureWrap != b.graphTextureWrap;
}

bool heightmap_differs(const GraphSettings &a, const GraphSettings &b)
{
  return a.heightmapScale != b.heightmapScale || a.heightmapMin != b.heightmapMin || a.heightmapCellSize != b.heightmapCellSize;
}
} // namespace

GraphDocument::GraphDocument(GraphEditorPlg &plg) : plugin(plg) {}

void GraphDocument::reinsertNode(const GraphData::Node &node)
{
  if (view)
  {
    view->addNode(node);
  }
  else
  {
    mutateGraphData([&node](GraphData &gd) { gd.nodes.push_back(node); });
  }
  plugin.markGraphDirtyAndRegen();
}

void GraphDocument::reinsertEdge(const GraphData::Edge &edge)
{
  if (view)
  {
    view->addEdge(edge); // mutates + marks dirty + cull
  }
  else
  {
    mutateGraphData([&edge](GraphData &gd) { gd.edges.push_back(edge); });
    plugin.markGraphDirtyAndRegen();
  }
}

void GraphDocument::eraseNode(int node_id)
{
  if (view)
  {
    view->removeNodeById(node_id); // strips incident edges + regens internally
    return;
  }
  // Graph panel closed: mutate the canonical store directly so undo stays correct with no canvas.
  bool erased = false;
  mutateGraphData([node_id, &erased](GraphData &gd) { erased = erase_node_and_incident_edges(gd, node_id); });
  if (erased)
  {
    plugin.markGraphDirtyAndRegen();
  }
}

void GraphDocument::eraseEdge(int edge_id)
{
  if (view)
  {
    view->removeEdgeById(edge_id); // mutates + marks dirty + cull
    return;
  }
  // Graph panel closed: mutate the canonical store directly so undo stays correct with no canvas.
  bool erased = false;
  mutateGraphData([edge_id, &erased](GraphData &gd) { erased = erase_edge(gd, edge_id); });
  if (erased)
  {
    plugin.markGraphDirtyAndRegen();
  }
}

void GraphDocument::eraseNodes(const eastl::vector<int> &node_ids)
{
  for (int id : node_ids)
  {
    eraseNode(id);
  }
}

void GraphDocument::restoreNodesAndEdges(const eastl::vector<GraphData::Node> &nodes, const eastl::vector<GraphData::Edge> &edges)
{
  // Nodes first, then edges, so a restored edge never references a not-yet-present node.
  for (const GraphData::Node &n : nodes)
  {
    reinsertNode(n);
  }
  for (const GraphData::Edge &e : edges)
  {
    reinsertEdge(e);
  }
}

void GraphDocument::applyNodePositions(const eastl::vector<NodePos> &positions)
{
  if (positions.empty())
  {
    return;
  }
  mutateGraphData([&positions](GraphData &gd) {
    for (const NodePos &p : positions)
    {
      for (GraphData::Node &n : gd.nodes)
      {
        if (n.id == p.nodeId)
        {
          n.x = p.x;
          n.y = p.y;
          break;
        }
      }
    }
  });
  if (view)
  {
    eastl::vector<int> ids;
    ids.reserve(positions.size());
    for (const NodePos &p : positions)
    {
      ids.push_back(p.nodeId);
    }
    view->markPositionsPending(ids);
  }
  // Node position is display-only -- do not markGraphDirtyAndRegen.
}

void GraphDocument::applyBlockSizes(const eastl::vector<BlockSize> &sizes)
{
  mutateGraphData([&](GraphData &gd) {
    for (const BlockSize &bs : sizes)
    {
      for (GraphData::Node &n : gd.nodes)
      {
        if (n.id == bs.nodeId)
        {
          n.blockWidth = bs.width;
          n.blockHeight = bs.height;
          break;
        }
      }
    }
  });
  // The cull cache holds the pre-undo bounds; draw_block_node pushes the restored size to ne itself.
  if (view)
  {
    view->markBlockSizesChanged();
  }
  // Block size is display-only; no regen needed.
}

void GraphDocument::applySelection(const GraphSelection &selection)
{
  if (view)
  {
    view->setPendingSelection(selection);
  }
}

void GraphDocument::getNodeProperties(int node_id, eastl::vector<eastl::pair<eastl::string, eastl::string>> &out) const
{
  out.clear();
  // Main-thread read of node data (see the snapshot note in deleteNodes).
  if (const GraphData::Node *const node = find_node_by_id(graphData, node_id))
  {
    out = node->propertyValues;
  }
}

void GraphDocument::applyNodeProperties(int node_id, const eastl::vector<eastl::pair<eastl::string, eastl::string>> &props)
{
  bool found = false;
  mutateGraphData([&](GraphData &gd) {
    for (auto &node : gd.nodes)
    {
      if (node.id == node_id)
      {
        node.propertyValues = props;
        found = true;
        break;
      }
    }
  });
  if (found)
  {
    plugin.markGraphDirtyAndRegen();
  }
  // The displayed controls still show the pre-undo values; force the panel to rebuild them.
  plugin.invalidatePropertiesPanel();
}

void GraphDocument::getGraphSettings(GraphSettings &out) const
{
  // Main-thread read of graph data (see the snapshot note in deleteNodes).
  out.renderDir = graphData.renderDir;
  out.entityDir = graphData.entityDir;
  out.heightmapScale = graphData.heightmapScale;
  out.heightmapMin = graphData.heightmapMin;
  out.heightmapCellSize = graphData.heightmapCellSize;
  out.graphTextureWidth = graphData.graphTextureWidth;
  out.graphTextureHeight = graphData.graphTextureHeight;
  out.graphTextureDepth = graphData.graphTextureDepth;
  out.graphTextureType = graphData.graphTextureType;
  out.graphTextureWrap = graphData.graphTextureWrap;
}

void GraphDocument::pushHeightmapParams()
{
  IGraphTexGenService *texGen = plugin.getTexGenService();
  if (!texGen)
  {
    return;
  }
  texGen->setHeightmapParams(effective_height(graphData.heightmapScale, DEFAULT_HEIGHT_SCALE),
    effective_height(graphData.heightmapMin, DEFAULT_HEIGHT_MIN), effective_height(graphData.heightmapCellSize, DEFAULT_CELL_SIZE));
}

void GraphDocument::writeGraphSettings(const GraphSettings &settings)
{
  GraphSettings before;
  getGraphSettings(before);
  mutateGraphData([&](GraphData &gd) {
    gd.renderDir = settings.renderDir;
    gd.entityDir = settings.entityDir;
    gd.heightmapScale = settings.heightmapScale;
    gd.heightmapMin = settings.heightmapMin;
    gd.heightmapCellSize = settings.heightmapCellSize;
    gd.graphTextureWidth = settings.graphTextureWidth;
    gd.graphTextureHeight = settings.graphTextureHeight;
    gd.graphTextureDepth = settings.graphTextureDepth;
    gd.graphTextureType = settings.graphTextureType;
    gd.graphTextureWrap = settings.graphTextureWrap;
  });
  pushHeightmapParams();
  // Which fields moved decides the work, not which control was edited. The export dirs are read when
  // textures are saved, so a change to one needs neither pass.
  if (compile_inputs_differ(before, settings))
  {
    plugin.markGraphDirtyAndRegen();
  }
  else if (heightmap_differs(before, settings))
  {
    if (IGraphTexGenService *texGen = plugin.getTexGenService())
    {
      texGen->requestRegenerate();
    }
  }
}

void GraphDocument::applyGraphSettings(const GraphSettings &settings)
{
  writeGraphSettings(settings);
  plugin.invalidatePropertiesPanel();
}

void GraphDocument::setGraphSettings(const GraphSettings &settings)
{
  GraphSettings before;
  getGraphSettings(before);
  if (settings == before)
  {
    return;
  }
  writeGraphSettings(settings);
  recordGraphSettingsChange(eastl::move(before));
}

void GraphDocument::applyPinComment(int node_id, int pin_index, const eastl::string &comment)
{
  mutateGraphData([&](GraphData &gd) {
    for (GraphData::Node &n : gd.nodes)
    {
      if (n.id == node_id && pin_index >= 0 && pin_index < static_cast<int>(n.pins.size()))
      {
        n.pins[pin_index].comment = comment;
        break;
      }
    }
  });
  // Display-only annotation: the canvas re-reads graphData each frame, so no regen is needed.
}

void GraphDocument::applyEdgeMuted(int edge_id, bool muted)
{
  bool changed = false;
  mutateGraphData([edge_id, muted, &changed](GraphData &gd) {
    auto it = eastl::find_if(gd.edges.begin(), gd.edges.end(), [edge_id](const GraphData::Edge &e) { return e.id == edge_id; });
    if (it != gd.edges.end() && it->muted != muted)
    {
      it->muted = muted;
      changed = true;
    }
  });
  if (!changed)
  {
    return;
  }
  // Endpoints did not move, so the cull cache stays valid -- but the compile result does not.
  // The canvas picks up the new dead-path set through the graph revision bumped above.
  plugin.markGraphDirtyAndRegen();
}

// The selection as of frame start, or null with the canvas closed.
const GraphSelection *GraphDocument::recordedSelection() const { return view ? &view->getRecordedSelection() : nullptr; }

// Folds this edit's selection change into the operation being recorded, so one Ctrl+Z covers both the
// edit and the reselection. Must be called between begin() and accept().
void GraphDocument::dropErasedLinksFromSelection(const eastl::vector<GraphData::Edge> &erased)
{
  const GraphSelection *const oldSel = recordedSelection();
  if (!oldSel || erased.empty())
  {
    return;
  }

  eastl::hash_set<int> erasedIds;
  erasedIds.reserve(erased.size());
  for (const GraphData::Edge &e : erased)
  {
    erasedIds.insert(e.id);
  }
  GraphSelection newSel;
  newSel.nodes = oldSel->nodes;
  for (int id : oldSel->links)
  {
    if (erasedIds.find(id) == erasedIds.end())
    {
      newSel.links.push_back(id);
    }
  }
  foldSelectionChange(eastl::move(newSel));
}

void GraphDocument::foldSelectionChange(GraphSelection new_selection)
{
  G_ASSERT(EDITORCORE->getUndoSystem()->is_holding());
  if (!view)
  {
    return;
  }
  const GraphSelection &oldSel = view->getRecordedSelection();
  if (oldSel != new_selection)
  {
    EDITORCORE->getUndoSystem()->put<UndoSelection>(*this, oldSel, eastl::move(new_selection));
  }
  view->suppressSelectionUndoThisFrame();
}

void GraphDocument::deleteNodes(const eastl::vector<int> &node_ids)
{
  if (node_ids.empty())
  {
    return;
  }

  // Snapshot the removed sub-graph BEFORE erasing: the nodes plus every edge incident to any of
  // them, so undo can restore the exact connections. This is a main-thread READ of node/edge data,
  // which only the main thread ever writes (always under mutateGraphData). The texgen worker takes
  // graphData by const ref and writes only the compiled-output BLKs, so at most it reads these
  // concurrently -- and read-vs-read is not a race -- so no graph mutex is needed for the snapshot.
  eastl::hash_set<int> idSet;
  idSet.reserve(node_ids.size());
  for (int id : node_ids)
  {
    idSet.insert(id);
  }
  eastl::vector<GraphData::Node> removedNodes;
  eastl::vector<GraphData::Edge> removedEdges;
  removedNodes.reserve(node_ids.size());
  for (const GraphData::Node &n : graphData.nodes)
  {
    if (idSet.find(n.id) != idSet.end())
    {
      removedNodes.push_back(n);
    }
  }
  for (const GraphData::Edge &e : graphData.edges)
  {
    if (idSet.find(e.elemA) != idSet.end() || idSet.find(e.elemB) != idSet.end())
    {
      removedEdges.push_back(e);
    }
  }
  if (removedNodes.empty())
  {
    return;
  }

  eraseNodes(node_ids);

  // Incident-edge ids (for the link-selection fold below), captured before removedEdges is moved.
  eastl::hash_set<int> removedEdgeIds;
  removedEdgeIds.reserve(removedEdges.size());
  for (const GraphData::Edge &e : removedEdges)
  {
    removedEdgeIds.insert(e.id);
  }

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  undoSystem->put<UndoDeleteNodes>(*this, eastl::move(removedNodes), eastl::move(removedEdges));
  if (const GraphSelection *oldSel = recordedSelection())
  {
    GraphSelection newSel;
    for (int id : oldSel->nodes)
    {
      if (idSet.find(id) == idSet.end())
      {
        newSel.nodes.push_back(id);
      }
    }
    for (int id : oldSel->links)
    {
      if (removedEdgeIds.find(id) == removedEdgeIds.end())
      {
        newSel.links.push_back(id);
      }
    }
    foldSelectionChange(eastl::move(newSel));
  }
  undoSystem->accept("Delete nodes");
}

void GraphDocument::collectReplacedEdges(int node_id, int pin_index, int except_edge_id, eastl::vector<GraphData::Edge> &out) const
{
  const GraphData::Pin *const pin = find_pin(graphData, node_id, pin_index);
  if (!pin || !pin->singleConnect)
  {
    return;
  }

  eastl::vector<PinEdge> pinEdges;
  collect_pin_edges(graphData, node_id, pin_index, pinEdges);
  for (const PinEdge &pinEdge : pinEdges)
  {
    // Callers ask about both ends, so an edge between two single-connect pins must not list twice.
    const bool listed = eastl::any_of(out.begin(), out.end(), [&](const GraphData::Edge &e) { return e.id == pinEdge.edgeId; });
    if (pinEdge.edgeId == except_edge_id || listed)
    {
      continue;
    }
    if (const GraphData::Edge *const e = find_edge_by_id(graphData, pinEdge.edgeId))
    {
      out.push_back(*e);
    }
  }
}

void GraphDocument::recordConnectEdge(GraphData::Edge edge)
{
  // hideSingleConnections lets the drag through by hiding the incumbent of either end, so both ends
  // are asked here. Without this the pin keeps two edges and the single-driver contract is a lie.
  eastl::vector<GraphData::Edge> replacedEdges;
  collectReplacedEdges(edge.elemA, edge.pinA, /*except_edge_id=*/-1, replacedEdges);
  collectReplacedEdges(edge.elemB, edge.pinB, /*except_edge_id=*/-1, replacedEdges);

  for (const GraphData::Edge &replaced : replacedEdges)
  {
    eraseEdge(replaced.id);
  }
  reinsertEdge(edge);

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  // Apply order; undo replays it backwards, so the evicted edge comes back last.
  if (!replacedEdges.empty())
  {
    dropErasedLinksFromSelection(replacedEdges);
    undoSystem->put<UndoDeleteEdges>(*this, replacedEdges);
  }
  undoSystem->put<UndoCreateEdge>(*this, edge);
  undoSystem->accept("Create edge");
}

void GraphDocument::deleteEdges(const eastl::vector<int> &edge_ids)
{
  if (edge_ids.empty())
  {
    return;
  }

  // Snapshot the edges before erasing so undo can restore them. Main-thread read of edge data (see
  // the snapshot note in deleteNodes).
  eastl::vector<GraphData::Edge> removedEdges;
  removedEdges.reserve(edge_ids.size());
  for (int id : edge_ids)
  {
    if (const GraphData::Edge *const edge = find_edge_by_id(graphData, id))
    {
      removedEdges.push_back(*edge);
    }
  }
  if (removedEdges.empty())
  {
    return;
  }

  for (int id : edge_ids)
  {
    eraseEdge(id);
  }

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  dropErasedLinksFromSelection(removedEdges);
  undoSystem->put<UndoDeleteEdges>(*this, eastl::move(removedEdges));
  undoSystem->accept("Delete edges");
}

void GraphDocument::toggleEdgeMuted(int edge_id)
{
  // Main-thread read of edge data (see the snapshot note in deleteNodes).
  const GraphData::Edge *const edge = find_edge_by_id(graphData, edge_id);
  if (!edge)
  {
    return;
  }
  const bool oldMuted = edge->muted;

  applyEdgeMuted(edge_id, !oldMuted);

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  undoSystem->put<UndoToggleEdgeMuted>(*this, edge_id, oldMuted);
  undoSystem->accept(oldMuted ? "Unmute edge" : "Mute edge");
}

void GraphDocument::setEdgesMuted(const eastl::vector<int> &edge_ids, bool muted)
{
  // Main-thread read of edge data (see the snapshot note in deleteNodes). Edges already in
  // the wanted state are skipped, or undo would restore ones this never touched.
  eastl::vector<int> wanted(edge_ids);
  eastl::sort(wanted.begin(), wanted.end());

  eastl::vector<int> changed;
  changed.reserve(wanted.size());
  for (const GraphData::Edge &e : graphData.edges)
  {
    if (e.muted != muted && eastl::binary_search(wanted.begin(), wanted.end(), e.id))
    {
      changed.push_back(e.id);
    }
  }
  if (changed.empty())
  {
    return;
  }

  // One mutation and one regen for the batch: applyEdgeMuted per edge would bump the graph revision
  // and wake the compile worker once per edge.
  // Selected by the same test that built `changed`, off the sorted `wanted`: two filters that have
  // to agree would be one more thing to keep in step.
  mutateGraphData([&wanted, muted](GraphData &gd) {
    for (GraphData::Edge &e : gd.edges)
    {
      if (e.muted != muted && eastl::binary_search(wanted.begin(), wanted.end(), e.id))
      {
        e.muted = muted;
      }
    }
  });
  plugin.markGraphDirtyAndRegen();

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  for (int edgeId : changed)
  {
    undoSystem->put<UndoToggleEdgeMuted>(*this, edgeId, !muted);
  }
  undoSystem->accept(muted ? "Mute edges" : "Unmute edges");
}

void GraphDocument::setPinComment(int node_id, int pin_index, const eastl::string &new_comment)
{
  // Read the current comment as the undo's old value (main-thread read; see deleteNodes).
  eastl::string oldComment;
  bool found = false;
  const GraphData::Node *const node = find_node_by_id(graphData, node_id);
  if (node && pin_index >= 0 && pin_index < static_cast<int>(node->pins.size()))
  {
    oldComment = node->pins[pin_index].comment;
    found = true;
  }
  if (!found || oldComment == new_comment)
  {
    return;
  }

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  undoSystem->put<UndoPinComment>(*this, node_id, pin_index, oldComment, new_comment);
  applyPinComment(node_id, pin_index, new_comment);
  undoSystem->accept("Edit pin comment");
}

void GraphDocument::setNodeProperty(int node_id, const eastl::string &prop_name, eastl::string value)
{
  if (!find_node_by_id(graphData, node_id))
  {
    return;
  }

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  undoSystem->put<UndoNodeProps>(*this, node_id);

  // Re-find the node inside mutateGraphData so the mutable reference comes from the locked-access
  // path. node_id is stable across the lambda; main is the only writer that could remove a node.
  mutateGraphData([&](GraphData &gd) {
    for (auto &node : gd.nodes)
    {
      if (node.id != node_id)
      {
        continue;
      }
      bool found = false;
      for (auto &pv : node.propertyValues)
      {
        if (pv.first == prop_name)
        {
          pv.second = eastl::move(value);
          found = true;
          break;
        }
      }
      if (!found)
      {
        node.propertyValues.emplace_back(prop_name, eastl::move(value));
      }
      break;
    }
  });

  plugin.markGraphDirtyAndRegen();
  undoSystem->accept("Change property");
}

void GraphDocument::createNode(GraphData::Node node)
{
  reinsertNode(node);

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  undoSystem->put<UndoCreateNode>(*this, eastl::move(node));
  undoSystem->accept("Create node");
}

void GraphDocument::recordPaste(eastl::vector<GraphData::Node> pasted_nodes, eastl::vector<GraphData::Edge> pasted_edges,
  const char *undo_name)
{
  if (pasted_nodes.empty())
  {
    return;
  }

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  for (const GraphData::Node &n : pasted_nodes)
  {
    undoSystem->put<UndoCreateNode>(*this, n);
  }
  for (const GraphData::Edge &e : pasted_edges)
  {
    undoSystem->put<UndoCreateEdge>(*this, e);
  }
  // Paste clears the selection and selects the pasted nodes; fold that so undo restores the prior
  // selection (and removes the nodes) while redo reselects the paste.
  GraphSelection newSel;
  newSel.nodes.reserve(pasted_nodes.size());
  for (const GraphData::Node &n : pasted_nodes)
  {
    newSel.nodes.push_back(n.id);
  }
  foldSelectionChange(eastl::move(newSel));
  undoSystem->accept(undo_name);
}

int GraphDocument::recordAddConnectedNode(GraphData::Node node, int source_node, int source_pin)
{
  const int nodeId = node.id;

  // Before the pin is chosen: validate_new_edge is a whole-graph solver. Also regenerates, which is
  // what covers the unconnected case.
  reinsertNode(node);

  const int targetPin = find_connectable_pin(graphData, source_node, source_pin, nodeId);

  const GraphData::Pin *const srcPin = find_pin(graphData, source_node, source_pin);
  const bool srcIsOutput = srcPin && srcPin->role == PinRole::Out;

  // Only the source can hold one: the target pin belongs to the node just inserted.
  eastl::vector<GraphData::Edge> replacedEdges;
  if (targetPin >= 0)
  {
    collectReplacedEdges(source_node, source_pin, /*except_edge_id=*/-1, replacedEdges);
  }

  GraphData::Edge added;
  bool haveEdge = false;
  if (targetPin >= 0)
  {
    // Stored orientation is always (Out -> In), as in the load path.
    added.id = next_edge_id(graphData);
    added.elemA = srcIsOutput ? source_node : nodeId;
    added.pinA = srcIsOutput ? source_pin : targetPin;
    added.elemB = srcIsOutput ? nodeId : source_node;
    added.pinB = srcIsOutput ? targetPin : source_pin;
    haveEdge = true;
  }

  for (const GraphData::Edge &edge : replacedEdges)
  {
    eraseEdge(edge.id);
  }
  if (haveEdge)
  {
    reinsertEdge(added);
  }

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  // Put in apply order; undo replays it backwards, so the evicted edge comes back last.
  if (!replacedEdges.empty())
  {
    undoSystem->put<UndoDeleteEdges>(*this, replacedEdges);
  }
  undoSystem->put<UndoCreateNode>(*this, eastl::move(node));
  if (haveEdge)
  {
    undoSystem->put<UndoCreateEdge>(*this, added);
  }
  GraphSelection newSel;
  newSel.nodes.push_back(nodeId);
  foldSelectionChange(eastl::move(newSel));
  undoSystem->accept("Add node");

  return targetPin;
}

int GraphDocument::recordInsertNodeOnPin(GraphData::Node node, int anchor_node, int anchor_pin, int anchor_edge_id)
{
  // A stale anchor leaves the feed pointing at nothing, which find_connectable_pin refuses below --
  // so it lands on the unconnected-create path rather than returning a node that was never made.
  SpliceEnds ends;
  splice_ends(graphData, anchor_node, anchor_pin, anchor_edge_id, ends);

  const int nodeId = node.id;

  reinsertNode(node); // before any pin is chosen: the two solvers below are whole-graph

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  // An input, except on an In anchor with no driver: there the anchor feeds and takes an output.
  const int wiredPin = find_connectable_pin(graphData, ends.feed.node, ends.feed.pin, nodeId);
  if (wiredPin < 0)
  {
    // Keeping the node beats discarding what the user dropped; every edge stays as it was.
    undoSystem->begin();
    undoSystem->put<UndoCreateNode>(*this, eastl::move(node));
    GraphSelection unconnectedSel;
    unconnectedSel.nodes.push_back(nodeId);
    foldSelectionChange(eastl::move(unconnectedSel));
    undoSystem->accept("Add node");
    return -1;
  }

  const GraphData::Pin *const feedPinDesc = find_pin(graphData, ends.feed.node, ends.feed.pin);
  const bool feedIsOutput = feedPinDesc && feedPinDesc->role == PinRole::Out;

  eastl::vector<GraphData::Edge> erasedEdges;
  eastl::vector<GraphData::Edge> addedEdges;

  // The feed goes in before the consumers are asked: check_type_correct narrows over the whole
  // graph, so a node whose pins share a typeGroup reports its real output type only once fed.
  collectReplacedEdges(ends.feed.node, ends.feed.pin, /*except_edge_id=*/-1, erasedEdges);
  for (const GraphData::Edge &replaced : erasedEdges)
  {
    eraseEdge(replaced.id);
  }

  GraphData::Edge feed;
  feed.id = next_edge_id(graphData);
  feed.elemA = feedIsOutput ? ends.feed.node : nodeId;
  feed.pinA = feedIsOutput ? ends.feed.pin : wiredPin;
  feed.elemB = feedIsOutput ? nodeId : ends.feed.node;
  feed.pinB = feedIsOutput ? wiredPin : ends.feed.pin;
  reinsertEdge(feed);
  addedEdges.push_back(feed);

  for (const SplicePin &consumer : ends.sinks)
  {
    int outPin = -1;
    for (int i = 0; i < static_cast<int>(node.pins.size()); ++i)
    {
      if (node.pins[i].role == PinRole::Out && validate_new_edge(graphData, consumer.node, consumer.pin, nodeId, i))
      {
        outPin = i;
        break;
      }
    }
    if (outPin < 0)
    {
      continue; // keeps its edge, unless a single-connect feed pin already evicted it
    }

    if (const GraphData::Edge *const original = find_edge_by_id(graphData, consumer.edgeId))
    {
      erasedEdges.push_back(*original);
      eraseEdge(consumer.edgeId);
    }

    GraphData::Edge moved;
    moved.id = next_edge_id(graphData);
    moved.elemA = nodeId;
    moved.pinA = outPin;
    moved.elemB = consumer.node;
    moved.pinB = consumer.pin;
    moved.muted = consumer.muted; // the mute rides this hop, so a muted path stays off
    reinsertEdge(moved);
    addedEdges.push_back(moved);

    if (node.pins[outPin].singleConnect)
    {
      break; // a second would evict the first
    }
  }

  undoSystem->begin();
  if (!erasedEdges.empty())
  {
    undoSystem->put<UndoDeleteEdges>(*this, erasedEdges);
  }
  undoSystem->put<UndoCreateNode>(*this, eastl::move(node));
  for (const GraphData::Edge &added : addedEdges)
  {
    undoSystem->put<UndoCreateEdge>(*this, added);
  }
  GraphSelection newSel;
  newSel.nodes.push_back(nodeId);
  foldSelectionChange(eastl::move(newSel));
  const bool spliced = addedEdges.size() > 1; // the feed is always there; more means a wire moved
  undoSystem->accept(spliced ? "Insert node into connection" : "Add node");

  return wiredPin;
}

void GraphDocument::recordRemoveKeepingConnections(eastl::vector<GraphData::Node> removed_nodes,
  eastl::vector<GraphData::Edge> removed_edges, eastl::vector<GraphData::Edge> bridge_edges)
{
  if (removed_nodes.empty())
  {
    return;
  }

  // One UndoDeleteNodes (the removed nodes + their incident edges) followed by a UndoCreateEdge per
  // bridge edge. The holder redoes forward (delete, then add bridges) and undoes in reverse (remove
  // bridges, then restore the sub-graph), reproducing/reverting the splice exactly.
  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  undoSystem->put<UndoDeleteNodes>(*this, eastl::move(removed_nodes), eastl::move(removed_edges));
  for (const GraphData::Edge &e : bridge_edges)
  {
    undoSystem->put<UndoCreateEdge>(*this, e);
  }
  // The splice clears the selection (it removed the selected nodes); fold that so undo brings the
  // nodes back selected and redo clears again.
  foldSelectionChange(GraphSelection());
  undoSystem->accept("Remove keeping connections");
}

void GraphDocument::recordReconnectEdge(const GraphData::Edge &removed_edge, const GraphData::Edge *added_edge)
{
  // The caller dropped the rerouted edge and inserted the new one already, so only what that new
  // edge displaces is left, and it belongs in this same entry. A reroute stores a new edge rather
  // than moving one, so the anchor end takes a new connection too and both ends are asked.
  eastl::vector<GraphData::Edge> replacedEdges;
  if (added_edge)
  {
    collectReplacedEdges(added_edge->elemA, added_edge->pinA, added_edge->id, replacedEdges);
    collectReplacedEdges(added_edge->elemB, added_edge->pinB, added_edge->id, replacedEdges);
  }
  for (const GraphData::Edge &replaced : replacedEdges)
  {
    eraseEdge(replaced.id);
  }

  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  eastl::vector<GraphData::Edge> removed;
  removed.push_back(removed_edge);
  undoSystem->put<UndoDeleteEdges>(*this, eastl::move(removed));
  if (!replacedEdges.empty())
  {
    dropErasedLinksFromSelection(replacedEdges);
    undoSystem->put<UndoDeleteEdges>(*this, replacedEdges);
  }
  if (added_edge)
  {
    undoSystem->put<UndoCreateEdge>(*this, *added_edge);
  }
  undoSystem->accept("Reconnect edge");
}

void GraphDocument::recordSelectionChange(GraphSelection old_selection, GraphSelection new_selection)
{
  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  undoSystem->put<UndoSelection>(*this, eastl::move(old_selection), eastl::move(new_selection));
  undoSystem->accept("Select");
}

void GraphDocument::recordGraphSettingsChange(GraphSettings old_settings)
{
  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  undoSystem->put<UndoGraphSettings>(*this, eastl::move(old_settings));
  undoSystem->accept("Change graph settings");
}

void GraphDocument::commitNodeTransforms(eastl::vector<NodePos> old_positions, eastl::vector<NodePos> new_positions,
  eastl::vector<BlockSize> old_sizes, eastl::vector<BlockSize> new_sizes)
{
  applyNodePositions(new_positions); // commit positions to graphData + push to ne (no-op if empty)
  // Sizes are already committed to graphData live by syncBlockSizes; only the undo entry is needed.

  // Fold a corner resize (which moves and resizes the same block) into one entry, so a single Ctrl+Z
  // restores both -- the deferred SetNodePosition + SetGroupSize on undo re-anchor the block correctly.
  const bool hasResize = !old_sizes.empty();
  UndoSystem *undoSystem = EDITORCORE->getUndoSystem();
  undoSystem->begin();
  if (!old_positions.empty())
  {
    undoSystem->put<UndoMoveNodes>(*this, eastl::move(old_positions), eastl::move(new_positions));
  }
  if (hasResize)
  {
    undoSystem->put<UndoBlockResize>(*this, eastl::move(old_sizes), eastl::move(new_sizes));
  }
  undoSystem->accept(hasResize ? "Resize block" : "Move nodes");
}
