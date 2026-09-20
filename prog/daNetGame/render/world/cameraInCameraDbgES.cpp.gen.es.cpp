// Built with ECS codegen version 1.0
#include <daECS/core/entitySystem.h>
#include <daECS/core/componentTypes.h>
#include "cameraInCameraDbgES.cpp.inl"
ECS_DEF_PULL_VAR(cameraInCameraDbg);
#include <daECS/core/internal/performQuery.h>
static constexpr ecs::ComponentDesc camcam_debug_view_nodes_es_comps[] =
{
//start of 1 rq components at [0]
  {ECS_HASH("camcam_debug_nodes_registrator"), ecs::ComponentTypeInfo<ecs::Tag>()}
};
static void camcam_debug_view_nodes_es_all_events(const ecs::Event &__restrict evt, const ecs::QueryView &__restrict components)
{
  G_UNUSED(components);
  G_FAST_ASSERT(evt.is<OnCameraMainViewNodeConstruction>());
  camcam_debug_view_nodes_es(static_cast<const OnCameraMainViewNodeConstruction&>(evt)
        );
}
static ecs::EntitySystemDesc camcam_debug_view_nodes_es_es_desc
(
  "camcam_debug_view_nodes_es",
  "prog/daNetGame/render/world/cameraInCameraDbgES.cpp.inl",
  ecs::EntitySystemOps(nullptr, camcam_debug_view_nodes_es_all_events),
  empty_span(),
  empty_span(),
  make_span(camcam_debug_view_nodes_es_comps+0, 1)/*rq*/,
  empty_span(),
  ecs::EventSetBuilder<OnCameraMainViewNodeConstruction>::build(),
  0
,"render");
