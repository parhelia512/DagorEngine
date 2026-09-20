// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <assets/assetPlugin.h>
#include <assets/assetExpCache.h>
#include <ioSys/dag_dataBlock.h>
#include <util/dag_string.h>

namespace
{

class DaBuildHost final : public IDaBuildHost
{
public:
  explicit DaBuildHost(const DataBlock &app_blk) : appBlk(app_blk) {}

  void *__stdcall getExpCacheSharedData() override { return AssetExportCache::getSharedDataPtr(); }
  const char *__stdcall getAppDir() override { return appBlk.getStr("appDir", "."); }

private:
  const DataBlock &appBlk;
};

} // namespace

void __stdcall IDaBuildPlugin::initGlobals(IDaBuildHost &host)
{
  AssetExportCache::setSharedDataPtr(host.getExpCacheSharedData());
  AssetExportCache::setSdkRoot(host.getAppDir(), "develop");
}

bool dabuild_plugin_init(IDaBuildPlugin &plugin, const DataBlock &app_blk)
{
  DaBuildHost host(app_blk);
  plugin.initGlobals(host);
  return plugin.init(app_blk);
}
