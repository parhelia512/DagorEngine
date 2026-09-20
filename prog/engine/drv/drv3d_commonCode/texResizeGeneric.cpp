// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "texResizeGeneric.h"

int tex_resize_generic::copy_sub_res(BaseTexture *src, int src_subres_idx, int src_w, int src_h, int src_d, BaseTexture *dst,
  int dst_subres_idx)
{
  return d3d::update_sub_region(src, src_subres_idx, 0, 0, 0, src_w, src_h, src_d, dst, dst_subres_idx, 0, 0, 0);
}

BaseTexture *tex_resize_generic::down_size_tex(BaseTexture *tex, int width, int height, int depth, int mips, unsigned start_src_level,
  unsigned level_offset, CopySubRes copy)
{
  auto rep = tex->makeTmpTexResCopy(width, height, depth, mips);
  if (!rep)
    return nullptr;

  TextureInfo selfInfo;
  tex->getinfo(selfInfo);

  unsigned sourceLevel = max<unsigned>(level_offset, start_src_level);
  unsigned sourceLevelEnd = min<unsigned>(selfInfo.mipLevels, mips + level_offset);
  rep->texmiplevel(sourceLevel - level_offset, sourceLevelEnd - level_offset - 1);
  for (; sourceLevel < sourceLevelEnd; sourceLevel++)
  {
    for (int s = 0; s < selfInfo.a; s++)
    {
      // copy depth is the source mip depth: 1 for the layered types (each slice is a separate
      // subresource iterated by s), the per-mip depth for volumes
      copy(tex, BaseTexture::calcSubResIdx(sourceLevel, s, selfInfo.mipLevels), max<int>(selfInfo.w >> sourceLevel, 1),
        max<int>(selfInfo.h >> sourceLevel, 1), max<int>(selfInfo.d >> sourceLevel, 1), rep,
        BaseTexture::calcSubResIdx(sourceLevel - level_offset, s, mips));
    }
  }
  return rep;
}

BaseTexture *tex_resize_generic::up_size_tex(BaseTexture *tex, int width, int height, int depth, int mips, unsigned start_src_level,
  unsigned level_offset, CopySubRes copy)
{
  auto rep = tex->makeTmpTexResCopy(width, height, depth, mips);
  if (!rep)
    return nullptr;

  TextureInfo selfInfo;
  tex->getinfo(selfInfo);

  unsigned destinationLevel = level_offset + start_src_level;
  unsigned destinationLevelEnd = min<unsigned>(selfInfo.mipLevels + level_offset, mips);
  rep->texmiplevel(destinationLevel, destinationLevelEnd - 1);
  for (; destinationLevel < destinationLevelEnd; destinationLevel++)
  {
    for (int s = 0; s < selfInfo.a; s++)
    {
      // copy depth is the source mip depth: 1 for the layered types (each slice is a separate
      // subresource iterated by s), the per-mip depth for volumes
      copy(tex, BaseTexture::calcSubResIdx(destinationLevel - level_offset, s, selfInfo.mipLevels),
        max<int>(width >> destinationLevel, 1), max<int>(height >> destinationLevel, 1),
        max<int>(selfInfo.d >> (destinationLevel - level_offset), 1), rep, BaseTexture::calcSubResIdx(destinationLevel, s, mips));
    }
  }

  return rep;
}
