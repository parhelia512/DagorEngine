// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <gameRes/collisionResourceBuilder.h>
#include <daECS/core/entityManager.h>
#include <daECS/core/entitySystem.h>
#include <daECS/core/componentTypes.h>
#include <scene/dag_physMat.h>

// The primitive nodes of a collres desc appended to the builder, then the resource built from it
// (a desc_add builds a new resource next to the entity's copy). The block is midmem, as the owner
// destroys it.
CollisionResource *build_collres_from_ecs_object(CollisionResourceBuilder &builder, const ecs::Array &desc, const char *res_name)
{
  for (const auto &nodeIt : desc)
  {
    const ecs::Object &nodeDesc = nodeIt.get<ecs::Object>();
    const ecs::string &name = nodeDesc[ECS_HASH("name")].get<ecs::string>();
    const ecs::string *material = nodeDesc[ECS_HASH("physMatId")].getNullable<ecs::string>();
    int16_t matId = material ? PhysMat::getMaterialId(material->c_str()) : PHYSMAT_INVALID;
    int nodeId = -1;
    if (const Point4 *bsph = nodeDesc[ECS_HASH("bsph")].getNullable<Point4>())
    {
      nodeId = builder.addSphereNode(name.c_str(), matId, BSphere3(Point3::xyz(*bsph), bsph->w));
    }
    else if (const Point3 *bmin = nodeDesc[ECS_HASH("bbox_min")].getNullable<Point3>())
    {
      const Point3 *bmax = nodeDesc[ECS_HASH("bbox_max")].getNullable<Point3>();
      if (bmax)
        nodeId = builder.addBoxNode(name.c_str(), matId, BBox3(*bmin, *bmax));
    }
    if (nodeId < 0)
    {
      const Point3 *p0 = nodeDesc[ECS_HASH("capsule_p0")].getNullable<Point3>();
      const Point3 *p1 = nodeDesc[ECS_HASH("capsule_p1")].getNullable<Point3>();
      const float *r = nodeDesc[ECS_HASH("capsule_r")].getNullable<float>();
      if (p0 && p1 && r)
        nodeId = builder.addCapsuleNode(name.c_str(), matId, *p0, *p1, *r);
    }
    if (nodeId < 0)
    {
      logerr("Can't detect node <%s> type in collres desc", name.c_str());
      break;
    }
  }
  builder.recomputeBounds();
  builder.sortNodes();
  return builder.build(res_name, midmem->alloc(sizeof(CollisionResource))); // the decline diagnostics name the template
}

static CollisionResource *create_collres_from_ecs_object(const ecs::Array &desc, const char *res_name)
{
  CollisionResourceBuilder builder;
  return build_collres_from_ecs_object(builder, desc, res_name);
}

CollisionResource *create_collres_from_ecs_object(ecs::EntityManager &mgr, ecs::EntityId eid)
{
  const ecs::Array *desc = mgr.getNullable<ecs::Array>(eid, ECS_HASH("collres__desc"));
  return desc ? create_collres_from_ecs_object(*desc, mgr.getEntityTemplateName(eid)) : nullptr;
}
