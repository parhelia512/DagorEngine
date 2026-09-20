// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <util/dag_globDef.h>
#include <math/dag_bits.h>
#include <drv/3d/dag_consts.h>
#include <drv/3d/dag_renderPass.h>

#include <drv_assert_defs.h>

namespace render_pass_validation
{
constexpr int STENCIL_LOAD_MASK = RP_TA_LOAD_STENCIL_CLEAR | RP_TA_LOAD_STENCIL_READ | RP_TA_LOAD_STENCIL_NO_CARE;
constexpr int TARGET_LOAD_MASK = RP_TA_LOAD_MASK & ~STENCIL_LOAD_MASK;

inline uint32_t resolve_source_actions(int32_t slot)
{
  return slot == RenderPassExtraIndexes::RP_SLOT_DEPTH_STENCIL ? (RP_TA_SUBPASS_WRITE | RP_TA_SUBPASS_READ) : RP_TA_SUBPASS_WRITE;
}

inline bool validate_action(RenderPassTargetAction action, uint32_t bind_index, const char *name)
{
  G_UNUSED(bind_index);
  G_UNUSED(name);

  bool noErrors = true;

  D3D_CONTRACT_ASSERTF_AND_DO(dag::popcount(unsigned(action & RP_TA_SUBPASS_MASK)) <= 1, noErrors = false,
    "bind %u of render pass '%s' has multiple subpass operations", bind_index, name);
  D3D_CONTRACT_ASSERTF_AND_DO(dag::popcount(unsigned(action & TARGET_LOAD_MASK)) <= 1, noErrors = false,
    "bind %u of render pass '%s' has multiple target load operations", bind_index, name);
  D3D_CONTRACT_ASSERTF_AND_DO(dag::popcount(unsigned(action & STENCIL_LOAD_MASK)) <= 1, noErrors = false,
    "bind %u of render pass '%s' has multiple stencil load operations", bind_index, name);
  D3D_CONTRACT_ASSERTF_AND_DO(dag::popcount(unsigned(action & RP_TA_STORE_MASK)) <= 1, noErrors = false,
    "bind %u of render pass '%s' has multiple store operations", bind_index, name);

  return noErrors;
}
} // namespace render_pass_validation

inline bool validate_render_pass_desc(const RenderPassDesc &rp_desc)
{
  using namespace render_pass_validation;

  const char *name = rp_desc.debugName ? rp_desc.debugName : "<unnamed>";
  bool noErrors = true;

  D3D_CONTRACT_ASSERTF_RETURN(rp_desc.bindCount > 0, false, "render pass '%s' has no binds", name);

  int lastSubpass = RenderPassExtraIndexes::RP_SUBPASS_EXTERNAL_END;
  for (uint32_t i = 0; i < rp_desc.bindCount; ++i)
  {
    const RenderPassBind &bind = rp_desc.binds[i];

    noErrors &= validate_action(bind.action, i, name);

    D3D_CONTRACT_ASSERTF_AND_DO(uint32_t(bind.target) < rp_desc.targetCount, noErrors = false,
      "bind %u of render pass '%s' names target %d, which is out of the %u declared targets", i, name, bind.target,
      rp_desc.targetCount);

    if (bind.slot != RenderPassExtraIndexes::RP_SLOT_DEPTH_STENCIL)
      D3D_CONTRACT_ASSERTF_AND_DO((bind.action & STENCIL_LOAD_MASK) == 0, noErrors = false,
        "bind %u of render pass '%s' carries a stencil load action on target %d, which it does not bind as depth-stencil", i, name,
        bind.target);

    if (bind.action & (RP_TA_LOAD_CLEAR | RP_TA_LOAD_STENCIL_CLEAR))
    {
      int firstSubpass = -1;
      bool firstReads = false;
      for (uint32_t j = 0; j < rp_desc.bindCount; ++j)
      {
        const RenderPassBind &other = rp_desc.binds[j];
        if (other.target != bind.target || other.subpass == RenderPassExtraIndexes::RP_SUBPASS_EXTERNAL_END ||
            (other.action & RP_TA_SUBPASS_ACCESS_MASK) == 0)
          continue;
        if (firstSubpass < 0 || other.subpass < firstSubpass)
        {
          firstSubpass = other.subpass;
          firstReads = (other.action & RP_TA_SUBPASS_READ) != 0;
        }
        else if (other.subpass == firstSubpass && (other.action & RP_TA_SUBPASS_WRITE))
          firstReads = false;
      }
      D3D_CONTRACT_ASSERTF_AND_DO(!firstReads, noErrors = false,
        "bind %u of render pass '%s' clears target %d, which subpass %d reads before any subpass writes it; the attachment is "
        "read-only where the load runs, so it cannot be cleared",
        i, name, bind.target, firstSubpass);
    }

    if (bind.action & RP_TA_SUBPASS_RESOLVE)
    {
      bool hasSource = false;
      for (uint32_t j = 0; j < rp_desc.bindCount && !hasSource; ++j)
        hasSource = rp_desc.binds[j].slot == bind.slot && rp_desc.binds[j].subpass == bind.subpass &&
                    (rp_desc.binds[j].action & resolve_source_actions(bind.slot));
      D3D_CONTRACT_ASSERTF_AND_DO(hasSource, noErrors = false,
        "resolve bind %u of render pass '%s' has no source bound in the same slot and subpass", i, name);
    }

    if (bind.subpass > lastSubpass)
      lastSubpass = bind.subpass;
  }

  D3D_CONTRACT_ASSERTF_AND_DO(lastSubpass > RenderPassExtraIndexes::RP_SUBPASS_EXTERNAL_END, noErrors = false,
    "render pass '%s' declares no subpass", name);

  return noErrors;
}
