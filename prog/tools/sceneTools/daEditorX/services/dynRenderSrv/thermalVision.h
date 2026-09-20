// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <math/dag_Point3.h>
#include <math/dag_color.h>
#include <math/dag_integer.h>
#include <EASTL/unique_ptr.h>

class BaseTexture;

class DataBlock;
class DeferredRenderTarget;
class NightVisionPostFx;
class TMatrix;
class TMatrix4;
class ThermalVision
{
public:
  ThermalVision();
  ~ThermalVision();

  ThermalVision(const ThermalVision &) = delete;
  ThermalVision &operator=(const ThermalVision &) = delete;

  void init();

  void setEnabled(bool enable);
  bool isEnabled() const { return enabled; }
  bool isSupported() const { return postFx != nullptr; }
  bool isActive() const;

  void resize(int target_w, int target_h);
  void closeTargets();

  void resolve(DeferredRenderTarget &gbuf, BaseTexture *dest, const TMatrix &view_tm, const TMatrix4 &proj_tm);
  void apply(BaseTexture *spectre, BaseTexture *dest);

private:
  void createTargets();

  eastl::unique_ptr<NightVisionPostFx> postFx;
  IPoint2 pendingSize = {0, 0};
  IPoint2 currentSize = {0, 0};
  bool enabled = false;
  bool noiseAcquired = false;
};
