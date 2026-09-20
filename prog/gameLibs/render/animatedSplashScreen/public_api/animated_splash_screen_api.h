// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

class DataBlock;

// Scene config for the splash starts that follow. The block has the
// settings.blk loadingScreen{} layout (screens{}, default, mode, exact,
// chosen); a key it lacks still comes from settings.blk, so a list-only config
// keeps the user's mode, exact and chosen keys, though a name they pick that
// the config does not list falls back to its default.
// nullptr returns to settings.blk, and so does an empty block: every lookup
// falls through it.
// graphDir is not layered: where a scene's graph blk is packed is a build
// fact of the game, not a runtime choice.
// A set lands at the next start: one whose content differs from the active
// config re-resolves the scene there and restarts its visual session, an
// equal one changes nothing.
// The block is copied; safe to call from any thread.
void animated_splash_screen_set_config(const DataBlock *cfg);

// A scene's graph blk the game carries itself, for the window before the vrom
// holding that blk is mounted. scene_name is the resolved loadingScreen scene
// name, the one that also names <graphDir>/<scene>.graph.blk: an entry under a
// name no start resolves is never read, and nothing reports it. The entry lasts
// for the process. Used only where the scene's file does not exist, so a copy
// left registered never hides a newer one; a file that exists but
// cannot be read stays the failure it is. The block is copied; nullptr or an
// empty block drops the scene's copy. Unlike set_config above, this one is not
// guarded: call it on the main thread before the first start, which is where a
// start reads it.
void animated_splash_screen_set_graph(const char *scene_name, const DataBlock *blk);

void animated_splash_screen_start(bool do_encode = true);
// ends the session and its exit stamps
void animated_splash_screen_stop();
// Call under the d3d ownership lock (d3d::GpuAutoLock) held across the whole
// draw: a draw may reset the scene's graph, latch its failure and re-init the
// material, and the splash thread draws too while a session runs
void animated_splash_screen_draw();
bool is_animated_splash_screen_started();
bool is_animated_splash_screen_encoding();

// latch-once atomic stores, any thread
void animated_splash_screen_begin_slow_exit();
// implies the slow exit
void animated_splash_screen_begin_rush_exit();

// one word: the graph can vanish, and the word is loaded atomically
struct alignas(8) SplashExitSeconds
{
  // the graph blk's own seconds; 0 = the scene declares none
  float slow = 0.f, rush = 0.f;
};
SplashExitSeconds animated_splash_screen_exit_seconds();

// the scene's graph says its composite reveals the game under it
bool animated_splash_screen_opens_onto_game();

// the rush is over and the scene is done; main thread, after the thread stops
bool animated_splash_screen_exit_done();

// latched or requested; atomic loads, so any thread may ask
bool animated_splash_screen_rush_under_way();

// the host's next draw paints over its finished frame; clear it after that draw
void animated_splash_screen_set_over_game(bool over_game);

void start_animated_splash_screen_in_thread();
// stops thread and session both
void stop_animated_splash_screen_in_thread();
// keeps the scene for the host, but only while a rush runs
// blocking join: main thread only
void stop_animated_splash_screen_thread_keep_scene();
bool is_animated_splash_screen_in_thread();

void animated_splash_screen_register_render(void (*render_func)(int w, int h, void *arg), void *arg, bool exclusive = false);
void animated_splash_screen_allow_watchdog_kick(bool allow);
