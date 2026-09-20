// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <util/dag_bitFlagsMask.h>
#include <3d/dag_materialData.h>
#include <shaders/dag_shaders.h>
#include <daFracture/core/meshSlicing.h>

class DataBlock;
namespace rendinst
{
struct RendInstDesc;
}

namespace rendinstdestr
{

struct RiDestrMaterial
{
  Ptr<MaterialData> matData;
  Ptr<ShaderMaterial> shMat;
  Ptr<ShaderElement> shElem;
};

enum class FrxDestrFlag : uint8_t
{
  Shatter = 1 << 0
};
using FrxDestrFlags = BitFlagsMask<FrxDestrFlag>;
BITMASK_DECLARE_FLAGS_OPERATORS(FrxDestrFlag);

struct FrxDestrProps
{
  FrxDestrFlags flags;
  frx::ShatterMaterialProfile shatterProfile;
  const RiDestrMaterial *interiorMat = nullptr;

  void loadFromBlk(const DataBlock &blk);
};

struct DestrObjProps
{
  float timeToLive = 15.f;
  float inactiveTimeBeforeSink = -1.f;
  float timeToKinematic = -1.f;
  float timeToSinkUnderground = -1.f;
  float timeToStartDisintegration = -1.f;
  float disintegrationDuration = 0.f;
  float disintegrationScale = 1.f;
  bool keepAlive = false;
  bool interactiveDebris = false;

  void loadFromBlk(const DataBlock &blk);
};

// per rendinst type by value: params{} block in presets, flat keys in the rendinst block
struct RiDestrParams
{
  void loadFromBlk(const DataBlock &blk);
};

struct RiDestrProps
{
  RiDestrParams params;
  const FrxDestrProps *frxDestr = nullptr;
  const DestrObjProps *destrObj = nullptr;
};

void init_ri_destr_props();
void shutdown_ri_destr_props();

// null for a rendinst type without a destruction entry; props and features live until the rendinst config is cleared
const RiDestrProps *get_ri_destr_props(const rendinst::RendInstDesc &desc);
const RiDestrMaterial *get_or_load_ri_destr_material(const char *name);

} // namespace rendinstdestr
