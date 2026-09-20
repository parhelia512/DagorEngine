// SDR and HDR encodings shared by every per-scene loading_splash pixel shader.
// Contract: postfx_ps computes float4 load in linear rec709 with alpha, then
// picks one of these encoders based on the shader HDR variant.

#ifndef LOADING_SPLASH_TONEMAP_HLSL
#define LOADING_SPLASH_TONEMAP_HLSL

#include <pixelPacking/ColorSpaceUtility.hlsl>

#define LOADING_SPLASH_LINEAR_SCALE 3.f
// Scene peak brightness fed into the HDR10 PQ transfer; matches the WT-era
// shared loading.dshl SCENE_MAX_NITS so hosts on HDR10 keep their prior look.
#define LOADING_SPLASH_SCENE_MAX_NITS 700.f
#ifndef LOADING_SPLASH_WHITE
#define LOADING_SPLASH_WHITE 8.f
#endif

float3 loading_splash_uncharted2(float3 x)
{
  float A = 0.15;
  float B = 0.50;
  float C = 0.10;
  float D = 0.20;
  float E = 0.02;
  float F = 0.30;
  return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 loading_splash_uncharted_main(float3 texColor, float White)
{
  float3 curr = loading_splash_uncharted2(2.0f * texColor);
  float3 whiteScale = 1.0f / loading_splash_uncharted2(float3(White, White, White));
  return ApplySRGBCurve(curr * whiteScale);
}

float3 loading_splash_hdr10_encode(float3 texColor)
{
  float3 linearColor = RemoveSRGBCurve(texColor);
  float3 color2020 = REC709toREC2020(linearColor);
  color2020 *= (LOADING_SPLASH_SCENE_MAX_NITS / 10000.f);
  float3 colorPow = pow(abs(color2020), 0.1593017578);
  return pow((0.8359375 + 18.8515625 * colorPow) / (1. + 18.6875 * colorPow), 78.84375);
}

#endif
