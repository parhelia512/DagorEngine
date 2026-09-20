//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <generic/dag_tab.h>
#include <EASTL/fixed_function.h>
#include <shaders/dag_shaders.h>
#include <util/dag_oaHashNameMap.h>

class ShaderMesh;

// false leaves an elem out of the bake, judged by its material; an empty callback keeps
// every elem. a project that classifies vegetation per material uses it so a vine covered
// crate contributes its vines and not the crate
typedef eastl::fixed_function<64, bool(const ShaderMaterial &mat)> media_voxelize_elem_cb;

// draws vegetation meshes into the media brick a DaGIMediaVolumes bake has open, through this
// lib's own bake shader, so a project needs no media variant in its own vegetation shaders.
// one instance per project, held across bakes: the bake shader cache and the once per class
// skip log live on it, a per callback instance would rebuild and re-log every retry.
// DaGIMediaVolumes knows nothing of it: a project whose own shaders already voxelize media
// keeps doing that in its render callback and never instantiates this
struct VegetationMediaVoxelizer
{
  // a bake render callback answers "ready" with a bool, so AND (status != NotStreamed) over
  // the meshes of the type: any NotStreamed keeps the type pending. NoElems is ready, not a
  // failure: a type with nothing to bake has to settle on its empty brick, or the bake
  // retries it for ever
  enum class Status
  {
    Drawn,       // at least one elem rasterized into the open brick
    NoElems,     // nothing the bake shader can draw: the mesh holds no media, or none in a
                 // form it reads (each such shader class logerrs once; classification owns the fix)
    NotStreamed, // vertex data, a texture, or the bake shader itself is not available yet; retry later
  };
  // call from the render callback of a bake, once per mesh of the type. only indexed elems
  // of the opaque and atest stages are considered, and one whose vertex format or material
  // can not feed the shader (an unmodified float3 POSITION and float2 TEXCOORD0, both at
  // usage index 0, and a diffuse texture) is skipped. cutout
  // (the material var atest above 0, or SHFLG_2SIDED) clips at 0.5 on the mask: the red
  // channel of texture slot 1 if present, else diffuse alpha; opaque elems write every
  // fragment. the draw runs with rendinst_render_pass pinned to the normal pass, the
  // frame's value is put back
  Status voxelize(const ShaderMesh &mesh, const media_voxelize_elem_cb &keep_elem = {});

protected:
  // one bake shader element per source vertex format: the source layout is imposed on it, and
  // replaceVdecl resets the shader programs, so a shared element would thrash between formats
  struct BakeShader
  {
    VDECL vdecl = BAD_VDECL;
    Ptr<ShaderMaterial> mat;
    Ptr<ShaderElement> elem;
  };
  Tab<BakeShader> bakeShaders;
  OAHashNameMap<false> reportedUnreadable; // shader classes already named in the skip logerr
  ShaderElement *bakeElem(VDECL vdecl);
};
