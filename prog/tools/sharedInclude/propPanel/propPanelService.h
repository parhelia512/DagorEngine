//
// Dagor Tech 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

typedef unsigned int ImGuiID;
struct ImFont;
#ifndef ImTextureID
typedef void *ImTextureID;
#endif

namespace PropPanel
{

class IImguiWidgetService;

enum class IconId : int;

// Service that grants access to engine-backed imgui resources owned by the editor host. The host links propPanel
// (where this is implemented) and the engine subsystems propPanel depends on (resource manager, engine/imgui font
// atlas). It exposes the single instance to plugin DLLs via IDaEditor3Engine::getPropPanelService(). Plugins call
// these methods through the vtable, so they do not need to link propPanel themselves -- which would otherwise drag in
// the resource manager and engine/imgui and create split-brain state across the DLL boundary.
// Add methods only at the end: a plugin DLL built against an older copy of this header keeps its vtable slots.
class IPropPanelService
{
public:
  virtual ~IPropPanelService() = default;

  virtual void *getImguiContext() = 0;
  virtual ImTextureID getIconTextureId(const char *icon_name, int size) = 0;

  // size <= 0 loads at ImguiHelper::getFontSizedIconSize, as load_icon does.
  virtual IconId loadIcon(const char *icon_name, int size = 0) = 0;
  virtual ImTextureID getImTextureId(IconId icon_id) = 0;
  virtual ImFont *getBoldFont() = 0;
  virtual IImguiWidgetService &getImguiWidgets() = 0;

  // nullptr when the host registered no font under that name.
  virtual ImFont *getCustomFont(const char *name) = 0;
};

IPropPanelService &get_service();

} // namespace PropPanel
