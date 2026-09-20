// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <drv/3d/dag_shader.h>
#include <ioSys/dag_zstdIo.h>
#include <debug/dag_assert.h>
#include <EASTL/algorithm.h>

const uint32_t *ShaderSource::uncompress(Tab<uint8_t> &tmpbuf) const
{
  G_ASSERT(!compressedData.empty());
  G_ASSERT(dictionary || compressedData.size() <= uncompressedSize);

  tmpbuf.resize(uncompressedSize);

  if (dictionary == nullptr)
    eastl::copy(compressedData.begin(), compressedData.end(), tmpbuf.begin());
  else
  {
    ZSTD_DCtx_s *dctx = zstd_create_dctx(true); // tmp for framemem
    uint32_t decompressed_size = zstd_decompress_with_dict(dctx, tmpbuf.data(), tmpbuf.size(), compressedData.data(),
      compressedData.size(), (const ZSTD_DDict_s *)dictionary);
    zstd_destroy_dctx(dctx);
    G_ASSERT(decompressed_size <= tmpbuf.size());
  }

  return (const uint32_t *)tmpbuf.data();
}
