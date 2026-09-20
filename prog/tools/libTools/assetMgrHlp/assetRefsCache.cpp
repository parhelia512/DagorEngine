// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <assets/assetRefsCache.h>
#include <assets/asset.h>
#include <assets/assetMgr.h>
#include <debug/dag_debug.h>
#include <ioSys/dag_dataBlock.h>
#include <ioSys/dag_fileIo.h>
#include <ioSys/dag_memIo.h>
#include <libTools/util/makeBindump.h>
#include <osApiWrappers/dag_direct.h>
#include <osApiWrappers/dag_files.h>

// The version must be increased when the file format or the reference gathering code changes.
static constexpr int ASSET_REFERENCES_CACHE_FILE_VERSION = 1;
static constexpr int ASSET_REFERENCES_CACHE_FILE_MAGIC = _MAKE4C('ARCF'); // ARCF = asset reference cache file
static constexpr int ASSET_REFERENCES_CACHE_FILE_END = _MAKE4C('.end');

G_STATIC_ASSERT(AssetExportCache::HASH_SZ == 16); // Because of the file format.

static bool load_name_map(IGenLoad &crd, OAHashNameMap<true> &names, int count)
{
  String tempString;
  for (int i = 0; i < count; ++i)
  {
    crd.readString(tempString);
    if (i != names.addNameId(tempString))
      return false; // Name duplication should not happen.
  }
  return true;
}

static void save_name_map(IGenSave &cwr, const OAHashNameMap<true> &names)
{
  cwr.writeInt(names.nameCount());
  iterate_names(names, [&](int, const char *name) { cwr.writeString(name); });
}

static void append_blk_hash(const DataBlock &blk, uint8_t inout_hash[AssetExportCache::HASH_SZ])
{
  DynamicMemGeneralSaveCB cwr(tmpmem, 4096, 4096);
  blk.saveToTextStreamCompact(cwr);
  AssetExportCache::sharedDataAppendHash(cwr.data(), cwr.size(), inout_hash);
}

static bool can_store_references(dag::ConstSpan<IDagorAssetRefProvider::Ref> refs)
{
  for (const IDagorAssetRefProvider::Ref &ref : refs)
  {
    const char *referenceName = ref.getBrokenRef();
    if (referenceName && *referenceName)
      return false; // A named broken reference is an error that must be reported.
    if (!referenceName && !ref.getAsset())
      return false; // Not broken and not resolved. This is an error. It is used for example in AnimCharRefs and LandClassRefs.
  }

  return true;
}

AssetRefsCache::AssetRefsCache(const char *asset_type_name, const char *cache_file_name, const char *cache_file_path,
  unsigned gameres_class_id, const DataBlock *global_extra_blk, bool read_only) :
  assetTypeName(asset_type_name),
  cacheFileName(cache_file_name),
  cacheFilePath(cache_file_path),
  gameresClassId(gameres_class_id),
  readOnly(read_only)
{
  if (global_extra_blk)
    globalExtra.setFrom(global_extra_blk);
}

AssetRefsCache::~AssetRefsCache()
{
  WinAutoLock lock(critSec);

  if (!readOnly && mgr && dirty)
  {
    dd_mkpath(cacheFilePath);

    const String tmpPath = String::mk_str_cat(cacheFilePath, ".tmp");

    // AssetExportCache::save() reports only the failure to open the file, every other error arrives as a SaveException.
    bool saved = false;
    DAGOR_TRY
    {
      mkbindump::BinDumpSaveCB cwr(4 * 1024 * 1024, _MAKE4C('PC'), false);
      saveCacheFile(cwr);
      saved = c4.save(tmpPath, *mgr, &cwr);
    }
    DAGOR_CATCH(const IGenSave::SaveException &) { saved = false; }

    if (saved)
    {
      dd_erase(cacheFilePath);
      if (!dd_rename(tmpPath, cacheFilePath))
        logwarn("AssetRefsCache(%s): cannot rename \"%s\" to \"%s\".", cacheFileName.c_str(), tmpPath.c_str(), cacheFilePath.c_str());
    }
    else
      logwarn("AssetRefsCache(%s): cannot write \"%s\".", cacheFileName.c_str(), tmpPath.c_str());
  }

  if (hitCount != 0 || missCount != 0)
    logdbg("AssetRefsCache(%s): %u hits, %u misses", cacheFileName.c_str(), hitCount, missCount);
}

void AssetRefsCache::lazyLoadCacheFile(const DagorAssetMgr &asset_mgr)
{
  if (loaded)
    return;
  loaded = true;
  mgr = &asset_mgr;
  assetTypeId = asset_mgr.getAssetTypeId(assetTypeName);
  G_ASSERT(assetTypeId >= 0); // The reference provider that made this cache is registered for the type, so the type exists.

  // If the cache file is missing, old or truncated then reset everything.
  int cacheFileStartPosition = 0;
  if (!c4.load(cacheFilePath, asset_mgr, &cacheFileStartPosition) || !loadCacheFile(cacheFileStartPosition))
  {
    if (!readOnly)
      dd_erase(cacheFilePath);

    c4.reset();
    assetNames.reset();
    allReferencedAssetNames.reset();
    assetReferencePointers.clear();
    allReferences.clear();
  }

  append_blk_hash(globalExtra, globalExtraHash);

  logdbg("AssetRefsCache(%s): loaded %d total references for %d assets", cacheFileName.c_str(), allReferences.size(),
    assetReferencePointers.size());
}

bool AssetRefsCache::loadCacheFile(int start_position)
{
  FullFileLoadCB crd(cacheFilePath, DF_READ | DF_IGNORE_MISSING);
  if (!crd.fileHandle)
    return false;

  // Use the file size to validate the read counts. No count should exceed it.
  const int fileSize = df_length(crd.fileHandle);

  Tab<int> savedAssetTypeToCurrentType(tmpmem);

  DAGOR_TRY
  {
    crd.seekto(start_position);
    if (crd.beginBlock() <= 0)
      return false;
    if (crd.readInt() != ASSET_REFERENCES_CACHE_FILE_MAGIC || crd.readInt() != ASSET_REFERENCES_CACHE_FILE_VERSION)
      return false;

    String tempString;
    const int assetTypeCount = crd.readInt();
    if (assetTypeCount < 0 || assetTypeCount > fileSize)
      return false;
    savedAssetTypeToCurrentType.resize(assetTypeCount);
    for (int i = 0; i < assetTypeCount; ++i)
    {
      crd.readString(tempString);
      savedAssetTypeToCurrentType[i] = mgr->getAssetTypeId(tempString);
    }

    const int referencedAssetNameCount = crd.readInt();
    if (referencedAssetNameCount < 0 || referencedAssetNameCount > fileSize)
      return false;
    if (!load_name_map(crd, allReferencedAssetNames, referencedAssetNameCount))
      return false;

    const int assetNamesCount = crd.readInt();
    if (assetNamesCount < 0 || assetNamesCount > fileSize)
      return false;
    if (!load_name_map(crd, assetNames, assetNamesCount))
      return false;
    assetReferencePointers.resize(assetNamesCount);
    crd.readTabData(assetReferencePointers);

    const int allReferencesCount = crd.readInt();
    if (allReferencesCount < 0 || allReferencesCount > fileSize)
      return false;
    allReferences.resize(allReferencesCount);
    crd.readTabData(allReferences);

    if (crd.readInt() != ASSET_REFERENCES_CACHE_FILE_END)
      return false;
    crd.endBlock();
  }
  DAGOR_CATCH(const IGenLoad::LoadException &)
  {
    logwarn("AssetRefsCache(%s): error reading the cache file \"%s\".", cacheFileName.c_str(), cacheFilePath.c_str());
    return false;
  }

  // Validate input.
  for (const AssetReferencePointer &r : assetReferencePointers)
    if (r.count > 0 && (r.index < 0 || (int64_t(r.index) + r.count) > allReferences.size()))
      return false;
  for (AssetReference &ar : allReferences)
  {
    if (ar.referencedAssetNameId >= (int)allReferencedAssetNames.nameCount())
      return false;
    if (ar.assetTypeId < -1 || ar.assetTypeId >= (int)savedAssetTypeToCurrentType.size())
      return false;
    if (ar.assetTypeId >= 0)
      ar.assetTypeId = savedAssetTypeToCurrentType[ar.assetTypeId];
  }

  return true;
}

void AssetRefsCache::saveCacheFile(mkbindump::BinDumpSaveCB &cwr)
{
  Tab<int> liveAssetReferencePointers(tmpmem);
  liveAssetReferencePointers.reserve(assetReferencePointers.size());
  for (int i = 0; i < assetReferencePointers.size(); ++i)
    if (!assetReferencePointers[i].isMarkedForDeletion())
      liveAssetReferencePointers.push_back(i);

  Tab<AssetReferencePointer> outputAssetReferencePointers(tmpmem);
  Tab<AssetReference> outputAllReferences(tmpmem);
  OAHashNameMap<true> outputAllReferencedAssetNames;
  outputAssetReferencePointers.reserve(liveAssetReferencePointers.size());
  outputAllReferences.reserve(allReferences.size());
  for (int assetNameIndex : liveAssetReferencePointers)
  {
    const AssetReferencePointer &inputArp = assetReferencePointers[assetNameIndex];
    AssetReferencePointer &outputArp = outputAssetReferencePointers.push_back();
    outputArp.index = outputAllReferences.size();
    outputArp.count = inputArp.count;
    memcpy(outputArp.inputHash, inputArp.inputHash, sizeof(outputArp.inputHash));

    // Save only the used asset names, otherwise a name would stay in the file forever.
    for (int i = 0; i < inputArp.count; ++i)
    {
      const AssetReference &inputAr = allReferences[inputArp.index + i];
      AssetReference &outputAr = outputAllReferences.push_back();
      outputAr = inputAr;
      if (!inputAr.isBrokenWithoutName())
      {
        const char *assetName = allReferencedAssetNames.getName(inputAr.referencedAssetNameId);
        outputAr.referencedAssetNameId = outputAllReferencedAssetNames.addNameId(assetName);
      }
    }
  }

  DynamicMemGeneralSaveCB mem(tmpmem, 256 * 1024, 64 * 1024);
  mem.writeInt(ASSET_REFERENCES_CACHE_FILE_MAGIC);
  mem.writeInt(ASSET_REFERENCES_CACHE_FILE_VERSION);

  const int assetTypeCount = mgr->getAssetTypesCount();
  mem.writeInt(assetTypeCount);
  for (int i = 0; i < assetTypeCount; ++i)
    mem.writeString(mgr->getAssetTypeName(i));

  save_name_map(mem, outputAllReferencedAssetNames);

  mem.writeInt(liveAssetReferencePointers.size());
  for (int assetNameIndex : liveAssetReferencePointers)
    mem.writeString(assetNames.getName(assetNameIndex));
  if (!outputAssetReferencePointers.empty())
    mem.write(outputAssetReferencePointers.data(), data_size(outputAssetReferencePointers));

  mem.writeInt(outputAllReferences.size());
  if (!outputAllReferences.empty())
    mem.write(outputAllReferences.data(), data_size(outputAllReferences));

  mem.writeInt(ASSET_REFERENCES_CACHE_FILE_END);

  cwr.writeRaw(mem.data(), mem.size());
}

bool AssetRefsCache::calculateInputHash(const DagorAsset &asset, dag::ConstSpan<SimpleString> source_files,
  dag::ConstSpan<uint8_t> extra_source_bytes, dag::ConstSpan<IDagorAssetRefProvider::Ref> refs,
  uint8_t out_hash[AssetExportCache::HASH_SZ], SimpleString *out_unusable_file) const
{
  memcpy(out_hash, globalExtraHash, AssetExportCache::HASH_SZ);
  append_blk_hash(asset.props, out_hash);

  bool usable = true;
  for (const SimpleString &filePath : source_files)
  {
    unsigned fileSize;
    const unsigned fileModTime = AssetExportCache::getFileTime(filePath, fileSize);
    if (fileModTime == 0)
    {
      // Refuse missing files. This causes a cache miss in get(), and prevents storing it in put().
      usable = false;
      if (out_unusable_file && out_unusable_file->empty())
        *out_unusable_file = filePath;
      continue;
    }

    // The reason we don't use content hashing for the source files (AssetExportCache::sharedDataGetFileHash()) is that
    // in most cases when a file's last modification time changes the file most likely really changed (especially if it
    // is coming from a version control system). Getting asset references is not that slow, and not using content hash
    // saves a lot of time when the asset file hash and the asset reference cache are empty or fully stale.

    TwoStepRelPath::storage_t tempStorage;
    const char *relativeFilePath = AssetExportCache::mkRelPath(filePath, tempStorage);
    AssetExportCache::sharedDataAppendHash(relativeFilePath, strlen(relativeFilePath), out_hash);

    AssetExportCache::sharedDataAppendHash(&fileSize, sizeof(fileSize), out_hash);
    AssetExportCache::sharedDataAppendHash(&fileModTime, sizeof(fileModTime), out_hash);
  }

  if (!extra_source_bytes.empty())
    AssetExportCache::sharedDataAppendHash(extra_source_bytes.data(), extra_source_bytes.size(), out_hash);

  // A referenced asset's own properties (e.g. a proxyMat's texture-selection rules) can change which references are
  // valid even when the referencing asset's own file and properties are untouched.
  for (const IDagorAssetRefProvider::Ref &ref : refs)
    if (const DagorAsset *refAsset = ref.getAsset())
    {
      const String nameTypified = refAsset->getNameTypified();
      AssetExportCache::sharedDataAppendHash(nameTypified.c_str(), nameTypified.length(), out_hash);
      append_blk_hash(refAsset->props, out_hash);
    }

  return usable;
}

bool AssetRefsCache::resolve(const AssetReferencePointer &arp, const DagorAssetMgr &asset_mgr,
  Tab<IDagorAssetRefProvider::Ref> &out_refs)
{
  G_ASSERT(!arp.isMarkedForDeletion()); // Already checked in get().

  for (int i = 0; i < arp.count; ++i)
  {
    const AssetReference &ar = allReferences[arp.index + i];
    IDagorAssetRefProvider::Ref &ref = out_refs.push_back();

    if (ar.isBrokenWithoutName())
    {
      // A broken reference with no name is not an error. (See VehicleRefs.)
      ref.setBrokenRef("");
      ref.flags |= ar.flags;
      continue;
    }

    DagorAsset *refAsset = asset_mgr.findAsset(allReferencedAssetNames.getName(ar.referencedAssetNameId), ar.assetTypeId);
    if (!refAsset)
      return false;

    ref.refAsset = refAsset;
    ref.flags = ar.flags;
  }

  return true;
}

void AssetRefsCache::dropAsset(const char *asset_name)
{
  const int assetNameIndex = assetNames.getNameId(asset_name);
  if (assetNameIndex < 0 || assetReferencePointers[assetNameIndex].isMarkedForDeletion())
    return;
  assetReferencePointers[assetNameIndex].markForDeletion();
  dirty = true;
}

void AssetRefsCache::addAsset(const char *asset_name, dag::ConstSpan<IDagorAssetRefProvider::Ref> refs,
  const uint8_t input_hash[AssetExportCache::HASH_SZ])
{
  const int assetNameIndex = assetNames.addNameId(asset_name);
  if (assetNameIndex >= assetReferencePointers.size())
  {
    G_ASSERT(assetNameIndex == assetReferencePointers.size());
    append_items(assetReferencePointers, 1);
  }

  assetReferencePointers[assetNameIndex].index = allReferences.size();
  assetReferencePointers[assetNameIndex].count = refs.size();
  memcpy(assetReferencePointers[assetNameIndex].inputHash, input_hash, AssetExportCache::HASH_SZ);
  for (const IDagorAssetRefProvider::Ref &ref : refs)
  {
    AssetReference &ar = allReferences.push_back();
    ar.flags = ref.flags;
    if (DagorAsset *refAsset = ref.getAsset())
    {
      ar.referencedAssetNameId = allReferencedAssetNames.addNameId(refAsset->getName());
      ar.assetTypeId = refAsset->getType();
    }
    else
    {
      ar.setBrokenWithoutName();
    }
  }

  dirty = true;
}

bool AssetRefsCache::get(DagorAsset &asset, dag::ConstSpan<SimpleString> source_files, dag::ConstSpan<uint8_t> extra_source_bytes,
  Tab<IDagorAssetRefProvider::Ref> &out_refs)
{
  WinAutoLock lock(critSec);
  lazyLoadCacheFile(asset.getMgr());

  G_ASSERT(assetTypeId >= 0);
  G_ASSERT(asset.getType() == assetTypeId);

  const int assetNameIndex = assetNames.getNameId(asset.getName());
  if (assetNameIndex < 0 || assetReferencePointers[assetNameIndex].isMarkedForDeletion())
  {
    ++missCount;
    return false;
  }

  // Honor the always-rebuild type list (AssetExportCache::sharedDataAddRebuildType()).
  if (c4.checkAssetExpVerChanged(assetTypeId, gameresClassId, ASSET_REFERENCES_CACHE_FILE_VERSION))
  {
    ++missCount;
    return false;
  }

  out_refs.clear();
  if (!resolve(assetReferencePointers[assetNameIndex], asset.getMgr(), out_refs))
  {
    ++missCount;
    return false;
  }

  uint8_t inputHash[AssetExportCache::HASH_SZ];
  if (!calculateInputHash(asset, source_files, extra_source_bytes, out_refs, inputHash) ||
      memcmp(inputHash, assetReferencePointers[assetNameIndex].inputHash, AssetExportCache::HASH_SZ) != 0)
  {
    ++missCount;
    return false;
  }

  ++hitCount;
  return true;
}

void AssetRefsCache::put(DagorAsset &asset, dag::ConstSpan<SimpleString> source_files, dag::ConstSpan<uint8_t> extra_source_bytes,
  dag::ConstSpan<IDagorAssetRefProvider::Ref> refs, bool source_parsed_ok)
{
  if (readOnly)
    return;

  WinAutoLock lock(critSec);
  lazyLoadCacheFile(asset.getMgr());

  G_ASSERT(assetTypeId >= 0);
  G_ASSERT(asset.getType() == assetTypeId);

  bool storable = source_parsed_ok && can_store_references(refs);

  uint8_t inputHash[AssetExportCache::HASH_SZ];
  SimpleString unusableFile;
  if (storable && !calculateInputHash(asset, source_files, extra_source_bytes, refs, inputHash, &unusableFile))
  {
    storable = false;

    if (!statFailReported)
    {
      statFailReported = true;
      logwarn("AssetRefsCache(%s): input file \"%s\" of asset \"%s\" does not exist. Not caching it.", cacheFileName.c_str(),
        unusableFile.c_str(), asset.getName());
    }
  }

  if (!storable)
  {
    dropAsset(asset.getName());
    return;
  }

  c4.setAssetExpVer(assetTypeId, gameresClassId, ASSET_REFERENCES_CACHE_FILE_VERSION);
  addAsset(asset.getName(), refs, inputHash); //-V614 Use of uninitialized variable.
}

eastl::unique_ptr<AssetRefsCache> AssetRefsCache::createCache(const char *asset_type_name, unsigned gameres_class_id,
  const DataBlock &app_blk, const DataBlock *global_extra_blk)
{
  G_ASSERT(asset_type_name && *asset_type_name);
  const String cacheFileName(0, "assets-refs-%s.c4.bin", asset_type_name);

  // Disable the cache if strip_d3dres is set, because add_dag_texture_and_proxymat_refs() won't return with anything.
  if (app_blk.getBool("strip_d3dres", false))
  {
    logdbg("AssetRefsCache(%s): disabled, strip_d3dres is set in application.blk.", cacheFileName.c_str());
    return nullptr;
  }

  if (!app_blk.getBlockByNameEx("assets")->getBlockByNameEx("build")->getBool("cacheAssetRefs", true))
  {
    logdbg("AssetRefsCache(%s): disabled, due to assets{build{cacheAssetRefs:b=no}} in application.blk.", cacheFileName.c_str());
    return nullptr;
  }

  // Cannot use %appDir or assetlocalprops::makePath() inside daBuild plugin DLLs.
  const char *appDir = app_blk.getStr("appDir", nullptr);
  if (!appDir || !*appDir)
  {
    logdbg("AssetRefsCache(%s): disabled, appDir is not set in application.blk.", cacheFileName.c_str());
    return nullptr;
  }

  String cacheFilePath(0, "%s/develop/.asset-local/%s", appDir, cacheFileName.c_str());
  simplify_fname(cacheFilePath);

  // daBuild job processes each hold only their own packs' assets, so letting them write would make the last
  // one to exit overwrite everybody else's records. dabuildJobCount is set only by the daBuild jobs.
  const bool readOnly = app_blk.getInt("dabuildJobCount", -1) >= 0;

  return eastl::unique_ptr<AssetRefsCache>(
    new AssetRefsCache(asset_type_name, cacheFileName, cacheFilePath, gameres_class_id, global_extra_blk, readOnly));
}
