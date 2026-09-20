// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <drv/3d/dag_consts.h>

#define DRV3DC

inline constexpr uint32_t MAX_CBUFFER_VECTORS = 4096;

inline constexpr int REGISTER_BYTE_SIZE = 16;

inline constexpr const char *SHADER_STAGE_SHORT_NAMES[STAGE_MAX] = {"cs", "ps", "vs"};
inline constexpr const char *SHADER_STAGE_NAMES[STAGE_MAX] = {"compute", "pixel", "vertex"};

inline constexpr int MATERIAL_PARAMS_CONST_BUF_REGISTER = 1;

// A texture hardcoded at register tN takes sampler sN, so tN above MAX_S_REGISTERS cannot be sampled.
#if _CROSS_TARGET_DX12
#include <drv/shadersMetaData/dxil/compiled_shader_header.h>

inline constexpr int MAX_T_REGISTERS = dxil::MAX_T_REGISTERS;
inline constexpr int MAX_S_REGISTERS = dxil::MAX_S_REGISTERS;
inline constexpr int MAX_U_REGISTERS = dxil::MAX_U_REGISTERS;
inline constexpr int MAX_B_REGISTERS = dxil::MAX_B_REGISTERS;

static constexpr bool UAVS_CONTEND_WITH_RTVS = false;

inline constexpr int IMMEDIATE_CB_REGISTER = dxil::ROOT_CONSTANT_BUFFER_REGISTER_INDEX;

#elif _CROSS_TARGET_SPIRV
#include <drv/shadersMetaData/spirv/compiled_meta_data.h>

inline constexpr int MAX_T_REGISTERS = spirv::T_REGISTER_INDEX_MAX;
inline constexpr int MAX_S_REGISTERS = spirv::S_REGISTER_INDEX_MAX;
inline constexpr int MAX_U_REGISTERS = spirv::U_REGISTER_INDEX_MAX;
inline constexpr int MAX_B_REGISTERS = spirv::B_REGISTER_INDEX_MAX;

static constexpr bool UAVS_CONTEND_WITH_RTVS = false;

inline constexpr int IMMEDIATE_CB_REGISTER = -1; // Uses real push constants

#elif _CROSS_TARGET_DX11
#include <drv/shadersMetaData/dx11/register_limits.h>

inline constexpr int MAX_T_REGISTERS = dx11::MAX_T_REGISTERS;
inline constexpr int MAX_S_REGISTERS = dx11::MAX_S_REGISTERS;
inline constexpr int MAX_U_REGISTERS = dx11::MAX_U_REGISTERS;
inline constexpr int MAX_B_REGISTERS = dx11::MAX_B_REGISTERS;

inline constexpr bool UAVS_CONTEND_WITH_RTVS = dx11::UAVS_CONTEND_WITH_RTVS;

inline constexpr int IMMEDIATE_CB_REGISTER = dx11::IMMEDIATE_CB_REGISTER;

#elif _CROSS_TARGET_METAL
#include <drv/shadersMetaData/metal/register_limits.h>

inline constexpr int MAX_T_REGISTERS = metal::MAX_T_REGISTERS;
inline constexpr int MAX_S_REGISTERS = metal::MAX_S_REGISTERS;
inline constexpr int MAX_U_REGISTERS = metal::MAX_U_REGISTERS;
inline constexpr int MAX_B_REGISTERS = metal::MAX_B_REGISTERS;

inline constexpr bool UAVS_CONTEND_WITH_RTVS = metal::UAVS_CONTEND_WITH_RTVS;

inline constexpr int IMMEDIATE_CB_REGISTER = metal::IMMEDIATE_CB_REGISTER;

#elif _CROSS_TARGET_C1











#elif _CROSS_TARGET_C2











#elif _CROSS_TARGET_EMPTY

// The stub target has no driver to agree with, so these are arbitrary but must stay put: they shape
// the register layout of the dumps it produces.
inline constexpr int MAX_T_REGISTERS = 32;
inline constexpr int MAX_S_REGISTERS = 16;
inline constexpr int MAX_U_REGISTERS = 13;
inline constexpr int MAX_B_REGISTERS = 12;

inline constexpr bool UAVS_CONTEND_WITH_RTVS = false;

inline constexpr int IMMEDIATE_CB_REGISTER = 8;

#else
#error No register limits for this cross target. Add a shadersMetaData/<target>/register_limits.h shared with its driver.
#endif
