// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <graphEditor/graph_data.h>

#include <EASTL/vector.h>

// Returns true if the proposed edge (output side: elem_a/pin_a; input side: elem_b/pin_b
// OR vice versa, the validator handles either orientation) would be a valid addition to
// gd.edges. Pure: reads gd only and never mutates it.
//
// Mirrors the WebUI graph editor's graphEditor.js isValidConnection: pin role compatibility
// (in/out/any/ctrl with the ctrl_t pairing rule), duplicate / single-connect handling,
// terminal-elimination cycle detection, and iterative type narrowing across the graph
// (per-edge convertibility + typeGroup synchronization).
//
// Muted edges are treated as absent, so muting one frees the types it pinned and opens the loop it
// closed -- mute is meant to be as weak as delete. The cost is that unmuting is not re-checked and
// can therefore reinstate a cycle. Deliberately not guarded here: unmute must always succeed, and
// every edit recompiles, so compile_graph_to_blks reports the loop it left behind through its
// out_message instead of this validator refusing the edit.
//
// elem_a / elem_b are GraphData::Node ids (matching Node::id, not vector positions).
// pin_a / pin_b are indices into the corresponding node's pins[] vector (same convention as
// GraphData::Edge::pinA / pinB).
bool validate_new_edge(const GraphData &gd, int elem_a, int pin_a, int elem_b, int pin_b);

// Role and declared-type compatibility of one pin pair, and nothing else -- for ranking descriptors
// whose node is not in the graph yet, so none of the whole-graph passes above run. validate_new_edge
// still has the last word once the node is inserted.
bool can_pins_ever_connect(const GraphData::Pin &source, const GraphData::Pin &candidate);

// Index of the first pin on candidate_node_id that validate_new_edge accepts against
// (source_node_id, source_pin), or -1. Both nodes must already be in gd. First rather than best:
// descriptor pin order is the author's own, and the JS editor wired the same way.
int find_connectable_pin(const GraphData &gd, int source_node_id, int source_pin, int candidate_node_id);

struct SplicePin
{
  int node = -1;
  int pin = -1;
  int edgeId = -1; // the wire a sink sits on; unset on the feed, which is never the end erased
  bool muted = false;
};

// The wires a node spliced in at (anchor_node, anchor_pin) takes over: `feed` drives the new node,
// every `sinks` entry is driven by it. An Out anchor hands on all of its consumers; an In anchor is
// itself the only one, driven by its own driver. anchor_edge_id narrows that to one wire, -1 takes
// the pin's lot. Ends with no reachable far end are skipped, so `sinks` is empty when the anchor
// holds no usable wire -- there `feed` is the anchor and the splice is a plain connection. The
// picker, the preview and the commit all read this, so none of them can disagree.
struct SpliceEnds
{
  SplicePin feed;
  eastl::vector<SplicePin> sinks;
};

void splice_ends(const GraphData &gd, int anchor_node, int anchor_pin, int anchor_edge_id, SpliceEnds &out);

// Whether a splice here has a wire to take over, which is what makes the gesture worth offering.
// Every surface that offers it asks this one, so the key, the menu row and the hint bar cannot
// disagree about a pin whose only edge ends somewhere the renderer never submitted.
bool has_splice_target(const GraphData &gd, int anchor_node, int anchor_pin, int anchor_edge_id);

// Whether `candidate` can carry the splice `ends` describes: a pin that takes the feed, and an Out
// pin that fits at least one sink. With no sink the splice is a plain connection and only the feed
// is asked. Pin pairs only, so it answers before the node is in the graph -- which is the whole
// point, both for the picker's offer list and for a library drop that has not spawned yet.
// validate_new_edge still has the last word once the node is inserted.
bool can_node_splice(const GraphData &gd, const SpliceEnds &ends, const GraphData::Node &candidate);
