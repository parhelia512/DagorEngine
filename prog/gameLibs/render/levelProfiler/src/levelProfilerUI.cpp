// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <cstring>
#include <EASTL/algorithm.h>
#include "levelProfilerUI.h"
#include "levelProfilerRiTable.h"
#include "riModule.h"

static_assert(levelprofiler::ImGuiConstants::WITH_BORDER == true, "WITH_BORDER constant must match ImGui boolean true value");
static_assert(levelprofiler::ImGuiConstants::NO_BORDER == false, "NO_BORDER constant must match ImGui boolean false value");

namespace levelprofiler
{

// --- TextureProfilerUI ---

TextureProfilerUI::TextureProfilerUI(TextureModule *texture_module, RIModule *ri_module) :
  textureModule(texture_module),
  riModule(ri_module),
  exporter(texture_module),
  horizontalSplitter(LpSplitterDirection::HORIZONTAL, 0.6f),
  verticalSplitter(LpSplitterDirection::VERTICAL, 0.7f)
{
  filterManager.setModules(texture_module, ri_module);

  textureTable = eastl::make_unique<LpTextureTable>(this);
  exporter.setTextureTable(textureTable.get());
}

TextureProfilerUI::~TextureProfilerUI() {}

void TextureProfilerUI::init()
{
  if (textureTable)
    textureTable->setCopyManager(getCopyManager());
}

void TextureProfilerUI::onDataCollected()
{
  if (textureModule->getTotalTextureCount() == 0)
    return;

  // Only the first collect seeds the defaults; later ones move the bounds onto the new data and
  // keep what the user set, so a recollect does not throw away a filter setup.
  if (filtersSeeded)
    filterManager.rebindFiltersToData();
  else
  {
    filterManager.resetAllFilters();
    filtersSeeded = true;
  }
  filterManager.applyFilters();

  // The selection is by name, so it survives a recollect as long as the texture is still there.
  if (textureTable)
  {
    const ProfilerString &selected = textureTable->getSelectedTexture();
    if (!selected.empty() && textureModule->getTextures().find(selected) == textureModule->getTextures().end())
      textureTable->setSelectedTexture(ProfilerString());
  }
}

void TextureProfilerUI::shutdown() {}

void TextureProfilerUI::drawUI()
{
  ImVec2 availableArea = ImGui::GetContentRegionAvail();
  float leftPanelWidth = availableArea.x * horizontalSplitter.getRatio();

  // Left panel: table and filters
  ImGui::BeginChild("PoolLeft", ImVec2(leftPanelWidth, 0), ImGuiConstants::WITH_BORDER);
  drawLeftPanel();
  ImGui::EndChild();

  ImGui::SameLine();

  horizontalSplitter.draw(availableArea);

  // Right panel: preview and Usage list
  ImGui::SameLine();
  ImGui::BeginChild("PoolRight", ImVec2(0, 0), ImGuiConstants::WITH_BORDER);
  drawRightPanel();
  ImGui::EndChild();
}

void TextureProfilerUI::drawLeftPanel()
{
  float tableRegionHeight = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.5f;

  ImGui::BeginChild("TableRegion", ImVec2(0, tableRegionHeight), ImGuiConstants::NO_BORDER);
  if (textureTable)
  {
    if (textureTable->isSortActive())
      textureTable->sortFilteredTextures();

    textureTable->draw();
  }
  ImGui::EndChild();

  ImGui::Separator();

  // Display statistics
  ImGui::Text("Textures: %u / %u", textureModule->getFilteredTextureCount(), textureModule->getTotalTextureCount());
  ImGui::Text("Memory: %.2f / %.2f MB", textureModule->getFilteredTotalMemorySize(), textureModule->getTotalMemorySize());
}

void TextureProfilerUI::drawRightPanel()
{
  // Calculate available area and divide vertically
  ImVec2 availablePanelArea = ImGui::GetContentRegionAvail();
  float previewPaneHeight = availablePanelArea.y * verticalSplitter.getRatio();

  // Preview pane (top)
  ImGui::BeginChild("PreviewPane", ImVec2(0, previewPaneHeight), ImGuiConstants::NO_BORDER);
  {
    const auto &filteredTextureNames = textureModule->getFilteredTextures();
    ProfilerString selectedTextureName = textureTable->getSelectedTexture();

    if (selectedTextureName.empty() && !filteredTextureNames.empty())
    {
      selectedTextureName = filteredTextureNames[0];
      textureTable->setSelectedTexture(selectedTextureName);
    }

    if (!selectedTextureName.empty())
      textureModule->drawTextureView(selectedTextureName.c_str());
  }
  ImGui::EndChild();

  verticalSplitter.draw(availablePanelArea);

  // Texture Usage pane (bottom)
  ImGui::BeginChild("TextureUsagePane", ImVec2(0, 0), ImGuiConstants::WITH_BORDER);
  {
    ImGui::TextUnformatted("Texture Usage Objects:");

    const ProfilerString currentSelectedTextureName = textureTable->getSelectedTexture();

    // Clear selected asset if texture changed
    if (static ProfilerString lastSelectedTexture; lastSelectedTexture != currentSelectedTextureName)
    {
      selectedAssetName.clear();
      lastSelectedTexture = currentSelectedTextureName;
    }

    if (!currentSelectedTextureName.empty())
    {
      // Look up assets using this texture
      const auto &textureToAssetsMap = riModule->getTextureToAssetsMap();
      if (auto iterator = textureToAssetsMap.find(currentSelectedTextureName); iterator != textureToAssetsMap.end())
      {
        ImGui::BeginChild("TextureUsageList", ImVec2(0, 0), ImGuiConstants::NO_BORDER);

        const auto &riCounts = riModule->getRiInstanceCounts();
        for (const auto &assetName : iterator->second)
        {
          int count = 0;
          if (auto itCnt = riCounts.find(assetName); itCnt != riCounts.end())
            count = itCnt->second;
          char displayBuf[256];
          if (count > 0)
            snprintf(displayBuf, sizeof(displayBuf), "%s (%d)", assetName.c_str(), count);
          else
            snprintf(displayBuf, sizeof(displayBuf), "%s", assetName.c_str());

          bool isSelected = (selectedAssetName == assetName);
          if (ImGui::Selectable(displayBuf, isSelected))
            selectedAssetName = assetName;
        }

        ImGui::EndChild();
      }
      else
        ImGui::TextDisabled("No texture usage data available");
    }
  }
  ImGui::EndChild();
}

CopyResult TextureProfilerUI::handleGlobalCopy() const
{
  // Priority 1: Selected asset name
  if (!selectedAssetName.empty())
    return CopyResult(selectedAssetName, "Asset name copied");

  // Priority 2: Selected texture name
  if (textureTable)
  {
    const ProfilerString &selectedTexture = textureTable->getSelectedTexture();
    if (!selectedTexture.empty())
      return CopyResult(selectedTexture, "Texture name copied");
  }

  return CopyResult(); // Nothing to copy
}

CopyResult TextureProfilerUI::handleContextCopy(const CopyRequest &request) const
{
  if (request.customData.empty())
    return CopyResult();

  ProfilerString notificationMsg;
  switch (request.type)
  {
    case CopyType::CONTEXT_CELL: notificationMsg = "Cell copied"; break;
    case CopyType::CONTEXT_ROW: notificationMsg = "Row copied"; break;
    case CopyType::CONTEXT_CUSTOM: notificationMsg = "Info copied"; break;
    default: notificationMsg = "Copied to clipboard";
  }

  return CopyResult(request.customData, notificationMsg);
}

eastl::vector<ProfilerString> TextureProfilerUI::getContextMenuItems() const { return {}; }

GlobalCopyManager *TextureProfilerUI::getCopyManager() const
{
  auto levelProfiler = static_cast<LevelProfilerUI *>(ILevelProfiler::getInstance());
  return levelProfiler->getCopyManager();
}

ProfilerString TextureProfilerUI::generateFullTextureInfo(const ProfilerString &texture_name) const
{
  const auto &textures = textureModule->getTextures();
  auto it = textures.find(texture_name);
  if (it == textures.end())
    return ProfilerString{};

  const TextureData &textureData = it->second;

  ProfilerString info;
  info += "Texture: " + texture_name + "\n";
  info += "Format: " + ProfilerString(TextureModule::getFormatName(textureData.info.cflg)) + "\n";
  info += "Dimensions: " + eastl::to_string(textureData.info.w) + "x" + eastl::to_string(textureData.info.h) + "\n";
  info += "Mip levels: " + eastl::to_string(textureData.info.mipLevels) + "\n";

  float sizeMB = textureModule->getTextureMemorySize(textureData);
  info += "Memory size: " + eastl::to_string(sizeMB) + " MB\n";

  const auto &textureUsageMap = riModule->getTextureUsage();
  if (auto usageIt = textureUsageMap.find(texture_name); usageIt != textureUsageMap.end())
  {
    const TextureUsage &usage = usageIt->second;
    if (usage.unique == 0)
      info += "Usage: Non-RI";
    else if (usage.unique == 1)
      info += "Usage: Unique";
    else
      info += "Usage: Shared(" + eastl::to_string(usage.unique) + ")";
  }
  else
    info += "Usage: Non-RI";

  return info;
}


// --- RIProfilerUI ---

RIProfilerUI::RIProfilerUI(RIModule *ri_module) : riModule(ri_module) { riTable = eastl::make_unique<LpRiTable>(ri_module); }

RIProfilerUI::~RIProfilerUI() {}

void RIProfilerUI::init()
{
  if (riTable)
    riTable->setCopyManager(getCopyManager());
}

void RIProfilerUI::shutdown() {}

void RIProfilerUI::drawUI()
{
  float tableRegionHeight = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.5f;

  ImGui::BeginChild("RiTableRegion", ImVec2(0.0f, tableRegionHeight), ImGuiConstants::NO_BORDER);

  bool hasModule = riTable && riModule;

  if (hasModule)
  {
    const auto &riData = riModule->getRiData();
    if (!riData.empty())
    {
      if (riModule->hasProvisionalRiData())
      {
        if (riModule->isCollecting())
          ImGui::TextDisabled("Collecting RI data... values update live.");
        else if (riModule->isPaused())
          ImGui::TextDisabled("Collection paused. Rendinst counts are incomplete.");
        else if (riModule->wasCollectCancelled())
          ImGui::TextDisabled("Collection stopped: the level was unloaded. Counts are incomplete.");
        else
          ImGui::TextDisabled("RI data not finalized. Values remain provisional.");
        ImGui::Spacing();
      }
      riTable->draw();
    }
    else
    {
      if (riModule->isCollecting())
        ImGui::Text("Collecting RI data... preparing table.");
      else
        ImGui::Text("No RI assets found. Please collect data first.");
    }
  }
  else
  {
    ImGui::Text("No RI module available");
  }

  ImGui::EndChild();

  ImGui::Separator();

  if (hasModule && !riModule->getRiData().empty())
  {
    ImGui::Text("RI Assets: %u / %u", (unsigned)riTable->getFilteredRiCount(), (unsigned)riTable->getTotalRiCount());
  }
}

CopyResult RIProfilerUI::handleGlobalCopy() const
{
  if (riTable)
  {
    const ProfilerString &selectedRi = riTable->getSelectedRi();
    if (!selectedRi.empty())
      return CopyResult(selectedRi, "RI asset name copied");
  }

  return CopyResult();
}

CopyResult RIProfilerUI::handleContextCopy(const CopyRequest &request) const
{
  if (request.customData.empty())
    return CopyResult();

  ProfilerString notificationMsg;
  switch (request.type)
  {
    case CopyType::CONTEXT_CELL: notificationMsg = "Cell copied"; break;
    case CopyType::CONTEXT_ROW: notificationMsg = "Row copied"; break;
    case CopyType::CONTEXT_CUSTOM: notificationMsg = "Info copied"; break;
    default: notificationMsg = "Copied to clipboard";
  }

  return CopyResult(request.customData, notificationMsg);
}

eastl::vector<ProfilerString> RIProfilerUI::getContextMenuItems() const { return {}; }

GlobalCopyManager *RIProfilerUI::getCopyManager() const
{
  auto levelProfiler = static_cast<LevelProfilerUI *>(ILevelProfiler::getInstance());
  return levelProfiler->getCopyManager();
}

ProfilerString RIProfilerUI::generateFullRiInfo(const ProfilerString &ri_name) const
{
  const RiData *riDataItem = riModule->getRiDataByName(ri_name);
  if (!riDataItem)
    return ProfilerString{};

  ProfilerString info;
  info += "RI Asset: " + ri_name + "\n";
  info += "Count on map: " + eastl::to_string(riDataItem->countOnMap) + "\n";
  info += "BSphere radius: " + eastl::to_string(riDataItem->bSphereRadius) + "\n";
  info += "BBox radius: " + eastl::to_string(riDataItem->bBoxRadius) + "\n";

  for (size_t i = 0; i < riDataItem->lods.size(); ++i)
  {
    const auto &lod = riDataItem->lods[i];
    info += "LOD" + eastl::to_string(i) + " - Dips: " + eastl::to_string(lod.drawCalls);
    info += ", Tris: " + eastl::to_string(lod.totalFaces);
    info += ", Dist: " + eastl::to_string(lod.lodDistance);
    info += ", Screen%: " + eastl::to_string(lod.screenPercent) + "\n";
  }

  if (riDataItem->collision.physTriangles > 0 || riDataItem->collision.traceTriangles > 0)
  {
    info += "Collision - Phys: " + eastl::to_string(riDataItem->collision.physTriangles);
    info += ", Trace: " + eastl::to_string(riDataItem->collision.traceTriangles) + "\n";
  }

  return info;
}

// --- LevelProfilerUI ---

LevelProfilerUI::LevelProfilerUI()
{
  textureModule = eastl::make_unique<TextureModule>();
  riModule = eastl::make_unique<RIModule>();

  textureProfilerUI = eastl::make_unique<TextureProfilerUI>(textureModule.get(), riModule.get());
  riProfilerUI = eastl::make_unique<RIProfilerUI>(riModule.get());

  // RI first: the texture usage view is built from RI instance counts.
  registerDataModule(riModule.get());
  registerDataModule(textureModule.get());
}


void LevelProfilerUI::initialize()
{
  toastAdapter = eastl::make_unique<ToastNotificationAdapter>(&textureProfilerUI->getExporter().getToast());
  textureProfilerUI->getExporter().setNotificationHandler(toastAdapter.get());
  globalCopyManager.setNotificationHandler(toastAdapter.get());

  globalCopyManager.registerProvider(textureProfilerUI.get(), "Texture pool");
  globalCopyManager.registerProvider(riProfilerUI.get(), "Lods statistic");

  addTab("Texture pool", textureProfilerUI.get());
  addTab("Lods statistic", riProfilerUI.get());
}

LevelProfilerUI::~LevelProfilerUI()
{
  globalCopyManager.unregisterProvider(textureProfilerUI.get());
  globalCopyManager.unregisterProvider(riProfilerUI.get());
  tabs.clear();
}

void LevelProfilerUI::init()
{
  for (DataModuleEntry &entry : dataModules)
    entry.module->init();

  for (auto &tab : tabs)
    tab.module->init();
}

void LevelProfilerUI::shutdown()
{
  // Shutdown UI modules (in reverse order)
  for (auto &tab : tabs)
    tab.module->shutdown();

  for (DataModuleEntry &entry : dataModules)
    entry.module->shutdown();
}

void LevelProfilerUI::drawUI()
{
  ImGui::Begin("Level Profiler");

  globalCopyManager.update();

  for (DataModuleEntry &entry : dataModules)
    entry.collector->continueCollect();

  const bool collectBusy = isAnyBusy();
  if (collectWasBusy && !collectBusy)
    notifyTabsDataCollected();
  collectWasBusy = collectBusy;

  if (toastAdapter)
    toastAdapter->update();

  const bool collecting = isAnyCollecting();
  const char *collectButtonLabel = "Collect Data";
  if (collecting)
    collectButtonLabel = "Pause Collecting";
  else if (isAnyPaused())
    collectButtonLabel = "Resume Collecting";

  bool applyPauseStyle = collecting;
  if (applyPauseStyle)
  {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.14f, 0.11f, 0.02f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.96f, 0.82f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.88f, 0.34f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.90f, 0.75f, 0.22f, 1.0f));
  }

  if (ImGui::Button(collectButtonLabel))
  {
    if (collecting)
    {
      for (DataModuleEntry &entry : dataModules)
        entry.collector->pauseCollection();
    }
    else if (isAnyPaused())
    {
      for (DataModuleEntry &entry : dataModules)
        entry.collector->resumeCollection();
    }
    else
      collectData();
  }

  if (applyPauseStyle)
    ImGui::PopStyleColor(4);

  ImGui::SameLine();

  // Export button (shared exporter currently owned by texture tab UI)
  if (!tabs.empty())
  {
    TextureProfilerUI *texUI = static_cast<TextureProfilerUI *>(tabs[0].module);
    if (currentTabIndex == 0)
    {
      texUI->getExporter().setFilenameBase(tabs[0].name);
      texUI->getExporter().setTextureTable(texUI->getTextureTable());
      texUI->getExporter().setRiTable(nullptr);
      texUI->getExporter().drawExportButton();
      texUI->getExporter().drawExportMenu();
    }
    else if (currentTabIndex == 1 && riProfilerUI)
    {
      texUI->getExporter().setFilenameBase(tabs[1].name);
      texUI->getExporter().setTextureTable(nullptr);
      if (LpRiTable *riTablePtr = riProfilerUI->getRiTable())
        texUI->getExporter().setRiTable(riTablePtr);
      texUI->getExporter().drawExportButton();
      texUI->getExporter().drawExportMenu();
    }
  }

  ImGui::Separator();

  float statusBarHeight = ImGui::GetTextLineHeightWithSpacing() * 2.5f;
  ImVec2 contentAvail = ImGui::GetContentRegionAvail();
  float tabAreaHeight = eastl::max(0.0f, contentAvail.y - statusBarHeight);

  ImGui::BeginChild("TabArea", ImVec2(0.0f, tabAreaHeight), ImGuiConstants::NO_BORDER);
  drawTabBar();
  ImGui::EndChild();

  drawRenamePopup();

  ImGui::Separator();

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.12f, 0.9f));
  ImGui::BeginChild("StatusBar", ImVec2(0.0f, statusBarHeight), ImGuiConstants::WITH_BORDER,
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  drawStatusBar();
  ImGui::EndChild();
  ImGui::PopStyleColor();

  ImGui::End();

  if (toastAdapter)
    toastAdapter->draw();
}

bool LevelProfilerUI::isAnyCollecting() const
{
  for (const DataModuleEntry &entry : dataModules)
    if (entry.collector->isCollecting())
      return true;
  return false;
}

float LevelProfilerUI::collectProgress() const
{
  // Only the collectors that are still working: a synchronous one reports 1.0 and would otherwise
  // drag the bar to the middle of its range for the whole walk.
  float sum = 0.0f;
  int count = 0;
  for (const DataModuleEntry &entry : dataModules)
    if (entry.collector->isCollecting() || entry.collector->isPaused())
    {
      sum += entry.collector->getCollectProgress();
      count++;
    }
  return count ? sum / count : 1.0f;
}

bool LevelProfilerUI::isAnyPaused() const
{
  for (const DataModuleEntry &entry : dataModules)
    if (entry.collector->isPaused())
      return true;
  return false;
}

void LevelProfilerUI::collectData()
{
  // The tabs read one level snapshot and depend on each other's data, so collection is one action
  // over every collector rather than a per-tab one. An incremental collector keeps working after
  // its collect() returns and is pumped from drawUI.
  for (DataModuleEntry &entry : dataModules)
    entry.collector->collect();

  collectWasBusy = isAnyBusy();
  notifyTabsDataCollected();
}

void LevelProfilerUI::notifyTabsDataCollected()
{
  for (auto &tab : tabs)
    tab.module->onDataCollected();
}

void LevelProfilerUI::clearData()
{
  for (DataModuleEntry &entry : dataModules)
    entry.collector->clear();
}

void LevelProfilerUI::addTab(const char *name, IProfilerModule *module_ptr) { tabs.push_back(ProfilerTab(name, module_ptr)); }

ProfilerTab *LevelProfilerUI::getTab(int index)
{
  if (index >= 0 && index < static_cast<int>(tabs.size()))
    return &tabs[index];

  return nullptr;
}

void LevelProfilerUI::renameTab(int index, const char *new_name)
{
  if (index >= 0 && index < static_cast<int>(tabs.size()))
    tabs[index].name = new_name;
}

void LevelProfilerUI::drawTabBar()
{
  if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_Reorderable))
  {
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
    {
      ImGui::PushID(i);

      bool isTabVisible = ImGui::BeginTabItem(tabs[i].name.c_str());

      // Handle right-click for renaming
      bool isTabHovered = ImGui::IsItemHovered();
      bool isRightClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

      if (isTabVisible)
      {
        if (currentTabIndex != i)
        {
          currentTabIndex = i;
          if (ICopyProvider *provider = tabs[i].module->getCopyProvider())
            globalCopyManager.setActiveProvider(provider);
        }

        tabs[i].module->drawUI();
        ImGui::EndTabItem();
      }

      // Apply right-click logic after tab content is processed
      if (isTabHovered && isRightClicked)
      {
        renameTabIndex = i;
        renameBuffer = tabs[i].name;
        isOpenRenamePopup = true; // Signal to open the rename popup.
      }

      ImGui::PopID();
    }
    ImGui::EndTabBar();
  }
}

void LevelProfilerUI::drawRenamePopup()
{
  // Open popup if requested
  if (isOpenRenamePopup)
  {
    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
    ImGui::OpenPopup("Rename Tab");
    isOpenRenamePopup = false; // Reset flag after opening.
  }

  if (ImGui::BeginPopupModal("Rename Tab", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    renameBuffer.resize(64);

    if (ImGui::InputText("New name", renameBuffer.data(), renameBuffer.capacity()))
      renameBuffer.resize(strlen(renameBuffer.c_str()));

    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);

    if (ImGui::Button("OK") || enterPressed)
    {
      renameTab(renameTabIndex, renameBuffer.c_str());
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel") || escapePressed)
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void LevelProfilerUI::drawStatusBar()
{
  if (!riModule)
  {
    ImGui::TextUnformatted("RI module unavailable.");
    return;
  }

  if (isAnyCollecting())
  {
    // The status bar child has no scrollbar, so the message keeps its own line: chaining the
    // indicators after it with SameLine pushes them past the clip rect in a narrow window.
    ImGui::TextUnformatted("Counting rendinst objects on the level (table updates live). Closing the window pauses collection.");

    float frameHeight = ImGui::GetFrameHeight();

    ImGui::ProgressBar(collectProgress(), ImVec2(160.0f, frameHeight));

    // The detail below is riGen specific. A second incremental collector would need this line
    // split per collector, or reduced to the combined progress bar above.
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Layers %d/%d", riModule->getCollectCompletedLayers(), riModule->getCollectLayerCount());

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Cells %u/%u", static_cast<unsigned>(riModule->getCollectProcessedCells()),
      static_cast<unsigned>(riModule->getCollectTotalCells()));

    return;
  }

  if (riModule->wasCollectCancelled())
  {
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.82f, 0.24f, 1.0f));
    ImGui::TextUnformatted("RI collection stopped: the level was unloaded. Press Collect Data to start over.");
    ImGui::PopStyleColor();

    return;
  }

  if (riModule->hasProvisionalRiData())
  {
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.82f, 0.24f, 1.0f));
    ImGui::TextUnformatted("RI data incomplete. Rendinst counts may differ. Resume or recollect for accurate totals.");
    ImGui::PopStyleColor();

    return;
  }

  if (riModule->getRiData().empty())
  {
    ImGui::TextUnformatted("Press Collect Data to begin.");
    return;
  }

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Data collected.");
}

} // namespace levelprofiler