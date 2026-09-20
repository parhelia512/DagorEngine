from "%darg/ui_imports.nut" import *
from "math" import PI, min, max, clamp

let cursors = require("samples_prog/_cursors.nut")

// Segmented progress along a closed path: one arc per step up to MAX_STEP_ARCS steps,
// above that a done arc and a rest arc, a single step is one full loop.
const MAX_STEP_ARCS = 20
const PIECE_OVERLAP = 0.5

let countOf = @(v) typeof v == "array" ? v.len() : v

function progressSpans(current, total, gapFrac) {
  let steps = max(countOf(total).tointeger(), 1)
  let done = clamp(countOf(current), 0, steps)
  if (steps == 1)
    return [{ from = 0.0, to = 1.0, done = done >= 1 }]
  if (steps > MAX_STEP_ARCS) {
    if (done == 0 || done == steps)
      return [{ from = 0.0, to = 1.0, done = done == steps }]
    let split = done.tofloat() / steps
    let g = min(gapFrac, split * 0.5, (1.0 - split) * 0.5)
    return [
      { from = g * 0.5, to = split - g * 0.5, done = true }
      { from = split + g * 0.5, to = 1.0 - g * 0.5, done = false }
    ]
  }
  let step = 1.0 / steps
  let g = min(gapFrac, step * 0.4)
  return array(steps).map(@(_, i) { from = i * step + g * 0.5, to = (i + 1) * step - g * 0.5, done = i + 1 <= done })
}

let isFullLoop = @(span) span.to - span.from >= 1.0

function ringCommands(spans, size, lineWidth, doneColor, restColor) {
  let r = 50.0 - 50.0 * lineWidth / size
  let cmds = [[VECTOR_WIDTH, lineWidth], [VECTOR_FILL_COLOR, 0]]
  foreach (isDone in [true, false]) {
    let arcs = spans.filter(@(s) s.done == isDone)
    if (arcs.len() == 0)
      continue
    cmds.append([VECTOR_COLOR, isDone ? doneColor : restColor])
    foreach (s in arcs)
      cmds.append(isFullLoop(s) ? [VECTOR_ELLIPSE, 50, 50, r, r]
        : [VECTOR_SECTOR, 50, 50, r, r, s.from * 360.0 - 90.0, s.to * 360.0 - 90.0])
  }
  return cmds
}

// The frame path runs clockwise from the top center: straight pieces and quarter arcs, in px.
function framePieces(w, h, radius, lineWidth) {
  let x0 = lineWidth * 0.5
  let x1 = w - x0
  let y1 = h - x0
  let rc = max(radius - x0, 0.0)
  let topHalf = x1 - rc - w * 0.5
  let sideV = y1 - x0 - 2 * rc
  let sideH = x1 - x0 - 2 * rc
  let arc = PI * 0.5 * rc
  return [
    { len = topHalf, line = [w * 0.5, x0, x1 - rc, x0] }
    { len = arc, arc = [x1 - rc, x0 + rc, -90.0] }
    { len = sideV, line = [x1, x0 + rc, x1, y1 - rc] }
    { len = arc, arc = [x1 - rc, y1 - rc, 0.0] }
    { len = sideH, line = [x1 - rc, y1, x0 + rc, y1] }
    { len = arc, arc = [x0 + rc, y1 - rc, 90.0] }
    { len = sideV, line = [x0, y1 - rc, x0, x0 + rc] }
    { len = arc, arc = [x0 + rc, x0 + rc, 180.0] }
    { len = topHalf, line = [x0 + rc, x0, w * 0.5, x0] }
  ]
}

let pathLength = @(pieces) pieces.reduce(@(sum, p) sum + p.len, 0.0)

// Adjacent pieces of one span overlap a little, or their antialiased flat ends leave a notch.
function frameSpanCommands(span, pieces, total, w, h, radius, lineWidth) {
  let cmds = []
  let a = span.from * total
  let b = span.to * total
  let rc = max(radius - lineWidth * 0.5, 0.0)
  local start = 0.0
  foreach (p in pieces) {
    let pieceStart = start
    start += p.len
    if (p.len <= 0.0)
      continue
    local ia = max(a, pieceStart)
    local ib = min(b, start)
    if (ib - ia <= 0.01)
      continue
    if (ia > a)
      ia -= PIECE_OVERLAP
    if (ib < b)
      ib += PIECE_OVERLAP
    let t0 = (ia - pieceStart) / p.len
    let t1 = (ib - pieceStart) / p.len
    if ("line" in p) {
      let [px0, py0, px1, py1] = p.line
      cmds.append([VECTOR_LINE,
        (px0 + (px1 - px0) * t0) / w * 100.0, (py0 + (py1 - py0) * t0) / h * 100.0,
        (px0 + (px1 - px0) * t1) / w * 100.0, (py0 + (py1 - py0) * t1) / h * 100.0])
    }
    else {
      let [cx, cy, a0] = p.arc
      cmds.append([VECTOR_SECTOR, cx / w * 100.0, cy / h * 100.0, rc / w * 100.0, rc / h * 100.0,
        a0 + t0 * 90.0, a0 + t1 * 90.0])
    }
  }
  return cmds
}

function frameCommands(spans, w, h, radius, lineWidth, doneColor, restColor) {
  let pieces = framePieces(w, h, radius, lineWidth)
  let total = pathLength(pieces)
  let cmds = [[VECTOR_WIDTH, lineWidth], [VECTOR_FILL_COLOR, 0]]
  foreach (isDone in [true, false]) {
    let parts = spans.filter(@(s) s.done == isDone)
    if (parts.len() == 0)
      continue
    cmds.append([VECTOR_COLOR, isDone ? doneColor : restColor])
    foreach (s in parts)
      cmds.extend(frameSpanCommands(s, pieces, total, w, h, radius, lineWidth))
  }
  return cmds
}

let doneColor = Color(120, 220, 120)
let restColor = Color(90, 90, 90)

function mkProgressRing(current, total, params = {}) {
  let { size = hdpx(32), lineWidth = hdpx(2), gap = hdpx(2), children = null } = params
  let spans = progressSpans(current, total, gap / (PI * (size - lineWidth)))
  return {
    rendObj = ROBJ_VECTOR_CANVAS
    size = [size, size]
    halign = ALIGN_CENTER
    valign = ALIGN_CENTER
    commands = ringCommands(spans, size, lineWidth, doneColor, restColor)
    children
  }
}

function mkProgressFrame(current, total, params = {}) {
  let { size = [hdpx(64), hdpx(24)], radius = hdpx(8), lineWidth = hdpx(2), gap = hdpx(2), children = null } = params
  let [w, h] = size
  let spans = progressSpans(current, total, gap / pathLength(framePieces(w, h, radius, lineWidth)))
  return {
    rendObj = ROBJ_VECTOR_CANVAS
    size
    halign = ALIGN_CENTER
    valign = ALIGN_CENTER
    commands = frameCommands(spans, w, h, radius, lineWidth, doneColor, restColor)
    children
  }
}

let txt = @(text, fontSize = hdpx(14)) { rendObj = ROBJ_TEXT, text, fontSize, color = Color(230, 230, 230) }

let cases = [[0, 1], [1, 1], [1, 2], [0, 3], [2, 3], [3, 3], [2, 5], [5, 8], [7, 12], [7, 20], [20, 20], [7, 21], [450, 1200], [1200, 1200]]

let mkRingRow = @(size, lineWidth) {
  flow = FLOW_HORIZONTAL
  gap = hdpx(16)
  valign = ALIGN_CENTER
  children = cases.map(@(c) {
    flow = FLOW_VERTICAL
    gap = hdpx(4)
    halign = ALIGN_CENTER
    children = [
      mkProgressRing(c[0], c[1], { size, lineWidth, children = txt("S", size * 0.4) })
      txt($"{c[0]}/{c[1]}", hdpx(11))
    ]
  })
}

function mkNumberFrame(cur, total) {
  let text = $"{cur}/{total}"
  let fontSize = hdpx(16)
  let box = calc_str_box({ rendObj = ROBJ_TEXT, text, fontSize })
  let padX = hdpx(10)
  let padY = hdpx(4)
  let h = box[1] + padY * 2
  return mkProgressFrame(cur, total, {
    size = [box[0] + padX * 2, h]
    radius = h * 0.5
    children = txt(text, fontSize)
  })
}

let mkFrameRow = @() {
  flow = FLOW_HORIZONTAL
  gap = hdpx(16)
  valign = ALIGN_CENTER
  children = cases.map(@(c) mkNumberFrame(c[0], c[1]))
}

return {
  rendObj = ROBJ_SOLID
  color = Color(30, 40, 50)
  cursor = cursors.normal
  size = flex()
  padding = hdpx(30)
  flow = FLOW_VERTICAL
  gap = hdpx(24)
  children = [
    txt("Rings: 1/2 step cases, 2..20 segments, 21+ two arcs", hdpx(16))
    mkRingRow(hdpx(28), hdpx(2))
    mkRingRow(hdpx(40), hdpx(3))
    mkRingRow(hdpx(72), hdpx(5))
    txt("Frames around numbers", hdpx(16))
    mkFrameRow()
    {
      flow = FLOW_HORIZONTAL
      gap = hdpx(16)
      children = [
        mkProgressFrame(2, 3, { size = [hdpx(160), hdpx(60)], radius = hdpx(30), lineWidth = hdpx(4), gap = hdpx(4), children = txt("2/3", hdpx(24)) })
        mkProgressFrame(7, 20, { size = [hdpx(160), hdpx(60)], radius = hdpx(14), lineWidth = hdpx(3), gap = hdpx(3), children = txt("7/20", hdpx(24)) })
        mkProgressFrame(450, 1200, { size = [hdpx(200), hdpx(60)], radius = hdpx(30), lineWidth = hdpx(4), gap = hdpx(4), children = txt("450/1200", hdpx(24)) })
        mkProgressRing(2, 3, { size = hdpx(120), lineWidth = hdpx(8), gap = hdpx(6), children = txt("2/3", hdpx(24)) })
      ]
    }
  ]
}
