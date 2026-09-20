// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <render/rtsm/rtdl_api.h>

namespace rtdl
{
void initialize() {}
void teardown() {}
bool is_initialized() { return false; }

void get_required_persistent_texture_descriptors(denoiser::TexInfoMap &) {}
void get_required_transient_texture_descriptors(denoiser::TexInfoMap &) {}

bool render(const Params &, const denoiser::TexMap &) { return false; }
} // namespace rtdl
