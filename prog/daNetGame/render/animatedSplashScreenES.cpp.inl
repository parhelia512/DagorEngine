// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <animated_splash_screen_api.h>

#include <daECS/core/entitySystem.h>
#include <daECS/net/netEvents.h>

static void stop_splash_on_disconnect_es_event_handler(const EventOnDisconnectedFromServer &)
{
  stop_animated_splash_screen_in_thread();
}
