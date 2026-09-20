// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EASTL/algorithm.h>

#include <imgui/imgui.h>

#include <stddef.h>
#include <string.h>

// The payload is the descriptor's templateUid, not its visible name, so a template renamed in
// base_nodes.blk between sessions still resolves on drop.
constexpr const char *BASE_NODE_DRAG_PAYLOAD = "DAGOR_BASE_NODE";
constexpr size_t BASE_NODE_DRAG_PAYLOAD_SIZE = 128;

// Zero-padded to a fixed size in both directions, so neither end has to trust the other's length.
using BaseNodeDragUid = char[BASE_NODE_DRAG_PAYLOAD_SIZE];

inline void set_base_node_drag_payload(const char *template_uid)
{
  BaseNodeDragUid uid;
  memset(uid, 0, sizeof(uid));
  memcpy(uid, template_uid, eastl::min<size_t>(strlen(template_uid), sizeof(uid) - 1));
  ImGui::SetDragDropPayload(BASE_NODE_DRAG_PAYLOAD, uid, sizeof(uid));
}

inline void read_base_node_drag_payload(const ImGuiPayload &payload, BaseNodeDragUid &out_uid)
{
  memset(out_uid, 0, sizeof(out_uid));
  memcpy(out_uid, payload.Data, eastl::min<int>(payload.DataSize, static_cast<int>(sizeof(out_uid)) - 1));
}
