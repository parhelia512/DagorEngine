// Built with ECS codegen version 1.0
#include <daECS/core/entitySystem.h>
#include <daECS/core/componentTypes.h>
#include "animCharConsoleUtilsES.cpp.inl"
ECS_DEF_PULL_VAR(animCharConsoleUtils);
#include <daECS/core/internal/performQuery.h>
static constexpr ecs::ComponentDesc count_animchar_renderer_ecs_query_comps[] =
{
//start of 2 ro components at [0]
  {ECS_HASH("animchar__res"), ecs::ComponentTypeInfo<ecs::string>()},
  {ECS_HASH("animchar_visbits"), ecs::ComponentTypeInfo<animchar_visbits_t>()}
};
static ecs::CompileTimeQueryDesc count_animchar_renderer_ecs_query_desc
(
  "count_animchar_renderer_ecs_query",
  empty_span(),
  make_span(count_animchar_renderer_ecs_query_comps+0, 2)/*ro*/,
  empty_span(),
  empty_span());
template<typename Callable>
inline void count_animchar_renderer_ecs_query(ecs::EntityManager &manager, Callable function)
{
  perform_query(&manager, count_animchar_renderer_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        auto comp = components.begin(), compE = components.end(); G_ASSERT(comp != compE); do
        {
          function(
              ECS_RO_COMP(count_animchar_renderer_ecs_query_comps, "animchar__res", ecs::string)
            , ECS_RO_COMP(count_animchar_renderer_ecs_query_comps, "animchar_visbits", animchar_visbits_t)
            );

        }while (++comp != compE);
    }
  );
}
static constexpr ecs::ComponentDesc gather_animchar_renderer_ecs_query_comps[] =
{
//start of 2 ro components at [0]
  {ECS_HASH("animchar_render"), ecs::ComponentTypeInfo<AnimV20::AnimcharRendComponent>()},
  {ECS_HASH("animchar__res"), ecs::ComponentTypeInfo<ecs::string>()}
};
static ecs::CompileTimeQueryDesc gather_animchar_renderer_ecs_query_desc
(
  "gather_animchar_renderer_ecs_query",
  empty_span(),
  make_span(gather_animchar_renderer_ecs_query_comps+0, 2)/*ro*/,
  empty_span(),
  empty_span());
template<typename Callable>
inline void gather_animchar_renderer_ecs_query(ecs::EntityManager &manager, Callable function)
{
  perform_query(&manager, gather_animchar_renderer_ecs_query_desc.getHandle(),
    [&function](const ecs::QueryView& __restrict components)
    {
        auto comp = components.begin(), compE = components.end(); G_ASSERT(comp != compE); do
        {
          function(
              ECS_RO_COMP(gather_animchar_renderer_ecs_query_comps, "animchar_render", AnimV20::AnimcharRendComponent)
            , ECS_RO_COMP(gather_animchar_renderer_ecs_query_comps, "animchar__res", ecs::string)
            );

        }while (++comp != compE);
    }
  );
}
