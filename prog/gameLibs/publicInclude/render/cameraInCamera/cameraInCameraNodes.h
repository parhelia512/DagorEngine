//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <EASTL/fixed_vector.h>

#include <render/daFrameGraph/nodeHandle.h>

namespace camera_in_camera
{
inline constexpr const char *LENS_AREA_CAMERA_SOURCE_BLOB = "lens_area_camera_source";

eastl::fixed_vector<dafg::NodeHandle, 2, false> make_camera_nodes();

// root/view0 - always exists; root/view1 - only while camcam is active
dafg::NodeHandle make_view_camera_provider_node(const char *view_ns, const char *src_camera_blob);
} // namespace camera_in_camera
