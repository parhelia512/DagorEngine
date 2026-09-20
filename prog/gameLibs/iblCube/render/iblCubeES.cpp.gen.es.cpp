// Built with ECS codegen version 1.0
#include <daECS/core/entitySystem.h>
#include <daECS/core/componentTypes.h>
#include "iblCubeES.cpp.inl"
ECS_DEF_PULL_VAR(iblCube);
#include <daECS/core/internal/performQuery.h>
static constexpr ecs::ComponentDesc ibl_cube_created_es_comps[] =
{
//start of 1 rw components at [0]
  {ECS_HASH("ibl_cube__panorama"), ecs::ComponentTypeInfo<SharedTexWithShaderVar>()},
//start of 2 ro components at [1]
  {ECS_HASH("ibl_cube__panorama_res"), ecs::ComponentTypeInfo<ecs::string>()},
  {ECS_HASH("ibl_cube__panorama_var"), ecs::ComponentTypeInfo<ecs::string>()}
};
static void ibl_cube_created_es_all_events(const ecs::Event &__restrict evt, const ecs::QueryView &__restrict components)
{
  G_FAST_ASSERT(evt.is<ecs::EventEntityCreated>());
  auto comp = components.begin(), compE = components.end(); G_ASSERT(comp!=compE); do
    ibl_cube_created_es(static_cast<const ecs::EventEntityCreated&>(evt)
        , ECS_RW_COMP(ibl_cube_created_es_comps, "ibl_cube__panorama", SharedTexWithShaderVar)
    , ECS_RO_COMP(ibl_cube_created_es_comps, "ibl_cube__panorama_res", ecs::string)
    , ECS_RO_COMP(ibl_cube_created_es_comps, "ibl_cube__panorama_var", ecs::string)
    );
  while (++comp != compE);
}
static ecs::EntitySystemDesc ibl_cube_created_es_es_desc
(
  "ibl_cube_created_es",
  "prog/gameLibs/iblCube/render/iblCubeES.cpp.inl",
  ecs::EntitySystemOps(nullptr, ibl_cube_created_es_all_events),
  make_span(ibl_cube_created_es_comps+0, 1)/*rw*/,
  make_span(ibl_cube_created_es_comps+1, 2)/*ro*/,
  empty_span(),
  empty_span(),
  ecs::EventSetBuilder<ecs::EventEntityCreated>::build(),
  0
,"render");
static constexpr ecs::ComponentDesc ibl_cube_panorama_changed_es_comps[] =
{
//start of 1 rw components at [0]
  {ECS_HASH("ibl_cube__panorama"), ecs::ComponentTypeInfo<SharedTexWithShaderVar>()},
//start of 2 ro components at [1]
  {ECS_HASH("ibl_cube__panorama_res"), ecs::ComponentTypeInfo<ecs::string>()},
  {ECS_HASH("ibl_cube__panorama_var"), ecs::ComponentTypeInfo<ecs::string>()}
};
static void ibl_cube_panorama_changed_es_all_events(const ecs::Event &__restrict evt, const ecs::QueryView &__restrict components)
{
  auto comp = components.begin(), compE = components.end(); G_ASSERT(comp!=compE); do
    ibl_cube_panorama_changed_es(evt
        , ECS_RW_COMP(ibl_cube_panorama_changed_es_comps, "ibl_cube__panorama", SharedTexWithShaderVar)
    , ECS_RO_COMP(ibl_cube_panorama_changed_es_comps, "ibl_cube__panorama_res", ecs::string)
    , ECS_RO_COMP(ibl_cube_panorama_changed_es_comps, "ibl_cube__panorama_var", ecs::string)
    );
  while (++comp != compE);
}
static ecs::EntitySystemDesc ibl_cube_panorama_changed_es_es_desc
(
  "ibl_cube_panorama_changed_es",
  "prog/gameLibs/iblCube/render/iblCubeES.cpp.inl",
  ecs::EntitySystemOps(nullptr, ibl_cube_panorama_changed_es_all_events),
  make_span(ibl_cube_panorama_changed_es_comps+0, 1)/*rw*/,
  make_span(ibl_cube_panorama_changed_es_comps+1, 2)/*ro*/,
  empty_span(),
  empty_span(),
  ecs::EventSetBuilder<>::build(),
  0
,"render","ibl_cube__panorama_res");
static constexpr ecs::ComponentDesc ibl_cube_destroyed_es_comps[] =
{
//start of 1 ro components at [0]
  {ECS_HASH("ibl_cube__panorama_res"), ecs::ComponentTypeInfo<ecs::string>()}
};
static void ibl_cube_destroyed_es_all_events(const ecs::Event &__restrict evt, const ecs::QueryView &__restrict components)
{
  G_FAST_ASSERT(evt.is<ecs::EventEntityDestroyed>());
  auto comp = components.begin(), compE = components.end(); G_ASSERT(comp!=compE); do
    ibl_cube_destroyed_es(static_cast<const ecs::EventEntityDestroyed&>(evt)
        , ECS_RO_COMP(ibl_cube_destroyed_es_comps, "ibl_cube__panorama_res", ecs::string)
    );
  while (++comp != compE);
}
static ecs::EntitySystemDesc ibl_cube_destroyed_es_es_desc
(
  "ibl_cube_destroyed_es",
  "prog/gameLibs/iblCube/render/iblCubeES.cpp.inl",
  ecs::EntitySystemOps(nullptr, ibl_cube_destroyed_es_all_events),
  empty_span(),
  make_span(ibl_cube_destroyed_es_comps+0, 1)/*ro*/,
  empty_span(),
  empty_span(),
  ecs::EventSetBuilder<ecs::EventEntityDestroyed>::build(),
  0
,"render");
//static constexpr ecs::ComponentDesc ibl_cube_after_reset_es_comps[] ={};
static void ibl_cube_after_reset_es_all_events(const ecs::Event &__restrict evt, const ecs::QueryView &__restrict components)
{
  G_UNUSED(components);
  G_FAST_ASSERT(evt.is<EventAfterDeviceReset>());
  ibl_cube_after_reset_es(static_cast<const EventAfterDeviceReset&>(evt)
        );
}
static ecs::EntitySystemDesc ibl_cube_after_reset_es_es_desc
(
  "ibl_cube_after_reset_es",
  "prog/gameLibs/iblCube/render/iblCubeES.cpp.inl",
  ecs::EntitySystemOps(nullptr, ibl_cube_after_reset_es_all_events),
  empty_span(),
  empty_span(),
  empty_span(),
  empty_span(),
  ecs::EventSetBuilder<EventAfterDeviceReset>::build(),
  0
,"render");
//static constexpr ecs::ComponentDesc ibl_cube_reset_es_comps[] ={};
static void ibl_cube_reset_es_all_events(const ecs::Event &__restrict evt, const ecs::QueryView &__restrict components)
{
  G_UNUSED(components);
  G_FAST_ASSERT(evt.is<UpdateStageInfoBeforeRender>());
  ibl_cube_reset_es(static_cast<const UpdateStageInfoBeforeRender&>(evt)
        );
}
static ecs::EntitySystemDesc ibl_cube_reset_es_es_desc
(
  "ibl_cube_reset_es",
  "prog/gameLibs/iblCube/render/iblCubeES.cpp.inl",
  ecs::EntitySystemOps(nullptr, ibl_cube_reset_es_all_events),
  empty_span(),
  empty_span(),
  empty_span(),
  empty_span(),
  ecs::EventSetBuilder<UpdateStageInfoBeforeRender>::build(),
  0
,"render",nullptr,"ibl_cube_before_render_es","animchar_before_render_es");
static constexpr ecs::ComponentDesc ibl_cube_update_is_baked_es_comps[] =
{
//start of 1 rw components at [0]
  {ECS_HASH("ibl_cube__is_baked"), ecs::ComponentTypeInfo<bool>()},
//start of 1 ro components at [1]
  {ECS_HASH("ibl_cube__panorama_res"), ecs::ComponentTypeInfo<ecs::string>()}
};
static void ibl_cube_update_is_baked_es_all_events(const ecs::Event &__restrict evt, const ecs::QueryView &__restrict components)
{
  G_FAST_ASSERT(evt.is<UpdateStageInfoBeforeRender>());
  auto comp = components.begin(), compE = components.end(); G_ASSERT(comp!=compE); do
    ibl_cube_update_is_baked_es(static_cast<const UpdateStageInfoBeforeRender&>(evt)
        , ECS_RO_COMP(ibl_cube_update_is_baked_es_comps, "ibl_cube__panorama_res", ecs::string)
    , ECS_RW_COMP(ibl_cube_update_is_baked_es_comps, "ibl_cube__is_baked", bool)
    );
  while (++comp != compE);
}
static ecs::EntitySystemDesc ibl_cube_update_is_baked_es_es_desc
(
  "ibl_cube_update_is_baked_es",
  "prog/gameLibs/iblCube/render/iblCubeES.cpp.inl",
  ecs::EntitySystemOps(nullptr, ibl_cube_update_is_baked_es_all_events),
  make_span(ibl_cube_update_is_baked_es_comps+0, 1)/*rw*/,
  make_span(ibl_cube_update_is_baked_es_comps+1, 1)/*ro*/,
  empty_span(),
  empty_span(),
  ecs::EventSetBuilder<UpdateStageInfoBeforeRender>::build(),
  0
,"render",nullptr,nullptr,"ibl_cube_before_render_es");
static constexpr ecs::ComponentDesc ibl_cube_before_render_es_comps[] =
{
//start of 9 ro components at [0]
  {ECS_HASH("transform"), ecs::ComponentTypeInfo<TMatrix>()},
  {ECS_HASH("ibl_cube__mul_specular"), ecs::ComponentTypeInfo<float>()},
  {ECS_HASH("ibl_cube__mul_diffuse"), ecs::ComponentTypeInfo<float>()},
  {ECS_HASH("ibl_cube__mul_specular__falloff_end"), ecs::ComponentTypeInfo<float>()},
  {ECS_HASH("ibl_cube__mul_diffuse__falloff_end"), ecs::ComponentTypeInfo<float>()},
  {ECS_HASH("ibl_cube__falloff_start"), ecs::ComponentTypeInfo<float>()},
  {ECS_HASH("ibl_cube__falloff_end"), ecs::ComponentTypeInfo<float>()},
  {ECS_HASH("ibl_cube__panorama_res"), ecs::ComponentTypeInfo<ecs::string>()},
  {ECS_HASH("ibl_cube__panorama"), ecs::ComponentTypeInfo<SharedTexWithShaderVar>()}
};
static void ibl_cube_before_render_es_all_events(const ecs::Event &__restrict evt, const ecs::QueryView &__restrict components)
{
  G_FAST_ASSERT(evt.is<UpdateStageInfoBeforeRender>());
  auto comp = components.begin(), compE = components.end(); G_ASSERT(comp!=compE); do
    ibl_cube_before_render_es(static_cast<const UpdateStageInfoBeforeRender&>(evt)
        , ECS_RO_COMP(ibl_cube_before_render_es_comps, "transform", TMatrix)
    , ECS_RO_COMP(ibl_cube_before_render_es_comps, "ibl_cube__mul_specular", float)
    , ECS_RO_COMP(ibl_cube_before_render_es_comps, "ibl_cube__mul_diffuse", float)
    , ECS_RO_COMP(ibl_cube_before_render_es_comps, "ibl_cube__mul_specular__falloff_end", float)
    , ECS_RO_COMP(ibl_cube_before_render_es_comps, "ibl_cube__mul_diffuse__falloff_end", float)
    , ECS_RO_COMP(ibl_cube_before_render_es_comps, "ibl_cube__falloff_start", float)
    , ECS_RO_COMP(ibl_cube_before_render_es_comps, "ibl_cube__falloff_end", float)
    , ECS_RO_COMP(ibl_cube_before_render_es_comps, "ibl_cube__panorama_res", ecs::string)
    , ECS_RO_COMP(ibl_cube_before_render_es_comps, "ibl_cube__panorama", SharedTexWithShaderVar)
    );
  while (++comp != compE);
}
static ecs::EntitySystemDesc ibl_cube_before_render_es_es_desc
(
  "ibl_cube_before_render_es",
  "prog/gameLibs/iblCube/render/iblCubeES.cpp.inl",
  ecs::EntitySystemOps(nullptr, ibl_cube_before_render_es_all_events),
  empty_span(),
  make_span(ibl_cube_before_render_es_comps+0, 9)/*ro*/,
  empty_span(),
  empty_span(),
  ecs::EventSetBuilder<UpdateStageInfoBeforeRender>::build(),
  0
,"render",nullptr,nullptr,"animchar_before_render_es");
