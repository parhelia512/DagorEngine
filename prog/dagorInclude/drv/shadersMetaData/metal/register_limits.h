//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

// Register limits shared by the Metal shader compiler target and drv3d_Metal: the two must agree.
namespace metal
{

// Doubles as the SRV/UAV discriminator baked into the metal shader header: the driver reads a
// texture slot >= MAX_T_REGISTERS as a UAV, so a change here is a format break.
inline constexpr int MAX_T_REGISTERS = 32;

inline constexpr int MAX_S_REGISTERS = 16;

inline constexpr int MAX_U_REGISTERS = 10;
static_assert(MAX_U_REGISTERS <= MAX_T_REGISTERS);

// Physical limits are 14 for dx11, 20 for PS4, 32 for PS5 (or even 256), putting at 12 to start unification
inline constexpr int MAX_B_REGISTERS = 12;

// Deliberately above MAX_B_REGISTERS: the immediate constants take their own backend bind slot and never
// contend with the b registers the allocator hands out.
inline constexpr int IMMEDIATE_CB_REGISTER = 13;

inline constexpr bool UAVS_CONTEND_WITH_RTVS = false;

} // namespace metal
