// Built with ECS codegen version 1.0
#include <daECS/core/entitySystem.h>
#include <daECS/core/componentTypes.h>
#include "cameraInCameraES.cpp.inl"
ECS_DEF_PULL_VAR(cameraInCamera);
#include <daECS/core/internal/performQuery.h>
static constexpr ecs::ComponentDesc camcam_preprocess_prev_frame_weapon_es_comps[] =
{
//start of 1 rw components at [0]
  {ECS_HASH("camcam__lens_only_zoom_enabled"), ecs::ComponentTypeInfo<bool>()}
};
static void camcam_preprocess_prev_frame_weapon_es_all(const ecs::UpdateStageInfo &__restrict info, const ecs::QueryView & __restrict components)
{
  auto comp = components.begin(), compE = components.end(); G_ASSERT(comp!=compE);
  do
    camcam_preprocess_prev_frame_weapon_es(*info.cast<ecs::UpdateStageInfoAct>()
    , ECS_RW_COMP(camcam_preprocess_prev_frame_weapon_es_comps, "camcam__lens_only_zoom_enabled", bool)
    );
  while (++comp != compE);
}
static ecs::EntitySystemDesc camcam_preprocess_prev_frame_weapon_es_es_desc
(
  "camcam_preprocess_prev_frame_weapon_es",
  "prog/daNetGame/render/world/cameraInCameraES.cpp.inl",
  ecs::EntitySystemOps(camcam_preprocess_prev_frame_weapon_es_all),
  make_span(camcam_preprocess_prev_frame_weapon_es_comps+0, 1)/*rw*/,
  empty_span(),
  empty_span(),
  empty_span(),
  ecs::EventSetBuilder<>::build(),
  (1<<ecs::UpdateStageInfoAct::STAGE)
,"render",nullptr,"camcam_activate_view_es,camera_set_sync");
//static constexpr ecs::ComponentDesc camcam_activate_view_es_comps[] ={};
static void camcam_activate_view_es_all(const ecs::UpdateStageInfo &__restrict info, const ecs::QueryView & __restrict components)
{
  G_UNUSED(components);
    camcam_activate_view_es(*info.cast<ecs::UpdateStageInfoAct>());
}
static ecs::EntitySystemDesc camcam_activate_view_es_es_desc
(
  "camcam_activate_view_es",
  "prog/daNetGame/render/world/cameraInCameraES.cpp.inl",
  ecs::EntitySystemOps(camcam_activate_view_es_all),
  empty_span(),
  empty_span(),
  empty_span(),
  empty_span(),
  ecs::EventSetBuilder<>::build(),
  (1<<ecs::UpdateStageInfoAct::STAGE)
,"render",nullptr,"camera_update_lods_scaling_es","update_shooter_camera_aim_parameters_es");
