// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <dag/dag_vector.h>
#include <imgui/imgui.h>

namespace PropPanel
{
class PropertyControlBase;

// Follows an onChange with an onChangeFinished, so a listener can treat the two as one edit whatever
// kind of control sent it. One finish can close several changes, and a control may also send one with
// no change before it, so it is not one finish per change. Controls that know when their edit ends
// report it themselves, the rest are finished here once they stop holding ImGui's active item.
// A control destroyed before its finish goes out sends none.
class ChangeFinishTracker
{
public:
  void add(PropertyControlBase &control);
  void remove(PropertyControlBase &control);

  void beforeEndFrame();

private:
  dag::Vector<PropertyControlBase *> pendingControls;
};

// Marks `control` as still being edited when the draw just done left ImGui's active item inside it.
// `active_id_was_alive` is ActiveIdIsAlive read before that draw. Every path that draws a control has to
// call this, or the control is finished in the frame its change arrived, in the middle of the edit.
void note_held_active_imgui_item(PropertyControlBase &control, ImGuiID active_id_was_alive);

extern ChangeFinishTracker change_finish_tracker;
} // namespace PropPanel
