// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "graph_row_list.h"

#include <imgui/imgui.h>

CanvasRowList update_canvas_row_list(int row_count, float row_pitch, bool just_opened, int &inout_selected)
{
  CanvasRowList result;
  result.rowWidth = ImGui::GetContentRegionAvail().x;

  // Resolved off the uniform grid before the paint loop, so a moving cursor can move the selection
  // ahead of the rows being drawn.
  const ImVec2 listMin = ImGui::GetCursorScreenPos();
  if (ImGui::IsWindowHovered() && row_pitch > 0.0f)
  {
    const ImVec2 mouse = ImGui::GetMousePos();
    const int row = static_cast<int>((mouse.y - listMin.y) / row_pitch);
    if (mouse.y >= listMin.y && row >= 0 && row < row_count && mouse.x >= listMin.x && mouse.x < listMin.x + result.rowWidth)
    {
      result.hoveredRow = row;
    }
  }

  if (result.hoveredRow >= 0)
  {
    result.clicked = !just_opened && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
    if (result.clicked || mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)
    {
      inout_selected = result.hoveredRow;
    }
  }

  return result;
}
