// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include "pointing_device.h"
#include "gameinput.h"

#include <math/integer/dag_IPoint2.h>


namespace HumanInput
{

class GameInputMouseDevice final : public GameInputPointingDevice
{
public:
  constexpr static int TOTAL_BUTTONS = 7;

  const char *getName() const override { return "Mouse"; }
  int getBtnCount() const override { return TOTAL_BUTTONS; }
  const char *getBtnName(int idx) const override;

  void setDpiScale(int scale) { dpiScale = scale; }

  void update() override;

private:
  struct MouseState
  {
    IPoint2 currentHwPos = {0, 0};
    uint64_t lastHwButtons = 0;
    int currentHwWheel = 0;
  };

  MouseState mouse_states[gameinput::MAX_DEVICES_PER_TYPE] = {};
  int dpiScale = 1;

  void updateMouseState(MouseState &state, IGameInputDevice *device);
};

} // namespace HumanInput
