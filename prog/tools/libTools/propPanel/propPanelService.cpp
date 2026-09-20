// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <propPanel/propPanelService.h>
#include <propPanel/imguiWidgetService.h>
#include <propPanel/propPanel.h>

#include <gui/dag_imgui.h>
#include <imgui/imgui.h>

namespace PropPanel
{

namespace
{

class ImguiWidgetServiceImpl final : public IImguiWidgetService
{
public:
  bool searchInput(const void *focus_id, const char *label, const char *hint, String &text_to_search, ImTextureID search_icon,
    ImTextureID clear_icon, bool *input_focused, ImGuiID *input_id, ImRect *input_rect, bool *deactivated_after_edit) override
  {
    return ImguiHelper::searchInput(focus_id, label, hint, text_to_search, search_icon, clear_icon, input_focused, input_id,
      input_rect, deactivated_after_edit);
  }

  bool searchInput(const void *focus_id, const char *label, const char *hint, String &text_to_search, IconId search_icon_id,
    IconId clear_icon_id, bool *input_focused, ImGuiID *input_id, ImRect *input_rect, bool *deactivated_after_edit) override
  {
    return ImguiHelper::searchInput(focus_id, label, hint, text_to_search, search_icon_id, clear_icon_id, input_focused, input_id,
      input_rect, deactivated_after_edit);
  }

  bool imageButtonWithArrow(const char *str_id, ImTextureID texture_id, const ImVec2 &image_size, bool checked,
    ImGuiButtonFlags flags) override
  {
    return ImguiHelper::imageButtonWithArrow(str_id, texture_id, image_size, checked, flags);
  }

  bool imageButtonWithArrow(const char *str_id, IconId icon_id, const ImVec2 &image_size, bool checked,
    ImGuiButtonFlags flags) override
  {
    return ImguiHelper::imageButtonWithArrow(str_id, icon_id, image_size, checked, flags);
  }

  bool imageButtonFrameless(const char *str_id, ImTextureID texture_id, const ImVec2 &image_size, const char *tooltip) override
  {
    return ImguiHelper::imageButtonFrameless(str_id, texture_id, image_size, tooltip);
  }

  bool treeNodeStart(ImGuiID id, ImGuiTreeNodeFlags flags, const char *label, const char *label_end,
    ImguiHelper::TreeNodeWithSpecialHoverBehaviorEndData &end_data, bool allow_blocked_hover) override
  {
    return ImguiHelper::treeNodeWithSpecialHoverBehaviorStart(id, flags, label, label_end, end_data, allow_blocked_hover);
  }

  void treeNodeRender(ImguiHelper::TreeNodeWithSpecialHoverBehaviorEndData &end_data, bool show_arrow) override
  {
    ImguiHelper::treeNodeWithSpecialHoverBehaviorRender(end_data, show_arrow);
  }

  void treeNodeEnd(const ImguiHelper::TreeNodeWithSpecialHoverBehaviorEndData &end_data, const float *label_clip_max_x) override
  {
    ImguiHelper::treeNodeWithSpecialHoverBehaviorEnd(end_data, label_clip_max_x);
  }

  float treeNodeGetLabelClipMaxX() override { return ImguiHelper::treeNodeWithSpecialHoverBehaviorGetLabelClipMaxX(); }
};

ImguiWidgetServiceImpl the_imgui_widget_service;

class PropPanelServiceImpl final : public IPropPanelService
{
public:
  void *getImguiContext() override { return ImGui::GetCurrentContext(); }

  ImTextureID getIconTextureId(const char *icon_name, int size) override
  {
    return get_im_texture_id_from_icon_id(load_icon(icon_name, size));
  }

  IconId loadIcon(const char *icon_name, int size) override { return load_icon(icon_name, size); }

  ImTextureID getImTextureId(IconId icon_id) override { return get_im_texture_id_from_icon_id(icon_id); }

  ImFont *getBoldFont() override { return imgui_get_bold_font(); }
  ImFont *getCustomFont(const char *name) override { return imgui_get_custom_font(name); }

  IImguiWidgetService &getImguiWidgets() override { return the_imgui_widget_service; }
};

PropPanelServiceImpl the_service;

} // namespace

IPropPanelService &get_service() { return the_service; }

} // namespace PropPanel
