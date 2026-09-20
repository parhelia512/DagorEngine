// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <drv/hid/dag_hiPointing.h>
#include <drv/hid/dag_hiGlobals.h>


namespace HumanInput
{

class GameInputPointingDevice : public IGenPointing
{
public:
  virtual ~GameInputPointingDevice() = default;

  const PointingRawState &getRawState() const override { return raw_state_pnt; }

  IGenPointingClient *getClient() const override { return client; }
  void setClient(IGenPointingClient *cli) override
  {
    if (cli == client)
      return;
    if (client)
      client->detached(this);
    client = cli;
    if (client)
      client->attached(this);
  }

  void setRelativeMovementMode(bool) override {}
  bool getRelativeMovementMode() override { return true; }
  void setClipRect(int, int, int, int) override {}
  void setPosition(int, int) override {}
  void setMouseCapture(void *) override {}
  void releaseMouseCapture() override {}
  bool isPointerOverWindow() override { return true; }

  virtual void update() = 0;

protected:
  IGenPointingClient *client = nullptr;

  void onMove(float dx, float dy)
  {
    if (client && (dx || dy))
      client->gmcMouseMove(this, dx, dy);
  }
  void onButtonDown(int btn)
  {
    if (client)
      client->gmcMouseButtonDown(this, btn);
  }
  void onButtonUp(int btn)
  {
    if (client)
      client->gmcMouseButtonUp(this, btn);
  }
  void onWheel(int delta)
  {
    if (client && delta)
    {
      client->gmcMouseWheel(this, delta);
      static constexpr int mouseWheelUp = 5;
      static constexpr int mouseWheelDown = 6;
      const int wheelBtn = delta > 0 ? mouseWheelUp : mouseWheelDown;
      client->gmcMouseButtonDown(this, wheelBtn);
      client->gmcMouseButtonUp(this, wheelBtn);
    }
  }
};

} // namespace HumanInput
