//
// Dagor Tech 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <propPanel/control/menu.h>
#include <propPanel/messageQueue.h>
#include <dag/dag_vector.h>
#include <util/dag_string.h>

namespace PropPanel
{

// Generally you do not need this use this.
// You are likely looking for create_menu() instead. It can be found in propPanel/control/menu.h.
class Menu : public IMenu, public IDelayedCallbackHandler
{
public:
  Menu();
  ~Menu() override;

  void addItem(unsigned menu_id, unsigned item_id, const char *title) override;
  void addItemWithComment(unsigned menu_id, unsigned item_id, const char *title, const char *comment) override;
  void addSeparator(unsigned menu_id) override;
  void addSubMenu(unsigned menu_id, unsigned submenu_id, const char *title) override;
  void addLabel(unsigned menu_id, const char *title) override;
  void setStyle(const MenuStyle &style) override { menuStyle = style; }

  int getItemCount(unsigned menu_id) override;
  bool isEmpty() const override;

  void clearMenu(unsigned menu_id) override;

  void setEnabledById(unsigned item_id, bool enabled) override;
  void setCheckById(unsigned item_id, bool checked) override;
  void setRadioById(unsigned item_id, unsigned group_first_item_id, unsigned group_last_item_id) override;
  void setCaptionById(unsigned item_id, const char *title = nullptr, const char *shortcut = nullptr) override;

  void setEventHandler(IMenuEventHandler *event_handler) override;
  void click(unsigned item_id) override;

  bool isContextMenu() const override { return false; }

  void *queryInterfacePtr(unsigned huid) override { return nullptr; }

  void updateImgui();

protected:
  enum class MenuItemType
  {
    Button,
    CheckButton,
    RadioButton,
    Separator,
    SubMenu,
    Label,
  };

  class MenuItem
  {
  public:
    MenuItem(unsigned item_id, MenuItemType item_type) : id(item_id), itemType(item_type) {}

    virtual ~MenuItem() { clear_all_ptr_items(children); }

    virtual void *queryInterfacePtr([[maybe_unused]] unsigned huid) { return nullptr; }

    template <class T>
    T *queryInterface()
    {
      return (T *)queryInterfacePtr(T::HUID);
    }

    MenuItem *getMenuItemById(unsigned item_id);

    const char *getTitle() const { return title; }

    void setTitle(const char *new_title)
    {
      G_ASSERT(new_title && *new_title); // It would cause "Cannot have an empty ID at the root of a window." assert in ImGui.
      title = new_title;
    }

    void setTitleAndShortcut(const char *title_with_shortcut);

    virtual bool updateImguiButton(bool is_checked, bool is_bullet, const MenuStyle &style);
    void updateImgui(MenuItem *&clicked_item, const MenuStyle &style);

  private:
    String title;

  public:
    String shortcut;
    bool secondaryIsComment = false; // pins to the label column instead of the trailing edge
    const unsigned id;
    bool enabled = true;
    bool checked = false;
    MenuItemType itemType;

    dag::Vector<MenuItem *> children;
  };

  void addItem(unsigned menu_id, MenuItem &item);

  void onImguiDelayedCallback(void *user_data) override;

  MenuItem *getMenuItemById(unsigned id);

  MenuItem rootMenu;
  MenuStyle menuStyle;
  IMenuEventHandler *eventHandler = nullptr;
};

} // namespace PropPanel