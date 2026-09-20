// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <grdk.h>
#include <GameInput.h>
#include <EASTL/array.h>
#include <EASTL/unique_ptr.h>


namespace gameinput
{

constexpr size_t MAX_DEVICES_PER_TYPE = 8;
using DevicesList = eastl::array<IGameInputDevice *, MAX_DEVICES_PER_TYPE>;

struct ReadingDeleter
{
  void operator()(IGameInputReading *reading);
};
using Reading = eastl::unique_ptr<IGameInputReading, ReadingDeleter>;

void init();
void shutdown();

unsigned get_devices_config_generation(GameInputKind kind);

Reading get_current_reading(GameInputKind kind, IGameInputDevice *device);

DevicesList get_devices(GameInputKind kind);
bool has_input_device_of_kind(GameInputKind kind);

} // namespace gameinput
