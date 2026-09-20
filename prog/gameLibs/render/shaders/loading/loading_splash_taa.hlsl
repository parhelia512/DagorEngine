// TAA contract shared by graph scene nodes and loading_splash_node_taa: the
// scene adds this offset to its pixel position (in its own pixels) and the
// resolve node places the samples with the same offset. The scene's motion
// output is the uv delta from the jittered current sample to the previous
// frame's unjittered projection of the same point: the resolve reads history
// there, and a delta that carries the previous jitter blurs the blend.
#ifndef LOADING_SPLASH_TAA_HLSL
#define LOADING_SPLASH_TAA_HLSL

// Halton (2,3) point in [0, 1) for a 1-based index
float2 loading_splash_halton23(uint i)
{
  float2 r = float2(0, 0);
  float f2 = 0.5, f3 = 1.0 / 3.0;
  uint n = i;
  [loop] while (n > 0u)
  {
    r.x += f2 * float(n & 1u);
    n >>= 1u;
    f2 *= 0.5;
  }
  n = i;
  [loop] while (n > 0u)
  {
    r.y += f3 * float(n % 3u);
    n /= 3u;
    f3 /= 3.0;
  }
  return r;
}

// Sample offset of this frame in scene pixels. ratio = resolve size / scene
// size (1 = plain TAA, a 16-frame Halton cycle). A scene rendered below the
// resolve size walks the output pixels of its block and puts a rotated-grid
// sample in each, so every output pixel gets 4 evenly placed samples per
// cycle; the cycle grows with the block (64 frames at 4x, 256 at 8x).
float2 loading_splash_taa_jitter(float frame, float2 ratio)
{
  int rx = max(int(round(ratio.x)), 1);
  int ry = max(int(round(ratio.y)), 1);
  if (rx == 1 && ry == 1)
    return loading_splash_halton23((uint(frame) & 15u) + 1u) - 0.5;
  uint phases = uint(rx * ry);
  uint i = uint(frame) % (phases * 4u);
  uint ph = i % phases;
  uint sub = i / phases;
  float2 cell = float2(float(ph % uint(rx)), float(ph / uint(rx)));
  float2 rgss = sub == 0u ? float2(-0.125, -0.375) : sub == 1u ? float2(0.375, -0.125) : sub == 2u ? float2(0.125, 0.375) : float2(-0.375, 0.125);
  return (cell + 0.5 + rgss) / float2(rx, ry) - 0.5;
}

#endif
