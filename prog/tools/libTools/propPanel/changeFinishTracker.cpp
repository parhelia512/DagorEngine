// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "changeFinishTracker.h"
#include <propPanel/control/propertyControlBase.h>
#include <imgui/imgui_internal.h>

namespace PropPanel
{
ChangeFinishTracker change_finish_tracker;

// Two tests, because the active id can change within the frame. Same idiom as ImGui::EndGroup uses
// to tell that the active item is inside the group.
void note_held_active_imgui_item(PropertyControlBase &control, ImGuiID active_id_was_alive)
{
  const ImGuiContext &g = *ImGui::GetCurrentContext();
  if (g.ActiveId != 0 && g.ActiveIdIsAlive == g.ActiveId && active_id_was_alive != g.ActiveId)
    control.heldActiveImguiItem();
}

void ChangeFinishTracker::add(PropertyControlBase &control) { pendingControls.push_back(&control); }

void ChangeFinishTracker::remove(PropertyControlBase &control)
{
  for (size_t i = 0; i < pendingControls.size(); ++i)
  {
    if (pendingControls[i] == &control)
    {
      pendingControls.erase(pendingControls.begin() + i);
      return;
    }
  }
}

void ChangeFinishTracker::beforeEndFrame()
{
  // A notification can delete other controls in the list, so it is searched again after each one.
  // Bounded by the starting count, so a notification that starts a new change cannot loop here.
  for (size_t pass = pendingControls.size(); pass > 0; --pass)
  {
    PropertyControlBase *controlToFinish = nullptr;
    for (PropertyControlBase *control : pendingControls)
    {
      if (!control->isEditInProgress())
      {
        controlToFinish = control;
        break;
      }
    }

    if (!controlToFinish)
      return;

    // Removed first, and nothing touches the control after the notification: a handler is free to refill
    // the panel, which destroys it. sendChangeFinishedIfPending() and onWcChangeFinished() both end on
    // that call for the same reason.
    remove(*controlToFinish);
    controlToFinish->sendChangeFinishedIfPending();
  }
}
} // namespace PropPanel
