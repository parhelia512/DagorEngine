// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <osApiWrappers/dag_atomic.h>
#include <osApiWrappers/dag_critSec.h>

#include <graphEditor/graph_data.h>

#include <EASTL/algorithm.h>
#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include "graph_edit_types.h"

class GraphEditorPlg;
class GraphPanel;

// One open graph and the lock that guards it.
class GraphDocument
{
public:
  explicit GraphDocument(GraphEditorPlg &plg);

  // The canvas showing this graph, null while its panel is closed. The plugin owns the panel and
  // must clear this before destroying it.
  void setView(GraphPanel *v) { view = v; }

  // Lock-free on purpose: main is the only writer of the source fields, and the texgen worker reads
  // them under the mutex. The compiled BLKs are not covered by that -- see graphMutex.
  const GraphData &getGraphData() const { return graphData; }

  // The only legal write path for the source fields. Without the mutex the texgen worker can read
  // a half-mutated graph.
  template <class Fn>
  void mutateGraphData(Fn &&fn)
  {
    WinAutoLock lock(graphMutex);
    fn(graphData);
    interlocked_increment(graphRevision);
  }

  // Relaxed on purpose: it gates a display-only cache, so losing a race costs one late frame.
  uint64_t getGraphRevision() const { return interlocked_relaxed_load(graphRevision); }

  // For worker-thread readers that must not race a main-thread write.
  template <class Fn>
  void readGraphData(Fn &&fn)
  {
    WinAutoLock lock(graphMutex);
    fn(static_cast<const GraphData &>(graphData));
  }

  // The service keeps this pointer for the life of the graph and writes mainGraphBlk through it,
  // so it cannot take a copy. The one mutable handle that escapes.
  GraphData *dataForTexGenService() { return &graphData; }

  // Hand-off from GraphCompilerImpl::compile on the worker thread, which is already inside
  // mutateGraphData: replaces the buffer with the most recent compile's output.
  void setPendingPinCustomTextureNames(eastl::vector<eastl::pair<int, eastl::vector<eastl::string>>> names)
  {
    pendingPinCustomTextureNames = eastl::move(names);
  }

  // Drains the buffer into nodes[].pins[].customTextureName. Must run on the main thread: the
  // texture-preview lookup (graph_panel.cpp) reads that Pin field without the mutex, so the write
  // has to originate from the same thread to keep main the only writer of nodes and pins. Called
  // once per tick from the plugin's actObjects; cheap no-op when no compile has finished. Entries
  // are keyed by node id, so a delete between compile and drain drops its entry instead of
  // corrupting whatever node now sits at that index.
  void applyPendingPinCustomTextureNames()
  {
    // Skip the lock when there's nothing to apply. Reading the size without the
    // mutex is safe-ish (worst case we miss a just-arrived batch and pick it up
    // next tick), and lets the typical no-pending-work tick avoid the
    // WinCritSec round-trip entirely.
    if (pendingPinCustomTextureNames.empty())
    {
      return;
    }
    mutateGraphData([&](GraphData &gd) {
      if (pendingPinCustomTextureNames.empty())
      {
        return;
      }
      eastl::hash_map<int, int> idToIdx;
      idToIdx.reserve(gd.nodes.size());
      for (int i = 0; i < static_cast<int>(gd.nodes.size()); ++i)
      {
        idToIdx[gd.nodes[i].id] = i;
      }
      for (auto &entry : pendingPinCustomTextureNames)
      {
        const auto it = idToIdx.find(entry.first);
        if (it == idToIdx.end())
        {
          continue; // node deleted between compile and drain
        }
        GraphData::Node &n = gd.nodes[it->second];
        const int count = static_cast<int>(eastl::min(entry.second.size(), n.pins.size()));
        for (int j = 0; j < count; ++j)
        {
          n.pins[j].customTextureName = eastl::move(entry.second[j]);
        }
      }
      pendingPinCustomTextureNames.clear();
    });
  }

  // Snapshotted by the PropertiesPanel before a graph-field edit, so this one is not undo-only.
  void getGraphSettings(GraphSettings &out) const;

  // Pushes the heightmap fields to the texgen service with the defaults substituted for the unset
  // sentinel; a raw push would feed UNSET_HEIGHT to the landscape preview.
  void pushHeightmapParams();

  // Edits that apply the change and record one undo entry for it.
  // The caller builds the node, which needs the base-node blk; this adds it and records.
  void createNode(GraphData::Node node);
  void deleteNodes(const eastl::vector<int> &node_ids);
  // Connects the two pins and evicts whatever a single-connect end already held, as one entry.
  void recordConnectEdge(GraphData::Edge edge);
  void deleteEdges(const eastl::vector<int> &edge_ids);
  void toggleEdgeMuted(int edge_id);
  // The context menu acts on a whole selection, so this cannot be a toggle.
  void setEdgesMuted(const eastl::vector<int> &edge_ids, bool muted);
  void setPinComment(int node_id, int pin_index, const eastl::string &new_comment);
  // Takes the whole settings block, so one edit gesture is one entry. No-op when nothing changed.
  void setGraphSettings(const GraphSettings &settings);
  // One entry per edit gesture: continuous controls commit once on finish, discrete ones once per
  // change. The whole propertyValues vector is snapshotted, so a freshly-set value undoes back to
  // its descriptor default instead of lingering as an explicit one.
  void setNodeProperty(int node_id, const eastl::string &prop_name, eastl::string value);

  // Edits handed over after the caller applied them, so one entry covers the whole gesture.
  // commitNodeTransforms is the exception: block sizes arrive already applied, positions do not.
  void recordPaste(eastl::vector<GraphData::Node> pasted_nodes, eastl::vector<GraphData::Edge> pasted_edges,
    const char *undo_name = "Paste");
  // Insert, wire to the source pin, and evict what a singleConnect source pin held, as one entry. It
  // owns the insert because find_connectable_pin needs the node already in the graph. Returns the pin
  // it wired, or -1 -- the node is still created, unconnected.
  int recordAddConnectedNode(GraphData::Node node, int source_node, int source_pin);
  // Splices `node` into the connections at (anchor_node, anchor_pin): its source feeds the node, the
  // consumers move onto the node's output. anchor_edge_id narrows that to one wire, -1 takes the
  // pin's lot. Returns the pin it wired, or -1 -- the node is still created, unconnected.
  int recordInsertNodeOnPin(GraphData::Node node, int anchor_node, int anchor_pin, int anchor_edge_id);
  void recordRemoveKeepingConnections(eastl::vector<GraphData::Node> removed_nodes, eastl::vector<GraphData::Edge> removed_edges,
    eastl::vector<GraphData::Edge> bridge_edges);
  // The caller applied both halves already, so this only evicts what a single-connect end of the
  // new edge held, and records the whole reroute as one entry.
  void recordReconnectEdge(const GraphData::Edge &removed_edge, const GraphData::Edge *added_edge);
  void recordSelectionChange(GraphSelection old_selection, GraphSelection new_selection);
  void commitNodeTransforms(eastl::vector<NodePos> old_positions, eastl::vector<NodePos> new_positions,
    eastl::vector<BlockSize> old_sizes, eastl::vector<BlockSize> new_sizes);

private:
  // Edges that a new one on (node_id, pin_index) replaces, appended to out. validate_new_edge only
  // hides the incumbent of a single-connect pin, so removing it is the caller's job; a multi-connect
  // pin replaces nothing. A muted incumbent goes with the rest: unmuting it later would put two
  // live edges on the pin. except_edge_id skips an edge the caller has already inserted.
  void collectReplacedEdges(int node_id, int pin_index, int except_edge_id, eastl::vector<GraphData::Edge> &out) const;

  // The apply half: the smallest steps that change the graph. The undo entries replay them, and the
  // edits above apply through them. Node insert and erase go through the canvas when there is one so
  // it refreshes, and write graphData directly when there is not.

  // These change what the compiler sees, so each regenerates.
  void reinsertNode(const GraphData::Node &node);
  void reinsertEdge(const GraphData::Edge &edge);
  void eraseNode(int node_id);
  void eraseEdge(int edge_id);
  void eraseNodes(const eastl::vector<int> &node_ids);
  void restoreNodesAndEdges(const eastl::vector<GraphData::Node> &nodes, const eastl::vector<GraphData::Edge> &edges);
  void applyNodeProperties(int node_id, const eastl::vector<eastl::pair<eastl::string, eastl::string>> &props);
  // writeGraphSettings is the shared body; applyGraphSettings adds the panel rebuild, which only
  // undo needs -- an edit came from the panel and must not rebuild under it.
  void writeGraphSettings(const GraphSettings &settings);
  void applyGraphSettings(const GraphSettings &settings);
  void recordGraphSettingsChange(GraphSettings old_settings);
  void applyEdgeMuted(int edge_id, bool muted);

  // Display-only, so these do not regenerate.
  void applyNodePositions(const eastl::vector<NodePos> &positions);
  void applyBlockSizes(const eastl::vector<BlockSize> &sizes);
  void applySelection(const GraphSelection &selection);
  void applyPinComment(int node_id, int pin_index, const eastl::string &comment);

  void getNodeProperties(int node_id, eastl::vector<eastl::pair<eastl::string, eastl::string>> &out) const;

  // A new entry class has to be listed here, which is the point: it makes widening the apply
  // surface a deliberate edit rather than a side effect.
  friend class UndoCreateNode;
  friend class UndoDeleteNodes;
  friend class UndoMoveNodes;
  friend class UndoCreateEdge;
  friend class UndoDeleteEdges;
  friend class UndoToggleEdgeMuted;
  friend class UndoSelection;
  friend class UndoNodeProps;
  friend class UndoGraphSettings;
  friend class UndoPinComment;
  friend class UndoBlockResize;

  const GraphSelection *recordedSelection() const;
  void foldSelectionChange(GraphSelection new_selection);
  // Takes erased edges out of the recorded link selection, so no entry keeps an id that resolves to
  // nothing and undo reselects them with their edges. Same call window as foldSelectionChange.
  void dropErasedLinksFromSelection(const eastl::vector<GraphData::Edge> &erased);

  GraphEditorPlg &plugin;
  GraphPanel *view = nullptr;

  GraphData graphData;

  // Per-pin texgen register names from the most recent compile_graph_to_blks, keyed by
  // GraphData::Node::id. Inner vector is parallel to that node's pins[] at compile time. Guarded by
  // graphMutex: filled inside the worker's critical section, drained inside main's.
  eastl::vector<eastl::pair<int, eastl::vector<eastl::string>>> pendingPinCustomTextureNames;

  // Also covers mainGraphBlk / shaderListBlk on the write side, so a main-thread load cannot wipe
  // them while the worker commits. The texgen service reads and writes those two under its own
  // stateLock rather than this mutex, so those accesses race a main-thread load.
  WinCritSec graphMutex;

  volatile uint64_t graphRevision = 0;
};
