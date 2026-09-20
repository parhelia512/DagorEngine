// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <propPanel/c_control_event_handler.h>
#include <propPanel/control/panelWindow.h>

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/vector_map.h>
#include <EASTL/vector_set.h>

#include <graphEditor/graph_data.h>

class CurvePreviewControl;
class DataBlock;
class GraphDocument;
class GraphEditorPlg;

// Context-sensitive property grid. With no node selected it shows graph-level fields
// (output dirs, heightmap parameters); with one node selected it walks the
// node's descriptor in base_nodes.blk and creates a control per `property {}` block.
// Edits write back into GraphData and ask IGraphTexGenService to regenerate.
class PropertiesPanel final : public PropPanel::ControlEventHandler
{
public:
  PropertiesPanel(GraphEditorPlg &plugin, GraphDocument &doc);
  ~PropertiesPanel() override;

  PropPanel::PanelWindowPropertyControl *getPanelWindow() { return panelWindow; }

  // Called every frame after GraphPanel updates. Diffs against lastRenderedNodeId /
  // lastRenderedSourcePath to decide whether a control rebuild is required; the no-change
  // path is just a few comparisons + panelWindow->updateImgui().
  void updateImgui();

  // Force a control rebuild on the next updateImgui. Called after an undo/redo changes a property
  // value out-of-band (UndoNodeProps) so the controls reflect the restored values.
  void invalidateControls() { forceRebuild = true; }

  void onChange(int pcb_id, PropPanel::ContainerPropertyControl *panel) override;
  void onChangeFinished(int pcb_id, PropPanel::ContainerPropertyControl *panel) override;

private:
  enum class Mode
  {
    None,
    Graph,
    Node,
  };

  void rebuildForGraph();
  void rebuildForNode(int node_id);

  void commitNodeProperty(int pcb_id, PropPanel::ContainerPropertyControl *panel, bool finished);

  bool shouldWarnOnce(int node_id, const char *prop_name);

  const GraphData::Node *findNodeById(int id) const;

  PropPanel::PanelWindowPropertyControl *panelWindow = nullptr;
  GraphEditorPlg &plugin;
  GraphDocument &doc;

  Mode currentMode = Mode::None;
  int lastRenderedNodeId = -1;
  eastl::string lastRenderedSourcePath;
  bool forceRebuild = false; // one-shot rebuild request from invalidateControls() (undo/redo refresh)

  // Per-pid -> property name (node mode), wiped on every rebuild, keyed on the property's PID block
  // base.
  eastl::vector_map<int, eastl::string> pidToPropertyName;

  // gradient_preview strips, owned here because the holder control only borrows the pointer.
  eastl::vector<eastl::unique_ptr<CurvePreviewControl>> curvePreviews;

  // Malformed values already reported, so re-selecting the node does not repeat the warning.
  eastl::vector_set<eastl::string> warnedProperties;
};
