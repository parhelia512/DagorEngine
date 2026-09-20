// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <drv/3d/dag_consts.h>
#include <EASTL/string.h>

namespace shader_name_format
{

struct VariantIdentifier
{
  eastl::string shClassName{};
  eastl::string shDumpName{};
  int stVarCode = -1;
  int dynVarCode = -1;
  ShaderStage stage = STAGE_MAX;
};

struct VariantIdentifierRef
{
  eastl::string_view shClassName{}; // required to be '\0' terminated
  eastl::string_view shDumpName{};  // required to be '\0' terminated
  int stVarCode = -1;
  int dynVarCode = -1;
  ShaderStage stage = STAGE_MAX;
};

inline VariantIdentifierRef make_ident_ref(const VariantIdentifier &ident)
{
  return {.shClassName = ident.shClassName,
    .shDumpName = ident.shDumpName,
    .stVarCode = ident.stVarCode,
    .dynVarCode = ident.dynVarCode,
    .stage = ident.stage};
}

struct VariantNameParsingSettings
{
  bool allowNoDynamicVariant = false;
};

struct VariantNameCompilationSettings
{
  bool intervalValuesAsNames = true;
  bool omitDumpName = false;
  bool listAllAliases = false;
  bool minimalNamesOnly = false;
  size_t maxNameLen = SIZE_MAX;
};

} // namespace shader_name_format
