// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include "canvas_clipboard.h"
#include "graph_add_node_popup.h"
#include "graph_canvas_cull.h"
#include "graph_context_menu.h"
#include "graph_dead_paths.h"
#include "graph_drag_collision.h"
#include "graph_edge_reconnect.h"
#include "graph_edge_render.h" // LinkDropOnCanvas
#include "graph_edit_types.h"
#include "graph_pin_jump_menu.h"

#include <propPanel/c_control_event_handler.h>
#include <propPanel/control/panelWindow.h>

#include <EASTL/hash_map.h>
#include <EASTL/hash_set.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <graphEditor/graph_data.h>

namespace ax
{
namespace NodeEditor
{
struct EditorContext;
}
} // namespace ax

class GraphDocument;
class GraphEditorPlg;
struct IGraphTexGenService;

class GraphPanel final : public PropPanel::ControlEventHandler
{
public:
  GraphPanel(GraphEditorPlg &plugin, IGraphTexGenService *tex_gen_service, GraphDocument &doc);
  ~GraphPanel() override;

  PropPanel::PanelWindowPropertyControl *getPanelWindow() { return panelWindow; }

  ImVec2 getLastCanvasCenter() const { return lastCanvasCenter; }

  // True while a node-library drag hovers this canvas -- while releasing would create the node.
  bool isBaseNodeDropTargetHot() const;

  void updateImgui();

  // Called by the plugin after it (re)populates the shared GraphData. Resets the NodeEditor
  // context (positions, fit, selection) so the panel doesn't carry stale display state across
  // graph swaps.
  void onGraphDataChanged();

  // Universal node-insertion entry point. Trusts the caller's id; pushes onto graphData.nodes
  // and marks the id as pending so the next render pushes node.x/node.y to ne::SetNodePosition.
  void addNode(GraphData::Node node);

  // Returns max(existing.id) + 1, or 0 if empty. Stable allocator -- callers that need a
  // unique id call this then pass the result to addNode.
  int allocateNodeId() const;

  // addEdge trusts the caller's id and just appends; removeEdgeById is a linear scan + erase
  // (returns false if no edge with that id). Edge ids come from next_edge_id.
  void addEdge(GraphData::Edge edge);
  bool removeEdgeById(int edge_id);
  bool removeNodeById(int node_id);

  // Queue a SetNodePosition push for existing nodes whose graphData x/y was changed out of frame
  // (move undo/redo): inserts the ids into pendingPositionIds and marks the cull cache dirty so the
  // next render pushes graphData.x/y to the node editor.
  void markPositionsPending(const eastl::vector<int> &node_ids);

  // Block sizes changed out of frame (resize undo/redo): drops the cull cache, which holds the old
  // bounds. draw_block_node pushes the size to ne itself, on the first frame the block draws.
  void markBlockSizesChanged();

  // Per-tick hook driven by the plugin's actObjects. Runs between ImGui frames -- the right
  // place for anything that needs to be done outside WithinFrameScope (modal dialogs in
  // particular: PropPanel::DialogWindow::showDialog aborts inside an ImGui frame).
  void actObjects(float dt);

  // Original node id (GraphData::Node::id) of the single selected node, or -1 when none /
  // multiple are selected. Refreshed each updateImgui frame after the ne::End call. Stable
  // across the rest of the frame so other panels (e.g. PropertiesPanel) can read it.
  int getSelectedNodeId() const { return selectedNodeId; }

  // Fills out_child_ids with the ids of every node (other than the block itself) whose
  // on-screen rect centre lies inside the block's on-screen rect. Uses ne::GetNodePosition
  // / ne::GetNodeSize so the layout reflects the live view (post-resize, post-pan). The
  // BLOCK_CONTAINMENT_GAP sentinel ensures the block is never seen as its own child.
  // Recursion is implicit: a nested block's centre is inside the outer rect, so the nested
  // block and everything inside it land in the output too -- one pass picks up the whole
  // transitive set. Public so the canvas Copy/Cut helpers (in canvas_clipboard.cpp) can
  // build the selection closure for block nodes.
  void collectNodesInsideBlock(int block_node_id, eastl::vector<int> &out_child_ids) const;

  // Selection undo support (used by GraphDocument). getRecordedSelection returns the selection (nodes
  // + links, sorted) as of frame start -- the "old" set a graph op folds into its undo entry.
  // suppressSelectionUndoThisFrame tells the frame-end detector that this frame's selection change is
  // already accounted for (folded into an edit, or undo-driven), so it resyncs instead of recording.
  // setPendingSelection queues a set for the next render pass to push to ne (UndoSelection apply).
  const GraphSelection &getRecordedSelection() const { return lastSelection; }
  void suppressSelectionUndoThisFrame() { suppressSelectionRecord = true; }
  void setPendingSelection(const GraphSelection &selection);

  // Ctrl+A / Ctrl+D / Ctrl+I and the editor's Zoom and center reach the plugin on the editor's command
  // path, out of frame. ne's calls need the editor current, so all four are queued for the next render pass.
  void requestSelectAll() { selectAllRequested = true; }
  void requestDeselectAll() { deselectAllRequested = true; }
  void requestInvertSelection() { invertSelectionRequested = true; }
  void requestFrameSelected() { frameSelectedRequested = true; }

private:
  // Node deletes captured during the deletion loop but waiting for the next actObjects tick
  // to resolve as a batch (so one prompt covers the whole selection and Cancel can roll back
  // all-or-nothing). For block nodes, childIds is the spatial-containment snapshot taken at
  // queue time -- ne::GetNodePosition / GetNodeSize would not be valid by actObjects time.
  // For non-block nodes childIds stays empty.
  struct PendingNodeDelete
  {
    int nodeId;
    eastl::vector<int> childIds;
  };

  PropPanel::PanelWindowPropertyControl *panelWindow = nullptr;
  GraphEditorPlg &plugin;
  IGraphTexGenService *texGenService = nullptr;
  GraphDocument &doc;
  // Writes go through doc.mutateGraphData, under the graph lock.
  const GraphData &graphData;
  ax::NodeEditor::EditorContext *editor = nullptr;

  // Node ids that need ne::SetNodePosition pushed this frame (using node.x/node.y, which is
  // already canvas-space). Populated by onGraphDataChanged (every loaded id) and addNode
  // (each drag-drop insert). Drained by GraphCanvasCull::update; cleared on graph reload.
  eastl::hash_set<int> pendingPositionIds;

  // Group size last pushed per block id (GraphData::Node::id); draw_block_node re-pushes on a change.
  eastl::hash_map<int, ImVec2> appliedBlockGroupSizes;

  // Canvas space, from the last rendered frame. This panel draws before the node library, so a
  // double click there always follows a capture.
  ImVec2 lastCanvasCenter = ImVec2(0.0f, 0.0f);


  // Stamped with a frame rather than cleared: a hidden panel never runs updateImgui, so a plain
  // bool would stay stuck on the last visible frame.
  int dropTargetHotFrame = -1;

  // Pin a node-library drag is over, resolved before the node pass so the pin that lights up is the
  // pin the drop splices into.
  int dropTargetPinNode = -1;
  int dropTargetPinIndex = -1;

  // Add transit node pressed with no pin or edge under the cursor: the gesture stays armed, previews
  // the node it would splice in, and the next click on a pin or edge is what opens the picker.
  bool transitArmed = false;
  int transitTargetNode = -1;
  int transitTargetPin = -1;
  int transitTargetEdge = -1;

  CanvasClipboard canvasClipboard;
  GraphContextMenu contextMenu;
  // Where the standing menu's Paste lands, rather than wherever the cursor ended up on the row.
  ImVec2 contextMenuCanvasPos = ImVec2(0.0f, 0.0f);

  GraphEdgeReconnect edgeReconnect;
  GraphData::Edge reconnectRemovedEdge; // edge dropped when a reconnect (A) begins; recorded for undo on resolve
  GraphPinJumpMenu pinJumpMenu;

  // "Drop a link on empty canvas to make a node." linkDrop outlives the frame: ne confirms the drop
  // a frame after the button comes up.
  LinkDropOnCanvas linkDrop;
  GraphAddNodePopup addNodePopup;

  // Selection undo. lastSelection is the selection (nodes + links, sorted) as of the last settled
  // frame -- the diff basis for recording deliberate changes and the "old" set graph ops fold into
  // their entry. pendingSelection is a set an undo/redo queued; it is pushed to ne on the next render
  // pass. suppressSelectionRecord marks a frame whose selection change is undo-driven or folded into
  // an edit, so the frame-end detector resyncs lastSelection instead of recording a new entry.
  GraphSelection lastSelection;
  GraphSelection pendingSelection;
  bool hasPendingSelection = false;
  bool suppressSelectionRecord = false;
  bool selectAllRequested = false;
  bool deselectAllRequested = false;
  bool invertSelectionRequested = false;
  bool frameSelectedRequested = false;

  // Reads the current ne selection (nodes + links) into out, as sorted original ids.
  void readSelection(GraphSelection &out) const;

  eastl::string lastSelectedNodeName;
  int selectedNodeId = -1;
  int previewNodeId = -1;
  int navigationFramesLeft = 5;
  int lastShownSelectedNodeId = -1;
  // Pin whose comment the C shortcut queued for editing; resolved (modal dialog) in actObjects.
  int pendingCommentNodeId = -1;
  int pendingCommentPinIndex = -1;

  eastl::vector<PendingNodeDelete> pendingNodeDeletes;

  // True once a left-drag has started; gates the post-frame move-detection scan to drag releases
  // (so plain clicks don't pay for it). Reset when the drag's release is processed.
  bool nodeDragInProgress = false;

  // Pre-drag sizes of the block (group) nodes resized during the current drag, captured lazily by
  // syncBlockSizes on each block's first size change -- the "old" sizes the resize-undo records on
  // release. Avoids scanning all nodes on every mouse-press. Drained when the release is processed.
  eastl::vector<BlockSize> blockResizeOld;

  GraphCanvasCull canvasCull;
  GraphDragCollision dragCollision;

  DeadPaths deadPaths;
  // Pins touched by any edge, and the subset touched by at least one live one, as make_pin_id keys.
  // ne::PinHadAnyLinks cannot serve instead: a muted link is still submitted, so ne keeps reporting
  // its pins as fed.
  eastl::hash_set<uint64_t> linkedPins;
  eastl::hash_set<uint64_t> livePins;
  uint64_t deadPathsRevision = ~uint64_t(0);
  void refreshDeadPaths();

  // Call between ne::Suspend() and ne::Resume().
  void updateContextMenu();
  // The canvas rect is threaded down to the "Add node" row, which places into it.
  void applyPendingMenuAction(const ImVec2 &canvas_min, const ImVec2 &canvas_max);
  void applySelectionRequests();

  // Copy + paste at an offset, leaving the canvas clipboard alone: the design puts Duplicate next to
  // Copy / Paste, so one must not overwrite what the other holds.
  void duplicateSelection();
  void selectAllNodes();
  // Nodes only, for the reason selectAllNodes gives: a link the user had picked is dropped, not kept.
  void invertNodeSelection();
  // Snapshots the edge first so the whole gesture resolves as one undo entry (recordReconnectEdge).
  void beginEdgeReroute(int edge_id, int detach_node, int detach_pin);

  void showNextSelectedNode();
  void removeSelectedKeepingConnections();
  void removeEdgesUnderCursor();
  void jumpToOppositePin();
  // The two placed drop points, one per trigger: below-forward of the pin, or above the cursor on
  // the edge. Each is shared by its Space shortcut and its context menu row. splice picks between
  // the Add node and Add transit node rows; an edge passes its id so a splice moves only that wire.
  void openAddNodePopupAtPin(int node_id, int pin_index, const ImVec2 &canvas_min, const ImVec2 &canvas_max, bool splice);
  void openAddNodePopupAtEdge(int source_node, int source_pin, int edge_id, const ImVec2 &cursor_canvas, const ImVec2 &canvas_min,
    const ImVec2 &canvas_max, bool splice);
  // Where a node dropped on a pin lands. Not the cursor: that is on the pin, inside another node.
  ImVec2 spawnPosAtPin(int node_id, int pin_index, const ImVec2 &canvas_min, const ImVec2 &canvas_max) const;
  // Node top-left for a drop point, shared by every placement: the picker's view clamp, then the
  // ghost body offset, so a preview and the node that follows it land together.
  ImVec2 spawnPosForDrop(const ImVec2 &drop_canvas, bool source_is_output, const ImVec2 &canvas_min, const ImVec2 &canvas_max) const;
  // And whether the dragged template could carry it, which the picker answers by filtering its list.
  bool canSpliceDrop(const char *template_uid, int node_id, int pin_index) const;
  void resolveBaseNodeDropTarget();
  // Both run before ne::Begin, and must: the canvas pass rewrites the mouse into canvas space, which
  // the edge target's ScreenToCanvas must not meet, and the picker has to stand before that pass
  // captures the aim the ghost is drawn from.
  void resolveArmedTransitTarget();
  void updateArmedTransit(const ImVec2 &canvas_min, const ImVec2 &canvas_max);
  // After the node pass instead, so the preview lies over the canvas it describes.
  void drawArmedTransitPreview(const ImVec2 &canvas_min, const ImVec2 &canvas_max);

  // After-frame pass: mirror each block's current ne::GetNodeSize back into GraphData so a
  // user-driven resize (handled internally by ne::SizeAction for group nodes) round-trips
  // through save / reload. Must run while the imgui-node-editor is still current (between
  // ne::End and ne::SetCurrentEditor(nullptr)).
  void syncBlockSizes();
};
