// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include "pointing_device.h"


namespace HumanInput
{

class GamepadMouseEmuDevice final : public GameInputPointingDevice
{
public:
  bool cursorEnabled = true;
  bool buttonsEnabled = true;

  const char *getName() const override { return "Mouse @ Gamepad"; }
  int getBtnCount() const override { return 1; }
  const char *getBtnName(int idx) const override { return idx == 0 ? "LMB" : nullptr; }

  bool isEnabled() const { return cursorEnabled || buttonsEnabled; }

  void update() override;

private:
  float incX = 0.f, incY = 0.f;
  uint64_t lastButtons = 0;
};

} // namespace HumanInput
