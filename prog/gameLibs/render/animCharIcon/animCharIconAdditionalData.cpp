// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <render/animCharIconAdditionalData.h>

#include <ioSys/dag_dataBlock.h>
#include <debug/dag_debug.h>
#include <string.h>

namespace animchar_icon
{
AdditionalDataPacker *AdditionalDataPacker::tail = nullptr;

static const AdditionalDataPacker *find_packer(const char *name)
{
  for (const AdditionalDataPacker *p = AdditionalDataPacker::tail; p; p = p->next)
    if (strcmp(p->name, name) == 0)
      return p;
  return nullptr;
}

void fill_additional_data(const DataBlock &animchar_blk, ecs::Point4List &additional_data)
{
  additional_data.clear();
  const DataBlock *dataBlk = animchar_blk.getBlockByName("animchar_additional_data");
  if (!dataBlk)
    return;
  for (int i = 0; i < dataBlk->blockCount(); ++i)
  {
    const DataBlock *blk = dataBlk->getBlock(i);
    if (const AdditionalDataPacker *packer = find_packer(blk->getBlockName()))
      packer->pack(*blk, additional_data);
    else
      logerr("icon animchar '%s': no additional data packer for '%s'", animchar_blk.getStr("animchar", "?"), blk->getBlockName());
  }
}
} // namespace animchar_icon
