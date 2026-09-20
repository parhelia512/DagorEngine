// Copyright (C) Gaijin Games KFT.  All rights reserved.

// Own translation unit so the linker drops the EXR encoder, and with it
// engine/image and tinyexr, from every game that never names write_env_exr.

#include <render/giVerifierCapture.h>
#include <image/dag_exr.h>
#include <generic/dag_tab.h>
#include <util/dag_string.h>

namespace gi_verify
{

bool write_env_exr(const char *path, const uint16_t *rgba16f, int w, int h)
{
  Tab<uint16_t> planes[3];
  for (int c = 0; c < 3; ++c)
    planes[c].resize(w * h);
  for (int i = 0; i < w * h; ++i)
    for (int c = 0; c < 3; ++c)
      planes[c][i] = rgba16f[i * 4 + c];
  uint8_t *planePtrs[3] = {(uint8_t *)planes[2].data(), (uint8_t *)planes[1].data(), (uint8_t *)planes[0].data()};
  const char *planeNames[3] = {"B", "G", "R"};
  return save_exr(path, planePtrs, w, h, 3, w * sizeof(uint16_t), planeNames, String("verify env"));
}

} // namespace gi_verify
