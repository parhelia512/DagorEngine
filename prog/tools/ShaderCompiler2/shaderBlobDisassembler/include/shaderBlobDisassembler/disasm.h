// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <shaderBlobUnpack/shaderBlobUnpack.h>
#include <generic/dag_span.h>
#include <EASTL/string.h>

namespace shader_blob_disasm
{

eastl::string disassembleShaderBlob(dag::ConstSpan<uint8_t> bytecode, dag::ConstSpan<uint8_t> metadata);

} // namespace shader_blob_disasm
