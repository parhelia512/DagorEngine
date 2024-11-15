// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <EditorCore/ec_startDlg.h>
#include <EditorCore/ec_workspace.h>
#include <EditorCore/ec_application_creator.h>

#include <libTools/util/strUtil.h>

#include <osApiWrappers/dag_direct.h>
#include <debug/dag_debug.h>
#include <util/dag_globDef.h>

#include <propPanel/control/container.h>
#include <winGuiWrapper/wgw_dialogs.h>
#include <direct.h>

using hdpi::_pxScaled;

//==================================================================================================
EditorStartDialog::EditorStartDialog(const char *caption, EditorWorkspace &worksp, const char *wsp_blk, const char *select_wsp) :
  DialogWindow(nullptr, _pxScaled(500), _pxScaled(160), caption), wsp(worksp), blkName(wsp_blk), wspInited(false), startWsp(select_wsp)
{
  setModalBackgroundDimmingEnabled(true);

  ::dd_mkpath(wsp_blk);
  PropPanel::ContainerPropertyControl *_panel = getPanel();
  G_ASSERT(_panel && "No panel in EditorStartDialog");

  Tab<String> _app_list(tmpmem);

  _panel->createCombo(ID_START_DIALOG_COMBO, "Workspaces:", _app_list, -1);
  _panel->createStatic(0, "");
  _panel->createStatic(0, "", false);
  _panel->createButton(ID_START_DIALOG_BUTTON_ADD, "Add", true, false);
  _panel->createButton(ID_START_DIALOG_BUTTON_EDIT, "Edit", true, false);

  initWsp(true);
}


//==================================================================================================
EditorStartDialog::~EditorStartDialog() {}


//==================================================================================================
void EditorStartDialog::editWorkspace()
{
  initWsp(false);

  wspInited = true;

  onEditWorkspace();
}


//==================================================================================================
void EditorStartDialog::onChangeWorkspace(const char *name)
{
  if (!name || !*name)
    return;

  bool appDirSet = false;
  if (wsp.load(name, &appDirSet))
  {
    if (appDirSet)
      wsp.save();
  }
  else
  {
    debug("Errors while loading workspace \"%s\"", name);
    wingw::message_box(0, "Workspace error",
      "Errors while loading workspace \n"
      "Some workspace settings may be wrong.");
  }
}


//==================================================================================================
void EditorStartDialog::initWsp(bool init_combo)
{
  wsp.initWorkspaceBlk(blkName);

  Tab<String> wspNames(tmpmem);

  wsp.getWspNames(wspNames);

  PropPanel::ContainerPropertyControl *_panel = getPanel();
  G_ASSERT(_panel && "No panel in EditorStartDialog");

  int i;
  if (init_combo)
  {
    _panel->setStrings(ID_START_DIALOG_COMBO, wspNames);
  }

  if (startWsp.length())
  {
    bool wsp_found = false;
    for (i = 0; i < wspNames.size(); ++i)
      if (!::stricmp(startWsp, wspNames[i]))
      {
        onChangeWorkspace(startWsp);

        if (init_combo)
          _panel->setInt(ID_START_DIALOG_COMBO, i);
        wsp_found = true;
        break;
      }
    if (!wsp_found)
    {
      wingw::message_box(0, "Workspace not found",
        String(0,
          "Workspace \"%s\" not found in local settings\n"
          "You have to init it with proper application.blk",
          startWsp));
      onAddWorkspace();
    }
  }

  if (init_combo)
  {
    if (_panel->getInt(ID_START_DIALOG_COMBO) < 0 && wspNames.size())
    {
      onChangeWorkspace(wspNames[0]);
      _panel->setInt(ID_START_DIALOG_COMBO, 0);
    }
  }
}


//==================================================================================================
void EditorStartDialog::reloadWsp()
{
  wsp.initWorkspaceBlk(blkName);

  Tab<String> wspNames(tmpmem);

  wsp.getWspNames(wspNames);

  PropPanel::ContainerPropertyControl *_panel = getPanel();
  G_ASSERT(_panel && "No panel in EditorStartDialog");

  int i;
  _panel->setStrings(ID_START_DIALOG_COMBO, wspNames);

  for (i = 0; i < wspNames.size(); ++i)
  {
    const char *wspName = wsp.getName();

    if (wspName && !::stricmp(wspName, wspNames[i]))
    {
      onChangeWorkspace(wspName);

      _panel->setInt(ID_START_DIALOG_COMBO, i);
      break;
    }
  }
}


//==================================================================================================
void EditorStartDialog::onAddWorkspace()
{
  editWsp = false;

  WorkspaceDialog dlg(this, "Create new workspace", wsp, editWsp);
  if (dlg.showDialog() == PropPanel::DIALOG_ID_OK)
    reloadWsp();
}


//==================================================================================================
void EditorStartDialog::onEditWorkspace()
{
  editWsp = true;

  WorkspaceDialog dlg(this, "Edit workspace", wsp, editWsp);
  if (dlg.showDialog() == PropPanel::DIALOG_ID_OK)
    reloadWsp();
}


//==================================================================================================

void EditorStartDialog::onChange(int pcb_id, PropPanel::ContainerPropertyControl *panel)
{
  switch (pcb_id)
  {
    case ID_START_DIALOG_COMBO:
    {
      int _sel = panel->getInt(ID_START_DIALOG_COMBO);
      if (_sel > -1)
        onChangeWorkspace(panel->getText(ID_START_DIALOG_COMBO).str());
      break;
    }
  }
}


void EditorStartDialog::onClick(int pcb_id, PropPanel::ContainerPropertyControl *panel)
{
  switch (pcb_id)
  {
    case ID_START_DIALOG_BUTTON_ADD: onAddWorkspace(); break;

    case ID_START_DIALOG_BUTTON_EDIT: onEditWorkspace(); break;
  }
}

//==================================================================================================

bool EditorStartDialog::onOk()
{
  PropPanel::ContainerPropertyControl *_panel = getPanel();
  G_ASSERT(_panel && "No panel in EditorStartDialog");

  SimpleString wspName(_panel->getText(ID_START_DIALOG_COMBO));

  if (wspName.empty())
    return false;

  onChangeWorkspace(wspName.str());

  return true;
}


//==================================================================================================

// Workspace dialog

//==================================================================================================

WorkspaceDialog::WorkspaceDialog(EditorStartDialog *esd, const char *caption, EditorWorkspace &wsp, bool is_editing) :

  DialogWindow(nullptr, _pxScaled(400), _pxScaled(245 - (is_editing ? 35 : 0)), caption), mEditing(is_editing), mWsp(wsp), mEsd(esd)
{
  setModalBackgroundDimmingEnabled(true);

  PropPanel::ContainerPropertyControl *_panel = getPanel();
  G_ASSERT(_panel && "No panel in EditorStartDialog");

  PropPanel::ContainerPropertyControl *_grp = _panel->createGroup(PID_COMMON_PARAMETERS_GRP, "Common parameters");

  G_ASSERT(_grp);

  _grp->createEditBox(PID_WSP_NAME, "Workspace name:");
  _grp->createFileEditBox(PID_APP_FOLDER, "Path to application BLK:");

  Tab<String> filters(tmpmem);
  filters.push_back() = "Application|application.blk";
  filters.push_back() = "BLK files|*.blk";
  _grp->setStrings(PID_APP_FOLDER, filters);

  if (is_editing)
  {
    _panel->setText(PID_WSP_NAME, wsp.getName());
    _panel->setText(PID_APP_FOLDER, wsp.getAppPath());
  }
  else
  {
    _grp->createButton(PID_NEW_APPLICATION, "Create new application", false);

    char app_fname[DAGOR_MAX_PATH] = "";
    getcwd(app_fname, sizeof(app_fname));
    strcat(app_fname, "/../application.blk");
    dd_simplify_fname_c(app_fname);
    if (dd_file_exist(app_fname))
    {
      _panel->setText(PID_APP_FOLDER, app_fname);
      onChange(PID_APP_FOLDER, _panel);
    }
  }

  if (mEsd)
    mEsd->onCustomFillPanel(*_panel);
}


bool WorkspaceDialog::onOk()
{
  PropPanel::ContainerPropertyControl *_panel = getPanel();
  G_ASSERT(_panel && "No panel in WorkspaceDialog");

  SimpleString wspName(_panel->getText(PID_WSP_NAME));

  bool sameName = false;

  if (!wspName.empty())
  {
    sameName = !::stricmp(wspName.str(), mWsp.getName());

    if (!sameName)
    {
      Tab<String> wspNames(tmpmem);

      mWsp.getWspNames(wspNames);

      for (int i = 0; i < wspNames.size(); ++i)
        if (!::stricmp(wspNames[i], wspName.str()))
        {
          wingw::message_box(0, "Workspace error", "Workspace already exists.");

          return false;
        }
    }
  }
  else
  {
    wingw::message_box(0, "Workspace error", "You have to specify workspace name.");
    return false;
  }

  SimpleString appPath(_panel->getText(PID_APP_FOLDER));

  if (!appPath.empty())
  {
    mWsp.setAppPath(appPath.str());
  }
  else
  {
    wingw::message_box(0, "Workspace error", "You have to specify path to application.blk.");
    return false;
  }

  if ((mEsd) && (!mEsd->onCustomSettings(*_panel)))
    return false;

  if (mEditing && !sameName)
    mWsp.remove();

  mWsp.setName(wspName);
  mWsp.save();

  return true;
}


void WorkspaceDialog::onChange(int pcb_id, PropPanel::ContainerPropertyControl *panel)
{
  if (pcb_id == PID_APP_FOLDER)
  {
    SimpleString appPath(panel->getText(PID_APP_FOLDER));

    if (::dd_file_exist(appPath))
    {
      debug("appPath = %s", appPath.str());

      SimpleString wspName(panel->getText(PID_WSP_NAME));

      if (!wspName.length())
      {
        const char *begin;
        const char *end;

        for (end = appPath.str() + strlen(appPath.str()); end > appPath.str(); --end)
          if (*end == '/' || *end == '\\')
            break;

        if (end > appPath.str())
        {
          for (begin = end - 1; begin > appPath.str(); --begin)
            if (*begin == '/' || *begin == '\\')
              break;

          debug("begin = %s, end = %s", begin, end);

          if (begin + 1 < end)
            panel->setText(PID_WSP_NAME, String::mk_sub_str(begin + 1, end));
        }
      }
    }
  }
}


void WorkspaceDialog::onClick(int pcb_id, PropPanel::ContainerPropertyControl *panel)
{
  if (pcb_id == PID_NEW_APPLICATION)
  {
    ApplicationCreator creator(mWsp);

    if (creator.showDialog() == PropPanel::DIALOG_ID_OK)
    {
      panel->setText(PID_WSP_NAME, mWsp.getName());
      panel->setText(PID_APP_FOLDER, mWsp.getAppPath());
    }
  }
}
