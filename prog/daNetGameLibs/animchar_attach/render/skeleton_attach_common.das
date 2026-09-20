module skeleton_attach_common shared

require ecs
require DagorMath
require AnimV20
require DagorSystem
require GeomNodeTree
require danetlibs.animchar_attach.main.attach_common


def copy_nodes_with_remap(var node_tree : GeomNodeTree&; att_node_tree : GeomNodeTree; skeleton_remap : UInt16List; base_eid, att_eid : EntityId)
  let nodeTreeCount = node_tree.nodeCount
  let attNodeTreeCount = att_node_tree.nodeCount
  for skelNode in skeleton_remap
    let nodeNo = get_node_no(skelNode)
    let remappedNodeNo = get_remapped_node_no(skelNode)
    if uint(nodeNo) < nodeTreeCount && uint(remappedNodeNo) < attNodeTreeCount
      geomtree_getNodeTm(node_tree, nodeNo) = geomtree_getNodeTm(att_node_tree, remappedNodeNo)
      geomtree_getNodeWtmRel(node_tree, nodeNo) = geomtree_getNodeWtmRel(att_node_tree, remappedNodeNo)
    else
      logerr("Failed to copy nodes {att_eid} {getEntityTemplateName(att_eid)} from parent {base_eid} {getEntityTemplateName(base_eid)}.
       ({nodeNo} < {nodeTreeCount} && {remappedNodeNo} < {attNodeTreeCount}")
      query(att_eid) <| $ [es] (animchar_attach__attachedTo : EntityId;
                                slot_attach__inAttachedList : EntityId)
        logerr("Failed to copy nodes info {animchar_attach__attachedTo} {getEntityTemplateName(animchar_attach__attachedTo)}.
         {slot_attach__inAttachedList} {getEntityTemplateName(slot_attach__inAttachedList)}")
      destroyEntity(att_eid)


def recalc_branches(var node_tree : GeomNodeTree&; skeleton_recalc_branch : UInt8List)
  for nodeNo in skeleton_recalc_branch
    let intNodeNo = int(nodeNo)
    let parentNodeIdx = geomtree_getParentNodeIdx(node_tree, intNodeNo)
    let parentNodeWtm = float3x4(geomtree_getNodeWtmRel(node_tree, parentNodeIdx))
    let curNodeWtm = float3x4(geomtree_getNodeTm(node_tree, intNodeNo))
    geomtree_getNodeWtmRel(node_tree, intNodeNo) = mat44f(parentNodeWtm * curNodeWtm)
    geomtree_calcWtmForBranch(node_tree, intNodeNo)


def skeleton_attached_to(base_eid : EntityId;
                         base_animchar : AnimcharBaseComponent&;
                         animchar__visible : bool;
                         base_attaches_list : EidList;
                         dt : float;
                         base_animchar_bbox : bbox3f;
                         var base_animchar_bsph_precalculated : vec4f&;
                         var base_animchar_attaches_bbox_precalculated : bbox3f?)
  if base_animchar_attaches_bbox_precalculated != null
    *base_animchar_attaches_bbox_precalculated = base_animchar_bbox

  for attachEid in base_attaches_list
    query(attachEid) <| $ [es(trust_access, REQUIRE=animchar_attach__attachedTo)] (skeleton_attach__attached : bool;
                                                                                   skeleton_attach__skeletonRemap : UInt16List;
                                                                                   skeleton_attach__skeletonRecalcBranch : UInt8List;
                                                                                   attaches_list : EidList const?;
                                                                                   animchar_bbox : bbox3f;
                                                                                   var animchar : AnimcharBaseComponent&;
                                                                                   animchar_render : AnimcharRendComponent;
                                                                                   var animchar__usePrecalculatedData : bool&;
                                                                                   var animchar_bsph_precalculated : vec4f&;
                                                                                   var animchar_bbox_precalculated : BBox3&;
                                                                                   var animchar_shadow_cull_bbox_precalculated : BBox3&;
                                                                                   var animchar_node_wtm : AnimcharNodesMat44;
                                                                                   var animchar_render__root_pos : vec4f&;
                                                                                   var animchar_attaches_bbox_precalculated : bbox3f?;
                                                                                   animchar_render__enabled : bool = true)
      if !animchar_render__enabled || !skeleton_attach__attached
        return

      animchar__usePrecalculatedData = true

      assume attNodeTree = *base_animchar.nodeTree
      assume nodeTree = *animchar.nodeTree

      let wtmOfs = geomtree_getWtmOfs(attNodeTree)
      geomtree_setWtmOfs(nodeTree, wtmOfs)

      copy_nodes_with_remap(nodeTree, attNodeTree, skeleton_attach__skeletonRemap, base_eid, attachEid)
      recalc_branches(nodeTree, skeleton_attach__skeletonRecalcBranch)

      if animchar__visible
        animchar_forcePostRecalcWtm(animchar, dt)

      animchar_copy_nodes(animchar, animchar_node_wtm, animchar_render__root_pos)
      animchar_bsph_precalculated = animchar_render_prepareSphere(animchar_render, animchar_node_wtm)
      animchar_bbox_precalculated = animchar_render_calcWorldBox(animchar_render, animchar_node_wtm, true)
      animchar_shadow_cull_bbox_precalculated = animchar_render_calcWorldBox(animchar_render, animchar_node_wtm, false)

      if attaches_list != null
        skeleton_attached_to(base_eid, animchar, animchar__visible, *attaches_list, dt, animchar_bbox, animchar_bsph_precalculated, animchar_attaches_bbox_precalculated)

      let attachDir = animchar_bsph_precalculated.xyz - base_animchar_bsph_precalculated.xyz
      let maxAttachDistance = length(attachDir) + animchar_bsph_precalculated.w
      if maxAttachDistance > base_animchar_bsph_precalculated.w
        base_animchar_bsph_precalculated.w = maxAttachDistance

      if base_animchar_attaches_bbox_precalculated != null
        bbox3f_add_box(*base_animchar_attaches_bbox_precalculated, animchar_bbox)
