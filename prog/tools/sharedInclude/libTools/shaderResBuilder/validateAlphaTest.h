// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EASTL/functional.h>

class DataBlock;
class Mesh;
class MaterialData;
class ShaderMaterial;
class ILogWriter;
class DagorAssetMgr;
class TexImage32;
class IMemAlloc;

namespace AlphaTestValidation
{
using AssetMgrProvider = eastl::function<DagorAssetMgr *()>;
using ImageLoader = eastl::function<TexImage32 *(const char *, IMemAlloc *)>;

void init(const DataBlock &blk, AssetMgrProvider provider, ImageLoader loader);
void shutdown();
void validate(const Mesh &mesh, int mat_id, const MaterialData &mat, const ShaderMaterial &sh_mat, const char *dag_fn,
  const char *node_name, ILogWriter &log);
}; // namespace AlphaTestValidation
