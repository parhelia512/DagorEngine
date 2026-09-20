// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <startup/dag_globalSettings.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <osApiWrappers/dag_basePath.h>
#include <osApiWrappers/dag_direct.h>
#include <ioSys/dag_fileIo.h>
#include <math/dag_math3d.h>
#include <util/dag_globDef.h>
#include <util/dag_threadPool.h>
#include <debug/dag_logSys.h>
#include <daSDF/generate_sdf.h>


static void show_usage();

struct Mesh
{
  dag::Vector<uint32_t> indices;
  dag::Vector<Point3> vertices;
  dag::Vector<mat43f> instances;
  MeshBLAS blas;
};

#include <rendInst/riCollisionDump.h>

// Fills Mesh storage straight from the shared dump walk. Narrow (16-bit) records go through a
// scratch and are widened in endMesh; wide records are read in place. Every record is kept --
// unlike the SWRT sample path, SDF generation has no 16-bit index constraint.
struct DumpReadHandler
{
  dag::Vector<Mesh> &meshes;
  dag::Vector<uint16_t> indices16;
  bool wantMesh(int, bool)
  {
    meshes.push_back();
    return true;
  }
  void *indexBuffer(int count, bool wide)
  {
    if (wide)
    {
      meshes.back().indices.resize(count);
      return meshes.back().indices.data();
    }
    indices16.resize(count);
    return indices16.data();
  }
  Point3 *vertexBuffer(int count)
  {
    meshes.back().vertices.resize(count);
    return meshes.back().vertices.data();
  }
  mat43f *instanceBuffer(int count)
  {
    meshes.back().instances.resize(count);
    return meshes.back().instances.data();
  }
  void endMesh(int, bool wide, int index_count, int, int)
  {
    Mesh &mesh = meshes.back();
    if (!wide)
    {
      mesh.indices.resize(index_count);
      for (int i = 0; i < index_count; ++i)
        mesh.indices[i] = indices16[i];
    }
  }
};

static bool loadObjects(IGenLoad &cb, dag::Vector<Mesh> &meshes)
{
  DumpReadHandler h{meshes};
  return read_ri_collision_dump(cb, h);
}

static void generateSDF(dag::Vector<Mesh> &meshes, IGenSave &scb, float density)
{
  int verts = 0, inds = 0, inst = 0;
  for (auto &m : meshes)
  {
    verts += m.vertices.size();
    inds += m.indices.size();
    inst += m.instances.size();
  }
  printf("total %d meshes, %d vertices, %d tri, %d instances\n", (int)meshes.size(), verts, inds / 3, inst);
  int noGeom = 0;
  for (auto &m : meshes)
    if (!build_mesh_blas(m.blas, make_span_const(m.vertices), make_span_const(m.indices)))
      noGeom++;
  if (noGeom)
    printf("%d of %d meshes were refused and bake empty: an index past the vertex count, no buildable triangle, a non-finite "
           "vertex, or a vertex span past the leaf base range\n",
      noGeom, (int)meshes.size());
  size_t cnt = 0, maxCnt = 64 << 10;
  // scb.writeInt(meshes.size());
  scb.writeInt(min<size_t>(maxCnt, meshes.size()));
  for (auto &m : meshes)
  {
    int c = cnt++;
    // if (c != 1263)
    //   continue;
    MippedMeshSDF meshSDF;
    generate_sdf(m.blas, meshSDF, density, 512);
    if (c >= maxCnt)
      break;
    scb.write(&meshSDF.mipCountTwoSided, sizeof(meshSDF.mipCountTwoSided));
    scb.write(&meshSDF.localBounds, sizeof(meshSDF.localBounds));
    scb.write(meshSDF.mipInfo.data(), meshSDF.mipCount() * sizeof(meshSDF.mipInfo[0]));
    scb.writeInt(meshSDF.compressedMips.size());
    scb.write(meshSDF.compressedMips.data(), meshSDF.compressedMips.size());
    printf(" %3d/%3d\r", uint32_t(c), uint32_t(meshes.size()));
  }
}

int DagorWinMain(bool debugmode)
{
  start_classic_debug_system("debug", false);
  dd_get_fname(""); //== pull in directoryService.obj
  printf("SDF build generattor\n");
  printf("Copyright (c) Gaijin Games KFT, 2026\n");
  int ctime = time(NULL);

  if (dgs_argc < 2)
  {
    ::show_usage();
    return 1;
  }

  cpujobs::init(-1);
  int num_workers = cpujobs::get_core_count() - 1;
  if (dgs_argc > 2)
    num_workers = clamp(atoi(dgs_argv[2]), 0, 64);
  int stack_size = 128 << 10;
  threadpool::init(num_workers, 2048, stack_size);
  printf("process in %d workers\n", num_workers);

  dd_add_base_path("");

  printf("Loading file...\n");
  FullFileLoadCB cb(dgs_argv[1]);
  if (!cb.fileHandle)
  {
    printf("ERROR LOADING file <%s>\n", dgs_argv[1]);
    return 2;
  }
  init_sdf_generate();
  // Load before opening the output: a malformed/truncated dump must fail the run, not leave a
  // partial or empty sdf.bin behind.
  dag::Vector<Mesh> meshes;
  if (!loadObjects(cb, meshes))
  {
    printf("ERROR: malformed or truncated collision dump <%s>\n", dgs_argv[1]);
    return 3;
  }
  FullFileSaveCB scb(dgs_argc > 3 ? dgs_argv[3] : "sdf.bin");
  float density = dgs_argc > 4 ? atof(dgs_argv[4]) : 2.0f;
  printf("%f density\n", density);
  generateSDF(meshes, scb, density);
  printf("done in %ds\n", int(time(NULL) - ctime));
  threadpool::shutdown();
  cpujobs::term(true, 1000);
  return 0;
}


//==============================================================================
static void show_usage() { printf("usage: buildSDF <file name in> \n"); }
