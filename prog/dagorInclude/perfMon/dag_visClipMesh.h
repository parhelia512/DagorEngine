//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <math/dag_e3dColor.h>
#include <math/dag_Point3.h>
#include <vecmath/dag_vecMathDecl.h>
#include <EASTL/fixed_function.h>


#define MAX_VISCLIPMESH_FACETS 1024


//************************************************************************
//* forwards
//************************************************************************
class CfgReader;
class FastRtDump;
class FastRtDumpManager;

struct VisClipMeshVertex
{
  Point3 pos;
  E3DCOLOR color;
};


bool create_visclipmesh(CfgReader &cfg, bool for_game = false);
void delete_visclipmesh(void);

void render_visclipmesh(const FastRtDump &cliprtr, const Point3 &pos);
void render_visclipmesh(const FastRtDumpManager &cliprtr, const Point3 &pos);
// For a source this library must not link: walk is called once and calls emit per triangle, with
// the physmat the triangle takes its colour from.
using VisClipMeshEmit = eastl::fixed_function<sizeof(void *) * 2, void(vec3f, vec3f, vec3f, int)>;
void render_visclipmesh_stream(const eastl::fixed_function<sizeof(void *) * 2, void(const VisClipMeshEmit &)> &walk);

float get_vcm_rad();
void set_vcm_rad(float rad);

bool is_vcm_visible();
void set_vcm_visible(bool visible);
int set_vcm_draw_type(int type);

// call it after all objects are rendered (after all)
void render_visclipmesh_info(bool need_start_render);
