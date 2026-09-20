//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <EASTL/functional.h>
#include <EASTL/string_view.h>
#include <render/debugMultiTextOverlay.h>
#include <util/dag_console.h>
#include <osApiWrappers/dag_threadSafety.h>

class IPoint2;
class BBox3;
class Point3;
class TMatrix4;
class HudPrimitives;
class BaseTexture;
struct mat44f;

namespace debug_draw
{
void register_cmd(eastl::string_view sys, eastl::string_view cmd, bool auto_toggle = true);

void on_console_cmd(const char *argv[], int argc);
void flush_render(HudPrimitives *imm_prim, const TMatrix4 &globtm, const IPoint2 &target_size, float current_time);

void init();
void clear();
bool active(eastl::string_view sys, eastl::string_view cmd);
bool begin_debug(eastl::string_view sys, eastl::string_view cmd);
void end_debug();

void render_box(const BBox3 &box, uint32_t color, bool use_depth, float time = 0);
void render_box(const Point3 &pos, const BBox3 &box, uint32_t color, bool use_depth, float time = 0);
void render_box_tm(const TMatrix &tm, const BBox3 &box, uint32_t color, bool use_depth, float time = 0);
void render_square(const Point3 &pos, const Point3 &axis, float width, uint32_t color, bool use_depth, float time = 0);
void render_sphere(const Point3 &pos, float radius, uint32_t color, bool use_depth, float time = 0);
void render_cylinder(const Point3 &base_center, const Point3 &base_normal, float radius, float height, uint32_t color, bool use_depth,
  float time = 0);
void render_proj_tm(const TMatrix4 &tm, uint32_t color, bool use_depth, float time = 0);
void render_proj_tm(const mat44f &tm, uint32_t color, bool use_depth, float time = 0);
void render_tm_axis(const TMatrix &tm, bool use_depth, float time = 0);
void render_text(const Point3 &pos, eastl::string_view text, uint32_t color, float time = 0);
void render_text2d(const Point2 &pos, eastl::string_view text, uint32_t color, float time = 0);
void render_multi_text(dag::ConstSpan<Point3> positions, dag::ConstSpan<eastl::string_view> names, const DebugMultiTextOverlay &cfg,
  float time = 0);
void render_multi_text(dag::ConstSpan<Point3> positions, dag::ConstSpan<eastl::string_view> names, dag::ConstSpan<int> complexities,
  const DebugMultiTextOverlay &cfg, float time = 0);
void render_cb(eastl::function<void()> cb, float time = 0);
void render_snapshot(BaseTexture *tex, const char *name); // will make a copy or texture and render it (saved for 1 frame, usecase -
                                                          // temp targets)
eastl::vector<console::CommandOptions> get_cmd_options();
} // namespace debug_draw