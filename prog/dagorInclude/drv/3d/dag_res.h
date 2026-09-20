//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <drv/3d/dag_multi_interface.h>

namespace d3d _MULTI_INTERFACE
{
//! reserves resource entries using max count values, optionally disabling exceeding count limit
void reserve_res_entries(bool strict_max, int max_tex, int max_vs, int max_ps, int max_vdecl, int max_vb, int max_ib, int max_stblk);

//! returns maximum resource entries count values
void get_max_used_res_entries(int &max_tex, int &max_vs, int &max_ps, int &max_vdecl, int &max_vb, int &max_ib, int &max_stblk);

//! returns current resource entries count values
void get_cur_used_res_entries(int &max_tex, int &max_vs, int &max_ps, int &max_vdecl, int &max_vb, int &max_ib, int &max_stblk);
} // namespace d3d _MULTI_INTERFACE
