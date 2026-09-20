// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <memory/dag_framemem.h>

#include <daFracture/core/destrMesh.h>
#include <daFracture/core/meshCommon.h>
#include <daFracture/core/meshSlicing.h>
#include <daFracture/core/cutFaceFill.h>

#include "cutMeshCommon.h"
#include "cutMeshPlane.h"
#include "cutMeshHmap.h"
#include "cutFaceFill.h"

namespace frx
{

__forceinline PlaneBasis transform_basis_to_local(PlaneBasis world, const TMatrix &tm)
{
  mat44f m44, m44t;
  v_mat44_make_from_43cu(m44, tm.array);
  v_mat44_transpose(m44t, m44);
  PlaneBasis local;
  v_stu(&local.plane.n.x, v_mat44_mul_vec4(m44t, v_ldu(&world.plane.n.x)));
  v_stu_p3(&local.U.x, v_mat44_mul_vec3v(m44t, v_ldu(&world.U.x)));
  v_stu_p3(&local.V.x, v_mat44_mul_vec3v(m44t, v_ldu(&world.V.x)));
  // re-orthonormalize
  const float invNLen = 1.f / length(local.plane.n);
  local.plane.n *= invNLen;
  local.plane.d *= invNLen;
  local.U = normalize(local.U - local.plane.n * (local.U * local.plane.n));
  local.V = cross(local.U, local.plane.n);
  return local;
}

template <typename HmapImplT = const eastl::monostate>
void mesh_slice_impl(DestrContext &ctx, const DestrMesh &mesh, const MeshSliceParams &params, HmapImplT &hmap = eastl::monostate())
{
  if (!params.upMesh && !params.downMesh)
    return;
  if (mesh.faces.empty())
    return;

  ctx.dbgDraw.tm = mesh.tm;
  PlaneBasis localCutPlane = transform_basis_to_local(params.cutPlane, mesh.tm);
  const uint16_t cutMatId = params.cutMatId == -1 ? mesh.faces[0].mat : params.cutMatId;

  DestrMesh meshToDiscard;
  DestrMesh &upMeshRef = params.upMesh ? *params.upMesh : meshToDiscard;
  DestrMesh &downMeshRef = params.downMesh ? *params.downMesh : meshToDiscard;
  upMeshRef.tm = downMeshRef.tm = mesh.tm;
  CutFaceData cutFaceData; // intentionally not framemem, fill_cut_faces need a lot of it
  cutFaceData.basis = localCutPlane;

  // crude estimate
  upMeshRef.verts.reserve(mesh.verts.size());
  downMeshRef.verts.reserve(mesh.verts.size());
  upMeshRef.faces.reserve(mesh.faces.size());
  downMeshRef.faces.reserve(mesh.faces.size());
  cutFaceData.edges.reserve(16 + mesh.faces.size() / 4);
  cutFaceData.verts.reserve(cutFaceData.edges.size() / 2);

  {
    FRAMEMEM_REGION; // note: safe because cutFaceData and meshes are not framemem
    if constexpr (eastl::is_same_v<eastl::decay_t<HmapImplT>, eastl::monostate>)
      cut_mesh_plane_impl(ctx, mesh, upMeshRef, downMeshRef, cutFaceData);
    else
      cut_mesh_hmap_impl(ctx, mesh, upMeshRef, downMeshRef, cutFaceData, hmap);
  }
  {
    FRAMEMEM_REGION;
    fill_cut_faces(ctx, cutFaceData, params.upMesh, params.downMesh, cutMatId, hmap);
  }

  if (params.pushDist != 0.f)
  {
    if (params.downMesh)
      params.downMesh->tm.setcol(3, params.downMesh->tm.getcol(3) - params.cutPlane.plane.n * params.pushDist);
    if (params.upMesh)
      params.upMesh->tm.setcol(3, params.upMesh->tm.getcol(3) + params.cutPlane.plane.n * params.pushDist);
  }
}

} // namespace frx