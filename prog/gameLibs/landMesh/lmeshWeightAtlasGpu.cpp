// Copyright (C) Gaijin Games KFT.  All rights reserved.

// The GPU residency of the atlas: the texture, the records buffer and the
// layout the sampling shader reads. The packing itself is driver-free and
// stays in lmeshWeightAtlas.cpp, linkable without either library;
// Builder::finish() lives here because it makes the runtime atlas resident.

#include "lmeshWeightAtlasInternal.h"
#include <drv/3d/dag_texture.h>
#include <drv/3d/dag_tex3d.h>
#include <drv/3d/dag_driver.h>
#include <drv/3d/dag_info.h>
#include <drv/3d/dag_driverDesc.h>
#include <drv/3d/dag_shaderConstants.h>
#include <shaders/dag_shaderVariableInfo.h>
#include <startup/dag_globalSettings.h>
#include <ioSys/dag_dataBlock.h>
#include <math/dag_color.h>
#include <debug/dag_debug.h>
#include <perfMon/dag_cpuFreq.h>
#include <EASTL/unique_ptr.h>

#define GLOBAL_VARS_LIST          \
  VAR(land_weight_cells_const_no) \
  VAR(land_weight_uv_const_no)    \
  VAR(land_weight_ofs_const_no)   \
  VAR(land_weight_cell_const_no)  \
  VAR(land_weight_fold_const_no)

#define VAR(a) static ShaderVariableInfo a##VarId(#a, true);
GLOBAL_VARS_LIST
#undef VAR

// The layout constants sit at hardcoded ps registers (see land_weight_inc.dshl),
// so they are written straight there rather than through the shader var system.
static void set_ps_const4(const ShaderVariableInfo &slot, float x, float y, float z, float w)
{
  const Color4 v(x, y, z, w);
  if (slot.get_var_id() >= 0)
    d3d::set_ps_const(slot.get_int(), &v.r, 1);
}

bool land_weight_atlas_cpu_pack() { return d3d::check_texformat(TEXFMT_DXT1) && !land_weight_atlas_force_gpu(); }

// A raw bind, like the landmesh textures: the slot is above the samplers the
// renderer nulls per cell, so nothing in a land pass overwrites it. The atlas
// layout is published from here rather than at upload, so that the atlas being
// rendered from owns the globals even while another one is built (the editor
// rebuilding a map, a test packing its own).
void LandWeightAtlas::bindCells() const
{
  setShaderVars();
  if (cellsBuf && land_weight_cells_const_noVarId.get_var_id() >= 0)
    d3d::set_buffer(STAGE_PS, land_weight_cells_const_noVarId.get_int(), cellsBuf);
}

bool LandWeightAtlas::publishRecords()
{
  if (!cellsBuf)
  {
    cellsBuf = d3d::buffers::create_persistent_sr_byte_address(records.size(), "land_weight_cells");
    if (!cellsBuf) // nothing would bind the slot the shader reads its cell from
      return false;
    if (!cellsReload)
      cellsReload = new CellsReload(*this);
    cellsBuf->setReloadCallback(cellsReload);
  }
  return cellsBuf->updateData(0, data_size(records), records.data(), VBLOCK_WRITEONLY);
}

bool LandWeightAtlas::upload()
{
  if (kind == Pages::Raw || kind == Pages::Loaded) // the exporter encodes Raw pages; a Loaded atlas came resident
    return false;
  if (kind == Pages::Dxt1 && !pages.size()) // published already, the CPU copy is gone
    return false;
  if (cellsPastBudget) // once per batch: a refill of a whole map would log a line per cell
  {
    logerr("land weight atlas: %d cells blend more than the %d pages the atlas holds and render as a single landclass; "
           "blend fewer landclasses per cell",
      cellsPastBudget, pageBudget(pageW));
    cellsPastBudget = 0;
  }
  // reserved atlases are sized for the worst case up front, so painting never
  // has to recreate the texture (and invalidate the renderer's cell states)
  const int n = max(reserved && pages.size() ? (int)pages.size() / pageBytes : pageCount, 1);
  // MAX_TEX_SIDE caps the row search, not a device claim; the same cap on the
  // height keeps the atlas valid when the desc overstates the device limit
  G_ASSERT_RETURN(pageW <= min(d3d::get_driver_desc().maxtexw, d3d::get_driver_desc().maxtexh), false);
  const int maxW = clamp(d3d::get_driver_desc().maxtexw, pageW, max(pageW, MAX_TEX_SIDE)),
            maxH = clamp(d3d::get_driver_desc().maxtexh, pageW, max(pageW, MAX_TEX_SIDE));
  pagesPerRow = land_weight_choose_pages_per_row(n, pageW, maxW, maxH);
  const int rows = (n + pagesPerRow - 1) / pagesPerRow;
  const int w = pagesPerRow * pageW, h = rows * pageW;
  if (h > maxH)
  {
    logerr("land weight atlas: %d pages need %dx%d, more than the driver's %dx%d", n, w, h, maxW, maxH);
    return false;
  }
  if (!tex)
  {
    // the rendered path can carry no system copy, so the owner's flags apply
    // only where the CPU packs
    tex = d3d::create_tex(nullptr, w, h, kind == Pages::Dxt1 ? TEXFMT_DXT1 | extraTexCflg : TEXFMT_A8R8G8B8 | TEXCF_RTARGET, 1,
      "land_weight_atlas");
  }
  if (!tex || !publishRecords())
    return false;

  atlasW = w;
  atlasH = h;
  if (kind == Pages::Rendered) // renderCell() draws into it, there is nothing to copy up
    return true;

  uint8_t *dst = nullptr;
  int stride = 0;
  if (!tex->lockimg((void **)&dst, stride, 0, TEXLOCK_WRITE) || !dst)
    return false;
  const int rowsPerPage = pageW / 4;
  const int rowBytes = pageBytes / rowsPerPage;
  for (int p = 0; p < pageCount; p++)
  {
    const int gx = (p % pagesPerRow) * rowBytes, gy = (p / pagesPerRow) * rowsPerPage;
    const uint8_t *src = &pages[p * pageBytes];
    for (int row = 0; row < rowsPerPage; row++)
      memcpy(dst + (gy + row) * stride + gx, src + row * rowBytes, rowBytes);
  }
  tex->unlockimg();
  return true;
}

LandWeightAtlas *LandWeightAtlas::createLoaded(int cells_x, int cells_y, int elem_size, int pages_per_row, int page_count,
  SmallTab<uint32_t> &&packed_records, Texture *tex)
{
  eastl::unique_ptr<LandWeightAtlas> a(new LandWeightAtlas(Pages::Loaded, cells_x, cells_y, elem_size, 0, false));
  a->tex = tex; // owned from here on, whatever happens below
  if (!tex || pages_per_row <= 0 || packed_records.size() != a->records.size())
  {
    logerr("land weight atlas: the level's packed atlas is incomplete (%d records for %d cells); re-export the location",
      (int)packed_records.size() / WORDS_PER_CELL, cells_x * cells_y);
    return nullptr;
  }
  // the texture was composed for this build's page size: a border or rounding
  // change since the export shows as a size mismatch, and the records would
  // address the wrong texels
  TextureInfo ti;
  tex->getinfo(ti);
  const int rows = (max(page_count, 1) + pages_per_row - 1) / pages_per_row;
  if (ti.w != pages_per_row * a->pageW || ti.h != rows * a->pageW)
  {
    logerr("land weight atlas: the level's %dx%d atlas does not hold %d pages of %d texels; re-export the location", ti.w, ti.h,
      page_count, a->pageW);
    return nullptr;
  }
  a->records = eastl::move(packed_records);
  a->pagesPerRow = pages_per_row;
  a->pageCount = page_count;
  a->atlasW = ti.w;
  a->atlasH = ti.h;
  return a->publishRecords() ? a.release() : nullptr;
}

void LandWeightAtlas::setCellMapping(float cell_size, float grid_cell_size, const Point3 &mesh_offset,
  const IPoint2 &cell_origin) const
{
  const float invCell = cell_size > 0 ? 1.f / cell_size : 0.f;
  set_ps_const4(land_weight_cell_const_noVarId, invCell, -(mesh_offset.x * invCell + cell_origin.x),
    -(mesh_offset.z * invCell + cell_origin.y), (float)cellsX);
  // mirroring reflects about 0 and about half a grid cell inside the far edge,
  // which one triangle wave of this period does for any number of reflections
  const Point2 period(2 * (cellsX - 0.5f * grid_cell_size * invCell), 2 * (cellsY - 0.5f * grid_cell_size * invCell));
  set_ps_const4(land_weight_fold_const_noVarId, period.x, period.y, safeinv(period.x), safeinv(period.y));
}

void LandWeightAtlas::setShaderVars() const
{
  set_ps_const4(land_weight_uv_const_noVarId, (float)elemW / atlasW, (float)elemW / atlasH, (float)pageW / atlasW,
    (float)pageW / atlasH);
  set_ps_const4(land_weight_ofs_const_noVarId, (float)LAND_WEIGHT_BORDER / atlasW, (float)LAND_WEIGHT_BORDER / atlasH,
    (float)pagesPerRow, 0);
}

// the packing half must not ask the driver, so the format decision happens here
LandWeightAtlas::LandWeightAtlas(int cells_x, int cells_y, int elem_size, unsigned tex_cflg, bool reserve_for_edit) :
  LandWeightAtlas(land_weight_atlas_cpu_pack() ? Pages::Dxt1 : Pages::Rendered, cells_x, cells_y, elem_size, tex_cflg,
    reserve_for_edit)
{}

LandWeightAtlas *LandWeightAtlasBuilder::finish()
{
  int64_t reft = ref_time_ticks();
  if (texSize < 4 || elemSize < 1)
    return nullptr;
  eastl::unique_ptr<LandWeightAtlas> a(new LandWeightAtlas(cellsX, cellsY, elemSize, texCflg));
  if (a->getCellTexSize() > 4096)
    return nullptr;
  packCells(*a);
  if (!a->upload())
    return nullptr;
#if DAGOR_DBGLEVEL > 0
  selfCheck(*a);
#endif
  a->dropCpuPages();

  if (undecodedCells)
    logerr("land weight atlas: %d of %d cells have undecodable weight textures and render as a "
           "single landclass; re-export the location",
      undecodedCells, (int)cells.size());

  debug("land weight atlas: %dx%d cells, page %d (elem %d of source %d + border), %d pages, DXT1, in %d us", cellsX, cellsY,
    a->getCellTexSize(), elemSize, texSize, a->getPageCount(), (int)get_time_usec(reft));
  return a.release();
}
