// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <daEditorE/daEditorE.h>
#include <daEditorE/de_objEditor.h>
#include <daEditorE/de_interface.h>
#include <squirrel.h>
#include <sqrat.h>
#include <sqmodules/sqmodules.h>
#include <quirrel/frp/dag_frp.h>

namespace
{
static ObjectEditor **curObjEdRef;
static inline ObjectEditor *objEd() { return *curObjEdRef; }

// Read-only for script; the C++ setters below are the only writers
struct De4Observables
{
  sqfrp::NativeWatched workMode, editMode, gizmoBasisType, gizmoCenterType, editorIsActive, editorFreeCam;
  bool isBound() const { return workMode.graph != nullptr; }
};
static De4Observables de4_obs;

// no-op until a VM binds the module; the node's graph carries the VM
template <typename T>
static void publish(sqfrp::NativeWatched &node, const T &value)
{
  if (node.graph)
    node.setValue(Sqrat::Object(value, node.graph->vm));
}
} // namespace


void register_da_editor4_objed_ptr(ObjectEditor **oe_ptr) { curObjEdRef = oe_ptr; }

static void set_work_mode_sq(const char *mode)
{
  if (objEd())
    objEd()->setWorkMode(mode);
}
static const char *get_work_mode_sq() { return objEd() ? objEd()->getWorkMode() : ""; }

static void set_edit_mode_sq(int mode) { objEd()->setEditMode(mode); }
static int get_edit_mode_sq() { return objEd() ? objEd()->getEditMode() : CM_OBJED_MODE_SELECT; }
static void set_gizmo_basis_type_sq(int basis) { DAEDITOR4.setGizmoBasisType((IDaEditor4Engine::BasisType)basis); }
static int get_gizmo_basis_type_sq() { return DAEDITOR4.getGizmoBasisType(); }

static void set_gizmo_center_type_sq(int center)
{
  DAEDITOR4.setGizmoCenterType((IDaEditor4Engine::CenterType)center);
  if (objEd())
    objEd()->updateGizmo();
}

static int get_gizmo_center_type_sq() { return DAEDITOR4.getGizmoCenterType(); }

void set_point_action_preview_sq(const char *shape, float param)
{
  if (objEd())
    objEd()->setPointActionPreview(shape, param);
}


/// @module daEditorEmbedded

void register_da_editor4_script(SqModules *module_mgr, bool editor_active)
{
  if (!curObjEdRef)
    return;

  HSQUIRRELVM vm = module_mgr->getVM();
  Sqrat::Table daEditor(vm);
  daEditor //
    .Func("setEditMode", &set_edit_mode_sq)
    .SetValue("DE4_MODE_SELECT", CM_OBJED_MODE_SELECT)
    .SetValue("DE4_MODE_MOVE", CM_OBJED_MODE_MOVE)
    .SetValue("DE4_MODE_MOVE_SURF", CM_OBJED_MODE_SURF_MOVE)
    .SetValue("DE4_MODE_ROTATE", CM_OBJED_MODE_ROTATE)
    .SetValue("DE4_MODE_SCALE", CM_OBJED_MODE_SCALE)
    .SetValue("DE4_MODE_POINT_ACTION", CM_OBJED_MODE_POINT_ACTION)
    .SetValue("DE4_CMD_DROP", CM_OBJED_DROP)
    .SetValue("DE4_CMD_DEL", CM_OBJED_DELETE)
    .Func("setWorkMode", &set_work_mode_sq)
    .Func("setPointActionPreview", &set_point_action_preview_sq)
    .SetValue("DE4_BASIS_WORLD", IDaEditor4Engine::BASIS_world)
    .SetValue("DE4_BASIS_LOCAL", IDaEditor4Engine::BASIS_local)
    .SetValue("DE4_BASIS_PARENT", IDaEditor4Engine::BASIS_parent)
    .SetValue("DE4_CENTER_PIVOT", IDaEditor4Engine::CENTER_pivot)
    .SetValue("DE4_CENTER_SELECTION", IDaEditor4Engine::CENTER_sel)
    .Func("setGizmoBasisType", &set_gizmo_basis_type_sq)
    .Func("setGizmoCenterType", &set_gizmo_center_type_sq)
    /**/;

  // No module at all is better than a module without its observables
  sqfrp::ObservablesGraph *graph = sqfrp::ObservablesGraph::get_from_vm(vm);
  G_ASSERTF_RETURN(graph, , "daEditorEmbedded: the VM has no FRP graph");
  G_ASSERTF_RETURN(!de4_obs.isBound(), , "daEditorEmbedded: already bound to a VM; the editor UI runs in one VM at a time");

  de4_obs.workMode.create(graph, Sqrat::Object(get_work_mode_sq(), vm).GetObject());
  de4_obs.editMode.create(graph, Sqrat::Object(SQInteger(get_edit_mode_sq()), vm).GetObject());
  de4_obs.gizmoBasisType.create(graph, Sqrat::Object(SQInteger(get_gizmo_basis_type_sq()), vm).GetObject());
  de4_obs.gizmoCenterType.create(graph, Sqrat::Object(SQInteger(get_gizmo_center_type_sq()), vm).GetObject());
  de4_obs.editorIsActive.create(graph, Sqrat::Object(editor_active, vm).GetObject());
  de4_obs.editorFreeCam.create(graph, Sqrat::Object(DAEDITOR4.isFreeCameraActive(), vm).GetObject());

  daEditor //
    .SetValue("workMode", &de4_obs.workMode)
    .SetValue("editMode", &de4_obs.editMode)
    .SetValue("gizmoBasisType", &de4_obs.gizmoBasisType)
    .SetValue("gizmoCenterType", &de4_obs.gizmoCenterType)
    .SetValue("editorIsActive", &de4_obs.editorIsActive)
    .SetValue("editorFreeCam", &de4_obs.editorFreeCam)
    /**/;

  module_mgr->addNativeModule("daEditorEmbedded", daEditor);
}

void unregister_da_editor4_script(HSQUIRRELVM vm)
{
  if (!de4_obs.isBound() || de4_obs.workMode.graph->vm != vm)
    return;
  de4_obs.workMode.destroy();
  de4_obs.editMode.destroy();
  de4_obs.gizmoBasisType.destroy();
  de4_obs.gizmoCenterType.destroy();
  de4_obs.editorIsActive.destroy();
  de4_obs.editorFreeCam.destroy();
}

void update_gizmo_basis_type_on_toolbar() { publish(de4_obs.gizmoBasisType, SQInteger(DAEDITOR4.getGizmoBasisType())); }

void update_gizmo_center_type_on_toolbar() { publish(de4_obs.gizmoCenterType, SQInteger(DAEDITOR4.getGizmoCenterType())); }

void update_active_state_on_toolbar(bool active) { publish(de4_obs.editorIsActive, active); }

void update_free_camera_state_on_toolbar() { publish(de4_obs.editorFreeCam, DAEDITOR4.isFreeCameraActive()); }

void ObjectEditor::updateToolbarButtons()
{
  publish(de4_obs.workMode, workMode.c_str());
  publish(de4_obs.editMode, SQInteger(editMode));
}
