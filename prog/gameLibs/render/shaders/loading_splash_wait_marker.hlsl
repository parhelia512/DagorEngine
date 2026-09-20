// Rotating "still loading" marker: a ring arc that turns in the bottom-right
// corner, composited over the finished scene color. A scene opts in with
// #define LOADING_SPLASH_WAIT_MARKER before its LOADING_SPLASH_PS_MAIN; the
// macro then applies it after mainImage, in the scene's display-space output
// (before the HDR encode), so it looks the same in every variant.

#ifndef LOADING_SPLASH_WAIT_MARKER_HLSL
#define LOADING_SPLASH_WAIT_MARKER_HLSL

// margin is a fraction of each axis; sizes are fractions of the screen height
// so the marker keeps its proportions at any aspect
#define LOADING_SPLASH_MARKER_INSET 0.06
#define LOADING_SPLASH_MARKER_RADIUS 0.02
#define LOADING_SPLASH_MARKER_RING_HALF_WIDTH 0.0035
#define LOADING_SPLASH_MARKER_TURNS_PER_SEC 0.6
#define LOADING_SPLASH_MARKER_TAIL_TURNS 0.8
// gone this long after the exit begins
#define LOADING_SPLASH_MARKER_FADE_OUT_SEC 0.5

// the slot names are defines
float3 loading_splash_wait_marker(float3 color, float2 frag_coord, float2 resolution, float time, float exit_time)
{
  float fade = exit_time > 0.0 ? 1.0 - saturate((time - exit_time) / LOADING_SPLASH_MARKER_FADE_OUT_SEC) : 1.0;
  float2 center = resolution * (1.0 - LOADING_SPLASH_MARKER_INSET);
  float2 d = frag_coord - center;
  float dist = length(d);
  float r = resolution.y * LOADING_SPLASH_MARKER_RADIUS;
  float halfW = resolution.y * LOADING_SPLASH_MARKER_RING_HALF_WIDTH;
  float haloR = r + halfW * 6.0;
  BRANCH
  if (fade <= 0.0 || dist > haloR)
    return color;

  // soft dark disc under the ring keeps it readable on bright scenes
  float halo = 1.0 - smoothstep(r - halfW * 2.0, haloR, dist);
  float ring = 1.0 - smoothstep(halfW - 0.75, halfW + 0.75, abs(dist - r));
  // screen y points down, so a growing head angle turns clockwise
  float angle = atan2(d.y, d.x);
  float head = time * LOADING_SPLASH_MARKER_TURNS_PER_SEC * 2.0 * PI;
  float behind = frac((head - angle) / (2.0 * PI)); // 0 at the head, 1 just ahead of it
  float arc = 1.0 - smoothstep(0.0, LOADING_SPLASH_MARKER_TAIL_TURNS, behind);
  // the head edge is angular: soften it over one pixel of arc length
  arc *= smoothstep(0.0, 1.0 / (2.0 * PI * r), behind);

  color *= 1.0 - 0.35 * halo * fade;
  return lerp(color, float3(1.0, 1.0, 1.0), ring * arc * fade);
}

#endif
