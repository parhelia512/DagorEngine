// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <ioSys/dag_dataBlock.h>
#include <osApiWrappers/dag_localConv.h>

#include <string.h>

// Shared reads of base_nodes.blk. Two search boxes offer this set -- the node library panel and the
// add-node popup -- and one match rule here is what stops them answering differently.

// Only node{} blocks are descriptors; the rest are their pins and properties.
template <typename Fn>
void for_each_node_descriptor(const DataBlock &blk, Fn fn)
{
  for (uint32_t i = 0; i < blk.blockCount(); ++i)
  {
    const DataBlock *node = blk.getBlock(i);
    if (strcmp(node->getBlockName(), "node") == 0)
    {
      fn(*node);
    }
  }
}

// Hidden by the blk, or with no uid for a payload or spawn call to name it. add_template_uids.py
// should have populated every node{}, so a missing uid means the migrator was skipped.
inline bool node_desc_is_offerable(const DataBlock &desc)
{
  return !desc.getBool("hidden", false) && desc.getStr("templateUid", "")[0];
}

// synonyms:t is the keyword list the JS editor searched: comma-separated alternative names a query
// should hit too, e.g. "final,result,output" on the node named "result".
inline bool node_name_matches_search(const char *name, const char *synonyms, const char *query)
{
  return dd_stristr(name, query) || dd_stristr(synonyms, query);
}
