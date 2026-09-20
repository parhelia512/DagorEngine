//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

// Register limits shared by the DX11 shader compiler target and drv3d_DX11: the two must agree.
namespace dx11
{

// D3D11 has 128 input resource slots, but the driver tracks per-stage SRV dirtiness in a 32-bit mask,
// so 32 is the hard cap.
inline constexpr int MAX_T_REGISTERS = 32;

inline constexpr int MAX_S_REGISTERS = 16;

inline constexpr int MAX_U_REGISTERS = 8;
// Physical limits are 14 for dx11, 20 for PS4, 32 for PS5 (or even 256), putting at 12 to start unification
inline constexpr int MAX_B_REGISTERS = 12;

inline constexpr int IMMEDIATE_CB_REGISTER = 8;

// OMSetRenderTargetsAndUnorderedAccessViews binds RTVs and pixel-shader UAVs out of one slot range.
inline constexpr bool UAVS_CONTEND_WITH_RTVS = true;

} // namespace dx11
