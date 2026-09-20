// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <drv/3d/dag_texture.h>


namespace tex_resize_generic
{

// Copies one mip of one slice, always from origin 0,0,0 to origin 0,0,0. A driver that migrates
// mips without TEXCF_UPDATE_DESTINATION supplies its own, so the level math stays shared.
using CopySubRes = int (*)(BaseTexture *src, int src_subres_idx, int src_w, int src_h, int src_d, BaseTexture *dst,
  int dst_subres_idx);

int copy_sub_res(BaseTexture *src, int src_subres_idx, int src_w, int src_h, int src_d, BaseTexture *dst, int dst_subres_idx);

BaseTexture *down_size_tex(BaseTexture *tex, int width, int height, int depth, int mips, unsigned start_src_level,
  unsigned level_offset, CopySubRes copy = copy_sub_res);
BaseTexture *up_size_tex(BaseTexture *tex, int width, int height, int depth, int mips, unsigned start_src_level, unsigned level_offset,
  CopySubRes copy = copy_sub_res);

} // namespace tex_resize_generic


#define IMPLEMENT_D3D_TEX_RESIZE_API_USING_GENERIC()                                                                      \
  BaseTexture *d3d::down_size_tex(BaseTexture *tex, int width, int height, int depth, int mips, unsigned start_src_level, \
    unsigned level_offset)                                                                                                \
  {                                                                                                                       \
    return tex_resize_generic::down_size_tex(tex, width, height, depth, mips, start_src_level, level_offset);             \
  }                                                                                                                       \
  BaseTexture *d3d::up_size_tex(BaseTexture *tex, int width, int height, int depth, int mips, unsigned start_src_level,   \
    unsigned level_offset)                                                                                                \
  {                                                                                                                       \
    return tex_resize_generic::up_size_tex(tex, width, height, depth, mips, start_src_level, level_offset);               \
  }
