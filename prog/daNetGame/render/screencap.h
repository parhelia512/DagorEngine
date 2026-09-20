// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <3d/dag_resPtr.h>
#include <EASTL/optional.h>

class Video360;
class DataBlock;
class BaseTexture;
struct CameraSetupPerspPair;
typedef BaseTexture Texture;

namespace screencap
{
enum class ColorSpace
{
  sRGB,
  Linear,
};

void init(const DataBlock *blk);
void term();

void set_comments(const char *comments);
void make_screenshot(const ManagedTex &srgb_frame,
  const char *file_name_override = nullptr,
  bool force_tga = false,
  ColorSpace colorspace = ColorSpace::sRGB,
  const char *additional_postfix = nullptr);
void schedule_screenshot(bool with_gui = false, int sequence_number = -1, const char *name_override = nullptr);
void start_pending_request();

bool is_screenshot_scheduled();

// pre-tonemap linear HDR capture: schedule_hdr_screenshot arms a one-frame
// request, the postfx node serves it via make_hdr_screenshot with the
// frame_for_postfx texture (linear, scene-referred, after TAA/upscale).
// Always written as EXR regardless of screenshots{format:t=}.
void schedule_hdr_screenshot(const char *name_override = nullptr);
bool is_hdr_screenshot_scheduled();
void make_hdr_screenshot(const ManagedTex &linear_frame);

void toggle_avi_writer();

void screenshots_saved();
void update(const ManagedTex &frame, ColorSpace colorspace);

bool should_hide_gui();
bool should_hide_debug();

int subsamples();
int supersamples();
float fixed_act_rate();

eastl::optional<CameraSetupPerspPair> get_camera();

void start_video360(int resolution, int convergence_frames, float fixed_exposure);
void cleanup_video360();
Video360 *get_video360();
}; // namespace screencap
