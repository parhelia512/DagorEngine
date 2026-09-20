// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <animated_splash_screen_api.h>

void animated_splash_screen_set_config(const DataBlock *) {}
void animated_splash_screen_set_graph(const char *, const DataBlock *) {}
void animated_splash_screen_start(bool) {}
void animated_splash_screen_stop() {}
void animated_splash_screen_draw() {}
void debug_animated_splash_screen() {}
bool is_animated_splash_screen_started() { return false; }
bool is_animated_splash_screen_encoding() { return false; }
void animated_splash_screen_begin_slow_exit() {}
void animated_splash_screen_begin_rush_exit() {}
SplashExitSeconds animated_splash_screen_exit_seconds() { return SplashExitSeconds(); }
bool animated_splash_screen_opens_onto_game() { return false; }
// no splash to wait for, so a host asking is already past it
bool animated_splash_screen_exit_done() { return true; }
bool animated_splash_screen_rush_under_way() { return false; }
void animated_splash_screen_set_over_game(bool) {}

void start_animated_splash_screen_in_thread() {}
void stop_animated_splash_screen_in_thread() {}
void stop_animated_splash_screen_thread_keep_scene() {}
bool is_animated_splash_screen_in_thread() { return false; }
void animated_splash_screen_allow_watchdog_kick(bool) {}
void animated_splash_screen_register_render(void (*)(int, int, void *), void *, bool) {}
