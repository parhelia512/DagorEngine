// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <daECS/core/entityManager.h>
#include <daECS/core/entitySystem.h>
#include <render/daFrameGraph/daFG.h>
#include <render/daFrameGraph/ecs/frameGraphNode.h>

#include <render/renderEvent.h>
#include <render/cameraInCamera/cameraInCamera.h>
#include <render/cameraInCamera/cameraInCameraNodes.h>
#include <render/cameraParams.h>
#include <render/world/frameGraphNodes/frameGraphNodes.h>

#define INSIDE_RENDERER 1
#include <render/world/private_worldRenderer.h>

dafg::NodeHandle makeLensAreaCameraSourceNode()
{
  return dafg::register_node("lens_area_camera_source_node", DAFG_PP_NODE_SRC, [](dafg::Registry registry) {
    auto srcHndl = registry.createBlob<CameraParams>(camera_in_camera::LENS_AREA_CAMERA_SOURCE_BLOB).handle();
    registry.multiplex(dafg::multiplexing::Mode::None);

    return [srcHndl]() {
      auto *wr = static_cast<WorldRenderer *>(get_world_renderer());
      if (!wr->camcamParams)
      {
        G_ASSERT(!camera_in_camera::is_lens_render_active());
        return;
      }
      srcHndl.ref() = *wr->camcamParams;
      srcHndl.ref().jobsMgr = &wr->camcamVisibilityMgr;
    };
  });
}

ECS_TAG(render)
ECS_ON_EVENT(OnCameraNodeConstruction)
static void create_camera_in_camera_setup_nodes_es(const OnCameraNodeConstruction &evt)
{
  if (!renderer_has_feature(CAMERA_IN_CAMERA))
    return;

  evt.nodes->push_back(makeLensAreaCameraSourceNode());
  for (auto &&n : camera_in_camera::make_camera_nodes())
    evt.nodes->push_back(eastl::move(n));
}
