// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <gameDebugRender/debugDraw.h>

namespace debug_draw
{
void register_cmd(eastl::string_view, eastl::string_view, bool) {}
void on_console_cmd(const char *[], int) {}
void flush_render(HudPrimitives *, const TMatrix4 &, const IPoint2 &, float) {}

void init() {}
void clear() {}
bool active(eastl::string_view, eastl::string_view) { return false; }
bool begin_debug(eastl::string_view, eastl::string_view) { return false; }
void end_debug() {}

void render_box(const Point3 &, const BBox3 &, uint32_t, bool, float) {}
void render_box(const BBox3 &, uint32_t, bool, float) {}
void render_box_tm(const TMatrix &, const BBox3 &, uint32_t, bool, float) {}
void render_sphere(const Point3 &, float, uint32_t, bool, float) {};
void render_cylinder(const Point3 &, const Point3 &, float, float, uint32_t, bool, float) {}
void render_square(const Point3 &, const Point3 &, float, uint32_t, bool, float) {}

void render_proj_tm(const TMatrix4 &, uint32_t, bool, float) {}
void render_proj_tm(const mat44f &, uint32_t, bool, float) {}
void render_tm_axis(const TMatrix &, bool, float) {}
void render_text(const Point3 &, eastl::string_view, uint32_t, float) {}
void render_text2d(const Point2 &, eastl::string_view, uint32_t, float) {}
void render_multi_text(dag::ConstSpan<Point3>, dag::ConstSpan<eastl::string_view>, const DebugMultiTextOverlay &, float) {}
void render_multi_text(dag::ConstSpan<Point3>, dag::ConstSpan<eastl::string_view>, dag::ConstSpan<int>, const DebugMultiTextOverlay &,
  float)
{}

void render_cb(eastl::function<void()>, float) {}
void render_snapshot(BaseTexture *, const char *) {}
eastl::vector<console::CommandOptions> get_cmd_options() { return {}; }
} // namespace debug_draw