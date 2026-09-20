// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <frontend/internalRegistry.h>
#include <backend/intermediateRepresentation.h>
#include <backend/nodeStateDeltas.h>


namespace dafg
{

void dump_state_to_json(const InternalRegistry &registry, const intermediate::Graph &ir_graph,
  const sd::NodeStateDeltas &state_deltas);

} // namespace dafg
