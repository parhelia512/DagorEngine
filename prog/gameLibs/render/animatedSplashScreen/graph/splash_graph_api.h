// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EASTL/unique_ptr.h>

class BaseTexture;
class DataBlock;
struct ShaderVariableInfo;

// A multi-pass splash scene: <graph_dir>/<scene>.graph.blk lists compute and
// fullscreen nodes plus one output node that draws the graph's composite
// shader into the host's target.
// Schema and contract: samples/loadingSplash/prog/splash_graph_spec.md
class SplashGraph
{
public:
  struct Frame
  {
    float time = 0; // iGlobalTime
    float seed = 0; // iSeed
    // 0 = that state is not entered; a rush needs the slow stamp too
    float slowExitTime = 0;
    float rushExitTime = 0;
    float paperWhiteNits = 200;
    BaseTexture *titleLogo = nullptr; // bound at the output node's t2 when set
    // the target already holds the host's finished frame, so a composite that
    // opens onto the game may reveal it
    bool overGame = false;
  };

  // nullptr when graph_dir is empty or the scene has no graph file (silent);
  // a broken graph logs one error and returns nullptr too. A graph scene has
  // no single-shader form (its composite is a shader family of its own, never
  // loading_splash_<scene>), so the host then draws its fallback scene.
  // output_shader_name is the name the host resolved for this scene,
  // loading_splash_<scene_name> plus its HDR variant suffix; the composite
  // takes that suffix, and any other shape fails the load.
  // blk_present, when given, tells a missing blk (the legacy path) from a
  // broken one (a failure): nullptr comes back for both
  static eastl::unique_ptr<SplashGraph> load(const char *graph_dir, const char *scene_name, const char *output_shader_name,
    bool *blk_present = nullptr);

  // Same, from a blk the caller already holds, for a scene whose file is not
  // readable yet. src_name only names the source in this graph's messages;
  // nullptr labels it as the game's carried copy of that scene.
  static eastl::unique_ptr<SplashGraph> load_from_blk(const DataBlock &blk, const char *src_name, const char *scene_name,
    const char *output_shader_name);
  virtual ~SplashGraph() = default;

  // One frame into the current render target: frame nodes, the output node,
  // then the history swap. A resize or device reset recreates the resources
  // inside. false = a resource failed to create (logged): drop the graph.
  virtual bool draw(const Frame &frame) = 0;

  virtual float slowExitSeconds() const = 0;
  virtual float rushExitSeconds() const = 0;
  // the scene reveals the game through its rush exit, so the host may draw it
  // over the finished frame
  virtual bool opensOntoGame() const = 0;
};

struct SplashExitPacing
{
  float elapsed = -1.f;  // < 0 while loading
  float approach = -1.f; // 0 trigger, 1 arrival, 2 end pose, < 0 loading
  float portal = 0.f;    // 0..1 opening onto the finished frame
};
SplashExitPacing splash_exit_pacing(float t, float slow_at, float rush_at, float slow_seconds, float rush_seconds);

// 0 is the "not entered" sentinel
inline float splash_exit_stamp(float t) { return t > 1e-3f ? t : 1e-3f; }

// The fixed slots of loading_splash_common.dshl are positional: the dshl pins
// register numbers only, and scene shaders read the components by position.
// Both hosts, the graph and the single-shader draw, encode the slots with more
// than one live component here, so the two paths cannot disagree on a slot they
// name alike; c1 (seed) carries one scalar.
namespace splash_slots
{
// c0 = {time, width, height, paper white nits}
void set_time_const(unsigned stage, int reg, float time, int width, int height, float paper_white_nits);
void set_exit_const(unsigned stage, int reg, float slow_at, float rush_at, float slow_seconds, float rush_seconds);
// c3 = {frames drawn since the scene was applied, history valid 0/1, the
// previous frame's time (the current one on the first frame), the target holds
// the host's finished frame 0/1}
void set_frame_const(unsigned stage, int reg, int frame_index, bool history_valid, float prev_time, float time, bool game_is_under);
// s2 = linear clamp, s3 = point clamp; requested per call, the driver caches
void bind_clamp_samplers(unsigned stage, int linear_reg, int point_reg);
// the single-shader draw's share of the map, the same in every host: c3 with
// no history, a zeroed c4 (a scene shader that reads the knobs must see
// zeros), and the clamp pair. A register under 0 is a slot the dump lacks
void bind_single_shader_slots(unsigned stage, int frame_reg, int node_params_reg, int linear_reg, int point_reg, int frame_index,
  float prev_time, float time);
// the register of an optional slot the host resolved by name, -1 when the dump lacks it
int reg_or_none(const ShaderVariableInfo &var);
} // namespace splash_slots
