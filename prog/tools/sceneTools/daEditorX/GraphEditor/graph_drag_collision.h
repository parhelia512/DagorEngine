// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EASTL/vector.h>

#include <imgui/imgui.h>

struct GraphData;
class GraphCanvasCull;

// Keeps a drag from putting a node's box -- its rect grown by a fixed margin -- over another node's.
// It only tests, never repositions, so nothing can shove a node onto a third one. Pairs already in
// violation when the drag began stay exempt until they separate, so a node buried under an
// existing overlap can still be dragged out of it.
class GraphDragCollision
{
public:
  // Needs the node editor current, GraphCanvasCull::update already run (it reads those rects) and
  // the node pass not yet started: ne::BeginNode draws and hit-tests wherever the node stands then.
  void update(const GraphData &graph, const GraphCanvasCull &cull);

private:
  enum class Phase
  {
    Idle,
    Armed,     // button down, nothing has moved yet
    Tracking,  // policing `dragged`
    Abandoned, // moves an annotation node, so it stays unpoliced to the end of the gesture
  };

  struct ArmedNode
  {
    int nodeId;
    ImVec2 pos;
    ImVec2 docPos; // an undo landing before the drag moves anything shows up as a change here
    bool known;
  };
  struct Dragged
  {
    int nodeId;
    ImVec2 docPos; // graph position when armed; a move undo or reload changes it and re-arms
    ImVec2 lastGood;
    // Nodes this one already violated when armed, sorted. Ids, not indices, so the blocker walk
    // needs no index stability.
    eastl::vector<int> exemptIds;
  };
  struct NodeBox
  {
    ImVec2 rectMin;
    ImVec2 rectMax;
    int nodeId;
  };

  // Also the recovery path: re-arming mid-gesture rebases a drag whose basis a graph edit broke,
  // which keeps it policed. Dropping it instead would let the release commit an untested overlap.
  void arm(const GraphData &graph, const GraphCanvasCull &cull);
  void discover(const GraphData &graph, const GraphCanvasCull &cull);
  void enforce(const GraphData &graph, const GraphCanvasCull &cull);
  void reset();

  bool isDragged(int node_id) const;
  void buildBlockers(const GraphData &graph, const GraphCanvasCull &cull);
  bool violatesAt(const Dragged &d, const ImVec2 &rect_min, const ImVec2 &rect_max) const;
  void seedExemptions(Dragged &d, const ImVec2 &rect_min, const ImVec2 &rect_max) const;
  void dropClearedExemptions(const GraphCanvasCull &cull, Dragged &d, const ImVec2 &rect_min, const ImVec2 &rect_max) const;

  // Positions as of the frame the button went down. Saved graphs hold fractional and negative
  // positions and the editor truncates towards zero, so only an editor-against-editor diff can tell
  // a drag from rounding.
  eastl::vector<ArmedNode> armed;
  eastl::vector<Dragged> dragged; // sorted by node id, so the blocker walk can binary-search it
  // Rebuilt every enforced frame, kept between them for the capacity: what the dragged set is
  // tested against, and the candidate rect of each dragged node in `dragged` order.
  eastl::vector<NodeBox> blockers;
  eastl::vector<NodeBox> candidates;
  Phase phase = Phase::Idle;
};
