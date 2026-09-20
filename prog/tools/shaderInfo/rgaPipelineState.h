// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <shaderBlobUnpack/shaderBlobUnpack.h>
#include <drv/3d/dag_renderStates.h>
#include <EASTL/string.h>

namespace rga::pso
{

// Text .gpso for 'rga -s dx12 --gpso'. Input layout comes from the VS semantic use mask,
// render target count from the PS output mask; formats are generic as the dump does not know them.
// @TODO: once we have dshl renderpasses, use them for better format specification
// @NOTE: doesn't require a RenderState struct -- dx12 gpso is very limited compared to vulkan one.
bool write_dx12_gpso(const char *fn, const shader_blob::UnpackedShader &vs, const shader_blob::UnpackedShader &ps,
  eastl::string &out_error);

// Serialized root signature blob for 'rga -s dx12 --rs-bin', built with the same
// dxil::decode_graphics_root_signature the DX12 driver uses at runtime.
bool write_dx12_root_signature(const char *fn, const shader_blob::UnpackedShader &vs, const shader_blob::UnpackedShader &ps,
  eastl::string &out_error);

// Json .gpso for 'rga -s vulkan --pso' in the RGA pipeline state schema: pipeline create info
// with states from the dump RenderState, plus the pipeline layout and the descriptor set
// layouts rebuilt from the spirv shader headers.
bool write_vulkan_gpso(const char *fn, const shader_blob::UnpackedShader &vs, const shader_blob::UnpackedShader &ps,
  const shaders::RenderState &render_state, eastl::string &out_error);

} // namespace rga::pso
