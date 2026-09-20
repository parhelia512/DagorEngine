// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "composite_pointing_device.h"

#include <supp/dag_math.h>


namespace HumanInput
{

void GameInputCompositePointingDevice::init()
{
  hwMouse.setClient(this);
  emuMouse.setClient(this);
  updateStgMouseEnabled();
}

void GameInputCompositePointingDevice::updateStgMouseEnabled()
{
  stg_pnt.mouseEnabled = (emuEnabled && emuMouse.isEnabled()) || hwMouseEnabled;
}

void GameInputCompositePointingDevice::update()
{
  if (emuEnabled && emuMouse.isEnabled())
    emuMouse.update();
  if (hwMouseEnabled)
    hwMouse.update();
}

void GameInputCompositePointingDevice::setClipRect(int l, int t, int r, int b)
{
  clip = {l, t, r, b};
  hwMouse.setDpiScale(max(1, b / 1080));
  if (clampStateCoord())
    setPosition(raw_state_pnt.mouse.x, raw_state_pnt.mouse.y);
}

void GameInputCompositePointingDevice::setPosition(int x, int y)
{
  raw_state_pnt.mouse.x = x;
  raw_state_pnt.mouse.y = y;
  clampStateCoord();

  if (client)
    client->gmcMouseMove(this, 0, 0);
}

void GameInputCompositePointingDevice::gmcMouseMove(IGenPointing *, float _dx, float _dy)
{
  float dx = _dx * stg_pnt.xSens, dy = _dy * stg_pnt.ySens;

  raw_state_pnt.mouse.deltaX += dx;
  raw_state_pnt.mouse.deltaY += dy;

  raw_state_pnt.mouse.x += dx;
  raw_state_pnt.mouse.y += dy;
  clampStateCoord();

  if (client && (dx || dy))
    client->gmcMouseMove(this, dx, dy);
}

void GameInputCompositePointingDevice::gmcMouseButtonDown(IGenPointing *, int btn)
{
  raw_state_pnt.mouse.buttons |= 1 << btn;
  if (client)
    client->gmcMouseButtonDown(this, btn);
}

void GameInputCompositePointingDevice::gmcMouseButtonUp(IGenPointing *, int btn)
{
  raw_state_pnt.mouse.buttons &= ~(1 << btn);
  if (client)
    client->gmcMouseButtonUp(this, btn);
}

void GameInputCompositePointingDevice::gmcMouseWheel(IGenPointing *, int delta)
{
  raw_state_pnt.mouse.deltaZ += delta * stg_pnt.zSens;
  if (client && delta)
    client->gmcMouseWheel(this, delta);
}

bool GameInputCompositePointingDevice::clampStateCoord()
{
  bool clipped = false;

  if (raw_state_pnt.mouse.x < clip.l)
  {
    raw_state_pnt.mouse.x = clip.l;
    clipped = true;
  }
  if (raw_state_pnt.mouse.y < clip.t)
  {
    raw_state_pnt.mouse.y = clip.t;
    clipped = true;
  }
  if (raw_state_pnt.mouse.x > clip.r)
  {
    raw_state_pnt.mouse.x = clip.r;
    clipped = true;
  }
  if (raw_state_pnt.mouse.y > clip.b)
  {
    raw_state_pnt.mouse.y = clip.b;
    clipped = true;
  }

  return clipped;
}

} // namespace HumanInput
