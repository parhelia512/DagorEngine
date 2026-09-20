// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <daECS/core/event.h>
#include <ioSys/dag_dataBlock.h>

class DataBlock;
class LandMeshManager;
class RenderScene;


bool is_level_loaded();
bool is_level_loaded_not_empty();
// the loading screen ends by opening onto the game's own frame, and this host
// draws it. The renderer publishes it; it lives in the das module because the
// das AOT links that and not the renderer
bool loading_splash_opens_onto_game();
void set_loading_splash_opens_onto_game(bool opens);
// the renderer's own answer, for a caller that runs before its next draw
bool splash_opens_onto_game();
// the splash is covering a loaded game right now; safe from any thread
bool is_splash_over_game_active();
bool is_level_loaded_no_binary();
bool is_level_loading();
bool is_level_unloading();
ecs::EntityId get_current_level_eid();

void select_weather_preset(const char *preset_name);
void select_weather_preset_delayed(const char *preset_name);
void update_delayed_weather_selection();

const char *get_rendinst_dmg_blk_fn();

const LandMeshManager *get_landmesh_manager();
RenderScene *get_rivers();

void save_weather_settings_to_screenshot(DataBlock &blk);

ECS_BROADCAST_EVENT_TYPE(EventLevelLoaded, const DataBlock & /*level_blk*/);
ECS_BROADCAST_EVENT_TYPE(EventGameObjectsCreated, ecs::EntityId); /* EntityId of game_objects entity */
ECS_BROADCAST_EVENT_TYPE(EventGameObjectsEntitiesScheduled, int); /* count of scheduled (or created if synced) entities */
// all Game Objects that are not needed anymore, can be destroyed
// this event is always called immediatele after EventGameObjectsCreated
ECS_BROADCAST_EVENT_TYPE(EventGameObjectsOptimize, ecs::EntityId); /* EntityId of game_objects entity */
ECS_UNICAST_EVENT_TYPE(EventRIGenExtraRequested);                  /* EntityId of game_objects entity */
