// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <heightmap/heightmapPhysHandler.h>
#include <math/dag_adjpow2.h>

uint32_t calc_hmap_max_neighbor_delta(const CompressedHeightmap &c, uint32_t first_block, uint32_t block_count)
{
  uint32_t best = 0;
  const uint32_t blocks = uint32_t(c.bw) * c.bh, bwd = c.block_width, shift = c.block_width_shift;
  auto ad = [](uint8_t a, uint8_t b) { return uint32_t(a > b ? a - b : b - a); };
  auto doBlock = [&](uint32_t bi) {
    const uint32_t bx = bi % c.bw;
    const CompressedHeightmap::BlockInfo &b = c.getBlockInfo(bi);
    // inside one block the pair delta maxes in the 8 bit variance domain and
    // decodes once at the end; the +1 covers the per pixel decode rounding
    if (b.delta > best)
    {
      const uint8_t *v = c.getBlockVariance(bi);
      uint32_t dv = 0;
      for (uint32_t j = 0; j + 1 < bwd; ++j)
      {
        const uint8_t *row = v + (j << shift);
        for (uint32_t i = 0; i + 1 < bwd; ++i)
          dv = max(dv, max(ad(row[i], row[i + 1]), ad(row[i], row[i + bwd])));
        dv = max(dv, ad(row[bwd - 1], row[2 * bwd - 1]));
      }
      const uint8_t *last = v + ((bwd - 1) << shift);
      for (uint32_t i = 0; i + 1 < bwd; ++i)
        dv = max(dv, ad(last[i], last[i + 1]));
      if (dv)
        best = max(best, (dv * uint32_t(b.delta) + 127) / 255 + 1);
    }
    // the seams decode both sides exactly, only when their range bound can win
    auto crossBound = [&](const CompressedHeightmap::BlockInfo &n) {
      return uint32_t(int(max(b.getMax(), n.getMax())) - int(min(b.getMin(), n.getMin())));
    };
    if (bx + 1 < c.bw)
    {
      const CompressedHeightmap::BlockInfo &r = c.getBlockInfo(bi + 1);
      if (crossBound(r) > best)
      {
        const uint8_t *va = c.getBlockVariance(bi) + bwd - 1, *vb = c.getBlockVariance(bi + 1);
        for (uint32_t j = 0; j < bwd; ++j)
          best = max(best, uint32_t(abs(int(b.decodeVariance(va[j << shift])) - int(r.decodeVariance(vb[j << shift])))));
      }
    }
    if (bi + c.bw < blocks)
    {
      const CompressedHeightmap::BlockInfo &d = c.getBlockInfo(bi + c.bw);
      if (crossBound(d) > best)
      {
        const uint8_t *va = c.getBlockVariance(bi) + ((bwd - 1) << shift), *vb = c.getBlockVariance(bi + c.bw);
        for (uint32_t i = 0; i < bwd; ++i)
          best = max(best, uint32_t(abs(int(b.decodeVariance(va[i])) - int(d.decodeVariance(vb[i])))));
      }
    }
  };
  first_block = min(first_block, blocks);
  const uint32_t be = first_block + min(block_count, blocks - first_block);
  // the deepest level whose cells still hold at least 2x2 blocks
  const uint32_t lev =
    c.htRangeBlocksLevels ? min(c.bw > 2 ? uint32_t(get_log2i(c.bw - 1)) - 1 : 0u, uint32_t(c.htRangeBlocksLevels) - 1) : 0;
  const uint32_t res = 2u << lev, nodeStride = 1u << lev;
  const CompressedHeightmap::HeightRangeBlock *hrb = c.getHtRangeBlocksLevData(lev);
  if (first_block == 0 && be == blocks && hrb && c.bw == c.bh && res <= c.bw && c.bw % res == 0)
  {
    // a range cell of the precomputed hierarchy bounds every pair its blocks
    // own (its own range plus the right/bottom crosses): whole cells prune
    auto cMin = [&](uint32_t rx, uint32_t ry) { return int(hrb[(ry >> 1) * nodeStride + (rx >> 1)].hMin[(ry & 1) * 2 + (rx & 1)]); };
    auto cMax = [&](uint32_t rx, uint32_t ry) { return int(hrb[(ry >> 1) * nodeStride + (rx >> 1)].hMax[(ry & 1) * 2 + (rx & 1)]); };
    const uint32_t cellBlocks = c.bw / res;
    for (uint32_t ry = 0; ry < res; ++ry)
      for (uint32_t rx = 0; rx < res; ++rx)
      {
        const int mn = cMin(rx, ry), mx = cMax(rx, ry);
        uint32_t bound = uint32_t(mx - mn);
        if (rx + 1 < res)
          bound = max(bound, uint32_t(max(mx, cMax(rx + 1, ry)) - min(mn, cMin(rx + 1, ry))));
        if (ry + 1 < res)
          bound = max(bound, uint32_t(max(mx, cMax(rx, ry + 1)) - min(mn, cMin(rx, ry + 1))));
        if (bound <= best)
          continue;
        for (uint32_t by = ry * cellBlocks, bye = by + cellBlocks; by < bye; ++by)
          for (uint32_t bx = rx * cellBlocks, bxe = bx + cellBlocks; bx < bxe; ++bx)
            doBlock(by * c.bw + bx);
      }
    return best;
  }
  for (uint32_t bi = first_block; bi < be; ++bi)
    doBlock(bi);
  return best;
}
