// Copyright (C) Gaijin Games KFT.  All rights reserved.

// The CPU path: decode each cell's exported legacy weight textures; the
// packing they feed lives in lmeshWeightAtlas.cpp.
// See lmeshWeightAtlasLegacy.h for why this file exists.

#include "lmeshWeightAtlasInternal.h"
#include "lmeshWeightAtlasLegacy.h"
#include <drv/3d/dag_tex3d.h>
#include <3d/ddsxTex.h>
#include <3d/ddsFormat.h>
#include <ioSys/dag_memIo.h>
#include <loadDDSx/uniTexCrd.h>
#include <math/dag_mathBase.h>
#include <image/dag_dxtCompress.h>
#include <generic/dag_tab.h>
#include <memory/dag_framemem.h>
#include <debug/dag_debug.h>

static constexpr int DET_NUM = LandWeightAtlas::DET_NUM;

// ---- legacy ddsx stream -> raw texel bytes -----------------------------------

// Returns the offset of the top mip, which is last when mips are stored in
// reverse order. UnifiedTexGenLoad picks the decoder the ddsx was packed with,
// which is the only way to read one correctly: the packers disagree on framing
// (an oodle payload is one block, a zstd one is a stream).
static bool unpack_ddsx(dag::ConstSpan<uint8_t> data, ddsx::Header &hdr, Tab<uint8_t> &texels, int &top_mip_ofs)
{
  if (data.size() < sizeof(ddsx::Header))
    return false;
  memcpy(&hdr, data.data(), sizeof(hdr));
  if (!hdr.checkLabel() || hdr.memSz == 0)
    return false;

  // reverse mip order puts the top one last, so it starts a surface short of the
  // end; a corrupt header must not make that offset wrap and read before the buffer
  const unsigned topMipSz = (hdr.flags & ddsx::Header::FLG_REV_MIP_ORDER) && hdr.levels > 1 ? hdr.getSurfaceSz(0) : hdr.memSz;
  if (topMipSz > hdr.memSz)
    return false;
  texels.resize(hdr.memSz);
  top_mip_ofs = hdr.memSz - topMipSz;
  InPlaceMemLoadCB mcrd(data.data() + sizeof(hdr), data.size() - sizeof(hdr));
  UnifiedTexGenLoad crd(mcrd, hdr);
  return crd->tryRead(texels.data(), hdr.memSz) == (int)hdr.memSz;
}

// decodes one weight texture into 4 planes (r,g,b,a), each size*size bytes
static bool decode_weight_tex(dag::ConstSpan<uint8_t> ddsx_data, int size, uint8_t *planes[4])
{
  ddsx::Header hdr;
  Tab<uint8_t> texels(framemem_ptr());
  int topMipOfs = 0;
  if (!unpack_ddsx(ddsx_data, hdr, texels, topMipOfs))
    return false;
  if (hdr.w != size || hdr.h != size)
    return false;
  const uint8_t *t = texels.data() + topMipOfs;
  if (hdr.d3dFormat == D3DFMT_DXT1 || hdr.d3dFormat == D3DFMT_DXT5)
  {
    const bool dxt1 = hdr.d3dFormat == D3DFMT_DXT1;
    if (texels.size() - topMipOfs < (size / 4) * (size / 4) * (dxt1 ? 8 : 16))
      return false;
    Tab<uint8_t> rgba(framemem_ptr());
    rgba.resize(size * size * 4);
    decompress_dxt(rgba.data(), size, size, size * 4, (unsigned char *)t, dxt1);
    for (int i = 0; i < size * size; i++) // bgra
    {
      planes[0][i] = rgba[i * 4 + 2];
      planes[1][i] = rgba[i * 4 + 1];
      planes[2][i] = rgba[i * 4 + 0];
      planes[3][i] = dxt1 ? 0 : rgba[i * 4 + 3];
    }
    return true;
  }
  if (hdr.d3dFormat == D3DFMT_A4R4G4B4)
  {
    if (texels.size() - topMipOfs < size * size * 2)
      return false;
    for (int i = 0; i < size * size; i++)
    {
      uint16_t v = t[i * 2] | (t[i * 2 + 1] << 8);
      for (int ch = 0; ch < LEGACY_TEX1_CHANNELS; ch++)
        planes[ch][i] = ((v >> LEGACY_TEX1_NIBBLE[ch]) & 15) * 17;
    }
    return true;
  }
  if (hdr.d3dFormat == D3DFMT_A8R8G8B8)
  {
    if (texels.size() - topMipOfs < size * size * 4)
      return false;
    for (int i = 0; i < size * size; i++)
    {
      planes[0][i] = t[i * 4 + 2];
      planes[1][i] = t[i * 4 + 1];
      planes[2][i] = t[i * 4 + 0];
      planes[3][i] = t[i * 4 + 3];
    }
    return true;
  }
  return false; // unsupported source (e.g. ASTC): the cell degrades, caller reports
}

// ---- conversion of the legacy per-cell weight textures ------------------------

bool LandWeightAtlasBuilder::addCell(int index, const uint8_t *det_tex_ids, dag::ConstSpan<uint8_t> tex1_ddsx,
  dag::ConstSpan<uint8_t> tex2_ddsx)
{
  if (uint32_t(index) >= uint32_t(cells.size()) || texSize < 4)
    return false;
  Cell &c = cells[index];
  memcpy(c.detTexIds, det_tex_ids, sizeof(c.detTexIds));
  int numTex = 0;
  for (int i = 0; i < DET_NUM; i++)
    if (c.detTexIds[i] != 0xFF)
      numTex++;
  if (!numTex)
    return true;

  Tab<uint8_t> planes1(framemem_ptr()), planes2(framemem_ptr());
  planes1.resize(texSize * texSize * 4);
  planes2.resize(texSize * texSize * 4);
  uint8_t *p1[4], *p2[4];
  for (int i = 0; i < 4; i++)
  {
    p1[i] = planes1.data() + i * texSize * texSize;
    p2[i] = planes2.data() + i * texSize * texSize;
  }
  bool has1 = tex1_ddsx.size() > 0;
  bool has2 = tex2_ddsx.size() > 0;
  if ((has1 && !decode_weight_tex(tex1_ddsx, texSize, p1)) || (has2 && !decode_weight_tex(tex2_ddsx, texSize, p2)))
  {
    // undecodable source (e.g. ASTC-packed mobile export): render the cell as its
    // first landclass until the location is re-exported; finish() reports the count
    undecodedCells++;
    return true;
  }

  for (int ch = 0; ch < numTex; ch++) // channels are dense: DET_NUM - numTex stay empty
    c.chan[ch].resize(texSize * texSize);
  for (int t = 0; t < texSize * texSize; t++)
  {
    float w[DET_NUM] = {0};
    for (int ch = 0; ch < LEGACY_TEX1_CHANNELS; ch++)
      w[ch] = has1 ? p1[ch][t] / 255.f : 0.f;
    for (int ch = 0; ch < LEGACY_TEX2_CHANNELS; ch++)
      w[LEGACY_TEX1_CHANNELS + ch] = has2 ? p2[ch][t] / 255.f : 0.f;
    land_weight_derive(w, numTex);
    for (int ch = 0; ch < numTex; ch++)
      c.chan[ch][t] = (uint8_t)clamp((int)(w[ch] * 255.f + 0.5f), 0, 255);
  }
  return true;
}
