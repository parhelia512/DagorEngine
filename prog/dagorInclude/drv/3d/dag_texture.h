//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <drv/3d/dag_consts.h>
#include <drv/3d/dag_tex3d.h>
#include <drv/3d/dag_multi_interface.h>
#include <3d/dag_resourceTags.h>

struct TexImage32;

namespace d3d _MULTI_INTERFACE
{
/**
 * @brief Check whether the specified texture format is available.
 * @param cflg The texture format to check.
 * @return Returns false if a texture of the specified format cannot be created, otherwise returns true.
 */
bool check_texformat(int cflg);

/**
 * @brief Get the maximum sample count for the given texture format.
 * @param cflg The texture format.
 * @return The maximum sample count for the given texture format.
 */
int get_max_sample_count(int cflg);

/**
 * @brief Get the texture format usage for the given texture format.
 * @param cflg The texture format.
 * @param type The resource type (default value is D3DResourceType::TEX).
 * @return The texture format usage. One of the USAGE_XXX flags.
 * @todo Use enum class as a returned type.
 */
unsigned get_texformat_usage(int cflg, D3DResourceType type = D3DResourceType::TEX);

/**
 * @brief Check whether two texture creation flags result in the same format.
 * @param cflg1 The first texture creation flag.
 * @param cflg2 The second texture creation flag.
 * @return Returns true if the two texture creation flags result in the same format, otherwise returns false.
 */
bool issame_texformat(int cflg1, int cflg2);

/**
 * @brief Check whether the specified cube texture format is available.
 * @param cflg The cube texture format to check.
 * @return Returns false if a cube texture of the specified format cannot be created, otherwise returns true.
 */
bool check_cubetexformat(int cflg);

/**
 * @brief Check whether the specified volume texture format is available.
 * @param cflg The volume texture format to check.
 * @return Returns false if a volume texture of the specified format cannot be created, otherwise returns true.
 */
bool check_voltexformat(int cflg);

/**
 * @brief Create a texture.
 * @param img 32-bit image data. nullptr if there are no image data.
 * @param w The width of the texture.
 * @param h The height of the texture.
 * @param flg The texture creation flags.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes (optional).
 * @return A pointer to the created texture, or nullptr on error.
 */
BaseTexture *create_tex(TexImage32 *img, int w, int h, int flg, int levels, const char *stat_name = nullptr,
  ResourceTagType tag = nullptr);

/**
 * @brief Create a texture from a DDSx stream.
 * @param crd The DDSx stream.
 * @param flg The texture creation flags.
 * @param quality_id The quality index.
 * @param levels The number of loaded mipmaps (0=all, >0=only first 'levels' mipmaps).
 * @param stat_name The name of the texture for statistics purposes (optional).
 * @return A pointer to the created texture, or nullptr on error.
 */
BaseTexture *create_ddsx_tex(IGenLoad &crd, int flg, int quality_id, int levels = 0, const char *stat_name = nullptr);

/**
 * @brief Allocate a texture object using a DDSx header (not texture contents loaded at this time).
 * @param hdr The DDSx header.
 * @param flg The texture creation flags.
 * @param quality_id The quality index.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes (optional).
 * @param stub_tex_idx The index of the stub texture (default value is -1).
 * @return A pointer to the allocated texture, or nullptr on error.
 */
BaseTexture *alloc_ddsx_tex(const ddsx::Header &hdr, int flg, int quality_id, int levels = 0, const char *stat_name = nullptr,
  int stub_tex_idx = -1);

/**
 * @brief Load the texture content from a DDSx stream using a DDSx header for a previously allocated texture.
 * @param t The previously allocated texture.
 * @param hdr The DDSx header.
 * @param crd The DDSx stream.
 * @param q_id The quality index.
 * @return Returns true if the texture content was successfully loaded, otherwise returns false.
 */
inline TexLoadRes load_ddsx_tex_contents(BaseTexture *t, const ddsx::Header &hdr, IGenLoad &crd, int q_id)
{
  return d3d_load_ddsx_tex_contents(t, t->getTID(), BAD_TEXTUREID, hdr, crd, q_id);
}

/**
 * @brief Create a cubic texture.
 * @param size The size of the texture. (6 faces size x size)
 * @param flg The texture creation flags.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes (optional).
 * @return A pointer to the created texture, or nullptr on error.
 */
BaseTexture *create_cubetex(int size, int flg, int levels, const char *stat_name = nullptr, ResourceTagType tag = nullptr);

/**
 * @brief Create a volume texture.
 * @param w The width of the texture.
 * @param h The height of the texture.
 * @param d The depth of the texture.
 * @param flg The texture creation flags.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes (optional).
 * @return A pointer to the created texture, or nullptr on error.
 */
BaseTexture *create_voltex(int w, int h, int d, int flg, int levels, const char *stat_name = nullptr, ResourceTagType tag = nullptr);

/**
 * @brief Create a texture2d array.
 * @param w The width of the texture.
 * @param h The height of the texture.
 * @param d Amount of textures in the array.
 * @param flg The texture creation flags.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes.
 * @return A pointer to the created texture, or nullptr on error.
 */
BaseTexture *create_array_tex(int w, int h, int d, int flg, int levels, const char *stat_name, ResourceTagType tag = nullptr);

/**
 * @brief Create a cube array tex object
 * @param side The size of the texture. (6 faces size x size)
 * @param d Amount of textures in the array.
 * @param flg The texture creation flags.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes.
 * @return A pointer to the created texture, or nullptr on error.
 */
BaseTexture *create_cube_array_tex(int side, int d, int flg, int levels, const char *stat_name, ResourceTagType tag = nullptr);

/**
 * @brief Create a texture alias, a texture using the same memory as another texture but with different properties.
 * The alias may use a different resolution, format or mip level, subject to restrictions below.
 * @warning You also must dispatch a RB_ALIAS_FROM/RB_ALIAS_TO barrier after you stop using one of the aliases
 * and before you start using a different alias, where all textures occupying the same memory are considered
 * to be aliases of each other.
 * @warning After switching to a new alias, it will contain garbage and will need to be initialized.
 * This API does NOT guarantee data inheritance whatsoever. It is to be used for memory reuse only.
 * @warning It is not guaranteed that an intuitively smaller alias can be created from a bigger texture.
 * Decreasing the resolution by several pixels causes the amount of memory needed to store the texture
 * to INCREASE on some platforms for some formats and some concrete resolutions. Hence, this function
 * CAN fail and will return nullptr and logerr in such a case. Use `d3d::get_resource_allocation_properties`
 * to check whether the alias will not take up more memory than the original texture before calling this API.
 * @deprecated Always prefer higher-level APIs for saving memory (e.g. frame graph, ResizableTex).
 * Avoid using this as much as possible, it will be removed in favor of using heaps via a frame graph system.
 * @param baseTexture The base texture to alias.
 * @param img 32-bit image data.
 * @param w The width of the texture.
 * @param h The height of the texture.
 * @param flg The texture creation flags.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes (optional).
 * @return A pointer to the created texture alias, or nullptr on error.
 */
BaseTexture *alias_tex(BaseTexture *baseTexture, TexImage32 *img, int w, int h, int flg, int levels, const char *stat_name = nullptr);

/**
 * @brief Create a cube texture alias, a texture using the same memory as another cube texture but with a different format.
 * @param baseTexture The base cube texture to alias.
 * @param size The size of the texture.
 * @param flg The texture creation flags.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes (optional).
 * @return A pointer to the created cube texture alias, or nullptr on error.
 */
BaseTexture *alias_cubetex(BaseTexture *baseTexture, int size, int flg, int levels, const char *stat_name = nullptr);

/**
 * @brief Create a volume texture alias, a texture using the same memory as another volume texture but with a different format.
 * @param baseTexture The base volume texture to alias.
 * @param w The width of the texture.
 * @param h The height of the texture.
 * @param d The depth of the texture.
 * @param flg The texture creation flags.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes (optional).
 * @return A pointer to the created volume texture alias, or nullptr on error.
 */
BaseTexture *alias_voltex(BaseTexture *baseTexture, int w, int h, int d, int flg, int levels, const char *stat_name = nullptr);

/**
 * @brief Create a texture2d array alias, a texture using the same memory as another texture2d array but with a different format.
 * @param baseTexture The base texture2d array to alias.
 * @param w The width of the texture.
 * @param h The height of the texture.
 * @param d Amount of textures in the array.
 * @param flg The texture creation flags.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes (optional).
 * @return A pointer to the created texture2d array alias, or nullptr on error.
 */
BaseTexture *alias_array_tex(BaseTexture *baseTexture, int w, int h, int d, int flg, int levels, const char *stat_name);

/**
 * @brief Create a cube array texture alias, a texture using the same memory as another cube array texture but with a different format.
 * @param baseTexture The base cube array texture to alias.
 * @param side The side of the cube texture.
 * @param d Amount of textures in the array.
 * @param flg The texture creation flags.
 * @param levels The maximum number of mipmap levels.
 * @param stat_name The name of the texture for statistics purposes (optional).
 * @return A pointer to the created cube array texture alias, or nullptr on error.
 */
BaseTexture *alias_cube_array_tex(BaseTexture *baseTexture, int side, int d, int flg, int levels, const char *stat_name);

/**
 * @brief Stretch a rectangle from the source texture to the destination texture.
 *
 * Under the hood it is a call of CopySubresourceRegion if source and destination textures are the same type and their texel could be
 * mapped one to one. Otherwise it is an execution of a shader that does the stretching.
 *
 * @param src The source texture.
 * @param dst The destination texture.
 * @param rsrc The source rectangle (optional).
 * @param rdst The destination rectangle (optional).
 * @return Returns true if the stretch operation was successful, otherwise returns false.
 */
bool stretch_rect(BaseTexture *src, BaseTexture *dst, const RectInt *rsrc = nullptr, const RectInt *rdst = nullptr);

/**
 * @brief Make a replacement for tex with fewer mips, migrating the mips both keep.
 *
 * Mip level_offset + n of tex becomes mip n of the replacement, for every n at or above
 * start_src_level - level_offset. A migrated mip is copied without change: nothing is resampled.
 * A mip of the replacement below the first migrated one keeps undefined content for the caller to
 * fill. The caller owns the replacement and gives it to BaseTexture::replaceTexResObject.
 *
 * width, height and depth must be the extent of mip level_offset of tex, so that a migrated mip has
 * the same extent in both textures. The extent and mips are independent arguments and no driver
 * checks that they agree.
 *
 * Works without TEXCF_UPDATE_DESTINATION on tex, even where the generic implementation needs it.
 *
 * @param tex The texture to make a replacement for.
 * @param width The width of mip 0 of the replacement.
 * @param height The height of mip 0 of the replacement.
 * @param depth The depth of mip 0 for a volume, the slice count for an array or a cube array, 1
 *   for the other types.
 * @param mips The mip count of the replacement.
 * @param start_src_level The first mip of tex to migrate. Mips of tex below level_offset have no
 *   mip in the replacement, so a smaller value acts as level_offset.
 * @param level_offset The distance between the two mip chains.
 * @return The replacement, or nullptr if it could not be allocated.
 */
[[nodiscard]] BaseTexture *down_size_tex(BaseTexture *tex, int width, int height, int depth, int mips, unsigned start_src_level,
  unsigned level_offset);

/**
 * @brief Make a replacement for tex with more mips, migrating the mips both keep.
 *
 * Mip n of tex becomes mip level_offset + n of the replacement, for every n at or above
 * start_src_level. A migrated mip is copied without change: nothing is resampled, and a mip of the
 * replacement below level_offset + start_src_level keeps undefined content for the caller to fill.
 * The caller owns the replacement and gives it to BaseTexture::replaceTexResObject.
 *
 * The extent of tex must be the extent of mip level_offset of the replacement, so that a migrated
 * mip has the same extent in both textures. The extent and mips are independent arguments and no
 * driver checks that they agree.
 *
 * Works without TEXCF_UPDATE_DESTINATION on tex, even where the generic implementation needs it.
 *
 * @param tex The texture to make a replacement for.
 * @param width The width of mip 0 of the replacement.
 * @param height The height of mip 0 of the replacement.
 * @param depth The depth of mip 0 for a volume, the slice count for an array or a cube array, 1
 *   for the other types.
 * @param mips The mip count of the replacement.
 * @param start_src_level The first mip of tex to migrate.
 * @param level_offset The distance between the two mip chains.
 * @return The replacement, or nullptr if it could not be allocated.
 */
[[nodiscard]] BaseTexture *up_size_tex(BaseTexture *tex, int width, int height, int depth, int mips, unsigned start_src_level,
  unsigned level_offset);

/**
 * @brief Get the texture statistics.
 * @param num_textures Pointer to store the number of textures.
 * @param total_mem Pointer to store the total memory used by textures.
 * @param out_dump Pointer to store the texture statistics dump.
 */
void get_texture_statistics(uint32_t *num_textures, uint64_t *total_mem, String *out_dump);

/**
 * @brief Set a texture for a shader stage and slot.
 * @param shader_stage The shader stage. One of the STAGE_XXX flags.
 * @param slot The slot.
 * @param tex The texture to set.
 * @return Returns true if the texture was successfully set, otherwise returns false.
 */
bool set_tex(unsigned shader_stage, unsigned slot, BaseTexture *tex);

/**
 * @brief Set a texture for a pixel shader slot.
 * @param slot The slot.
 * @param tex The texture to set.
 * @return Returns true if the texture was successfully set, otherwise returns false.
 */
inline bool settex(int slot, BaseTexture *tex) { return set_tex(STAGE_PS, slot, tex); }

/**
 * @brief Set a texture for a vertex shader slot.
 * @param slot The slot.
 * @param tex The texture to set.
 * @return Returns true if the texture was successfully set, otherwise returns false.
 */
inline bool settex_vs(int slot, BaseTexture *tex) { return set_tex(STAGE_VS, slot, tex); }

/**
 * @brief Copy a region of one texture subresource into another.
 *
 * The copy runs in the order it was recorded against the other recorded commands.
 *
 * Requires TEXCF_UPDATE_DESTINATION, TEXCF_RTARGET or TEXCF_UNORDERED usage on dst.
 *
 * @param src The source texture.
 * @param src_subres_idx The source subresource index, see BaseTexture::calcSubResIdx.
 * @param src_x The x origin of the copied region in the source subresource.
 * @param src_y The y origin of the copied region in the source subresource.
 * @param src_z The z origin of the copied region in the source subresource.
 * @param src_w The width of the copied region.
 * @param src_h The height of the copied region.
 * @param src_d The depth of the copied region.
 * @param dst The destination texture.
 * @param dst_subres_idx The destination subresource index, see BaseTexture::calcSubResIdx.
 * @param dst_x The x origin of the copied region in the destination subresource.
 * @param dst_y The y origin of the copied region in the destination subresource.
 * @param dst_z The z origin of the copied region in the destination subresource.
 * @return Returns non-zero on success, 0 on error.
 */
int update_sub_region(BaseTexture *src, int src_subres_idx, int src_x, int src_y, int src_z, int src_w, int src_h, int src_d,
  BaseTexture *dst, int dst_subres_idx, int dst_x, int dst_y, int dst_z);

/**
 * @brief Same as update_sub_region, but the driver can run the copy out of its recorded order.
 *
 * Use it only when no command near the copy depends on that order.
 */
int update_sub_region_no_order(BaseTexture *src, int src_subres_idx, int src_x, int src_y, int src_z, int src_w, int src_h, int src_d,
  BaseTexture *dst, int dst_subres_idx, int dst_x, int dst_y, int dst_z);

/**
 * @brief Discard the texture. It initializes the texture leaving the texels in an undefined state.
 * @param tex Texture to discard. Now usage is limited to UA and RT textures only.
 * @return true if the operation was successful, false otherwise.
 */
bool discard_tex(BaseTexture *tex);

} // namespace d3d _MULTI_INTERFACE


#if _TARGET_D3D_MULTI
#include <drv/3d/dag_interface_table.h>
namespace d3d
{
inline bool check_texformat(int cflg) { return d3di.check_texformat(cflg); }
inline int get_max_sample_count(int cflg) { return d3di.get_max_sample_count(cflg); }
inline unsigned get_texformat_usage(int cflg, D3DResourceType type) { return d3di.get_texformat_usage(cflg, type); }
inline bool issame_texformat(int cflg1, int cflg2) { return d3di.issame_texformat(cflg1, cflg2); }
inline bool check_cubetexformat(int cflg) { return d3di.check_cubetexformat(cflg); }
inline bool check_voltexformat(int cflg) { return d3di.check_voltexformat(cflg); }

inline BaseTexture *create_tex(TexImage32 *img, int w, int h, int flg, int levels, const char *stat_name, ResourceTagType tag)
{
  return d3di.create_tex(img, w, h, flg, levels, stat_name, tag);
}
inline BaseTexture *create_cubetex(int size, int flg, int levels, const char *stat_name, ResourceTagType tag)
{
  return d3di.create_cubetex(size, flg, levels, stat_name, tag);
}
inline BaseTexture *create_voltex(int w, int h, int d, int flg, int levels, const char *stat_name, ResourceTagType tag)
{
  return d3di.create_voltex(w, h, d, flg, levels, stat_name, tag);
}
inline BaseTexture *create_array_tex(int w, int h, int d, int flg, int levels, const char *stat_name, ResourceTagType tag)
{
  return d3di.create_array_tex(w, h, d, flg, levels, stat_name, tag);
}
inline BaseTexture *create_cube_array_tex(int side, int d, int flg, int levels, const char *stat_name, ResourceTagType tag)
{
  return d3di.create_cube_array_tex(side, d, flg, levels, stat_name, tag);
}

inline BaseTexture *create_ddsx_tex(IGenLoad &crd, int flg, int quality_id, int levels, const char *stat_name)
{
  return d3di.create_ddsx_tex(crd, flg, quality_id, levels, stat_name);
}

inline BaseTexture *alloc_ddsx_tex(const ddsx::Header &hdr, int flg, int quality_id, int levels, const char *stat_name,
  int stub_tex_idx)
{
  return d3di.alloc_ddsx_tex(hdr, flg, quality_id, levels, stat_name, stub_tex_idx);
}

inline BaseTexture *alias_tex(BaseTexture *baseTexture, TexImage32 *img, int w, int h, int flg, int levels, const char *stat_name)
{
  return d3di.alias_tex(baseTexture, img, w, h, flg, levels, stat_name);
}

inline BaseTexture *alias_cubetex(BaseTexture *baseTexture, int size, int flg, int levels, const char *stat_name)
{
  return d3di.alias_cubetex(baseTexture, size, flg, levels, stat_name);
}

inline BaseTexture *alias_voltex(BaseTexture *baseTexture, int w, int h, int d, int flg, int levels, const char *stat_name)
{
  return d3di.alias_voltex(baseTexture, w, h, d, flg, levels, stat_name);
}

inline BaseTexture *alias_array_tex(BaseTexture *baseTexture, int w, int h, int d, int flg, int levels, const char *stat_name)
{
  return d3di.alias_array_tex(baseTexture, w, h, d, flg, levels, stat_name);
}

inline BaseTexture *alias_cube_array_tex(BaseTexture *baseTexture, int side, int d, int flg, int levels, const char *stat_name)
{
  return d3di.alias_cube_array_tex(baseTexture, side, d, flg, levels, stat_name);
}

inline bool stretch_rect(BaseTexture *src, BaseTexture *dst, const RectInt *rsrc, const RectInt *rdst)
{
  return d3di.stretch_rect(src, dst, rsrc, rdst);
}

inline BaseTexture *down_size_tex(BaseTexture *tex, int width, int height, int depth, int mips, unsigned start_src_level,
  unsigned level_offset)
{
  return d3di.down_size_tex(tex, width, height, depth, mips, start_src_level, level_offset);
}

inline BaseTexture *up_size_tex(BaseTexture *tex, int width, int height, int depth, int mips, unsigned start_src_level,
  unsigned level_offset)
{
  return d3di.up_size_tex(tex, width, height, depth, mips, start_src_level, level_offset);
}

inline void get_texture_statistics(uint32_t *num_textures, uint64_t *total_mem, String *out_dump)
{
  d3di.get_texture_statistics(num_textures, total_mem, out_dump);
}

inline bool set_tex(unsigned shader_stage, unsigned slot, BaseTexture *tex) { return d3di.set_tex(shader_stage, slot, tex); }

inline int update_sub_region(BaseTexture *src, int src_subres_idx, int src_x, int src_y, int src_z, int src_w, int src_h, int src_d,
  BaseTexture *dst, int dst_subres_idx, int dst_x, int dst_y, int dst_z)
{
  return d3di.update_sub_region(src, src_subres_idx, src_x, src_y, src_z, src_w, src_h, src_d, dst, dst_subres_idx, dst_x, dst_y,
    dst_z);
}

inline int update_sub_region_no_order(BaseTexture *src, int src_subres_idx, int src_x, int src_y, int src_z, int src_w, int src_h,
  int src_d, BaseTexture *dst, int dst_subres_idx, int dst_x, int dst_y, int dst_z)
{
  return d3di.update_sub_region_no_order(src, src_subres_idx, src_x, src_y, src_z, src_w, src_h, src_d, dst, dst_subres_idx, dst_x,
    dst_y, dst_z);
}

inline bool discard_tex(BaseTexture *tex) { return d3di.discard_tex(tex); }
} // namespace d3d
#endif
