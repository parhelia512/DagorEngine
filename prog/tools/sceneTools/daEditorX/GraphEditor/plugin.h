// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EditorCore/ec_interface_ex.h>
#include <EditorCore/ec_wndPublic.h>
#include <oldEditor/de_interface.h>
#include <propPanel/c_control_event_handler.h>

#include <ioSys/dag_dataBlock.h>
#include <util/dag_string.h>

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <graphEditor/graph_data.h>

#include "graph_document.h"
#include "node_drag_overlay.h"
#include "resource_paths.hpp"

class NodeLibraryPanel;
class GraphPanel;
struct IGraphCompiler;
struct IGraphTexGenService;
class TexturePreviewPanel;
class HistogramPanel;
class LandscapePreviewPanel;
class PropertiesPanel;
class ShortcutsPanel;

class GraphEditorPlg final : public IGenEditorPlugin,
                             public IGenEventHandler,
                             public PropPanel::ControlEventHandler,
                             public IWndManagerWindowHandler
{
public:
  GraphEditorPlg();
  ~GraphEditorPlg() override;

  const char *getInternalName() const override { return "graphEditor"; }
  const char *getMenuCommandName() const override { return "GraphEditor"; }
  const char *getHelpUrl() const override { return "/html/Plugins/GraphEditor/index.htm"; }

  int getRenderOrder() const override { return 100; }
  int getBuildOrder() const override { return 0; }

  bool showInTabs() const override { return true; }
  bool showSelectAll() const override { return true; }

  bool acceptSaveLoad() const override { return true; }

  void registered() override;
  void loadSettings(const DataBlock &global_settings, const DataBlock &per_app_settings) override;
  void saveSettings(DataBlock &global_settings, DataBlock &per_app_settings) override;
  void unregistered() override;
  void beforeMainLoop() override;

  bool begin(int toolbar_id, unsigned menu_id) override;
  bool end() override;
  void onNewProject() override;
  IGenEventHandler *getEventHandler() override { return this; }

  void setVisible(bool vis) override { isVisible = vis; }
  bool getVisible() const override { return isVisible; }
  bool getSelectionBox(BBox3 &box) const override { return false; }
  bool getStatusBarPos(Point3 &pos) const override { return false; }

  void clearObjects() override;
  void saveObjects(DataBlock &blk, DataBlock &local_data, const char *base_path) override;
  void loadObjects(const DataBlock &blk, const DataBlock &local_data, const char *base_path) override;
  void selectAll() override;
  void deselectAll() override;
  void invertSelection() override;

  void actObjects(float dt) override;
  void beforeRenderObjects(IGenViewportWnd *vp) override;
  void renderObjects() override;
  void renderTransObjects() override;
  void updateImgui() override;

  void *queryInterfacePtr(unsigned huid) override;

  bool catchEvent(unsigned event_huid, void *user_data) override;
  bool onPluginMenuClick(unsigned id) override;
  void handleViewportAcceleratorCommand(unsigned id) override;
  void registerEditorCommands(IEditorCommandSystem &command_system) override;
  void registerMenuAccelerators() override;

  bool handleMouseMove(IGenViewportWnd *wnd, int x, int y, bool inside, int buttons, int key_modif) override;
  bool handleMouseLBPress(IGenViewportWnd *wnd, int x, int y, bool inside, int buttons, int key_modif) override;
  bool handleMouseLBRelease(IGenViewportWnd *wnd, int x, int y, bool inside, int buttons, int key_modif) override;
  bool handleMouseRBPress(IGenViewportWnd *wnd, int x, int y, bool inside, int buttons, int key_modif) override;
  bool handleMouseRBRelease(IGenViewportWnd *wnd, int x, int y, bool inside, int buttons, int key_modif) override;
  bool handleMouseCBPress(IGenViewportWnd *wnd, int x, int y, bool inside, int buttons, int key_modif) override;
  bool handleMouseCBRelease(IGenViewportWnd *wnd, int x, int y, bool inside, int buttons, int key_modif) override;
  bool handleMouseWheel(IGenViewportWnd *wnd, int wheel_d, int x, int y, int key_modif) override;
  bool handleMouseDoubleClick(IGenViewportWnd *wnd, int x, int y, int key_modif) override;
  void handleViewportPaint(IGenViewportWnd *wnd) override;
  void handleViewChange(IGenViewportWnd *wnd) override;

  void *onWmCreateWindow(int type) override;
  bool onWmDestroyWindow(void *window) override;

  void onClick(int pcb_id, PropPanel::ContainerPropertyControl *panel) override;

  // Base-node registry (base_nodes.blk). Single source of truth for descriptor data
  // (pin shape, property constraints, hidden flags). Loaded lazily on first access.
  const DataBlock &getBaseNodesBlk();

  // Primary descriptor lookup: O(1) hash-map by stable per-template uid (`templateUid:t`
  // in base_nodes.blk). Returns nullptr if the uid is unknown.
  const DataBlock *findBaseNodeBlockByUid(const char *template_uid);

  // Legacy fallback: linear-scan by `name:t`. Used only by the graph loaders when a
  // pre-templateUid file is opened. New code MUST go through findBaseNodeBlockByUid --
  // name lookup is not stable across template renames.
  const DataBlock *findBaseNodeBlockByName(const char *desc_name);

  // Pure factory: BLK lookup by uid + populate `out` (templateUid, descName from current
  // descriptor, position, default propertyValues from BLK, and pin-name-only stubs in
  // `pins[]` matching descriptor's pin order). Does NOT assign id or insert into the
  // graph. Returns false if `template_uid` is unknown.
  bool makeNodeFromBaseBlk(const char *template_uid, float x, float y, GraphData::Node &out);

  // makeNodeFromBaseBlk plus a fresh id, shared by every spawn path. False with no canvas or an
  // unknown uid.
  bool buildSpawnedNode(const char *template_uid, float x, float y, GraphData::Node &out);

  // Builds a fresh node via makeNodeFromBaseBlk, allocates its id via graphPanel and hands it to
  // the document. Reached from the canvas drop handler and from the node library's double click.
  // No-op if graphPanel is null or template_uid is unknown, and notes the descriptor as recently
  // used.
  void spawnBaseNode(const char *template_uid, float x, float y);
  void spawnBaseNodeAtCanvasCenter(const char *template_uid);
  // Same, wired to (anchor_node, anchor_pin) as one undo entry: alongside what that pin feeds, or
  // spliced into it when splice is set. Returns the new node id for the canvas, or -1.
  int spawnBaseNodeWired(const char *template_uid, float x, float y, int anchor_node, int anchor_pin, bool splice, int anchor_edge);
  void noteRecentlyUsedNode(const char *template_uid);

  const eastl::vector<eastl::string> &getRecentNodeUids() const { return recentNodeUids; }

  // Mark the graph dirty so the texgen worker thread runs `compile_graph_to_blks`
  // asynchronously and regenerates. Use for every mutation that doesn't change
  // the graph *source* (property edit, link add/remove, node spawn). Returns
  // instantly; coalesces with other pending marks before the worker picks them up.
  void markGraphDirtyAndRegen();

  // Use after a graph load (BLK / initial registration). Pushes heightmap
  // params, hands the GraphData pointer to the service (which resets pipeline
  // state -- preview-final, selected texture, etc.), then marks dirty so the
  // worker compiles the freshly-loaded graph. Do NOT call this from edit paths;
  // it wipes preview state.
  void notifyGraphSourceChanged();

  // Accessors for sibling panels (in particular PropertiesPanel) that need to read shared
  // state without reaching into private members. GraphPanel may be null when the user has
  // closed it; texGenService may be null until the texgen service initialises.
  GraphPanel *getGraphPanel() const { return graphPanel.get(); }
  IGraphTexGenService *getTexGenService() const { return texGenService; }

  // The displayed controls still hold the pre-undo values; make the panel rebuild them.
  void invalidatePropertiesPanel();

  const char *getShaderIncludesDir() const { return resourcePaths.shaderIncludesDir; }
  const char *getMainGraphsDir() const { return resourcePaths.mainGraphsDir; }
  const char *getSubgraphsDir() const { return resourcePaths.subgraphsDir; }

  // Drops the cached base-nodes blk + uid index and re-runs the lazy load + synthesis pipeline,
  // then marks the NodeLibraryPanel tabs stale so newly added shader / subgraph files appear
  // without restarting the editor. Cheap: the load itself is bounded (~100 base nodes plus a
  // small fixed set of shader / subgraph files on disk). Triggered from the panel's reload button.
  void reloadBaseNodes();

  bool promptPinComment(eastl::string &inout_comment);

private:
  void dropMyUndoOps();
  void initResourcePaths();
  void toggleNodeLibraryPanel();
  void toggleShortcutsPanel();
  void newEmptyGraph();
  void promptAndLoadGraphBlk();
  void promptAndSaveGraphBlk();
  void promptAndSaveAsSubgraphBlk();
  bool loadBaseNodesBlkIfNeeded();

  void appendShaderTemplatesToBaseNodes();
  // Mirrors appendShaderTemplatesToBaseNodes for *.subgraph.blk files under
  // resourcePaths.subgraphsDir. Each graph file becomes a synthesized node{} descriptor in
  // baseNodesBlk with category "Subgraphs", plugin "subgraph", and one pin{} per
  // `subgraph in: TYPE` / `subgraph out` boundary node found inside the child. The pin's
  // interface name comes from the boundary's `name` property value (not its descriptor
  // pin name), so multiple boundaries in one child stay distinguishable.
  void appendSubgraphTemplatesToBaseNodes();

  NodeDragOverlay dragOverlay;

  bool isVisible = false;
  int toolBarId = -1;
  // Declared before the panels: they hold a GraphDocument &, so it has to outlive them.
  GraphDocument document;

  eastl::unique_ptr<GraphPanel> graphPanel;
  IGraphTexGenService *texGenService = nullptr;
  eastl::unique_ptr<TexturePreviewPanel> previewPanel;
  eastl::unique_ptr<HistogramPanel> histogramPanel;
  eastl::unique_ptr<LandscapePreviewPanel> landscapePanel;
  eastl::unique_ptr<NodeLibraryPanel> nodeLibraryPanel;
  eastl::unique_ptr<PropertiesPanel> propertiesPanel;
  eastl::unique_ptr<ShortcutsPanel> shortcutsPanel;

  ResourcePaths resourcePaths;

  // Adapter that forwards IGraphCompiler::compile() calls (issued from the
  // texgen worker) into compile_graph_to_blks under the document's graph mutex.
  // Constructed once the texgen service is resolved; cleared from the service
  // before the impl is destroyed at shutdown.
  eastl::unique_ptr<IGraphCompiler> graphCompiler;

  DataBlock baseNodesBlk;
  // uid -> node{} block pointer into baseNodesBlk. Built once in
  // loadBaseNodesBlkIfNeeded() after a successful load; lifetime is tied to
  // baseNodesBlk (do NOT reload baseNodesBlk without rebuilding this map).
  eastl::hash_map<eastl::string, const DataBlock *> baseNodesByUid;
  bool baseNodesBlkLoaded = false;

  // Newest first. On the plugin, not the panel: loadSettings runs before the panel can exist.
  eastl::vector<eastl::string> recentNodeUids;

  bool isTexturePreviewVisible = true;
  bool isHistogramVisible = true;
  bool isLandscapeVisible = true;
  bool isGraphVisible = true;
  bool isNodeLibraryVisible = false;
  bool isPropertiesVisible = true;
  bool isShortcutsVisible = false;
};
