// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <render/cameraInCamera/cameraInCamera.h>

#include <daECS/core/componentTypes.h>
#include <daECS/core/entityManager.h>
#include <daECS/core/entitySystem.h>
#include <render/daFrameGraph/daFG.h>
#include <render/renderEvent.h>
#include <render/world/frameGraphHelpers.h>
#include <shaders/dag_postFxRenderer.h>
#include <util/dag_console.h>


ECS_TAG(render)
ECS_ON_EVENT(OnCameraMainViewNodeConstruction)
ECS_REQUIRE(ecs::Tag camcam_debug_nodes_registrator)
static void camcam_debug_view_nodes_es(const OnCameraMainViewNodeConstruction &evt)
{
  evt.nodes->push_back(dafg::root().registerNode("camcam_debug_ellipse_node", DAFG_PP_NODE_SRC, [](dafg::Registry registry) {
    registry.orderMeAfter("post_fx_node");
    registry.requestRenderPass().color({registry.modifyTexture("postfxed_frame")});
    registry.multiplex(dafg::multiplexing::Mode::FullMultiplex);
    auto cameraHndl = read_camera_in_camera(registry).handle();

    return [cameraHndl, renderer = PostFxRenderer("camcam_debug_ellipse")](const dafg::multiplexing::Index &multiplexing_index) {
      if (camera_in_camera::is_main_view(multiplexing_index))
        return;

      camera_in_camera::ApplyPostfxState camcam{multiplexing_index, cameraHndl.ref()};
      renderer.render();
    };
  }));
}

static bool camcam_console_handler(const char *argv[], int argc)
{
  int found = 0;
  CONSOLE_CHECK_NAME("camcam", "debug_ellipse", 1, 1)
  {
    const ecs::EntityId dbgEid = g_entity_mgr->getSingletonEntity(ECS_HASH("camera_in_camera_dbg_render"));
    console::print_d("camcam.debug_ellipse: %s", dbgEid ? "off" : "on");
    if (dbgEid)
      g_entity_mgr->destroyEntity(dbgEid);
    else
      g_entity_mgr->getOrCreateSingletonEntity(ECS_HASH("camera_in_camera_dbg_render"));
  }
  return found;
}

REGISTER_CONSOLE_HANDLER(camcam_console_handler);
