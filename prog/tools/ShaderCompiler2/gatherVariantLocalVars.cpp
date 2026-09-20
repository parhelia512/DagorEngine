// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "gatherVariantLocalVars.h"

#include "boolExpr.h"
#include "semUtils.h"

#include <util/dag_bitwise_cast.h>
#include <shaders/shOpcodeFormat.h>
#include <shaders/shOpcode.h>
#include <shaders/shUtils.h>
#include <shaders/shFunc.h>

namespace ShaderParser
{

int appendVarToContext(shc::VariantContext &ctx, const char *name, ShaderVarType type, const int nameId, void *loc, bool dynamic,
  bool noWarning, bool used, int slot)
{
  ShaderSemCode &code = ctx.parsedSemCode();

  int vi = append_items(code.vars, 1);
  code.vars[vi].type = type;
  code.vars[vi].nameId = nameId;
  code.vars[vi].terminal = loc;
  code.vars[vi].dynamic = dynamic;
  code.vars[vi].used = used;
  code.vars[vi].slot = slot;
  code.vars[vi].noWarnings = noWarning;

  code.staticStcodeVars.add(name, vi);

  return vi;
}

void parseAttribs(shc::VariantContext &ctx, const eastl::vector<static_attrib_decl *> &attribs, const ShaderSemCode::Var &var,
  uint32_t &flags, uint32_t &stubCol)
{
  ExpressionParser exprParser{ctx};
  auto &parser = ctx.tgtCtx().sourceParseState().parser;
  for (auto *attrib : attribs)
  {
    if (streq(attrib->name->text, "stub"))
    {
      if (var.dynamic)
      {
        report_error(parser, attrib->name, "'%s' attribute is not supported for dynamic vars", attrib->name->text);
        return;
      }
      if (var.type != SHVT_TEXTURE)
      {
        report_error(parser, attrib->name, "'%s' attribute is only supported for texture vars", attrib->name->text);
        return;
      }
      if (attrib->value)
      {
        report_error(parser, attrib->name, "'%s' attribute takes a color expression", attrib->name->text);
        return;
      }

      Color4 val{};
      if (!exprParser.parseConstExpression(*attrib->expr, val, ExpressionParser::Context{shexpr::VT_COLOR4, false, attrib->name}))
      {
        report_error(parser, attrib->name, "Wrong expression for '%s' color", attrib->name->text);
        return;
      }

      val = clamp(val, Color4(0.f, 0.f, 0.f, 0.f), Color4(1.f, 1.f, 1.f, 1.f));
      flags |= ShaderClass::VF_HAS_STUB_COLOR;
      stubCol =
        (uint32_t(255.f * val.r)) | (uint32_t(255.f * val.g) << 8) | (uint32_t(255.f * val.b) << 16) | (uint32_t(255.f * val.a) << 24);
    }
    else
    {
      report_warning(parser, *attrib->name, "Unknown static attribute '%s'", attrib->name->text);
    }
  }
}

eastl::optional<ShaderVarType> shtok_to_shvt(int shtok)
{
  switch (shtok)
  {
    case SHADER_TOKENS::SHTOK_int: return SHVT_INT;
    case SHADER_TOKENS::SHTOK_int4: return SHVT_INT4;
    case SHADER_TOKENS::SHTOK_float: return SHVT_REAL;
    case SHADER_TOKENS::SHTOK_float4x4: return SHVT_FLOAT4X4;
    case SHADER_TOKENS::SHTOK_float4x3: return SHVT_FLOAT4x3;
    case SHADER_TOKENS::SHTOK_float4: return SHVT_COLOR4;
    case SHADER_TOKENS::SHTOK_texture: return SHVT_TEXTURE;
    default: return eastl::nullopt;
  }
}

void GatherVariantLocalVarsCB::eval_static(static_var_decl &s)
{
  auto shvt = shtok_to_shvt(s.type->type->num);
  if (!shvt)
  {
    report_error(parser, s.type->type, "Unsupported shadervar type %s to declare as static/dynamic variable", s.type->type->text);
    return;
  }
  ShaderVarType t = *shvt;

  int varNameId = ctx.tgtCtx().varNameMap().addVarId(s.name->text);

  int v = code.find_var(varNameId);
  if (v >= 0)
  {
    eastl::string message(eastl::string::CtorSprintf{}, "static variable '%s' already declared in ", s.name->text);
    message += parser.get_lexer().get_symbol_location(varNameId, SymbolType::STATIC_VARIABLE);
    report_error(parser, s.name, message.c_str());
    return;
  }
  parser.get_lexer().register_symbol(varNameId, SymbolType::STATIC_VARIABLE, s.name);

  v = appendVarToContext(ctx, s.name->text, t, varNameId, s.name, s.mode && s.mode->mode->num == SHADER_TOKENS::SHTOK_dynamic,
    s.no_warnings);

  bool inited = code.vars[v].dynamic || (t != SHVT_TEXTURE) || s.init;
  if (!inited)
    report_error(parser, s.name, "Variable '%s' must be inited", s.name->text);

  uint32_t flags = 0;
  uint32_t stubCol = 0;
  parseAttribs(ctx, s.attrib, code.vars[v], flags, stubCol);

  const bool hasStubColor = flags & ShaderClass::VF_HAS_STUB_COLOR;

  bool varReferenced = true;

  if (!s.mode && !hasStubColor)
  {
    const ShaderVariant::TypeTable &allRefStaticVars = ctx.shCtx().typeTables().referencedTypes;
    int intervalNameId = ctx.tgtCtx().intervalNameMap().getNameId(s.name->text);
    ShaderVariant::ExtType intervalIndex = allRefStaticVars.getIntervals()->getIntervalIndex(intervalNameId);
    const Interval *interv = allRefStaticVars.getIntervals()->getInterval(intervalIndex);
    varReferenced = interv ? allRefStaticVars.findType(interv->getVarType(), intervalIndex) != -1 : false;
    code.vars[v].used = varReferenced;
  }

  int sv = sclass.find_static_var(varNameId);
  if (sv < 0 && varReferenced)
  {
    sv = append_items(sclass.stvar, 1);
    sclass.stvar[sv].type = t;
    sclass.stvar[sv].nameId = varNameId;
    sclass.stvar[sv].additionalFlags = flags;
    sclass.stvar[sv].stubColor = stubCol;

    if (sclass.stvarsAreDynamic.size() <= sv)
      sclass.stvarsAreDynamic.resize(sv + 1);
    sclass.stvarsAreDynamic[sv] = code.vars[v].dynamic;

    const bool expectingInt = t == SHVT_INT || t == SHVT_INT4;
    Color4 val = expectingInt ? Color4{bitwise_cast<float>(0), bitwise_cast<float>(0), bitwise_cast<float>(0), bitwise_cast<float>(1)}
                              : Color4{0, 0, 0, 1};
    if (s.init && s.init->expr)
    {
      shexpr::ValueType expectedValType = shexpr::VT_UNDEFINED;
      if (t == SHVT_REAL || t == SHVT_INT || t == SHVT_TEXTURE)
        expectedValType = shexpr::VT_REAL;
      else if (t == SHVT_COLOR4 || t == SHVT_INT4)
        expectedValType = shexpr::VT_COLOR4;
      else if (t == SHVT_FLOAT4X4 || t == SHVT_FLOAT4x3)
      {
        report_error(parser, s.name, "float4x4/float4x3 default value is not supported");
        return;
      }

      const ExpressionParser exprParser{ctx};
      if (!exprParser.parseConstExpression(*s.init->expr, val, ExpressionParser::Context{expectedValType, expectingInt, s.name}))
      {
        report_error(parser, s.name, "Wrong expression");
        return;
      }
    }

    switch (t)
    {
      case SHVT_COLOR4: sclass.stvar[sv].defval.c4.set(val); break;
      case SHVT_REAL: sclass.stvar[sv].defval.r = val[0]; break;
      case SHVT_INT: sclass.stvar[sv].defval.i = bitwise_cast<int>(val[0]); break;
      case SHVT_INT4:
        sclass.stvar[sv].defval.i4.set(bitwise_cast<int>(val[0]), bitwise_cast<int>(val[1]), bitwise_cast<int>(val[2]),
          bitwise_cast<int>(val[3]));
        break;
      case SHVT_FLOAT4X4:
      case SHVT_FLOAT4x3:
        // default value is not supported
        break;
      case SHVT_TEXTURE:
        if (real2int(val[0]) != 0)
        {
          report_error(parser, s.name, "texture may be inited only with 0");
          return;
        }
        sclass.stvar[sv].defval.texId = unsigned(BAD_TEXTUREID);
        break;
      default: G_ASSERT(0);
    }
  }
  else if (sv >= 0)
  {
    if (sclass.stvar[sv].type != t)
    {
      report_error(parser, s.name, "static var '%s' defined with different type", s.name->text);
      return;
    }
    if (sclass.stvar[sv].additionalFlags != flags)
    {
      report_error(parser, s.name, "static var '%s' defined with different attributes", s.name->text);
      return;
    }
    if ((flags & ShaderClass::VF_HAS_STUB_COLOR) && (sclass.stvar[sv].stubColor != stubCol))
    {
      report_error(parser, s.name, "static var '%s' defined with different stub colors", s.name->text);
      return;
    }
  }

  if (varReferenced)
  {
    int i = append_items(code.stvarmap, 1);
    code.stvarmap[i].v = v;
    code.stvarmap[i].sv = sv;
  }

  if (preshaderSource)
    preshaderSource->staticVarDecls.push_back(&s);

  if (s.init && !s.init->expr)
    eval_init_stat(s.name, *s.init, varReferenced);

  if (hasStubColor)
  {
    int opcode = shaderopcode::makeOp2(SHCOD_TEXTURE_STUBCOL, 0, sv);
    for (int i = 0; i < sclass.shInitCode.size(); i += 2)
      if (sclass.shInitCode[i + 1] == opcode)
      {
        if (sclass.shInitCode[i] != stubCol)
          report_error(parser, s.name, "ambiguous stub color for static texture <%s> used in branching", s.name->text);
        return;
      }

    sclass.shInitCode.push_back(stubCol);
    sclass.shInitCode.push_back(opcode);
  }
}

void GatherVariantLocalVarsCB::eval_init_stat(SHTOK_ident *var, shader_init_value &v, bool is_referenced)
{
  if (ctx.tgtCtx().isPreshaderOnly())
    return;

  int varNameId = ctx.tgtCtx().varNameMap().getVarId(var->text);

  int vi = code.find_var(varNameId);
  if (vi < 0)
  {
    report_error(parser, var, "unknown variable '%s'", var->text);
    return;
  }

  if (v.color)
  {
    if (code.vars[vi].type != SHVT_COLOR4)
    {
      report_error(parser, v.color->color, "can't assign color to %s", ShUtils::shader_var_type_name(code.vars[vi].type));
      return;
    }
    int c;
    switch (v.color->color->num)
    {
      case SHADER_TOKENS::SHTOK_diffuse: c = SHCOD_DIFFUSE; break;
      case SHADER_TOKENS::SHTOK_emissive: c = SHCOD_EMISSIVE; break;
      case SHADER_TOKENS::SHTOK_specular: c = SHCOD_SPECULAR; break;
      case SHADER_TOKENS::SHTOK_ambient: c = SHCOD_AMBIENT; break;
      default: G_ASSERT(0);
    }
    if (is_referenced && !code.vars[vi].dynamic)
    {
      int stVarId = sclass.find_static_var(varNameId);
      if (stVarId < 0)
      {
        report_error(parser, var, "variable <%s> is not static var", var->text);
        return;
      }

      sclass.shInitCode.push_back(stVarId);
      sclass.shInitCode.push_back(shaderopcode::makeOp0(c));
      return;
    }

    code.initcode.push_back(vi);
    code.initcode.push_back(shaderopcode::makeOp0(c));
  }
  else if (v.tex)
  {
    if (code.vars[vi].type != SHVT_TEXTURE)
    {
      report_error(parser, v.tex->tex, "can't assign texture to %s", ShUtils::shader_var_type_name(code.vars[vi].type));
      return;
    }
    int ind = 0;
    if (v.tex->tex_num)
      ind = semutils::int_number(v.tex->tex_num->text);
    else if (v.tex->tex_name && v.tex->tex_name->num == SHADER_TOKENS::SHTOK_diffuse)
      ind = 0;
    else
      G_ASSERT(0);

    code.vars[vi].slot = ind;

    int stVarId = is_referenced ? sclass.find_static_var(varNameId) : -1;
    if (stVarId >= 0 && !code.vars[vi].dynamic)
    {
      int opcode = shaderopcode::makeOp2(SHCOD_TEXTURE, ind, 0);
      for (int i = 0; i < sclass.shInitCode.size(); i += 2)
        if (sclass.shInitCode[i] == stVarId)
        {
          if (sclass.shInitCode[i + 1] != opcode)
            report_error(parser, v.tex->tex, "ambiguous init for static texture <%s> used in branching", var->text);
          return;
        }

      sclass.shInitCode.push_back(stVarId);
      sclass.shInitCode.push_back(opcode);
      return;
    }

    code.initcode.push_back(vi);
    code.initcode.push_back(shaderopcode::makeOp2(SHCOD_TEXTURE, ind, 0));
  }
  else
    G_ASSERT(0);
}

void GatherVariantLocalVarsCB::eval_bool_decl(bool_decl &decl)
{
  if (ctx.shCtx().blockLevel() == ShaderBlockLevel::SHADER)
  {
    G_ASSERT(decl.resolvedNid >= 0);
    G_ASSERT(decl.expr->compiled);
  }
  else
  {
    if (decl.resolvedNid < 0)
      decl.resolvedNid = ctx.tgtCtx().boolVarNameMap().addVarId(decl.name->text);
    compile_bool_expr_cached(*decl.expr, ctx.tgtCtx());
  }
  ctx.localBoolVars().add(decl.resolvedNid, decl.expr, parser, decl.name);
}

void GatherVariantLocalVarsCB::decl_bool_alias(const char *name, const char *base_name)
{
  ctx.localBoolVars().addAlias(name, base_name, parser);
}

void GatherVariantLocalVarsCB::eval_command(shader_directive &s)
{
  if (ctx.tgtCtx().isPreshaderOnly())
    return;
  if (s.command->num == SHADER_TOKENS::SHTOK_dont_render)
    throw GsclStopProcessingException();
}

} // namespace ShaderParser