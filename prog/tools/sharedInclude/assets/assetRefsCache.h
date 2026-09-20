//
// Dagor Tech 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <assets/assetExpCache.h>
#include <assets/assetRefs.h>
#include <generic/dag_tab.h>
#include <ioSys/dag_dataBlock.h>
#include <osApiWrappers/dag_critSec.h>
#include <util/dag_oaHashNameMap.h>
#include <util/dag_simpleString.h>
#include <util/dag_string.h>

#include <EASTL/unique_ptr.h>

class DagorAsset;
class DagorAssetMgr;

// Persistent cache for IDagorAssetRefProvider results for one asset type.
class AssetRefsCache final
{
public:
  ~AssetRefsCache();

  // extra_source_bytes: extra bytes to add to the digest. For example the contents of the compiled
  //   shader dump the gathering reads. get() and put() of the same asset must always pass the same bytes.
  // out_refs: output parameter. It is set to the asset's references. Only valid if the function returns true.
  // Returns true if every stored reference still resolves, no tracked input has changed, and the asset type is not in
  // the always-rebuild list.
  bool get(DagorAsset &asset, dag::ConstSpan<SimpleString> source_files, dag::ConstSpan<uint8_t> extra_source_bytes,
    Tab<IDagorAssetRefProvider::Ref> &out_refs);

  // Store the provided asset's references.
  // source_parsed_ok: it must be false when any source file failed to read.
  void put(DagorAsset &asset, dag::ConstSpan<SimpleString> source_files, dag::ConstSpan<uint8_t> extra_source_bytes,
    dag::ConstSpan<IDagorAssetRefProvider::Ref> refs, bool source_parsed_ok);

  // Create the asset reference cache.
  // asset_type_name: rendInst, dynModel, and so on. It names the cache file and resolves the asset type ID.
  // gameres_class_id: standard game resource class ID. See dag_stdGameResId.h.
  // global_extra_blk: settings from outside any asset that influence every result.
  // Returns null if the cache cannot be enabled.
  static eastl::unique_ptr<AssetRefsCache> createCache(const char *asset_type_name, unsigned gameres_class_id,
    const DataBlock &app_blk, const DataBlock *global_extra_blk = nullptr);

private:
  AssetRefsCache(const char *asset_type_name, const char *cache_file_name, const char *cache_file_path, unsigned class_id,
    const DataBlock *global_extra_blk, bool read_only);

  struct AssetReference
  {
    void setBrokenWithoutName()
    {
      referencedAssetNameId = -1;
      assetTypeId = -1;
    }

    bool isBrokenWithoutName() const { return referencedAssetNameId < 0; }

    int referencedAssetNameId; // Index into allReferencedAssetNames. < 0 for a broken reference with no name.
    int assetTypeId;           // DagorAsset::getType().
    int flags;                 // IDagorAssetRefProvider::RFLG_...
  };

  struct AssetReferencePointer
  {
    // A marked AssetReferencePointer is left out of the cache file at save time.
    void markForDeletion() { index = -1; }
    bool isMarkedForDeletion() const { return index < 0; }

    int index; // Index into allReferences. < 0 means that this record is marked for deletion.
    int count; // Number of AssetReference elements in allReferences. 0 means that the asset has no references.
    uint8_t inputHash[AssetExportCache::HASH_SZ]; // The digest of every input the record was gathered from.
  };

  // Hashes every input of one AssetReferencePointer record: the global settings; the asset properties; the path, size
  // and last modification time of the source files; the caller provided extra source bytes; and the properties of the
  // referenced assets. Each record has its own digest, so an input shared by several assets cannot validate a record
  // that was gathered before that input changed.
  // Returns false when a source file does not exist. The digest is incomplete then, so the record must neither be
  // stored nor reused.
  // out_unusable_file: optional output parameter. It is set to the first source file that does not exist.
  bool calculateInputHash(const DagorAsset &asset, dag::ConstSpan<SimpleString> source_files,
    dag::ConstSpan<uint8_t> extra_source_bytes, dag::ConstSpan<IDagorAssetRefProvider::Ref> refs,
    uint8_t out_hash[AssetExportCache::HASH_SZ], SimpleString *out_unusable_file = nullptr) const;
  bool resolve(const AssetReferencePointer &arp, const DagorAssetMgr &asset_mgr, Tab<IDagorAssetRefProvider::Ref> &out_refs);
  void addAsset(const char *asset_name, dag::ConstSpan<IDagorAssetRefProvider::Ref> refs,
    const uint8_t input_hash[AssetExportCache::HASH_SZ]);
  void dropAsset(const char *asset_name);
  void lazyLoadCacheFile(const DagorAssetMgr &asset_mgr);
  bool loadCacheFile(int start_position);
  void saveCacheFile(mkbindump::BinDumpSaveCB &cwr);

  const SimpleString assetTypeName;
  const SimpleString cacheFileName;
  const String cacheFilePath;
  const unsigned gameresClassId;
  const bool readOnly;

  int assetTypeId = -1; // Set by lazyLoadCacheFile(). Every record belongs to this type.
  bool loaded = false;
  bool dirty = false;
  bool statFailReported = false;
  unsigned hitCount = 0;
  unsigned missCount = 0;

  DataBlock globalExtra;
  uint8_t globalExtraHash[AssetExportCache::HASH_SZ] = {}; // Set by lazyLoadCacheFile(), the seed of every input digest.
  AssetExportCache c4;
  const DagorAssetMgr *mgr = nullptr;

  OAHashNameMap<true> assetNames;                    // Asset names. The type is assetTypeId for all of them.
  Tab<AssetReferencePointer> assetReferencePointers; // It has the same number of elements as assetNames.
  Tab<AssetReference> allReferences;
  OAHashNameMap<true> allReferencedAssetNames; // Asset names of the referenced assets.

  WinCritSec critSec;
};
