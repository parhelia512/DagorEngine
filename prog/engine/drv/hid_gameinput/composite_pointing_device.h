// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <drv/hid/dag_hiPointing.h>
#include <drv/hid/dag_hiGlobals.h>

#include "pointing_device.h"
#include "mouse_device.h"
#include "mouse_emu_device.h"


namespace HumanInput
{

class GameInputCompositePointingDevice final : public GameInputPointingDevice, public IGenPointingClient
{
public:
  bool hwMouseEnabled = false;
  bool emuEnabled = false;

  GameInputMouseDevice hwMouse;
  GamepadMouseEmuDevice emuMouse;

  const char *getName() const override { return "CompositePointer"; }

  int getBtnCount() const override { return hwMouse.getBtnCount(); }
  const char *getBtnName(int idx) const override { return hwMouse.getBtnName(idx); }

  void setRelativeMovementMode(bool en) override { isRelative = en; }
  bool getRelativeMovementMode() override { return isRelative; }
  void setClipRect(int l, int t, int r, int b) override;
  void setPosition(int x, int y) override;

  void attached(IGenPointing *) override {}
  void detached(IGenPointing *) override {}
  void gmcMouseMove(IGenPointing *mouse, float dx, float dy) override;
  void gmcMouseButtonDown(IGenPointing *mouse, int btn) override;
  void gmcMouseButtonUp(IGenPointing *mouse, int btn) override;
  void gmcMouseWheel(IGenPointing *mouse, int scroll) override;

  void init();
  void update() override;
  void updateStgMouseEnabled();

private:
  bool isRelative = true;
  struct
  {
    int l, t, r, b;
  } clip = {0, 0, 1024, 1024};

  bool clampStateCoord();
};

} // namespace HumanInput
