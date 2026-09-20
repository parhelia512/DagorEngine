// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <render/cameraInCamera/cameraInCamera.h>
#include <ecs/render/aimRender.h>

#include <daECS/core/entitySystem.h>
#include <daECS/core/updateStage.h>
#include <render/rendererFeatures.h>

ECS_TAG(render)
ECS_BEFORE(camera_set_sync)
ECS_BEFORE(camcam_activate_view_es)
static void camcam_preprocess_prev_frame_weapon_es(const ecs::UpdateStageInfoAct &, bool &camcam__lens_only_zoom_enabled)
{
  const bool hasCamCam = renderer_has_feature(CAMERA_IN_CAMERA);
  const AimRenderingData aimData = get_aim_rendering_data();

  const bool hasWeaponWithScopeInHands = static_cast<bool>(aimData.entityWithScopeLensEid);
  camcam__lens_only_zoom_enabled = hasCamCam && hasWeaponWithScopeInHands;
}

ECS_TAG(render)
ECS_BEFORE(camera_update_lods_scaling_es)
ECS_AFTER(update_shooter_camera_aim_parameters_es)
static void camcam_activate_view_es(const ecs::UpdateStageInfoAct &) { camera_in_camera::activate_view(); }
