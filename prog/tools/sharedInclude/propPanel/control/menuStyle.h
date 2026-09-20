//
// Dagor Tech 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

namespace PropPanel
{

// Per-menu overrides for the parts of a menu's look ImGuiStyle cannot express; the rest is reachable
// by pushing ImGui style around the render call. Zero, or false, means "as menus drew before" for
// every field, so a default-constructed MenuStyle changes nothing. Values are raw pixels, so a
// DPI-aware caller scales them, as it already must for the style vars -- except the font size, which
// ImGui scales itself.
struct MenuStyle
{
  // Off: labels start at rowPadX instead of past a gutter nothing uses. A checked row then draws no
  // mark: rowPadX is a text pad, too narrow to hold one.
  bool checkmarkColumn = true;

  // A row's leading pad, and only while checkmarkColumn is off -- the gutter carries it otherwise.
  // The trailing pad is ItemSpacing.x on every path.
  float rowPadX = 0.0f;

  // Unscaled, unlike every other field here: ImGui multiplies a pushed font size by FontScaleMain
  // and FontScaleDpi, so a caller that pre-scales gets the factor twice. 0 = the label's font.
  float secondaryFontSizeBase = 0.0f;

  // Right-align against the trailing pad rather than left-aligning in a column sized to the widest
  // entry, which leaves short entries ragged. Comments ignore this and pin to the label column.
  bool rightAlignShortcut = false;

  // A thin chevron instead of ImGui's filled triangle; proportions come from the font.
  bool submenuChevronArrow = false;

  // 0 = ImGuiStyle::ItemSpacing.y. Worth setting when that is carrying a tall row height.
  float separatorSpacingY = 0.0f;
};

inline constexpr MenuStyle DEFAULT_MENU_STYLE{};

} // namespace PropPanel
