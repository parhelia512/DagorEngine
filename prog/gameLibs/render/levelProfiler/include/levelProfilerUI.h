// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <gui/dag_imgui.h>
#include <EASTL/unique_ptr.h>
#include "levelProfilerInterface.h"
#include "levelProfilerExporter.h"
#include "levelProfilerTextureTable.h"
#include "levelProfilerRiTable.h"
#include "levelProfilerFilter.h"
#include "levelProfilerWidgets.h"

namespace levelprofiler
{

// ImGui constants to improve code readability
namespace ImGuiConstants
{
static constexpr bool WITH_BORDER = true;
static constexpr bool NO_BORDER = false;
} // namespace ImGuiConstants

class TextureProfilerUI : public IProfilerModule, public ICopyProvider
{
public:
  TextureProfilerUI(TextureModule *texture_module, RIModule *ri_module);
  virtual ~TextureProfilerUI();

  TextureModule *getTextureModule() const { return textureModule; }
  RIModule *getRIModule() const { return riModule; }
  LpTextureTable *getTextureTable() const { return textureTable.get(); }

  void init() override;
  void shutdown() override;
  void drawUI() override;
  void onDataCollected() override;
  ICopyProvider *getCopyProvider() override { return this; }

  CopyResult handleGlobalCopy() const override;
  CopyResult handleContextCopy(const CopyRequest &request) const override;
  eastl::vector<ProfilerString> getContextMenuItems() const override;

  ProfilerExporter &getExporter() { return exporter; }
  TextureFilterManager &getFilterManager() { return filterManager; }
  const TextureFilterManager &getFilterManager() const { return filterManager; }

private:
  TextureModule *textureModule = nullptr;
  RIModule *riModule = nullptr;
  TextureFilterManager filterManager;
  ProfilerExporter exporter;
  eastl::unique_ptr<LpTextureTable> textureTable;
  ProfilerString selectedAssetName;
  bool filtersSeeded = false;

  LpSplitter horizontalSplitter;
  LpSplitter verticalSplitter;

  // Tab rename popup
  bool isShowRenamePopup = false;
  int renameTabIndex = -1;
  char renameBuffer[64] = "";

  void drawLeftPanel();  // Table and statistics
  void drawRightPanel(); // Preview and texture usage list

  GlobalCopyManager *getCopyManager() const;
  ProfilerString generateFullTextureInfo(const ProfilerString &texture_name) const;
};

class RIProfilerUI : public IProfilerModule, public ICopyProvider
{
public:
  RIProfilerUI(RIModule *ri_module);
  virtual ~RIProfilerUI();

  RIModule *getRIModule() const { return riModule; }
  LpRiTable *getRiTable() const { return riTable.get(); }

  void init() override;
  void shutdown() override;
  void drawUI() override;
  ICopyProvider *getCopyProvider() override { return this; }

  CopyResult handleGlobalCopy() const override;
  CopyResult handleContextCopy(const CopyRequest &request) const override;
  eastl::vector<ProfilerString> getContextMenuItems() const override;

private:
  RIModule *riModule = nullptr;
  eastl::unique_ptr<LpRiTable> riTable;

  GlobalCopyManager *getCopyManager() const;
  ProfilerString generateFullRiInfo(const ProfilerString &ri_name) const;
};

class LevelProfilerUI : public ILevelProfiler
{
public:
  LevelProfilerUI();
  virtual ~LevelProfilerUI();

  void initialize();

  void init() override;
  void shutdown() override;
  void drawUI() override;
  void collectData() override;
  void clearData() override;
  void addTab(const char *name, IProfilerModule *module_ptr) override;
  int getTabCount() const override { return static_cast<int>(tabs.size()); }
  ProfilerTab *getTab(int index) override;
  void renameTab(int index, const char *new_name) override;

  GlobalCopyManager *getCopyManager() { return &globalCopyManager; }

private:
  GlobalCopyManager globalCopyManager;
  eastl::unique_ptr<ToastNotificationAdapter> toastAdapter;

  eastl::unique_ptr<TextureModule> textureModule;
  eastl::unique_ptr<RIModule> riModule;

  eastl::unique_ptr<TextureProfilerUI> textureProfilerUI;
  eastl::unique_ptr<RIProfilerUI> riProfilerUI;

  // A data module is both a collector and a lifecycle module, and the profiler drives it through
  // both. A new tab needs two calls that nothing links: registerDataModule for the data module
  // and addTab for the UI module. Miss the first and the tab never collects, miss the second
  // and the data is collected with nothing showing it.
  struct DataModuleEntry
  {
    IDataCollector *collector = nullptr;
    IProfilerModule *module = nullptr;
  };

  // In dependency order: a module may read data produced by an earlier one.
  eastl::vector<DataModuleEntry> dataModules;

  template <typename T>
  void registerDataModule(T *data_module)
  {
    dataModules.push_back(DataModuleEntry{data_module, data_module});
  }

  eastl::vector<ProfilerTab> tabs;
  int currentTabIndex = 0;

  bool isOpenRenamePopup = false;
  int renameTabIndex = -1;
  ProfilerString renameBuffer;

  // Armed in collectData rather than sampled from the drawUI edge: a walk short enough to end in
  // the first pump would never be seen busy there. Its falling edge is what re-notifies the
  // tabs, because an incremental collector has only its synchronous part done at collect time.
  bool collectWasBusy = false;

  bool isAnyCollecting() const;
  bool isAnyPaused() const;
  bool isAnyBusy() const { return isAnyCollecting() || isAnyPaused(); }
  float collectProgress() const;
  void notifyTabsDataCollected();

  void drawTabBar();
  void drawRenamePopup();
  void drawStatusBar();
};

} // namespace levelprofiler