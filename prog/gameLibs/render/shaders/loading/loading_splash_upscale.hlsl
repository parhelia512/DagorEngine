#ifndef LOADING_SPLASH_UPSCALE_INCLUDED
#define LOADING_SPLASH_UPSCALE_INCLUDED 1

#include <tex2d_bicubic.hlsl>

// How a node reads a graph resource the size cap (splash_graph_spec.md section
// 5) left smaller than its own raster. A bilinear tap over a 2x magnification
// is mush: it keeps two fifths of the edge contrast the uncapped render has.
// The sharpening bicubic gets that to a half for four more taps, which at a 4K
// output measured under a twentieth of a millisecond. Where nothing was capped
// the source matches the raster and the plain tap stands, so a composite can
// read every input through this and pay for it only where it buys something.
// 1.0 is the top of compute_bicubic_sharpen_weights' range; it showed no ringing
// on this scene, whose content is fine ripple rather than hard edges.
#define LOADING_SPLASH_UPSCALE_SHARPNESS 1.0

float4 loading_splash_upsample(Texture2D tex, SamplerState tex_samplerstate, float2 uv, float2 out_res)
{
  float2 sz;
  tex.GetDimensions(sz.x, sz.y);
  if (sz.x >= out_res.x)
    return tex2Dlod(tex, float4(uv, 0, 0));
  // The kernel sharpens through negative outer lobes, so a dark texel beside a
  // bright one comes back under zero. The graph carries HDR radiance, where that
  // is not a code value something later clips but a negative the tonemap and the
  // bloom would go on using.
  return max(tex2D_bicubic_sharpen(tex, tex_samplerstate, uv, sz, 1.0 / sz, LOADING_SPLASH_UPSCALE_SHARPNESS), 0.0);
}

#endif
