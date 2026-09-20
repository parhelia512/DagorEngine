#ifndef DAFX_MODFX_RENDER_FUNCS_HLSL
#define DAFX_MODFX_RENDER_FUNCS_HLSL

float dafx_linearize_z(float depth, GlobalData_cref gdata)
{
  return linearize_z(depth, gdata.zn_zfar.zw);
}

half dafx_calc_soft_depth_mask(float sampled_depth, GlobalData_cref gdata, float softness_depth_rcp, float tc_w)
{
  float linearizedDepth = dafx_linearize_z(sampled_depth, gdata);
  float diff = linearizedDepth - tc_w;
  return saturate(softness_depth_rcp * diff);
}

#ifdef DAFX_DEPTH_TEX
float dafx_sample_depth(uint2 tci, GlobalData_cref gdata)
{
#ifdef DAFX_DEPTH_FOR_COLLISION
  uint2 limit = uint2(gdata.depth_size_for_collision.xy);
#else
  tci += gdata.depth_tci_offset.xy;
  uint2 limit = uint2(gdata.depth_size.xy) + gdata.depth_tci_offset.xy;
#endif
  return texelFetch(DAFX_DEPTH_TEX, min(tci, limit - uint2(1, 1)), 0).r; // tc _must_ be clamped, otherwise it causes huge spike on NV 10XX series
}

float dafx_sample_linear_depth(uint2 tci, GlobalData_cref gdata)
{
  return dafx_linearize_z(dafx_sample_depth(tci, gdata), gdata);
}

half dafx_get_depth_base(float2 tc, GlobalData gdata)
{
  return dafx_sample_linear_depth(tc * gdata.depth_size.xy, gdata);
}

half dafx_get_soft_depth_mask(float4 tc, float4 cloud_tc, float softness_depth_rcp, GlobalData_cref gdata)
{
  float depth = dafx_sample_depth(tc.xy * gdata.depth_size.xy, gdata);
  half depthMask = dafx_calc_soft_depth_mask(depth, gdata, softness_depth_rcp, tc.w);

#ifndef DAFXEX_DISABLE_NEARPLANE_FADE
  depthMask *= saturate(tc.w - tc.w*tc.z);
#endif
#ifdef DAFXEX_CLOUD_MASK_ENABLED
  depthMask *= dafx_get_screen_cloud_volume_mask(cloud_tc.xy, cloud_tc.w);
#endif
  return depthMask;
}

half dafx_get_hard_depth_mask(float4 tc, GlobalData gdata)
{
  float depth = dafx_get_depth_base(tc.xy, gdata);
  half depthMask = (depth >= tc.w) ? 1. : 0.;
#ifdef DAFXEX_CLOUD_MASK_ENABLED
  depthMask *= dafx_get_screen_cloud_volume_mask(tc.xy, tc.w);
#endif
  return depthMask;
}
#endif

#endif