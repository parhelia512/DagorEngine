#ifndef DAGI_MEDIA_VOLUMES_HLSLI
#define DAGI_MEDIA_VOLUMES_HLSLI 1

// per-type media density volumes (tree canopies, bushes, any translucent
// aggregate), baked at runtime from the render mesh into an extinction atlas
#define DAGI_MEDIA_VOL_BRICK 16   // atlas brick resolution per type
#define DAGI_MEDIA_VOL_RASTER 64  // bake raster = 4x4x4 sub-occupancy bits per brick voxel
#define DAGI_MEDIA_VOL_MIPS 5
// byte offset of the albedo accumulators after the sub-occupancy bits
#define DAGI_MEDIA_VOL_ALBEDO_OFS (DAGI_MEDIA_VOL_RASTER * DAGI_MEDIA_VOL_RASTER * DAGI_MEDIA_VOL_RASTER / 8)
// instance record: float3 pos + f16x3 scale + u16 type + snorm8x4 quat
#define DAGI_MEDIA_VOL_INST_SIZE 24
// per region instance cap: the type packs as u16 and the grid slot offsets keep 20 bits
#define DAGI_MEDIA_VOLS_MAX_REGION_INSTANCES ((64 << 10) - 1)
// grid cell word: slot list offset in the low bits, entry count above
#define DAGI_MEDIA_VOL_GRID_OFS_BITS 20
#define DAGI_MEDIA_VOL_GRID_MAX_COUNT ((1 << (32 - DAGI_MEDIA_VOL_GRID_OFS_BITS)) - 1)
// finalize cs uav registers, bound from C++: dshl can not bind a specific mip, and dx11
// allows only u0..u7 so the layout must be explicit to not collide with the allocator.
// DAGI_MEDIA_VOL_UAV_REG makes the shader declarations read these same numbers
#define DAGI_MEDIA_VOL_ALBEDO_UAV_NO 2
#define DAGI_MEDIA_VOL_ATLAS_UAV_NO 3
#define DAGI_MEDIA_VOL_UAV_CAT(a, b) a##b
#define DAGI_MEDIA_VOL_UAV_REG(n) register(DAGI_MEDIA_VOL_UAV_CAT(u, n))

#ifndef __cplusplus
// atlas cell of a type: types pack as a near cubic 3d grid of bricks (grid.w = x*y)
uint3 dagi_media_vol_atlas_cell(uint type, int4 grid)
{
  return uint3(type % uint(grid.x), (type / uint(grid.x)) % uint(grid.y), type / uint(grid.w));
}
#endif

#endif
