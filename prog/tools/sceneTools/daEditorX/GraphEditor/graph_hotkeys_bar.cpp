// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "graph_hotkeys_bar.h"

#include <de3_interface.h>
#include <propPanel/propPanelService.h>
#include <libTools/util/hdpiUtil.h>

#include <imgui/imgui.h>

#include <EASTL/algorithm.h>

namespace
{
constexpr int HOTKEYS_ROW_HEIGHT = 20;      // row / key-cap / mouse-icon height
constexpr int HOTKEYS_MOUSE_ICON_SIZE = 20; // mouse_wheel / mouse_move are 20x20
constexpr int HOTKEYS_ENTRY_GAP = 12;       // gap between adjacent entries
constexpr int HOTKEYS_LABEL_GAP = 4;        // gap between an entry's icon / key-cap and its label
constexpr int HOTKEYS_KEYCAP_PAD_X = 6;     // key-cap inner horizontal padding (each side)
constexpr int HOTKEYS_KEYCAP_ROUNDING = 3;  // key-cap corner radius
constexpr int HOTKEYS_MARGIN = 10;          // inset from the canvas bottom and right edges

constexpr ImU32 HOTKEYS_TEXT_COLOR = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);          // white labels + cap text
constexpr ImU32 HOTKEYS_KEYCAP_BORDER_COLOR = IM_COL32(0x8C, 0x8C, 0x8C, 0xFF); // key-cap outline
constexpr ImU32 HOTKEYS_HINT_COLOR = IM_COL32(0x81, 0xC5, 0xFF, 0xFF);          // a contextual entry, cap and label alike

// A contextual entry draws only while its action is available, and is tinted to say so.
enum class HotkeyHint
{
  Always,
  PinJump,
  AddNode,
  AddTransitNode,
};

struct HotkeyEntry
{
  bool isKeyCap;
  const char *iconName;
  const char *capText;
  const char *label;
  HotkeyHint hint;
};

// The icon name is pasted into the path, so the "dark/" prefix pins these to commonData/icons/dark
// whatever the active theme is -- the bar sits on the canvas, which never goes light.
const HotkeyEntry HOTKEY_ENTRIES[] = {
  {true, nullptr, "Space", "Add node", HotkeyHint::AddNode},
  {true, nullptr, "Shift+Space", "Add transit node", HotkeyHint::AddTransitNode},
  {true, nullptr, "Tab", "Jump to opposite pin", HotkeyHint::PinJump},
  {false, "dark/mouse_wheel", nullptr, "Zoom", HotkeyHint::Always},
  {false, "dark/mouse_move", nullptr, "Pan", HotkeyHint::Always},
  {true, nullptr, "Ctrl+Space", "Zoom and Center", HotkeyHint::Always},
  {true, nullptr, "F1", "Tutorial", HotkeyHint::Always},
};
} // namespace

void draw_graph_hotkeys_bar(const ImVec2 &canvas_max, const GraphHotkeyContext &context)
{
  ImDrawList *dl = ImGui::GetWindowDrawList();

  const float rowH = static_cast<float>(hdpi::_pxS(HOTKEYS_ROW_HEIGHT));
  const float iconSize = static_cast<float>(hdpi::_pxS(HOTKEYS_MOUSE_ICON_SIZE));
  const float entryGap = static_cast<float>(hdpi::_pxS(HOTKEYS_ENTRY_GAP));
  const float labelGap = static_cast<float>(hdpi::_pxS(HOTKEYS_LABEL_GAP));
  const float keycapPadX = static_cast<float>(hdpi::_pxS(HOTKEYS_KEYCAP_PAD_X));
  const float rounding = static_cast<float>(hdpi::_pxS(HOTKEYS_KEYCAP_ROUNDING));
  const float margin = static_cast<float>(hdpi::_pxS(HOTKEYS_MARGIN));
  const float borderThickness = static_cast<float>(eastl::max(1, hdpi::_pxS(1))); // never vanish at sub-96 DPI
  const float textH = ImGui::GetTextLineHeight();

  // Lead = the mouse icon (fixed) or the key-cap (text width + padding on both sides).
  auto leadWidth = [&](const HotkeyEntry &e) -> float {
    if (e.isKeyCap)
    {
      return ImGui::CalcTextSize(e.capText).x + keycapPadX * 2.0f;
    }
    return iconSize;
  };

  // Both passes must skip the same entries: the bar is right-aligned from totalWidth, so a filter
  // applied to one loop only would shift every entry.
  auto hidden = [&context](const HotkeyEntry &e) {
    switch (e.hint)
    {
      case HotkeyHint::PinJump: return !context.pinJumpAvailable;
      case HotkeyHint::AddNode: return !context.addNodeAvailable;
      case HotkeyHint::AddTransitNode: return !context.addTransitNodeAvailable;
      default: return false;
    }
  };

  float totalWidth = 0.0f;
  bool first = true;
  for (const HotkeyEntry &e : HOTKEY_ENTRIES)
  {
    if (hidden(e))
    {
      continue;
    }
    if (!first)
    {
      totalWidth += entryGap;
    }
    totalWidth += leadWidth(e) + labelGap + ImGui::CalcTextSize(e.label).x;
    first = false;
  }

  const float originX = canvas_max.x - margin - totalWidth;
  const float rowTop = canvas_max.y - margin - rowH;
  const float textY = rowTop + (rowH - textH) * 0.5f;
  const float iconY = rowTop + (rowH - iconSize) * 0.5f;

  float x = originX;
  first = true;
  for (const HotkeyEntry &e : HOTKEY_ENTRIES)
  {
    if (hidden(e))
    {
      continue;
    }
    if (!first)
    {
      x += entryGap;
    }
    first = false;

    const bool contextual = e.hint != HotkeyHint::Always;
    const ImU32 textColor = contextual ? HOTKEYS_HINT_COLOR : HOTKEYS_TEXT_COLOR;
    if (e.isKeyCap)
    {
      const float capW = ImGui::CalcTextSize(e.capText).x + keycapPadX * 2.0f;
      const ImU32 capBorder = contextual ? HOTKEYS_HINT_COLOR : HOTKEYS_KEYCAP_BORDER_COLOR;
      dl->AddRect(ImVec2(x, rowTop), ImVec2(x + capW, rowTop + rowH), capBorder, rounding, ImDrawFlags_None, borderThickness);
      dl->AddText(ImVec2(x + keycapPadX, textY), textColor, e.capText);
      x += capW;
    }
    else
    {
      const ImTextureID tex = DAEDITOR3.getPropPanelService()->getIconTextureId(e.iconName, hdpi::_pxS(HOTKEYS_MOUSE_ICON_SIZE));
      if (tex)
      {
        dl->AddImage(tex, ImVec2(x, iconY), ImVec2(x + iconSize, iconY + iconSize));
      }
      x += iconSize;
    }

    x += labelGap;
    dl->AddText(ImVec2(x, textY), textColor, e.label);
    x += ImGui::CalcTextSize(e.label).x;
  }
}
