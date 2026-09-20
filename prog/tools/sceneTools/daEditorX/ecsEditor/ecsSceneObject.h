// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <daECS/scene/scene.h>
#include <EditorCore/ec_rendEdObject.h>
#include <libTools/util/undo.h>
#include <EASTL/memory.h>
#include <ioSys/dag_dataBlock.h>

static const int CID_ECSSceneObject = 0xDAC17B47u; // ECSSceneObject

class ECSSceneObject : public RenderableEditableObject
{
  using Base = RenderableEditableObject;

public:
  EO_IMPLEMENT_RTTI_EX(CID_ECSSceneObject, RenderableEditableObject)

  // The editable state of the scene record, kept apart so undo can snapshot and restore it as a whole.
  struct Props
  {
    Point3 pivot = Point3::ZERO;
    bool transformable = true;
    ecs::Scene::SceneId parent = ecs::Scene::C_INVALID_SCENE_ID;
    uint32_t order = ecs::Scene::C_INVALID_SCENE_ID;
  };

  explicit ECSSceneObject(ecs::Scene::SceneId scene_id);

  ecs::Scene::SceneId getSceneId() const { return sceneId; }

  Props getProps() const;
  void setProps(const Props &p);

  bool isSelectedByRectangle(IGenViewportWnd *vp, const EcRect &rect) const override;
  bool isSelectedByPointClick(IGenViewportWnd *vp, int x, int y) const override;

  bool setPos(const Point3 &p) override;
  void setWtm(const TMatrix &wtm) override;

  void update(float dt) override;
  void beforeRender() override;
  void render() override;
  void renderTrans() override;

  bool getWorldBox(BBox3 &box) const override;

  bool canTransform() const override;
  bool mayDelete() override;
  bool mayRename() override { return false; }

  void onRemove(ObjectEditor *editor) override;
  void onAdd(ObjectEditor *editor) override;

  void hideObject(bool hide = true) override;
  void lockObject(bool lock = true) override;

  void createEntityObjects(ObjectEditor *editor);

private:
  class UndoPropsChange : public UndoRedoObject
  {
    Ptr<ECSSceneObject> obj;
    ECSSceneObject::Props oldProps, redoProps;

  public:
    UndoPropsChange(ECSSceneObject *o) : obj(o) { oldProps = redoProps = obj->getProps(); }

    void restore(bool save_redo) override;
    void redo() override;

    size_t size() override { return sizeof(*this); }
    UNDO_MERGE_SNAPSHOT_BY_TARGET(0x867A9D86u, obj.get()) // ECSSceneObject_UndoPropsChange
    void accepted() override {}
    void get_description(String &s) override { s = "UndoScenePropsChange"; }

  private:
    // Without this the panel keeps the pre undo values, and the next edit writes them back over the restored ones.
    void refillPanel();
  };

  void updateSceneTransform();
  bool canBeMoved() const;
  bool canEditSceneProps() const;
  bool canChangeSceneParent() const;
  bool canChangeSceneParentTo(ecs::Scene::SceneId potential_parent) const;
  void init();

  void fillProps(PropPanel::ContainerPropertyControl &panel, DClassID for_class_id,
    dag::ConstSpan<RenderableEditableObject *> objects) override;

  void createSceneParentControl(PropPanel::ContainerPropertyControl &panel);

  void onPPChange(int pid, bool edit_finished, PropPanel::ContainerPropertyControl &panel,
    dag::ConstSpan<RenderableEditableObject *> objects) override;

  struct UndoData
  {
    ecs::Scene::SceneId parentId = ecs::Scene::C_INVALID_SCENE_ID;
    eastl::string path;
    uint32_t order = ecs::Scene::C_INVALID_SCENE_ID;
    DataBlock sceneData;
  };

  eastl::unique_ptr<UndoData> undoData;

  ecs::Scene::SceneId sceneId;
  bool addEntities = false;
};
