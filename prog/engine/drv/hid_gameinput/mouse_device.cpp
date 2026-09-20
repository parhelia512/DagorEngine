// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "mouse_device.h"

#include <util/dag_globDef.h>


namespace HumanInput
{

const char *GameInputMouseDevice::getBtnName(int idx) const
{
  static const char *btn_names[TOTAL_BUTTONS] = {"LMB", "RMB", "MMB", "M4B", "M5B", "MWUp", "MWDown"};
  return idx >= 0 && idx < TOTAL_BUTTONS ? btn_names[idx] : nullptr;
}

void GameInputMouseDevice::updateMouseState(MouseState &state, IGameInputDevice *device)
{
  if (!device)
    return;

  gameinput::Reading reading = gameinput::get_current_reading(GameInputKindMouse, device);
  if (!reading)
    return;

  GameInputMouseState mouseState;
  if (!reading->GetMouseState(&mouseState))
    return;

  static constexpr uint32_t mouseButtons[] = {
    GameInputMouseButtons::GameInputMouseLeftButton,
    GameInputMouseButtons::GameInputMouseRightButton,
    GameInputMouseButtons::GameInputMouseMiddleButton,
    GameInputMouseButtons::GameInputMouseButton4,
    GameInputMouseButtons::GameInputMouseButton5,
  };

  for (int buttonNo = 0; buttonNo < countof(mouseButtons); buttonNo++)
  {
    uint64_t buttonBit = 1ull << buttonNo;
    if (mouseState.buttons & mouseButtons[buttonNo])
    {
      if (!(state.lastHwButtons & buttonBit))
        onButtonDown(buttonNo);
      state.lastHwButtons |= buttonBit;
    }
    else
    {
      if (state.lastHwButtons & buttonBit)
        onButtonUp(buttonNo);
      state.lastHwButtons &= ~buttonBit;
    }
  }

  IPoint2 newHwPos(mouseState.positionX, mouseState.positionY);
  IPoint2 delta = newHwPos - state.currentHwPos;
  delta *= dpiScale;
  state.currentHwPos = newHwPos;
  if (delta.x != 0 || delta.y != 0)
    onMove(delta.x, delta.y);

  int wheelInput = mouseState.wheelY;
  int wheelDelta = wheelInput - state.currentHwWheel;
  state.currentHwWheel = wheelInput;
  if (wheelDelta != 0)
    onWheel(wheelDelta);
}

void GameInputMouseDevice::update()
{
  gameinput::DevicesList mouses = gameinput::get_devices(GameInputKindMouse);

  for (size_t mouseNo = 0; mouseNo < mouses.size(); ++mouseNo)
    updateMouseState(mouse_states[mouseNo], mouses[mouseNo]);
}

} // namespace HumanInput
