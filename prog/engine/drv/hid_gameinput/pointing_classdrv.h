// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <drv/hid/dag_hiPointing.h>

#include "composite_pointing_device.h"


namespace HumanInput
{

class GameInputPointingClassDriver final : public IGenPointingClassDrv
{
public:
  GameInputCompositePointingDevice composite;

  int getDeviceCount() const override { return 1; }
  IGenPointing *getDevice(int idx) const override { return idx == 0 ? (IGenPointing *)&composite : nullptr; }
  void useDefClient(IGenPointingClient *cli) override { composite.setClient(cli); }

  void enable(bool en) override
  {
    composite.emuEnabled = en;
    composite.updateStgMouseEnabled();
  }
  void acquireDevices() override {}
  void unacquireDevices() override {}
  void destroy() override;
  void refreshDeviceList() override {}
  void updateDevices() override;
};

} // namespace HumanInput
