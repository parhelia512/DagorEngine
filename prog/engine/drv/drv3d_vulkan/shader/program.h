// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <drv/3d/dag_consts.h>
#include <generic/dag_align.h>

#include "driver.h"
#include "driver_defs.h"

namespace drv3d_vulkan
{

class DeviceContext;

// Program ID encoding for shader programs:
//
// Index | implicit cbuf size(s) | program type (0 for graphics, 1 for compute)
//
// Reason it is done this way is to not have to query program db with locks for cbuf size
// Implicit cbuf sizes use quants of 512 bytes to save bits
// For compute programs, 8 bits encode the size (up to 4096 registers)
// For graphics programs, 8 bits encode vs cbuf size (up to 4096 regs) and 4 bits encode fs cbuf size (up to 256 regs)
//
// 1 sign bit is left alone to allow for BAD_PROGRAM to have a special value
//
// With this, graphics program indices have 18 bits
// And compute program indices have 22 bits

inline constexpr uint32_t program_type_bits = 2;
inline constexpr uint32_t program_type_mask = 1 | 2;
inline constexpr uint32_t program_type_graphics = 0;
inline constexpr uint32_t program_type_compute = 1;

inline constexpr uint32_t program_implicit_cbuf_reg_quant = 32;

constexpr uint32_t bits_to_hold(uint32_t v) { return v ? 1 + bits_to_hold(v >> 1) : 0; }
constexpr uint32_t implicit_cbuf_regs_to_quants(uint32_t regs) { return dag::divide_align_up(regs, program_implicit_cbuf_reg_quant); }
constexpr uint32_t implicit_cbuf_quants_to_regs(uint32_t quants) { return quants * program_implicit_cbuf_reg_quant; }
constexpr uint32_t implicit_cbuf_field_bits(uint32_t max_regs) { return bits_to_hold(implicit_cbuf_regs_to_quants(max_regs)); }
constexpr uint32_t bit_mask(uint32_t bits) { return (1u << bits) - 1; }

inline constexpr uint32_t program_vs_implicit_cbuf_bits = implicit_cbuf_field_bits(VERTEX_SHADER_MAX_REGISTERS);
inline constexpr uint32_t program_fs_implicit_cbuf_bits = implicit_cbuf_field_bits(FRAGMENT_SHADER_REGISTERS);
inline constexpr uint32_t program_cs_implicit_cbuf_bits = implicit_cbuf_field_bits(MAX_COMPUTE_CONST_REGISTERS);

inline constexpr uint32_t graphics_fs_implicit_cbuf_shift = program_type_bits;
inline constexpr uint32_t graphics_vs_implicit_cbuf_shift = graphics_fs_implicit_cbuf_shift + program_fs_implicit_cbuf_bits;
inline constexpr uint32_t graphics_program_index_shift = graphics_vs_implicit_cbuf_shift + program_vs_implicit_cbuf_bits;

inline constexpr uint32_t compute_cs_implicit_cbuf_shift = program_type_bits;
inline constexpr uint32_t compute_program_index_shift = compute_cs_implicit_cbuf_shift + program_cs_implicit_cbuf_bits;

inline constexpr uint32_t graphics_program_max_index = bit_mask(31 - graphics_program_index_shift);
inline constexpr uint32_t compute_program_max_index = bit_mask(31 - compute_program_index_shift);

static_assert(graphics_program_index_shift <= 16, "too few bits left for graphics program index");
static_assert(compute_program_index_shift <= 16, "too few bits left for compute program index");

static_assert(graphics_program_index_shift == 14);
static_assert(compute_program_index_shift == 10);

inline uint32_t encode_implicit_cbuf_field(uint32_t regs, uint32_t max_regs, uint32_t shift)
{
  G_ASSERTF(regs <= max_regs, "vulkan: implicit cbuf reg count %u exceeds stage limit %u", regs, max_regs);
  return implicit_cbuf_regs_to_quants(regs < max_regs ? regs : max_regs) << shift;
}

inline uint32_t decode_implicit_cbuf_field(ProgramID id, uint32_t bits, uint32_t shift)
{
  return implicit_cbuf_quants_to_regs((uint32_t(id.get()) >> shift) & bit_mask(bits));
}

inline int get_program_type(ProgramID id) { return id.get() & program_type_mask; }

template <typename ItemType, typename IdType>
class ShaderProgramDatabaseStorage;

struct BaseProgram
{
  bool inRemoval = false;
  bool isRemovalPending() { return inRemoval; }
  void removeFromContext(DeviceContext &ctx, ProgramID prog);
};

struct GraphicsProgram : BaseProgram
{
  InputLayoutID inputLayout;
  ShaderInfo *vertexShader;
  ShaderInfo *fragmentShader;
  ShaderInfo *geometryShader;
  ShaderInfo *controlShader;
  ShaderInfo *evaluationShader;
  uint32_t refCount = 1;

  struct IDCachedPayload
  {
    uint16_t implicitVsCbufRegCount = 0;
    uint16_t implicitFsCbufRegCount = 0;
  } idPayload;

  struct CreationInfo
  {
    InputLayoutID layout;
    ShaderInfo *vs;
    ShaderInfo *fs;
    uint16_t implicitVsCbufRegCount;
    uint16_t implicitFsCbufRegCount;

    uint32_t getHash32() const
    {
      return (uint32_t)layout.get() ^ mem_hash_fnv1<32>((const char *)&vs, sizeof(vs)) ^
             mem_hash_fnv1<32>((const char *)&fs, sizeof(fs));
    }
  };

  GraphicsProgram(const CreationInfo &info);

  bool isSame(const CreationInfo &obj) const
  {
    if (inputLayout != obj.layout)
      return false;

    // as the geometry shader is a child of vs this is enough
    if (vertexShader != obj.vs)
      return false;

    if (fragmentShader != obj.fs)
      return false;

    return true;
  }

  bool usesVertexShader(ShaderInfo *shader) const { return vertexShader == shader; }

  bool usesFragmentShader(ShaderInfo *shader) const { return fragmentShader == shader; }

  static constexpr bool alwaysUnique() { return false; }
  bool release()
  {
    G_ASSERT(refCount > 0);
    return --refCount == 0;
  }
  void onDuplicateAddition() { ++refCount; }
  void addToContext(DeviceContext &ctx, ProgramID prog, const CreationInfo &);

  static ProgramID makeID(LinearStorageIndex index, const IDCachedPayload &payload)
  {
    G_ASSERTF(uint32_t(index) <= graphics_program_max_index, "vulkan: graphics program index %d out of ID range", index);
    return ProgramID(
      (uint32_t(index) << graphics_program_index_shift) |
      encode_implicit_cbuf_field(payload.implicitVsCbufRegCount, VERTEX_SHADER_MAX_REGISTERS, graphics_vs_implicit_cbuf_shift) |
      encode_implicit_cbuf_field(payload.implicitFsCbufRegCount, FRAGMENT_SHADER_REGISTERS, graphics_fs_implicit_cbuf_shift) |
      program_type_graphics);
  }
  static bool checkID(ProgramID id) { return program_type_graphics == get_program_type(id); }
  static LinearStorageIndex getIndexFromID(ProgramID id) { return uint32_t(id.get()) >> graphics_program_index_shift; }
  static IDCachedPayload createInfoToIDPayload(const CreationInfo &info)
  {
    return {info.implicitVsCbufRegCount, info.implicitFsCbufRegCount};
  }

  static void getImplicitCbufRegCountsFromID(ProgramID id, uint32_t &vs_count, uint32_t &fs_count)
  {
    vs_count = decode_implicit_cbuf_field(id, program_vs_implicit_cbuf_bits, graphics_vs_implicit_cbuf_shift);
    fs_count = decode_implicit_cbuf_field(id, program_fs_implicit_cbuf_bits, graphics_fs_implicit_cbuf_shift);
  }
};

struct ComputeProgram : BaseProgram
{
  struct CreationInfo
  {
    const ShaderModuleHeader &smh;
    const ShaderModuleBlob &smb;

    uint32_t getHash32() const { return 0; }
  };

  struct IDCachedPayload
  {
    uint32_t implicitCbufRegCount = 0;
  } idPayload;

  ComputeProgram(const CreationInfo &info);
  static constexpr bool alwaysUnique() { return true; }
  bool isSame(const CreationInfo &) { return false; }
  bool release() { return true; }
  void onDuplicateAddition() {}
  void addToContext(DeviceContext &ctx, ProgramID prog, const CreationInfo &info);

  static ProgramID makeID(LinearStorageIndex index, const IDCachedPayload &payload)
  {
    G_ASSERTF(uint32_t(index) <= compute_program_max_index, "vulkan: compute program index %d out of ID range", index);
    return ProgramID(
      (uint32_t(index) << compute_program_index_shift) |
      encode_implicit_cbuf_field(payload.implicitCbufRegCount, MAX_COMPUTE_CONST_REGISTERS, compute_cs_implicit_cbuf_shift) |
      program_type_compute);
  }
  static bool checkID(ProgramID id) { return program_type_compute == get_program_type(id); }
  static LinearStorageIndex getIndexFromID(ProgramID id) { return uint32_t(id.get()) >> compute_program_index_shift; }
  static IDCachedPayload createInfoToIDPayload(const CreationInfo &info) { return {info.smh.header.implicitCbufRegCount}; }

  static uint32_t getImplicitCbufRegCountFromID(ProgramID id)
  {
    return decode_implicit_cbuf_field(id, program_cs_implicit_cbuf_bits, compute_cs_implicit_cbuf_shift);
  }
};

} // namespace drv3d_vulkan
