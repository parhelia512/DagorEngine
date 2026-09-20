// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <3d/dag_texMgr.h>
#include <drv/3d/dag_draw.h>
#include <drv/3d/dag_driver.h>
#include <drv/3d/dag_info.h>
#include <shaders/dag_shaders.h>
#include <shaders/dag_shaderMesh.h>
#include <rendInst/renderPass.h>
#include <voxelizedMedia/vegetationMediaVoxelizer.h>

#define GLOBAL_VARS_LIST            \
  VAR(dagi_media_vol_veg_tex)       \
  VAR(dagi_media_vol_veg_alpha_tex) \
  VAR(dagi_media_vol_veg_atest)

#define VAR(a) static ShaderVariableInfo a##VarId(#a, true);
GLOBAL_VARS_LIST
#undef VAR

// the vertex layout of a source elem, kept for our own bake shader. the bake draws one
// instance per voxelize axis and reads no per instance data
struct VegBakeChannels : public ShaderChannelsEnumCB
{
  static constexpr int MAX_CHANNELS = 32;
  CompiledShaderChannelId ch[MAX_CHANNELS];
  int count = 0;
  bool posOk = false, tcOk = false;
  bool usable() const { return posOk && tcOk && count <= MAX_CHANNELS; }
  void enum_shader_channel(int, int, int t, int vbu, int vbui, ChannelModifier mod, int) override
  {
    // the layout offsets are implied by the channel order, so an overflow can not be
    // truncated: it disqualifies the elem
    if (count < MAX_CHANNELS)
      ch[count] = CompiledShaderChannelId(t, vbu, vbui, 0);
    ++count;
    if (vbu == SCUSAGE_POS && vbui == 0)
      posOk = t == SCTYPE_FLOAT3 && mod == CMOD_NONE;
    else if (vbu == SCUSAGE_TC && vbui == 0)
      tcOk = t == SCTYPE_FLOAT2 && mod == CMOD_NONE;
  }
};

ShaderElement *VegetationMediaVoxelizer::bakeElem(VDECL vdecl)
{
  for (const BakeShader &s : bakeShaders)
    if (s.vdecl == vdecl)
      return s.elem;
  bakeShaders.push_back(BakeShader());
  BakeShader &s = bakeShaders.back();
  s.vdecl = vdecl;
  s.mat = new_shader_material_by_name_optional("dagi_media_vol_voxelize_veg");
  s.elem = s.mat ? s.mat->make_elem("dagi media volume bake") : nullptr;
  if (s.elem)
    s.elem->replaceVdecl(vdecl);
  else
    LOGERR_ONCE("daGI media volumes: no dagi_media_vol_voxelize_veg shader, the bakes stay pending");
  return s.elem;
}

VegetationMediaVoxelizer::Status VegetationMediaVoxelizer::voxelize(const ShaderMesh &mesh, const media_voxelize_elem_cb &keep_elem)
{
  // a material (not global) var: a source shader that declares atest tells us how it masks
  static const int atestVarId = ::get_shader_variable_id("atest", true);
  // reading a channel list resolves the material's static variant against the rendinst pass,
  // and the vegetation shaders are dont_render for the albedo voxelize passes a frame can
  // leave the var at: such an elem would settle an empty brick daGI2 keeps for good. pin
  // the normal pass, and put the frame's value back
  static const int riRenderPassVarId = ::get_shader_variable_id("rendinst_render_pass", true);
  const int prevRenderPass = ShaderGlobal::get_int(riRenderPassVarId);
  ShaderGlobal::set_int(riRenderPassVarId, int(rendinst::RenderPass::Normal));
  int drawn = 0;
  Status status = Status::NoElems;
  const GlobalVertexData *boundVd = nullptr;
  for (const ShaderMesh::RElem &re : mesh.getElems(ShaderMesh::STG_opaque, ShaderMesh::STG_atest))
  {
    if (!re.e || !re.mat || re.si == RELEM_NO_INDEX_BUFFER)
      continue;
    if (keep_elem && !keep_elem(*re.mat))
      continue;
    // wait for every kept elem, vertex data and sampled textures alike: a completed bake is
    // permanent, and a stub texture bakes a wrong brick as surely as a missing vertex buffer
    if (re.vertexData->isEmpty())
    {
      status = Status::NotStreamed;
      break;
    }
    const TEXTUREID diffuse = re.mat->get_texture(0);
    VegBakeChannels ch;
    int codeFlags = 0;
    // an elem the bake shader can not read is dropped, not given a variant of its own. it can
    // not be silent: a type whose every elem drops bakes an empty brick and keeps it for good
    if (diffuse == BAD_TEXTUREID || !re.mat->enum_channels(ch, codeFlags) || !ch.usable())
    {
      // once per shader class, not once per process: every distinct unreadable class must
      // say so, or a later type settles its empty brick in silence
      const char *cls = re.mat->getShaderClassName();
      const int seen = reportedUnreadable.nameCount();
      if (reportedUnreadable.addNameId(cls) >= seen)
        logerr("daGI media volumes: <%s> elem is not bakeable (no diffuse, or a vertex layout the bake shader"
               " can not read), it adds no media",
          cls);
      continue;
    }
    ShaderElement *elem = bakeElem(dynrender::addShaderVdecl(ch.ch, ch.count));
    if (!elem)
    {
      // an absent bake shader is not "no media": NoElems would settle a permanently empty brick
      status = Status::NotStreamed;
      break;
    }
    // cutout by the exporter's atest (the rule that sorts an elem into STG_atest), and a
    // two sided elem is a card whose alpha is the mask even when it declares atest 0
    int atest = 0;
    const bool cutout = (re.mat->getIntVariable(atestVarId, atest) && atest > 0) || (re.mat->get_flags() & SHFLG_2SIDED) != 0;
    // the mask of a cutout is the diffuse alpha, unless the material carries a second
    // texture: the vegetation shaders that split the mask off keep it in slot 1, and the
    // diffuse they pair it with has no alpha channel of its own
    const TEXTUREID alphaTex = cutout ? re.mat->get_texture(1) : BAD_TEXTUREID;
    // full quality: the cutout decides the brick occupancy, and a base quality mip clips a
    // different set of texels. the bake is permanent, so a later full load never redoes it
    if (!prefetch_and_check_managed_texture_loaded(diffuse, true) ||
        (alphaTex != BAD_TEXTUREID && !prefetch_and_check_managed_texture_loaded(alphaTex, true)))
    {
      status = Status::NotStreamed;
      break;
    }
    ShaderGlobal::set_texture(dagi_media_vol_veg_texVarId, diffuse);
    ShaderGlobal::set_texture(dagi_media_vol_veg_alpha_texVarId, alphaTex);
    ShaderGlobal::set_float(dagi_media_vol_veg_atestVarId, cutout ? 0.5f : 0.f);
    if (re.vertexData != boundVd)
    {
      boundVd = re.vertexData;
      re.vertexData->setToDriver();
    }
    if (elem->setStates())
    {
      // a draw failure is never silent: d3d_err fatals in dev, and the device lost path ends
      // in a reset, whose afterReset rebakes every type
      d3d_err(d3d::drawind_instanced(PRIM_TRILIST, re.si, re.numf, re.baseVertex, 3)); // one instance per voxelize axis
      ++drawn;
    }
    else
    {
      // same infrastructure failure as an absent shader: retry, do not settle a partial brick
      LOGERR_ONCE("daGI media volumes: dagi_media_vol_voxelize_veg has no renderable variant, the bakes stay pending");
      status = Status::NotStreamed;
      break;
    }
  }
  ShaderGlobal::set_int(riRenderPassVarId, prevRenderPass);
  ShaderGlobal::set_texture(dagi_media_vol_veg_texVarId, BAD_TEXTUREID);
  ShaderGlobal::set_texture(dagi_media_vol_veg_alpha_texVarId, BAD_TEXTUREID);
  return status == Status::NotStreamed ? status : (drawn ? Status::Drawn : Status::NoElems);
}
