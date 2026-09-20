// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "graph_pin_colors.h"

namespace
{
constexpr float DEAD_ALPHA = 0.5f;
} // namespace

ImU32 pin_color_for_type(PinType t)
{
  switch (t)
  {
    case PinType::Bool: return IM_COL32(0x77, 0x77, 0x77, 0xFF);
    case PinType::Int: return IM_COL32(0x11, 0x88, 0xFF, 0xFF);
    case PinType::Uint: return IM_COL32(0x00, 0x00, 0xAA, 0xFF);
    case PinType::Float: return IM_COL32(0x00, 0xAA, 0x00, 0xFF);
    case PinType::Float2: return IM_COL32(0xFF, 0xFF, 0x00, 0xFF);
    case PinType::Float3: return IM_COL32(0x00, 0xFF, 0xFF, 0xFF);
    case PinType::Float4: return IM_COL32(0xFF, 0x00, 0xFF, 0xFF);
    case PinType::Texture1D:
    case PinType::Texture2D:
    case PinType::Texture3D:
    case PinType::Texture2DArray:
    case PinType::Texture2DShdArray: return IM_COL32(0xFF, 0x88, 0x11, 0xFF);
    case PinType::Particles: return IM_COL32(0x88, 0x55, 0xFF, 0xFF);
    case PinType::BiomeData: return IM_COL32(0xFF, 0x99, 0x00, 0xFF);
    case PinType::NBSGbuffer: return IM_COL32(0xAA, 0x11, 0xAA, 0xFF);
    case PinType::MaterialT: return IM_COL32(0x88, 0x66, 0x44, 0xFF);
    case PinType::LayerT: return IM_COL32(0xFF, 0x44, 0x00, 0xFF);
    case PinType::MaskT: return IM_COL32(0x44, 0xFF, 0x00, 0xFF);
    case PinType::CtrlT: return IM_COL32(0x86, 0x00, 0x00, 0xFF);
    case PinType::Unknown: break;
  }
  return IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
}

ImU32 dead_color_for_type(PinType t)
{
  const ImU32 packed = pin_color_for_type(t);
  const ImU32 alpha = static_cast<ImU32>(((packed >> IM_COL32_A_SHIFT) & 0xFF) * DEAD_ALPHA);
  return (packed & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}
