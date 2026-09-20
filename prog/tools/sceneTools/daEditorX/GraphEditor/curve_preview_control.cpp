// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "curve_preview_control.h"
#include "graph_curve_value.h"
#include "graph_document.h"

#include <ioSys/dag_dataBlock.h>
#include <libTools/util/hdpiUtil.h>
#include <math/dag_mathBase.h>
#include <webui/editorCurves.h>

#include <imgui/imgui.h>

namespace
{
// Same sampling as the JS strip: 182 columns at x = i / 181, 32 pixels tall.
constexpr int PREVIEW_SAMPLES = 182;
constexpr int PREVIEW_WIDTH = 182;
constexpr int PREVIEW_HEIGHT = 32;

// EditorCurve reads the kind from a letter the stored text need not carry, so take it from the
// property's declared type instead.
int editor_curve_type(const DataBlock *prop_desc)
{
  CurveKind kind = CurveKind::Steps;
  if (!prop_desc || !curve_kind_for_prop_type(infer_prop_type(prop_desc), kind))
  {
    return -1;
  }
  switch (kind)
  {
    case CurveKind::Steps: return EditorCurve::STEPS;
    case CurveKind::Linear: return EditorCurve::PIECEWISE_LINEAR;
    case CurveKind::Monotonic: return EditorCurve::PIECEWISE_MONOTONIC;
    case CurveKind::Polynom: return EditorCurve::POLYNOM;
  }
  return -1;
}
} // namespace

CurvePreviewControl::CurvePreviewControl(const GraphDocument &document, int node_id, const DataBlock *node_desc,
  const DataBlock *background) :
  doc(document), nodeId(node_id)
{
  if (!background)
  {
    return;
  }
  for (int i = 0; i < background->paramCount(); ++i)
  {
    if (strcmp(background->getParamName(i), "ref") == 0 && background->getParamType(i) == DataBlock::TYPE_STRING)
    {
      refNames.push_back(eastl::string(background->getStr(i)));
      const DataBlock *refDesc = find_property_desc(node_desc, background->getStr(i));
      refDefaults.push_back(refDesc ? param_as_string(refDesc, "val") : eastl::string());
      refKinds.push_back(editor_curve_type(refDesc));
    }
  }
}

void CurvePreviewControl::resampleIfSourcesChanged()
{
  eastl::string sources[3];
  int kinds[3] = {-1, -1, -1};
  const GraphData::Node *n = find_node_by_id(doc.getGraphData(), nodeId);
  for (int k = 0; k < 3 && k < static_cast<int>(refNames.size()); ++k)
  {
    const eastl::string *live = n ? find_property_value(*n, refNames[k].c_str()) : nullptr;
    sources[k] = live ? *live : refDefaults[k];
    kinds[k] = refKinds[k];
  }

  if (!columnColors.empty() && sources[0] == sampledFrom[0] && sources[1] == sampledFrom[1] && sources[2] == sampledFrom[2])
  {
    return;
  }
  for (int k = 0; k < 3; ++k)
  {
    sampledFrom[k] = sources[k];
  }

  EditorCurve curves[3];
  bool parsed[3] = {false, false, false};
  for (int k = 0; k < 3; ++k)
  {
    parsed[k] = !sources[k].empty() && curves[k].parse(sources[k].c_str(), kinds[k]);
  }

  columnColors.resize(PREVIEW_SAMPLES);
  for (int i = 0; i < PREVIEW_SAMPLES; ++i)
  {
    const float x = static_cast<float>(i) / (PREVIEW_SAMPLES - 1);
    int rgb[3] = {0, 0, 0};
    for (int k = 0; k < 3; ++k)
    {
      // getValue() asserts on a curve that did not parse, and does not clamp; the fill clamps here.
      rgb[k] = parsed[k] ? static_cast<int>(clamp(curves[k].getValue(x), 0.f, 1.f) * 255.f + 0.5f) : 0;
    }
    columnColors[i] = IM_COL32(rgb[0], rgb[1], rgb[2], 255);
  }
}

void CurvePreviewControl::customControlUpdate(int)
{
  resampleIfSourcesChanged();

  const float height = hdpi::_pxS(PREVIEW_HEIGHT);
  const float width = min(static_cast<float>(hdpi::_pxS(PREVIEW_WIDTH)), ImGui::GetContentRegionAvail().x);
  const ImVec2 topLeft = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(width, height));
  if (width <= 0.f)
  {
    return;
  }

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  for (int i = 0; i < PREVIEW_SAMPLES; ++i)
  {
    const float x0 = topLeft.x + (width * i) / PREVIEW_SAMPLES;
    const float x1 = topLeft.x + (width * (i + 1)) / PREVIEW_SAMPLES;
    drawList->AddRectFilled(ImVec2(x0, topLeft.y), ImVec2(x1, topLeft.y + height), columnColors[i]);
  }
  drawList->AddRect(topLeft, ImVec2(topLeft.x + width, topLeft.y + height), IM_COL32(0, 0, 0, 255));
}
