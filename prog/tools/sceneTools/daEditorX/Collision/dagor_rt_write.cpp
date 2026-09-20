// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "collision_builder.h"

#include <de3_interface.h>
#include <libTools/staticGeom/staticGeometry.h>
#include <libTools/util/makeBindump.h>
#include <libTools/util/binDumpUtil.h>

#include <scene/dag_physMat.h>
#include <sceneRay/dag_sceneRay.h>
#include <gameRes/collisionResourceBuilder.h>
#include <gameRes/collResStream.h>
#include <ioSys/dag_dataBlock.h>
#include <ioSys/dag_chainedMemIo.h>
#include <ioSys/dag_zstdIo.h>
#include <ioSys/dag_btagCompr.h>

#include <osApiWrappers/dag_direct.h>
#include <util/dag_oaHashNameMap.h>

#include <debug/dag_debug.h>
#include <debug/dag_log.h>


// The gathered scene: geometry with a material per face, and no tracer until a writer needs one.
class DagorRayTracerBuilder : public ICollisionDumpBuilder
{
protected:
  dag::Vector<Point3_vec4> verts;
  dag::Vector<uint32_t> faces; // index triples into verts
  NameMap matName;
  Tab<unsigned char> matId; // one per face: its physmat's index in matName
  Mesh boxMesh;
  Point3 leafSize = Point3(1.f, 1.f, 1.f);
  int levels = 4;
  bool started = false;

public:
  DagorRayTracerBuilder() : matId(tmpmem)
  {
    supportMask = SUPPORT_BOX;

    boxMesh.vert.resize(8);
    mem_set_0(boxMesh.vert);

    boxMesh.face.resize(12);
    boxMesh.face[0].set(0, 1, 2, 0, 1);
    boxMesh.face[1].set(2, 3, 0, 0, 1);
    boxMesh.face[2].set(4, 5, 6, 0, 1);
    boxMesh.face[3].set(6, 7, 4, 0, 1);
    boxMesh.face[4].set(2, 6, 5, 0, 1);
    boxMesh.face[5].set(5, 3, 2, 0, 1);
    boxMesh.face[6].set(0, 3, 5, 0, 1);
    boxMesh.face[7].set(5, 4, 0, 0, 1);
    boxMesh.face[8].set(0, 4, 7, 0, 1);
    boxMesh.face[9].set(7, 1, 0, 0, 1);
    boxMesh.face[10].set(1, 7, 6, 0, 1);
    boxMesh.face[11].set(6, 2, 1, 0, 1);
  }

  // destroy() is delete this, and the game clip builder derives from this one with members of
  // its own.
  virtual ~DagorRayTracerBuilder() { clear(); }

  void clear()
  {
    verts.clear();
    verts.shrink_to_fit();
    faces.clear();
    faces.shrink_to_fit();
    matName.clear();
    clear_and_shrink(matId);
    started = false;
  }

  void destroy() override { delete this; }

  void start(const CollisionBuildSettings &stg) override
  {
    clear();
    leafSize = stg.leafSize();
    levels = stg.levels;
    started = true;
  }

  void addCapsule(Capsule &c, int obj_id, int obj_flags, const char *physmat) override
  {
    if (!started)
    {
      DEBUG_CTX("addCapsule without start");
      return;
    }

    DEBUG_CTX("addCapsule not implemented!");
  }
  void addSphere(const Point3 &c, float rad, int obj_id, int obj_flags, const char *physmat) override
  {
    G_ASSERT(0 && "addSphere not implemented");
  }
  void addBox(const TMatrix &box_tm, int obj_id, int obj_flags, const char *physmat) override
  {
    int pm_id = PhysMat::getMaterialId(physmat ? physmat : "::default");
    const PhysMat::MaterialData &pm = PhysMat::getMaterial(pm_id);

    if (pm.camera_collision)
    {
      boxMesh.vert[0] = -box_tm.getcol(0) - box_tm.getcol(1) + box_tm.getcol(2);
      boxMesh.vert[1] = -box_tm.getcol(0) + box_tm.getcol(1) + box_tm.getcol(2);
      boxMesh.vert[2] = +box_tm.getcol(0) + box_tm.getcol(1) + box_tm.getcol(2);
      boxMesh.vert[3] = +box_tm.getcol(0) - box_tm.getcol(1) + box_tm.getcol(2);

      boxMesh.vert[4] = -box_tm.getcol(0) - box_tm.getcol(1) - box_tm.getcol(2);
      boxMesh.vert[5] = +box_tm.getcol(0) - box_tm.getcol(1) - box_tm.getcol(2);
      boxMesh.vert[6] = +box_tm.getcol(0) + box_tm.getcol(1) - box_tm.getcol(2);
      boxMesh.vert[7] = -box_tm.getcol(0) + box_tm.getcol(1) - box_tm.getcol(2);

      for (int i = 0; i < 8; i++)
        boxMesh.vert[i] += box_tm.getcol(3);

      addMesh(boxMesh, NULL, TMatrix::IDENT, obj_id, obj_flags, pm_id, true, NULL);
    }
  }
  void addConvexHull(Mesh &m, MaterialDataList *mat, TMatrix &wtm, int obj_id, int obj_flags, int physmat_id, bool pmid_force,
    StaticGeometryMesh *sgm = NULL) override
  {
    G_ASSERT(0 && "addSphere not implemented");
  }

  void addMesh(Mesh &m, MaterialDataList *mat, const TMatrix &wtm, int obj_id, int obj_flags, int physmat_id, bool pmid_force,
    StaticGeometryMesh *sgm) override
  {
    if (!started)
    {
      DEBUG_CTX("addMesh without start");
      return;
    }

    const bool placed = &wtm != &TMatrix::IDENT && wtm != TMatrix::IDENT;
    const uint32_t base = (uint32_t)verts.size();
    verts.reserve(base + m.vert.size());
    for (int i = 0; i < m.vert.size(); ++i)
    {
      const Point3 p = placed ? wtm * m.vert[i] : m.vert[i];
      Point3_vec4 &v = verts.push_back();
      v.x = p.x, v.y = p.y, v.z = p.z, v.resv = 1.f;
    }
    faces.reserve(faces.size() + m.face.size() * 3);
    for (int i = 0; i < m.face.size(); ++i)
      for (int k = 0; k < 3; ++k)
        faces.push_back(base + m.face[i].v[k]);

    // setup pmid for each face
    int base_f = append_items(matId, m.face.size());
    String pmname;

    const PhysMat::MaterialData &pm = PhysMat::getMaterial(physmat_id);
    physmat_id = matName.addNameId((physmat_id == PHYSMAT_INVALID) ? "::default" : pm.name);

    if (mat)
    {
      int matnum = mat->subMatCount();

      for (int i = 0; i < m.face.size(); i++)
      {
        int pmid = physmat_id;

        if (matnum > 0 && !pmid_force)
        {
          int matn = m.face[i].mat % matnum;
          if (::getPhysMatNameFromMatName(mat->getSubMat(matn), pmname))
          {
            pmid = matName.addNameId(pmname);
            // if ( PhysMat::getMaterialId ( pmname ) == PHYSMAT_INVALID )
            //   logerr ...
          }
        }

        matId[base_f + i] = pmid;
      }
    }
    else if (sgm)
    {
      for (int i = 0; i < m.face.size(); ++i)
      {
        int pmid = physmat_id;

        if (!pmid_force)
        {
          const int matIdx = m.face[i].mat;

          if (matIdx >= 0 && matIdx < sgm->mats.size())
          {
            if (sgm->mats[matIdx] && ::getPhysMatNameFromMatName(sgm->mats[matIdx]->name, pmname))
              pmid = matName.addNameId(pmname);
          }
          else
            debug("Material index [%i] in face #%i more then materials count [%i]", matIdx, i, sgm->mats.size());
        }

        matId[base_f + i] = pmid;
      }
    }
    else
    {
      for (int i = 0; i < m.face.size(); ++i)
        matId[base_f + i] = physmat_id;
    }
  }

  bool finishAndWrite(const char * /*temp_fname*/, IGenSave &cwr, unsigned target_code) override
  {
    if (!started)
    {
      DEBUG_CTX("finishAndWrite without start");
      return false;
    }

    bool big_endian = dagor_target_code_be(target_code);
    const int fcnt = (int)faces.size() / 3, vcnt = (int)verts.size();

    // write FRT dump
    mkbindump::BinDumpSaveCB bdcwr(256 << 10, target_code, big_endian);
    bdcwr.writeFourCC(MAKE4C(0xFF, 'v', '1', 0xFF));

    // The tracer is this writer's own format: it is built here, from the gathered scene.
    BuildableStaticSceneRayTracer *rt = new BuildableStaticSceneRayTracer(leafSize, levels);
    rt->addmesh((const uint8_t *)verts.data(), sizeof(Point3_vec4), vcnt, faces.data(), sizeof(uint32_t) * 3, fcnt, nullptr,
      /*rebuild_now*/ true);
    int dumplen = 0;
    const bool serialized = rt->serialize(bdcwr.getRawWriter(), big_endian, &dumplen);
    del_it(rt);
    if (!serialized)
    {
      logerr("Can't save raytracer dump");
      return false;
    }
    bdcwr.writeTabDataRaw(matId); // because of 8-bit elemsize
    bdcwr.align4();

    bdcwr.beginBlock();
    bdcwr.setOrigin();
    bdcwr.writeRef(bdcwr.TAB_SZ, matName.nameCount());
    bdcwr.writeZeroes(bdcwr.PTR_SZ * matName.nameCount());
    for (int i = 0; i < matName.nameCount(); i++)
    {
      bdcwr.writeInt32eAt(bdcwr.tell(), bdcwr.TAB_SZ + bdcwr.PTR_SZ * i);
      bdcwr.writeRaw(matName.getName(i), (int)strlen(matName.getName(i)) + 1);
    }
    bdcwr.align4();
    bdcwr.popOrigin();
    bdcwr.endBlock();

    unsigned physmats_used = matName.nameCount();
    for (unsigned i = 0; i < physmats_used; i++)
      debug("  physmat[%d]=%s", i, matName.getName(i));
    clear();

    // try load dump (only when built for PC)
    if (target_code == _MAKE4C('PC'))
    {
      MemoryLoadCB crd(bdcwr.getMem(), false);
      crd.seekrel(4);
      auto drt = DeserializedStaticSceneRayTracer::load(crd);
      if (!drt)
      {
        logerr("Can't load raytracer dump from");
        return false;
      }
      destroy_it(drt);
    }

    if (fcnt && vcnt)
      bdcwr.copyDataTo(cwr);
    DAEDITOR3.conNote("FRT dump size: %dK, packed: %dK;  %d faces, %d verts (%d phys mats used)", dumplen >> 10, bdcwr.getSize() >> 10,
      fcnt, vcnt, physmats_used);

    return true;
  }
};


// The game's static collision: the gathered scene written as a cooked collision asset is. Its
// water goes to a second stream of its own, which finishAndWriteWater hands over.
class StaticCollisionDumpBuilder final : public DagorRayTracerBuilder
{
  String physmatPath;
  bool failOnJoltDegenerate;
  DynamicMemGeneralSaveCB waterStream;
  static const char *mat_name(int id) { return PhysMat::getMaterial(id).name.str(); }

  // One resource out of the faces named by idx, written as a cooked collision asset is: the label,
  // one block, ZSTD when it pays, loaded back to refuse an empty landing.
  bool writeStream(IGenSave &cwr, const char *name, uint16_t behavior_flags, dag::ConstSpan<uint32_t> idx,
    dag::ConstSpan<int16_t> pmid)
  {
    CollisionResourceBuilder builder;
    const int parts = builder.addSplitMeshNodes(make_span_const(&behavior_flags, 1), make_span_const(verts), idx, pmid);
    if (parts <= 0)
    {
      logerr("%s: made no node out of %d faces (%d parts)", name, (int)idx.size() / 3, parts);
      return false;
    }
    builder.recomputeBounds();
    builder.collapse(name);
    builder.collisionFlags |= COLLISION_RES_FLAG_BLAS_TWO_SIDED;

    const unsigned label = 0xACE50000 | COLLRES_STREAM_VERSION;
    mkbindump::BinDumpSaveCB mcwr(1 << 20, _MAKE4C('PC'), /*be_target*/ false); // the stream is little-endian for every target
    if (!builder.write(mcwr.getRawWriter(), name, &mat_name))
    {
      logerr("%s: the stream cannot be written", name);
      return false;
    }

    mkbindump::BinDumpSaveCB out(mcwr.getSize() + 16, mcwr);
    out.writeInt32e(label);
    out.beginBlock();
    mkbindump::BinDumpSaveCB zcwr(mcwr.getSize(), mcwr);
    MemoryLoadCB mcrd(mcwr.getRawWriter().getMem(), false);
    if (mcwr.getSize() >= 512)
      zstd_compress_data(zcwr.getRawWriter(), mcrd, mcwr.getSize(), 256 << 10, 19);
    if (zcwr.getSize() && zcwr.getSize() < mcwr.getSize() * 8 / 10 && zcwr.getSize() + 256 < mcwr.getSize()) // enough profit
    {
      zcwr.copyDataTo(out.getRawWriter());
      out.endBlock(btag_compr::ZSTD);
    }
    else
    {
      mcwr.copyDataTo(out.getRawWriter());
      out.endBlock(btag_compr::NONE);
    }

    // the shipped bytes back through the loader, compression and all: a refused landing ships nothing
    int landedNodes = 0;
    {
      MemoryLoadCB acrd(out.getRawWriter().getMem(), false);
      CollisionResource back(acrd, -1, name); // no resolver: the default is PhysMat's own answer
      landedNodes = (int)back.getAllNodes().size();
      if (!landedNodes)
      {
        logerr("%s: the stream loads as an empty resource", name);
        return false;
      }
      // only a phys-collidable stream becomes Jolt bodies; water is traced, never built
      if ((behavior_flags & CollisionNode::PHYS_COLLIDABLE) && !back.validateVerticesForJolt(name))
      {
        if (failOnJoltDegenerate)
        {
          DAEDITOR3.conError("%s: Jolt-degenerate faces, and joltDegenerativeTriFailExport is set", name);
          return false;
        }
        DAEDITOR3.conWarning("%s: Jolt-degenerate faces (see the log)", name);
      }
    }
    out.copyDataTo(cwr);
    DAEDITOR3.conNote("%s: %d faces -> %d parts -> %d nodes; stream %dK, packed %dK", name, (int)idx.size() / 3, parts, landedNodes,
      mcwr.getSize() >> 10, out.getSize() >> 10);
    return true;
  }

public:
  StaticCollisionDumpBuilder(const char *physmat_path, bool fail_on_jolt_degenerate) :
    physmatPath(physmat_path), failOnJoltDegenerate(fail_on_jolt_degenerate), waterStream(tmpmem, 0, 64 << 10)
  {}
  bool finishAndWrite(const char * /*temp_fname*/, IGenSave &cwr, unsigned /*target_code*/) override
  {
    if (!started)
    {
      DEBUG_CTX("finishAndWrite without start");
      return false;
    }

    DataBlock physmatBlk;
    const DataBlock *matsBlk = !physmatPath.empty() && physmatBlk.load(physmatPath) ? physmatBlk.getBlockByName("PhysMats") : nullptr;
    if (!matsBlk)
    {
      DAEDITOR3.conError("static collision: no PhysMats in <%s>; a water face would ship as solid collision",
        physmatPath.empty() ? "(no physmat blk configured)" : physmatPath.str());
      return false;
    }
    const int matCount = matName.nameCount();
    Tab<uint8_t> matRuntimeId(tmpmem), matIsWater(tmpmem);
    matRuntimeId.resize(matCount);
    matIsWater.resize(matCount);
    for (int i = 0; i < matCount; ++i)
    {
      // getMaterialId answers the default for a name the table lacks: name it back to see a typo
      const int id = PhysMat::getMaterialId(matName.getName(i));
      if (id == PHYSMAT_DEFAULT && dd_stricmp(PhysMat::getMaterial(id).name, matName.getName(i)) != 0)
        logwarn("static collision: physmat <%s> is not in the table; its faces take the default", matName.getName(i));
      G_ASSERT(id >= 0 && id <= 255);
      matRuntimeId[i] = (uint8_t)id;
      const DataBlock *matBlk = matsBlk->getBlockByName(matName.getName(i));
      matIsWater[i] = matBlk && matBlk->getBool("isWater", false);
    }
    const int faceCount = (int)faces.size() / 3;
    if (faceCount <= 0)
    {
      DAEDITOR3.conNote("static collision: no faces, nothing written");
      clear();
      return true;
    }
    // The scene and its water are two resources: the water is traced alone, and nothing else may
    // trace or collide with it.
    Tab<uint32_t> sceneIdx(tmpmem), waterIdx(tmpmem);
    Tab<int16_t> scenePmid(tmpmem), waterPmid(tmpmem);
    for (int f = 0; f < faceCount; ++f)
    {
      const bool isWater = matIsWater[matId[f]] != 0;
      Tab<uint32_t> &idx = isWater ? waterIdx : sceneIdx;
      for (int k = 0; k < 3; ++k)
        idx.push_back(faces[f * 3 + k]);
      (isWater ? waterPmid : scenePmid).push_back((int16_t)matRuntimeId[matId[f]]);
    }

    if (!waterIdx.empty() &&
        !writeStream(waterStream, "water", CollisionNode::TRACEABLE, make_span_const(waterIdx), make_span_const(waterPmid)))
      return false;
    if (sceneIdx.empty())
    {
      DAEDITOR3.conNote("static collision: water only, no scene faces");
      clear();
      return true;
    }
    if (!writeStream(cwr, "frt", CollisionNode::TRACEABLE | CollisionNode::PHYS_COLLIDABLE, make_span_const(sceneIdx),
          make_span_const(scenePmid)))
      return false;
    DAEDITOR3.conNote("static collision: %d faces (%d water), %d verts, %d phys mats", faceCount, (int)waterIdx.size() / 3,
      (int)verts.size(), matCount);
    clear();
    return true;
  }

  bool finishAndWriteWater(IGenSave &cwr) override
  {
    if (!waterStream.size())
      return false;
    cwr.write(waterStream.data(), waterStream.size());
    return true;
  }
};


ICollisionDumpBuilder *create_dagor_raytracer_dump_builder() { return new (tmpmem) DagorRayTracerBuilder; }
ICollisionDumpBuilder *create_static_collision_dump_builder(const char *physmat_path, bool fail_on_jolt_degenerate)
{
  return new (tmpmem) StaticCollisionDumpBuilder(physmat_path, fail_on_jolt_degenerate);
}
