// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <ecs/anim/anim.h>
#include <shaders/dag_dynSceneRes.h>
#include <ioSys/dag_fileIo.h>
#include <osApiWrappers/dag_files.h>
#include <util/dag_console.h>
#include <EASTL/vector_map.h>
#include <daECS/core/entityManager.h>
#include <ecs/anim/animchar_visbits.h>

template <typename Callable>
inline void count_animchar_renderer_ecs_query(ecs::EntityManager &manager, Callable c);
template <typename Callable>
inline void gather_animchar_renderer_ecs_query(ecs::EntityManager &manager, Callable c);

static bool animchar_console_handler(const char *argv[], int argc)
{
  int found = 0;
  CONSOLE_CHECK_NAME("animchar", "verify_lods", 1, 2)
  {
    float fov = argc > 1 ? console::to_real(argv[1]) : 90.f;
    float tg = tan(fov / 180.f * PI * 0.5f);

    eastl::string cvs_dump = "name;count;rendered;bBox rad;"
                             "L0 dips;L1 dips;L2 dips;L3 dips;"
                             "L0 tris;L1 tris;L2 tris;L3 tris;"
                             "L0 dist;L1 dist;L2 dist;L3 dist;"
                             "L0 screen%;L1 screen%;L2 screen%;L3 screen%;"
                             "\n";
    eastl::vector_map<ecs::string, IPoint2> nameCountMap;

    count_animchar_renderer_ecs_query(*g_entity_mgr, [&](const ecs::string &animchar__res, animchar_visbits_t animchar_visbits) {
      IPoint2 &count_rendered = nameCountMap[animchar__res];
      count_rendered.x += 1;
      count_rendered.y +=
        (animchar_visbits & (VISFLG_MAIN_CAMERA_RENDERED | VISFLG_SEMI_TRANS_RENDERED | VISFLG_COCKPIT_VISIBLE)) != 0;
    });

    gather_animchar_renderer_ecs_query(*g_entity_mgr,
      [&](const AnimV20::AnimcharRendComponent &animchar_render, const ecs::string &animchar__res) {
        const DynamicRenderableSceneInstance *scene = animchar_render.getSceneInstance();
        auto lodsResources = scene ? scene->getLodsResource() : nullptr;
        if (!lodsResources)
          return;
        IPoint2 &count_rendered_ref = nameCountMap[animchar__res];
        IPoint2 count_rendered = count_rendered_ref;
        if (count_rendered.x == 0) // already processed animchars with that name
          return;
        count_rendered_ref.x = 0;
        Point3 bboxWidth = lodsResources->bbox.width();
        float maxBoxEdge = max(max(bboxWidth.x, bboxWidth.y), bboxWidth.z) * 0.5f; // half of edge like a radius

        cvs_dump += eastl::string(eastl::string::CtorSprintf{}, "%s;%d;%d;%f;", animchar__res.c_str(), count_rendered.x,
          count_rendered.y, maxBoxEdge);


        struct LodInfo
        {
          int totalTris, totalDrawcalls;
          int skinTris, skinDrawcalls, rigidsTris, rigidsDrawcalls;
          float lodDistance, screenPercent;
          LodInfo() = default;
        };
        constexpr int MAX_LODS = 4;
        eastl::fixed_vector<LodInfo, MAX_LODS> lodsInfo;


        for (const auto &lod : lodsResources->lods)
        {
          LodInfo info = LodInfo();
          for (const auto &skin : lod.scene->getSkins())
          {
            const auto &mesh = skin->getMesh()->getShaderMesh();
            info.skinTris += mesh.calcTotalFaces();
            info.skinDrawcalls += mesh.getAllElems().size();
          }
          for (const auto &rigid : lod.scene->getRigidsConst())
          {
            const auto &mesh = *rigid.mesh->getMesh();
            info.rigidsTris += mesh.calcTotalFaces();
            info.rigidsDrawcalls += mesh.getAllElems().size();
          }

          float lodDistance = lod.range;
          float sizeScale = 2.f / (tg * lodDistance);
          // float spherePartOfScreen = bSphereRad * sizeScale;
          float boxPartOfScreen = maxBoxEdge * sizeScale;
          info.totalTris = info.rigidsTris + info.skinTris;
          info.totalDrawcalls = info.rigidsDrawcalls + info.skinDrawcalls;
          info.screenPercent = boxPartOfScreen * 100;
          info.lodDistance = lodDistance;
          lodsInfo.emplace_back(info);
        }
#define DUMP(format, var_name)                  \
  for (uint32_t lod = 0; lod < MAX_LODS; lod++) \
    cvs_dump += lod < lodsInfo.size() ? eastl::string(eastl::string::CtorSprintf{}, format, lodsInfo[lod].var_name) : ";";
        DUMP("%d;", totalDrawcalls)
        DUMP("%d;", totalTris)
        DUMP("%.0f;", lodDistance)
        DUMP("%.1f;", screenPercent)
        cvs_dump += "\n";
      });
    FullFileSaveCB cb("animchar_profiling_statistic.csv", DF_WRITE | DF_CREATE);
    cb.write(cvs_dump.c_str(), cvs_dump.size() - 1);
  }
  return found;
}

REGISTER_CONSOLE_HANDLER(animchar_console_handler);
