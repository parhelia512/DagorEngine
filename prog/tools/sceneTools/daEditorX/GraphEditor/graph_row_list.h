// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

// Selection driving shared by the hand-drawn row lists the canvas raises -- the pin jump menu and
// the add-node popup. Only this half is common: each list paints its own rows.
struct CanvasRowList
{
  // Excludes the scrollbar the child adds once the rows overflow, which the caller's content width
  // does not.
  float rowWidth = 0.0f;
  int hoveredRow = -1;
  bool clicked = false; // a left click landed on hoveredRow, so the caller can treat it as a confirm
};

// Call right after the rows child opens and before painting. row_pitch is the row height plus any
// item spacing between rows. just_opened suppresses the click that a list opened under a resting
// cursor would otherwise see. inout_selected is moved only by a click or a moving cursor, because
// the keyboard owns it otherwise.
CanvasRowList update_canvas_row_list(int row_count, float row_pitch, bool just_opened, int &inout_selected);
