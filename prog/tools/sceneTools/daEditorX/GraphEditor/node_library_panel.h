// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <propPanel/c_common.h> // TLeafHandle
#include <propPanel/c_control_event_handler.h>
#include <propPanel/control/dragAndDropHandler.h>
#include <propPanel/control/panelWindow.h>
#include <propPanel/control/treeInterface.h>
#include <propPanel/propPanelService.h> // IconId

#include <util/dag_string.h>

#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

class DataBlock;
class GraphEditorPlg;

class NodeLibraryPanel final : public PropPanel::ControlEventHandler, public PropPanel::ITreeDragHandler, public PropPanel::ITreeFilter
{
public:
  explicit NodeLibraryPanel(GraphEditorPlg &plugin);
  ~NodeLibraryPanel() override;

  PropPanel::PanelWindowPropertyControl *getPanelWindow() { return panelWindow; }

  void updateImgui();

  // Marks both tabs stale, so a newly added shader or subgraph file surfaces.
  void refresh();

  void onRecentNodesChanged();

  void onBeginDrag(PropPanel::TLeafHandle leaf) override;
  void onDoubleClick(int pcb_id, PropPanel::ContainerPropertyControl *panel) override;

  bool filterNode(const PropPanel::TreeNode &node) override;
  bool hasAnyFilter() const override;
  const char *getNoResultsMsg() const override { return "No nodes match the filter"; }

private:
  // Category name as it appears in base_nodes.blk, and its filter checkbox.
  using CategoryFilter = eastl::pair<eastl::string, bool>;

  enum class Tab
  {
    All,
    RecentlyUsed,
    COUNT,
  };

  // One per tab, never destroyed, so an inactive tab keeps its expansion and selection.
  struct TabTree
  {
    PropPanel::ContainerPropertyControl *control = nullptr;
    bool contentDirty = true;
    unsigned filteredGeneration = 0;
  };

  void createTrees();
  void refillTreeIfNeeded(Tab tab);
  void drawHeader();
  void drawFilterPopup();
  void syncCategoryFilters();
  static const CategoryFilter *findCategoryFilter(const eastl::vector<CategoryFilter> &filters, const char *category);
  bool categoryEnabled(const char *category) const;
  bool matchesSearch(const DataBlock &desc) const;
  void populateAllNodes(PropPanel::ContainerPropertyControl &tree);
  void populateRecentlyUsed(PropPanel::ContainerPropertyControl &tree);

  TabTree &tabTree(Tab tab) { return tabTrees[static_cast<int>(tab)]; }
  PropPanel::ContainerPropertyControl *activeTree() { return tabTree(activeTab).control; }

  PropPanel::PanelWindowPropertyControl *panelWindow = nullptr;
  GraphEditorPlg &plugin;

  TabTree tabTrees[static_cast<int>(Tab::COUNT)];
  Tab activeTab = Tab::All;
  String searchText;
  bool searchInputFocused = false;

  // Resolved once: a theme switch keeps the IconId and swaps the texture under it.
  PropPanel::IconId searchIcon{};
  PropPanel::IconId clearIcon{};
  PropPanel::IconId filterDefaultIcon{};
  PropPanel::IconId filterActiveIcon{};
  PropPanel::IconId reloadIcon{};

  // Filter checkboxes in base_nodes.blk first-appearance order, kept across refills.
  eastl::vector<CategoryFilter> categoryFilters;

  // Starts above a fresh TabTree::filteredGeneration, so a tree's first draw always filters.
  unsigned filterGeneration = 1;
};
