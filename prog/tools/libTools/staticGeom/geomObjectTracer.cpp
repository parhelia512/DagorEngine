// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <libTools/staticGeom/geomObject.h>
#include <libTools/staticGeom/matFlags.h>
#include <libTools/staticGeom/staticGeometryContainer.h>
#include <sceneRay/dag_sceneRay.h>
#include <math/dag_rayIntersectSphere.h>
// #include <debug/dag_debug.h>

class GeomObject::SceneRayTracer
{
public:
  DAG_DECLARE_NEW(midmem)

  StaticSceneRayTracer *tracer;
  bool hasCollision;
  SceneRayTracer() : tracer(NULL), hasCollision(false) {}
  void clear() { destroy_it(tracer); }
  ~SceneRayTracer() { clear(); }
};

StaticSceneRayTracer *GeomObject::getRayTracer()
{
  if (!rayTracer)
    initRayTracer();
  return rayTracer->tracer;
}

void GeomObject::delRayTracer() { del_it(rayTracer); }


bool GeomObject::reloadRayTracer()
{
  delRayTracer();
  return (bool)getRayTracer();
}


void GeomObject::initRayTracer()
{
  delRayTracer();
  if (!geom)
    return;

  rayTracer = new GeomObject::SceneRayTracer;

  int totalFCount = 0, totalVCount = 0;
  for (int j = 0; j < geom->nodes.size(); ++j)
  {
    G_ASSERT(geom->nodes[j] && geom->nodes[j]->mesh);

    //(geom->nodes[j]->checkBillboard()) ||
    if (!geom->nodes[j]->isNonTopLod())
    {
      totalVCount += geom->nodes[j]->mesh->mesh.getVert().size();
      totalFCount += geom->nodes[j]->mesh->mesh.getFace().size();
    }
  }

  // target heuristic: 20 faces per XZ cell
  BBox3 bbox = getBoundBox(true);
  if (bbox.isempty() || !totalFCount)
    return;

  Point3 cellSize = bbox.width() / 8;
  float xz_scale = sqrtf(64.0f * 20.0f / float(totalFCount + 1));
  if (xz_scale > 8)
    xz_scale = 8;
  cellSize.x *= xz_scale;
  cellSize.z *= xz_scale;
  cellSize.y *= 2;

  if (cellSize.x < 1)
    cellSize.x = 1;
  if (cellSize.y < 1)
    cellSize.y = 1;
  if (cellSize.z < 1)
    cellSize.z = 1;

  BuildableStaticSceneRayTracer *tracer = ::create_buildable_staticmeshscene_raytracer(cellSize, 7);

  tracer->reserve(totalFCount, totalVCount);
  Tab<Point3> vert(tmpmem);
  Tab<unsigned> flags(tmpmem);

  bool hasCollision = false;

  for (int j = 0; j < geom->nodes.size(); ++j)
  {
    //(geom->nodes[j]->checkBillboard()) ||
    if (geom->nodes[j]->isNonTopLod())
      continue;
    vert.resize(geom->nodes[j]->mesh->mesh.getVert().size());
    for (int vi = 0; vi < vert.size(); ++vi)
      vert[vi] = geom->nodes[j]->wtm * geom->nodes[j]->mesh->mesh.getVert()[vi];
    flags.resize(geom->nodes[j]->mesh->mesh.getFace().size());
    for (int fi = 0; fi < geom->nodes[j]->mesh->mesh.getFace().size(); ++fi)
    {
      flags[fi] = StaticSceneRayTracer::CULL_CCW;
      int mat = geom->nodes[j]->mesh->mesh.getFaceMaterial(fi);
      if (mat >= geom->nodes[j]->mesh->mats.size())
        mat = geom->nodes[j]->mesh->mats.size() - 1;
      StaticGeometryMaterial *material = geom->nodes[j]->mesh->mats[mat];
      if (!material)
        continue;

      if (material->flags & MatFlags::FLG_2SIDED)
        flags[fi] |= StaticSceneRayTracer::CULL_BOTH;
      if ((geom->nodes[j]->flags & StaticGeometryNode::FLG_COLLIDABLE))
      {
        flags[fi] |= StaticSceneRayTracer::USER_FLAG3;
        hasCollision = true;
      }
    }

    tracer->addmesh(&vert[0], vert.size(), (const unsigned int *)&geom->nodes[j]->mesh->mesh.getFace()[0].v[0],
      elem_size(geom->nodes[j]->mesh->mesh.getFace()), geom->nodes[j]->mesh->mesh.getFace().size(), &flags[0], false);
  }

  tracer->rebuild();

  rayTracer->tracer = tracer;
  rayTracer->hasCollision = hasCollision;
}

bool GeomObject::hasCollision()
{
  if (!geom)
    return false;
  StaticSceneRayTracer *tracer = getRayTracer();
  if (!tracer || !rayTracer->hasCollision)
    return false;

  return true;
}

bool GeomObject::traceRay(const Point3 &p, const Point3 &dir, real &maxt, Point3 *norm)
{
  if (!geom)
    return false;
  StaticSceneRayTracer *tracer = getRayTracer();
  if (!tracer || !rayTracer->hasCollision)
    return false;
  tracer->setSkipFlagMask(StaticSceneRayTracer::USER_INVISIBLE);
  tracer->setUseFlagMask(StaticSceneRayTracer::USER_FLAG3);
  tracer->setCullFlags(0);
  int fi;
  if ((fi = tracer->traceray(p, dir, maxt)) >= 0)
  {
    if (norm)
      *norm = tracer->facebounds(fi).n;
    return true;
  }
  return false;
}
