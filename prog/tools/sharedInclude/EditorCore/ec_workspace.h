// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <generic/dag_tab.h>
#include <libTools/containers/dag_StrMap.h>

#include <ioSys/dag_dataBlock.h>

#include <EASTL/optional.h>

struct WspLibData
{
  String name;
  String fname;
};


class EditorWorkspace
{
public:
  EditorWorkspace();
  virtual ~EditorWorkspace();

  // Initialize workspace.blk file to get list of available workspaces
  bool initWorkspaceBlk(const char *path);

  // Delete data loaded in initWorkspaceBlk(). Must be called after workspace init
  //(usually after loadFromBlk() method
  void freeWorkspaceBlk();

  // Returns worksapaces names list
  void getWspNames(Tab<String> &list) const;

  // Gets the names of the workspaces that use this application.blk path, sorted by name.
  // It must be called after initWorkspaceBlk().
  void getWspNamesByAppBlkPath(const char *app_blk_path, Tab<String> &list) const;

  // Gets the name of the workspace that uses this application.blk path.
  // Returns:
  //   - nullopt when several workspaces use the path, and puts the reason in error_message. (That is an error,
  //     because only a human can say which one to use.)
  //   - empty name when no workspace uses the path
  //   - the workspace name when only one workspace uses the path.
  // It must be called after initWorkspaceBlk().
  eastl::optional<String> getWspNameByAppBlkPathIfOnlyOneMatches(const char *app_blk_path, String &error_message) const;

  // Adds a workspace for this application.blk, named after its folder, and returns that name.
  // Returns an empty name when it cannot add the workspace, and puts the reason in error_message.
  // The instance becomes that workspace, loaded from its application.blk, and the new entry is written from that file
  // alone. A workspace loaded before this call is discarded, recent projects and all. Every failure leaves the instance
  // unnamed, and an unnamed instance holds no workspace: the paths a failed load left behind mean nothing until a
  // workspace loads again.
  // It must be called after initWorkspaceBlk().
  String addWspForAppBlkPath(const char *app_blk_path, String &error_message);

  // Loads workspace from BLK. Must be called after initWorkspaceBlk()
  bool load(const char *workspace_name, bool *app_path_set = NULL);
  // Loads workspace directly from "application.blk" file
  bool loadIndirect(const char *app_blk_path);

  // Saves workspace in BLK
  bool save();

  // Remove current workspace from BLK
  bool remove();

  inline void setName(const char *new_name) { name = new_name; }
  void setAppPath(const char *new_path);

  inline const char *getName() const { return name; }
  inline const char *getAppDir() const { return appDir; }
  inline const char *getAppBlkPath() const { return appBlkPath; }
  inline const char *getAppBlkShortName() const { return appBlkShortName; }
  inline const char *getSdkDir() const { return sdkDir; }
  inline const char *getLibDir() const { return libDir; }
  inline const char *getLevelsDir() const { return levelsDir; }
  inline const char *getResDir() const { return resDir; }
  inline const char *getScriptDir() const { return scriptDir; }
  inline const char *getAssetScriptDir() const { return assetScriptsDir; }
  inline const char *getGameDir() const { return gameDir; }
  inline const char *getLevelsBinDir() const { return levelsBinDir; }
  inline const char *getPhysmatPath() const { return physmatPath; }
  inline const char *getScriptLibrary() const { return scriptLibrary; }
  inline const char *getCollisionName() const { return collisionName; }
  inline const char *getSceneDir() const { return sceneDir; }

  inline const Tab<String> &getDagorEdDisabled() const { return deDisabled; }
  inline const Tab<String> &getResourceEdDisabled() const { return reDisabled; }

  inline dag::ConstSpan<unsigned> getAdditionalPlatforms() const { return platforms; }
  static unsigned getPlatformFromStr(const char *platf);
  static const char *getPlatformNameFromId(unsigned plt);

  inline float getMaxTraceDistance() const { return maxTraceDistance; }

  const Tab<String> &getMountPoints() const { return mountPoints; }

  inline bool isUsingDngBasedSceneRender() const { return useDngBasedSceneRender; }

protected:
  String blkPath;

  struct Workspaces
  {
    DataBlock blk;
    StriMap<DataBlock *> names;

    Workspaces() : names(tmpmem) {}
  };

  Workspaces *wspData;

  // Load application-specific data from BLK
  virtual bool loadSpecific([[maybe_unused]] const DataBlock &blk) { return true; }
  // Load application-specific data from application.blk
  virtual bool loadAppSpecific([[maybe_unused]] const DataBlock &blk) { return true; }

  // Save application-specific data in BLK
  // Must use only DataBlock::set... methods and DataBlock::addBlock to avoid rewrite other
  // application data
  virtual bool saveSpecific([[maybe_unused]] DataBlock &blk) { return true; }

  virtual bool createApplicationBlk(const char *path) const;

private:
  String name;
  String appDir;
  String appBlkPath;
  String appBlkShortName;

  String sdkDir;
  String libDir;
  String levelsDir;
  String resDir;
  String scriptDir;
  String assetScriptsDir;
  String gameDir;
  String levelsBinDir;
  String physmatPath;
  String scriptLibrary;

  String collisionName;
  String sceneDir;

  Tab<String> deDisabled;
  Tab<String> reDisabled;

  Tab<unsigned> platforms;

  float maxTraceDistance;

  Tab<String> mountPoints;

  bool useDngBasedSceneRender;

  DataBlock *findWspBlk(DataBlock &blk, const char *wsp_name, bool create_new);

  bool loadFromBlk(DataBlock &blk, bool *app_path_set = NULL);

  // Writes this workspace's fields into wsp_blk.
  bool writeWspBlk(DataBlock &wsp_blk);
};

// Get a suggested workspace name from the path to application.blk. It will be the name of the parent folder.
String get_workspace_name_from_application_blk_path(const char *app_blk_path);
