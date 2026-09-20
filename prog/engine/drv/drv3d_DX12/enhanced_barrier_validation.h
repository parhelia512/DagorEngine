// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <drv/3d/dag_enhanced_barrier.h>


namespace drv3d_dx12
{
class BaseTex;

void validate_enhanced_texture_barrier(const d3d::TextureBarrier &barrier, BaseTex *btex);
void validate_enhanced_buffer_barrier(const d3d::BufferBarrier &barrier, Sbuffer *buffer);
} // namespace drv3d_dx12
