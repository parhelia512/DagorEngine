// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <propPanel/control/customControl.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>

class DataBlock;
class GraphDocument;

// The strip a gradient_preview property shows. It stores nothing of its own: the descriptor names
// three sibling curve properties in `background { ref:t= }` (R, G, B) and it paints what they hold.
class CurvePreviewControl final : public PropPanel::ICustomControl
{
public:
  CurvePreviewControl(const GraphDocument &doc, int node_id, const DataBlock *node_desc, const DataBlock *background);

  void customControlUpdate(int id) override;

private:
  void resampleIfSourcesChanged();

  const GraphDocument &doc;
  const int nodeId;
  eastl::vector<eastl::string> refNames;
  eastl::vector<eastl::string> refDefaults; // descriptor value, for a property with nothing stored
  eastl::vector<int> refKinds;              // EditorCurve type, -1 to let it read the kind letter
  eastl::string sampledFrom[3];
  eastl::vector<unsigned> columnColors;
};
