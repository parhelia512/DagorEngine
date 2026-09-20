// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

/************************************************************************
  shader assembler
/************************************************************************/

#include "shsem.h"
#include "shsyn.h"
#include "shlexterm.h"
#include "shcode.h"
#include "shSemCode.h"
#include "shVariantContext.h"
#include "variantSemantic.h"
#include "preshaderCompilation.h"
#include "globalConfig.h"
#include "shExprParser.h"
#include "namedConst.h"
#include "nameMap.h"
#include "shErrorReporting.h"
#include "fast_isalnum.h"
#include <dag/dag_vectorMap.h>
#include <dag/dag_vectorSet.h>


namespace ShaderParser
{

class GatherVariantLocalVarsCB : public ShaderEvalCB, public semantic::VariantBoolExprEvalCB
{
public:
  shc::VariantContext &ctx;
  // Cached refs from ctx
  ShaderClass &sclass; // @TODO: this should be const
  ShaderSemCode &code;
  Parser &parser;

  PreshaderCompilationInput *preshaderSource = nullptr; // needed only in derived assembly shader

  explicit GatherVariantLocalVarsCB(shc::VariantContext &ctx_, PreshaderCompilationInput *preshader_source = nullptr) :
    semantic::VariantBoolExprEvalCB{ctx_},
    ctx{ctx_},
    sclass{ctx_.shCtx().compiledShader()},
    code{ctx_.parsedSemCode()},
    parser{ctx_.tgtCtx().sourceParseState().parser},
    preshaderSource{preshader_source}
  {}

  void eval_static(static_var_decl &s) override;
  void eval_interval_decl(interval &interv) override {}
  void eval_bool_decl(bool_decl &) override;
  void decl_bool_alias(const char *name, const char *base_name) override;
  void eval_command(shader_directive &s) override; // don't render handler
  int eval_if(bool_expr &e) override { return semantic::VariantBoolExprEvalCB::eval_expr(e).value ? IF_TRUE : IF_FALSE; }

  void eval_init_stat(SHTOK_ident *var, shader_init_value &v, bool is_referenced);

  void eval_error_stat(error_stat &s) override { report_error(parser, s.message, s.message->text); }
  void eval_channel_decl(channel_decl &, int stream_id = 0) override {}
  void eval_state(state_stat &) override {}
  void eval_zbias_state(zbias_state_stat &) override {}
  void eval_external_block(external_state_block &) override {}
  void eval(immediate_const_block &) override {}
  void eval_supports(supports_stat &) override {}
  void eval_render_stage(render_stage_stat &) override {}
  // Only AssembleShaderEvalCB authors RT pipeline config; other callbacks ignore the block.
  void eval_raytrace_pipeline(raytrace_pipeline_stat &) override {}
  void eval_assume_stat(assume_stat &) override {}
  void eval_assume_if_not_assumed_stat(assume_if_not_assumed_stat &) override {}

  void eval_else(bool_expr &) override {}
  void eval_endif(bool_expr &) override {}

  void eval_shader_locdecl(local_var_decl &s) override {}

  void eval_hlsl_compile(hlsl_compile_class &hlsl_compile) override {}
  void eval_hlsl_decl(hlsl_local_decl_class &hlsl_compile) override {}
};

int appendVarToContext(shc::VariantContext &ctx, const char *name, ShaderVarType type, const int nameId, void *loc, bool dynamic,
  bool noWarning, bool used = false, int slot = -1);
void parseAttribs(shc::VariantContext &ctx, const eastl::vector<static_attrib_decl *> &attribs, const ShaderSemCode::Var &var,
  uint32_t &flags, uint32_t &stubCol);

} // namespace ShaderParser
