//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <math/dag_color.h>

class BaseTexture;

namespace prerotation
{
void rotate_rect(int angle, int target_w, int target_h, int &l, int &t, int &w, int &h);

#if _TARGET_ANDROID

void refresh();

int frame_angle();

int angle_for_target(BaseTexture *dst);

Color4 ndc_rotation(int angle);

void set_shader_var(int angle);
int get_current_angle();

class Scope
{
  int savedAngle;

public:
  explicit Scope(BaseTexture *dst);
  ~Scope();
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;
};

#else

inline void refresh() {}

inline int frame_angle() { return 0; }

inline int angle_for_target(BaseTexture *) { return 0; }

inline Color4 ndc_rotation(int) { return Color4(1, 0, 0, 1); }

inline void set_shader_var(int) {}
inline int get_current_angle() { return 0; }

class Scope
{
public:
  explicit Scope(BaseTexture *) {}
  ~Scope() {}
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;
};

#endif
} // namespace prerotation
