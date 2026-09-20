// Built with ECS codegen version 1.0
#include <daECS/core/entitySystem.h>
#include <daECS/core/componentTypes.h>
#include "cameraInCameraCommonES.cpp.inl"
ECS_DEF_PULL_VAR(cameraInCameraCommon);
#include <daECS/core/internal/performQuery.h>
static constexpr ecs::ComponentDesc check_if_frame_after_deactivation_ecs_query_comps[] =
{
//start of 1 ro components at [0]
  {ECS_HASH("camcam__frame_after_deactivation"), ecs::ComponentTypeInfo<bool>()}
};
static ecs::CompileTimeQueryDesc check_if_frame_after_deactivation_ecs_query_desc
(
  "check_if_frame_after_deactivation_ecs_query",
  empty_span(),
  make_span(check_if_frame_after_deactivation_ecs_query_comps+0, 1)/*ro*/,
  empty_span(),
  empty_span());
template<typename Callable>
inline void check_if_frame_after_deactivation_ecs_query(ecs::EntityManager &manager, Callable function)
{
  perform_query(&manager, check_if_frame_after_deactivation_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        auto comp = components.begin(), compE = components.end(); G_ASSERT(comp != compE); do
        {
          function(
              ECS_RO_COMP(check_if_frame_after_deactivation_ecs_query_comps, "camcam__frame_after_deactivation", bool)
            );

        }while (++comp != compE);
    }
  );
}
static constexpr ecs::ComponentDesc get_camcam_frame_number_ecs_query_comps[] =
{
//start of 1 ro components at [0]
  {ECS_HASH("camcam__iFrame"), ecs::ComponentTypeInfo<int>()}
};
static ecs::CompileTimeQueryDesc get_camcam_frame_number_ecs_query_desc
(
  "get_camcam_frame_number_ecs_query",
  empty_span(),
  make_span(get_camcam_frame_number_ecs_query_comps+0, 1)/*ro*/,
  empty_span(),
  empty_span());
template<typename Callable>
inline void get_camcam_frame_number_ecs_query(ecs::EntityManager &manager, Callable function)
{
  perform_query(&manager, get_camcam_frame_number_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        auto comp = components.begin(), compE = components.end(); G_ASSERT(comp != compE); do
        {
          function(
              ECS_RO_COMP(get_camcam_frame_number_ecs_query_comps, "camcam__iFrame", int)
            );

        }while (++comp != compE);
    }
  );
}
static constexpr ecs::ComponentDesc update_camcam_state_ecs_query_comps[] =
{
//start of 3 rw components at [0]
  {ECS_HASH("camcam__lens_render_active"), ecs::ComponentTypeInfo<bool>()},
  {ECS_HASH("camcam__iFrame"), ecs::ComponentTypeInfo<int>()},
  {ECS_HASH("camcam__frame_after_deactivation"), ecs::ComponentTypeInfo<bool>()},
//start of 1 ro components at [3]
  {ECS_HASH("camcam__lens_only_zoom_enabled"), ecs::ComponentTypeInfo<bool>()}
};
static ecs::CompileTimeQueryDesc update_camcam_state_ecs_query_desc
(
  "update_camcam_state_ecs_query",
  make_span(update_camcam_state_ecs_query_comps+0, 3)/*rw*/,
  make_span(update_camcam_state_ecs_query_comps+3, 1)/*ro*/,
  empty_span(),
  empty_span());
template<typename Callable>
inline void update_camcam_state_ecs_query(ecs::EntityManager &manager, Callable function)
{
  perform_query(&manager, update_camcam_state_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        auto comp = components.begin(), compE = components.end(); G_ASSERT(comp != compE); do
        {
          function(
              ECS_RW_COMP(update_camcam_state_ecs_query_comps, "camcam__lens_render_active", bool)
            , ECS_RW_COMP(update_camcam_state_ecs_query_comps, "camcam__iFrame", int)
            , ECS_RO_COMP(update_camcam_state_ecs_query_comps, "camcam__lens_only_zoom_enabled", bool)
            , ECS_RW_COMP(update_camcam_state_ecs_query_comps, "camcam__frame_after_deactivation", bool)
            );

        }while (++comp != compE);
    }
  );
}
static constexpr ecs::ComponentDesc get_camcam_render_state_ecs_query_comps[] =
{
//start of 1 ro components at [0]
  {ECS_HASH("camcam__lens_render_active"), ecs::ComponentTypeInfo<bool>()}
};
static ecs::CompileTimeQueryDesc get_camcam_render_state_ecs_query_desc
(
  "get_camcam_render_state_ecs_query",
  empty_span(),
  make_span(get_camcam_render_state_ecs_query_comps+0, 1)/*ro*/,
  empty_span(),
  empty_span());
template<typename Callable>
inline void get_camcam_render_state_ecs_query(ecs::EntityManager &manager, Callable function)
{
  perform_query(&manager, get_camcam_render_state_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        auto comp = components.begin(), compE = components.end(); G_ASSERT(comp != compE); do
        {
          function(
              ECS_RO_COMP(get_camcam_render_state_ecs_query_comps, "camcam__lens_render_active", bool)
            );

        }while (++comp != compE);
    }
  );
}
static constexpr ecs::ComponentDesc get_camcam_lens_only_zoom_state_ecs_query_comps[] =
{
//start of 1 ro components at [0]
  {ECS_HASH("camcam__lens_only_zoom_enabled"), ecs::ComponentTypeInfo<bool>()}
};
static ecs::CompileTimeQueryDesc get_camcam_lens_only_zoom_state_ecs_query_desc
(
  "get_camcam_lens_only_zoom_state_ecs_query",
  empty_span(),
  make_span(get_camcam_lens_only_zoom_state_ecs_query_comps+0, 1)/*ro*/,
  empty_span(),
  empty_span());
template<typename Callable>
inline void get_camcam_lens_only_zoom_state_ecs_query(ecs::EntityManager &manager, Callable function)
{
  perform_query(&manager, get_camcam_lens_only_zoom_state_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        auto comp = components.begin(), compE = components.end(); G_ASSERT(comp != compE); do
        {
          function(
              ECS_RO_COMP(get_camcam_lens_only_zoom_state_ecs_query_comps, "camcam__lens_only_zoom_enabled", bool)
            );

        }while (++comp != compE);
    }
  );
}
static constexpr ecs::ComponentDesc get_camcam_viewport_scissor_ecs_query_comps[] =
{
//start of 1 ro components at [0]
  {ECS_HASH("camcam__lens_viewport_uv"), ecs::ComponentTypeInfo<Point4>()}
};
static ecs::CompileTimeQueryDesc get_camcam_viewport_scissor_ecs_query_desc
(
  "get_camcam_viewport_scissor_ecs_query",
  empty_span(),
  make_span(get_camcam_viewport_scissor_ecs_query_comps+0, 1)/*ro*/,
  empty_span(),
  empty_span());
template<typename Callable>
inline void get_camcam_viewport_scissor_ecs_query(ecs::EntityManager &manager, Callable function)
{
  perform_query(&manager, get_camcam_viewport_scissor_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        auto comp = components.begin(), compE = components.end(); G_ASSERT(comp != compE); do
        {
          function(
              ECS_RO_COMP(get_camcam_viewport_scissor_ecs_query_comps, "camcam__lens_viewport_uv", Point4)
            );

        }while (++comp != compE);
    }
  );
}
static constexpr ecs::ComponentDesc update_camcam_transforms_ecs_query_comps[] =
{
//start of 2 rw components at [0]
  {ECS_HASH("camcam__uv_remapping"), ecs::ComponentTypeInfo<Point4>()},
  {ECS_HASH("camcam__lens_viewport_uv"), ecs::ComponentTypeInfo<Point4>()}
};
static ecs::CompileTimeQueryDesc update_camcam_transforms_ecs_query_desc
(
  "update_camcam_transforms_ecs_query",
  make_span(update_camcam_transforms_ecs_query_comps+0, 2)/*rw*/,
  empty_span(),
  empty_span(),
  empty_span());
template<typename Callable>
inline void update_camcam_transforms_ecs_query(ecs::EntityManager &manager, Callable function)
{
  perform_query(&manager, update_camcam_transforms_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        auto comp = components.begin(), compE = components.end(); G_ASSERT(comp != compE); do
        {
          function(
              ECS_RW_COMP(update_camcam_transforms_ecs_query_comps, "camcam__uv_remapping", Point4)
            , ECS_RW_COMP(update_camcam_transforms_ecs_query_comps, "camcam__lens_viewport_uv", Point4)
            );

        }while (++comp != compE);
    }
  );
}
static constexpr ecs::ComponentDesc get_scope_lens_node_wtm_ecs_query_comps[] =
{
//start of 2 ro components at [0]
  {ECS_HASH("animchar_render"), ecs::ComponentTypeInfo<AnimV20::AnimcharRendComponent>()},
  {ECS_HASH("animchar_node_wtm"), ecs::ComponentTypeInfo<AnimcharNodesMat44>()}
};
static ecs::CompileTimeQueryDesc get_scope_lens_node_wtm_ecs_query_desc
(
  "get_scope_lens_node_wtm_ecs_query",
  empty_span(),
  make_span(get_scope_lens_node_wtm_ecs_query_comps+0, 2)/*ro*/,
  empty_span(),
  empty_span());
template<typename Callable>
inline void get_scope_lens_node_wtm_ecs_query(ecs::EntityManager &manager, ecs::EntityId eid, Callable function)
{
  perform_query(&manager, eid, get_scope_lens_node_wtm_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        constexpr size_t comp = 0;
        {
          function(
              ECS_RO_COMP(get_scope_lens_node_wtm_ecs_query_comps, "animchar_render", AnimV20::AnimcharRendComponent)
            , ECS_RO_COMP(get_scope_lens_node_wtm_ecs_query_comps, "animchar_node_wtm", AnimcharNodesMat44)
            );

        }
    }
  );
}
static constexpr ecs::ComponentDesc get_camcam_uv_remapping_ecs_query_comps[] =
{
//start of 1 ro components at [0]
  {ECS_HASH("camcam__uv_remapping"), ecs::ComponentTypeInfo<Point4>()}
};
static ecs::CompileTimeQueryDesc get_camcam_uv_remapping_ecs_query_desc
(
  "get_camcam_uv_remapping_ecs_query",
  empty_span(),
  make_span(get_camcam_uv_remapping_ecs_query_comps+0, 1)/*ro*/,
  empty_span(),
  empty_span());
template<typename Callable>
inline void get_camcam_uv_remapping_ecs_query(ecs::EntityManager &manager, Callable function)
{
  perform_query(&manager, get_camcam_uv_remapping_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        auto comp = components.begin(), compE = components.end(); G_ASSERT(comp != compE); do
        {
          function(
              ECS_RO_COMP(get_camcam_uv_remapping_ecs_query_comps, "camcam__uv_remapping", Point4)
            );

        }while (++comp != compE);
    }
  );
}
