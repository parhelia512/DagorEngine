// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "node_library_panel.h"

#include "base_node_descriptors.h"
#include "node_drag_payload.h"
#include "plugin.h"

#include <de3_interface.h>
#include <EditorCore/ec_interface.h>
#include <imgui/imgui.h>
#include <ioSys/dag_dataBlock.h>
#include <osApiWrappers/dag_localConv.h>
#include <propPanel/control/container.h>
#include <propPanel/control/treeNode.h>
#include <propPanel/imguiWidgetService.h>
#include <propPanel/propPanelService.h>

#include <EASTL/algorithm.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace
{
enum
{
  PID_NODE_LIBRARY_ALL_TREE = 12000,
  PID_NODE_LIBRARY_RECENT_TREE = 12001,
};

constexpr const char *FILTER_POPUP_ID = "nodeLibraryFilter";

// The categories the node library design ships an icon for. An entry is both the category and its
// icon file name, which is the category lower-cased, resolved under commonData/icons/<theme>/.
// base_nodes.blk also has a "Relay" category the design does not cover, so a lookup can fail.
constexpr const char *CATEGORY_ICONS[] = {
  "output", "input", "math", "convert", "native", "creation", "comment", "shaders", "subgraphs"};

// Empty rather than the raw category for a miss: createTreeLeaf treats "" as no icon, while an
// unknown name would send image_helper looking for a file that is not there.
const char *category_icon_name(const char *category)
{
  for (const char *icon : CATEGORY_ICONS)
  {
    if (dd_stricmp(icon, category) == 0)
    {
      return icon;
    }
  }

  return "";
}
} // namespace

NodeLibraryPanel::NodeLibraryPanel(GraphEditorPlg &plg) : plugin(plg)
{
  panelWindow = IEditorCoreEngine::get()->createPropPanel(this, "Node library");

  PropPanel::IPropPanelService *service = DAEDITOR3.getPropPanelService();
  searchIcon = service->loadIcon("search");
  clearIcon = service->loadIcon("close_editor");
  filterDefaultIcon = service->loadIcon("filter_default");
  filterActiveIcon = service->loadIcon("filter_active");
  reloadIcon = service->loadIcon("compile");

  // Created here so drawHeader, which runs before the refill, never sees a null tree.
  createTrees();
}

NodeLibraryPanel::~NodeLibraryPanel() { IEditorCoreEngine::get()->deleteCustomPanel(panelWindow); }

void NodeLibraryPanel::updateImgui()
{
  if (!panelWindow)
  {
    return;
  }

  drawHeader();

  // After drawHeader: it switches activeTab, bumps filterGeneration, and its reload button frees
  // the descriptors the leaves hold.
  refillTreeIfNeeded(activeTab);

  // Not panelWindow->updateImgui(): that draws every control, the inactive tab's tree included.
  // The cost is the panel's middle-mouse drag scroll and its right-click menu, neither of which
  // applies to one full-height tree that scrolls itself. PushID separates the trees' child
  // windows, which TreeControlStandalone seeds from GetID("c").
  ImGui::PushID(static_cast<int>(activeTab));
  activeTree()->updateImgui();
  ImGui::PopID();
}

// Widgets that need the host-driven propPanel tooltip and focus helpers go through the service: this plugin is a
// DLL and must not link propPanel (see IImguiWidgetService).
void NodeLibraryPanel::drawHeader()
{
  if (ImGui::BeginTabBar("nodeLibraryTabs"))
  {
    if (ImGui::BeginTabItem("All"))
    {
      activeTab = Tab::All;
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Recently used"))
    {
      activeTab = Tab::RecentlyUsed;
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  PropPanel::IPropPanelService *service = DAEDITOR3.getPropPanelService();
  PropPanel::IImguiWidgetService &widgets = service->getImguiWidgets();

  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 iconSize = PropPanel::ImguiHelper::getFontSizedIconSize();
  const bool filterOpen = ImGui::IsPopupOpen(FILTER_POPUP_ID);

  const ImVec2 filterButtonSize = PropPanel::ImguiHelper::getImageButtonWithDownArrowSize(iconSize);
  const ImVec2 reloadButtonSize = PropPanel::ImguiHelper::getImageButtonSize(iconSize);
  const float searchWidth =
    ImGui::GetContentRegionAvail().x - (style.ItemSpacing.x + filterButtonSize.x) - (style.ItemSpacing.x + reloadButtonSize.x);
  // Floored: ImGui reads a negative width as an offset from the right edge rather than clamping it.
  ImGui::SetNextItemWidth(eastl::max(searchWidth, iconSize.x));
  if (widgets.searchInput(&searchText, "##searchInput", "Filter and search", searchText, searchIcon, clearIcon, &searchInputFocused))
  {
    ++filterGeneration;
  }

  ImGui::SameLine();
  if (widgets.imageButtonWithArrow("filterButton", filterOpen ? filterActiveIcon : filterDefaultIcon, iconSize, filterOpen))
  {
    ImGui::OpenPopup(FILTER_POPUP_ID);
  }
  drawFilterPopup();

  ImGui::SameLine();
  if (ImGui::ImageButton("reloadBaseNodes", service->getImTextureId(reloadIcon), iconSize))
  {
    plugin.reloadBaseNodes();
  }
  ImGui::SetItemTooltip("Reload base nodes");

  if (activeTab == Tab::All)
  {
    const char *expandAllTitle = "Expand all";
    const char *collapseAllTitle = "Collapse all";
    const ImVec2 expandAllSize = PropPanel::ImguiHelper::getButtonSize(expandAllTitle);
    const ImVec2 collapseAllSize = PropPanel::ImguiHelper::getButtonSize(collapseAllTitle);
    const float rowLeftX = ImGui::GetCursorPosX();
    const float rowRightX = rowLeftX + ImGui::GetContentRegionAvail().x;
    const float buttonsX = rowRightX - (expandAllSize.x + style.ItemSpacing.x + collapseAllSize.x);
    // Floored like the search field above, so a narrow panel does not push the pair off the left edge.
    ImGui::SetCursorPosX(eastl::max(buttonsX, rowLeftX));

    if (ImGui::Button(expandAllTitle))
    {
      activeTree()->setExpandedRecursively(nullptr, true);
    }

    ImGui::SameLine();
    if (ImGui::Button(collapseAllTitle))
    {
      activeTree()->setExpandedRecursively(nullptr, false);
    }
  }
}

void NodeLibraryPanel::drawFilterPopup()
{
  // The design draws this popup on WindowBg rather than the darker PopupBg the theme gives menus.
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::GetColorU32(ImGuiCol_WindowBg));

  if (ImGui::BeginPopup(FILTER_POPUP_ID))
  {
    ImFont *boldFont = DAEDITOR3.getPropPanelService()->getBoldFont();
    if (boldFont)
    {
      ImGui::PushFont(boldFont, 0.0f);
    }
    ImGui::TextUnformatted("Select filter");
    if (boldFont)
    {
      ImGui::PopFont();
    }

    bool filterChanged = false;

    // Split the row between the two buttons, as the asset type filter does: right-aligning them would
    // have to measure against an auto-sized popup that is still being laid out.
    const char *resetTitle = "Reset";
    const char *invertTitle = "Invert";
    const float halfRow = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const float naturalWidth =
      eastl::max(PropPanel::ImguiHelper::getButtonSize(resetTitle).x, PropPanel::ImguiHelper::getButtonSize(invertTitle).x);
    const float buttonWidth = eastl::max(halfRow, naturalWidth);

    if (ImGui::Button(resetTitle, ImVec2(buttonWidth, 0.0f)))
    {
      for (auto &category : categoryFilters)
      {
        category.second = true;
      }
      filterChanged = true;
    }

    ImGui::SameLine();
    if (ImGui::Button(invertTitle, ImVec2(buttonWidth, 0.0f)))
    {
      for (auto &category : categoryFilters)
      {
        category.second = !category.second;
      }
      filterChanged = true;
    }

    for (auto &category : categoryFilters)
    {
      bool enabled = category.second;
      if (ImGui::Checkbox(category.first.c_str(), &enabled))
      {
        category.second = enabled;
        filterChanged = true;
      }
    }

    if (filterChanged)
    {
      ++filterGeneration;
    }

    ImGui::EndPopup();
  }

  ImGui::PopStyleColor();
}

// Marked only, not emptied: refillTreeIfNeeded clears before it repopulates, and the deferred
// filter never runs on a tab that is still dirty, so no leaf is read after baseNodesBlk is reset.
void NodeLibraryPanel::refresh()
{
  for (TabTree &target : tabTrees)
  {
    target.contentDirty = true;
  }
}

void NodeLibraryPanel::onRecentNodesChanged() { tabTree(Tab::RecentlyUsed).contentDirty = true; }

// Uids that no longer resolve are skipped, so a removed shader or subgraph file drops out.
void NodeLibraryPanel::populateRecentlyUsed(PropPanel::ContainerPropertyControl &tree)
{
  bool anyLeaf = false;
  for (const eastl::string &uid : plugin.getRecentNodeUids())
  {
    const DataBlock *desc = plugin.findBaseNodeBlockByUid(uid.c_str());
    if (!desc || desc->getBool("hidden", false))
    {
      continue;
    }

    PropPanel::TLeafHandle leaf = tree.createTreeLeaf(nullptr, desc->getStr("name", ""), "");
    tree.setUserData(leaf, desc);
    anyLeaf = true;
  }

  if (!anyLeaf)
  {
    tree.setTreeMessage("No recently used nodes");
  }
}

void NodeLibraryPanel::createTrees()
{
  const int pids[] = {PID_NODE_LIBRARY_ALL_TREE, PID_NODE_LIBRARY_RECENT_TREE};
  for (int i = 0; i < static_cast<int>(Tab::COUNT); ++i)
  {
    PropPanel::ContainerPropertyControl *control = panelWindow->createTree(pids[i], "", hdpi::_pxScaled(0));
    control->setTreeDragHandler(this);
    // The drag ghost is the preview, drawn over every panel; ImGui's own pill would sit beside it.
    control->setTreeDragDropFlags(ImGuiDragDropFlags_PayloadNoCrossProcess | ImGuiDragDropFlags_SourceNoPreviewTooltip);
    tabTrees[i].control = control;
  }
}

// Lazy rather than on change: the change can arrive from inside a tree's own render, where clearing
// would free the nodes being walked.
void NodeLibraryPanel::refillTreeIfNeeded(Tab tab)
{
  TabTree &target = tabTree(tab);
  if (target.contentDirty)
  {
    target.contentDirty = false;
    syncCategoryFilters();

    // Detached while filling: a live filter refilters the branch around every new leaf, and a leaf
    // has no user data until after createTreeLeaf, so filterNode would read it as a category row.
    target.control->setTreeFilter(nullptr);
    target.control->clear();
    // clear() keeps the message the last fill set, so every path has to state its own.
    target.control->setTreeMessage("");

    if (tab == Tab::RecentlyUsed)
    {
      populateRecentlyUsed(*target.control);
    }
    else
    {
      populateAllNodes(*target.control);
    }

    // Reinstalling filters the finished tree once.
    target.control->setTreeFilter(this);
    target.filteredGeneration = filterGeneration;
  }
  else if (target.filteredGeneration != filterGeneration)
  {
    target.filteredGeneration = filterGeneration;
    target.control->filterTree();
  }
}

// Rebuilt, not appended to: a category dropped from base_nodes.blk has to lose its checkbox, and a
// reintroduced one has to return to its blk position. Existing checkbox states carry over.
void NodeLibraryPanel::syncCategoryFilters()
{
  eastl::vector<CategoryFilter> refreshed;

  for_each_node_descriptor(plugin.getBaseNodesBlk(), [&](const DataBlock &node) {
    // Skipped like the tree fills do, so the popup cannot offer a category that has no visible nodes.
    if (node.getBool("hidden", false))
    {
      return;
    }

    const char *category = node.getStr("category", "");
    if (!category[0] || findCategoryFilter(refreshed, category))
    {
      return;
    }

    const CategoryFilter *previous = findCategoryFilter(categoryFilters, category);
    refreshed.emplace_back(eastl::string(category), previous ? previous->second : true);
  });

  categoryFilters = eastl::move(refreshed);
}

const NodeLibraryPanel::CategoryFilter *NodeLibraryPanel::findCategoryFilter(const eastl::vector<CategoryFilter> &filters,
  const char *category)
{
  for (const CategoryFilter &entry : filters)
  {
    if (dd_stricmp(entry.first.c_str(), category) == 0)
    {
      return &entry;
    }
  }

  return nullptr;
}

bool NodeLibraryPanel::categoryEnabled(const char *category) const
{
  // Absent means no checkbox has hidden it.
  const CategoryFilter *entry = findCategoryFilter(categoryFilters, category);
  return entry ? entry->second : true;
}

bool NodeLibraryPanel::matchesSearch(const DataBlock &desc) const
{
  return node_name_matches_search(desc.getStr("name", ""), desc.getStr("synonyms", ""), searchText.c_str());
}

bool NodeLibraryPanel::hasAnyFilter() const
{
  if (!searchText.empty())
  {
    return true;
  }

  for (const auto &entry : categoryFilters)
  {
    if (!entry.second)
    {
      return true;
    }
  }

  return false;
}

bool NodeLibraryPanel::filterNode(const PropPanel::TreeNode &node)
{
  const DataBlock *desc = static_cast<const DataBlock *>(node.getUserData());
  if (!desc)
  {
    // A category must fail its own filter while a search runs, or filterRoutine keeps it for passing
    // even with no matching child, leaving a column of empty headers. It survives through a child.
    return categoryEnabled(node.getTitle().c_str()) && searchText.empty();
  }

  return categoryEnabled(desc->getStr("category", "")) && matchesSearch(*desc);
}

void NodeLibraryPanel::populateAllNodes(PropPanel::ContainerPropertyControl &tree)
{
  const DataBlock &blk = plugin.getBaseNodesBlk();
  if (blk.blockCount() == 0)
  {
    tree.setTreeMessage("base_nodes.blk could not be loaded");
    return;
  }

  // Group nodes by category, preserving the order categories first appear in the BLK
  // (matches the order the JS editor's "Add node" menu shows them).
  eastl::vector<eastl::string> categoryOrder;
  eastl::vector<PropPanel::TLeafHandle> categoryLeaves;

  // Case-insensitive like findCategoryFilter and category_icon_name: a case variant must not split a
  // category into two rows that then share one filter checkbox.
  auto findCategory = [&](const char *name) -> int {
    for (int i = 0; i < (int)categoryOrder.size(); ++i)
    {
      if (dd_stricmp(categoryOrder[i].c_str(), name) == 0)
      {
        return i;
      }
    }
    return -1;
  };

  for_each_node_descriptor(blk, [&](const DataBlock &node) {
    if (!node_desc_is_offerable(node))
    {
      return;
    }

    // A leaf needs both to be placed and labelled; the uid check lives in node_desc_is_offerable.
    const char *category = node.getStr("category", "");
    const char *name = node.getStr("name", "");
    if (!category[0] || !name[0])
    {
      return;
    }

    int idx = findCategory(category);
    if (idx < 0)
    {
      categoryOrder.emplace_back(category);
      categoryLeaves.push_back(tree.createTreeLeaf(nullptr, category, category_icon_name(category)));
      idx = (int)categoryOrder.size() - 1;
    }

    // The descriptor rides on the leaf: filterNode reads its category and name, and onBeginDrag its
    // templateUid. refresh() is what keeps these from being read through a reset baseNodesBlk. A
    // null user data is a category row, which is how filterNode tells them apart.
    PropPanel::TLeafHandle leaf = tree.createTreeLeaf(categoryLeaves[idx], name, "");
    tree.setUserData(leaf, &node);
  });

  for (PropPanel::TLeafHandle leaf : categoryLeaves)
  {
    tree.setExpanded(leaf, true);
  }
}

void NodeLibraryPanel::onDoubleClick(int pcb_id, PropPanel::ContainerPropertyControl * /*panel*/)
{
  if (pcb_id != PID_NODE_LIBRARY_ALL_TREE && pcb_id != PID_NODE_LIBRARY_RECENT_TREE)
  {
    return;
  }

  // Taken from the id rather than activeTree(), so this does not rest on only the active tree being
  // drawn. Null user data is a category row, whose double click only expands it.
  const Tab raisedBy = pcb_id == PID_NODE_LIBRARY_ALL_TREE ? Tab::All : Tab::RecentlyUsed;
  PropPanel::ContainerPropertyControl *tree = tabTree(raisedBy).control;
  const DataBlock *desc = static_cast<const DataBlock *>(tree->getUserData(tree->getSelLeaf()));
  if (!desc)
  {
    return;
  }

  plugin.spawnBaseNodeAtCanvasCenter(desc->getStr("templateUid", ""));
}

void NodeLibraryPanel::onBeginDrag(PropPanel::TLeafHandle leaf)
{
  // Null on a category row, so dragging one produces no payload and can only ever be refused.
  const DataBlock *desc = static_cast<const DataBlock *>(activeTree()->getUserData(leaf));
  if (!desc)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
    return;
  }

  set_base_node_drag_payload(desc->getStr("templateUid", ""));
}
