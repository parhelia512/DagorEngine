//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <daECS/core/componentType.h>
#include <daECS/core/componentTypes.h>

class DataBlock;

namespace animchar_icon
{
// One packer per block name under animchar_additional_data{} in an icon description.
// pack() reads its block and writes the slot into the icon animchar's additional data.
// Register a packer with REGISTER_ANIMCHAR_ICON_ADDITIONAL_DATA in the lib that owns the slot.
// The registering module must list animchar_icon_additional_data_<block_name> in its jamfile
// AddPullVars, or the linker drops the registration with the unreferenced object file.
// List it under the same HaveRenderer condition as the registering source, or a dedicated
// server build references a symbol that no compiled file defines.
// The pull variable is a global symbol, so two registrations of one block name fail to link.
struct AdditionalDataPacker
{
  using PackFn = void (*)(const DataBlock &blk, ecs::Point4List &additional_data);

  const char *name;
  PackFn pack;
  AdditionalDataPacker *next;
  static AdditionalDataPacker *tail;

  AdditionalDataPacker(const char *name_, PackFn pack_) : name(name_), pack(pack_), next(tail) { tail = this; }
};

void fill_additional_data(const DataBlock &animchar_blk, ecs::Point4List &additional_data);
} // namespace animchar_icon

#define REGISTER_ANIMCHAR_ICON_ADDITIONAL_DATA(block_name, pack_fn) \
  ECS_DEF_PULL_VAR(animchar_icon_additional_data_##block_name);     \
  static animchar_icon::AdditionalDataPacker animchar_icon_additional_data_packer_##block_name(#block_name, pack_fn)
