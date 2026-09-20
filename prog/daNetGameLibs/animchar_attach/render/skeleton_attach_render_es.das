require ecs
require DagorMath
require AnimV20
require DagorSystem
require DagorMath
require GeomNodeTree
require gameLibs.pufd_events.pufd_events
require danetlibs.animchar_attach.main.attach_common
require danetlibs.animchar_attach.render.skeleton_attach_common




[es(tag=gameClient, after=visual_offset_updater_es, before=(slot_attach_init_tms_es, vehicle_wheels_prepare_es, node_attached_itm, node_attached_pos, start_async_phys_sim_es), parallel_for=4, REQUIRE_NOT=attachedToParent)]
def skeleton_attach_es(info : ParallelUpdateFrameDelayed;
                       eid : EntityId;
                       animchar : AnimcharBaseComponent&;
                       animchar__visible : bool;
                       animchar_bbox : bbox3f;
                       attaches_list : EidList;
                       var animchar_bsph_precalculated : vec4f&;
                       var animchar_bbox_precalculated : BBox3&;
                       var animchar_shadow_cull_bbox_precalculated : BBox3&;
                       var animchar_attaches_bbox_precalculated : bbox3f?;
                       var animchar__usePrecalculatedData : bool&;
                       animchar_render : AnimcharRendComponent;
                       animchar_node_wtm : AnimcharNodesMat44;
                       var attaches_list__additionalUpdateFrames : int?;
                       animchar__updateAttachesWhenNotUpdatable : Tag const?;
                       animchar__updatable = true)
  if !animchar__updatable && animchar__updateAttachesWhenNotUpdatable == null && (attaches_list__additionalUpdateFrames == null || *attaches_list__additionalUpdateFrames <= 0)
    return

  if attaches_list__additionalUpdateFrames != null && *attaches_list__additionalUpdateFrames > 0
    *attaches_list__additionalUpdateFrames -= 1

  animchar__usePrecalculatedData = true

  animchar_bsph_precalculated = animchar_render_prepareSphere(animchar_render, animchar_node_wtm)
  animchar_bbox_precalculated = animchar_render_calcWorldBox(animchar_render, animchar_node_wtm, true)
  animchar_shadow_cull_bbox_precalculated = animchar_render_calcWorldBox(animchar_render, animchar_node_wtm, false)
  skeleton_attached_to(eid, animchar, animchar__visible, attaches_list, info.dt, animchar_bbox, animchar_bsph_precalculated, animchar_attaches_bbox_precalculated)

//for independent created attaches
//we update animchars in animchar_non_updatable_es and don't recalculate bsph e.t.c. components
[es(tag=gameClient, track=transform, on_appear, after=animchar_non_updatable_es, REQUIRE_NOT=attachedToParent, REQUIRE=(disableUpdate, attachmentUpdate, transform))]
def skeleton_attach_not_updatable(evt : Event;
                                  eid : EntityId;
                                  animchar : AnimcharBaseComponent&;
                                  animchar__visible : bool;
                                  animchar_bbox : bbox3f;
                                  attaches_list : EidList;
                                  var animchar_bsph_precalculated : vec4f&;
                                  var animchar_bbox_precalculated : BBox3&;
                                  var animchar_shadow_cull_bbox_precalculated : BBox3&;
                                  var animchar_attaches_bbox_precalculated : bbox3f?;
                                  var animchar__usePrecalculatedData : bool&;
                                  animchar_render : AnimcharRendComponent;
                                  animchar_node_wtm : AnimcharNodesMat44)
  animchar__usePrecalculatedData = true

  animchar_bsph_precalculated = animchar_render_prepareSphere(animchar_render, animchar_node_wtm)
  animchar_bbox_precalculated = animchar_render_calcWorldBox(animchar_render, animchar_node_wtm, true)
  animchar_shadow_cull_bbox_precalculated = animchar_render_calcWorldBox(animchar_render, animchar_node_wtm, false)
  skeleton_attached_to(eid, animchar, animchar__visible, attaches_list, 0.0, animchar_bbox, animchar_bsph_precalculated, animchar_attaches_bbox_precalculated)


[es(tag=gameClient, on_appear, after=(skeleton_attach_init_es, skeleton_attach_reinit_es), REQUIRE=attachedToParent)]
def init_skeleton_attachment_wtm_ofs(evt : Event;
                                     eid : EntityId;
                                     skeleton_attach__skeletonRemap : UInt16List;
                                     skeleton_attach__skeletonRecalcBranch : UInt8List;
                                     animchar_attach__attachedTo : EntityId;
                                     var animchar aka attachment_animchar : AnimcharBaseComponent&)
  query(animchar_attach__attachedTo) <| $ [es] (animchar aka attached_to_animchar : AnimcharBaseComponent)
    assume attNodeTree = *attached_to_animchar.nodeTree
    assume nodeTree = *attachment_animchar.nodeTree

    let wtmOfs = geomtree_getWtmOfs(attNodeTree)
    geomtree_setWtmOfs(nodeTree, wtmOfs)

    copy_nodes_with_remap(nodeTree, attNodeTree, skeleton_attach__skeletonRemap, animchar_attach__attachedTo, eid)
    recalc_branches(nodeTree, skeleton_attach__skeletonRecalcBranch)

    attachment_animchar |> animchar_resetFastPhysWtmOfs(wtmOfs)
    // simulate fastphys for some frames to let fastphys items settle
    let fastPhysSimTimeOnReset = 1.5
    attachment_animchar |> animchar_updateFastPhys(fastPhysSimTimeOnReset)


def save_cur_geom_node_tree(animchar : AnimcharBaseComponent&;
                            var saved_animchar : AnimcharBaseComponent?)
  if saved_animchar != null
    assume nodeTree = *(animchar.nodeTree)
    assume savedNodeTree = *((*saved_animchar).nodeTree)
    let nodeCount = nodeTree.nodeCount
    if savedNodeTree.nodeCount != nodeCount
      logerr("Incorrect animchar res inited for saved animchar")
      return
    for i in range(0, int(nodeCount))
      geomtree_getNodeTm(savedNodeTree, i) = geomtree_getNodeTm(nodeTree, i)
      geomtree_getNodeWtmRel(savedNodeTree, i) = geomtree_getNodeWtmRel(nodeTree, i)


def save_attaches_animchar_tree(attaches_list : EidList const?)
  if attaches_list == null
    return
  for attach in *attaches_list
    query(attach) <| $ [es] (animchar : AnimcharBaseComponent&;
                             attaches_list : EidList const?;
                             var saved_animchar : AnimcharBaseComponent?)
      save_cur_geom_node_tree(animchar, saved_animchar)
      save_attaches_animchar_tree(attaches_list)


[es(tag=gameClient, on_appear, REQUIRE=deadEntity)]
def skeleton_attach_save_on_dead_es(evt : Event;
                                    animchar : AnimcharBaseComponent&;
                                    attaches_list : EidList const?;
                                    var saved_animchar : AnimcharBaseComponent?)
  save_cur_geom_node_tree(animchar, saved_animchar)
  save_attaches_animchar_tree(attaches_list)
  // error("ANIMCHAR SAVED {saved_animchar != null}")

[es(tag=gameClient, on_event=CmdShowHitcamera, REQUIRE_NOT=deadEntity)]
def skeleton_attach_save_on_hit_cam_es(evt : Event;
                                       hitcamera__target : EntityId)
  query(hitcamera__target) <| $ [es] (animchar : AnimcharBaseComponent&;
                                      attaches_list : EidList const?;
                                      var saved_animchar : AnimcharBaseComponent?)
    save_cur_geom_node_tree(animchar, saved_animchar)
    save_attaches_animchar_tree(attaches_list)


[es(tag=gameClient, after=(animated_visible_attachments_act_animchar_es, skeleton_attach_es), parallel_for=4)]
def skeleton_hide_node_attach_es(info : ParallelUpdateFrameDelayed;
                                 var animchar : AnimcharBaseComponent&;
                                 var animchar_node_wtm : AnimcharNodesMat44;
                                 var animchar_render__root_pos : vec4f&;
                                 animchar__scaledWtmNodeIds : IntList;
                                 animchar__scaledWtmScales : Point3List;
                                 animchar__scaledWtmRecalc : BoolList;
                                 animchar__visible : bool = true)
  if !animchar__visible
    return
  assume nodeTree = *animchar.nodeTree
  for nodeId, scale, recalc in animchar__scaledWtmNodeIds, animchar__scaledWtmScales, animchar__scaledWtmRecalc
    var curNodeWtm : float3x4
    nodeTree |> geomtree_getNodeWtmRelScalar(nodeId, curNodeWtm)

    curNodeWtm[0] *= scale.x
    curNodeWtm[1] *= scale.y
    curNodeWtm[2] *= scale.z

    geomtree_setNodeWtmRelScalar(nodeTree, nodeId, curNodeWtm)
    if recalc
      geomtree_calcWtmForBranch(nodeTree, nodeId)

  if length(animchar__scaledWtmNodeIds) > 0
    animchar_copy_nodes(animchar, animchar_node_wtm, animchar_render__root_pos)
